/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Robust mutex implementation for cross-process synchronization.
 * Follows the same table/slot/spinlock architecture as sem.cpp.
 *
 * When a holder thread dies, the kernel automatically releases the
 * mutex and wakes the next waiter with KOSM_MUTEX_OWNER_DEAD status.
 */

#include <kosm_mutex.h>

#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <arch/int.h>
#include <debug.h>
#include <kernel.h>
#include <kscheduler.h>
#include <syscall_restart.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vm/vm_page.h>


// #define TRACE_KOSM_MUTEX
#ifdef TRACE_KOSM_MUTEX
#	define TRACE(x) dprintf_no_syslog x
#else
#	define TRACE(x) ;
#endif


enum {
	KOSM_MUTEX_STATE_NORMAL = 0,
	KOSM_MUTEX_STATE_NEEDS_RECOVERY,
	KOSM_MUTEX_STATE_NOT_RECOVERABLE
};


struct kosm_mutex_waiter : DoublyLinkedListLinkImpl<kosm_mutex_waiter> {
	kosm_mutex_waiter(Thread* thread)
		:
		thread(thread),
		queued(false)
	{
	}

	Thread*	thread;
	bool	queued;
};

typedef DoublyLinkedList<kosm_mutex_waiter> WaiterQueue;


struct kosm_mutex_entry {
	union {
		struct {
			struct list_link	team_link;
			char*				name;
			team_id				owner_team;
			thread_id			holder_thread;
			int32				recursion;
			uint32				creation_flags;
			int32				state;
		} used;

		struct {
			kosm_mutex_id		next_id;
			struct kosm_mutex_entry* next;
		} unused;
	} u;

	kosm_mutex_id				id;
	spinlock					lock;
	WaiterQueue					queue;

	// Held-list links (hlist pattern for O(1) insert/remove).
	// Links this entry into the holder thread's held_kosm_mutexes list.
	struct kosm_mutex_entry*	next_held;
	struct kosm_mutex_entry**	prev_held_ptr;
};


static const int32 kMaxMutexes = 16384;
static int32 sMaxMutexes = 2048;
static int32 sUsedMutexes = 0;

static struct kosm_mutex_entry* sMutexes = NULL;
static bool sMutexesActive = false;
static struct kosm_mutex_entry* sFreeMutexesHead = NULL;
static struct kosm_mutex_entry* sFreeMutexesTail = NULL;

static spinlock sMutexListSpinlock = B_SPINLOCK_INITIALIZER;


// Held-list helpers (hlist-style intrusive doubly linked list)

static inline void
held_list_add(Thread* thread, kosm_mutex_entry* entry)
{
	entry->next_held = thread->first_held_kosm_mutex;
	entry->prev_held_ptr = &thread->first_held_kosm_mutex;
	if (entry->next_held != NULL)
		entry->next_held->prev_held_ptr = &entry->next_held;
	thread->first_held_kosm_mutex = entry;
}


static inline void
held_list_remove(kosm_mutex_entry* entry)
{
	if (entry->prev_held_ptr == NULL)
		return;
	*entry->prev_held_ptr = entry->next_held;
	if (entry->next_held != NULL)
		entry->next_held->prev_held_ptr = entry->prev_held_ptr;
	entry->next_held = NULL;
	entry->prev_held_ptr = NULL;
}


// Slot management

static void
free_mutex_slot(int slot, kosm_mutex_id nextID)
{
	struct kosm_mutex_entry* entry = sMutexes + slot;
	if (nextID < 0)
		entry->u.unused.next_id = slot;
	else
		entry->u.unused.next_id = nextID;

	if (sFreeMutexesTail != NULL)
		sFreeMutexesTail->u.unused.next = entry;
	else
		sFreeMutexesHead = entry;
	sFreeMutexesTail = entry;
	entry->u.unused.next = NULL;
}


