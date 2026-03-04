/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmOS Ray — paired IPC channels.
 *
 * Design combines:
 *   Fuchsia Zircon: paired endpoints, handle passing, move semantics
 *   iOS/XNU Mach:   QoS propagation, importance donation
 *   Haiku:          team ownership, select, ConditionVariable, KDL, refcount
 *
 * INTEGRATION NOTE: Team struct must have the following field added:
 *   struct list  kosm_ray_list;   // list of RayEndpoints owned by team
 * Initialize with list_init(&team->kosm_ray_list) in Team constructor.
 * Call kosm_ray_delete_owned(team) in team cleanup (alongside delete_owned_ports).
 */


#include <kosm_ray.h>

#include <algorithm>
#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <AutoDeleter.h>

#include <arch/int.h>
#include <heap.h>
#include <kernel.h>
#include <kosm_mutex.h>
#include <Notifications.h>
#include <sem.h>
#include <syscall_restart.h>
#include <team.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <util/list.h>
#include <vm/vm.h>
#include <wait_for_objects.h>


//#define TRACE_KOSM_RAY
#ifdef TRACE_KOSM_RAY
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif

//#define RAY_TRACING


// Locking (follows port.cpp model):
//
// * sRaysLock: Protects the sRays hash table.
// * sTeamListLock[]: Protects Team::kosm_ray_list. Index = team_id % count.
// * RayEndpoint::lock: Protects all members except team_link, hash_link,
//   lock, state, and id (immutable).
// * RayEndpoint::state: Atomic linearization point for creation/deletion.
//
// Peer locking order: always lock lower id first to avoid deadlock.


// #pragma mark - tracing


#if RAY_TRACING
namespace RayTracing {

class Create : public AbstractTraceEntry {
public:
	Create(kosm_ray_id id0, kosm_ray_id id1, team_id owner)
		:
		fID0(id0),
		fID1(id1),
		fOwner(owner)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("ray pair %" B_PRId32 " <-> %" B_PRId32
			" created, owner %" B_PRId32, fID0, fID1, fOwner);
	}

private:
	kosm_ray_id	fID0;
	kosm_ray_id	fID1;
	team_id		fOwner;
};


class Close : public AbstractTraceEntry {
public:
	Close(kosm_ray_id id)
		:
		fID(id)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("ray %" B_PRId32 " closed", fID);
	}

private:
	kosm_ray_id	fID;
};


class Write : public AbstractTraceEntry {
public:
	Write(kosm_ray_id id, size_t dataSize, uint32 handleCount,
		uint8 qos, status_t result)
		:
		fID(id),
		fDataSize(dataSize),
		fHandleCount(handleCount),
		fQoS(qos),
		fResult(result)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("ray %" B_PRId32 " write, data=%zu handles=%" B_PRIu32
			" qos=%d: %s",
			fID, fDataSize, fHandleCount, fQoS, strerror(fResult));
	}

private:
	kosm_ray_id	fID;
	size_t		fDataSize;
	uint32		fHandleCount;
	uint8		fQoS;
	status_t	fResult;
};


class Read : public AbstractTraceEntry {
public:
	Read(kosm_ray_id id, size_t dataSize, uint32 handleCount,
		int32 senderPriority, status_t result)
		:
		fID(id),
		fDataSize(dataSize),
		fHandleCount(handleCount),
		fSenderPriority(senderPriority),
		fResult(result)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("ray %" B_PRId32 " read, data=%zu handles=%" B_PRIu32
			" sender_pri=%" B_PRId32 ": %s",
			fID, fDataSize, fHandleCount, fSenderPriority,
			strerror(fResult));
	}

private:
	kosm_ray_id	fID;
	size_t		fDataSize;
	uint32		fHandleCount;
	int32		fSenderPriority;
	status_t	fResult;
};


class PeerClosed : public AbstractTraceEntry {
public:
	PeerClosed(kosm_ray_id id, kosm_ray_id peerId)
		:
		fID(id),
		fPeerID(peerId)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("ray %" B_PRId32 " peer %" B_PRId32 " closed",
			fID, fPeerID);
	}

private:
	kosm_ray_id	fID;
	kosm_ray_id	fPeerID;
};

}	// namespace RayTracing

#	define T(x) new(std::nothrow) RayTracing::x;
#else
#	define T(x) ;
#endif


namespace {


// ---- Message ----


struct ray_message : DoublyLinkedListLinkImpl<ray_message> {
	size_t				data_size;
	uint32				handle_count;
	uid_t				sender;
	team_id				sender_team;
	int32				sender_priority;
	uint8				qos_class;
	kosm_handle_t		handles[KOSM_RAY_MAX_HANDLES];
	char				data[0];
};

typedef DoublyLinkedList<ray_message> MessageList;


// ---- Endpoint ----


struct RayEndpoint : public KernelReferenceable {
	enum State {
		kUnused = 0,
		kActive,
		kDeleted
	};

	struct list_link	team_link;
	RayEndpoint*		hash_link;
	kosm_ray_id			id;
	team_id				owner;
	mutex				lock;
	int32				state;

	RayEndpoint*		peer;
	BReference<RayEndpoint>	peer_ref;

	MessageList			messages;
	uint32				read_count;
	uint32				max_messages;

	ConditionVariable	read_condition;
	ConditionVariable	write_condition;

	select_info*		select_infos;

	uint8				qos_class;
	bool				peer_closed;

	uint32				total_count;

	RayEndpoint(team_id ownerTeam, uint32 maxMsg)
		:
		owner(ownerTeam),
		state(kUnused),
		peer(NULL),
		read_count(0),
		max_messages(maxMsg),
		select_infos(NULL),
		qos_class(KOSM_QOS_DEFAULT),
		peer_closed(false),
		total_count(0)
	{
		mutex_init(&lock, "kosm_ray");
		read_condition.Init(this, "ray read");
		write_condition.Init(this, "ray write");
	}

	virtual ~RayEndpoint()
	{
		while (ray_message* msg = messages.RemoveHead())
			free(msg);

		mutex_destroy(&lock);
	}
};


// ---- Hash table ----


struct RayHashDefinition {
	typedef kosm_ray_id	KeyType;
	typedef RayEndpoint	ValueType;

	size_t HashKey(kosm_ray_id key) const
	{
		return key;
	}

	size_t Hash(RayEndpoint* value) const
	{
		return HashKey(value->id);
	}

	bool Compare(kosm_ray_id key, RayEndpoint* value) const
	{
		return value->id == key;
	}

	RayEndpoint*& GetLink(RayEndpoint* value) const
	{
		return value->hash_link;
	}
};

typedef BOpenHashTable<RayHashDefinition> RayHashTable;


// ---- Notifications ----


#define RAY_MONITOR		'_Rm_'
#define RAY_ADDED		0x01
#define RAY_REMOVED		0x02

class RayNotificationService : public DefaultNotificationService {
public:
	RayNotificationService()
		:
		DefaultNotificationService("kosm_rays")
	{
	}

