/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * kosm_dot -- libroot syscall wrappers.
 *
 * All functions take per-process handles, not global IDs.
 * Handles are obtained via kosm_create_dot() or received via kosm_ray.
 */

#include <KosmOS.h>

#include <libroot_private.h>

#include "syscalls.h"


kosm_dot_id
kosm_create_dot(const char* name, void** address, uint32 addressSpec,
	size_t size, uint32 protection, uint32 flags, uint32 tag,
	phys_addr_t physicalAddress)
{
	if (addressSpec == B_ANY_ADDRESS)
		addressSpec = B_RANDOMIZED_ANY_ADDRESS;
	if (addressSpec == B_BASE_ADDRESS)
		addressSpec = B_RANDOMIZED_BASE_ADDRESS;

	return _kern_kosm_create_dot(name, address, addressSpec, size,
		protection, flags, tag, physicalAddress);
}


status_t
kosm_delete_dot(kosm_dot_id handle)
{
	return _kern_kosm_delete_dot(handle);
}


status_t
kosm_map_dot(kosm_dot_id handle, void** address, uint32 addressSpec,
	uint32 protection)
{
	if (addressSpec == B_ANY_ADDRESS)
		addressSpec = B_RANDOMIZED_ANY_ADDRESS;
	if (addressSpec == B_BASE_ADDRESS)
		addressSpec = B_RANDOMIZED_BASE_ADDRESS;

	return _kern_kosm_map_dot(handle, address, addressSpec, protection);
}


status_t
kosm_unmap_dot(kosm_dot_id handle)
{
	return _kern_kosm_unmap_dot(handle);
}


status_t
kosm_protect_dot(kosm_dot_id handle, uint32 newProtection)
{
	return _kern_kosm_protect_dot(handle, newProtection);
}


status_t
kosm_dot_set_cache_policy(kosm_dot_id handle, uint8 cachePolicy)
{
	return _kern_kosm_dot_set_cache_policy(handle, cachePolicy);
}


status_t
kosm_dot_mark_volatile(kosm_dot_id handle, uint8* oldState)
{
	return _kern_kosm_dot_mark_volatile(handle, oldState);
}


status_t
kosm_dot_mark_nonvolatile(kosm_dot_id handle, uint8* oldState)
{
	return _kern_kosm_dot_mark_nonvolatile(handle, oldState);
}


status_t
kosm_dot_wire(kosm_dot_id handle, size_t offset, size_t size)
{
	return _kern_kosm_dot_wire(handle, offset, size);
}


status_t
kosm_dot_unwire(kosm_dot_id handle, size_t offset, size_t size)
{
	return _kern_kosm_dot_unwire(handle, offset, size);
}


status_t
kosm_dot_get_phys(kosm_dot_id handle, size_t offset,
	phys_addr_t* physicalAddress)
{
	return _kern_kosm_dot_get_phys(handle, offset, physicalAddress);
}


status_t
_kosm_get_dot_info(kosm_dot_id handle, kosm_dot_info* info,
	size_t size)
{
	return _kern_kosm_get_dot_info(handle, info, size);
}


status_t
_kosm_get_next_dot_info(team_id team, int32* cookie,
	kosm_dot_info* info, size_t size)
{
	return _kern_kosm_get_next_dot_info(team, cookie, info, size);
}


kosm_dot_id
kosm_create_dot_file(int fd, off_t fileOffset, const char* name,
	void** address, uint32 addressSpec, size_t size,
	uint32 protection, uint32 flags, uint32 tag)
{
	if (addressSpec == B_ANY_ADDRESS)
		addressSpec = B_RANDOMIZED_ANY_ADDRESS;
	if (addressSpec == B_BASE_ADDRESS)
		addressSpec = B_RANDOMIZED_BASE_ADDRESS;

	return _kern_kosm_create_dot_file(fd, fileOffset, name, address,
		addressSpec, size, protection, flags, tag);
}


status_t
kosm_dot_sync(kosm_dot_id handle, size_t offset, size_t size)
{
	return _kern_kosm_dot_sync(handle, offset, size);
}
