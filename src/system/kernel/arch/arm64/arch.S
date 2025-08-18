/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 64-bit Architecture Assembly Functions
 */

#include <asm_defs.h>
#include <arch/aarch64/arch_config.h>

.text

/*
 * ARM64 64-bit specific assembly functions
 */

/* void arm64_64_context_switch(addr_t newPageDirectory) */
FUNCTION(arm64_64_context_switch):
	// Switch page directory (TTBR0_EL1)
	dsb	ishst			// Ensure writes have completed
	msr	ttbr0_el1, x0	// Load new page directory
	isb					// Instruction synchronization barrier
	
	// Invalidate TLB
	tlbi	vmalle1is	// Invalidate all stage 1 translations
	dsb		ish			// Data synchronization barrier
	isb					// Instruction synchronization barrier
	
	ret
FUNCTION_END(arm64_64_context_switch)

/* uint64 arm64_64_get_current_thread() */
FUNCTION(arm64_64_get_current_thread):
	mrs		x0, tpidr_el1	// Thread pointer register
	ret
FUNCTION_END(arm64_64_get_current_thread)

/* void arm64_64_set_current_thread(addr_t thread) */
FUNCTION(arm64_64_set_current_thread):
	msr		tpidr_el1, x0	// Set thread pointer register
	ret
FUNCTION_END(arm64_64_set_current_thread)

/* void arm64_64_switch_stack_and_call(addr_t stack, void (*func)(void), addr_t arg) */
FUNCTION(arm64_64_switch_stack_and_call):
	mov		sp, x0		// Switch to new stack
	mov		x0, x2		// Move argument to x0
	br		x1			// Call function
FUNCTION_END(arm64_64_switch_stack_and_call)