	void Notify(uint32 opcode, kosm_ray_id ray)
	{
		char eventBuffer[128];
		KMessage event;
		event.SetTo(eventBuffer, sizeof(eventBuffer), RAY_MONITOR);
		event.AddInt32("event", opcode);
		event.AddInt32("ray", ray);

		DefaultNotificationService::Notify(event, opcode);
	}
};


} // namespace


// ---- Globals ----


static const size_t kTotalSpaceLimit = 64 * 1024 * 1024;

static int32 sMaxRays = 8192;
static int32 sUsedRays;

static RayHashTable sRays;
static kosm_ray_id sNextRayID = 1;
static bool sRaysActive = false;
static rw_lock sRaysLock = RW_LOCK_INITIALIZER("kosm rays");

enum {
	kTeamListLockCount = 8
};

static mutex sTeamListLock[kTeamListLockCount] = {
	MUTEX_INITIALIZER("kosm ray team 1"),
	MUTEX_INITIALIZER("kosm ray team 2"),
	MUTEX_INITIALIZER("kosm ray team 3"),
	MUTEX_INITIALIZER("kosm ray team 4"),
	MUTEX_INITIALIZER("kosm ray team 5"),
	MUTEX_INITIALIZER("kosm ray team 6"),
	MUTEX_INITIALIZER("kosm ray team 7"),
	MUTEX_INITIALIZER("kosm ray team 8")
};

static int32 sTotalSpaceUsed;

static RayNotificationService sNotificationService;

// Bootstrap rays per team.
// TODO: move this into the Team struct (add kosm_bootstrap_ray field).
// Using a static array for now to avoid modifying Team until all KosmOS
// primitives are finalized. Protected by sRaysLock.
static const int32 kMaxTeams = 4096;
static kosm_ray_id sBootstrapRays[kMaxTeams];


// ---- Helpers ----


static void
notify_ray_select_events(RayEndpoint* ep, uint16 events)
{
	if (ep->select_infos)
		notify_select_events_list(ep->select_infos, events);
}


static BReference<RayEndpoint>
get_locked_ray(kosm_ray_id id)
{
	BReference<RayEndpoint> ref;
	{
		ReadLocker locker(sRaysLock);
		ref.SetTo(sRays.Lookup(id));
	}

	if (ref != NULL && ref->state == RayEndpoint::kActive) {
		if (mutex_lock(&ref->lock) != B_OK)
			ref.Unset();
	} else
		ref.Unset();

	return ref;
}


static BReference<RayEndpoint>
get_ray(kosm_ray_id id)
{
	BReference<RayEndpoint> ref;
	ReadLocker locker(sRaysLock);
	ref.SetTo(sRays.Lookup(id));
	return ref;
}


/*!	Lock two endpoints in consistent order (lower id first) to avoid deadlock.
	Both must be active. Returns B_OK with both locked, or error.
*/
static status_t __attribute__((unused))
lock_endpoint_pair(RayEndpoint* a, RayEndpoint* b)
{
	RayEndpoint* first = a->id < b->id ? a : b;
	RayEndpoint* second = a->id < b->id ? b : a;

	status_t status = mutex_lock(&first->lock);
	if (status != B_OK)
		return status;

	if (first->state != RayEndpoint::kActive) {
		mutex_unlock(&first->lock);
		return B_BAD_PORT_ID;
	}

	status = mutex_lock(&second->lock);
	if (status != B_OK) {
		mutex_unlock(&first->lock);
		return status;
	}

	if (second->state != RayEndpoint::kActive) {
		mutex_unlock(&second->lock);
		mutex_unlock(&first->lock);
		return B_BAD_PORT_ID;
	}

	return B_OK;
}


static status_t
delete_ray_logical(RayEndpoint* ep)
{
	for (;;) {
		const int32 oldState = atomic_test_and_set(&ep->state,
			RayEndpoint::kDeleted, RayEndpoint::kActive);

		switch (oldState) {
			case RayEndpoint::kActive:
				return B_OK;
			case RayEndpoint::kDeleted:
				return B_BAD_PORT_ID;
			case RayEndpoint::kUnused:
				continue;
			default:
				panic("kosm_ray: invalid endpoint state!\n");
				return B_ERROR;
		}
	}
}


static void
uninit_ray(RayEndpoint* ep)
{
	MutexLocker locker(ep->lock);

	notify_ray_select_events(ep, B_EVENT_INVALID);
	ep->select_infos = NULL;

	ep->read_condition.NotifyAll(B_BAD_PORT_ID);
	ep->write_condition.NotifyAll(B_BAD_PORT_ID);
	sNotificationService.Notify(RAY_REMOVED, ep->id);
}


/*!	Notify peer that this endpoint is closing.
	Caller must NOT hold ep->lock (we lock peer).
*/
static void
notify_peer_closed(RayEndpoint* ep)
{
	RayEndpoint* peer = ep->peer;
	if (peer == NULL)
		return;

	BReference<RayEndpoint> peerRef = peer;
	MutexLocker peerLocker(peer->lock);

	if (peer->state != RayEndpoint::kActive)
		return;

	peer->peer = NULL;
	peer->peer_ref.Unset();
	peer->peer_closed = true;

	T(PeerClosed(peer->id, ep->id));

	// Wake readers — they can drain remaining messages,
	// then will get KOSM_RAY_PEER_CLOSED_ERROR
	peer->read_condition.NotifyAll();
	peer->write_condition.NotifyAll(KOSM_RAY_PEER_CLOSED_ERROR);

	notify_ray_select_events(peer, B_EVENT_DISCONNECTED);
}


static ray_message*
allocate_ray_message(size_t dataSize, uint32 handleCount)
{
	const size_t size = sizeof(ray_message) + dataSize;

	if (atomic_add(&sTotalSpaceUsed, size) + size > kTotalSpaceLimit) {
		atomic_add(&sTotalSpaceUsed, -size);
		return NULL;
	}

	ray_message* msg = (ray_message*)malloc(size);
	if (msg == NULL) {
		atomic_add(&sTotalSpaceUsed, -size);
		return NULL;
	}

	memset(msg, 0, sizeof(ray_message));
	msg->data_size = dataSize;
	msg->handle_count = handleCount;
	return msg;
}


static void
free_ray_message(ray_message* msg)
{
	const size_t size = sizeof(ray_message) + msg->data_size;
	free(msg);
	atomic_add(&sTotalSpaceUsed, -size);
}


// ---- Handle transfer ----


