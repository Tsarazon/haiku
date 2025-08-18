/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_ARM64_SIGNALS_H
#define _KERNEL_ARCH_ARM64_SIGNALS_H


#include <SupportDefs.h>


// ARM64 signal handling initialization
void	arm64_initialize_commpage_signal_handler();

// ARM64 signal handler wrapper
addr_t	arm64_get_user_signal_handler_wrapper(bool beosHandler,
	void* commPageAddress);

// ARM64-specific signal context
struct arm64_signal_frame_data {
	uint64	x[30];		// General purpose registers x0-x29
	uint64	lr;			// Link register (x30)
	uint64	sp;			// Stack pointer
	uint64	pc;			// Program counter
	uint64	pstate;		// Processor state
	
	// Floating-point/SIMD state
	uint64	v[32][2];	// Vector registers (128-bit each)
	uint64	fpsr;		// Floating-point status register
	uint64	fpcr;		// Floating-point control register
};


#endif	// _KERNEL_ARCH_ARM64_SIGNALS_H