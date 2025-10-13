/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011, Michael Lotz, mmlr@mlotz.ch.
 * Copyright 2010, Clemens Zeidler, haiku@clemens-zeidler.de.
 * Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*!	\file arch_int.cpp
 * \brief x86/x86_64 interrupt and exception handling
 *
 * This module handles:
 * - CPU exceptions (divide by zero, page faults, etc.)
 * - Hardware interrupts (IRQ routing via PIC/APIC/MSI)
 * - Exception-to-signal conversion for userland
 * - Interrupt controller abstraction
 *
 * References:
 * - Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A
 * - AMD64 Architecture Programmer's Manual, Volume 2: System Programming
 */


#include <cpu.h>
#include <interrupts.h>
#include <kscheduler.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>

#include <arch/cpu.h>
#include <arch/int.h>

#include <arch/x86/apic.h>
#include <arch/x86/descriptors.h>
#include <arch/x86/msi.h>
#include <arch/x86/msi_priv.h>

#include <fenv.h>
#include <stdio.h>

// interrupt controllers
#include <arch/x86/ioapic.h>
#include <arch/x86/pic.h>


//#define TRACE_ARCH_INT
#ifdef TRACE_ARCH_INT
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// #pragma mark - Constants and Globals


// Exception vector numbers (Intel SDM Vol. 3A, Table 6-1)
static const char *kInterruptNames[] = {
	/*  0 */ "Divide Error Exception",              // #DE
	/*  1 */ "Debug Exception",                     // #DB
	/*  2 */ "NMI Interrupt",                       // NMI
	/*  3 */ "Breakpoint Exception",                // #BP
	/*  4 */ "Overflow Exception",                  // #OF
	/*  5 */ "BOUND Range Exceeded Exception",     // #BR
	/*  6 */ "Invalid Opcode Exception",            // #UD
	/*  7 */ "Device Not Available Exception",      // #NM
	/*  8 */ "Double Fault Exception",              // #DF
	/*  9 */ "Coprocessor Segment Overrun",         // (reserved)
	/* 10 */ "Invalid TSS Exception",               // #TS
	/* 11 */ "Segment Not Present",                 // #NP
	/* 12 */ "Stack Fault Exception",               // #SS
	/* 13 */ "General Protection Exception",        // #GP
	/* 14 */ "Page-Fault Exception",                // #PF
	/* 15 */ "-",                                   // (reserved)
	/* 16 */ "x87 FPU Floating-Point Error",        // #MF
	/* 17 */ "Alignment Check Exception",           // #AC
	/* 18 */ "Machine-Check Exception",             // #MC
	/* 19 */ "SIMD Floating-Point Exception",       // #XM/#XF
};
static const int kInterruptNameCount = 20;

// Global interrupt routing state
static irq_source sVectorSources[NUM_IO_VECTORS];
static const interrupt_controller* sCurrentPIC = NULL;


// #pragma mark - Helper Functions


/*!	Get human-readable exception name
 * \param number Exception vector number (0-255)
 * \param buffer Fallback buffer for unknown exceptions
 * \param bufferSize Size of fallback buffer
 * \return Exception name string (either from table or formatted in buffer)
 */
static const char*
exception_name(int number, char* buffer, int32 bufferSize)
{
	if (number >= 0 && number < kInterruptNameCount)
		return kInterruptNames[number];

	snprintf(buffer, bufferSize, "exception %d", number);
	return buffer;
}


// #pragma mark - Exception Handlers


/*!	Handler for completely unhandled/unexpected exceptions
 * Panics the system as the state is undefined.
 * \param frame CPU state at exception time
 */
void
x86_invalid_exception(iframe* frame)
{
	Thread* thread = thread_get_current_thread();
	char name[32];
	panic("unhandled trap 0x%lx (%s) at ip 0x%lx, thread %" B_PRId32 "!\n",
		  (long unsigned int)frame->vector,
		  exception_name(frame->vector, name, sizeof(name)),
		  (long unsigned int)frame->ip, thread ? thread->id : -1);
}


/*!	Handler for fatal CPU exceptions that should never occur
 * Examples: Double Fault (#DF), Machine Check (#MC)
 * Intel SDM Vol. 3A, Section 6.15
 * \param frame CPU state at exception time
 */
void
x86_fatal_exception(iframe* frame)
{
	char name[32];
	panic("Fatal exception \"%s\" occurred! Error code: 0x%lx\n",
		  exception_name(frame->vector, name, sizeof(name)),
		  (long unsigned int)frame->error_code);
}