/*!	Validate that a handle refers to a real kernel object accessible
	by the given team. Returns B_OK if valid.
*/
static status_t
validate_handle(const kosm_handle_t* handle, team_id team)
{
	switch (handle->type) {
		case KOSM_HANDLE_RAY:
		{
			BReference<RayEndpoint> ref = get_ray(handle->id);
			if (ref == NULL || ref->state != RayEndpoint::kActive)
				return KOSM_RAY_INVALID_HANDLE;
			if (ref->owner != team)
				return B_NOT_ALLOWED;
			return B_OK;
		}

		case KOSM_HANDLE_MUTEX:
		{
			// Validate via kosm_mutex info — if we can get info, it exists
			kosm_mutex_info info;
			status_t status = _kosm_get_mutex_info(handle->id, &info,
				sizeof(info));
			if (status != B_OK)
				return KOSM_RAY_INVALID_HANDLE;
			if (info.team != team)
				return B_NOT_ALLOWED;
			return B_OK;
		}

		case KOSM_HANDLE_AREA:
		{
			// Validate via area_info — standard Haiku area for now,
			// kosm_area will replace when ready
			area_info info;
			status_t status = get_area_info(handle->id, &info);
			if (status != B_OK)
				return KOSM_RAY_INVALID_HANDLE;
			if (info.team != team)
				return B_NOT_ALLOWED;
			return B_OK;
		}

		case KOSM_HANDLE_SEM:
		{
			sem_info info;
			status_t status = _get_sem_info(handle->id, &info,
				sizeof(info));
			if (status != B_OK)
				return KOSM_RAY_INVALID_HANDLE;
			if (info.team != team)
				return B_NOT_ALLOWED;
			return B_OK;
		}

		case KOSM_HANDLE_FD:
		{
			// fd transfer not yet supported
			return B_NOT_SUPPORTED;
		}

		default:
			return KOSM_RAY_INVALID_HANDLE;
	}
}


/*!	Move a handle from sender to receiver.
	For move semantics: the handle is removed from the sender's namespace.
	For copy semantics (KOSM_RAY_COPY_HANDLES): sender retains the handle.

	Returns the new handle ID in the receiver's namespace via dst.
*/
static status_t
transfer_handle(const kosm_handle_t* src, kosm_handle_t* dst,
	team_id senderTeam, team_id receiverTeam, bool copyMode)
{
	dst->type = src->type;

	switch (src->type) {
		case KOSM_HANDLE_RAY:
		{
			BReference<RayEndpoint> ref = get_locked_ray(src->id);
			if (ref == NULL)
				return KOSM_RAY_INVALID_HANDLE;
			MutexLocker locker(ref->lock, true);

			if (!copyMode) {
				const uint8 oldLock = senderTeam % kTeamListLockCount;
				const uint8 newLock = receiverTeam % kTeamListLockCount;

				locker.Unlock();

				uint8 firstLock = std::min(oldLock, newLock);
				uint8 secondLock = std::max(oldLock, newLock);

				MutexLocker firstLocker(sTeamListLock[firstLock]);
				MutexLocker secondLocker;
				if (firstLock != secondLock)
					secondLocker.SetTo(sTeamListLock[secondLock], false);

				locker.SetTo(ref->lock, false);
				if (ref->state != RayEndpoint::kActive)
					return KOSM_RAY_INVALID_HANDLE;

				list_remove_link(&ref->team_link);

				Team* newTeam = Team::Get(receiverTeam);
				if (newTeam == NULL)
					return B_BAD_TEAM_ID;
				BReference<Team> teamRef(newTeam, true);

				list_add_item(&newTeam->kosm_ray_list, ref.Get());
				ref->owner = receiverTeam;
			}

			dst->id = src->id;
			return B_OK;
		}

		case KOSM_HANDLE_MUTEX:
		{
			// Mutexes are shared by ID — no ownership transfer needed,
			// both teams access by the same global kosm_mutex_id.
			// In copy mode, nothing changes. In move mode, we could
			// transfer team ownership, but kosm_mutex is global anyway.
			dst->id = src->id;
			return B_OK;
		}

		case KOSM_HANDLE_AREA:
		{
			void* addr;
			if (copyMode) {
				// Clone the area into receiver's address space
				area_id cloned = vm_clone_area(receiverTeam,
					"transferred area", &addr, B_ANY_ADDRESS,
					B_READ_AREA | B_WRITE_AREA | B_KERNEL_READ_AREA
						| B_KERNEL_WRITE_AREA,
					REGION_NO_PRIVATE_MAP, src->id, true);
				if (cloned < 0)
					return cloned;
				dst->id = cloned;
			} else {
				// Move: transfer area to receiver's address space
				area_id newArea = transfer_area(src->id, &addr,
					B_ANY_ADDRESS, receiverTeam, true);
				if (newArea < 0)
					return newArea;
				dst->id = newArea;
			}
			return B_OK;
		}

		case KOSM_HANDLE_SEM:
		{
			// Semaphores, like mutexes, are accessed by global ID.
			// Transfer ownership so receiver team owns the sem.
			if (!copyMode) {
				status_t status = set_sem_owner(src->id, receiverTeam);
				if (status != B_OK)
					return status;
			}
			dst->id = src->id;
			return B_OK;
		}

		case KOSM_HANDLE_FD:
		{
			// File descriptor transfer across teams.
			// Uses the kernel VFS layer to duplicate fd from sender's
			// table into receiver's table, similar to Unix SCM_RIGHTS.
			//
			// NOTE: requires vfs_dup_foreign_fd() — a new VFS function
			// that duplicates an fd from one team's table to another's.
			// Until implemented, fd transfer is not available.
			// TODO: implement vfs_dup_foreign_fd()
			return B_NOT_SUPPORTED;
		}

		default:
			return KOSM_RAY_INVALID_HANDLE;
	}
}


/*!	Reverse a handle transfer — used for rollback on partial failure.
	Moves the handle back from receiver to sender.
*/
static void
rollback_handle(const kosm_handle_t* transferred, const kosm_handle_t* original,
	team_id senderTeam, team_id receiverTeam, bool wasCopy)
{
	switch (transferred->type) {
		case KOSM_HANDLE_RAY:
		{
			if (!wasCopy) {
				// Move it back to sender
				kosm_handle_t back;
				transfer_handle(transferred, &back, receiverTeam,
					senderTeam, false);
			}
			break;
		}

		case KOSM_HANDLE_AREA:
		{
			if (wasCopy) {
				// Delete the clone we created in receiver's address space
				vm_delete_area(receiverTeam, transferred->id, true);
			} else {
				// Move area back to sender (handle is being discarded)
				void* addr;
				transfer_area(transferred->id, &addr,
					B_ANY_ADDRESS, senderTeam, true);
			}
			break;
		}

		case KOSM_HANDLE_SEM:
		{
			if (!wasCopy)
				set_sem_owner(transferred->id, senderTeam);
			break;
		}

		case KOSM_HANDLE_FD:
			// fd transfer not yet implemented, nothing to rollback
			break;

		case KOSM_HANDLE_MUTEX:
			// No-op: mutexes are global, no ownership change
			break;

		default:
			break;
	}
}


// ---- Importance donation ----


