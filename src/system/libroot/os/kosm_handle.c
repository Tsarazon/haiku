/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <KosmOS.h>
#include <syscalls.h>


status_t
kosm_close_handle(kosm_handle_t handle)
{
	return _kern_kosm_close_handle(handle);
}


kosm_handle_t
kosm_duplicate_handle(kosm_handle_t handle, uint32 rights)
{
	return _kern_kosm_duplicate_handle(handle, rights);
}


status_t
_kosm_get_handle_info(kosm_handle_t handle, kosm_handle_info *info)
{
	return _kern_kosm_handle_get_info(handle, info);
}
