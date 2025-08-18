/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Video Support
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>

#include "efi_platform.h"


extern "C" status_t
platform_init_video(void)
{
	// Initialize ARM64 EFI video subsystem
	dprintf("ARM64 EFI video initialization\n");

	// TODO: Implement GOP (Graphics Output Protocol) support
	// TODO: Initialize framebuffer if available
	// TODO: Set up EDID information

	// For now, clear framebuffer info
	gKernelArgs.frame_buffer.enabled = false;
	gKernelArgs.frame_buffer.physical_buffer.start = 0;
	gKernelArgs.frame_buffer.physical_buffer.size = 0;
	gKernelArgs.frame_buffer.width = 0;
	gKernelArgs.frame_buffer.height = 0;
	gKernelArgs.frame_buffer.depth = 0;
	gKernelArgs.frame_buffer.bytes_per_row = 0;

	return B_OK;
}


extern "C" void
platform_switch_to_logo(void)
{
	// Switch to logo display - stub implementation
}


extern "C" void
platform_switch_to_text_mode(void)
{
	// Switch to text mode - stub implementation
}


extern "C" bool
platform_supports_graphics_mode(void)
{
	// Check if graphics mode is supported
	return false;  // Stub implementation
}