/*!	Apply importance donation: boost the reading thread to the priority
	of the message sender if it's higher. This implements Mach-style
	importance donation over IPC.

	Returns the thread's original priority so it can be restored
	after message processing.
*/
static int32
apply_importance_donation(ray_message* msg)
{
	Thread* thread = thread_get_current_thread();
	if (thread == NULL)
		return -1;

	int32 originalPriority = thread->priority;

	if (msg->sender_priority > originalPriority) {
		// Boost: temporarily raise this thread's priority
		// This ensures UI→server IPC doesn't get delayed by
		// the server running at lower priority
		thread->priority = msg->sender_priority;
		TRACE(("kosm_ray: importance donation, thread %" B_PRId32
			" boosted %" B_PRId32 " -> %" B_PRId32 "\n",
			thread->id, originalPriority, msg->sender_priority));
	}

	return originalPriority;
}


/*!	Restore thread priority after processing a donated message.
	Should be called after the message has been consumed.
*/
static void
revert_importance_donation(int32 originalPriority)
{
	if (originalPriority < 0)
		return;

	Thread* thread = thread_get_current_thread();
	if (thread == NULL)
		return;

	if (thread->priority != originalPriority) {
		TRACE(("kosm_ray: importance revert, thread %" B_PRId32
			" restored %" B_PRId32 " -> %" B_PRId32 "\n",
			thread->id, thread->priority, originalPriority));
		thread->priority = originalPriority;
	}
}


// ---- Debugger commands ----


static int
dump_ray_list(int argc, char** argv)
{
	team_id owner = -1;

	if (argc > 2) {
		if (!strcmp(argv[1], "team") || !strcmp(argv[1], "owner"))
			owner = strtoul(argv[2], NULL, 0);
	} else if (argc > 1)
		owner = strtoul(argv[1], NULL, 0);

	kprintf("ray              id   queue  peer   peer-closed  qos  team\n");

	for (RayHashTable::Iterator it = sRays.GetIterator();
		RayEndpoint* ep = it.Next();) {
		if (owner != -1 && ep->owner != owner)
			continue;

		kprintf("%p %8" B_PRId32 " %5" B_PRIu32 "  %5" B_PRId32 "  %s"
			"          %d    %" B_PRId32 "\n",
			ep, ep->id, ep->read_count,
			ep->peer != NULL ? ep->peer->id : -1,
			ep->peer_closed ? "yes" : "no ",
			ep->qos_class, ep->owner);
	}

	return 0;
}


static void
_dump_ray_info(RayEndpoint* ep)
{
	kprintf("RAY ENDPOINT: %p\n", ep);
	kprintf(" id:           %" B_PRId32 "\n", ep->id);
	kprintf(" owner:        %" B_PRId32 "\n", ep->owner);
	kprintf(" state:        %s\n",
		ep->state == RayEndpoint::kActive ? "active" :
		ep->state == RayEndpoint::kDeleted ? "deleted" : "unused");
	kprintf(" peer:         %p (id %" B_PRId32 ")\n",
		ep->peer, ep->peer != NULL ? ep->peer->id : -1);
	kprintf(" peer_closed:  %s\n", ep->peer_closed ? "yes" : "no");
	kprintf(" queue:        %" B_PRIu32 " / %" B_PRIu32 "\n",
		ep->read_count, ep->max_messages);
	kprintf(" total_count:  %" B_PRIu32 "\n", ep->total_count);
	kprintf(" qos_class:    %d\n", ep->qos_class);

	if (!ep->messages.IsEmpty()) {
		kprintf(" messages:\n");
		MessageList::Iterator it = ep->messages.GetIterator();
		while (ray_message* msg = it.Next()) {
			kprintf("  %p  data=%zu  handles=%"  B_PRIu32
				"  qos=%d  sender=%" B_PRId32 "\n",
				msg, msg->data_size, msg->handle_count,
				msg->qos_class, msg->sender_team);
		}
	}

	set_debug_variable("_ray", (addr_t)ep);
	set_debug_variable("_rayID", ep->id);
	set_debug_variable("_owner", ep->owner);
}


static int
dump_ray_info(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc > 2 && !strcmp(argv[1], "address")) {
		_dump_ray_info((RayEndpoint*)parse_expression(argv[2]));
		return 0;
	}

	int32 num = parse_expression(argv[1]);
	RayEndpoint* ep = sRays.Lookup(num);
	if (ep == NULL || ep->state != RayEndpoint::kActive) {
		kprintf("ray %" B_PRId32 " doesn't exist!\n", num);
		return 0;
	}

	_dump_ray_info(ep);
	return 0;
}


// ---- Private kernel API ----


status_t
kosm_ray_init(kernel_args* args)
{
	new(&sRays) RayHashTable;
	if (sRays.Init() != B_OK) {
		panic("Failed to init kosm_ray hash table!");
		return B_NO_MEMORY;
	}

	memset(sBootstrapRays, -1, sizeof(sBootstrapRays));

	add_debugger_command_etc("kosm_rays", &dump_ray_list,
		"List all active kosm_ray endpoints",
		"[ ([ \"team\" | \"owner\" ] <team>) ]\n"
		"Lists all active ray endpoints, optionally filtered by team.\n", 0);
	add_debugger_command_etc("kosm_ray", &dump_ray_info,
		"Dump info about a kosm_ray endpoint",
		"(<id> | \"address\" <address>)\n"
		"Prints info about the specified ray endpoint.\n", 0);

	new(&sNotificationService) RayNotificationService();
	sNotificationService.Register();
	sRaysActive = true;
	return B_OK;
}


status_t
kosm_create_ray_etc(kosm_ray_id* endpoint0, kosm_ray_id* endpoint1,
	uint32 flags, team_id owner)
{
	TRACE(("kosm_create_ray(owner = %" B_PRId32 ")\n", owner));

	if (!sRaysActive)
		return B_NOT_INITIALIZED;

	Team* team = Team::Get(owner);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamRef(team, true);

	uint32 maxMsg = KOSM_RAY_MAX_QUEUE_MESSAGES;

	// Allocate both endpoints
	BReference<RayEndpoint> ep0;
	{
		RayEndpoint* e = new(std::nothrow) RayEndpoint(owner, maxMsg);
		if (e == NULL)
			return B_NO_MEMORY;
		ep0.SetTo(e, true);
	}

	BReference<RayEndpoint> ep1;
	{
		RayEndpoint* e = new(std::nothrow) RayEndpoint(owner, maxMsg);
		if (e == NULL)
			return B_NO_MEMORY;
		ep1.SetTo(e, true);
	}

	// Check limit
	const int32 previouslyUsed = atomic_add(&sUsedRays, 2);
	if (previouslyUsed + 2 > sMaxRays) {
		atomic_add(&sUsedRays, -2);
		return B_NO_MORE_PORTS;
	}

	// Link peers
	ep0->peer = ep1.Get();
	ep0->peer_ref.SetTo(ep1.Get());
	ep1->peer = ep0.Get();
	ep1->peer_ref.SetTo(ep0.Get());

	// Insert into hash table
	{
		WriteLocker locker(sRaysLock);

		do {
			ep0->id = sNextRayID++;
			if (sNextRayID < 0)
				sNextRayID = 1;
		} while (sRays.Lookup(ep0->id) != NULL);

		do {
			ep1->id = sNextRayID++;
			if (sNextRayID < 0)
				sNextRayID = 1;
		} while (sRays.Lookup(ep1->id) != NULL);

		ep0->AcquireReference();
		ep1->AcquireReference();
		sRays.Insert(ep0);
		sRays.Insert(ep1);
	}

	// Insert into team list
	{
		const uint8 lockIndex = owner % kTeamListLockCount;
		MutexLocker teamLocker(sTeamListLock[lockIndex]);

		ep0->AcquireReference();
		ep1->AcquireReference();
		list_add_item(&team->kosm_ray_list, ep0.Get());
		list_add_item(&team->kosm_ray_list, ep1.Get());
	}

	// Activate (linearization point)
	const int32 old0 = atomic_test_and_set(&ep0->state,
		RayEndpoint::kActive, RayEndpoint::kUnused);
	const int32 old1 = atomic_test_and_set(&ep1->state,
		RayEndpoint::kActive, RayEndpoint::kUnused);

	if (old0 != RayEndpoint::kUnused || old1 != RayEndpoint::kUnused)
		panic("kosm_ray: state modified during creation!\n");

	*endpoint0 = ep0->id;
	*endpoint1 = ep1->id;

	T(Create(ep0->id, ep1->id, owner));

	sNotificationService.Notify(RAY_ADDED, ep0->id);
	sNotificationService.Notify(RAY_ADDED, ep1->id);

	TRACE(("kosm_create_ray: created pair %" B_PRId32 " <-> %" B_PRId32 "\n",
		ep0->id, ep1->id));
	return B_OK;
}