static void
fill_mutex_info(struct kosm_mutex_entry* entry, kosm_mutex_info* info)
{
	info->mutex = entry->id;
	info->team = entry->u.used.owner_team;
	strlcpy(info->name, entry->u.used.name, sizeof(info->name));
	info->holder = entry->u.used.holder_thread;
	info->recursion = entry->u.used.recursion;
	info->flags = entry->u.used.creation_flags;
}


/*!	Uninitializes a mutex slot. Must be called with interrupts disabled,
	the entry's spinlock held. Unlocks the spinlock before returning.
	Returns the name pointer for the caller to free().
*/
static void
uninit_mutex_locked(struct kosm_mutex_entry& entry, char** _name,
	SpinLocker& locker)
{
	// Wake all waiters with error
	while (kosm_mutex_waiter* waiter = entry.queue.RemoveHead()) {
		waiter->queued = false;
		thread_unblock(waiter->thread, B_BAD_VALUE);
	}

	// If held, remove from holder's list
	held_list_remove(&entry);

	int32 id = entry.id;
	entry.id = -1;
	*_name = entry.u.used.name;
	entry.u.used.name = NULL;

	locker.Unlock();

	SpinLocker _(&sMutexListSpinlock);
	free_mutex_slot(id % sMaxMutexes, id + sMaxMutexes);
	atomic_add(&sUsedMutexes, -1);
}


// Debug commands

static int
dump_kosm_mutex_list(int argc, char** argv)
{
	const char* name = NULL;
	team_id owner = -1;

	if (argc > 2) {
		if (!strcmp(argv[1], "team") || !strcmp(argv[1], "owner"))
			owner = strtoul(argv[2], NULL, 0);
		else if (!strcmp(argv[1], "name"))
			name = argv[2];
	} else if (argc > 1) {
		owner = strtoul(argv[1], NULL, 0);
	}

	kprintf("%-*s       id   team  holder  rec  state  name\n",
		B_PRINTF_POINTER_WIDTH, "mutex");

	for (int32 i = 0; i < sMaxMutexes; i++) {
		struct kosm_mutex_entry* entry = &sMutexes[i];
		if (entry->id < 0)
			continue;
		if (name != NULL && strstr(entry->u.used.name, name) == NULL)
			continue;
		if (owner != -1 && entry->u.used.owner_team != owner)
			continue;

		const char* stateStr = "ok";
		if (entry->u.used.state == KOSM_MUTEX_STATE_NEEDS_RECOVERY)
			stateStr = "DEAD";
		else if (entry->u.used.state == KOSM_MUTEX_STATE_NOT_RECOVERABLE)
			stateStr = "LOST";

		kprintf("%p %6" B_PRId32 " %6" B_PRId32 " %6" B_PRId32
			" %4" B_PRId32 "  %-5s  %s\n",
			entry, entry->id, entry->u.used.owner_team,
			entry->u.used.holder_thread, entry->u.used.recursion,
			stateStr, entry->u.used.name);
	}

	return 0;
}


