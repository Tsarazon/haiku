/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmOS Ray -- paired IPC channels.
 *
 * Design combines:
 *   Fuchsia Zircon: paired endpoints, handle passing, move semantics,
 *                   per-process handle table (capability-based access)
 *   iOS/XNU Mach:   QoS propagation, importance donation
 *   Haiku:          ConditionVariable, KDL, refcount, select
 *
 * Access control: per-process handle table (see kosm_handle.h).
 * Userspace never sees internal endpoint IDs. All operations go through
 * handles, which are validated and rights-checked at the syscall boundary.
 *
 * INTEGRATION NOTE: Call kosm_ray_delete_owned(team) in team cleanup
 * (alongside delete_owned_ports) to close endpoints and notify peers.
 * The handle table (KosmHandleTable) must be destroyed separately.
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
#include <kosm_handle.h>
#include <vm/kosm_dot.h>
#include <kosm_mutex.h>
#include <Notifications.h>
#include <sem.h>
#include <syscall_restart.h>
#include <team.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <wait_for_objects.h>


//#define TRACE_KOSM_RAY
#ifdef TRACE_KOSM_RAY
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif

//#define RAY_TRACING


// Locking:
//
// * sRaysLock: Protects the sRays hash table (kernel-internal index).
// * RayEndpoint::lock: Protects all members except hash_link,
//   lock, state, and id (immutable).
// * RayEndpoint::state: Atomic linearization point for creation/deletion.
// * KosmHandleTable::fLock: Protects per-team handle entries.
//
// Access control is handled by the per-process handle table. If a process
// holds a handle to an endpoint, it has the right to operate on it
// (subject to the rights mask on the handle). No ownership checks are
// needed in the kernel functions themselves.
//
// Peer locking order: always lock lower id first to avoid deadlock.
// Handle table locking order: lower address first.


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


// Resolved handle stored in a message during transit.
// At write time, sender's handles are resolved to objects/IDs.
// At read time, objects are installed into the receiver's handle table.
struct ray_msg_handle {
	union {
		KernelReferenceable*	object;		// ray, mutex, dot (refcounted)
		int32					legacy_id;	// area, sem (already transferred)
	};
	uint16		type;		// KOSM_HANDLE_RAY, etc.
	uint32		rights;
};


struct ray_message : DoublyLinkedListLinkImpl<ray_message> {
	size_t				data_size;
	uint32				handle_count;
	uid_t				sender;
	team_id				sender_team;
	int32				sender_priority;
	uint8				qos_class;
	ray_msg_handle		handles[KOSM_RAY_MAX_HANDLES];
	char				data[0];
};

typedef DoublyLinkedList<ray_message> MessageList;


/*!	Release handles stored in an undelivered message (e.g. peer closed).
*/
static void
release_msg_handles(ray_message* msg)
{
	for (uint32 i = 0; i < msg->handle_count; i++) {
		ray_msg_handle* h = &msg->handles[i];
		switch (h->type) {
			case KOSM_HANDLE_RAY:
			case KOSM_HANDLE_MUTEX:
			case KOSM_HANDLE_DOT:
				if (h->object != NULL)
					h->object->ReleaseReference();
				break;
			case KOSM_HANDLE_AREA:
				// Area was already transferred; delete it
				vm_delete_area(B_SYSTEM_TEAM, h->legacy_id, true);
				break;
			case KOSM_HANDLE_SEM:
				// Sem ownership was transferred; can't easily undo
				break;
			default:
				break;
		}
	}
}


// Space accounting (needed by free_ray_message, used in ~RayEndpoint)
static int32 sTotalSpaceUsed;


static void
free_ray_message(ray_message* msg)
{
	const size_t size = sizeof(ray_message) + msg->data_size;
	free(msg);
	atomic_add(&sTotalSpaceUsed, -size);
}


// ---- Endpoint ----


struct RayEndpoint : public KernelReferenceable {
	enum State {
		kUnused = 0,
		kActive,
		kDeleted
	};

	RayEndpoint*		hash_link;
	kosm_ray_id			id;

