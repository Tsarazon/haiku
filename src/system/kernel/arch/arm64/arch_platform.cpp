/*
 * Copyright 2019-2022 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/platform.h>
#include <boot/kernel_args.h>
#include <boot_item.h>

// BCM2712 (Raspberry Pi 5) support
extern status_t bcm2712_init(kernel_args* args);


void *gFDT = NULL;
phys_addr_t sACPIRootPointer = 0;


status_t
arch_platform_init(struct kernel_args *kernelArgs)
{
	gFDT = kernelArgs->arch_args.fdt;
	return B_OK;
}


status_t
arch_platform_init_post_vm(struct kernel_args *kernelArgs)
{
	// Try to initialize BCM2712 (Raspberry Pi 5) hardware
	// This will auto-detect and only initialize if BCM2712 is present
	bcm2712_init(kernelArgs);

	if (kernelArgs->arch_args.acpi_root) {
		sACPIRootPointer = kernelArgs->arch_args.acpi_root.Get();
		add_boot_item("ACPI_ROOT_POINTER", &sACPIRootPointer, sizeof(sACPIRootPointer));
	}

	return B_OK;
}


status_t
arch_platform_init_post_thread(struct kernel_args *kernelArgs)
{
	return B_OK;
}