static int
dump_kosm_mutex_info(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	char* endptr;
	addr_t num = strtoul(argv[1], &endptr, 0);

	if (endptr == argv[1]) {
		// Search by name
		for (int32 i = 0; i < sMaxMutexes; i++) {
			if (sMutexes[i].id >= 0 && sMutexes[i].u.used.name != NULL
				&& strcmp(argv[1], sMutexes[i].u.used.name) == 0) {
				num = (addr_t)&sMutexes[i];
				break;
			}
		}
		if (endptr == argv[1]) {
			kprintf("kosm_mutex \"%s\" not found\n", argv[1]);
			return 0;
		}
	}

	struct kosm_mutex_entry* entry;
	if (IS_KERNEL_ADDRESS(num)) {
		entry = (struct kosm_mutex_entry*)num;
	} else {
		int32 slot = num % sMaxMutexes;
		if (sMutexes[slot].id != (int32)num) {
			kprintf("kosm_mutex %" B_PRId32 " doesn't exist\n", (int32)num);
			return 0;
		}
		entry = &sMutexes[slot];
	}

	kprintf("KOSM_MUTEX: %p\n", entry);
	kprintf("id:        %" B_PRId32 "\n", entry->id);
	if (entry->id >= 0) {
		kprintf("name:      '%s'\n", entry->u.used.name);
		kprintf("owner:     %" B_PRId32 "\n", entry->u.used.owner_team);
		kprintf("holder:    %" B_PRId32 "\n", entry->u.used.holder_thread);
		kprintf("recursion: %" B_PRId32 "\n", entry->u.used.recursion);
		kprintf("flags:     0x%" B_PRIx32 "\n", entry->u.used.creation_flags);
		kprintf("state:     %s\n",
			entry->u.used.state == KOSM_MUTEX_STATE_NORMAL ? "normal" :
			entry->u.used.state == KOSM_MUTEX_STATE_NEEDS_RECOVERY ? "needs_recovery" :
			"not_recoverable");

		kprintf("queue:    ");
		if (!entry->queue.IsEmpty()) {
			WaiterQueue::Iterator it = entry->queue.GetIterator();
			while (kosm_mutex_waiter* waiter = it.Next())
				kprintf(" %" B_PRId32, waiter->thread->id);
			kprintf("\n");
		} else {
			kprintf(" -\n");
		}
	}

	return 0;
}


//	#pragma mark - Private Kernel API


status_t
kosm_mutex_init(kernel_args* args)
{
	TRACE(("kosm_mutex_init: entry\n"));

	// Scale table size with available memory (more conservatively than sems)
	int32 pages = vm_page_num_pages() / 4;
	while (sMaxMutexes < pages && sMaxMutexes < kMaxMutexes)
		sMaxMutexes <<= 1;

	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};

	area_id area = create_area_etc(B_SYSTEM_TEAM, "kosm_mutex_table",
		sizeof(struct kosm_mutex_entry) * sMaxMutexes, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, CREATE_AREA_DONT_WAIT, 0,
		&virtualRestrictions, &physicalRestrictions, (void**)&sMutexes);
	if (area < 0)
		panic("unable to allocate kosm_mutex table!\n");

	memset(sMutexes, 0, sizeof(struct kosm_mutex_entry) * sMaxMutexes);
	for (int32 i = 0; i < sMaxMutexes; i++) {
		sMutexes[i].id = -1;
		sMutexes[i].next_held = NULL;
		sMutexes[i].prev_held_ptr = NULL;
		free_mutex_slot(i, i);
	}

	add_debugger_command_etc("kosm_mutexes", &dump_kosm_mutex_list,
		"List active KosmOS mutexes",
		"[ ([ \"team\" | \"owner\" ] <team>) | (\"name\" <name>) ]\n"
		"Lists all active kosm mutexes matching the filter.\n", 0);
	add_debugger_command_etc("kosm_mutex", &dump_kosm_mutex_info,
		"Dump info about a KosmOS mutex",
		"<mutex>\n"
		"Prints info for the specified kosm mutex (ID, pointer, or name).\n",
		0);

	sMutexesActive = true;

	TRACE(("kosm_mutex_init: exit\n"));
	return B_OK;
}


