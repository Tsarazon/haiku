/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Device Support
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/partitions.h>

#include "efi_platform.h"


extern "C" status_t
platform_add_boot_device(struct stage2_args* args, NodeList* devicesList)
{
	// Add ARM64 EFI boot devices
	dprintf("ARM64 EFI device discovery\n");

	// TODO: Enumerate EFI Simple File System Protocol instances
	// TODO: Add block devices for partition scanning
	// TODO: Support EFI device paths

	return B_OK;
}


extern "C" status_t
platform_get_boot_partitions(struct stage2_args* args, NodeList* list,
	NodeList* partitionList)
{
	// Get available boot partitions - stub implementation
	return B_OK;
}


extern "C" status_t
platform_add_block_devices(stage2_args* args, NodeList* devicesList)
{
	// Add block devices through EFI block I/O protocol
	dprintf("ARM64 EFI block device enumeration\n");

	// TODO: Enumerate EFI Block I/O Protocol instances
	// TODO: Create block device nodes
	// TODO: Support removable media detection

	return B_OK;
}


extern "C" status_t
platform_register_boot_device(Node* device)
{
	// Register boot device - stub implementation
	return B_OK;
}