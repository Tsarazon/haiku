/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <KosmOS.h>
#include <syscalls.h>


kosm_mutex_id
kosm_create_mutex(const char *name, uint32 flags)
{
	return _kern_kosm_create_mutex(name, flags);
}


status_t
kosm_delete_mutex(kosm_mutex_id id)
{
	return _kern_kosm_delete_mutex(id);
}


kosm_mutex_id
kosm_find_mutex(const char *name)
{
	return _kern_kosm_find_mutex(name);
}


status_t
kosm_acquire_mutex(kosm_mutex_id id)
{
	return _kern_kosm_acquire_mutex(id);
}


status_t
kosm_try_acquire_mutex(kosm_mutex_id id)
{
	return _kern_kosm_try_acquire_mutex(id);
}


status_t
kosm_acquire_mutex_etc(kosm_mutex_id id, uint32 flags, bigtime_t timeout)
{
	return _kern_kosm_acquire_mutex_etc(id, flags, timeout);
}


status_t
kosm_release_mutex(kosm_mutex_id id)
{
	return _kern_kosm_release_mutex(id);
}


status_t
kosm_mark_mutex_consistent(kosm_mutex_id id)
{
	return _kern_kosm_mark_mutex_consistent(id);
}


status_t
_kosm_get_mutex_info(kosm_mutex_id id, kosm_mutex_info *info, size_t size)
{
	return _kern_kosm_get_mutex_info(id, info, size);
}