kosm_mutex_id
kosm_create_mutex_etc(const char* name, uint32 flags, team_id owner)
{
	if (!sMutexesActive || sUsedMutexes == sMaxMutexes)
		return B_NO_MORE_SEMS;

	if (name == NULL)
		name = "unnamed kosm_mutex";

	Team* team = Team::Get(owner);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	size_t nameLength = strlen(name) + 1;
	nameLength = min_c(nameLength, (size_t)B_OS_NAME_LENGTH);
	char* tempName = (char*)malloc(nameLength);
	if (tempName == NULL)
		return B_NO_MEMORY;
	strlcpy(tempName, name, nameLength);

	InterruptsSpinLocker _(&sMutexListSpinlock);

	struct kosm_mutex_entry* entry = sFreeMutexesHead;
	kosm_mutex_id id = B_NO_MORE_SEMS;

	if (entry != NULL) {
		sFreeMutexesHead = entry->u.unused.next;
		if (sFreeMutexesHead == NULL)
			sFreeMutexesTail = NULL;

		SpinLocker entryLocker(entry->lock);
		entry->id = entry->u.unused.next_id;
		entry->u.used.name = tempName;
		entry->u.used.owner_team = team->id;
		entry->u.used.holder_thread = -1;
		entry->u.used.recursion = 0;
		entry->u.used.creation_flags = flags;
		entry->u.used.state = KOSM_MUTEX_STATE_NORMAL;
		new(&entry->queue) WaiterQueue;
		entry->next_held = NULL;
		entry->prev_held_ptr = NULL;
		id = entry->id;

		list_add_item(&team->kosm_mutex_list, &entry->u.used.team_link);

		entryLocker.Unlock();

		atomic_add(&sUsedMutexes, 1);

		TRACE(("kosm_create_mutex_etc(name: %s, owner: %ld) -> %ld\n",
			name, owner, id));
	}

	if (entry == NULL)
		free(tempName);

	return id;
}


static status_t
delete_mutex_internal(kosm_mutex_id id, bool checkPermission)
{
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_VALUE;

	int32 slot = id % sMaxMutexes;

	InterruptsLocker interruptsLocker;
	SpinLocker listLocker(sMutexListSpinlock);
	SpinLocker entryLocker(sMutexes[slot].lock);

	if (sMutexes[slot].id != id)
		return B_BAD_VALUE;

	if (checkPermission
		&& sMutexes[slot].u.used.owner_team == team_get_kernel_team_id()) {
		dprintf("thread %" B_PRId32 " tried to delete kernel kosm_mutex "
			"%" B_PRId32 "\n", thread_get_current_thread_id(), id);
		return B_NOT_ALLOWED;
	}

	if (sMutexes[slot].u.used.owner_team >= 0) {
		list_remove_link(&sMutexes[slot].u.used.team_link);
		sMutexes[slot].u.used.owner_team = -1;
	}

	listLocker.Unlock();

	char* name;
	uninit_mutex_locked(sMutexes[slot], &name, entryLocker);

	SpinLocker schedulerLocker(thread_get_current_thread()->scheduler_lock);
	scheduler_reschedule_if_necessary_locked();
	schedulerLocker.Unlock();

	interruptsLocker.Unlock();

	free(name);
	return B_OK;
}


/*!	Called when a thread is being destroyed. Releases all mutexes held by
	the thread and wakes waiters with KOSM_MUTEX_OWNER_DEAD.
*/
void
kosm_mutex_release_owned(Thread* thread)
{
	InterruptsLocker interruptsLocker;

	while (thread->first_held_kosm_mutex != NULL) {
		kosm_mutex_entry* entry = thread->first_held_kosm_mutex;

		SpinLocker entryLocker(entry->lock);

		// Verify this mutex is still held by this thread (might have been
		// deleted between our check and acquiring the lock)
		if (entry->id < 0 || entry->u.used.holder_thread != thread->id) {
			held_list_remove(entry);
			continue;
		}

		entry->u.used.state = KOSM_MUTEX_STATE_NEEDS_RECOVERY;
		entry->u.used.recursion = 0;
		held_list_remove(entry);

		// Transfer to first blocked waiter, if any
		bool transferred = false;
		while (kosm_mutex_waiter* waiter = entry->queue.Head()) {
			SpinLocker schedulerLocker(waiter->thread->scheduler_lock);
			if (thread_is_blocked(waiter->thread)) {
				entry->queue.Remove(waiter);
				waiter->queued = false;

				entry->u.used.holder_thread = waiter->thread->id;
				entry->u.used.recursion = 1;
				held_list_add(waiter->thread, entry);

				thread_unblock_locked(waiter->thread,
					KOSM_MUTEX_OWNER_DEAD);
				transferred = true;
				break;
			}
			// Waiter timed out or was interrupted, skip
			entry->queue.Remove(waiter);
			waiter->queued = false;
		}

		if (!transferred)
			entry->u.used.holder_thread = -1;
	}

	scheduler_reschedule_if_necessary();
}


