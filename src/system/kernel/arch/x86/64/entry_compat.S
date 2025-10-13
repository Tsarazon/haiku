/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#include <asm_defs.h>

#include <thread_types.h>

#include <arch/x86/descriptors.h>
#include <arch/x86/arch_altcodepatch.h>
#include <arch/x86/arch_cpu.h>
#include <arch/x86/arch_kernel.h>
#define COMMPAGE_COMPAT
#include <commpage_defs.h>

#include "asm_offsets.h"
#include "syscall_numbers.h"
#include "syscall_table.h"


// Constants for syscall argument handling
#define SYSCALL_ARG_REGISTER_COUNT	6
#define SYSCALL_ARG_REGISTER_SIZE	48		// 6 * 8 bytes
#define USER_STACK_ARG_OFFSET		4		// Skip return address for 32-bit
#define MAX_PARAM_COPY_ITERATIONS	64		// Safety limit


// Push the remainder of the interrupt frame onto the stack.
#define PUSH_IFRAME_BOTTOM(iframeType)	\
	push	%rax;	/* orig_rax */		\
	push	%rax;						\
	push	%rbx;						\
	push	%rcx;						\
	push	%rdx;						\
	push	%rdi;						\
	push	%rsi;						\
	push	%rbp;						\
	push	%r8;						\
	push	%r9;						\
	push	%r10;						\
	push	%r11;						\
	push	%r12;						\
	push	%r13;						\
	push	%r14;						\
	push	%r15;						\
	pushq	$0;							\
	push	$iframeType;


// Restore the interrupt frame.
#define RESTORE_IFRAME()				\
	add		$16, %rsp;					\
	pop		%r15;						\
	pop		%r14;						\
	pop		%r13;						\
	pop		%r12;						\
	pop		%r11;						\
	pop		%r10;						\
	pop		%r9;						\
	pop		%r8;						\
	pop		%rbp;						\
	pop		%rsi;						\
	pop		%rdi;						\
	pop		%rdx;						\
	pop		%rcx;						\
	pop		%rbx;						\
	pop		%rax;						\
	addq	$24, %rsp;


// The macros below require R12 to contain the current thread pointer. R12 is
// callee-save so will be preserved through all function calls and only needs
// to be obtained once. R13 is used to store the system call start time, will
// also be preserved.

#define LOCK_THREAD_TIME()										\
	leaq	THREAD_time_lock(%r12), %rdi;						\
	call	acquire_spinlock;

#define UNLOCK_THREAD_TIME()									\
	leaq	THREAD_time_lock(%r12), %rdi;						\
	call	release_spinlock;									\

#define UPDATE_THREAD_USER_TIME()								\
	LOCK_THREAD_TIME()											\
																\
	call	system_time;										\
	movq	%rax, %r13;											\
																\
	/* thread->user_time += now - thread->last_time; */			\
	subq	THREAD_last_time(%r12), %rax;						\
	addq	%rax, THREAD_user_time(%r12);						\
																\
	/* thread->last_time = now; */								\
	movq	%r13, THREAD_last_time(%r12);						\
																\
	/* thread->in_kernel = true; */								\
	movb	$1, THREAD_in_kernel(%r12);							\
																\
	UNLOCK_THREAD_TIME()

#define UPDATE_THREAD_KERNEL_TIME()								\
	LOCK_THREAD_TIME()											\
																\
	call	system_time;										\
	movq	%rax, %r13;											\
																\
	/* thread->kernel_time += now - thread->last_time; */		\
	subq	THREAD_last_time(%r12), %rax;						\
	addq	%rax, THREAD_kernel_time(%r12);						\
																\
	/* thread->last_time = now; */								\
	movq	%r13, THREAD_last_time(%r12);						\
																\
	/* thread->in_kernel = false; */							\
	movb	$0, THREAD_in_kernel(%r12);							\
																\
	UNLOCK_THREAD_TIME()

