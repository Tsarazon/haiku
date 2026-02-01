/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmOS kernel primitives.
 * Includes OS.h for Haiku compatibility, plus all KOSM extensions.
 */
#ifndef _KOSM_OS_H
#define _KOSM_OS_H

#include <OS.h>

#ifdef __cplusplus
extern "C" {
#endif


/* KosmOS Robust Mutex */

typedef int32 kosm_mutex_id;

#define KOSM_MUTEX_SHARED			0x0001
#define KOSM_MUTEX_RECURSIVE		0x0002
#define KOSM_MUTEX_PRIO_INHERIT		0x0004

enum {
	KOSM_MUTEX_OWNER_DEAD			= B_ERRORS_END + 0x2000,
	KOSM_MUTEX_NOT_OWNER,
	KOSM_MUTEX_NOT_RECOVERABLE,
	KOSM_MUTEX_DEADLOCK
};

typedef struct {
	kosm_mutex_id	mutex;
	team_id			team;
	char			name[B_OS_NAME_LENGTH];
	thread_id		holder;
	int32			recursion;
	uint32			flags;
} kosm_mutex_info;

extern kosm_mutex_id	kosm_create_mutex(const char* name, uint32 flags);
extern status_t			kosm_delete_mutex(kosm_mutex_id id);
extern kosm_mutex_id	kosm_find_mutex(const char* name);

extern status_t			kosm_acquire_mutex(kosm_mutex_id id);
extern status_t			kosm_try_acquire_mutex(kosm_mutex_id id);
extern status_t			kosm_acquire_mutex_etc(kosm_mutex_id id, uint32 flags,
							bigtime_t timeout);
extern status_t			kosm_release_mutex(kosm_mutex_id id);

extern status_t			kosm_mark_mutex_consistent(kosm_mutex_id id);

/* system private, use macro instead */
extern status_t			_kosm_get_mutex_info(kosm_mutex_id id,
							kosm_mutex_info* info, size_t size);

#define kosm_get_mutex_info(id, info) \
	_kosm_get_mutex_info((id), (info), sizeof(*(info)))


#ifdef __cplusplus
}
#endif

#endif
