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
	KOSM_MUTEX_DEADLOCK,
	KOSM_MUTEX_NO_MORE
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

/* system private, use macros instead */
extern status_t			_kosm_get_mutex_info(kosm_mutex_id id,
							kosm_mutex_info* info, size_t size);
extern status_t			_kosm_get_next_mutex_info(team_id team, int32* cookie,
							kosm_mutex_info* info, size_t size);

#define kosm_get_mutex_info(id, info) \
	_kosm_get_mutex_info((id), (info), sizeof(*(info)))

#define kosm_get_next_mutex_info(team, cookie, info) \
	_kosm_get_next_mutex_info((team), (cookie), (info), sizeof(*(info)))


/* Generic handle infrastructure */

typedef int32 kosm_handle_t;

#define KOSM_HANDLE_INVALID			0

/* Handle object types */

#define KOSM_HANDLE_RAY				0x01
#define KOSM_HANDLE_MUTEX			0x02
#define KOSM_HANDLE_AREA			0x03
#define KOSM_HANDLE_SEM				0x04
#define KOSM_HANDLE_FD				0x05

/* Handle rights (bitmask, passed to kosm_duplicate_handle) */

#define KOSM_RIGHT_READ				0x0001
#define KOSM_RIGHT_WRITE			0x0002
#define KOSM_RIGHT_TRANSFER			0x0004
#define KOSM_RIGHT_DUPLICATE		0x0008
#define KOSM_RIGHT_WAIT				0x0010
#define KOSM_RIGHT_MANAGE			0x0020
#define KOSM_RIGHT_ALL				(KOSM_RIGHT_READ | KOSM_RIGHT_WRITE \
									| KOSM_RIGHT_TRANSFER \
									| KOSM_RIGHT_DUPLICATE \
									| KOSM_RIGHT_WAIT | KOSM_RIGHT_MANAGE)

typedef struct {
	kosm_handle_t	handle;
	uint32			type;
	uint32			rights;
} kosm_handle_info;

/* Generic handle operations */

extern status_t			kosm_close_handle(kosm_handle_t handle);
extern kosm_handle_t	kosm_duplicate_handle(kosm_handle_t handle,
							uint32 rights);
extern status_t			_kosm_get_handle_info(kosm_handle_t handle,
							kosm_handle_info* info);

#define kosm_get_handle_info(handle, info) \
	_kosm_get_handle_info((handle), (info))


/* KosmOS Ray -- paired IPC channels */

/* kosm_ray_id is a handle to a ray endpoint (per-process, opaque). */
typedef int32 kosm_ray_id;

#define KOSM_RAY_MAX_HANDLES		64
#define KOSM_RAY_MAX_DATA_SIZE		(256 * 1024)
#define KOSM_RAY_MAX_QUEUE_MESSAGES	256

/* QoS classes */

#define KOSM_QOS_DEFAULT			0
#define KOSM_QOS_UTILITY			1
#define KOSM_QOS_USER_INITIATED		2
#define KOSM_QOS_USER_INTERACTIVE	3

/* Wait signals for kosm_ray_wait() */

#define KOSM_RAY_READABLE			0x0001
#define KOSM_RAY_WRITABLE			0x0002
#define KOSM_RAY_PEER_CLOSED		0x0004

/* Flags for read/write */

#define KOSM_RAY_PEEK				0x0100
#define KOSM_RAY_COPY_HANDLES		0x0200

/* Error codes */

enum {
	KOSM_RAY_PEER_CLOSED_ERROR		= B_ERRORS_END + 0x3000,
	KOSM_RAY_INVALID_HANDLE,
	KOSM_RAY_TOO_LARGE,
	KOSM_RAY_TOO_MANY_HANDLES
};

typedef struct {
	kosm_ray_id		ray;
	team_id			team;
	kosm_ray_id		peer;
	uint32			queue_count;
	uint32			flags;
	uint8			qos_class;
} kosm_ray_info;

extern status_t			kosm_create_ray(kosm_ray_id* endpoint0,
							kosm_ray_id* endpoint1, uint32 flags);
extern status_t			kosm_close_ray(kosm_ray_id id);

extern status_t			kosm_ray_write(kosm_ray_id id,
							const void* data, size_t dataSize,
							const kosm_handle_t* handles, size_t handleCount,
							uint32 flags);
extern status_t			kosm_ray_write_etc(kosm_ray_id id,
							const void* data, size_t dataSize,
							const kosm_handle_t* handles, size_t handleCount,
							uint32 flags, bigtime_t timeout);

extern status_t			kosm_ray_read(kosm_ray_id id,
							void* data, size_t* dataSize,
							kosm_handle_t* handles, size_t* handleCount,
							uint32 flags);
extern status_t			kosm_ray_read_etc(kosm_ray_id id,
							void* data, size_t* dataSize,
							kosm_handle_t* handles, size_t* handleCount,
							uint32 flags, bigtime_t timeout);

extern status_t			kosm_ray_wait(kosm_ray_id id, uint32 signals,
							uint32* observedSignals, uint32 flags,
							bigtime_t timeout);

extern status_t			kosm_ray_set_qos(kosm_ray_id id, uint8 qosClass);

extern kosm_ray_id		kosm_get_bootstrap_ray(void);
extern status_t			kosm_ray_set_bootstrap(team_id team,
							kosm_ray_id endpoint);

/* system private, use macro instead */
extern status_t			_kosm_get_ray_info(kosm_ray_id id,
							kosm_ray_info* info, size_t size);

#define kosm_get_ray_info(id, info) \
	_kosm_get_ray_info((id), (info), sizeof(*(info)))


#ifdef __cplusplus
}
#endif

#endif