/*!	Handler for CPU exceptions that can be recovered from userland
 *
 * Converts CPU exceptions to POSIX signals for userland:
 * - Divide by zero (#DE) → SIGFPE
 * - Invalid opcode (#UD) → SIGILL
 * - Page fault (#PF) → SIGSEGV (handled separately)
 * - FPU exceptions (#MF, #XM) → SIGFPE
 * - Stack fault (#SS) → SIGBUS
 * - General protection (#GP) → SIGILL
 * - Alignment check (#AC) → SIGBUS
 *
 * For kernel-mode exceptions, panics immediately.
 * Intel SDM Vol. 3A, Chapter 6
 *
 * \param frame CPU state at exception time
 */
void
x86_unexpected_exception(iframe* frame)
{
	debug_exception_type type;
	uint32 signalNumber;
	int32 signalCode;
	addr_t signalAddress = 0;
	int32 signalError = B_ERROR;

	// Map CPU exception to signal type (POSIX.1-2008)
	switch (frame->vector) {
		case 0:		// Divide Error Exception (#DE)
			type = B_DIVIDE_ERROR;
			signalNumber = SIGFPE;
			signalCode = FPE_INTDIV;
			signalAddress = frame->ip;
			break;

		case 4:		// Overflow Exception (#OF)
			type = B_OVERFLOW_EXCEPTION;
			signalNumber = SIGFPE;
			signalCode = FPE_INTOVF;
			signalAddress = frame->ip;
			break;

		case 5:		// BOUND Range Exceeded Exception (#BR)
			type = B_BOUNDS_CHECK_EXCEPTION;
			signalNumber = SIGTRAP;
			signalCode = SI_USER;
			break;

		case 6:		// Invalid Opcode Exception (#UD)
			type = B_INVALID_OPCODE_EXCEPTION;
			signalNumber = SIGILL;
			signalCode = ILL_ILLOPC;
			signalAddress = frame->ip;
			break;

		case 12: 	// Stack Fault (#SS)
			type = B_STACK_FAULT;
			signalNumber = SIGBUS;
			signalCode = BUS_ADRERR;
			signalAddress = frame->ip;
			break;

		case 13: 	// General Protection Exception (#GP)
			type = B_GENERAL_PROTECTION_FAULT;
			signalNumber = SIGILL;
			signalCode = ILL_PRVOPC;	// or ILL_PRVREG
			signalAddress = frame->ip;
			break;

		case 16: 	// x87 FPU Floating-Point Error (#MF)
		case 19: 	// SIMD Floating-Point Exception (#XM/#XF)
		{
			type = B_FLOATING_POINT_EXCEPTION;
			signalNumber = SIGFPE;
			signalCode = FPE_FLTINV;
			signalAddress = frame->ip;

			// Read FPU/SSE status to determine exact cause
			// Intel SDM Vol. 1, Section 8.7 (FPU), Section 11.5 (SSE)
			uint32 fpuStatus = 0;
			if (frame->vector == 19) {
				// MXCSR only available on SSE-capable CPUs
				// Exception 19 implies OSXMMEXCPT is set (CR4.OSXMMEXCPT)
				__stmxcsr(&fpuStatus);
			} else {
				uint16 fswRegister = 0;
				__fnstsw(&fswRegister);
				fpuStatus = fswRegister;
			}

			// Map FPU status flags to signal codes (IEEE 754)
			if ((fpuStatus & FE_INVALID) != 0)
				signalCode = FPE_FLTINV;      // Invalid operation
				else if ((fpuStatus & FE_DENORMAL) != 0)
					signalCode = FPE_FLTUND;      // Denormal operand
					else if ((fpuStatus & FE_DIVBYZERO) != 0)
						signalCode = FPE_FLTDIV;      // Divide by zero
						else if ((fpuStatus & FE_OVERFLOW) != 0)
							signalCode = FPE_FLTOVF;      // Overflow
							else if ((fpuStatus & FE_UNDERFLOW) != 0)
								signalCode = FPE_FLTUND;      // Underflow
								else if ((fpuStatus & FE_INEXACT) != 0)
									signalCode = FPE_FLTRES;      // Inexact result
									break;
		}

		case 17: 	// Alignment Check Exception (#AC)
			// Requires EFLAGS.AC=1 and CR0.AM=1
			// Intel SDM Vol. 3A, Section 6.15
			type = B_ALIGNMENT_EXCEPTION;
			signalNumber = SIGBUS;
			signalCode = BUS_ADRALN;
			signalError = EFAULT;
			// TODO: Also get the address (from where?). Since we don't enable
			// alignment checking this exception should never happen, though.
			break;

		default:
			x86_invalid_exception(frame);
			return;
	}

	if (IFRAME_IS_USER(frame)) {
		struct sigaction action;
		Thread* thread = thread_get_current_thread();

		enable_interrupts();

		// If the thread has a signal handler for the signal, we simply send it
		// the signal. Otherwise we notify the user debugger first.
		if ((sigaction(signalNumber, NULL, &action) == 0
			&& action.sa_handler != SIG_DFL
			&& action.sa_handler != SIG_IGN)
			|| user_debug_exception_occurred(type, signalNumber)) {
			Signal signal(signalNumber, signalCode, signalError,
						  thread->team->id);
			signal.SetAddress((void*)signalAddress);
		send_signal_to_thread(thread, signal, 0);
			}
	} else {
		// Kernel-mode exception - this is always a bug
		char name[32];
		panic("Unexpected exception \"%s\" occurred in kernel mode! "
		"Error code: 0x%lx\n",
		exception_name(frame->vector, name, sizeof(name)),
			  (long unsigned int)(frame->error_code));
	}
}


