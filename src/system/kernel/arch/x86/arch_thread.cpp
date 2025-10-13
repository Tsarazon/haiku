/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/thread.h>

#include <string.h>

#include <arch_cpu.h>
#include <cpu.h>
#include <kernel.h>
#include <ksignal.h>
#include <interrupts.h>
#include <team.h>
#include <thread.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

#include "paging/X86PagingStructures.h"
#include "paging/X86VMTranslationMap.h"
#include "x86_syscalls.h"


// from arch_interrupts.S
extern "C" void x86_return_to_userland(iframe* frame);

// from arch_cpu.cpp
#ifndef __x86_64__
extern void (*gX86SwapFPUFunc)(void *oldState, const void *newState);
#endif


static const addr_t STACK_FRAME_ALIGNMENT = sizeof(void*);
static const int kMaxStackFrameDepth = 512;


static bool
is_double_fault_stack_address(int32 cpu, addr_t address)
{
	size_t size;
	addr_t bottom = (addr_t)x86_get_double_fault_stack(cpu, &size);
	return address >= bottom && address < bottom + size;
}


static bool
is_kernel_stack_address(Thread* thread, addr_t address)
{
	// Early boot: no thread structure yet
	if (thread == NULL)
		return IS_KERNEL_ADDRESS(address);

	// Early boot: thread exists but stack not initialized
	if (thread->kernel_stack_top == 0)
		return IS_KERNEL_ADDRESS(address);

	// Normal kernel stack
	if (address >= thread->kernel_stack_base
		&& address < thread->kernel_stack_top) {
		return true;
		}

		// Double fault stack (critical for debugging)
		if (thread->cpu != NULL
			&& is_double_fault_stack_address(thread->cpu->cpu_num, address)) {
			return true;
			}

			return false;
}


static bool
is_valid_stack_frame(Thread* thread, addr_t frame)
{
	// Check alignment to catch corrupted stacks early
	if ((frame & (STACK_FRAME_ALIGNMENT - 1)) != 0)
		return false;

	// Use original stack address validation (handles early boot + double fault)
	if (!is_kernel_stack_address(thread, frame))
		return false;

	// Ensure we can safely read the frame pointer
	if (!is_kernel_stack_address(thread, frame + sizeof(addr_t)))
		return false;

	return true;
}


static struct iframe*
find_previous_iframe(Thread* thread, addr_t frame)
{
	// Iterate backwards through stack frames until we hit an iframe
	// Depth limit prevents infinite loops on corrupted stacks
	int depth = 0;

	while (is_valid_stack_frame(thread, frame)) {
		if (++depth > kMaxStackFrameDepth) {
			// Should never happen unless stack is corrupted
			dprintf("find_previous_iframe: exceeded max depth %d, stack may be "
			"corrupted\n", kMaxStackFrameDepth);
			return NULL;
		}

		addr_t previousFrame = *(addr_t*)frame;

		// Check for iframe marker (type encoded in low bits)
		if ((previousFrame & ~(addr_t)IFRAME_TYPE_MASK) == 0) {
			if (previousFrame == 0)
				return NULL;
			return (struct iframe*)frame;
		}

		frame = previousFrame;
	}

	return NULL;
}


static struct iframe*
get_previous_iframe(struct iframe* frame)
{
	if (frame == NULL)
		return NULL;

	return find_previous_iframe(thread_get_current_thread(), frame->bp);
}


/*!	Returns the current iframe structure of the running thread.
 * This function must only be called in a context where it's actually
 * sure that such iframe exists; ie. from syscalls, but usually not
 * from standard kernel threads.
 */
static struct iframe*
get_current_iframe(void)
{
	return find_previous_iframe(thread_get_current_thread(),
								x86_get_stack_frame());
}


/*!	\brief Returns the current thread's topmost (i.e. most recent)
 * userland->kernel transition iframe (usually the first one, save for
 * interrupts in signal handlers).
 * \return The iframe, or \c NULL, if there is no such iframe (e.g. when
 *		the thread is a kernel thread).
 */
struct iframe*
x86_get_user_iframe(void)
{
	struct iframe* frame = get_current_iframe();

	while (frame != NULL) {
		if (IFRAME_IS_USER(frame))
			return frame;
		frame = get_previous_iframe(frame);
	}

	return NULL;
}


/*!	\brief Like x86_get_user_iframe(), just for the given thread.
 * The thread must not be running and the threads spinlock must be held.
 */
struct iframe*
x86_get_thread_user_iframe(Thread* thread)
{
	if (thread == NULL)
		return NULL;

	// Thread must not be running (caller holds scheduler lock)
	if (thread->state == B_THREAD_RUNNING)
		return NULL;

	struct iframe* frame = find_previous_iframe(thread,
												thread->arch_info.GetFramePointer());

	while (frame != NULL) {
		if (IFRAME_IS_USER(frame))
			return frame;
		frame = get_previous_iframe(frame);
	}

	return NULL;
}


struct iframe*
x86_get_current_iframe(void)
{
	return get_current_iframe();
}