	// Diagnostic/tracking only. Updated on handle transfer.
	// NOT used for access control (handle table provides that).
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
		while (ray_message* msg = messages.RemoveHead()) {
			release_msg_handles(msg);
			free_ray_message(msg);
		}

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

static RayNotificationService sNotificationService;

// Bootstrap rays are stored in the Team struct (team->kosm_bootstrap_ray).
// No static array needed — works for any team_id range.


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


// ---- Handle transfer (via handle table) ----


static void rollback_resolved_handle(ray_msg_handle* resolved,
	KosmHandleTable* senderTable, bool wasCopy);


/*!	Resolve sender's handles into the message's internal format.
	For move mode: handles are removed from the sender's table.
	For copy mode: handles remain in the sender's table.

	Type-specific operations (area clone/transfer) are performed here
	so the message contains objects ready for the receiver.

	On partial failure, already-resolved handles are rolled back.
*/
static status_t
resolve_sender_handles(const kosm_handle_t* userHandles, size_t handleCount,
	ray_msg_handle* resolved, KosmHandleTable* senderTable,
	team_id receiverTeam, bool copyMode)
{
	for (size_t i = 0; i < handleCount; i++) {
		kosm_handle_t handle = userHandles[i];
		uint16 type;
		uint32 rights;

		status_t status = senderTable->GetInfo(handle, &type, &rights);
		if (status != B_OK) {
			// Rollback already-resolved handles
			for (size_t j = 0; j < i; j++)
				rollback_resolved_handle(&resolved[j], senderTable, copyMode);
			return KOSM_RAY_INVALID_HANDLE;
		}

		if (!copyMode && (rights & KOSM_RIGHT_TRANSFER) == 0) {
			for (size_t j = 0; j < i; j++)
				rollback_resolved_handle(&resolved[j], senderTable, copyMode);
			return B_NOT_ALLOWED;
		}

		resolved[i].type = type;
		resolved[i].rights = rights;

		switch (type) {
			case KOSM_HANDLE_RAY:
			case KOSM_HANDLE_MUTEX:
			case KOSM_HANDLE_DOT:
			{
				// Refcounted kernel object
				KernelReferenceable* object;
				if (copyMode) {
					status = senderTable->Lookup(handle, type,
						KOSM_RIGHT_DUPLICATE, &object);
					// Lookup acquires a reference for the message
				} else {
					status = senderTable->Remove(handle, &object);
					// Remove transfers reference to us
				}
				if (status != B_OK) {
					for (size_t j = 0; j < i; j++)
						rollback_resolved_handle(&resolved[j],
							senderTable, copyMode);
					return KOSM_RAY_INVALID_HANDLE;
				}
				resolved[i].object = object;

				// Update diagnostic owner for rays
				if (type == KOSM_HANDLE_RAY && !copyMode) {
					RayEndpoint* ep =
						static_cast<RayEndpoint*>(object);
					MutexLocker epLocker(ep->lock);
					ep->owner = receiverTeam;
				}
				break;
			}

			case KOSM_HANDLE_AREA:
			{
				int32 areaID;
				if (copyMode) {
					status = senderTable->LookupLegacy(handle,
						KOSM_HANDLE_AREA, KOSM_RIGHT_DUPLICATE, &areaID);
				} else {
					status = senderTable->RemoveLegacy(handle, &areaID);
				}
				if (status != B_OK) {
					for (size_t j = 0; j < i; j++)
						rollback_resolved_handle(&resolved[j],
							senderTable, copyMode);
					return KOSM_RAY_INVALID_HANDLE;
				}

				void* addr;
				if (copyMode) {
					area_id cloned = vm_clone_area(receiverTeam,
						"transferred area", &addr, B_ANY_ADDRESS,
						B_READ_AREA | B_WRITE_AREA | B_KERNEL_READ_AREA
							| B_KERNEL_WRITE_AREA,
						REGION_NO_PRIVATE_MAP, areaID, true);
					if (cloned < 0) {
						for (size_t j = 0; j < i; j++)
							rollback_resolved_handle(&resolved[j],
								senderTable, copyMode);
						return cloned;
					}
					resolved[i].legacy_id = cloned;
				} else {
					area_id newArea = transfer_area(areaID, &addr,
						B_ANY_ADDRESS, receiverTeam, true);
					if (newArea < 0) {
						// Transfer failed, rollback: re-insert into sender
						senderTable->InsertLegacy(areaID,
							KOSM_HANDLE_AREA, rights);
						for (size_t j = 0; j < i; j++)
							rollback_resolved_handle(&resolved[j],
								senderTable, copyMode);
						return newArea;
					}
					resolved[i].legacy_id = newArea;
				}
				break;
			}

			case KOSM_HANDLE_SEM:
			{
				int32 semID;
				if (copyMode) {
					status = senderTable->LookupLegacy(handle,
						KOSM_HANDLE_SEM, KOSM_RIGHT_DUPLICATE, &semID);
				} else {
					status = senderTable->RemoveLegacy(handle, &semID);
				}
				if (status != B_OK) {
					for (size_t j = 0; j < i; j++)
						rollback_resolved_handle(&resolved[j],
							senderTable, copyMode);
					return KOSM_RAY_INVALID_HANDLE;
				}

				if (!copyMode) {
					status = set_sem_owner(semID, receiverTeam);
					if (status != B_OK) {
						senderTable->InsertLegacy(semID,
							KOSM_HANDLE_SEM, rights);
						for (size_t j = 0; j < i; j++)
							rollback_resolved_handle(&resolved[j],
								senderTable, copyMode);
						return status;
					}
				}
				resolved[i].legacy_id = semID;
				break;
			}

			case KOSM_HANDLE_FD:
				// TODO: implement vfs_dup_foreign_fd()
				for (size_t j = 0; j < i; j++)
					rollback_resolved_handle(&resolved[j],
						senderTable, copyMode);
				return B_NOT_SUPPORTED;

			default:
				for (size_t j = 0; j < i; j++)
					rollback_resolved_handle(&resolved[j],
						senderTable, copyMode);
				return KOSM_RAY_INVALID_HANDLE;
		}
	}

	return B_OK;
}


/*!	Install resolved handles into the receiver's handle table.
	Called at read time. Returns new handle values via outHandles.
*/
static status_t
install_receiver_handles(const ray_msg_handle* resolved, size_t handleCount,
	kosm_handle_t* outHandles, KosmHandleTable* receiverTable)
{
	for (size_t i = 0; i < handleCount; i++) {
		kosm_handle_t newHandle;

		switch (resolved[i].type) {
			case KOSM_HANDLE_RAY:
			case KOSM_HANDLE_MUTEX:
			case KOSM_HANDLE_DOT:
			{
				// Insert transfers the message's reference to the table.
				// Insert acquires its own reference; we release the message's
				// reference afterward.
				newHandle = receiverTable->Insert(resolved[i].object,
					resolved[i].type, resolved[i].rights);
				if (newHandle < 0) {
					// Table full. Already-installed handles remain
					// (receiver can close them). Release remaining.
					for (size_t j = i; j < handleCount; j++) {
						if (resolved[j].type == KOSM_HANDLE_RAY
							|| resolved[j].type == KOSM_HANDLE_MUTEX
							|| resolved[j].type == KOSM_HANDLE_DOT) {
							resolved[j].object->ReleaseReference();
						}
					}
					return (status_t)newHandle;
				}
				// Release the message's reference (table now holds one)
				resolved[i].object->ReleaseReference();
				break;
			}

			case KOSM_HANDLE_AREA:
			case KOSM_HANDLE_SEM:
			{
				newHandle = receiverTable->InsertLegacy(
					resolved[i].legacy_id, resolved[i].type,
					resolved[i].rights);
				if (newHandle < 0)
					return (status_t)newHandle;
				break;
			}

			default:
				return KOSM_RAY_INVALID_HANDLE;
		}

		outHandles[i] = newHandle;
	}

	return B_OK;
}


/*!	Rollback a single resolved handle on send failure.
*/
static void
rollback_resolved_handle(ray_msg_handle* resolved,
	KosmHandleTable* senderTable, bool wasCopy)
{
	switch (resolved->type) {
		case KOSM_HANDLE_RAY:
		case KOSM_HANDLE_MUTEX:
		case KOSM_HANDLE_DOT:
			if (wasCopy) {
				// Just release the extra reference we acquired
				resolved->object->ReleaseReference();
			} else {
				// Re-insert into sender's table (restores the handle)
				kosm_handle_t h = senderTable->Insert(resolved->object,
					resolved->type, resolved->rights);
				if (h < 0) {
					// Cannot re-insert; release reference as fallback
					resolved->object->ReleaseReference();
				}
				// Insert acquires its own ref; we still hold the
				// message's ref, so release it.
				resolved->object->ReleaseReference();
			}
			break;

		case KOSM_HANDLE_AREA:
			if (wasCopy) {
				// Delete the clone we created
				vm_delete_area(B_SYSTEM_TEAM, resolved->legacy_id, true);
			} else {
				// Re-insert as legacy handle
				senderTable->InsertLegacy(resolved->legacy_id,
					KOSM_HANDLE_AREA, resolved->rights);
			}
			break;

		case KOSM_HANDLE_SEM:
			if (!wasCopy) {
				senderTable->InsertLegacy(resolved->legacy_id,
					KOSM_HANDLE_SEM, resolved->rights);
			}
			break;

		default:
			break;
	}
}


// ---- Importance donation ----
//
// TECH DEBT: Current implementation is a direct priority override, not
// true Mach-style importance donation. Known limitations:
//
// 1. No propagation through chains (A -> B -> C). If server B forwards
//    a request to server C, the donation from client A does not follow.
//
// 2. Manual revert. The boost is reverted explicitly after message
//    processing. If the reader receives two messages with different
//    sender priorities back-to-back, the first revert_importance_donation
//    may drop priority below what the second message requires.
//
// 3. No multi-donor tracking. With multiple concurrent donors, the
//    effective priority should be max(all active donations), but we
//    only track one sender_priority per message.
//
// Proper fix: turnstile-based donation integrated with LAVD scheduler.
// The turnstile would track all active donors per thread, compute
// effective priority as max(base, max(donors)), and automatically
// revert when the boosted section completes. This requires LAVD to
// expose a "temporary boost" API distinct from QoS classes.
//
// Do NOT build scheduler integration on the current mechanism. It will
// be replaced.


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

