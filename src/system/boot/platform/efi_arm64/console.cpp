/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Console Implementation
 */


#include <boot/platform/generic/text_console.h>
#include <boot/stdio.h>
#include <util/kernel_cpp.h>

#include "efi_platform.h"


class EFIConsole : public ConsoleNode {
public:
						EFIConsole();

	virtual ssize_t		ReadAt(void* cookie, off_t pos, void* buffer,
							size_t bufferSize);
	virtual ssize_t		WriteAt(void* cookie, off_t pos, const void* buffer,
							size_t bufferSize);
};


static EFIConsole sConsole;


EFIConsole::EFIConsole()
	: ConsoleNode()
{
}


ssize_t
EFIConsole::ReadAt(void* cookie, off_t pos, void* buffer, size_t bufferSize)
{
	// Basic keyboard input - not implemented in minimal version
	return 0;
}


ssize_t  
EFIConsole::WriteAt(void* cookie, off_t pos, const void* buffer, size_t bufferSize)
{
	const char* string = (const char*)buffer;
	
	if (kSystemTable == NULL || kSystemTable->ConOut == NULL)
		return 0;

	// Convert ASCII to UTF-16 for EFI console output
	for (size_t i = 0; i < bufferSize; i++) {
		char16_t utf16_char = string[i];
		if (utf16_char == 0)
			break;
			
		// Handle newlines
		if (utf16_char == '\n') {
			char16_t crlf[3] = { '\r', '\n', 0 };
			kSystemTable->ConOut->OutputString(kSystemTable->ConOut, crlf);
		} else {
			char16_t single_char[2] = { utf16_char, 0 };
			kSystemTable->ConOut->OutputString(kSystemTable->ConOut, single_char);
		}
	}
	
	return bufferSize;
}


extern "C" void
console_clear_screen(void)
{
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		kSystemTable->ConOut->ClearScreen(kSystemTable->ConOut);
	}
}


extern "C" int32
console_width(void)
{
	// Default console width for ARM64 EFI
	return 80;
}


extern "C" int32
console_height(void)
{
	// Default console height for ARM64 EFI
	return 25;
}


extern "C" void
console_set_cursor(int32 x, int32 y)
{
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		kSystemTable->ConOut->SetCursorPosition(kSystemTable->ConOut, x, y);
	}
}


extern "C" void
console_show_cursor(void)
{
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		kSystemTable->ConOut->EnableCursor(kSystemTable->ConOut, true);
	}
}


extern "C" void
console_hide_cursor(void)
{
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		kSystemTable->ConOut->EnableCursor(kSystemTable->ConOut, false);
	}
}


extern "C" void
console_set_color(int32 foreground, int32 background)
{
	if (kSystemTable != NULL && kSystemTable->ConOut != NULL) {
		int32 color = EFI_TEXT_ATTR(foreground, background);
		kSystemTable->ConOut->SetAttribute(kSystemTable->ConOut, color);
	}
}


extern "C" int
console_wait_for_key(void)
{
	// Basic key waiting - not implemented in minimal version
	return 0;
}


extern "C" uint32
console_check_boot_keys(void)
{
	// Check for special boot keys - not implemented in minimal version
	return 0;
}


extern "C" status_t
console_init(void)
{
	if (kSystemTable == NULL || kSystemTable->ConOut == NULL) {
		return B_ERROR;
	}

	// Reset and clear the console
	kSystemTable->ConOut->Reset(kSystemTable->ConOut, false);
	kSystemTable->ConOut->ClearScreen(kSystemTable->ConOut);
	
	// Set default text attributes
	kSystemTable->ConOut->SetAttribute(kSystemTable->ConOut, 
		EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));

	// Register console node
	gKernelArgs.debug_output = &sConsole;

	return B_OK;
}