/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <KosmOS.h>
#include <syscalls.h>


status_t
kosm_create_ray(kosm_ray_id *endpoint0, kosm_ray_id *endpoint1, uint32 flags)
{
	return _kern_kosm_create_ray(endpoint0, endpoint1, flags);
}


status_t
kosm_close_ray(kosm_ray_id id)
{
	return _kern_kosm_close_ray(id);
}


status_t
kosm_ray_write(kosm_ray_id id, const void *data, size_t dataSize,
	const kosm_handle_t *handles, size_t handleCount, uint32 flags)
{
	return _kern_kosm_ray_write(id, data, dataSize, handles, handleCount,
		flags);
}


status_t
kosm_ray_write_etc(kosm_ray_id id, const void *data, size_t dataSize,
	const kosm_handle_t *handles, size_t handleCount, uint32 flags,
	bigtime_t timeout)
{
	return _kern_kosm_ray_write_etc(id, data, dataSize, handles, handleCount,
		flags, timeout);
}


status_t
kosm_ray_read(kosm_ray_id id, void *data, size_t *dataSize,
	kosm_handle_t *handles, size_t *handleCount, uint32 flags)
{
	return _kern_kosm_ray_read(id, data, dataSize, handles, handleCount,
		flags);
}


status_t
kosm_ray_read_etc(kosm_ray_id id, void *data, size_t *dataSize,
	kosm_handle_t *handles, size_t *handleCount, uint32 flags,
	bigtime_t timeout)
{
	return _kern_kosm_ray_read_etc(id, data, dataSize, handles, handleCount,
		flags, timeout);
}


status_t
kosm_ray_wait(kosm_ray_id id, uint32 signals, uint32 *observedSignals,
	uint32 flags, bigtime_t timeout)
{
	return _kern_kosm_ray_wait(id, signals, observedSignals, flags, timeout);
}


status_t
kosm_ray_set_qos(kosm_ray_id id, uint8 qosClass)
{
	return _kern_kosm_ray_set_qos(id, qosClass);
}


kosm_ray_id
kosm_get_bootstrap_ray(void)
{
	return _kern_kosm_get_bootstrap_ray();
}


status_t
_kosm_get_ray_info(kosm_ray_id id, kosm_ray_info *info, size_t size)
{
	return _kern_kosm_get_ray_info(id, info, size);
}
