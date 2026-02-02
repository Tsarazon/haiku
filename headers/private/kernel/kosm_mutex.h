/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_KOSM_MUTEX_H
#define _KERNEL_KOSM_MUTEX_H

#include <KosmOS.h>

struct kernel_args;
struct Thread;
struct Team;

#define THREAD_BLOCK_TYPE_KOSM_MUTEX	0x4B4D


#ifdef __cplusplus
extern "C" {
#endif

status_t		kosm_mutex_init(struct kernel_args* args);

kosm_mutex_id	kosm_create_mutex_etc(const char* name, uint32 flags,
					team_id owner);

void			kosm_mutex_release_owned(struct Thread* thread);
void			kosm_mutex_delete_owned(struct Team* team);

int32			kosm_mutex_max(void);
int32			kosm_mutex_used(void);
off_t			kosm_mutex_team_link_offset(void);
status_t		kosm_try_acquire_mutex_etc(kosm_mutex_id id, uint32 flags);

/* Syscalls */

kosm_mutex_id	_user_kosm_create_mutex(const char* userName, uint32 flags);
status_t		_user_kosm_delete_mutex(kosm_mutex_id id);
kosm_mutex_id	_user_kosm_find_mutex(const char* userName);
status_t		_user_kosm_acquire_mutex(kosm_mutex_id id);
status_t		_user_kosm_try_acquire_mutex(kosm_mutex_id id);
status_t		_user_kosm_acquire_mutex_etc(kosm_mutex_id id, uint32 flags,
					bigtime_t timeout);
status_t		_user_kosm_release_mutex(kosm_mutex_id id);
status_t		_user_kosm_mark_mutex_consistent(kosm_mutex_id id);
status_t		_user_kosm_get_mutex_info(kosm_mutex_id id,
					kosm_mutex_info* userInfo, size_t size);

#ifdef __cplusplus
}
#endif

#endif
