/*
 * Copyright 2020, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AUTO_DELETER_DRIVERS_H
#define _AUTO_DELETER_DRIVERS_H


#include <AutoDeleter.h>
#include <driver_settings.h>

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
#include <vfs.h>
#include <fs/fd.h>
#include <vm/VMAddressSpace.h>
#include <device_keeper.h>
#endif


namespace BPrivate {


typedef CObjectDeleter<void, status_t, unload_driver_settings>
	DriverSettingsUnloader;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)

typedef CObjectDeleter<struct vnode, void, vfs_put_vnode> VnodePutter;
typedef CObjectDeleter<file_descriptor, void, put_fd> FileDescriptorPutter;
typedef MethodDeleter<VMAddressSpace, void, &VMAddressSpace::Put>
	VMAddressSpacePutter;

// RAII wrapper for dk_node references obtained via
// dk_keeper_info::get_root_node / get_parent_node / find_child_node /
// find_node / get_next_child_node. Releases via put_node on scope exit.

#if __GNUC__ >= 4

template <dk_keeper_info **deviceKeeper>
using DkNodePutter = MethodObjectDeleter<dk_node, dk_keeper_info,
	deviceKeeper, void, &dk_keeper_info::put_node>;

#else

template <dk_keeper_info **deviceKeeper>
struct DkNodePutter : MethodObjectDeleter<dk_node, dk_keeper_info,
	deviceKeeper, void, &dk_keeper_info::put_node>
{
	typedef MethodObjectDeleter<dk_node, dk_keeper_info,
		deviceKeeper, void, &dk_keeper_info::put_node> Base;

	DkNodePutter() : Base() {}
	DkNodePutter(dk_node* object) : Base(object) {}
};

#endif

#endif


}


using ::BPrivate::DriverSettingsUnloader;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)

using ::BPrivate::VnodePutter;
using ::BPrivate::FileDescriptorPutter;
using ::BPrivate::VMAddressSpacePutter;
using ::BPrivate::DkNodePutter;

#endif


#endif	// _AUTO_DELETER_DRIVERS_H
