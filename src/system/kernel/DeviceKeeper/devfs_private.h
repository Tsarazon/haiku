/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVFS_PRIVATE_H
#define DEVFS_PRIVATE_H


#include <SupportDefs.h>


class BaseDevice;


status_t	devfs_publish_device(const char* path, BaseDevice* device);
status_t	devfs_unpublish_device(BaseDevice* device, bool disconnect);

status_t	devfs_get_device(const char* path, BaseDevice*& _device);
void		devfs_put_device(BaseDevice* device);

#endif	/* DEVFS_PRIVATE_H */
