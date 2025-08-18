/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 UEFI Boot Loader Entry Point
 * 
 * This module implements the basic UEFI entry point for ARM64 platforms,
 * following the UEFI specification for AArch64 systems.
 */


#include <string.h>

#include <KernelExport.h>

#include <arch/cpu.h>
#include <arch_cpu_defs.h>
#include <kernel.h>

#include <boot/kernel_args.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/stdio.h>

#include <efi/types.h>
#include <efi/system-table.h>
#include <efi/boot-services.h>
#include <efi/runtime-services.h>

#include "arm64_uefi.h"

// ARM64-specific constants
#define KERNEL_STACK_SIZE	(16 * 1024)		// 16KB kernel stack per CPU


extern void (*__ctor_list)(void);
extern void (*__ctor_end)(void);


// Global EFI system interface variables
const efi_system_table		*kSystemTable;
const efi_boot_services		*kBootServices;
const efi_runtime_services	*kRuntimeServices;
efi_handle kImage;

static uint32 sBootOptions;

extern "C" int main(stage2_args *args);


/*!	Call C++ constructors for global objects */
static void
call_ctors(void)
{
	void (**f)(void);

	for (f = &__ctor_list; f < &__ctor_end; f++) {
		if (*f)
			(**f)();
	}
}


/*!	Return platform-specific boot options */
extern "C" uint32
platform_boot_options()
{
	return sBootOptions;
}


/*!	Basic console initialization for ARM64 UEFI */
static void
console_init()
{
	// Basic console setup using EFI simple text output protocol
	if (kSystemTable->ConOut != NULL) {
		// Clear screen and reset to default settings
		kSystemTable->ConOut->Reset(kSystemTable->ConOut, false);
		kSystemTable->ConOut->ClearScreen(kSystemTable->ConOut);
		
		// Set text mode to default
		kSystemTable->ConOut->SetMode(kSystemTable->ConOut, 0);
	}
}


/*!	Basic debug output initialization */
static void
debug_init()
{
	// Initialize debug output mechanisms
	// On ARM64, this might include serial ports or other debug interfaces
	dprintf("ARM64 UEFI Boot Loader Debug Initialized\n");
}


// ARM64 assembly functions are declared in arm64_uefi.h


/*!	Test ARM64 exception level management functionality */
static void
test_arm64_exception_levels()
{
	dprintf("=== ARM64 Exception Level Management Test ===\n");

	// Test 1: Exception level detection
	uint32 initial_el = arm64_detect_exception_level();
	dprintf("Test 1: Initial exception level: EL%u\n", initial_el);

	if (initial_el > 3) {
		dprintf("ERROR: Invalid exception level detected\n");
		return;
	}

	// Test 2: Individual EL1 transition (if not already in EL1)
	if (initial_el != 1) {
		dprintf("Test 2: Testing direct EL1 transition...\n");
		uint32 transition_result = arm64_transition_to_el1();
		if (transition_result == 0) {
			dprintf("Test 2: Direct transition successful\n");
		} else {
			dprintf("Test 2: Direct transition failed: %u\n", transition_result);
		}

		uint32 post_transition_el = arm64_detect_exception_level();
		dprintf("Test 2: Post-transition EL: EL%u\n", post_transition_el);
	} else {
		dprintf("Test 2: Already in EL1, skipping direct transition test\n");
	}

	// Test 3: EL1 environment setup
	dprintf("Test 3: Testing EL1 environment setup...\n");
	uint32 setup_result = arm64_setup_el1_environment();
	if (setup_result == 0) {
		dprintf("Test 3: EL1 environment setup successful\n");
	} else {
		dprintf("Test 3: EL1 environment setup failed: %u\n", setup_result);
	}

	// Test 4: Full initialization sequence
	dprintf("Test 4: Testing full initialization sequence...\n");
	uint32 init_result = arm64_init_exception_level();
	if (init_result == 0) {
		dprintf("Test 4: Full initialization successful\n");
	} else {
		dprintf("Test 4: Full initialization failed: %u\n", init_result);
	}

	// Test 5: Final verification
	uint32 final_el = arm64_detect_exception_level();
	dprintf("Test 5: Final exception level: EL%u\n", final_el);

	if (final_el == 1) {
		dprintf("=== ARM64 Exception Level Management Test: PASSED ===\n");
	} else {
		dprintf("=== ARM64 Exception Level Management Test: FAILED ===\n");
	}
}