/*!	Called when a team is being destroyed. Deletes all mutexes owned
	by the team.
*/
void
kosm_mutex_delete_owned(Team* team)
{
	while (true) {
		char* name;

		{
			InterruptsLocker _;
			SpinLocker listLocker(sMutexListSpinlock);
			kosm_mutex_entry* entry = (kosm_mutex_entry*)
				list_remove_head_item(&team->kosm_mutex_list);
			if (entry == NULL)
				break;

			SpinLocker entryLocker(entry->lock);
			listLocker.Unlock();
			uninit_mutex_locked(*entry, &name, entryLocker);
		}

		free(name);
	}

	scheduler_reschedule_if_necessary();
}


int32
kosm_mutex_max(void)
{
	return sMaxMutexes;
}


int32
kosm_mutex_used(void)
{
	return sUsedMutexes;
}


off_t
kosm_mutex_team_link_offset(void)
{
	return offsetof(kosm_mutex_entry, u.used.team_link);
}


//	#pragma mark - Public Kernel API


kosm_mutex_id
kosm_create_mutex(const char* name, uint32 flags)
{
	return kosm_create_mutex_etc(name, flags, team_get_kernel_team_id());
}


status_t
kosm_delete_mutex(kosm_mutex_id id)
{
	return delete_mutex_internal(id, false);
}


kosm_mutex_id
kosm_find_mutex(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;

	InterruptsLocker _;

	for (int32 i = 0; i < sMaxMutexes; i++) {
		SpinLocker entryLocker(sMutexes[i].lock);
		if (sMutexes[i].id >= 0 && sMutexes[i].u.used.name != NULL
			&& strcmp(name, sMutexes[i].u.used.name) == 0) {
			return sMutexes[i].id;
		}
	}

	return B_NAME_NOT_FOUND;
}


status_t
kosm_acquire_mutex(kosm_mutex_id id)
{
	return kosm_acquire_mutex_etc(id, 0, 0);
}