// #pragma mark - Hardware Interrupt Handler


/*!	Main hardware interrupt dispatcher
 *
 * Routes hardware interrupts (IRQs) from PIC/IO-APIC/MSI to device drivers.
 * Handles spurious interrupts and EOI (End of Interrupt) signaling.
 *
 * Edge-triggered interrupts: EOI before handler (prevents missed interrupts)
 * Level-triggered interrupts: EOI after handler (prevents interrupt storms)
 *
 * Intel SDM Vol. 3A, Section 10.8.5
 *
 * \param frame CPU state at interrupt time
 */
void
x86_hardware_interrupt(struct iframe* frame)
{
	// Convert IDT vector to normalized IRQ number
	int32 vector = frame->vector - ARCH_INTERRUPT_BASE;
	bool levelTriggered = false;
	Thread* thread = thread_get_current_thread();

	// Check for spurious interrupts (Intel SDM Vol. 3A, Section 10.9)
	if (sCurrentPIC->is_spurious_interrupt(vector)) {
		TRACE(("got spurious interrupt at vector %ld\n", vector));
		return;
	}

	levelTriggered = sCurrentPIC->is_level_triggered_interrupt(vector);

	// For edge-triggered interrupts: send EOI before handler
	// This allows the interrupt line to rise again while we're servicing
	if (!levelTriggered) {
		// If PIC doesn't handle this vector, it's from APIC/MSI
		if (!sCurrentPIC->end_of_interrupt(vector))
			apic_end_of_interrupt();
	}

	// Dispatch to registered interrupt handler(s)
	io_interrupt_handler(vector, levelTriggered);

	// For level-triggered interrupts: send EOI after handler
	// This prevents interrupt storms if the handler didn't clear the source
	if (levelTriggered) {
		if (!sCurrentPIC->end_of_interrupt(vector))
			apic_end_of_interrupt();
	}

	// Handle post-interrupt callbacks and rescheduling
	cpu_status state = disable_interrupts();
	if (thread->post_interrupt_callback != NULL) {
		void (*callback)(void*) = thread->post_interrupt_callback;
		void* data = thread->post_interrupt_data;

		thread->post_interrupt_callback = NULL;
		thread->post_interrupt_data = NULL;

		restore_interrupts(state);
		callback(data);
	} else if (thread->cpu->invoke_scheduler) {
		SpinLocker schedulerLocker(thread->scheduler_lock);
		scheduler_reschedule(B_THREAD_READY);
		schedulerLocker.Unlock();
		restore_interrupts(state);
	}
}


// #pragma mark - Page Fault Handler


/*!	Page fault exception handler (#PF, vector 14)
 *
 * Handles page faults by invoking the VM subsystem to:
 * - Map in new pages (demand paging)
 * - Handle copy-on-write
 * - Detect access violations
 * - Support user_memcpy() with fault handlers
 *
 * Error code format (Intel SDM Vol. 3A, Section 4.7):
 * - Bit 0 (P):  0 = not present, 1 = protection violation
 * - Bit 1 (W):  0 = read access, 1 = write access
 * - Bit 2 (U):  0 = supervisor mode, 1 = user mode
 * - Bit 3 (R):  1 = reserved bit violation
 * - Bit 4 (I):  1 = instruction fetch
 *
 * Faulting address is in CR2 register.
 *
 * \param frame CPU state at fault time
 */