#define STOP_USER_DEBUGGING()									\
	testl	$(THREAD_FLAGS_BREAKPOINTS_INSTALLED				\
			| THREAD_FLAGS_SINGLE_STEP), THREAD_flags(%r12);	\
	jz		1f;													\
	call	x86_exit_user_debug_at_kernel_entry;				\
  1:

#define CLEAR_FPU_STATE() \
	pxor %xmm0, %xmm0; \
	pxor %xmm1, %xmm1; \
	pxor %xmm2, %xmm2; \
	pxor %xmm3, %xmm3; \
	pxor %xmm4, %xmm4; \
	pxor %xmm5, %xmm5; \
	pxor %xmm6, %xmm6; \
	pxor %xmm7, %xmm7; \
	pxor %xmm8, %xmm8; \
	pxor %xmm9, %xmm9; \
	pxor %xmm10, %xmm10; \
	pxor %xmm11, %xmm11; \
	pxor %xmm12, %xmm12; \
	pxor %xmm13, %xmm13; \
	pxor %xmm14, %xmm14; \
	pxor %xmm15, %xmm15


// SYSCALL entry point for 32-bit processes (AMD-style).
FUNCTION(x86_64_syscall32_entry):
	// TODO: implement for AMD SYSCALL
	sysret
FUNCTION_END(x86_64_syscall32_entry)


// SYSENTER entry point for 32-bit processes (Intel-style).
// On entry: ecx = user esp
FUNCTION(x86_64_sysenter32_entry):
	swapgs

	// Set up iframe on stack. ECX = saved ESP (zero-extended to 64-bit).
	push	$USER_DATA_SELECTOR
	movl	%ecx, %ecx
	push	%rcx
	pushfq
	orl		$(1 << 9), (%rsp)			// Set IF (interrupt enable)
	push	$USER32_CODE_SELECTOR

	// Intel SDM 5.8.7: SYSENTER doesn't save return address, must be on commpage
	movq	%gs:0, %rdx
	movq	THREAD_team(%rdx), %rdx
	movq	TEAM_commpage_address(%rdx), %rdx
	ASM_STAC
	add		4 * COMMPAGE_ENTRY_X86_SYSCALL(%rdx), %rdx
	ASM_CLAC
	add		$4, %rdx					// Skip to instruction after sysenter
	push	%rdx

	push	$0							// error_code
	push	$99							// vector
	PUSH_IFRAME_BOTTOM(IFRAME_TYPE_SYSCALL)

	cld

	// Frame pointer is the iframe.
	movq	%rsp, %rbp
	andq	$~15, %rsp

	// Preserve call number (R14 = callee-save), get thread pointer.
	movq	%rax, %r14
	movq	%gs:0, %r12

	// Defensively clear fault handler (ensure clean state).
	movq	$0, THREAD_fault_handler(%r12)

	STOP_USER_DEBUGGING()
	UPDATE_THREAD_USER_TIME()

	sti

	// Validate syscall number.
	cmpq	$SYSCALL_COUNT, %r14
	jae		.Lsyscall_return

	// Get syscall table entry (hardcoded shift for sizeof(syscall_info) = 16).
	movq	%r14, %rax
	shlq	$4, %rax
	leaq	kSyscallCompatInfos(, %rax, 1), %rax

	// Get parameter size and check if stack copy needed.
	movq	SYSCALL_INFO_parameter_size(%rax), %rcx

	// First 6 args in registers, rest on stack.
	cmpq	$SYSCALL_ARG_REGISTER_SIZE, %rcx
	ja		.Lsyscall_stack_args

.Lperform_syscall:
	testl	$THREAD_FLAGS_DEBUGGER_INSTALLED, THREAD_flags(%r12)
	jnz		.Lpre_syscall_debug

.Lpre_syscall_debug_done:
	// Pop arguments into registers (already prepared on stack).
	pop		%rdi
	pop		%rsi
	pop		%rdx
	pop		%rcx
	pop		%r8
	pop		%r9

	// Call syscall function.
	call	*SYSCALL_INFO_function(%rax)

	// Save return value in iframe (32-bit compat: eax/edx pair).
	movq	%rax, %rdx
	movq	%rax, IFRAME_ax(%rbp)
	shrq	$32, %rdx
	movq	%rdx, IFRAME_dx(%rbp)

