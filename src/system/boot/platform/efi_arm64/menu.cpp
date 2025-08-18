/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Boot Menu Support
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/menu.h>

#include "efi_platform.h"


extern "C" status_t
platform_add_menus(Menu* menu)
{
	// Add ARM64-specific menu items
	dprintf("ARM64 platform menus\n");

	// TODO: Add ARM64-specific boot options
	// TODO: Add device tree configuration options
	// TODO: Add ARM64 debugging options

	return B_OK;
}


extern "C" void
platform_run_menu(Menu* menu)
{
	// Run platform-specific menu
	// This is a stub implementation
}


extern "C" status_t
platform_get_boot_partition(NodeOpener** _opener, Directory** _root,
	off_t* _kernelSize)
{
	// Get boot partition information - stub implementation
	return B_ERROR;
}