void
x86_page_fault_exception(struct iframe* frame)
{
	Thread* thread = thread_get_current_thread();
	addr_t faultAddress = x86_read_cr2();
	addr_t newip;

	// Special case: page fault in kernel debugger
	if (debug_debugger_running()) {
		// If this CPU or thread has a fault handler, we're allowed to be here
		if (thread != NULL) {
			cpu_ent* cpu = &gCPU[smp_get_current_cpu()];
			if (cpu->fault_handler != 0) {
				debug_set_page_fault_info(faultAddress, frame->ip,
										  (frame->error_code & PGFAULT_W) != 0
										  ? DEBUG_PAGE_FAULT_WRITE : 0);
				frame->ip = cpu->fault_handler;
				frame->bp = cpu->fault_handler_stack_pointer;
				return;
			}

			if (thread->fault_handler != 0) {
				kprintf("ERROR: thread::fault_handler used in kernel "
				"debugger!\n");
				debug_set_page_fault_info(faultAddress, frame->ip,
										  (frame->error_code & PGFAULT_W) != 0
										  ? DEBUG_PAGE_FAULT_WRITE : 0);
				frame->ip = reinterpret_cast<uintptr_t>(thread->fault_handler);
				return;
			}
		}

		panic("page fault in debugger without fault handler! Touching "
		"address %p from ip %p\n", (void*)faultAddress, (void*)frame->ip);
		return;
	}

	// Check for SMEP violation (Supervisor Mode Execution Prevention)
	// Intel SDM Vol. 3A, Section 4.6
	// Prevents kernel from executing user-mapped pages
	if (!IFRAME_IS_USER(frame)
		&& (frame->error_code & PGFAULT_I) != 0
		&& (x86_read_cr4() & IA32_CR4_SMEP) != 0) {
		panic("SMEP violation user-mapped address %p touched from kernel %p\n",
			  (void*)faultAddress, (void*)frame->ip);
		}

		// Check for SMAP violation (Supervisor Mode Access Prevention)
		// Intel SDM Vol. 3A, Section 4.6
		// Prevents kernel from accessing user-mapped pages (unless EFLAGS.AC=1)
		if ((frame->flags & X86_EFLAGS_ALIGNMENT_CHECK) == 0
			&& !IFRAME_IS_USER(frame)
			&& (frame->error_code & PGFAULT_P) != 0
			&& (x86_read_cr4() & IA32_CR4_SMAP) != 0) {
			panic("SMAP violation user-mapped address %p touched from kernel %p\n",
				  (void*)faultAddress, (void*)frame->ip);
			}

			// Check if interrupts were disabled (usually indicates bug)
			if ((frame->flags & X86_EFLAGS_INTERRUPT) == 0) {
				// Exception: user_memcpy() and friends are allowed with interrupts disabled
				if (thread != NULL && thread->fault_handler != 0) {
					uintptr_t handler = reinterpret_cast<uintptr_t>(thread->fault_handler);
					if (frame->ip != handler) {
						frame->ip = handler;
						return;
					}

					// Fault happened AT the fault handler → infinite loop detected
					panic("page fault, interrupts disabled, fault handler loop. "
					"Touching address %p from ip %p\n", (void*)faultAddress,
						  (void*)frame->ip);
				}

				panic("page fault, but interrupts were disabled. Touching address "
				"%p from ip %p\n", (void*)faultAddress, (void*)frame->ip);
				return;
			}

			// Check if page faults are allowed at this point
			if (thread != NULL && thread->page_faults_allowed < 1) {
				panic("page fault not allowed at this place. Touching address "
				"%p from ip %p\n", (void*)faultAddress, (void*)frame->ip);
				return;
			}

			// Let the VM handle the fault
			enable_interrupts();

			vm_page_fault(faultAddress, frame->ip,
						  (frame->error_code & PGFAULT_W) != 0,		// write access
						  (frame->error_code & PGFAULT_I) != 0,		// instruction fetch
						  IFRAME_IS_USER(frame),						// userland
						  &newip);
			if (newip != 0) {
				// VM wants us to redirect execution (e.g., signal handler)
				frame->ip = newip;
			}
}


// #pragma mark - Interrupt Source Management