	KosmHandleTable* table = KosmHandleTable::TableFor(owner);
	if (table == NULL)
		return B_BAD_TEAM_ID;

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

	// Check global limit
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

	// Insert into hash table (kernel-internal index by ray ID)
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

	// Insert into per-process handle table
	kosm_handle_t h0 = table->Insert(ep0.Get(), KOSM_HANDLE_RAY,
		KOSM_RIGHT_RAY_DEFAULT);
	if (h0 < 0) {
		// Undo hash table insertion
		WriteLocker locker(sRaysLock);
		sRays.Remove(ep0);
		sRays.Remove(ep1);
		ep0->ReleaseReference();
		ep1->ReleaseReference();
		atomic_add(&sUsedRays, -2);
		return (status_t)h0;
	}

	kosm_handle_t h1 = table->Insert(ep1.Get(), KOSM_HANDLE_RAY,
		KOSM_RIGHT_RAY_DEFAULT);
	if (h1 < 0) {
		table->Remove(h0);
		WriteLocker locker(sRaysLock);
		sRays.Remove(ep0);
		sRays.Remove(ep1);
		ep0->ReleaseReference();
		ep1->ReleaseReference();
		atomic_add(&sUsedRays, -2);
		return (status_t)h1;
	}

	// Activate (linearization point)
	const int32 old0 = atomic_test_and_set(&ep0->state,
		RayEndpoint::kActive, RayEndpoint::kUnused);
	const int32 old1 = atomic_test_and_set(&ep1->state,
		RayEndpoint::kActive, RayEndpoint::kUnused);

