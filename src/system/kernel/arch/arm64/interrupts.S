/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 64-bit Interrupt Handling Assembly
 */

#include <asm_defs.h>
#include <arch/aarch64/arch_config.h>

.text

/*
 * ARM64 interrupt and exception handlers
 */

/* void arm64_64_interrupt_handler(void) */
FUNCTION(arm64_64_interrupt_handler):
	// This is called from the main interrupt handling code
	// Save additional ARM64-specific state if needed
	
	// Call generic interrupt handler
	// (The main interrupt handling is done in arch_asm.S)
	
	ret
FUNCTION_END(arm64_64_interrupt_handler)

/* void arm64_64_exception_handler(void) */
FUNCTION(arm64_64_exception_handler):
	// Handle ARM64-specific exceptions
	// This complements the main exception handling
	
	ret
FUNCTION_END(arm64_64_exception_handler)