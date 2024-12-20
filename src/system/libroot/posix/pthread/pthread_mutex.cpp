/*
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <pthread.h>
#include "pthread_private.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syscalls.h>
#include <user_mutex_defs.h>
#include <time_private.h>


#define MUTEX_TYPE_BITS		0x0000000f
#define MUTEX_TYPE(mutex)	((mutex)->flags & MUTEX_TYPE_BITS)


static const pthread_mutexattr pthread_mutexattr_default = {
	PTHREAD_MUTEX_DEFAULT,
	false
};


int
pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* _attr)
{
	const pthread_mutexattr* attr = _attr != NULL
		? *_attr : &pthread_mutexattr_default;

	mutex->lock = 0;
	mutex->owner = -1;
	mutex->owner_count = 0;
	mutex->flags = attr->type | (attr->process_shared ? MUTEX_FLAG_SHARED : 0);

	return 0;
}


int
pthread_mutex_destroy(pthread_mutex_t* mutex)
{
	return 0;
}


status_t
__pthread_mutex_lock(pthread_mutex_t* mutex, uint32 flags, bigtime_t timeout)
{
	thread_id thisThread = find_thread(NULL);

	if (mutex->owner == thisThread) {
		// recursive locking handling
		if (MUTEX_TYPE(mutex) == PTHREAD_MUTEX_RECURSIVE) {
			if (mutex->owner_count == INT32_MAX)
				return EAGAIN;

			mutex->owner_count++;
			return 0;
		}

		// deadlock check (not for PTHREAD_MUTEX_NORMAL as per the specs)
		if (MUTEX_TYPE(mutex) == PTHREAD_MUTEX_ERRORCHECK
			|| MUTEX_TYPE(mutex) == PTHREAD_MUTEX_DEFAULT) {
			// we detect this kind of deadlock and return an error
			return timeout < 0 ? EBUSY : EDEADLK;
		}
	}

	// set the locked flag
	const int32 oldValue = atomic_test_and_set((int32*)&mutex->lock, B_USER_MUTEX_LOCKED, 0);
	if (oldValue != 0) {
		// someone else has the lock or is at least waiting for it
		if (timeout < 0)
			return EBUSY;
		if ((mutex->flags & MUTEX_FLAG_SHARED) != 0)
			flags |= B_USER_MUTEX_SHARED;

		// we have to call the kernel
		status_t error;
		do {
			error = _kern_mutex_lock((int32*)&mutex->lock, NULL, flags, timeout);
		} while (error == B_INTERRUPTED);

		if (error != B_OK)
			return error;
	}

	// we have locked the mutex for the first time
	assert(mutex->owner == -1);
	mutex->owner = thisThread;
	mutex->owner_count = 1;

	return 0;
}


int
pthread_mutex_lock(pthread_mutex_t* mutex)
{
	return __pthread_mutex_lock(mutex, 0, B_INFINITE_TIMEOUT);
}


int
pthread_mutex_trylock(pthread_mutex_t* mutex)
{
	return __pthread_mutex_lock(mutex, B_ABSOLUTE_REAL_TIME_TIMEOUT, -1);
}


int
pthread_mutex_clocklock(pthread_mutex_t* mutex, clockid_t clock_id,
	const struct timespec* abstime)
{
	bigtime_t timeout = 0;
	bool invalidTime = false;
	if (abstime == NULL || !timespec_to_bigtime(*abstime, timeout))
		invalidTime = true;

	uint32 flags = 0;
	switch (clock_id) {
		case CLOCK_REALTIME:
			flags = B_ABSOLUTE_REAL_TIME_TIMEOUT;
			break;
		case CLOCK_MONOTONIC:
			flags = B_ABSOLUTE_TIMEOUT;
			break;
		default:
			invalidTime = true;
			break;
	}

	status_t status = __pthread_mutex_lock(mutex, flags, timeout);

	if (status != B_OK && invalidTime)
		return EINVAL;
	return status;
}


int
pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* abstime)
{
	return pthread_mutex_clocklock(mutex, CLOCK_REALTIME, abstime);
}


int
pthread_mutex_unlock(pthread_mutex_t* mutex)
{
	if (mutex->owner != find_thread(NULL))
		return EPERM;

	if (MUTEX_TYPE(mutex) == PTHREAD_MUTEX_RECURSIVE
		&& --mutex->owner_count > 0) {
		// still locked
		return 0;
	}

	mutex->owner = -1;

	// clear the locked flag
	int32 oldValue = atomic_and((int32*)&mutex->lock,
		~(int32)B_USER_MUTEX_LOCKED);
	if ((oldValue & B_USER_MUTEX_WAITING) != 0) {
		_kern_mutex_unblock((int32*)&mutex->lock,
			(mutex->flags & MUTEX_FLAG_SHARED) ? B_USER_MUTEX_SHARED : 0);
	}

	if (MUTEX_TYPE(mutex) == PTHREAD_MUTEX_ERRORCHECK
		|| MUTEX_TYPE(mutex) == PTHREAD_MUTEX_DEFAULT) {
		if ((oldValue & B_USER_MUTEX_LOCKED) == 0)
			return EPERM;
	}

	return 0;
}


int
pthread_mutex_getprioceiling(const pthread_mutex_t* mutex, int* _prioCeiling)
{
	if (mutex == NULL || _prioCeiling == NULL)
		return EINVAL;

	*_prioCeiling = 0;
		// not implemented

	return 0;
}


int
pthread_mutex_setprioceiling(pthread_mutex_t* mutex, int prioCeiling,
	int* _oldCeiling)
{
	if (mutex == NULL)
		return EINVAL;

	// not implemented
	return EPERM;
}