status_t
kosm_close_ray_internal(kosm_ray_id id)
{
	TRACE(("kosm_close_ray(%" B_PRId32 ")\n", id));

	if (!sRaysActive || id < 0)
		return B_BAD_PORT_ID;

	BReference<RayEndpoint> ref = get_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;

	status_t status = delete_ray_logical(ref);
	if (status != B_OK)
		return status;

	// Notify peer before removing from data structures
	notify_peer_closed(ref);

	// Disconnect from peer
	{
		MutexLocker locker(ref->lock);
		ref->peer = NULL;
		ref->peer_ref.Unset();
	}

	// Remove from hash table
	{
		WriteLocker locker(sRaysLock);
		sRays.Remove(ref);
		ref->ReleaseReference();
	}

	// Remove from team list
	{
		const uint8 lockIndex = ref->owner % kTeamListLockCount;
		MutexLocker teamLocker(sTeamListLock[lockIndex]);
		list_remove_link(&ref->team_link);
		ref->ReleaseReference();
	}

	uninit_ray(ref);
	atomic_add(&sUsedRays, -1);

	T(Close(id));

	return B_OK;
}


void
kosm_ray_delete_owned(Team* team)
{
	TRACE(("kosm_ray_delete_owned(team = %" B_PRId32 ")\n", team->id));

	// Clear bootstrap ray for this team
	if (team->id >= 0 && team->id < kMaxTeams) {
		WriteLocker bootstrapLocker(sRaysLock);
		sBootstrapRays[team->id] = -1;
	}

	list deletionList;
	list_init_etc(&deletionList, kosm_ray_team_link_offset());

	const uint8 lockIndex = team->id % kTeamListLockCount;
	MutexLocker teamLocker(sTeamListLock[lockIndex]);

	RayEndpoint* ep = (RayEndpoint*)list_get_first_item(&team->kosm_ray_list);
	while (ep != NULL) {
		RayEndpoint* next =
			(RayEndpoint*)list_get_next_item(&team->kosm_ray_list, ep);

		if (delete_ray_logical(ep) == B_OK) {
			list_remove_link(&ep->team_link);
			list_add_item(&deletionList, ep);
		}

		ep = next;
	}

	teamLocker.Unlock();

	// Notify peers
	for (RayEndpoint* ep = (RayEndpoint*)list_get_first_item(&deletionList);
		ep != NULL;
		ep = (RayEndpoint*)list_get_next_item(&deletionList, ep)) {
		notify_peer_closed(ep);
		MutexLocker locker(ep->lock);
		ep->peer = NULL;
		ep->peer_ref.Unset();
	}

	// Remove from hash
	{
		WriteLocker locker(sRaysLock);
		for (RayEndpoint* ep =
			(RayEndpoint*)list_get_first_item(&deletionList);
			ep != NULL;
			ep = (RayEndpoint*)list_get_next_item(&deletionList, ep)) {
			sRays.Remove(ep);
			ep->ReleaseReference();
		}
	}

	// Uninit and release team list references
	while (RayEndpoint* ep =
		(RayEndpoint*)list_remove_head_item(&deletionList)) {
		atomic_add(&sUsedRays, -1);
		uninit_ray(ep);
		ep->ReleaseReference();
	}
}


status_t
kosm_ray_set_bootstrap(team_id team, kosm_ray_id endpoint)
{
	if (team < 0 || team >= kMaxTeams)
		return B_BAD_TEAM_ID;

	WriteLocker locker(sRaysLock);
	sBootstrapRays[team] = endpoint;
	return B_OK;
}


int32
kosm_ray_max(void)
{
	return sMaxRays;
}


int32
kosm_ray_used(void)
{
	return sUsedRays;
}


off_t
kosm_ray_team_link_offset(void)
{
	RayEndpoint* ep = (RayEndpoint*)0;
	return (off_t)&ep->team_link;
}


status_t
kosm_select_ray(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_PORT_ID;

	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	if (ref->state != RayEndpoint::kActive)
		return B_BAD_PORT_ID;

	if (!kernel && ref->owner == team_get_kernel_team_id())
		return B_NOT_ALLOWED;

	info->selected_events &= B_EVENT_READ | B_EVENT_WRITE | B_EVENT_INVALID
		| B_EVENT_DISCONNECTED;

	if (info->selected_events != 0) {
		uint16 events = 0;

		info->next = ref->select_infos;
		ref->select_infos = info;

		if ((info->selected_events & B_EVENT_READ) != 0
			&& !ref->messages.IsEmpty()) {
			events |= B_EVENT_READ;
		}

		if ((info->selected_events & B_EVENT_WRITE) != 0
			&& ref->peer != NULL && !ref->peer_closed) {
			events |= B_EVENT_WRITE;
		}

		if (ref->peer_closed)
			events |= B_EVENT_DISCONNECTED;

		if (events != 0)
			notify_select_events(info, events);
	}

	return B_OK;
}


status_t
kosm_deselect_ray(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_PORT_ID;
	if (info->selected_events == 0)
		return B_OK;

	// Use get_ray() instead of get_locked_ray() so deselect works even
	// when the endpoint state is kDeleted. This prevents stale select_info
	// pointers from remaining in the list after common_wait_for_objects
	// returns.
	BReference<RayEndpoint> ref = get_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock);

	select_info** infoLocation = &ref->select_infos;
	while (*infoLocation != NULL && *infoLocation != info)
		infoLocation = &(*infoLocation)->next;

	if (*infoLocation == info)
		*infoLocation = info->next;

	return B_OK;
}


