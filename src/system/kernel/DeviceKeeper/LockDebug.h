/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_LOCK_DEBUG_H
#define DEVICE_KEEPER_LOCK_DEBUG_H

#include <SupportDefs.h>


// DeviceKeeper internal lock ordering.
//
// Locks must always be acquired in ascending level order. A thread
// that holds a lock at level N may only acquire locks at level > N.
// Any driver callback (attach/detach/probe/rescan_children) is
// invoked with NO DeviceKeeper locks held — level must be 0.
//
// To enable runtime verification, build with DEVICE_KEEPER_LOCK_DEBUG=1.
// This is cheap enough to enable in KDEBUG builds; it is a no-op in
// release.

enum DkLockLevel {
	DK_LL_NONE			= 0,
	DK_LL_RESOURCE		= 1,	// DkResourceAllocator, DkIdGenerator,
								// DkLifecycle::fDeferredLock
	DK_LL_NODE			= 2,	// DkNode::fLock
	DK_LL_TREE			= 3,	// DkKeeper::fTreeLock (rw)
	DK_LL_MATCHER		= 4,	// DkMatcher::fLock (rw)
	DK_LL_MAX			= 5
};


#if defined(DEVICE_KEEPER_LOCK_DEBUG) && DEVICE_KEEPER_LOCK_DEBUG

void	dk_lock_debug_enter(int32 level, const char* name);
void	dk_lock_debug_exit(int32 level);
int32	dk_lock_debug_current_level();
bool	dk_lock_debug_any_held();
void	dk_lock_debug_dump_current();

#define DK_LOCK_ENTER(level, name) \
	dk_lock_debug_enter((level), (name))
#define DK_LOCK_EXIT(level) \
	dk_lock_debug_exit((level))
#define DK_ASSERT_NO_LOCKS_HELD() \
	do { \
		if (dk_lock_debug_any_held()) { \
			dk_lock_debug_dump_current(); \
			panic("DK: locks held at safe-point (%s:%d)", \
				__FILE__, __LINE__); \
		} \
	} while (0)
#define DK_ASSERT_LOCK_LEVEL_BELOW(level) \
	do { \
		if (dk_lock_debug_current_level() >= (level)) { \
			dk_lock_debug_dump_current(); \
			panic("DK: lock level >= %d at %s:%d", \
				(int)(level), __FILE__, __LINE__); \
		} \
	} while (0)

#else

#define DK_LOCK_ENTER(level, name) do {} while (0)
#define DK_LOCK_EXIT(level) do {} while (0)
#define DK_ASSERT_NO_LOCKS_HELD() do {} while (0)
#define DK_ASSERT_LOCK_LEVEL_BELOW(level) do {} while (0)

#endif


#endif	// DEVICE_KEEPER_LOCK_DEBUG_H
