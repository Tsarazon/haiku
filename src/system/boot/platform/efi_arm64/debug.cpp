/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Debug Support
 */


#include <boot/platform.h>
#include <boot/stdio.h>

#include "efi_platform.h"


extern "C" char*
platform_debug_get_log_buffer(size_t* _size)
{
	// Return debug log buffer - stub implementation
	if (_size != NULL)
		*_size = 0;
	return NULL;
}


extern "C" void
platform_debug_puts(const char* string, size_t length)
{
	// Basic debug output through EFI console
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		for (size_t i = 0; i < length && string[i] != 0; i++) {
			char16_t utf16_char = string[i];
			char16_t single_char[2] = { utf16_char, 0 };
			kSystemTable->ConOut->OutputString(kSystemTable->ConOut, single_char);
		}
	}
}


extern "C" void
platform_debug_init(void)
{
	// Initialize ARM64 debug subsystem
	// This is a minimal implementation
}


extern "C" void
platform_debug_cleanup(void)
{
	// Cleanup debug subsystem before kernel handoff
}