/*!	Enhanced ARM64 CPU identification and exception level setup */
static void
cpu_init()
{
	dprintf("ARM64 CPU initialization\n");

#ifdef DEBUG_ARM64_EXCEPTION_LEVELS
	// Run comprehensive exception level tests in debug builds
	test_arm64_exception_levels();
#endif

	// Step 1: Initialize exception level management
	uint32 current_el = arm64_detect_exception_level();
	dprintf("Current exception level: EL%u\n", current_el);

	// Initialize and transition to EL1 if needed
	uint32 el_result = arm64_init_exception_level();
	if (el_result != 0) {
		dprintf("Exception level initialization failed: %u\n", el_result);
		panic("Failed to initialize ARM64 exception levels");
	}

	// Verify we're now in EL1
	current_el = arm64_detect_exception_level();
	if (current_el != 1) {
		panic("Failed to transition to EL1 (current: EL%u)", current_el);
	}
	dprintf("Successfully running in EL1\n");

	// Step 2: Read CPU identification registers
	uint64 midr = arm64_get_midr();
	uint64 mpidr = arm64_get_mpidr();

	dprintf("ARM64 CPU Info:\n");
	dprintf("  MIDR_EL1:  0x%016llx\n", midr);
	dprintf("  MPIDR_EL1: 0x%016llx\n", mpidr);

	// Extract CPU information from MIDR_EL1
	uint32 implementer = (midr >> 24) & 0xFF;
	uint32 variant = (midr >> 20) & 0xF;
	uint32 architecture = (midr >> 16) & 0xF;
	uint32 part_num = (midr >> 4) & 0xFFF;
	uint32 revision = midr & 0xF;

	dprintf("  Implementer: 0x%02x\n", implementer);
	dprintf("  Variant: 0x%x\n", variant);
	dprintf("  Architecture: 0x%x\n", architecture);
	dprintf("  Part Number: 0x%03x\n", part_num);
	dprintf("  Revision: 0x%x\n", revision);

	// Set default CPU count to 1 until SMP initialization
	gKernelArgs.num_cpus = 1;
}


/*!	Platform exit - reset the system */
extern "C" void
platform_exit(void)
{
	if (kRuntimeServices != NULL) {
		kRuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	}
	
	// Should not reach here
	while (true) {
		// Infinite loop as fallback
	}
}


// Kernel handoff functions are declared in arm64_uefi.h


/*!	Prepare ARM64 registers for kernel handoff according to Haiku boot protocol */
static void
arm64_prepare_kernel_registers(kernel_args* kernelArgs, addr_t kernelEntry, addr_t stackTop)
{
	dprintf("Preparing ARM64 registers for kernel handoff\n");
	dprintf("  Kernel entry: 0x%016lx\n", kernelEntry);
	dprintf("  Stack top:    0x%016lx\n", stackTop);
	dprintf("  Kernel args:  %p\n", kernelArgs);

	// Ensure all memory operations are completed before kernel handoff
	arm64_memory_barrier();

	// Flush all caches to ensure kernel sees consistent memory
	dprintf("Flushing caches before kernel handoff...\n");
	arm64_cache_flush_all();
	arm64_invalidate_icache();
	arm64_memory_barrier();

	dprintf("ARM64 registers prepared for kernel handoff\n");
}


