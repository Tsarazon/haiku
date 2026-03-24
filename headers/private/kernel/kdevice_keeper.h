/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_DEVICE_KEEPER_H_PRIVATE
#define _KERNEL_DEVICE_KEEPER_H_PRIVATE

#include <device_keeper.h>
#include <lock.h>

struct kernel_args;


#ifdef __cplusplus
extern "C" {
#endif

status_t	dk_keeper_init(struct kernel_args* args);
status_t	dk_keeper_init_post_modules(struct kernel_args* args);
void		dk_keeper_uninit();

// called from devfs scan_for_drivers_if_needed()
status_t	dk_keeper_probe(const char* path, uint32 updateCycle);

#ifdef __cplusplus
}
#endif

#endif	// _KERNEL_DEVICE_KEEPER_H_PRIVATE