status_t
kosm_acquire_mutex_etc(kosm_mutex_id id, uint32 flags, bigtime_t timeout)
{
	int32 slot = id % sMaxMutexes;

	if (gKernelStartup)
		return B_OK;
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_VALUE;
	if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT))
		== (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) {
		return B_BAD_VALUE;
	}

	InterruptsLocker _;
	SpinLocker entryLocker(sMutexes[slot].lock);

	if (sMutexes[slot].id != id) {
		TRACE(("kosm_acquire_mutex: bad id %" B_PRId32 "\n", id));
		return B_BAD_VALUE;
	}

	struct kosm_mutex_entry& entry = sMutexes[slot];

	if ((flags & B_CHECK_PERMISSION) != 0
		&& entry.u.used.owner_team == team_get_kernel_team_id()) {
		dprintf("thread %" B_PRId32 " tried to acquire kernel kosm_mutex "
			"%" B_PRId32 "\n", thread_get_current_thread_id(), id);
		return B_NOT_ALLOWED;
	}

	thread_id currentThread = thread_get_current_thread_id();

	// Case 1: mutex not held
	if (entry.u.used.holder_thread < 0) {
		if (entry.u.used.state == KOSM_MUTEX_STATE_NOT_RECOVERABLE)
			return KOSM_MUTEX_NOT_RECOVERABLE;

		entry.u.used.holder_thread = currentThread;
		entry.u.used.recursion = 1;
		held_list_add(thread_get_current_thread(), &entry);

		if (entry.u.used.state == KOSM_MUTEX_STATE_NEEDS_RECOVERY)
			return KOSM_MUTEX_OWNER_DEAD;

		return B_OK;
	}

	// Case 2: already held by current thread
	if (entry.u.used.holder_thread == currentThread) {
		if ((entry.u.used.creation_flags & KOSM_MUTEX_RECURSIVE) != 0) {
			entry.u.used.recursion++;
			return B_OK;
		}
		return KOSM_MUTEX_DEADLOCK;
	}

	// Case 3: held by another thread, need to block
	if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
		return B_WOULD_BLOCK;
	if ((flags & B_ABSOLUTE_TIMEOUT) != 0 && timeout < 0)
		return B_TIMED_OUT;

	Thread* thread = thread_get_current_thread();

	// Check for pending signals
	SpinLocker schedulerLocker(thread->scheduler_lock);
	if (thread_is_interrupted(thread, flags)) {
		schedulerLocker.Unlock();
		return B_INTERRUPTED;
	}
	schedulerLocker.Unlock();

	if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == 0)
		timeout = B_INFINITE_TIMEOUT;

	kosm_mutex_waiter waiter(thread);
	entry.queue.Add(&waiter);
	waiter.queued = true;

	thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_KOSM_MUTEX,
		(void*)(addr_t)id);

	entryLocker.Unlock();

	status_t acquireStatus = timeout == B_INFINITE_TIMEOUT
		? thread_block() : thread_block_with_timeout(flags, timeout);

	entryLocker.Lock();

	if (waiter.queued) {
		// Acquisition failed (timeout, interrupt, or mutex deleted).
		// Remove ourselves from the queue.
		entry.queue.Remove(&waiter);
		waiter.queued = false;
	}
	// If !queued, the release path already transferred ownership to us
	// and acquireStatus is B_OK or KOSM_MUTEX_OWNER_DEAD.

	entryLocker.Unlock();

	TRACE(("kosm_acquire_mutex(%" B_PRId32 "): exit, status 0x%" B_PRIx32 "\n",
		id, acquireStatus));
	return acquireStatus;
}


status_t
kosm_release_mutex(kosm_mutex_id id)
{
	int32 slot = id % sMaxMutexes;

	if (gKernelStartup)
		return B_OK;
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_VALUE;

	InterruptsLocker _;
	SpinLocker entryLocker(sMutexes[slot].lock);

	if (sMutexes[slot].id != id)
		return B_BAD_VALUE;

	struct kosm_mutex_entry& entry = sMutexes[slot];

	if (entry.u.used.holder_thread != thread_get_current_thread_id())
		return KOSM_MUTEX_NOT_OWNER;

	// Recursive unlock
	entry.u.used.recursion--;
	if (entry.u.used.recursion > 0)
		return B_OK;

	// Full release
	held_list_remove(&entry);

	// If released without marking consistent after OWNER_DEAD,
	// the mutex becomes not recoverable.
	if (entry.u.used.state == KOSM_MUTEX_STATE_NEEDS_RECOVERY) {
		entry.u.used.state = KOSM_MUTEX_STATE_NOT_RECOVERABLE;
		entry.u.used.holder_thread = -1;
		entry.u.used.recursion = 0;

		// Wake all waiters with NOT_RECOVERABLE
		while (kosm_mutex_waiter* waiter = entry.queue.RemoveHead()) {
			waiter->queued = false;
			thread_unblock(waiter->thread, KOSM_MUTEX_NOT_RECOVERABLE);
		}

		return B_OK;
	}

	// Normal release: transfer to first blocked waiter
	while (kosm_mutex_waiter* waiter = entry.queue.Head()) {
		SpinLocker schedulerLocker(waiter->thread->scheduler_lock);

		if (thread_is_blocked(waiter->thread)) {
			entry.queue.Remove(waiter);
			waiter->queued = false;

			entry.u.used.holder_thread = waiter->thread->id;
			entry.u.used.recursion = 1;
			held_list_add(waiter->thread, &entry);

			thread_unblock_locked(waiter->thread, B_OK);
			goto done;
		}

		// Waiter no longer blocked (timed out or interrupted), skip
		entry.queue.Remove(waiter);
		waiter->queued = false;
	}

	// No waiters, mutex is now free
	entry.u.used.holder_thread = -1;
	entry.u.used.recursion = 0;

done:
	if ((entry.u.used.creation_flags & B_DO_NOT_RESCHEDULE) == 0) {
		entryLocker.Unlock();
		SpinLocker schedulerLocker(
			thread_get_current_thread()->scheduler_lock);
		scheduler_reschedule_if_necessary_locked();
	}

	return B_OK;
}


