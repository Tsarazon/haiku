/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Serial Support
 */


#include <boot/platform.h>
#include <boot/stdio.h>

#include "efi_platform.h"


extern "C" void
serial_init(void)
{
	// Initialize ARM64 serial support
	dprintf("ARM64 serial initialization\n");

	// TODO: Implement EFI Serial I/O Protocol support
	// TODO: Configure UART for debug output
	// TODO: Set up serial console redirection
}


extern "C" void
serial_cleanup(void)
{
	// Cleanup serial interface before kernel handoff
}


extern "C" void
serial_puts(const char* string, size_t length)
{
	// Output string to serial port - stub implementation
	// For now, fallback to console output
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		for (size_t i = 0; i < length && string[i] != 0; i++) {
			char16_t utf16_char = string[i];
			char16_t single_char[2] = { utf16_char, 0 };
			kSystemTable->ConOut->OutputString(kSystemTable->ConOut, single_char);
		}
	}
}


extern "C" void
serial_enable(void)
{
	// Enable serial output
}


extern "C" void
serial_disable(void)
{
	// Disable serial output
}