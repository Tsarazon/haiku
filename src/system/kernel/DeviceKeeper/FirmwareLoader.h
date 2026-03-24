/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_FIRMWARE_LOADER_H
#define DEVICE_KEEPER_FIRMWARE_LOADER_H

#include <SupportDefs.h>


// Load firmware blob by name. Searches KOSM_FIRMWARE_PATH_SYSTEM,
// then KOSM_FIRMWARE_PATH_DATA. Returns a kernel-heap buffer in
// *_data that must be freed with dk_release_firmware().
//
// name: relative path, e.g. "intel/ibt-20-1-3.sfi"
// _data: receives pointer to loaded blob
// _size: receives size in bytes

status_t	dk_load_firmware(const char* name, const void** _data,
				size_t* _size);
void		dk_release_firmware(const void* data);


#endif // DEVICE_KEEPER_FIRMWARE_LOADER_H
