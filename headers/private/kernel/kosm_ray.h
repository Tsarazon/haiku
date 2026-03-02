/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_KOSM_RAY_H
#define _KERNEL_KOSM_RAY_H

#include <KosmOS.h>

struct kernel_args;
struct Thread;
struct Team;

#define THREAD_BLOCK_TYPE_KOSM_RAY	0x4B52	/* 'KR' */

#define KOSM_RAY_MAX_HANDLES		64
#define KOSM_RAY_MAX_DATA_SIZE		(256 * 1024)
#define KOSM_RAY_MAX_QUEUE_MESSAGES	256

#ifdef __cplusplus
extern "C" {
#endif

status_t		kosm_ray_init(struct kernel_args* args);

/* Internal: create with explicit owner */
status_t		kosm_create_ray_etc(kosm_ray_id* endpoint0,
					kosm_ray_id* endpoint1, uint32 flags,
					team_id owner);

/* Cleanup on team/thread death */
void			kosm_ray_delete_owned(struct Team* team);

/* Bootstrap management */
status_t		kosm_ray_set_bootstrap(team_id team, kosm_ray_id endpoint);

/* Stats */
int32			kosm_ray_max(void);
int32			kosm_ray_used(void);
off_t			kosm_ray_team_link_offset(void);

/* Select integration */
struct select_info;
status_t		kosm_select_ray(int32 id, struct select_info* info,
					bool kernel);
status_t		kosm_deselect_ray(int32 id, struct select_info* info,
					bool kernel);

/* Syscalls */

status_t		_user_kosm_create_ray(kosm_ray_id* userEndpoint0,
					kosm_ray_id* userEndpoint1, uint32 flags);
status_t		_user_kosm_close_ray(kosm_ray_id id);

status_t		_user_kosm_ray_write(kosm_ray_id id,
					const void* userData, size_t dataSize,
					const kosm_handle_t* userHandles, size_t handleCount,
					uint32 flags);
status_t		_user_kosm_ray_write_etc(kosm_ray_id id,
					const void* userData, size_t dataSize,
					const kosm_handle_t* userHandles, size_t handleCount,
					uint32 flags, bigtime_t timeout);

status_t		_user_kosm_ray_read(kosm_ray_id id,
					void* userData, size_t* userDataSize,
					kosm_handle_t* userHandles, size_t* userHandleCount,
					uint32 flags);
status_t		_user_kosm_ray_read_etc(kosm_ray_id id,
					void* userData, size_t* userDataSize,
					kosm_handle_t* userHandles, size_t* userHandleCount,
					uint32 flags, bigtime_t timeout);

status_t		_user_kosm_ray_wait(kosm_ray_id id, uint32 signals,
					uint32* userObservedSignals, uint32 flags,
					bigtime_t timeout);

status_t		_user_kosm_ray_set_qos(kosm_ray_id id, uint8 qosClass);

kosm_ray_id		_user_kosm_get_bootstrap_ray(void);

status_t		_user_kosm_get_ray_info(kosm_ray_id id,
					kosm_ray_info* userInfo, size_t size);

#ifdef __cplusplus
}
#endif

#endif