// ---- Public kernel API ----


status_t
kosm_create_ray(kosm_ray_id* endpoint0, kosm_ray_id* endpoint1, uint32 flags)
{
	return kosm_create_ray_etc(endpoint0, endpoint1, flags,
		team_get_current_team_id());
}


status_t
kosm_close_ray(kosm_ray_id id)
{
	return kosm_close_ray_internal(id);
}


status_t
kosm_ray_write_internal(kosm_ray_id id, const void* data, size_t dataSize,
	const kosm_handle_t* handles, size_t handleCount, uint32 flags,
	bigtime_t timeout, bool hasTimeout, bool userCopy)
{
	TRACE(("kosm_ray_write(%" B_PRId32 ", data=%p, size=%zu, handles=%zu, "
		"flags=0x%" B_PRIx32 ")\n", id, data, dataSize, handleCount, flags));

	if (!sRaysActive || id < 0)
		return B_BAD_PORT_ID;
	if (dataSize > KOSM_RAY_MAX_DATA_SIZE)
		return KOSM_RAY_TOO_LARGE;
	if (handleCount > KOSM_RAY_MAX_HANDLES)
		return KOSM_RAY_TOO_MANY_HANDLES;

	bool copyHandles = (flags & KOSM_RAY_COPY_HANDLES) != 0;

	uint32 waitFlags = flags & (B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT
		| B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT);

	if (hasTimeout && (waitFlags & B_RELATIVE_TIMEOUT) != 0
		&& timeout > 0) {
		waitFlags = (waitFlags & ~B_RELATIVE_TIMEOUT) | B_ABSOLUTE_TIMEOUT;
		timeout += system_time();
	}

	// Get our endpoint
	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	if (ref->peer_closed || ref->peer == NULL)
		return KOSM_RAY_PEER_CLOSED_ERROR;

	RayEndpoint* peer = ref->peer;
	team_id senderTeam = ref->owner;

	// Copy handles from userspace if needed
	kosm_handle_t kernHandles[KOSM_RAY_MAX_HANDLES];
	if (handleCount > 0 && handles != NULL) {
		if (userCopy) {
			status_t status = user_memcpy(kernHandles, handles,
				handleCount * sizeof(kosm_handle_t));
			if (status != B_OK)
				return status;
		} else {
			memcpy(kernHandles, handles,
				handleCount * sizeof(kosm_handle_t));
		}

		// Validate all handles before transfer
		for (size_t i = 0; i < handleCount; i++) {
			status_t status = validate_handle(&kernHandles[i], senderTeam);
			if (status != B_OK)
				return status;
		}
	}

	// Wait for space in peer's queue if full.
	//
	// NOTE: peer->read_count is read under ref->lock, not peer->lock.
	// This is a benign race: the count can only decrease concurrently
	// (reader consuming under peer->lock), so worst case we see a
	// stale-high value and get a spurious WOULD_BLOCK or unnecessary
	// wait iteration. We re-check after re-acquiring the lock.
	// Acquiring peer->lock here would violate the id-ordered locking
	// protocol and risk deadlock.
	// Hold a reference to peer so it stays alive across unlock/relock.
	// BReference ctor acquires automatically.
	BReference<RayEndpoint> peerRef(peer);

	while (peer->read_count >= peer->max_messages) {
		if (!hasTimeout || ((waitFlags & B_RELATIVE_TIMEOUT) != 0
			&& timeout <= 0)) {
			return B_WOULD_BLOCK;
		}

		ConditionVariableEntry entry;
		ref->write_condition.Add(&entry);

		kosm_ray_id myId = ref->id;
		locker.Unlock();

		status_t status = entry.Wait(waitFlags, timeout);

		BReference<RayEndpoint> newRef = get_locked_ray(myId);
		if (newRef == NULL || newRef.Get() != ref.Get())
			return B_BAD_PORT_ID;
		locker.SetTo(newRef->lock, true);

		if (ref->peer_closed || ref->peer == NULL)
			return KOSM_RAY_PEER_CLOSED_ERROR;

		if (status != B_OK)
			return status;

		peer = ref->peer;
	}

	// Allocate message
	ray_message* msg = allocate_ray_message(dataSize, handleCount);
	if (msg == NULL)
		return B_NO_MEMORY;

	// Fill sender info
	Thread* currentThread = thread_get_current_thread();
	msg->sender = geteuid();
	msg->sender_team = senderTeam;
	msg->sender_priority = currentThread != NULL
		? currentThread->priority : 0;
	msg->qos_class = ref->qos_class;

	// Copy data
	if (dataSize > 0 && data != NULL) {
		if (userCopy) {
			status_t status = user_memcpy(msg->data, data, dataSize);
			if (status != B_OK) {
				free_ray_message(msg);
				return status;
			}
		} else {
			memcpy(msg->data, data, dataSize);
		}
	}

	// Transfer handles
	if (handleCount > 0) {
		team_id receiverTeam = peer->owner;
		TRACE(("kosm_ray_write: transferring %zu handles, sender=%" B_PRId32
			" receiver=%" B_PRId32 " copy=%d\n",
			handleCount, senderTeam, receiverTeam, (int)copyHandles));
		for (size_t i = 0; i < handleCount; i++) {
			TRACE(("  handle[%zu]: type=%u id=%" B_PRId32 "\n",
				i, kernHandles[i].type, kernHandles[i].id));
			status_t status = transfer_handle(&kernHandles[i],
				&msg->handles[i], senderTeam, receiverTeam, copyHandles);
			if (status != B_OK) {
				// Rollback already-transferred handles
				for (size_t j = 0; j < i; j++) {
					rollback_handle(&msg->handles[j], &kernHandles[j],
						senderTeam, receiverTeam, copyHandles);
				}
				free_ray_message(msg);
				T(Write(id, dataSize, handleCount, ref->qos_class, status));
				return status;
			}
		}
	}

	// Enqueue in peer's message list
	// We need to lock peer briefly to enqueue
	locker.Unlock();

	MutexLocker peerLocker(peer->lock);
	if (peer->state != RayEndpoint::kActive) {
		// Peer died between our unlock and this lock.
		// Rollback already-transferred handles before discarding.
		if (handleCount > 0) {
			team_id receiverTeam = peer->owner;
			for (size_t i = 0; i < handleCount; i++) {
				rollback_handle(&msg->handles[i], &kernHandles[i],
					senderTeam, receiverTeam, copyHandles);
			}
		}
		free_ray_message(msg);
		T(Write(id, dataSize, handleCount, ref->qos_class, B_BAD_PORT_ID));
		return B_BAD_PORT_ID;
	}

	peer->messages.Add(msg);
	peer->read_count++;

	T(Write(id, dataSize, handleCount, msg->qos_class, B_OK));

	notify_ray_select_events(peer, B_EVENT_READ);
	peer->read_condition.NotifyOne();

	return B_OK;
}