	if (old0 != RayEndpoint::kUnused || old1 != RayEndpoint::kUnused)
		panic("kosm_ray: state modified during creation!\n");

	// Return handles, not internal IDs
	*endpoint0 = h0;
	*endpoint1 = h1;

	T(Create(ep0->id, ep1->id, owner));

	sNotificationService.Notify(RAY_ADDED, ep0->id);
	sNotificationService.Notify(RAY_ADDED, ep1->id);

	TRACE(("kosm_create_ray: created pair %" B_PRId32 " <-> %" B_PRId32
		" (handles %" B_PRId32 ", %" B_PRId32 ")\n",
		ep0->id, ep1->id, h0, h1));
	return B_OK;
}


/*!	Close a ray endpoint by internal ID.
	Called when the last handle to this endpoint is removed from any
	handle table. Access control is handled by the handle table layer;
	this function assumes the caller has verified access.
*/
status_t
kosm_close_ray_internal(kosm_ray_id id)
{
	TRACE(("kosm_close_ray(%" B_PRId32 ")\n", id));

	if (!sRaysActive || id < 0)
		return B_BAD_PORT_ID;

	BReference<RayEndpoint> ref;
	{
		ReadLocker locker(sRaysLock);
		ref.SetTo(sRays.Lookup(id));
	}
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

	uninit_ray(ref);
	atomic_add(&sUsedRays, -1);

	T(Close(id));

	return B_OK;
}


kosm_ray_id
kosm_ray_object_id(KernelReferenceable* object)
{
	RayEndpoint* ep = static_cast<RayEndpoint*>(object);
	return ep->id;
}


/*!	Clean up all ray endpoints when a team exits.
	Called from team cleanup path. The handle table destructor releases
	handle references, but we also need to close the ray endpoints
	themselves (notify peers, remove from hash, release messages).
*/
void
kosm_ray_delete_owned(Team* team)
{
	TRACE(("kosm_ray_delete_owned(team = %" B_PRId32 ")\n", team->id));

	// Clear bootstrap ray for this team
	team->kosm_bootstrap_ray = -1;

	// Walk the hash table and close any endpoints owned by this team.
	// Repeat until no more are found, since closing one endpoint does
	// not affect iteration over others.
	// This is O(total_endpoints * owned_count) but team exit is not hot.
	for (;;) {
		BReference<RayEndpoint> found;
		{
			ReadLocker locker(sRaysLock);
			for (RayHashTable::Iterator it = sRays.GetIterator();
				RayEndpoint* ep = it.Next();) {
				if (ep->owner == team->id
					&& ep->state == RayEndpoint::kActive) {
					found.SetTo(ep);
					break;
				}
			}
		}

		if (found == NULL)
			break;

		if (delete_ray_logical(found) != B_OK)
			continue;

		notify_peer_closed(found);

		{
			MutexLocker locker(found->lock);
			found->peer = NULL;
			found->peer_ref.Unset();
		}

		{
			WriteLocker locker(sRaysLock);
			sRays.Remove(found);
			found->ReleaseReference();
		}

		uninit_ray(found);
		atomic_add(&sUsedRays, -1);
	}
}


