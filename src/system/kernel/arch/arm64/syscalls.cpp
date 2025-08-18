/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 64-bit System Call Implementation
 */

#include <KernelExport.h>
#include <kernel.h>
#include <syscall_restart.h>
#include <arch/arm64/arch_cpu.h>
#include <arch/arm64/arch_thread_types.h>

extern "C" void arm64_syscall_entry();

void (*gARM64SetSyscallStack)(addr_t stackTop) = NULL;

static void
arm64_64_prepare_syscall_args(struct iframe* frame)
{
	// ARM64 syscall arguments are passed in x0-x7
	// System call number is in x8
	// No additional preparation needed for ARM64
}

extern "C" uint64
arm64_handle_syscall(struct iframe* frame)
{
	// Extract syscall number from x8
	uint32 syscallNumber = frame->x[8];
	
	// Handle syscall restart
	if ((frame->x[0] & RESTART_SYSCALL) != 0) {
		frame->x[0] &= ~RESTART_SYSCALL;
		syscallNumber = frame->x[0];
	}
	
	// Prepare arguments for syscall
	arm64_64_prepare_syscall_args(frame);
	
	// Call generic syscall handler
	// This will be implemented when the syscall infrastructure is ready
	return B_ERROR;  // Placeholder
}

extern "C" void
arm64_return_to_userland(struct iframe* frame)
{
	// Return to user space
	// This will trigger the exception return mechanism
	// The actual return is handled in assembly (arch_asm.S)
}

void
arm64_initialize_syscall()
{
	// ARM64 uses SVC (supervisor call) instruction for system calls
	// The syscall vector is configured in the exception vector table
	// No special initialization needed beyond exception vector setup
	
	dprintf("ARM64 syscall interface initialized\n");
}