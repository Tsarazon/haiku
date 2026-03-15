/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_KOSM_MUTEX_H
#define _KERNEL_KOSM_MUTEX_H

#include <KosmOS.h>
#include <util/KernelReferenceable.h>

struct kernel_args;

#ifdef __cplusplus
namespace BKernel {
	struct Thread;
	struct Team;
}
using BKernel::Thread;
using BKernel::Team;
#else
struct Thread;
struct Team;
#endif

#define THREAD_BLOCK_TYPE_KOSM_MUTEX	0x4B4D


#ifdef __cplusplus

/*!	Refcounted wrapper stored in per-process handle tables.
	The slot table remains the authoritative store for mutex state;
	KosmMutexObject simply bridges the handle table to a slot ID.
	internal_id is set at creation and never changes for this object.
	Operations validate slot.id == internal_id to detect stale handles.
*/
class KosmMutexObject : public KernelReferenceable {
public:
	kosm_mutex_id	internal_id;

					KosmMutexObject(kosm_mutex_id id)
						: internal_id(id) {}
};

extern "C" {
#endif

status_t		kosm_mutex_init(struct kernel_args* args);

/* Returns a per-process handle via the handle table. */
kosm_mutex_id	kosm_create_mutex_etc(const char* name, uint32 flags,
					team_id owner);

void			kosm_mutex_release_owned(struct Thread* thread);
void			kosm_mutex_delete_owned(struct Team* team);

int32			kosm_mutex_max(void);
int32			kosm_mutex_used(void);
off_t			kosm_mutex_team_link_offset(void);

/* Internal functions operating on slot IDs (not handles).
   Used after handle resolution or from kernel code. */
status_t		kosm_delete_mutex_internal(kosm_mutex_id internalId);
status_t		kosm_try_acquire_mutex_internal(kosm_mutex_id internalId,
					uint32 flags);
status_t		kosm_acquire_mutex_internal(kosm_mutex_id internalId,
					uint32 flags, bigtime_t timeout);
status_t		kosm_release_mutex_internal(kosm_mutex_id internalId);
status_t		kosm_mark_mutex_consistent_internal(kosm_mutex_id internalId);

/* Resolve a per-process handle to an internal slot ID.
   Returns B_BAD_VALUE on invalid handle. */
status_t		kosm_mutex_resolve_handle(kosm_handle_t handle,
					uint32 requiredRights,
					kosm_mutex_id* outInternalId);

/* Syscalls (all take per-process handles, not slot IDs) */

kosm_mutex_id	_user_kosm_create_mutex(const char* userName, uint32 flags);
status_t		_user_kosm_delete_mutex(kosm_mutex_id handle);
kosm_mutex_id	_user_kosm_find_mutex(const char* userName);
status_t		_user_kosm_acquire_mutex(kosm_mutex_id handle);
status_t		_user_kosm_try_acquire_mutex(kosm_mutex_id handle);
status_t		_user_kosm_acquire_mutex_etc(kosm_mutex_id handle, uint32 flags,
					bigtime_t timeout);
status_t		_user_kosm_release_mutex(kosm_mutex_id handle);
status_t		_user_kosm_mark_mutex_consistent(kosm_mutex_id handle);
status_t		_user_kosm_get_mutex_info(kosm_mutex_id handle,
					kosm_mutex_info* userInfo, size_t size);
status_t		_user_kosm_get_next_mutex_info(team_id team, int32* userCookie,
					kosm_mutex_info* userInfo, size_t size);

#ifdef __cplusplus
}
#endif

#endif
