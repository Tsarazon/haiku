/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_X86_SYSCALLS_H
#define _KERNEL_ARCH_X86_SYSCALLS_H


#include <SupportDefs.h>


void	x86_initialize_syscall();


static inline void
x86_set_syscall_stack(addr_t stackTop)
{
}


#endif	// _KERNEL_ARCH_X86_SYSCALLS_H
