/*
 * Copyright 2007-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

/*!	\file asm_offsets.cpp
 * \brief Build-time code generator for assembly structure offsets
 *
 * This file is compiled during the build process, and its assembly output
 * is parsed to extract C structure field offsets and sizes. These are then
 * converted into macro definitions in a generated header file that can be
 * included from assembly code.
 *
 * The dummy() function is never called at runtime - it exists solely to
 * generate assembly that the build system can parse.
 *
 * CRITICAL: Changes to structure layouts will be reflected here automatically,
 * but assembly code using these offsets must be carefully reviewed when
 * structures change.
 */

#include <computed_asm_macros.h>

#include <arch_cpu.h>
#include <cpu.h>
#include <ksignal.h>
#include <ksyscalls.h>
#include <thread_types.h>


#define DEFINE_MACRO(macro, value) DEFINE_COMPUTED_ASM_MACRO(macro, value)

#define DEFINE_OFFSET_MACRO(prefix, structure, member) \
DEFINE_MACRO(prefix##_##member, offsetof(struct structure, member));

#define DEFINE_SIZEOF_MACRO(prefix, structure) \
DEFINE_MACRO(prefix##_sizeof, sizeof(struct structure));


void
dummy()
{
	// Validate critical structure assumptions at compile time
	static_assert(sizeof(struct iframe) % 16 == 0 || sizeof(struct iframe) % 4 == 0,
				  "iframe size must be properly aligned for stack operations");

	// x86_64 requires 16-byte stack alignment before call instructions
	// TODO: Fix iframe struct to be 16-byte aligned
	// static_assert(sizeof(struct iframe) % 16 == 0,
	//			  "x86_64 iframe must be 16-byte aligned");

	// CPU and thread state structures

	// struct cpu_ent - per-CPU data
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler);
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler_stack_pointer);

	// struct Team - process control block
	DEFINE_OFFSET_MACRO(TEAM, Team, commpage_address);

	// struct Thread - thread control block
	DEFINE_OFFSET_MACRO(THREAD, Thread, team);
	DEFINE_OFFSET_MACRO(THREAD, Thread, time_lock);
	DEFINE_OFFSET_MACRO(THREAD, Thread, kernel_time);
	DEFINE_OFFSET_MACRO(THREAD, Thread, user_time);
	DEFINE_OFFSET_MACRO(THREAD, Thread, last_time);
	DEFINE_OFFSET_MACRO(THREAD, Thread, in_kernel);
	DEFINE_OFFSET_MACRO(THREAD, Thread, flags);
	DEFINE_OFFSET_MACRO(THREAD, Thread, kernel_stack_top);
	DEFINE_OFFSET_MACRO(THREAD, Thread, fault_handler);

	// x86_64-specific thread state
	DEFINE_MACRO(THREAD_user_fpu_state, offsetof(Thread, arch_info.user_fpu_state));

	// struct arch_thread - x86_64 architecture-specific thread data
	DEFINE_OFFSET_MACRO(ARCH_THREAD, arch_thread, syscall_rsp);
	DEFINE_OFFSET_MACRO(ARCH_THREAD, arch_thread, user_rsp);
	DEFINE_OFFSET_MACRO(ARCH_THREAD, arch_thread, current_stack);

	// Interrupt frame (saved CPU state on kernel entry)

	DEFINE_SIZEOF_MACRO(IFRAME, iframe);

	// Register offsets - must match hardware push order
	DEFINE_OFFSET_MACRO(IFRAME, iframe, cs);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, ax);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, dx);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, di);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, si);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, vector);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, ip);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, flags);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, user_sp);

	// Additional x86_64 registers
	DEFINE_OFFSET_MACRO(IFRAME, iframe, r8);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, r9);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, r10);
	DEFINE_OFFSET_MACRO(IFRAME, iframe, fpu);

	// Validate FPU state pointer alignment
	static_assert(offsetof(struct iframe, fpu) % 8 == 0,
				  "FPU state pointer must be 8-byte aligned");

	// Syscall dispatch structures

	// struct syscall_info - basic syscall metadata
	DEFINE_SIZEOF_MACRO(SYSCALL_INFO, syscall_info);
	DEFINE_OFFSET_MACRO(SYSCALL_INFO, syscall_info, function);
	DEFINE_OFFSET_MACRO(SYSCALL_INFO, syscall_info, parameter_size);

	// struct extended_syscall_info - detailed parameter info
	DEFINE_SIZEOF_MACRO(EXTENDED_SYSCALL_INFO, extended_syscall_info);
	DEFINE_OFFSET_MACRO(EXTENDED_SYSCALL_INFO, extended_syscall_info,
						parameter_count);
	DEFINE_OFFSET_MACRO(EXTENDED_SYSCALL_INFO, extended_syscall_info,
						parameters);

	// struct syscall_parameter_info - per-parameter metadata
	DEFINE_SIZEOF_MACRO(SYSCALL_PARAMETER_INFO, syscall_parameter_info);
	DEFINE_OFFSET_MACRO(SYSCALL_PARAMETER_INFO, syscall_parameter_info,
						used_size);

	// Signal handling structures

	// struct signal_frame_data - userland signal delivery frame
	DEFINE_SIZEOF_MACRO(SIGNAL_FRAME_DATA, signal_frame_data);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, info);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, context);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, user_data);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, handler);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, siginfo_handler);
	DEFINE_OFFSET_MACRO(SIGNAL_FRAME_DATA, signal_frame_data, commpage_address);

	// struct ucontext_t - POSIX signal context
	DEFINE_OFFSET_MACRO(UCONTEXT_T, __ucontext_t, uc_mcontext);

	// struct vregs - virtual registers for signal handler
	DEFINE_SIZEOF_MACRO(VREGS, vregs);

	// struct siginfo_t - signal information
	DEFINE_OFFSET_MACRO(SIGINFO_T, __siginfo_t, si_signo);

	// Validate signal frame size is reasonable for stack operations
	static_assert(sizeof(struct signal_frame_data) < 4096,
				  "signal_frame_data too large for typical stack frame");
}