status_t
kosm_mark_mutex_consistent(kosm_mutex_id id)
{
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_VALUE;

	int32 slot = id % sMaxMutexes;

	InterruptsSpinLocker _(sMutexes[slot].lock);

	if (sMutexes[slot].id != id)
		return B_BAD_VALUE;

	struct kosm_mutex_entry& entry = sMutexes[slot];

	if (entry.u.used.holder_thread != thread_get_current_thread_id())
		return KOSM_MUTEX_NOT_OWNER;

	if (entry.u.used.state == KOSM_MUTEX_STATE_NOT_RECOVERABLE)
		return KOSM_MUTEX_NOT_RECOVERABLE;

	entry.u.used.state = KOSM_MUTEX_STATE_NORMAL;
	return B_OK;
}


status_t
_kosm_get_mutex_info(kosm_mutex_id id, kosm_mutex_info* info, size_t size)
{
	if (!sMutexesActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_VALUE;
	if (info == NULL || size != sizeof(kosm_mutex_info))
		return B_BAD_VALUE;

	int32 slot = id % sMaxMutexes;

	InterruptsSpinLocker _(sMutexes[slot].lock);

	if (sMutexes[slot].id != id)
		return B_BAD_VALUE;

	fill_mutex_info(&sMutexes[slot], info);
	return B_OK;
}


//	#pragma mark - Syscalls


kosm_mutex_id
_user_kosm_create_mutex(const char* userName, uint32 flags)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return kosm_create_mutex_etc(NULL, flags, team_get_current_team_id());

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return kosm_create_mutex_etc(name, flags, team_get_current_team_id());
}


status_t
_user_kosm_delete_mutex(kosm_mutex_id id)
{
	return delete_mutex_internal(id, true);
}


kosm_mutex_id
_user_kosm_find_mutex(const char* userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return kosm_find_mutex(name);
}


status_t
_user_kosm_acquire_mutex(kosm_mutex_id id)
{
	status_t error = kosm_acquire_mutex_etc(id,
		B_CAN_INTERRUPT | B_CHECK_PERMISSION, 0);

	return syscall_restart_handle_post(error);
}


status_t
_user_kosm_acquire_mutex_etc(kosm_mutex_id id, uint32 flags,
	bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t error = kosm_acquire_mutex_etc(id,
		flags | B_CAN_INTERRUPT | B_CHECK_PERMISSION, timeout);

	return syscall_restart_handle_timeout_post(error, timeout);
}


status_t
_user_kosm_release_mutex(kosm_mutex_id id)
{
	return kosm_release_mutex(id);
}


status_t
_user_kosm_mark_mutex_consistent(kosm_mutex_id id)
{
	return kosm_mark_mutex_consistent(id);
}


status_t
_user_kosm_get_mutex_info(kosm_mutex_id id, kosm_mutex_info* userInfo,
	size_t size)
{
	kosm_mutex_info info;

	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status_t status = _kosm_get_mutex_info(id, &info, size);

	if (status == B_OK && user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}
