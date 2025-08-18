/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Architecture-Specific Kernel Start
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>

#include "../efi_platform.h"


extern "C" void arch_enter_kernel(struct kernel_args* kernelArgs, 
	addr_t kernelEntry, addr_t kernelStackTop);


extern "C" void
arch_convert_kernel_args(void)
{
	// Convert bootloader addresses to kernel addresses
	dprintf("ARM64 kernel args conversion\n");

	// TODO: Fix up addresses in kernel_args for ARM64
	// TODO: Convert device tree pointer if present
	// TODO: Fix up ACPI table pointers
}


extern "C" void
arch_start_kernel(addr_t kernelEntry)
{
	dprintf("ARM64 starting kernel at %p\n", (void*)kernelEntry);

	// TODO: Complete ARM64 kernel startup sequence:
	// 1. Get EFI memory map
	// 2. Generate post-EFI page tables
	// 3. Exit EFI boot services
	// 4. Set up final MMU configuration
	// 5. Boot secondary CPUs
	// 6. Jump to kernel entry point

	// For now, this is a minimal implementation
	dprintf("ARM64 kernel startup - minimal implementation\n");
	
	// Convert kernel arguments to final format
	arch_convert_kernel_args();

	// TODO: Implement full kernel entry sequence
	// arch_enter_kernel(&gKernelArgs, kernelEntry, stack_top);

	panic("ARM64 kernel startup not fully implemented");
}


extern "C" status_t
arch_allocate_kernel_stack(addr_t* _stackTop)
{
	// Allocate kernel stack for ARM64
	size_t stackSize = 16 * 1024;  // 16KB stack
	
	if (kBootServices == NULL)
		return B_ERROR;

	efi_physical_addr addr = 0;
	size_t pages = (stackSize + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
	
	efi_status status = kBootServices->AllocatePages(AllocateAnyPages,
		EfiLoaderData, pages, &addr);
		
	if (status != EFI_SUCCESS)
		return B_NO_MEMORY;

	*_stackTop = addr + stackSize;
	return B_OK;
}