phys_addr_t
x86_next_page_directory(Thread* from, Thread* to)
{
	ASSERT(from != NULL);
	ASSERT(to != NULL);
	ASSERT(to->team != NULL);

	VMAddressSpace* toAddressSpace = to->team->address_space;
	if (from->team->address_space == toAddressSpace)
		return 0;

	if (toAddressSpace == NULL)
		toAddressSpace = VMAddressSpace::Kernel();

	return static_cast<X86VMTranslationMap*>(toAddressSpace->TranslationMap())
	->PagingStructures()->pgdir_phys;
}


/*!	Returns to the userland environment given by \a frame for a thread not
 * having been userland before.
 *
 * Before returning to userland all potentially necessary kernel exit work is
 * done.
 *
 * \param thread The current thread.
 * \param frame The iframe defining the userland environment. Must point to a
 *	location somewhere on the caller's stack (e.g. a local variable).
 */
void
x86_initial_return_to_userland(Thread* thread, iframe* frame)
{
	ASSERT(thread != NULL);
	ASSERT(frame != NULL);

	disable_interrupts();

	cpu_ent* cpu = get_cpu_struct();
	ASSERT(cpu != NULL);

	cpu->arch.tss.sp0 = thread->kernel_stack_top;
	x86_set_tls_context(thread);
	x86_set_syscall_stack(thread->kernel_stack_top);

	x86_return_to_userland(frame);
}


//	#pragma mark -


status_t
arch_team_init_team_struct(Team* p, bool kernel)
{
	return B_OK;
}


status_t
arch_thread_init_tls(Thread* thread)
{
	ASSERT(thread != NULL);

	thread->user_local_storage =
	thread->user_stack_base + thread->user_stack_size;
	return B_OK;
}


void
arch_thread_context_switch(Thread* from, Thread* to)
{
	ASSERT(from != NULL);
	ASSERT(to != NULL);
	ASSERT(to->cpu != NULL);
	ASSERT(to->team != NULL);

	cpu_ent* cpuData = to->cpu;

	cpuData->arch.tss.sp0 = to->kernel_stack_top;
	x86_set_syscall_stack(to->kernel_stack_top);

	// Set TLS GDT entry - must be done on correct CPU
	if (to->user_local_storage != 0)
		x86_set_tls_context(to);

	X86PagingStructures* activePagingStructures
	= cpuData->arch.active_paging_structures;
	VMAddressSpace* toAddressSpace = to->team->address_space;

	X86PagingStructures* toPagingStructures;
	if (toAddressSpace != NULL
		&& (toPagingStructures = static_cast<X86VMTranslationMap*>(
			toAddressSpace->TranslationMap())->PagingStructures())
		!= activePagingStructures) {
		// Update which CPUs are using this address space
		int cpu = cpuData->cpu_num;
	activePagingStructures->active_on_cpus.ClearBitAtomic(cpu);
	toPagingStructures->active_on_cpus.SetBitAtomic(cpu);

	// Assign new paging structures to CPU
	toPagingStructures->AddReference();
	cpuData->arch.active_paging_structures = toPagingStructures;

	// Switch page directory if needed
	addr_t newPageDirectory = toPagingStructures->pgdir_phys;
	if (newPageDirectory != activePagingStructures->pgdir_phys) {
		// CRITICAL: barrier prevents CPU from reordering CR3 load before
		// paging structure updates are visible to all CPUs
		memory_write_barrier();
		x86_swap_pgdir(newPageDirectory);
	}

	// Release old paging structures
	activePagingStructures->RemoveReference();
		}

		#ifndef __x86_64__
		gX86SwapFPUFunc(from->arch_info.fpu_state, to->arch_info.fpu_state);
		#endif

		x86_context_switch(&from->arch_info, &to->arch_info);
}


bool
arch_on_signal_stack(Thread* thread)
{
	ASSERT(thread != NULL);

	struct iframe* frame = get_current_iframe();
	if (frame == NULL)
		return false;

	return frame->user_sp >= thread->signal_stack_base
	&& frame->user_sp < thread->signal_stack_base
	+ thread->signal_stack_size;
}


/*!	Saves everything needed to restore the frame in the child fork in the
 * arch_fork_arg structure to be passed to arch_restore_fork_frame().
 * Also makes sure to return the right value.
 */
void
arch_store_fork_frame(struct arch_fork_arg* arg)
{
	ASSERT(arg != NULL);

	struct iframe* frame = x86_get_current_iframe();
	if (frame == NULL)
		panic("arch_store_fork_frame: no current iframe");

	// Copy thread's current iframe
	arg->iframe = *frame;

	// Make fork() return 0 for child
	arg->iframe.ax = 0;
}


/*!	Restores the frame from a forked team as specified by the provided
 * arch_fork_arg structure.
 * Needs to be called from within the child team, i.e. instead of
 * arch_thread_enter_userspace() as thread "starter".
 * This function does not return to the caller, but will enter userland
 * in the child team at the same position where the parent team left of.
 *
 * \param arg The architecture specific fork arguments including the
 *	environment to restore. Must point to a location somewhere on the
 *	caller's stack.
 */
void
arch_restore_fork_frame(struct arch_fork_arg* arg)
{
	ASSERT(arg != NULL);

	Thread* thread = thread_get_current_thread();
	ASSERT(thread != NULL);

	x86_initial_return_to_userland(thread, &arg->iframe);
}