/*!	Transfer a ray handle to another team as its bootstrap ray.
	The handle is moved from the caller's table to the target team's table.
	The target team retrieves it via kosm_get_bootstrap_ray().
*/
status_t
kosm_ray_set_bootstrap(team_id targetTeam, kosm_handle_t handle)
{
	KosmHandleTable* sourceTable = KosmHandleTable::TableForCurrent();
	if (sourceTable == NULL)
		return B_NOT_INITIALIZED;

	// Hold reference to target team through the entire operation
	// to prevent use-after-free on the handle table pointer.
	Team* target = Team::Get(targetTeam);
	if (target == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> targetRef(target, true);

	KosmHandleTable* targetTable = target->kosm_handle_table;
	if (targetTable == NULL)
		return B_BAD_TEAM_ID;

	// Transfer handle from caller to target
	kosm_handle_t newHandle;
	status_t status = sourceTable->Transfer(handle, targetTable, &newHandle);
	if (status != B_OK)
		return status;

	// Update diagnostic owner on the endpoint
	{
		KernelReferenceable* object;
		status = targetTable->Lookup(newHandle, KOSM_HANDLE_RAY, 0, &object);
		if (status == B_OK) {
			RayEndpoint* ep = static_cast<RayEndpoint*>(object);
			MutexLocker epLocker(ep->lock);
			ep->owner = targetTeam;
			epLocker.Unlock();
			object->ReleaseReference();
		}
	}

	// Record the handle value for kosm_get_bootstrap_ray().
	// No lock needed: parent writes before resume_thread (memory barrier),
	// child reads after being scheduled.
	target->kosm_bootstrap_ray = newHandle;
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


/*!	Register select events for a ray endpoint.
	Called from wait_for_objects infrastructure with the internal ray ID.
	Access control is handled at the syscall layer via handle table.
*/
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

	// Use a direct hash lookup (not get_locked_ray) so deselect works
	// even when the endpoint state is kDeleted. This prevents stale
	// select_info pointers from remaining in the list after
	// common_wait_for_objects returns.
	BReference<RayEndpoint> ref;
	{
		ReadLocker hashLocker(sRaysLock);
		ref.SetTo(sRays.Lookup(id));
	}
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


kosm_ray_id
kosm_ray_handle_to_id(kosm_handle_t handle)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	KernelReferenceable* object;
	status_t status = table->Lookup(handle, KOSM_HANDLE_RAY,
		KOSM_RIGHT_WAIT, &object);
	if (status != B_OK)
		return status;

	RayEndpoint* ep = static_cast<RayEndpoint*>(object);
	kosm_ray_id id = ep->id;
	object->ReleaseReference();
	return id;
}


// ---- Handle resolution helper ----


/*!	Look up a ray handle and return the endpoint with a reference.
	Returns B_OK with the endpoint's internal ID in *outInternalId.
	Used by both public kernel API and syscall wrappers.
*/
static status_t
resolve_ray_handle(kosm_ray_id handle, uint32 requiredRights,
	BReference<RayEndpoint>* outRef, kosm_ray_id* outInternalId)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	KernelReferenceable* object;
	status_t status = table->Lookup(handle, KOSM_HANDLE_RAY,
		requiredRights, &object);
	if (status != B_OK)
		return status;

	RayEndpoint* ep = static_cast<RayEndpoint*>(object);
	outRef->SetTo(ep, true);	// takes over reference from Lookup
	if (outInternalId != NULL)
		*outInternalId = ep->id;
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
kosm_close_ray(kosm_ray_id handle)
{
	// Kernel-space close: translate handle to internal ID, then close.
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	KernelReferenceable* object;
	uint16 type;
	status_t status = table->Remove(handle, &object, &type);
	if (status != B_OK)
		return status;
	if (type != KOSM_HANDLE_RAY) {
		object->ReleaseReference();
		return B_BAD_VALUE;
	}

	RayEndpoint* ep = static_cast<RayEndpoint*>(object);
	kosm_ray_id internalId = ep->id;
	object->ReleaseReference();

	return kosm_close_ray_internal(internalId);
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

	// Get our endpoint (by internal ID, already validated by syscall layer)
	BReference<RayEndpoint> ref = get_locked_ray(id);
	if (ref == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(ref->lock, true);

	if (ref->peer_closed || ref->peer == NULL)
		return KOSM_RAY_PEER_CLOSED_ERROR;

	RayEndpoint* peer = ref->peer;
	team_id senderTeam = ref->owner;

	// Copy handle values from userspace if needed
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
	}

	// Wait for space in peer's queue if full.
	//
	// NOTE: peer->read_count is read under ref->lock, not peer->lock.
	// This is a benign race: the count can only decrease concurrently
	// (reader consuming under peer->lock), so worst case we see a
	// stale-high value and get a spurious WOULD_BLOCK or unnecessary
	// wait iteration. We re-check after re-acquiring the lock.
	// Hold a reference to peer so it stays alive across unlock/relock.
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

	// Resolve handles via sender's handle table.
	// This removes/copies handles from the sender's table and stores
	// resolved objects in the message for the receiver to install.
	if (handleCount > 0) {
		// Use current team's table directly — avoids TableFor() which
		// releases the Team reference and leaves a dangling pointer.
		// The sender is always the current team (handle was validated
		// by resolve_ray_handle at the syscall boundary).
		KosmHandleTable* senderTable =
			KosmHandleTable::TableForCurrent();
		if (senderTable == NULL) {
			free_ray_message(msg);
			return B_NOT_INITIALIZED;
		}

		team_id receiverTeam = peer->owner;
		status_t status = resolve_sender_handles(kernHandles, handleCount,
			msg->handles, senderTable, receiverTeam, copyHandles);
		if (status != B_OK) {
			free_ray_message(msg);
			T(Write(id, dataSize, handleCount, ref->qos_class, status));
			return status;
		}
	}

	// Enqueue in peer's message list
	locker.Unlock();

	MutexLocker peerLocker(peer->lock);
	if (peer->state != RayEndpoint::kActive) {
		// Peer died between our unlock and this lock.
		// Release handles stored in the message.
		release_msg_handles(msg);
		free_ray_message(msg);
		T(Write(id, dataSize, handleCount, ref->qos_class, B_BAD_PORT_ID));
		return B_BAD_PORT_ID;
	}

	// Recheck queue capacity under peer->lock.
	// The initial check was under ref->lock (sender's lock), so concurrent
	// writers on the same endpoint could all pass it before any enqueued.
	if (peer->read_count >= peer->max_messages) {
		release_msg_handles(msg);
		free_ray_message(msg);
		return B_WOULD_BLOCK;
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

	// Get endpoint (by internal ID, already validated by syscall layer)
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
		if (userCopy) {
			if (user_memcpy(dataSize, &msg->data_size,
				sizeof(size_t)) != B_OK) {
				revert_importance_donation(originalPriority);
				return B_BAD_ADDRESS;
			}
		} else {
			*dataSize = msg->data_size;
		}
	}

	// Install handles into receiver's handle table.
	// This converts resolved objects into per-process handle values.
	// Peek mode does NOT consume handles (Zircon/Mach semantics):
	// the user sees handleCount but must do a real read to get them.
	size_t originalHandleCount = (size_t)msg->handle_count;
	size_t installCount = peekOnly
		? 0 : std::min(maxHandles, originalHandleCount);
	kosm_handle_t newHandles[KOSM_RAY_MAX_HANDLES];

	if (installCount > 0) {
		KosmHandleTable* receiverTable =
			KosmHandleTable::TableForCurrent();
		if (receiverTable == NULL) {
			revert_importance_donation(originalPriority);
			return B_NOT_INITIALIZED;
		}

		status_t status = install_receiver_handles(msg->handles,
			installCount, newHandles, receiverTable);
		if (status != B_OK) {
			revert_importance_donation(originalPriority);
			return status;
		}

		// Mark handles as consumed so a failed user_memcpy below
		// cannot cause double-install on a subsequent read attempt.
		msg->handle_count = 0;
	}

	T(Read(id, msg->data_size, originalHandleCount,
		msg->sender_priority, B_OK));

	// Point of no return for non-peek: dequeue the message before
	// user_memcpy of handle values.  If user_memcpy fails after this,
	// handles are in the receiver's table but the user won't know
	// their values (acceptable: userspace gave a bad pointer).
	BReference<RayEndpoint> peerNotify;
	if (!peekOnly) {
		ref->messages.RemoveHead();
		ref->read_count--;
		ref->total_count++;

		if (ref->peer != NULL)
			peerNotify.SetTo(ref->peer);
	}

	locker.Unlock();

	// Copy handle values to user (after dequeue — no UAF on retry)
	status_t result = B_OK;

	if (installCount > 0 && handles != NULL) {
		if (userCopy) {
			if (user_memcpy(handles, newHandles,
				installCount * sizeof(kosm_handle_t)) != B_OK) {
				result = B_BAD_ADDRESS;
			}
		} else {
			memcpy(handles, newHandles,
				installCount * sizeof(kosm_handle_t));
		}
	}

	if (handleCount != NULL && result == B_OK) {
		if (userCopy) {
			if (user_memcpy(handleCount, &originalHandleCount,
				sizeof(size_t)) != B_OK) {
				result = B_BAD_ADDRESS;
			}
		} else {
			*handleCount = originalHandleCount;
		}
	}

	if (!peekOnly) {
		free_ray_message(msg);

		// Notify the writing endpoint (our peer) that a slot freed up.
		if (peerNotify != NULL) {
			peerNotify->write_condition.NotifyOne();

			MutexLocker peerLocker(peerNotify->lock);
			if (peerNotify->state == RayEndpoint::kActive)
				notify_ray_select_events(peerNotify.Get(), B_EVENT_WRITE);
		}
	} else {
		// Peek: notify next reader (no message consumed)
		ref->read_condition.NotifyOne();
	}

	revert_importance_donation(originalPriority);
	return result;
}


status_t
kosm_ray_write(kosm_ray_id handle, const void* data, size_t dataSize,
	const kosm_handle_t* handles, size_t handleCount, uint32 flags)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WRITE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_write_internal(internalId, data, dataSize, handles,
		handleCount, flags, 0, false, false);
}