status_t
kosm_ray_read_internal(kosm_ray_id id, void* data, size_t* dataSize,
	kosm_handle_t* handles, size_t* handleCount, uint32 flags,
	bigtime_t timeout, bool hasTimeout, bool userCopy)
{
	TRACE(("kosm_ray_read(%" B_PRId32 ", data=%p, flags=0x%" B_PRIx32 ")\n",
		id, data, flags));

	if (!sRaysActive || id < 0)
		return B_BAD_PORT_ID;

	bool peekOnly = (flags & KOSM_RAY_PEEK) != 0;

	uint32 waitFlags = flags & (B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT
		| B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT);

	// Convert relative timeout to absolute so re-waits after
	// unlock/relock use the remaining time, not the full duration.
	if (hasTimeout && (waitFlags & B_RELATIVE_TIMEOUT) != 0
		&& timeout > 0) {
		waitFlags = (waitFlags & ~B_RELATIVE_TIMEOUT) | B_ABSOLUTE_TIMEOUT;
		timeout += system_time();
	}

	// Get endpoint
	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	// Wait for messages
	while (ref->read_count == 0) {
		if (ref->peer_closed)
			return KOSM_RAY_PEER_CLOSED_ERROR;

		if (!hasTimeout || ((waitFlags & B_RELATIVE_TIMEOUT) != 0
			&& timeout <= 0)) {
			return B_WOULD_BLOCK;
		}

		ConditionVariableEntry entry;
		ref->read_condition.Add(&entry);

		kosm_ray_id myId = ref->id;
		locker.Unlock();

		status_t status = entry.Wait(waitFlags, timeout);

		BReference<RayEndpoint> newRef = get_locked_ray(myId);
		if (newRef == NULL || newRef.Get() != ref.Get())
			return B_BAD_PORT_ID;
		locker.SetTo(newRef->lock, true);

		if (status != B_OK) {
			// Check if peer closed while we were waiting
			if (ref->peer_closed && ref->read_count > 0)
				break;	// drain remaining messages
			return status;
		}
	}

	// Get head message
	ray_message* msg = ref->messages.Head();
	if (msg == NULL) {
		panic("kosm_ray %" B_PRId32 ": no messages but read_count > 0\n",
			ref->id);
		return B_ERROR;
	}

	// Read buffer sizes from user before importance donation, so early
	// returns on bad addresses don't need to revert the boost.
	size_t maxData = 0;
	if (dataSize != NULL) {
		if (userCopy) {
			if (user_memcpy(&maxData, dataSize, sizeof(size_t)) != B_OK)
				return B_BAD_ADDRESS;
		} else {
			maxData = *dataSize;
		}
	}

	size_t maxHandles = 0;
	if (handleCount != NULL) {
		if (userCopy) {
			if (user_memcpy(&maxHandles, handleCount, sizeof(size_t)) != B_OK)
				return B_BAD_ADDRESS;
		} else {
			maxHandles = *handleCount;
		}
	}

	// Importance donation: boost reader to sender priority.
	// Save original priority for restoration after processing.
	int32 originalPriority = apply_importance_donation(msg);

	// Copy data out
	size_t copyData = std::min(maxData, msg->data_size);

	if (copyData > 0 && data != NULL) {
		if (userCopy) {
			status_t status = user_memcpy(data, msg->data, copyData);
			if (status != B_OK) {
				revert_importance_donation(originalPriority);
				return status;
			}
		} else {
			memcpy(data, msg->data, copyData);
		}
	}

	if (dataSize != NULL) {
		if (userCopy)
			user_memcpy(dataSize, &msg->data_size, sizeof(size_t));
		else
			*dataSize = msg->data_size;
	}

	// Copy handles out
	size_t copyHandles = std::min(maxHandles, (size_t)msg->handle_count);

	if (copyHandles > 0 && handles != NULL) {
		if (userCopy) {
			status_t status = user_memcpy(handles, msg->handles,
				copyHandles * sizeof(kosm_handle_t));
			if (status != B_OK) {
				revert_importance_donation(originalPriority);
				return status;
			}
		} else {
			memcpy(handles, msg->handles,
				copyHandles * sizeof(kosm_handle_t));
		}
	}

	if (handleCount != NULL) {
		size_t count = (size_t)msg->handle_count;
		if (userCopy)
			user_memcpy(handleCount, &count, sizeof(size_t));
		else
			*handleCount = count;
	}

	T(Read(id, msg->data_size, msg->handle_count,
		msg->sender_priority, B_OK));

	if (!peekOnly) {
		ref->messages.RemoveHead();
		ref->read_count--;
		ref->total_count++;

		// Grab peer reference while we hold our lock.
		// We need to notify the peer's write_condition, but we can't
		// access peer's select_infos without peer's lock, and we can't
		// hold both locks simultaneously (deadlock risk).
		BReference<RayEndpoint> peerNotify;
		if (ref->peer != NULL)
			peerNotify.SetTo(ref->peer);

		locker.Unlock();
		free_ray_message(msg);

		// Notify the writing endpoint (our peer) that a slot freed up.
		// The writer waits on its own endpoint's write_condition, which
		// is the peer from the reader's perspective.
		if (peerNotify != NULL) {
			// ConditionVariable::NotifyOne is thread-safe (internal
			// spinlock), no endpoint lock needed.
			peerNotify->write_condition.NotifyOne();

			// Select event notification traverses the select_infos
			// linked list, which requires the endpoint's lock.
			MutexLocker peerLocker(peerNotify->lock);
			if (peerNotify->state == RayEndpoint::kActive)
				notify_ray_select_events(peerNotify.Get(), B_EVENT_WRITE);
		}

		// Revert importance donation now that the message has been
		// consumed and copied out. The boost covered the critical
		// section (dequeue + memcpy) to ensure the reader runs at
		// sender priority while handling the message data.
		revert_importance_donation(originalPriority);
	} else {
		// Peek: notify next reader, revert boost (no message consumed)
		ref->read_condition.NotifyOne();
		revert_importance_donation(originalPriority);
	}

	return B_OK;
}


status_t
kosm_ray_write(kosm_ray_id id, const void* data, size_t dataSize,
	const kosm_handle_t* handles, size_t handleCount, uint32 flags)
{
	return kosm_ray_write_internal(id, data, dataSize, handles, handleCount,
		flags, 0, false, false);
}


status_t
kosm_ray_write_etc(kosm_ray_id id, const void* data, size_t dataSize,
	const kosm_handle_t* handles, size_t handleCount, uint32 flags,
	bigtime_t timeout)
{
	return kosm_ray_write_internal(id, data, dataSize, handles, handleCount,
		flags, timeout, true, false);
}


status_t
kosm_ray_read(kosm_ray_id id, void* data, size_t* dataSize,
	kosm_handle_t* handles, size_t* handleCount, uint32 flags)
{
	return kosm_ray_read_internal(id, data, dataSize, handles, handleCount,
		flags, 0, false, false);
}