/*!	Associate an IRQ vector with its source type
 * Used for MSI interrupt tracking.
 * \param irq IRQ vector number
 * \param source Source type (IRQ_SOURCE_IOAPIC, IRQ_SOURCE_MSI, etc.)
 */
void
x86_set_irq_source(int32 irq, irq_source source)
{
	sVectorSources[irq] = source;
}


// #pragma mark - Interrupt Control API


/*!	Enable (unmask) an I/O interrupt
 * Routes to the active interrupt controller (PIC/APIC/etc.)
 * \param irq IRQ number to enable
 */
void
arch_int_enable_io_interrupt(int32 irq)
{
	sCurrentPIC->enable_io_interrupt(irq);
}


/*!	Disable (mask) an I/O interrupt
 * \param irq IRQ number to disable
 */
void
arch_int_disable_io_interrupt(int32 irq)
{
	sCurrentPIC->disable_io_interrupt(irq);
}


/*!	Configure interrupt trigger mode and polarity
 * \param irq IRQ number to configure
 * \param config Configuration flags (B_LEVEL_TRIGGERED, B_EDGE_TRIGGERED, etc.)
 */
void
arch_int_configure_io_interrupt(int32 irq, uint32 config)
{
	sCurrentPIC->configure_io_interrupt(irq, config);
}


// Implement inline functions for non-inline contexts
#undef arch_int_enable_interrupts
#undef arch_int_disable_interrupts
#undef arch_int_restore_interrupts
#undef arch_int_are_interrupts_enabled


void
arch_int_enable_interrupts(void)
{
	arch_int_enable_interrupts_inline();
}


int
arch_int_disable_interrupts(void)
{
	return arch_int_disable_interrupts_inline();
}


void
arch_int_restore_interrupts(int oldState)
{
	arch_int_restore_interrupts_inline(oldState);
}


bool
arch_int_are_interrupts_enabled(void)
{
	return arch_int_are_interrupts_enabled_inline();
}


/*!	Assign an interrupt to a specific CPU
 * Used for load balancing and CPU affinity.
 * Only works with IO-APIC or MSI, not with legacy PIC.
 * \param irq IRQ number
 * \param cpu Target CPU number
 * \return The CPU number where the interrupt was assigned
 */
int32
arch_int_assign_to_cpu(int32 irq, int32 cpu)
{
	switch (sVectorSources[irq]) {
		case IRQ_SOURCE_IOAPIC:
			if (sCurrentPIC->assign_interrupt_to_cpu != NULL)
				sCurrentPIC->assign_interrupt_to_cpu(irq, cpu);
		break;

		case IRQ_SOURCE_MSI:
			msi_assign_interrupt_to_cpu(irq, cpu);
			break;

		default:
			break;
	}
	return cpu;
}


// #pragma mark - Initialization


/*!	Early interrupt subsystem initialization
 * Sets up the basic PIC (8259A) controller.
 * Intel SDM Vol. 3A, Section 10.2
 * \param args Kernel arguments from bootloader
 * \return B_OK on success
 */
status_t
arch_int_init(kernel_args* args)
{
	// Initialize the legacy 8259 PIC
	// This will be replaced by IO-APIC later if available
	pic_init();
	return B_OK;
}


/*!	Post-VM interrupt initialization
 * Initializes the local APIC after VM is available.
 * \param args Kernel arguments from bootloader
 * \return B_OK on success
 */
status_t
arch_int_init_post_vm(kernel_args* args)
{
	// Initialize local APIC (used even without IO-APIC for timers)
	apic_init(args);
	return B_OK;
}


/*!	I/O interrupt controller initialization
 * Initializes MSI and IO-APIC if available.
 * \param args Kernel arguments from bootloader
 * \return B_OK on success
 */
status_t
arch_int_init_io(kernel_args* args)
{
	msi_init(args);
	ioapic_preinit(args);
	return B_OK;
}


/*!	Post-device-manager interrupt initialization
 * Reserved for future use.
 * \param args Kernel arguments from bootloader
 * \return B_OK on success
 */
status_t
arch_int_init_post_device_manager(kernel_args* args)
{
	return B_OK;
}


/*!	Switch to a different interrupt controller
 * Allows transitioning from PIC to IO-APIC at runtime.
 * \param controller New interrupt controller to use
 */
void
arch_int_set_interrupt_controller(const interrupt_controller& controller)
{
	sCurrentPIC = &controller;
}