status_t
kosm_ray_write_etc(kosm_ray_id handle, const void* data, size_t dataSize,
	const kosm_handle_t* handles, size_t handleCount, uint32 flags,
	bigtime_t timeout)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WRITE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_write_internal(internalId, data, dataSize, handles,
		handleCount, flags, timeout, true, false);
}


status_t
kosm_ray_read(kosm_ray_id handle, void* data, size_t* dataSize,
	kosm_handle_t* handles, size_t* handleCount, uint32 flags)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_READ,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_read_internal(internalId, data, dataSize, handles,
		handleCount, flags, 0, false, false);
}


status_t
kosm_ray_read_etc(kosm_ray_id handle, void* data, size_t* dataSize,
	kosm_handle_t* handles, size_t* handleCount, uint32 flags,
	bigtime_t timeout)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_READ,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_read_internal(internalId, data, dataSize, handles,
		handleCount, flags, timeout, true, false);
}


static status_t
kosm_ray_wait_internal(kosm_ray_id id, uint32 signals,
	uint32* observedSignals, uint32 flags, bigtime_t timeout)
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

		// Benign race: peer->read_count read without peer->lock
		// (can only decrease concurrently, so worst case = spurious wait)
		if ((signals & KOSM_RAY_WRITABLE) != 0
			&& ref->peer != NULL && !ref->peer_closed
			&& ref->peer->read_count < ref->peer->max_messages)
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

		// write_condition wakes on queue slot freed;
		// read_condition wakes on data arrival / peer close
		ConditionVariableEntry entry;
		if (signals == KOSM_RAY_WRITABLE)
			ref->write_condition.Add(&entry);
		else
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


