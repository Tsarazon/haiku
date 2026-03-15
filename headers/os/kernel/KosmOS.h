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
#define KOSM_HANDLE_DOT				0x06

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


/* kosm_dot -- independent memory region */
/*
 * kosm_dot is a fully independent VM primitive, parallel to Haiku area.
 * It owns its own VMCache, manages its own page fault path, and
 * reserves address ranges in VMAddressSpace independently of VMArea.
 *
 * Access is through per-process handles (kosm_dot_id is a handle, not
 * a global ID). Handles are transferred between processes via kosm_ray.
 * Closing a handle unmaps that process's mapping; the dot lives until
 * the last handle is closed.
 */

typedef int32 kosm_dot_id;

/* Creation flags */

#define KOSM_DOT_LAZY				0x0000
#define KOSM_DOT_WIRED				0x0001
#define KOSM_DOT_CONTIGUOUS			0x0002
#define KOSM_DOT_DEVICE				0x0004

#define KOSM_DOT_PURGEABLE			0x0100
#define KOSM_DOT_NOCLEAR			0x0200
#define KOSM_DOT_GUARD				0x0400
#define KOSM_DOT_STACK				0x0800
#define KOSM_DOT_RECLAIMABLE		0x1000
#define KOSM_DOT_FILE				0x2000

/* Protection bits (own namespace, independent of Haiku) */

#define KOSM_PROT_NONE				0x00
#define KOSM_PROT_READ				0x01
#define KOSM_PROT_WRITE				0x02
#define KOSM_PROT_EXEC				0x04

/* Cache policy hints */

#define KOSM_CACHE_DEFAULT			0
#define KOSM_CACHE_WRITECOMBINE		1
#define KOSM_CACHE_UNCACHED			2
#define KOSM_CACHE_WRITETHROUGH		3
#define KOSM_CACHE_DEVICE			4

/* Purgeable states */

#define KOSM_PURGE_NONVOLATILE		0
#define KOSM_PURGE_VOLATILE			1
#define KOSM_PURGE_EMPTY			2

/* Memory tags for per-subsystem accounting */

#define KOSM_TAG_NONE				0
#define KOSM_TAG_GRAPHICS			1
#define KOSM_TAG_UI					2
#define KOSM_TAG_MEDIA				3
#define KOSM_TAG_NETWORK			4
#define KOSM_TAG_APP				5
#define KOSM_TAG_HEAP				6
#define KOSM_TAG_STACK				7
#define KOSM_TAG_USER_BASE			0x1000

/* Error codes */

enum {
	KOSM_DOT_WAS_PURGED				= B_ERRORS_END + 0x4000,
	KOSM_DOT_NOT_PURGEABLE,
	KOSM_DOT_BAD_CACHE_POLICY,
	KOSM_DOT_ALREADY_MAPPED,
	KOSM_DOT_NOT_MAPPED,
	KOSM_DOT_NOT_WIRED,
	KOSM_DOT_WOULD_FAULT
};

/* kosm_dot info */

typedef struct {
	kosm_dot_id		kosm_dot;
	team_id			team;
	char			name[B_OS_NAME_LENGTH];
	void*			address;
	size_t			size;
	uint32			protection;
	uint32			flags;
	uint8			cache_policy;
	uint8			purgeable_state;
	uint16			_reserved0;
	uint32			tag;
	phys_addr_t		phys_base;
	size_t			resident_size;
	size_t			wired_size;
} kosm_dot_info;

/*
 * All kosm_dot functions take per-process handles, not global IDs.
 * Handles are obtained via kosm_create_dot() or received via kosm_ray.
 * Permissions are controlled by handle rights (KOSM_RIGHT_*).
 *
 * For device mappings: physicalAddress = physical base of the range.
 * For anonymous memory: physicalAddress = 0.
 */
extern kosm_dot_id		kosm_create_dot(const char* name,
							void** address, uint32 addressSpec,
							size_t size, uint32 protection,
							uint32 flags, uint32 tag,
							phys_addr_t physicalAddress);

extern status_t			kosm_delete_dot(kosm_dot_id handle);

/* Map into current process (handle obtained via ray transfer) */

extern status_t			kosm_map_dot(kosm_dot_id handle,
							void** address, uint32 addressSpec,
							uint32 protection);

extern status_t			kosm_unmap_dot(kosm_dot_id handle);

/* Protection & cache policy (requires KOSM_RIGHT_MANAGE) */

extern status_t			kosm_protect_dot(kosm_dot_id handle,
							uint32 newProtection);

extern status_t			kosm_dot_set_cache_policy(kosm_dot_id handle,
							uint8 cachePolicy);

/* Purgeable memory (requires KOSM_RIGHT_MANAGE) */

extern status_t			kosm_dot_mark_volatile(kosm_dot_id handle,
							uint8* oldState);

extern status_t			kosm_dot_mark_nonvolatile(kosm_dot_id handle,
							uint8* oldState);

/* Physical access / DMA (requires KOSM_RIGHT_WRITE) */

extern status_t			kosm_dot_wire(kosm_dot_id handle,
							size_t offset, size_t size);

extern status_t			kosm_dot_unwire(kosm_dot_id handle,
							size_t offset, size_t size);

extern status_t			kosm_dot_get_phys(kosm_dot_id handle,
							size_t offset,
							phys_addr_t* physicalAddress);

/* File-backed dots (KOSM_DOT_FILE) */

extern kosm_dot_id		kosm_create_dot_file(int fd, off_t fileOffset,
							const char* name,
							void** address, uint32 addressSpec,
							size_t size, uint32 protection,
							uint32 flags, uint32 tag);

extern status_t			kosm_dot_sync(kosm_dot_id handle,
							size_t offset, size_t size);

/* system private, use macros instead */

extern status_t			_kosm_get_dot_info(kosm_dot_id handle,
							kosm_dot_info* info, size_t size);

extern status_t			_kosm_get_next_dot_info(team_id team,
							int32* cookie,
							kosm_dot_info* info, size_t size);

#define kosm_get_dot_info(handle, info) \
	_kosm_get_dot_info((handle), (info), sizeof(*(info)))

#define kosm_get_next_dot_info(team, cookie, info) \
	_kosm_get_next_dot_info((team), (cookie), (info), sizeof(*(info)))


#ifdef __cplusplus
}
#endif

#endif
