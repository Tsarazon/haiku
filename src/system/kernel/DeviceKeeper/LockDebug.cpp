/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "LockDebug.h"

#if defined(DEVICE_KEEPER_LOCK_DEBUG) && DEVICE_KEEPER_LOCK_DEBUG

#include <new>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <KernelExport.h>
#include <lock.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/OpenHashTable.h>


// Per-thread lock stack stored in a hash table keyed on thread_id.
// Each entry holds a small stack of DkLockLevel values; acquiring a
// lock pushes, releasing pops. The invariant is that the stack is
// strictly increasing — any violation triggers a panic.

namespace {

struct ThreadLockState {
	thread_id		tid;
	int32			depth;
	int32			stack[16];
	const char*		names[16];
	ThreadLockState* hash_link;
};


struct ThreadLockHashDefinition {
	typedef thread_id		KeyType;
	typedef ThreadLockState	ValueType;

	size_t HashKey(thread_id key) const { return (size_t)key * 2654435761U; }
	size_t Hash(const ThreadLockState* state) const { return HashKey(state->tid); }
	bool Compare(thread_id key, const ThreadLockState* state) const
		{ return state->tid == key; }
	ThreadLockState*& GetLink(ThreadLockState* state) const
		{ return state->hash_link; }
};


static mutex sTableLock = MUTEX_INITIALIZER("dk lock debug");
static BOpenHashTable<ThreadLockHashDefinition, false>* sTable = NULL;


ThreadLockState*
_get_or_create_state(thread_id tid)
{
	if (sTable == NULL) {
		sTable = new(std::nothrow) BOpenHashTable<ThreadLockHashDefinition, false>();
		if (sTable == NULL || sTable->Init(32) != B_OK) {
			delete sTable;
			sTable = NULL;
			return NULL;
		}
	}

	ThreadLockState* state = sTable->Lookup(tid);
	if (state != NULL)
		return state;

	state = new(std::nothrow) ThreadLockState;
	if (state == NULL)
		return NULL;
	state->tid = tid;
	state->depth = 0;
	memset(state->stack, 0, sizeof(state->stack));
	memset(state->names, 0, sizeof(state->names));
	sTable->Insert(state);
	return state;
}


void
_drop_state_if_empty(ThreadLockState* state)
{
	if (state->depth == 0) {
		sTable->Remove(state);
		delete state;
	}
}

} // namespace


void
dk_lock_debug_enter(int32 level, const char* name)
{
	thread_id tid = thread_get_current_thread_id();

	MutexLocker locker(sTableLock);
	ThreadLockState* state = _get_or_create_state(tid);
	if (state == NULL)
		return;	// OOM — silently skip tracking

	// Order check: new level must be strictly greater than the
	// topmost held level (if any). Equal levels are allowed only
	// between different instances at the same level (e.g. two
	// per-node locks on different nodes) — we cannot distinguish
	// those without instance IDs, so relax the check for level ==
	// previous to allow same-level chains.
	if (state->depth > 0) {
		int32 top = state->stack[state->depth - 1];
		if (level < top) {
			dprintf("DK LOCK ORDER VIOLATION: thread %" B_PRId32
				" holds level %" B_PRId32 " (%s), acquiring level %"
				B_PRId32 " (%s)\n",
				tid, top,
				state->names[state->depth - 1] != NULL
					? state->names[state->depth - 1] : "?",
				level, name != NULL ? name : "?");
			for (int32 i = 0; i < state->depth; i++) {
				dprintf("  [%d] level %" B_PRId32 " %s\n",
					(int)i, state->stack[i],
					state->names[i] != NULL ? state->names[i] : "?");
			}
			panic("DK: lock-order violation");
		}
	}

	if (state->depth >= (int32)(sizeof(state->stack) / sizeof(state->stack[0]))) {
		panic("DK: lock stack overflow (depth > %d)",
			(int)(sizeof(state->stack) / sizeof(state->stack[0])));
	}

	state->stack[state->depth] = level;
	state->names[state->depth] = name;
	state->depth++;
}


void
dk_lock_debug_exit(int32 level)
{
	thread_id tid = thread_get_current_thread_id();

	MutexLocker locker(sTableLock);
	if (sTable == NULL)
		return;
	ThreadLockState* state = sTable->Lookup(tid);
	if (state == NULL)
		return;

	// LIFO check: the topmost entry must match the level being released.
	if (state->depth == 0) {
		panic("DK: lock exit with empty stack (level %d)", (int)level);
	}
	int32 top = state->stack[state->depth - 1];
	if (top != level) {
		dprintf("DK: lock exit mismatch — top level %" B_PRId32
			", releasing %" B_PRId32 "\n", top, level);
		panic("DK: lock stack mismatch");
	}

	state->depth--;
	state->stack[state->depth] = 0;
	state->names[state->depth] = NULL;
	_drop_state_if_empty(state);
}


int32
dk_lock_debug_current_level()
{
	thread_id tid = thread_get_current_thread_id();

	MutexLocker locker(sTableLock);
	if (sTable == NULL)
		return 0;
	ThreadLockState* state = sTable->Lookup(tid);
	if (state == NULL || state->depth == 0)
		return 0;
	return state->stack[state->depth - 1];
}


bool
dk_lock_debug_any_held()
{
	return dk_lock_debug_current_level() > 0;
}


void
dk_lock_debug_dump_current()
{
	thread_id tid = thread_get_current_thread_id();

	MutexLocker locker(sTableLock);
	if (sTable == NULL) {
		dprintf("DK: lock debug table not initialized\n");
		return;
	}
	ThreadLockState* state = sTable->Lookup(tid);
	if (state == NULL || state->depth == 0) {
		dprintf("DK: thread %" B_PRId32 " holds no DK locks\n", tid);
		return;
	}
	dprintf("DK: thread %" B_PRId32 " holds %" B_PRId32 " DK locks:\n",
		tid, state->depth);
	for (int32 i = 0; i < state->depth; i++) {
		dprintf("  [%d] level %" B_PRId32 " %s\n",
			(int)i, state->stack[i],
			state->names[i] != NULL ? state->names[i] : "?");
	}
}

#endif	// DEVICE_KEEPER_LOCK_DEBUG