static status_t
kosm_ray_set_qos_internal(kosm_ray_id id, uint8 qosClass)
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


status_t
kosm_ray_wait(kosm_ray_id handle, uint32 signals, uint32* observedSignals,
	uint32 flags, bigtime_t timeout)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WAIT,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_wait_internal(internalId, signals, observedSignals,
		flags, timeout);
}


status_t
kosm_ray_set_qos(kosm_ray_id handle, uint8 qosClass)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_MANAGE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_set_qos_internal(internalId, qosClass);
}


kosm_ray_id
kosm_get_bootstrap_ray_internal(void)
{
	Thread* thread = thread_get_current_thread();
	if (thread == NULL || thread->team == NULL)
		return B_BAD_TEAM_ID;

	kosm_ray_id id = thread->team->kosm_bootstrap_ray;

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
//
// All syscalls take per-process handles (opaque int32), translate them
// to internal RayEndpoint* via the handle table, then call internal
// functions with the endpoint's kernel-internal ID.
// Access control is implicit: if you have the handle, you may use it.
// Rights on the handle further restrict operations.


status_t
_user_kosm_create_ray(kosm_ray_id* userEndpoint0, kosm_ray_id* userEndpoint1,
	uint32 flags)
{
	if (userEndpoint0 == NULL || userEndpoint1 == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userEndpoint0) || !IS_USER_ADDRESS(userEndpoint1))
		return B_BAD_ADDRESS;

	// kosm_create_ray returns handles (not internal IDs) in the new model
	kosm_ray_id h0, h1;
	status_t status = kosm_create_ray(&h0, &h1, flags);
	if (status != B_OK)
		return status;

	if (user_memcpy(userEndpoint0, &h0, sizeof(kosm_ray_id)) < B_OK
		|| user_memcpy(userEndpoint1, &h1, sizeof(kosm_ray_id)) < B_OK) {
		// Close via handle table
		KosmHandleTable* table = KosmHandleTable::TableForCurrent();
		if (table != NULL) {
			KernelReferenceable* obj0;
			KernelReferenceable* obj1;
			if (table->Remove(h0, &obj0) == B_OK) {
				RayEndpoint* ep = static_cast<RayEndpoint*>(obj0);
				kosm_close_ray_internal(ep->id);
				obj0->ReleaseReference();
			}
			if (table->Remove(h1, &obj1) == B_OK) {
				RayEndpoint* ep = static_cast<RayEndpoint*>(obj1);
				kosm_close_ray_internal(ep->id);
				obj1->ReleaseReference();
			}
		}
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


status_t
_user_kosm_close_ray(kosm_ray_id handle)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	// Remove handle from table, get the endpoint object
	KernelReferenceable* object;
	uint16 type;
	status_t status = table->Remove(handle, &object, &type);
	if (status != B_OK)
		return status;
	if (type != KOSM_HANDLE_RAY) {
		object->ReleaseReference();
		return B_BAD_VALUE;
	}

	RayEndpoint* ep = static_cast<RayEndpoint*>(object);
	kosm_ray_id internalId = ep->id;
	object->ReleaseReference();

	return kosm_close_ray_internal(internalId);
}


status_t
_user_kosm_ray_write(kosm_ray_id handle, const void* userData, size_t dataSize,
	const kosm_handle_t* userHandles, size_t handleCount, uint32 flags)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;

	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WRITE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_write_internal(internalId, userData, dataSize,
		userHandles, handleCount, flags, 0, false, true);
}