.Lsyscall_return:
	movq	%rbp, %rsp

	testl	$(THREAD_FLAGS_DEBUGGER_INSTALLED | THREAD_FLAGS_SIGNALS_PENDING \
			| THREAD_FLAGS_DEBUG_THREAD | THREAD_FLAGS_BREAKPOINTS_DEFINED \
			| THREAD_FLAGS_TRAP_FOR_CORE_DUMP \
			| THREAD_FLAGS_64_BIT_SYSCALL_RETURN \
			| THREAD_FLAGS_RESTART_SYSCALL | THREAD_FLAGS_SYSCALL_RESTARTED) \
			, THREAD_flags(%r12)
	jnz		.Lpost_syscall_work

	cli

	UPDATE_THREAD_KERNEL_TIME()

	// Use IRET path if restoring signal frame (FPU state needs restore).
	cmpq	$SYSCALL_RESTORE_SIGNAL_FRAME, %r14
	je		.Lrestore_fpu

	CLEAR_FPU_STATE()

	RESTORE_IFRAME()
	pop		%rdx
	addq	$8, %rsp
	andl	$~0x200,(%rsp)				// Clear IF in saved RFLAGS
	popfq
	pop		%rcx

	swapgs
	sti
	sysexit


.Lpre_syscall_debug:
	push	%rdi
	push	%rsi

	movq	%r14, %rdi
	movq	0x10(%rsp), %rsi
	push	%rax
	call	user_debug_pre_syscall
	pop		%rax

	pop		%rsi
	pop		%rdi
	jmp		.Lpre_syscall_debug_done

.Lpost_syscall_work:
	// Clear restarted flag.
	testl	$(THREAD_FLAGS_64_BIT_SYSCALL_RETURN \
				| THREAD_FLAGS_SYSCALL_RESTARTED), THREAD_flags(%r12)
	jz		2f
1:
	movl	THREAD_flags(%r12), %eax
	movl	%eax, %edx
	andl	$~(THREAD_FLAGS_64_BIT_SYSCALL_RETURN \
				| THREAD_FLAGS_SYSCALL_RESTARTED), %edx
	lock
	cmpxchgl	%edx, THREAD_flags(%r12)
	jnz		1b
2:
	testl	$THREAD_FLAGS_DEBUGGER_INSTALLED, THREAD_flags(%r12)
	jz		1f

	push	IFRAME_r9(%rbp)
	push	IFRAME_r8(%rbp)
	push	IFRAME_r10(%rbp)
	push	IFRAME_dx(%rbp)
	push	IFRAME_si(%rbp)
	push	IFRAME_di(%rbp)
	movq	%r14, %rdi
	movq	%rsp, %rsi
	movq	IFRAME_ax(%rbp), %rdx
	call	user_debug_post_syscall
	addq	$48, %rsp
1:
	testl	$(THREAD_FLAGS_SIGNALS_PENDING | THREAD_FLAGS_DEBUG_THREAD \
			| THREAD_FLAGS_TRAP_FOR_CORE_DUMP) \
			, THREAD_flags(%r12)
	jnz		.Lpost_syscall_handle_signals
	cli
	call	thread_at_kernel_exit_no_signals

.Lpost_syscall_work_done:
	testl	$THREAD_FLAGS_RESTART_SYSCALL, THREAD_flags(%r12)
	jz		1f
	movq	%rsp, %rdi
	call	x86_restart_syscall
1:
	testl	$THREAD_FLAGS_BREAKPOINTS_DEFINED, THREAD_flags(%r12)
	jz		1f
	movq	%rbp, %rdi
	call	x86_init_user_debug_at_kernel_exit
1:
	CLEAR_FPU_STATE()
	jmp .Liret