/*!	ARM64 kernel handoff function following Haiku boot protocol */
static void
start_kernel(addr_t kernelEntry)
{
	dprintf("ARM64 kernel handoff starting\n");

	// Step 1: Validate kernel entry point
	if (kernelEntry == 0) {
		panic("Invalid kernel entry point");
	}

	// Step 2: Allocate and set up kernel stack
	addr_t stackTop;
	size_t stackSize = KERNEL_STACK_SIZE;
	
	void* stackBase = NULL;
	if (platform_allocate_region(&stackBase, stackSize, 0) != B_OK) {
		panic("Failed to allocate kernel stack");
	}
	
	stackTop = (addr_t)stackBase + stackSize;
	dprintf("Kernel stack: 0x%016lx - 0x%016lx\n", (addr_t)stackBase, stackTop);

	// Ensure stack is properly aligned for ARM64 (16-byte alignment)
	stackTop = stackTop & ~0xF;

	// Step 3: Store stack information in kernel args
	gKernelArgs.cpu_kstack[0].start = (addr_t)stackBase;
	gKernelArgs.cpu_kstack[0].size = stackSize;

	// Step 4: Prepare memory management for kernel handoff
	dprintf("Preparing memory management for kernel handoff\n");
	
	// TODO: Set up final page tables
	// TODO: Exit EFI boot services
	// TODO: Configure ARM64 MMU for kernel
	
	// Step 5: Prepare CPU state for kernel
	dprintf("Preparing CPU state for kernel handoff\n");
	
	// Ensure we're in EL1
	uint32 current_el = arm64_detect_exception_level();
	if (current_el != 1) {
		panic("Kernel handoff must occur in EL1 (current: EL%u)", current_el);
	}

	// Step 6: Prepare ARM64-specific registers and state
	arm64_prepare_kernel_registers(&gKernelArgs, kernelEntry, stackTop);

	// Step 7: Final preparations before kernel entry
	dprintf("Final preparations before kernel entry\n");
	
	// Disable interrupts for kernel entry
	// (They should already be disabled from exception level setup)
	
	// Clear any pending exceptions
	// TODO: Clear pending exceptions and debug state

	// Step 8: Hand off to kernel
	dprintf("Handing off to kernel at 0x%016lx\n", kernelEntry);
	dprintf("Kernel args at %p, stack at 0x%016lx\n", &gKernelArgs, stackTop);

	// Call the kernel entry point via architecture-specific transition
	arch_enter_kernel(&gKernelArgs, kernelEntry, stackTop);

	// Should never reach here
	panic("Kernel returned to bootloader!");
}


/*!	Enhanced kernel startup with proper ARM64 initialization */
static void
start_kernel_enhanced(addr_t kernelEntry)
{
	dprintf("ARM64 enhanced kernel startup\n");

	// This is the enhanced version that will be called after
	// full EFI memory map processing and MMU setup is implemented

	// For now, call the basic version
	start_kernel(kernelEntry);
}


/**
 * efi_main - The entry point for the ARM64 EFI application
 * @image: firmware-allocated handle that identifies the image
 * @systemTable: EFI system table pointer
 * 
 * This function is called by the EFI firmware when the boot loader
 * is loaded. It implements the basic UEFI protocol initialization
 * required for ARM64 platforms.
 */
extern "C" efi_status
efi_main(efi_handle image, efi_system_table *systemTable)
{
	stage2_args args;
	memset(&args, 0, sizeof(stage2_args));

	// Validate input parameters
	if (image == NULL || systemTable == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	// Verify EFI system table signature
	if (systemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		return EFI_INCOMPATIBLE_VERSION;
	}

	// Store global EFI system interface pointers
	kImage = image;
	kSystemTable = systemTable;
	kBootServices = systemTable->BootServices;
	kRuntimeServices = systemTable->RuntimeServices;

	// Validate essential EFI services
	if (kBootServices == NULL || kRuntimeServices == NULL) {
		return EFI_UNSUPPORTED;
	}

	// Call C++ global constructors
	call_ctors();

	// Initialize basic platform services
	console_init();
	debug_init();
	
	dprintf("ARM64 UEFI Boot Loader Starting...\n");
	dprintf("EFI System Table at %p\n", systemTable);
	dprintf("EFI Boot Services at %p\n", kBootServices);
	dprintf("EFI Runtime Services at %p\n", kRuntimeServices);

	// Initialize boot options (keyboard input, etc.)
	sBootOptions = 0;  // Default boot options

	// Clear platform args structure
	memset(&gKernelArgs.platform_args, 0, sizeof(gKernelArgs.platform_args));

	// Enhanced ARM64 CPU initialization with exception level management
	cpu_init();

	// TODO: Add additional ARM64-specific initialization:
	// - ACPI table discovery
	// - Device tree blob processing  
	// - Timer initialization
	// - SMP initialization
	// - Memory map analysis

	dprintf("ARM64 initialization completed successfully\n");
	dprintf("Calling main boot loader logic...\n");

	// Call main boot loader logic
	// This will handle file system access, kernel loading, etc.
	main(&args);

	dprintf("Boot loader main() returned\n");
	dprintf("Note: Kernel handoff will be handled by arch_start_kernel()\n");

	// The actual kernel handoff is now handled by arch_start_kernel()
	// which will be called from the main boot logic after kernel loading

	return EFI_SUCCESS;
}