status_t
kosm_ray_read_etc(kosm_ray_id id, void* data, size_t* dataSize,
	kosm_handle_t* handles, size_t* handleCount, uint32 flags,
	bigtime_t timeout)
{
	return kosm_ray_read_internal(id, data, dataSize, handles, handleCount,
		flags, timeout, true, false);
}


status_t
kosm_ray_wait(kosm_ray_id id, uint32 signals, uint32* observedSignals,
	uint32 flags, bigtime_t timeout)
{
	if (!sRaysActive || id < 0)
		return B_BAD_PORT_ID;

	uint32 waitFlags = flags & (B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT
		| B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT);

	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	for (;;) {
		uint32 observed = 0;

		if ((signals & KOSM_RAY_READABLE) != 0 && ref->read_count > 0)
			observed |= KOSM_RAY_READABLE;

		if ((signals & KOSM_RAY_WRITABLE) != 0
			&& ref->peer != NULL && !ref->peer_closed)
			observed |= KOSM_RAY_WRITABLE;

		if ((signals & KOSM_RAY_PEER_CLOSED) != 0 && ref->peer_closed)
			observed |= KOSM_RAY_PEER_CLOSED;

		if (observed != 0) {
			if (observedSignals != NULL)
				*observedSignals = observed;
			return B_OK;
		}

		if ((waitFlags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
			return B_WOULD_BLOCK;

		ConditionVariableEntry entry;
		ref->read_condition.Add(&entry);

		kosm_ray_id myId = ref->id;
		locker.Unlock();

		status_t status = entry.Wait(waitFlags, timeout);

		BReference<RayEndpoint> newRef = get_locked_ray(myId);
		if (newRef == NULL || newRef.Get() != ref.Get())
			return B_BAD_PORT_ID;
		locker.SetTo(newRef->lock, true);

		if (status != B_OK)
			return status;
	}
}


status_t
kosm_ray_set_qos(kosm_ray_id id, uint8 qosClass)
{
	if (qosClass > KOSM_QOS_USER_INTERACTIVE)
		return B_BAD_VALUE;

	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	ref->qos_class = qosClass;
	return B_OK;
}


kosm_ray_id
kosm_get_bootstrap_ray_internal(void)
{
	team_id team = team_get_current_team_id();
	if (team < 0 || team >= kMaxTeams)
		return B_BAD_TEAM_ID;

	ReadLocker locker(sRaysLock);
	kosm_ray_id id = sBootstrapRays[team];

	if (id < 0)
		return B_NAME_NOT_FOUND;
	return id;
}


static void
fill_ray_info(RayEndpoint* ep, kosm_ray_info* info, size_t size)
{
	info->ray = ep->id;
	info->team = ep->owner;
	info->peer = ep->peer != NULL ? ep->peer->id : -1;
	info->queue_count = ep->read_count;
	info->flags = 0;
	info->qos_class = ep->qos_class;
}


// ---- Syscalls ----


status_t
_user_kosm_create_ray(kosm_ray_id* userEndpoint0, kosm_ray_id* userEndpoint1,
	uint32 flags)
{
	if (userEndpoint0 == NULL || userEndpoint1 == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userEndpoint0) || !IS_USER_ADDRESS(userEndpoint1))
		return B_BAD_ADDRESS;

	kosm_ray_id ep0, ep1;
	status_t status = kosm_create_ray(&ep0, &ep1, flags);
	if (status != B_OK)
		return status;

	if (user_memcpy(userEndpoint0, &ep0, sizeof(kosm_ray_id)) < B_OK
		|| user_memcpy(userEndpoint1, &ep1, sizeof(kosm_ray_id)) < B_OK) {
		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


status_t
_user_kosm_close_ray(kosm_ray_id id)
{
	return kosm_close_ray(id);
}


status_t
_user_kosm_ray_write(kosm_ray_id id, const void* userData, size_t dataSize,
	const kosm_handle_t* userHandles, size_t handleCount, uint32 flags)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;

	return kosm_ray_write_internal(id, userData, dataSize, userHandles,
		handleCount, flags, 0, false, true);
}


status_t
_user_kosm_ray_write_etc(kosm_ray_id id, const void* userData, size_t dataSize,
	const kosm_handle_t* userHandles, size_t handleCount, uint32 flags,
	bigtime_t timeout)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t status = kosm_ray_write_internal(id, userData, dataSize,
		userHandles, handleCount, flags | B_CAN_INTERRUPT,
		timeout, true, true);

	return syscall_restart_handle_timeout_post(status, timeout);
}


status_t
_user_kosm_ray_read(kosm_ray_id id, void* userData, size_t* userDataSize,
	kosm_handle_t* userHandles, size_t* userHandleCount, uint32 flags)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userDataSize != NULL && !IS_USER_ADDRESS(userDataSize))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;
	if (userHandleCount != NULL && !IS_USER_ADDRESS(userHandleCount))
		return B_BAD_ADDRESS;

	return kosm_ray_read_internal(id, userData, userDataSize, userHandles,
		userHandleCount, flags, 0, false, true);
}


status_t
_user_kosm_ray_read_etc(kosm_ray_id id, void* userData, size_t* userDataSize,
	kosm_handle_t* userHandles, size_t* userHandleCount, uint32 flags,
	bigtime_t timeout)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userDataSize != NULL && !IS_USER_ADDRESS(userDataSize))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;
	if (userHandleCount != NULL && !IS_USER_ADDRESS(userHandleCount))
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t status = kosm_ray_read_internal(id, userData, userDataSize,
		userHandles, userHandleCount, flags | B_CAN_INTERRUPT,
		timeout, true, true);

	return syscall_restart_handle_timeout_post(status, timeout);
}


status_t
_user_kosm_ray_wait(kosm_ray_id id, uint32 signals,
	uint32* userObservedSignals, uint32 flags, bigtime_t timeout)
{
	if (userObservedSignals != NULL
		&& !IS_USER_ADDRESS(userObservedSignals)) {
		return B_BAD_ADDRESS;
	}

	syscall_restart_handle_timeout_pre(flags, timeout);

	uint32 observed = 0;
	status_t status = kosm_ray_wait(id, signals, &observed,
		flags | B_CAN_INTERRUPT, timeout);

	if (status == B_OK && userObservedSignals != NULL) {
		if (user_memcpy(userObservedSignals, &observed,
			sizeof(uint32)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	return syscall_restart_handle_timeout_post(status, timeout);
}


status_t
_user_kosm_ray_set_qos(kosm_ray_id id, uint8 qosClass)
{
	return kosm_ray_set_qos(id, qosClass);
}


kosm_ray_id
_user_kosm_get_bootstrap_ray(void)
{
	return kosm_get_bootstrap_ray_internal();
}


status_t
_user_kosm_get_ray_info(kosm_ray_id id, kosm_ray_info* userInfo, size_t size)
{
	if (userInfo == NULL || size != sizeof(kosm_ray_info))
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	kosm_ray_info info;
	fill_ray_info(ref.Get(), &info, size);

	if (user_memcpy(userInfo, &info, sizeof(kosm_ray_info)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}