.Lrestore_fpu:
	movq	IFRAME_fpu(%rbp), %rax
	fxrstorq	(%rax)
.Liret:
	RESTORE_IFRAME()
	swapgs
	iretq

.Lpost_syscall_handle_signals:
	call	thread_at_kernel_exit
	jmp		.Lpost_syscall_work_done

.Lbad_syscall_args:
	ASM_CLAC							// Disable SMAP bypass on fault path
	movq	$0, THREAD_fault_handler(%r12)
	movq	%rbp, %rsp
	jmp		.Lsyscall_return

// Handle syscalls with >6 arguments (need to copy from user stack).
.Lsyscall_stack_args:
	// RAX = syscall table entry, RCX = total param size in bytes.
	subq	$SYSCALL_ARG_REGISTER_SIZE, %rcx

	// Get user stack source address (skip 32-bit return address).
	movq	IFRAME_user_sp(%rbp), %rsi
	addq	$USER_STACK_ARG_OFFSET, %rsi

	// Validate user pointer is in userspace.
	movabs	$(USER_BASE + USER_SIZE), %rdx
	cmp		%rdx, %rsi
	jae		.Lbad_syscall_args

	// Double parameter size for 32->64 bit conversion.
	shlq	$1, %rcx

	// Allocate aligned stack space (minimum 48 bytes for register args).
	cmpq	$SYSCALL_ARG_REGISTER_SIZE, %rcx
	ja		.Lprepare_stack
	movq	$SYSCALL_ARG_REGISTER_SIZE, %rcx
.Lprepare_stack:
	subq	%rcx, %rsp
	andq	$~15, %rsp
	movq	%rsp, %rdi

	// Get extended syscall info for parameter metadata.
	movq	%r14, %r15
	imulq	$ EXTENDED_SYSCALL_INFO_sizeof, %r15
	leaq	kExtendedSyscallCompatInfos(, %r15, 1), %r15
	xor		%rcx, %rcx
	movl	EXTENDED_SYSCALL_INFO_parameter_count(%r15), %ecx
	leaq	EXTENDED_SYSCALL_INFO_parameters(%r15), %r15

	// Set fault handler for user memory access.
	movq	$.Lbad_syscall_args, THREAD_fault_handler(%r12)

	ASM_STAC

	// Safety counter to prevent infinite loop on bad pointers.
	movl	$MAX_PARAM_COPY_ITERATIONS, %r8d
	jmp		.Lcopy_param_loop_check

.Lcopy_param_loop:
	// Safety check: prevent infinite loop.
	decl	%r8d
	jz		.Lbad_syscall_args

	addq	$ SYSCALL_PARAMETER_INFO_sizeof, %r15
	decq	%rcx

.Lcopy_param_loop_check:
	testq	%rcx, %rcx
	jz		.Lcopy_done

	// Copy 32-bit parameter (zero-extended to 64-bit).
	movsd
	cmpl	$8, SYSCALL_PARAMETER_INFO_used_size(%r15)
	je		.Lcopy_64bit_param

	// 32-bit param: zero-extend high dword.
	movl	$0, (%rdi)
	addq	$4, %rdi
	jmp		.Lcopy_param_loop

.Lcopy_64bit_param:
	// Copy second dword for 64-bit parameter.
	movsd
	jmp		.Lcopy_param_loop

.Lcopy_done:
	ASM_CLAC
	movq	$0, THREAD_fault_handler(%r12)
	jmp		.Lperform_syscall

FUNCTION_END(x86_64_sysenter32_entry)


// 32-bit thread exit stub (executed in userspace).
FUNCTION(x86_sysenter32_userspace_thread_exit):
	.byte	0x50					// push %eax
	mov		$SYSCALL_EXIT_THREAD, %eax
	.byte	0x89,0xe1				// mov %esp, %ecx
	sysenter
FUNCTION_END(x86_sysenter32_userspace_thread_exit)
SYMBOL(x86_sysenter32_userspace_thread_exit_end):