status_t
_user_kosm_ray_write_etc(kosm_ray_id handle, const void* userData,
	size_t dataSize, const kosm_handle_t* userHandles, size_t handleCount,
	uint32 flags, bigtime_t timeout)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;

	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WRITE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status = kosm_ray_write_internal(internalId, userData, dataSize,
		userHandles, handleCount, flags | B_CAN_INTERRUPT,
		timeout, true, true);

	return syscall_restart_handle_timeout_post(status, timeout);
}


status_t
_user_kosm_ray_read(kosm_ray_id handle, void* userData, size_t* userDataSize,
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

	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_READ,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_read_internal(internalId, userData, userDataSize,
		userHandles, userHandleCount, flags, 0, false, true);
}


status_t
_user_kosm_ray_read_etc(kosm_ray_id handle, void* userData,
	size_t* userDataSize, kosm_handle_t* userHandles,
	size_t* userHandleCount, uint32 flags, bigtime_t timeout)
{
	if (userData != NULL && !IS_USER_ADDRESS(userData))
		return B_BAD_ADDRESS;
	if (userDataSize != NULL && !IS_USER_ADDRESS(userDataSize))
		return B_BAD_ADDRESS;
	if (userHandles != NULL && !IS_USER_ADDRESS(userHandles))
		return B_BAD_ADDRESS;
	if (userHandleCount != NULL && !IS_USER_ADDRESS(userHandleCount))
		return B_BAD_ADDRESS;

	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_READ,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	syscall_restart_handle_timeout_pre(flags, timeout);

	status = kosm_ray_read_internal(internalId, userData, userDataSize,
		userHandles, userHandleCount, flags | B_CAN_INTERRUPT,
		timeout, true, true);

	return syscall_restart_handle_timeout_post(status, timeout);
}


status_t
_user_kosm_ray_wait(kosm_ray_id handle, uint32 signals,
	uint32* userObservedSignals, uint32 flags, bigtime_t timeout)
{
	if (userObservedSignals != NULL
		&& !IS_USER_ADDRESS(userObservedSignals)) {
		return B_BAD_ADDRESS;
	}

	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_WAIT,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	syscall_restart_handle_timeout_pre(flags, timeout);

	uint32 observed = 0;
	status = kosm_ray_wait_internal(internalId, signals, &observed,
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
_user_kosm_ray_set_qos(kosm_ray_id handle, uint8 qosClass)
{
	BReference<RayEndpoint> ref;
	kosm_ray_id internalId;
	status_t status = resolve_ray_handle(handle, KOSM_RIGHT_MANAGE,
		&ref, &internalId);
	if (status != B_OK)
		return status;

	return kosm_ray_set_qos_internal(internalId, qosClass);
}


kosm_ray_id
_user_kosm_get_bootstrap_ray(void)
{
	return kosm_get_bootstrap_ray_internal();
}


status_t
_user_kosm_ray_set_bootstrap(team_id targetTeam, kosm_ray_id handle)
{
	// kosm_ray_set_bootstrap handles everything via handle table transfer.
	// The caller must hold a handle with TRANSFER right.
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	// Verify the handle is a ray with transfer rights before calling
	uint16 type;
	uint32 rights;
	status_t status = table->GetInfo(handle, &type, &rights);
	if (status != B_OK)
		return status;
	if (type != KOSM_HANDLE_RAY)
		return B_BAD_VALUE;
	if ((rights & KOSM_RIGHT_TRANSFER) == 0)
		return B_NOT_ALLOWED;

	return kosm_ray_set_bootstrap(targetTeam, handle);
}


status_t
_user_kosm_get_ray_info(kosm_ray_id handle, kosm_ray_info* userInfo,
	size_t size)
{
	if (userInfo == NULL || size != sizeof(kosm_ray_info))
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	BReference<RayEndpoint> ref;
	status_t status = resolve_ray_handle(handle, 0, &ref, NULL);
	if (status != B_OK)
		return status;

	MutexLocker locker(ref->lock);

	kosm_ray_info info;
	fill_ray_info(ref.Get(), &info, size);
	// Store the handle value (not internal id) in the info struct
	info.ray = handle;

	// Resolve peer's internal ID to a handle in our table.
	// The peer may not have a handle in our table (e.g., after transfer
	// to another process), in which case we return KOSM_HANDLE_INVALID.
	if (ref->peer != NULL) {
		KosmHandleTable* table = KosmHandleTable::TableForCurrent();
		if (table != NULL) {
			info.peer = table->FindHandleForObject(ref->peer,
				KOSM_HANDLE_RAY);
		} else {
			info.peer = KOSM_HANDLE_INVALID;
		}
	} else {
		info.peer = KOSM_HANDLE_INVALID;
	}

	locker.Unlock();

	if (user_memcpy(userInfo, &info, sizeof(kosm_ray_info)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}
