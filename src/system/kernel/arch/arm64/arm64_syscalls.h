/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_ARM64_SYSCALLS_H
#define _KERNEL_ARCH_ARM64_SYSCALLS_H


#include <SupportDefs.h>


// ARM64 syscall initialization
void	arm64_initialize_syscall();

// ARM64 syscall stack management
extern void (*gARM64SetSyscallStack)(addr_t stackTop);

static inline void
arm64_set_syscall_stack(addr_t stackTop)
{
	if (gARM64SetSyscallStack != NULL)
		gARM64SetSyscallStack(stackTop);
}

// ARM64 syscall entry points
extern "C" void arm64_syscall_entry();
extern "C" void arm64_syscall_entry_fast();

// ARM64-specific syscall constants
#define ARM64_SYSCALL_VECTOR	0	// SVC #0 for syscalls


#endif	// _KERNEL_ARCH_ARM64_SYSCALLS_H