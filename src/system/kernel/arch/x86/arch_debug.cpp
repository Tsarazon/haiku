/*
 * Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/debug.h>

#include <x86intrin.h>
#include <stdio.h>
#include <stdlib.h>

#include <ByteOrder.h>
#include <TypeConstants.h>

#include <cpu.h>
#include <debug.h>
#include <debug_heap.h>
#include <elf.h>
#include <kernel.h>
#include <kimage.h>
#include <thread.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>


// Stack frame structure for x86 calling convention
// Reference: System V ABI x86-64 Architecture Processor Supplement
struct stack_frame {
	stack_frame*	previous;		// Frame pointer (RBP/EBP) of caller
	addr_t			return_address;	// Return address (saved RIP/EIP)
};

// Circular buffer size for loop detection in stack unwinding
#define NUM_PREVIOUS_LOCATIONS 32


static bool is_kernel_stack_address(Thread* thread, addr_t address);


// #pragma mark - Stack Frame Traversal Helpers


/*!	Detects loops in stack unwinding by maintaining circular buffer of visited frames
 *
 * Stack corruption or infinite recursion can create loops. We track the last
 * NUM_PREVIOUS_LOCATIONS frame pointers to detect revisits.
 *
 * \param visited Circular buffer of previously visited frame pointers
 * \param _last Index of most recently added entry (wrapped modulo buffer size)
 * \param _num Current number of entries in buffer (capped at buffer size)
 * \param bp Frame pointer to check for duplicate
 * \return true if frame pointer was already visited (loop detected)
 */
static bool
already_visited(addr_t* visited, int32* _last, int32* _num, addr_t bp)
{
	int32 last = *_last;
	int32 num = *_num;

	// Search backward through circular buffer for matching frame pointer
	for (int32 i = 0; i < num; i++) {
		int32 index = (NUM_PREVIOUS_LOCATIONS + last - i) % NUM_PREVIOUS_LOCATIONS;
		if (visited[index] == bp)
			return true;
	}

	// Add new entry to circular buffer
	last = (last + 1) % NUM_PREVIOUS_LOCATIONS;
	*_last = last;
	visited[last] = bp;

	// Increase count until buffer is full
	if (num < NUM_PREVIOUS_LOCATIONS)
		*_num = num + 1;

	return false;
}


/*!	Retrieves next stack frame in non-debugger context
 *
 * Uses safe memory access functions (memcpy/user_memcpy) appropriate for
 * context. Cannot be called from kernel debugger where normal paging may
 * not be available.
 *
 * \param bp Current frame pointer (RBP/EBP)
 * \param _next Output: next frame pointer from stack_frame::previous
 * \param _ip Output: return address from stack_frame::return_address
 * \param onKernelStack true if bp points to kernel stack memory
 * \param thread Current thread (for kernel stack bounds checking)
 * \return B_OK on success, B_BAD_ADDRESS if frame inaccessible
 */
static status_t
get_next_frame_no_debugger(addr_t bp, addr_t* _next, addr_t* _ip,
						   bool onKernelStack, Thread* thread)
{
	stack_frame frame;

	// Use appropriate memory copy based on address space
	if (onKernelStack && is_kernel_stack_address(thread, bp + sizeof(frame) - 1)) {
		// Kernel stack: direct memory access safe
		memcpy(&frame, (void*)bp, sizeof(frame));
	} else if (!IS_USER_ADDRESS(bp)
		|| user_memcpy(&frame, (void*)bp, sizeof(frame)) != B_OK) {
		// User stack: use safe copy that handles page faults
		return B_BAD_ADDRESS;
		}

		*_ip = frame.return_address;
	*_next = (addr_t)frame.previous;

	return B_OK;
}


/*!	Retrieves next stack frame in kernel debugger context
 *
 * Uses debug_memcpy which bypasses normal memory protection. This is necessary
 * in the debugger where paging structures may be in unknown state.
 *
 * \param bp Current frame pointer (RBP/EBP)
 * \param _next Output: next frame pointer
 * \param _ip Output: return address
 * \return B_OK on success, B_BAD_ADDRESS if frame inaccessible even in debug mode
 */
static status_t
get_next_frame_debugger(addr_t bp, addr_t* _next, addr_t* _ip)
{
	stack_frame frame;

	// debug_memcpy can read any physical memory, even if not mapped normally
	if (debug_memcpy(B_CURRENT_TEAM, &frame, (void*)bp, sizeof(frame)) != B_OK)
		return B_BAD_ADDRESS;

	*_ip = frame.return_address;
	*_next = (addr_t)frame.previous;

	return B_OK;
}


/*!	Resolves address to symbol name and image
 *
 * Looks up symbol in both kernel and userland images. For userland, uses
 * runtime loader structures if available.
 *
 * \param thread Thread context for userland lookups (may be NULL)
 * \param address Address to resolve
 * \param _baseAddress Output: base address of symbol (optional)
 * \param _symbolName Output: symbol name (optional)
 * \param _imageName Output: image name (optional)
 * \param _exactMatch Output: true if address exactly matches symbol start (optional)
 * \return B_OK if symbol found, B_ENTRY_NOT_FOUND otherwise
 */
static status_t
lookup_symbol(Thread* thread, addr_t address, addr_t* _baseAddress,
			  const char** _symbolName, const char** _imageName, bool* _exactMatch)
{
	status_t status = B_ENTRY_NOT_FOUND;

	if (IS_KERNEL_ADDRESS(address)) {
		// Kernel symbol lookup
		status = elf_debug_lookup_symbol_address(address, _baseAddress,
												 _symbolName, _imageName, _exactMatch);
	} else if (thread != NULL && thread->team != NULL) {
		// Userland symbol lookup via runtime loader structures
		status = elf_debug_lookup_user_symbol_address(thread->team, address,
													  _baseAddress, _symbolName, _imageName, _exactMatch);
	}

	return status;
}


// #pragma mark - CPU Register Access (Architecture-Specific)


#ifdef __x86_64__

/*!	Extracts CPU register state from x86_64 interrupt frame
 *
 * Populates debug_cpu_state with all general purpose registers from iframe.
 * Segment registers (ds, es, fs, gs) are read from current CPU state since
 * they're not saved in interrupt frames on x86_64.
 *
 * Reference: Intel SDM Vol. 3A, Section 6.14.1 (64-bit mode interrupt stack frame)
 */
static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	// General purpose registers from interrupt frame
	cpuState->r15 = frame->r15;
	cpuState->r14 = frame->r14;
	cpuState->r13 = frame->r13;
	cpuState->r12 = frame->r12;
	cpuState->r11 = frame->r11;
	cpuState->r10 = frame->r10;
	cpuState->r9 = frame->r9;
	cpuState->r8 = frame->r8;
	cpuState->rbp = frame->bp;
	cpuState->rsi = frame->si;
	cpuState->rdi = frame->di;
	cpuState->rdx = frame->dx;
	cpuState->rcx = frame->cx;
	cpuState->rbx = frame->bx;
	cpuState->rax = frame->ax;

	// Exception information
	cpuState->vector = frame->vector;
	cpuState->error_code = frame->error_code;

	// Program counter and stack pointer
	cpuState->rip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->rflags = frame->flags;
	cpuState->rsp = frame->sp;
	cpuState->ss = frame->ss;

	// Segment registers are not saved/restored on x86_64 interrupts
	// Read current values instead
	uint16 seg;
	__asm__ volatile ("movw %%ds, %0" : "=r" (seg));
	cpuState->ds = seg;
	__asm__ volatile ("movw %%es, %0" : "=r" (seg));
	cpuState->es = seg;
	__asm__ volatile ("movw %%fs, %0" : "=r" (seg));
	cpuState->fs = seg;
	__asm__ volatile ("movw %%gs, %0" : "=r" (seg));
	cpuState->gs = seg;
}


/*!	Updates x86_64 interrupt frame with modified register values
 *
 * Only user-modifiable registers are updated. Segment registers and some
 * RFLAGS bits are protected from modification to prevent privilege escalation.
 *
 * Reference: Intel SDM Vol. 3A, Section 6.14.1
 */
static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
	// Update general purpose registers
	frame->r15 = cpuState->r15;
	frame->r14 = cpuState->r14;
	frame->r13 = cpuState->r13;
	frame->r12 = cpuState->r12;
	frame->r11 = cpuState->r11;
	frame->r10 = cpuState->r10;
	frame->r9 = cpuState->r9;
	frame->r8 = cpuState->r8;
	frame->bp = cpuState->rbp;
	frame->si = cpuState->rsi;
	frame->di = cpuState->rdi;
	frame->dx = cpuState->rdx;
	frame->cx = cpuState->rcx;
	frame->bx = cpuState->rbx;
	frame->ax = cpuState->rax;

	// Update instruction pointer
	frame->ip = cpuState->rip;

	// Update RFLAGS: preserve system bits, allow only user-settable bits
	// User-settable: CF, PF, AF, ZF, SF, TF, DF, OF, NT, AC, ID
	// Protected: IF, IOPL, RF, VM, VIF, VIP (prevent privilege escalation)
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
	| (cpuState->rflags & X86_EFLAGS_USER_SETTABLE_FLAGS);

	// Update stack pointer
	frame->sp = cpuState->rsp;
}


#else	// __x86_64__ (32-bit x86)


/*!	Extracts CPU register state from x86 interrupt frame
 *
 * On 32-bit x86, all segment registers are saved in the interrupt frame.
 *
 * Reference: Intel SDM Vol. 3A, Section 6.12.1 (protected mode interrupt stack frame)
 */
static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	// Segment registers (saved in frame on 32-bit)
	cpuState->gs = frame->gs;
	cpuState->fs = frame->fs;
	cpuState->es = frame->es;
	cpuState->ds = frame->ds;

	// General purpose registers
	cpuState->edi = frame->di;
	cpuState->esi = frame->si;
	cpuState->ebp = frame->bp;
	cpuState->esp = frame->sp;
	cpuState->ebx = frame->bx;
	cpuState->edx = frame->orig_edx;  // Original EDX before syscall mangling
	cpuState->ecx = frame->cx;
	cpuState->eax = frame->orig_eax;  // Original EAX before syscall mangling

	// Exception information
	cpuState->vector = frame->vector;
	cpuState->error_code = frame->error_code;

	// Program counter and stack
	cpuState->eip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->eflags = frame->flags;
	cpuState->user_esp = frame->user_sp;
	cpuState->user_ss = frame->user_ss;
}


/*!	Updates x86 interrupt frame with modified register values
 *
 * Similar protection as x86_64: only allow user-settable EFLAGS bits.
 * Segment register updates are commented out to prevent security issues.
 */
static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
	// Segment registers - commented out for security
	// Modifying segment registers could allow ring 3 -> ring 0 escalation
	// frame->gs = cpuState->gs;
	// frame->fs = cpuState->fs;
	// frame->es = cpuState->es;
	// frame->ds = cpuState->ds;

	// General purpose registers
	frame->di = cpuState->edi;
	frame->si = cpuState->esi;
	frame->bp = cpuState->ebp;
	frame->bx = cpuState->ebx;
	frame->dx = cpuState->edx;
	frame->cx = cpuState->ecx;
	frame->ax = cpuState->eax;

	// Program counter
	frame->ip = cpuState->eip;

	// EFLAGS: preserve system bits, allow user-settable bits only
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
	| (cpuState->eflags & X86_EFLAGS_USER_SETTABLE_FLAGS);

	// User stack pointer
	frame->user_sp = cpuState->user_esp;

	// Protected fields (don't allow modification)
	// frame->cs = cpuState->cs;
	// frame->user_ss = cpuState->user_ss;
}


#endif	// __x86_64__


/*!	Captures complete CPU state including FPU/SSE registers
 *
 * FPU state handling differs by architecture:
 * - x86_64: Uses XSAVE/XSAVEC if available, or FXSAVE
 * - x86: Uses FXSAVE if SSE available, or FNSAVE (legacy)
 *
 * IMPORTANT: Calling function must not use FP/SSE registers, even indirectly,
 * to ensure accurate FPU state capture.
 *
 * Reference: Intel SDM Vol. 1, Chapter 13 (System Programming for Instruction Set Extensions)
 */
static void
get_cpu_state(Thread* thread, iframe* frame, debug_cpu_state* cpuState)
{
	#ifdef __x86_64__
	// x86_64: Always has SSE, potentially has XSAVE
	memset(&cpuState->extended_registers, 0, sizeof(cpuState->extended_registers));

	if (frame->fpu != nullptr) {
		if (gHasXsave) {
			// XSAVE format: includes XSAVE header and extended state components
			// TODO: Parse XSAVE header to determine actual saved state size
			// Currently assumes YMM (AVX) registers are included
			memcpy(&cpuState->extended_registers, frame->fpu,
				   sizeof(cpuState->extended_registers));
		} else {
			// FXSAVE format: legacy 512-byte FPU/SSE state
			memcpy(&cpuState->extended_registers, frame->fpu,
				   sizeof(cpuState->extended_registers.fp_fxsave));
		}
	}
	#else
	// x86: Check for SSE support
	Thread* thisThread = thread_get_current_thread();
	if (gHasSSE) {
		if (thread == thisThread) {
			// FXSAVE requires 16-byte alignment (Intel SDM Vol. 1, Section 13.5.1)
			// Use thread's fpu_state buffer which is guaranteed aligned
			// Must disable interrupts to prevent FPU state corruption
			InterruptsLocker locker;
			x86_fxsave(thread->arch_info.fpu_state);
			// Unlike FNSAVE, FXSAVE doesn't reset FPU state
		}
		memcpy(&cpuState->extended_registers, thread->arch_info.fpu_state,
			   sizeof(cpuState->extended_registers));
	} else {
		// Legacy x87 FPU without SSE
		if (thread == thisThread) {
			x86_fnsave(&cpuState->extended_registers);
			// FNSAVE resets FPU state, must restore it
			x86_frstor(&cpuState->extended_registers);
		} else {
			memcpy(&cpuState->extended_registers, thread->arch_info.fpu_state,
				   sizeof(cpuState->extended_registers));
		}
		// TODO: Convert FNSAVE format to FXSAVE format for consistency
	}
	#endif

	// Get general purpose registers
	get_iframe_registers(frame, cpuState);
}


/*!	Installs hardware breakpoints/watchpoints into debug registers
 *
 * x86 debug registers (DR0-DR7) control up to 4 hardware breakpoints.
 * Reference: Intel SDM Vol. 3B, Section 17.2 (Debug Registers)
 *
 * DR0-DR3: Breakpoint linear addresses
 * DR6: Debug status (which breakpoint triggered)
 * DR7: Debug control (enable breakpoints, set conditions)
 *
 * \param teamInfo Team debug info containing breakpoint configuration
 */
static inline void
install_breakpoints(const arch_team_debug_info& teamInfo)
{
	// Load breakpoint addresses into DR0-DR3
	asm("mov %0, %%dr0" : : "r"(teamInfo.breakpoints[0].address));
	asm("mov %0, %%dr1" : : "r"(teamInfo.breakpoints[1].address));
	asm("mov %0, %%dr2" : : "r"(teamInfo.breakpoints[2].address));
	asm("mov %0, %%dr3" : : "r"(teamInfo.breakpoints[3].address));

	// Enable breakpoints via DR7
	// DR7 format: LE/GE, GD, RTM, reserved, G0-G3, L0-L3, LEN/RW fields
	asm("mov %0, %%dr7" : : "r"(teamInfo.dr7));
}


/*!	Disables all hardware breakpoints
 *
 * Sets DR7 to disabled state while preserving reserved bits.
 * This prevents breakpoint exceptions until breakpoints are reconfigured.
 */
static inline void
disable_breakpoints()
{
	asm("mov %0, %%dr7" : : "r"((size_t)X86_BREAKPOINTS_DISABLED_DR7));
}


/*!	Sets hardware breakpoint in team debug info
 *
 * Finds free DR0-DR3 slot and configures DR7 enable bits and condition fields.
 *
 * DR7 condition encoding:
 * - RW field: 00=execution, 01=write, 11=read/write, 10=I/O (not used)
 * - LEN field: 00=1byte, 01=2bytes, 10=8bytes (x64), 11=4bytes
 *
 * \param info Team debug info to modify
 * \param address Breakpoint/watchpoint address
 * \param type X86_INSTRUCTION_BREAKPOINT, X86_DATA_WRITE_BREAKPOINT, etc.
 * \param length X86_BREAKPOINT_LENGTH_1/2/4/8
 * \param setGlobalFlag true to set G bit (global breakpoint), false for L bit (local)
 * \return B_OK, B_NO_MORE_BREAKPOINTS, or B_NO_MORE_WATCHPOINTS
 */
static inline status_t
set_breakpoint(arch_team_debug_info& info, void* address, size_t type,
			   size_t length, bool setGlobalFlag)
{
	// Check if breakpoint already exists at this address with same type
	bool alreadySet = false;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (info.breakpoints[i].address == address
			&& info.breakpoints[i].type == type) {
			alreadySet = true;
		break;
			}
	}

	if (!alreadySet) {
		// Find free debug register slot (DR0-DR3)
		int32 slot = -1;
		for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
			if (!info.breakpoints[i].address) {
				slot = i;
				break;
			}
		}

		if (slot >= 0) {
			// Configure breakpoint in slot
			info.breakpoints[slot].address = address;
			info.breakpoints[slot].type = type;
			info.breakpoints[slot].length = length;

			// Build DR7 value: LEN field (2 bits) | RW field (2 bits) | L/G bit (1 bit)
			info.dr7 |= (length << sDR7Len[slot])
			| (type << sDR7RW[slot])
			| (1 << sDR7L[slot]);

			if (setGlobalFlag)
				info.dr7 |= (1 << sDR7G[slot]);
		} else {
			// No free slots available
			if (type == X86_INSTRUCTION_BREAKPOINT)
				return B_NO_MORE_BREAKPOINTS;
			else
				return B_NO_MORE_WATCHPOINTS;
		}
	}

	return B_OK;
}


/*!	Clears hardware breakpoint from team debug info
 *
 * Removes breakpoint configuration and clears corresponding DR7 enable bits.
 *
 * \param info Team debug info to modify
 * \param address Breakpoint address to remove
 * \param watchpoint true if clearing watchpoint, false if clearing breakpoint
 * \return B_OK, B_BREAKPOINT_NOT_FOUND, or B_WATCHPOINT_NOT_FOUND
 */
static inline status_t
clear_breakpoint(arch_team_debug_info& info, void* address, bool watchpoint)
{
	// Find the breakpoint slot
	int32 slot = -1;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (info.breakpoints[i].address == address
			&& (watchpoint
			!= (info.breakpoints[i].type == X86_INSTRUCTION_BREAKPOINT))) {
			slot = i;
		break;
			}
	}

	if (slot >= 0) {
		// Clear breakpoint configuration
		info.breakpoints[slot].address = NULL;

		// Clear DR7 fields for this slot
		info.dr7 &= ~((0x3 << sDR7Len[slot])    // Clear LEN field (2 bits)
		| (0x3 << sDR7RW[slot])             // Clear RW field (2 bits)
		| (1 << sDR7L[slot])                // Clear L bit
		| (1 << sDR7G[slot]));              // Clear G bit
	} else {
		return watchpoint ? B_WATCHPOINT_NOT_FOUND : B_BREAKPOINT_NOT_FOUND;
	}

	return B_OK;
}


/*!	User-facing API to set breakpoint (wrapper with locking)
 *
 * Acquires team debug info lock before modifying breakpoint state.
 * Interrupts are disabled to prevent race conditions with breakpoint exceptions.
 */
static status_t
set_breakpoint(void* address, size_t type, size_t length)
{
	if (!address)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	status_t error = set_breakpoint(thread->team->debug_info.arch_info, address,
									type, length, false);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);

	return error;
}


/*!	User-facing API to clear breakpoint (wrapper with locking) */
static status_t
clear_breakpoint(void* address, bool watchpoint)
{
	if (!address)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	status_t error = clear_breakpoint(thread->team->debug_info.arch_info,
									  address, watchpoint);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);

	return error;
}


// #pragma mark - Kernel Breakpoint Support


#if KERNEL_BREAKPOINTS

/*!	Installs kernel breakpoints on all CPUs
 *
 * Called via SMP broadcast to ensure all CPUs have consistent breakpoint state.
 * This is necessary because debug registers are per-CPU.
 */
static void
install_breakpoints_per_cpu(void* /*cookie*/, int cpu)
{
	Team* kernelTeam = team_get_kernel_team();

	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
	install_breakpoints(kernelTeam->debug_info.arch_info);
	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
}


/*!	Sets kernel-wide breakpoint visible on all CPUs */
static status_t
set_kernel_breakpoint(void* address, size_t type, size_t length)
{
	if (!address)
		return B_BAD_VALUE;

	Team* kernelTeam = team_get_kernel_team();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	status_t error = set_breakpoint(kernelTeam->debug_info.arch_info, address,
									type, length, true);  // Global breakpoint (G bit set)

									RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

									// Update all CPUs via IPI
									call_all_cpus(install_breakpoints_per_cpu, NULL);

									restore_interrupts(state);

									return error;
}


/*!	Clears kernel-wide breakpoint from all CPUs */
static status_t
clear_kernel_breakpoint(void* address, bool watchpoint)
{
	if (!address)
		return B_BAD_VALUE;

	Team* kernelTeam = team_get_kernel_team();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	status_t error = clear_breakpoint(kernelTeam->debug_info.arch_info,
									  address, watchpoint);

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	// Update all CPUs via IPI
	call_all_cpus(install_breakpoints_per_cpu, NULL);

	restore_interrupts(state);

	return error;
}

#endif	// KERNEL_BREAKPOINTS


// #pragma mark - Demangled Symbol Printing


#ifndef __x86_64__

/*!	Sets debug variable for function call argument (x86 only)
 *
 * Variables are named _arg1, _arg2, etc. and can be referenced in other
 * debugger commands.
 */
static void
set_debug_argument_variable(int32 index, uint64 value)
{
	char name[8];
	snprintf(name, sizeof(name), "_arg%d", index);
	set_debug_variable(name, value);
}


/*!	Reads function argument value from stack with error checking
 *
 * \param argument Pointer to stack location containing argument
 * \param _valueKnown Output: set to true if read succeeded
 * \return Argument value (0 if read failed)
 */
template<typename Type>
static Type
read_function_argument_value(void* argument, bool& _valueKnown)
{
	Type value;
	if (debug_memcpy(B_CURRENT_TEAM, &value, argument, sizeof(Type)) == B_OK) {
		_valueKnown = true;
		return value;
	}

	_valueKnown = false;
	return 0;
}


/*!	Prints function call with demangled C++ symbol and decoded arguments
 *
 * Uses RTTI information to decode C++ name mangling and extract argument types.
 * Attempts to display argument values from stack memory.
 *
 * \param image Image name containing symbol
 * \param symbol Mangled symbol name
 * \param args Stack pointer to function arguments
 * \param noObjectMethod true to skip 'this' pointer handling
 * \param addDebugVariables true to populate _arg1, _arg2, etc. variables
 * \return B_OK if successfully printed, B_ERROR if demangling failed
 */
static status_t
print_demangled_call(const char* image, const char* symbol, addr_t args,
					 bool noObjectMethod, bool addDebugVariables)
{
	static const size_t kBufferSize = 256;
	char* buffer = (char*)debug_malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	bool isObjectMethod;
	const char* name = debug_demangle_symbol(symbol, buffer, kBufferSize,
											 &isObjectMethod);
	if (name == NULL) {
		debug_free(buffer);
		return B_ERROR;
	}

	uint32* arg = (uint32*)args;

	// Handle C++ object method calls with 'this' pointer
	if (noObjectMethod)
		isObjectMethod = false;

	if (isObjectMethod) {
		// Find namespace/class separator "::"
		const char* lastName = strrchr(name, ':') - 1;
		int namespaceLength = lastName - name;

		// First argument is 'this' pointer
		uint32 thisPointer = 0;
		if (debug_memcpy(B_CURRENT_TEAM, &thisPointer, arg, 4) == B_OK) {
			// Print: <image> ClassName<0xthisptr>::methodName
			kprintf("<%s> %.*s<\33[32m%#" B_PRIx32 "\33[0m>%s", image,
					namespaceLength, name, thisPointer, lastName);
		} else {
			kprintf("<%s> %.*s<\?\?\?>%s", image, namespaceLength, name, lastName);
		}

		if (addDebugVariables)
			set_debug_variable("_this", thisPointer);
		arg++;
	} else {
		kprintf("<%s> %s", image, name);
	}

	kprintf("(");

	// Iterate through function arguments using RTTI information
	size_t length;
	int32 type;
	int32 argIndex = 0;
	uint32 cookie = 0;

	while (debug_get_next_demangled_argument(&cookie, symbol, buffer,
		kBufferSize, &type, &length) == B_OK) {
		if (argIndex++ > 0)
			kprintf(", ");

		uint64 value;
	bool valueKnown = false;

	// Decode and print argument based on type information
	switch (type) {
		case B_INT64_TYPE:
			value = read_function_argument_value<int64>(arg, valueKnown);
			if (valueKnown)
				kprintf("int64: \33[34m%lld\33[0m", value);
		break;

		case B_INT32_TYPE:
			value = read_function_argument_value<int32>(arg, valueKnown);
			if (valueKnown)
				kprintf("int32: \33[34m%d\33[0m", (int32)value);
		break;

		case B_INT16_TYPE:
			value = read_function_argument_value<int16>(arg, valueKnown);
			if (valueKnown)
				kprintf("int16: \33[34m%d\33[0m", (int16)value);
		break;

		case B_INT8_TYPE:
			value = read_function_argument_value<int8>(arg, valueKnown);
			if (valueKnown)
				kprintf("int8: \33[34m%d\33[0m", (int8)value);
		break;

		case B_UINT64_TYPE:
			value = read_function_argument_value<uint64>(arg, valueKnown);
			if (valueKnown) {
				kprintf("uint64: \33[34m%#Lx\33[0m", value);
				if (value < 0x100000)
					kprintf(" (\33[34m%Lu\33[0m)", value);
			}
			break;

		case B_UINT32_TYPE:
			value = read_function_argument_value<uint32>(arg, valueKnown);
			if (valueKnown) {
				kprintf("uint32: \33[34m%#x\33[0m", (uint32)value);
				if (value < 0x100000)
					kprintf(" (\33[34m%u\33[0m)", (uint32)value);
			}
			break;

		case B_UINT16_TYPE:
			value = read_function_argument_value<uint16>(arg, valueKnown);
			if (valueKnown) {
				kprintf("uint16: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
						(uint16)value, (uint16)value);
			}
			break;

		case B_UINT8_TYPE:
			value = read_function_argument_value<uint8>(arg, valueKnown);
			if (valueKnown) {
				kprintf("uint8: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
						(uint8)value, (uint8)value);
			}
			break;

		case B_BOOL_TYPE:
			value = read_function_argument_value<uint8>(arg, valueKnown);
			if (valueKnown)
				kprintf("\33[34m%s\33[0m", value ? "true" : "false");
		break;

		default:
			// Unknown or complex type
			if (buffer[0])
				kprintf("%s: ", buffer);

		if (length == 4) {
			value = read_function_argument_value<uint32>(arg, valueKnown);
			if (valueKnown) {
				// NULL pointer special case
				if (value == 0 && (type == B_POINTER_TYPE || type == B_REF_TYPE))
					kprintf("NULL");
				else
					kprintf("\33[34m%#x\33[0m", (uint32)value);
			}
			break;
		}

		if (length == 8) {
			value = read_function_argument_value<uint64>(arg, valueKnown);
		} else {
			// Non-standard size, just show address
			value = (uint64)arg;
		}

		if (valueKnown)
			kprintf("\33[34m%#Lx\33[0m", value);
		break;
	}

	if (!valueKnown)
		kprintf("???");

		// Special handling for string arguments
		if (valueKnown && type == B_STRING_TYPE) {
			if (value == 0) {
				kprintf(" \33[31m\"<NULL>\"\33[0m");
			} else if (debug_strlcpy(B_CURRENT_TEAM, buffer, (char*)(addr_t)value,
				kBufferSize) < B_OK) {
				kprintf(" \33[31m\"<\?\?\?>\"\33[0m");
				} else {
					kprintf(" \33[36m\"%s\"\33[0m", buffer);
				}
		}

		if (addDebugVariables)
			set_debug_argument_variable(argIndex, value);

		// Advance to next argument on stack
		arg = (uint32*)((uint8*)arg + length);
		}

		debug_free(buffer);

		kprintf(")");
		return B_OK;
}

#else	// __x86_64__

/*!	Prints demangled function call without argument values (x86_64)
 *
 * x86_64 uses register calling convention for first 6 arguments (RDI, RSI,
 * RDX, RCX, R8, R9), so we can't reliably read them from stack.
 *
 * Full argument decoding would require DWARF debug info parsing.
 *
 * Reference: System V ABI x86-64, Section 3.2.3 (Parameter Passing)
 */
static status_t
print_demangled_call(const char* image, const char* symbol, addr_t args,
					 bool noObjectMethod, bool addDebugVariables)
{
	static const size_t kBufferSize = 256;
	char* buffer = (char*)debug_malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	bool isObjectMethod;
	const char* name = debug_demangle_symbol(symbol, buffer, kBufferSize,
											 &isObjectMethod);
	if (name == NULL) {
		debug_free(buffer);
		return B_ERROR;
	}

	// Print function signature without argument values
	kprintf("<%s> %s(", image, name);

	// Show argument types from RTTI
	size_t length;
	int32 type;
	int32 argIndex = 0;
	uint32 cookie = 0;

	while (debug_get_next_demangled_argument(&cookie, symbol, buffer,
		kBufferSize, &type, &length) == B_OK) {
		if (argIndex++ > 0)
			kprintf(", ");

		if (buffer[0])
			kprintf("%s", buffer);
		else
			kprintf("???");
		}

		debug_free(buffer);

		kprintf(")");
		return B_OK;
}

#endif	// __x86_64__


// #pragma mark - Stack Frame Printing


/*!	Prints single stack frame with symbol information
 *
 * Output format:
 * frame_num  frame_ptr (+delta)  return_addr   <image> symbol+offset
 *
 * \param thread Thread being traced
 * \param ip Instruction pointer (return address)
 * \param calleeBp Frame pointer of called function (for delta calculation)
 * \param bp Frame pointer of calling function
 * \param callIndex Call depth (0 = current frame)
 * \param demangle Whether to attempt C++ demangling
 */
static void
print_stack_frame(Thread* thread, addr_t ip, addr_t calleeBp, addr_t bp,
				  int32 callIndex, bool demangle)
{
	const char* symbol;
	const char* image;
	addr_t baseAddress;
	bool exactMatch;
	addr_t frameDelta;

	// Calculate frame size (stack growth direction: high to low addresses)
	frameDelta = bp - calleeBp;
	if (calleeBp > bp)
		frameDelta = 0;  // Kernel/user space transition

		status_t status = lookup_symbol(thread, ip, &baseAddress, &symbol, &image,
										&exactMatch);

		// Print frame header: index, frame pointer, delta, return address
		kprintf("%2" B_PRId32 " %0*lx (+%4ld) %0*lx   ", callIndex,
				B_PRINTF_POINTER_WIDTH, bp, frameDelta, B_PRINTF_POINTER_WIDTH, ip);

		if (status == B_OK) {
			// Try demangling if requested and symbol matches exactly
			if (exactMatch && demangle) {
				status = print_demangled_call(image, symbol,
											  bp + sizeof(stack_frame), false, false);
			}

			// Fallback: print raw symbol name
			if (!exactMatch || !demangle || status != B_OK) {
				if (symbol != NULL) {
					kprintf("<%s> %s%s", image, symbol,
							exactMatch ? "" : " (nearest)");
				} else {
					kprintf("<%s@%p> <unknown>", image, (void*)baseAddress);
				}
			}

			// Print offset from symbol base
			kprintf(" + %#04lx\n", ip - baseAddress);
		} else {
			// Symbol lookup failed, try to identify memory area
			VMArea* area = NULL;
			if (thread != NULL && thread->team != NULL
				&& thread->team->address_space != NULL) {
				area = thread->team->address_space->LookupArea(ip);
				}

				if (area != NULL) {
					kprintf("%" B_PRId32 ":%s@%p + %#lx\n", area->id, area->name,
							(void*)area->Base(), ip - area->Base());
				} else {
					kprintf("\n");
				}
		}
}


/*!	Prints interrupt frame contents
 *
 * Displays all saved registers at time of interrupt/exception.
 * Useful for debugging crashes and understanding interrupt context.
 *
 * Reference: Intel SDM Vol. 3A, Section 6.14 (Exception and Interrupt Handling)
 */
static void
print_iframe(iframe* frame)
{
	bool isUser = IFRAME_IS_USER(frame);

	#ifdef __x86_64__
	kprintf("%s iframe at %p (end = %p)\n", isUser ? "user" : "kernel", frame,
			frame + 1);

	// Print general purpose registers (3 columns for readability)
	kprintf(" rax %#-18lx    rbx %#-18lx    rcx %#lx\n", frame->ax,
			frame->bx, frame->cx);
	kprintf(" rdx %#-18lx    rsi %#-18lx    rdi %#lx\n", frame->dx,
			frame->si, frame->di);
	kprintf(" rbp %#-18lx     r8 %#-18lx     r9 %#lx\n", frame->bp,
			frame->r8, frame->r9);
	kprintf(" r10 %#-18lx    r11 %#-18lx    r12 %#lx\n", frame->r10,
			frame->r11, frame->r12);
	kprintf(" r13 %#-18lx    r14 %#-18lx    r15 %#lx\n", frame->r13,
			frame->r14, frame->r15);

	// Print control registers
	kprintf(" rip %#-18lx    rsp %#-18lx rflags %#lx\n", frame->ip,
			frame->sp, frame->flags);
	#else
	// 32-bit iframe is smaller for kernel (no user_sp)
	void* frameEnd = isUser ? (void*)(frame + 1) : (void*)&frame->user_sp;
	kprintf("%s iframe at %p (end = %p)\n", isUser ? "user" : "kernel",
			frame, frameEnd);

	// Print general purpose registers
	kprintf(" eax %#-10x    ebx %#-10x     ecx %#-10x  edx %#x\n",
			frame->ax, frame->bx, frame->cx, frame->dx);
	kprintf(" esi %#-10x    edi %#-10x     ebp %#-10x  esp %#x\n",
			frame->si, frame->di, frame->bp, frame->sp);
	kprintf(" eip %#-10x eflags %#-10x", frame->ip, frame->flags);

	if (isUser) {
		// User mode: show user stack pointer
		kprintf("user esp %#x", frame->user_sp);
	}
	kprintf("\n");
	#endif

	// Exception information
	kprintf(" vector: %#lx, error code: %#lx\n",
			(long unsigned int)frame->vector,
			(long unsigned int)frame->error_code);
}


// #pragma mark - Thread State Helpers


/*!	Sets up thread context and page directory for stack tracing
 *
 * Switches to thread's page directory if necessary to access userland memory.
 * Retrieves current frame pointer from running thread or saved context.
 *
 * \param arg Thread ID string (NULL = current thread)
 * \param _thread Output: thread structure
 * \param _bp Output: frame pointer to start unwinding from
 * \param _oldPageDirectory Output: previous CR3 value (0 if unchanged)
 * \return true on success, false if thread not found or CPU unavailable
 */
static bool
setup_for_thread(char* arg, Thread** _thread, addr_t* _bp,
				 phys_addr_t* _oldPageDirectory)
{
	Thread* thread = NULL;

	if (arg != NULL) {
		thread_id id = strtoul(arg, NULL, 0);
		thread = Thread::GetDebug(id);
		if (thread == NULL) {
			kprintf("could not find thread %" B_PRId32 "\n", id);
			return false;
		}

		if (id != thread_get_current_thread_id()) {
			// Switch page directory to access thread's userland memory
			// Reference: Intel SDM Vol. 3A, Section 4.3 (Paging)
			phys_addr_t newPageDirectory = x86_next_page_directory(
				thread_get_current_thread(), thread);

			if (newPageDirectory != 0) {
				*_oldPageDirectory = x86_read_cr3();
				x86_write_cr3(newPageDirectory);
			}

			// Get frame pointer from thread state
			if (thread->state == B_THREAD_RUNNING) {
				// Thread running on another CPU: use saved debug registers
				if (thread->cpu == NULL)
					return false;
				arch_debug_registers* registers = debug_get_debug_registers(
					thread->cpu->cpu_num);
				if (registers == NULL)
					return false;
				*_bp = registers->bp;
			} else {
				// Thread not running: read from saved context
				*_bp = thread->arch_info.GetFramePointer();
			}
		} else {
			thread = NULL;  // Use current thread instead
		}
	}

	// Default to current thread if no thread specified or same as current
	if (thread == NULL) {
		thread = thread_get_current_thread();
		// Caller has already set bp to current frame pointer
	}

	*_thread = thread;
	return true;
}


/*!	Checks if address is within double fault stack
 *
 * Each CPU has dedicated double fault stack to handle stack overflow faults.
 * Reference: Intel SDM Vol. 3A, Section 6.14.5 (Double Fault Exception)
 */
static bool
is_double_fault_stack_address(int32 cpu, addr_t address)
{
	size_t size;
	addr_t bottom = (addr_t)x86_get_double_fault_stack(cpu, &size);
	return address >= bottom && address < bottom + size;
}


/*!	Checks if address is within kernel stack
 *
 * During early boot, thread structure may not be initialized yet.
 * Any kernel address is considered valid in that case.
 */
static bool
is_kernel_stack_address(Thread* thread, addr_t address)
{
	// Early boot: no thread structure yet
	if (thread == NULL)
		return IS_KERNEL_ADDRESS(address);

	// Early boot: thread exists but stack not configured
	if (thread->kernel_stack_top == 0)
		return IS_KERNEL_ADDRESS(address);

	// Check normal kernel stack bounds
	bool inNormalStack = (address >= thread->kernel_stack_base
	&& address < thread->kernel_stack_top);

	// Check double fault stack
	bool inDoubleFaultStack = (thread->cpu != NULL
	&& is_double_fault_stack_address(thread->cpu->cpu_num, address));

	return inNormalStack || inDoubleFaultStack;
}


/*!	Determines if frame pointer points to interrupt frame
 *
 * Interrupt frames are identified by bottom bits of saved frame pointer:
 * - Normal frame: points to previous stack_frame
 * - Interrupt frame: bottom bits encode iframe type
 *
 * Reference: See iframe type definitions (IFRAME_TYPE_SYSCALL, etc.)
 */
static bool
is_iframe(Thread* thread, addr_t frame)
{
	if (!is_kernel_stack_address(thread, frame))
		return false;

	addr_t previousFrame = *(addr_t*)frame;
	return ((previousFrame & ~(addr_t)IFRAME_TYPE_MASK) == 0
	&& previousFrame != 0);
}


/*!	Searches backward through stack for interrupt frame
 *
 * Walks frame pointer chain until finding iframe or reaching invalid memory.
 */
static iframe*
find_previous_iframe(Thread* thread, addr_t frame)
{
	while (is_kernel_stack_address(thread, frame)) {
		if (is_iframe(thread, frame))
			return (iframe*)frame;

		frame = *(addr_t*)frame;
	}

	return NULL;
}


/*!	Gets next older interrupt frame from current iframe */
static iframe*
get_previous_iframe(Thread* thread, iframe* frame)
{
	if (frame == NULL)
		return NULL;

	return find_previous_iframe(thread, frame->bp);
}


/*!	Gets most recent interrupt frame for current thread */
static iframe*
get_current_iframe(Thread* thread)
{
	if (thread == thread_get_current_thread())
		return x86_get_current_iframe();

	// NOTE: Cannot call if thread is running on another CPU
	return find_previous_iframe(thread, thread->arch_info.GetFramePointer());
}


// #pragma mark - Debug Variable Access


#define CHECK_DEBUG_VARIABLE(_name, _member, _settable) \
if (strcmp(variableName, _name) == 0) { \
	settable = _settable; \
	return (addr_t*)&_member; \
}


/*!	Finds debug variable in current interrupt frame
 *
 * Debug variables allow examining/modifying register state in debugger.
 * Examples: $rax, $rip, $rflags
 *
 * \param variableName Variable name (without $ prefix)
 * \param settable Output: whether variable can be modified
 * \return Pointer to register in iframe, or NULL if not found
 */
static addr_t*
find_debug_variable(const char* variableName, bool& settable)
{
	iframe* frame = get_current_iframe(debug_get_debugged_thread());
	if (frame == NULL)
		return NULL;

	#ifdef __x86_64__
	// x86_64 register variable names
	CHECK_DEBUG_VARIABLE("cs", frame->cs, false);
	CHECK_DEBUG_VARIABLE("ss", frame->ss, false);
	CHECK_DEBUG_VARIABLE("r15", frame->r15, true);
	CHECK_DEBUG_VARIABLE("r14", frame->r14, true);
	CHECK_DEBUG_VARIABLE("r13", frame->r13, true);
	CHECK_DEBUG_VARIABLE("r12", frame->r12, true);
	CHECK_DEBUG_VARIABLE("r11", frame->r11, true);
	CHECK_DEBUG_VARIABLE("r10", frame->r10, true);
	CHECK_DEBUG_VARIABLE("r9", frame->r9, true);
	CHECK_DEBUG_VARIABLE("r8", frame->r8, true);
	CHECK_DEBUG_VARIABLE("rbp", frame->bp, true);
	CHECK_DEBUG_VARIABLE("rsi", frame->si, true);
	CHECK_DEBUG_VARIABLE("rdi", frame->di, true);
	CHECK_DEBUG_VARIABLE("rdx", frame->dx, true);
	CHECK_DEBUG_VARIABLE("rcx", frame->cx, true);
	CHECK_DEBUG_VARIABLE("rbx", frame->bx, true);
	CHECK_DEBUG_VARIABLE("rax", frame->ax, true);
	CHECK_DEBUG_VARIABLE("rip", frame->ip, true);
	CHECK_DEBUG_VARIABLE("rflags", frame->flags, true);
	CHECK_DEBUG_VARIABLE("rsp", frame->sp, true);
	#else
	// x86 register variable names
	CHECK_DEBUG_VARIABLE("gs", frame->gs, false);
	CHECK_DEBUG_VARIABLE("fs", frame->fs, false);
	CHECK_DEBUG_VARIABLE("es", frame->es, false);
	CHECK_DEBUG_VARIABLE("ds", frame->ds, false);
	CHECK_DEBUG_VARIABLE("cs", frame->cs, false);
	CHECK_DEBUG_VARIABLE("edi", frame->di, true);
	CHECK_DEBUG_VARIABLE("esi", frame->si, true);
	CHECK_DEBUG_VARIABLE("ebp", frame->bp, true);
	CHECK_DEBUG_VARIABLE("esp", frame->sp, true);
	CHECK_DEBUG_VARIABLE("ebx", frame->bx, true);
	CHECK_DEBUG_VARIABLE("edx", frame->dx, true);
	CHECK_DEBUG_VARIABLE("ecx", frame->cx, true);
	CHECK_DEBUG_VARIABLE("eax", frame->ax, true);
	CHECK_DEBUG_VARIABLE("orig_eax", frame->orig_eax, true);
	CHECK_DEBUG_VARIABLE("orig_edx", frame->orig_edx, true);
	CHECK_DEBUG_VARIABLE("eip", frame->ip, true);
	CHECK_DEBUG_VARIABLE("eflags", frame->flags, true);

	if (IFRAME_IS_USER(frame)) {
		CHECK_DEBUG_VARIABLE("user_esp", frame->user_sp, true);
		CHECK_DEBUG_VARIABLE("user_ss", frame->user_ss, false);
	}
	#endif

	return NULL;
}


// #pragma mark - Kernel Debugger Commands


/*!	Prints stack backtrace for current or specified thread
 *
 * Command: sc [-d] [thread_id]
 * Aliases: where, bt
 *
 * \param -d Disable C++ demangling
 * \param thread_id Thread to trace (default: current)
 */
static int
stack_trace(int argc, char** argv)
{
	static const char* usage = "usage: %s [-d] [ <thread id> ]\n"
	"Prints a stack trace for the current, respectively the specified\n"
	"thread.\n"
	"  -d           -  Disables the demangling of the symbols.\n"
	"  <thread id>  -  The ID of the thread for which to print the stack\n"
	"                  trace.\n";

	bool demangle = true;
	int32 threadIndex = 1;

	if (argc > 1 && !strcmp(argv[1], "-d")) {
		demangle = false;
		threadIndex++;
	}

	if (argc > threadIndex + 1
		|| (argc == 2 && strcmp(argv[1], "--help") == 0)) {
		kprintf(usage, argv[0]);
	return 0;
		}

		// Loop detection buffer
		addr_t previousLocations[NUM_PREVIOUS_LOCATIONS];
		int32 numPrevious = 0;
		int32 lastIndex = 0;

		Thread* thread = NULL;
		phys_addr_t oldPageDirectory = 0;
		addr_t bp = x86_get_stack_frame();

		if (!setup_for_thread(argc == threadIndex + 1 ? argv[threadIndex] : NULL,
			&thread, &bp, &oldPageDirectory))
			return 0;

		DebuggedThreadSetter threadSetter(thread);

		// Print thread information
		if (thread != NULL) {
			kprintf("stack trace for thread %" B_PRId32 " \"%s\"\n", thread->id,
					thread->name);

			kprintf("    kernel stack: %p to %p\n",
					(void*)thread->kernel_stack_base,
					(void*)(thread->kernel_stack_top));
			if (thread->user_stack_base != 0) {
				kprintf("      user stack: %p to %p\n",
						(void*)thread->user_stack_base,
						(void*)(thread->user_stack_base + thread->user_stack_size));
			}
		}

		// Print header
		kprintf("%-*s            %-*s   <image>:function + offset\n",
				B_PRINTF_POINTER_WIDTH, "frame", B_PRINTF_POINTER_WIDTH, "caller");

		bool onKernelStack = true;

		// Walk stack frames
		for (int32 callIndex = 0; ; callIndex++) {
			onKernelStack = onKernelStack
			&& is_kernel_stack_address(thread, bp);

			if (onKernelStack && is_iframe(thread, bp)) {
				// Interrupt frame
				iframe* frame = (iframe*)bp;

				print_iframe(frame);
				print_stack_frame(thread, frame->ip, bp, frame->bp, callIndex,
								  demangle);

				bp = frame->bp;
			} else {
				// Normal function frame
				addr_t ip, nextBp;

				if (get_next_frame_debugger(bp, &nextBp, &ip) != B_OK) {
					kprintf("%0*lx -- read fault\n", B_PRINTF_POINTER_WIDTH, bp);
					break;
				}

				if (ip == 0 || bp == 0)
					break;

				print_stack_frame(thread, ip, bp, nextBp, callIndex, demangle);
				bp = nextBp;
			}

			// Check for loops in stack
			if (already_visited(previousLocations, &lastIndex, &numPrevious, bp)) {
				kprintf("circular stack frame: %p!\n", (void*)bp);
				break;
			}

			if (bp == 0)
				break;
		}

		// Restore original page directory
		if (oldPageDirectory != 0) {
			x86_write_cr3(oldPageDirectory);
		}

		return 0;
}


#ifndef __x86_64__

/*!	Prints single function call with arguments (x86 only)
 *
 * Command: call [thread_id] <call_index> [-<arg_count>]
 *
 * \param thread_id Thread to examine (default: current)
 * \param call_index Stack frame index (from 'sc' output)
 * \param -c Force C++ demangling (assume method call)
 * \param -d Disable demangling
 * \param -N Show N arguments (default: auto-detect from RTTI)
 */
static int
show_call(int argc, char **argv)
{
	static const char* usage
	= "usage: %s [ <thread id> ] <call index> [ -<arg count> ]\n"
	"Prints a function call with parameters of the current, respectively\n"
	"the specified thread.\n"
	"  <thread id>   -  The ID of the thread for which to print the call.\n"
	"  <call index>  -  The index of the call in the stack trace.\n"
	"  <arg count>   -  The number of call arguments to print (use 'c' to\n"
	"                   force the C++ demangler to use class methods,\n"
	"                   use 'd' to disable demangling).\n";

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	Thread *thread = NULL;
	phys_addr_t oldPageDirectory = 0;
	addr_t ebp = x86_get_stack_frame();
	int32 argCount = 0;

	// Parse optional arg count flag
	if (argc >= 2 && argv[argc - 1][0] == '-') {
		if (argv[argc - 1][1] == 'c')
			argCount = -1;  // Force C++ method
			else if (argv[argc - 1][1] == 'd')
				argCount = -2;  // Disable demangling
				else
					argCount = strtoul(argv[argc - 1] + 1, NULL, 0);

		if (argCount < -2 || argCount > 16) {
			kprintf("Invalid argument count \"%d\".\n", argCount);
			return 0;
		}
		argc--;
	}

	if (argc < 2 || argc > 3) {
		kprintf(usage, argv[0]);
		return 0;
	}

	if (!setup_for_thread(argc == 3 ? argv[1] : NULL, &thread, &ebp,
		&oldPageDirectory))
		return 0;

	DebuggedThreadSetter threadSetter(thread);

	int32 callIndex = strtoul(argv[argc == 3 ? 2 : 1], NULL, 0);

	if (thread != NULL)
		kprintf("thread %d, %s\n", thread->id, thread->name);

	// Walk to requested frame
	bool onKernelStack = true;
	for (int32 index = 0; index <= callIndex; index++) {
		onKernelStack = onKernelStack
		&& is_kernel_stack_address(thread, ebp);

		if (onKernelStack && is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe *)ebp;

			if (index == callIndex)
				print_call(thread, frame->ip, ebp, frame->bp, argCount);

			ebp = frame->bp;
		} else {
			addr_t eip, nextEbp;

			if (get_next_frame_debugger(ebp, &nextEbp, &eip) != B_OK) {
				kprintf("%08lx -- read fault\n", ebp);
				break;
			}

			if (eip == 0 || ebp == 0)
				break;

			if (index == callIndex)
				print_call(thread, eip, ebp, nextEbp, argCount);

			ebp = nextEbp;
		}

		if (ebp == 0)
			break;
	}

	if (oldPageDirectory != 0) {
		x86_write_cr3(oldPageDirectory);
	}

	return 0;
}
#endif


/*!	Dumps all interrupt frames on thread's stack
 *
 * Command: iframe [thread_id]
 *
 * Shows complete iframe contents in chronological order (oldest first).
 * Useful for understanding nested interrupt/exception handling.
 */
static int
dump_iframes(int argc, char** argv)
{
	static const char* usage = "usage: %s [ <thread id> ]\n"
	"Prints the iframe stack for the current, respectively the specified\n"
	"thread.\n"
	"  <thread id>  -  The ID of the thread for which to print the iframe\n"
	"                  stack.\n";

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	Thread* thread = NULL;

	if (argc < 2) {
		thread = thread_get_current_thread();
	} else if (argc == 2) {
		thread_id id = strtoul(argv[1], NULL, 0);
		thread = Thread::GetDebug(id);
		if (thread == NULL) {
			kprintf("could not find thread %" B_PRId32 "\n", id);
			return 0;
		}
	} else {
		kprintf(usage, argv[0]);
		return 0;
	}

	if (thread != NULL) {
		kprintf("iframes for thread %" B_PRId32 " \"%s\"\n", thread->id,
				thread->name);
	}

	DebuggedThreadSetter threadSetter(thread);

	// Walk all iframes on stack
	iframe* frame = find_previous_iframe(thread, x86_get_stack_frame());
	while (frame != NULL) {
		print_iframe(frame);
		frame = get_previous_iframe(thread, frame);
	}

	return 0;
}


/*!	Checks if thread's call stack contains specified function
 *
 * Used for determining if specific code path was taken.
 *
 * \param thread Thread to examine
 * \param pattern Symbol name pattern to match (substring match)
 * \param start Start address range (for address-based matching)
 * \param end End address range
 * \return true if function found in call stack
 */
static bool
is_calling(Thread* thread, addr_t ip, const char* pattern, addr_t start,
		   addr_t end)
{
	if (pattern == NULL)
		return ip >= start && ip < end;

	if (!IS_KERNEL_ADDRESS(ip))
		return false;

	const char* symbol;
	if (lookup_symbol(thread, ip, NULL, &symbol, NULL, NULL) != B_OK)
		return false;

	return strstr(symbol, pattern);
}


/*!	Executes debugger command in context of specified thread
 *
 * Command: in_context <thread_id> <command...>
 *
 * Switches page directory to thread's address space before executing command.
 * This allows inspecting userland memory of other processes.
 */
static int
cmd_in_context(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	// Parse thread ID
	const char* commandLine = argv[1];
	char threadIDString[16];
	if (parse_next_debug_command_argument(&commandLine, threadIDString,
		sizeof(threadIDString)) != B_OK) {
		kprintf("Failed to parse thread ID.\n");
	return 0;
		}

		if (commandLine == NULL) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}

		uint64 threadID;
		if (!evaluate_debug_expression(threadIDString, &threadID, false))
			return 0;

	// Get thread
	Thread* thread = Thread::GetDebug(threadID);
	if (thread == NULL) {
		kprintf("Could not find thread with ID \"%s\".\n", threadIDString);
		return 0;
	}

	// Switch page directory if necessary
	phys_addr_t oldPageDirectory = 0;
	if (thread != thread_get_current_thread()) {
		phys_addr_t newPageDirectory = x86_next_page_directory(
			thread_get_current_thread(), thread);

		if (newPageDirectory != 0) {
			oldPageDirectory = x86_read_cr3();
			x86_write_cr3(newPageDirectory);
		}
	}

	// Execute command in thread's context
	{
		DebuggedThreadSetter threadSetter(thread);
		evaluate_debug_command(commandLine);
	}

	// Restore page directory
	if (oldPageDirectory)
		x86_write_cr3(oldPageDirectory);

	return 0;
}


// #pragma mark - Public Kernel API


void
arch_debug_save_registers(arch_debug_registers* registers)
{
	// Get caller's frame pointer (skip our own frame)
	stack_frame* frame = (stack_frame*)x86_get_stack_frame();
	registers->bp = (addr_t)frame->previous;
}


void
arch_debug_stack_trace(void)
{
	stack_trace(0, NULL);
}


bool
arch_debug_contains_call(Thread* thread, const char* symbol, addr_t start,
						 addr_t end)
{
	DebuggedThreadSetter threadSetter(thread);

	addr_t bp;
	if (thread == thread_get_current_thread()) {
		bp = x86_get_stack_frame();
	} else {
		if (thread->state == B_THREAD_RUNNING) {
			// Thread running on another CPU
			if (thread->cpu == NULL)
				return false;
			arch_debug_registers* registers = debug_get_debug_registers(
				thread->cpu->cpu_num);
			if (registers == NULL)
				return false;
			bp = registers->bp;
		} else {
			// Thread not running
			bp = thread->arch_info.GetFramePointer();
		}
	}

	// Walk stack checking each return address
	for (;;) {
		if (!is_kernel_stack_address(thread, bp))
			break;

		if (is_iframe(thread, bp)) {
			iframe* frame = (iframe*)bp;

			if (is_calling(thread, frame->ip, symbol, start, end))
				return true;

			bp = frame->bp;
		} else {
			addr_t ip, nextBp;

			if (get_next_frame_no_debugger(bp, &nextBp, &ip, true,
				thread) != B_OK
				|| ip == 0 || bp == 0)
				break;

			if (is_calling(thread, ip, symbol, start, end))
				return true;

			bp = nextBp;
		}

		if (bp == 0)
			break;
	}

	return false;
}


/*!	Captures stack trace of current thread
 *
 * Captures return addresses (not frame pointers) for later analysis.
 * Used by profiling and crash reporting.
 *
 * \param returnAddresses Output array for return addresses
 * \param maxCount Maximum addresses to capture
 * \param skipIframes Number of interrupt frames to skip before capturing
 * \param skipFrames Number of normal frames to skip (ignored if skipIframes > 0)
 * \param flags STACK_TRACE_KERNEL and/or STACK_TRACE_USER
 * \return Number of addresses captured
 */
int32
arch_debug_get_stack_trace(addr_t* returnAddresses, int32 maxCount,
						   int32 skipIframes, int32 skipFrames, uint32 flags)
{
	// Keep skipping frames until we've skipped requested iframes
	if (skipIframes > 0)
		skipFrames = INT_MAX;

	Thread* thread = thread_get_current_thread();
	int32 count = 0;
	addr_t bp = x86_get_stack_frame();
	bool onKernelStack = true;

	// If only want user stack, jump to user iframe
	if ((flags & (STACK_TRACE_KERNEL | STACK_TRACE_USER)) == STACK_TRACE_USER) {
		iframe* frame = x86_get_user_iframe();
		if (frame == NULL)
			return 0;

		bp = (addr_t)frame;
	}

	while (bp != 0 && count < maxCount) {
		onKernelStack = onKernelStack
		&& is_kernel_stack_address(thread, bp);

		// Stop if left kernel stack and not tracing user
		if (!onKernelStack && (flags & STACK_TRACE_USER) == 0)
			break;

		addr_t ip, nextBp;

		if (onKernelStack && is_iframe(thread, bp)) {
			iframe* frame = (iframe*)bp;
			ip = frame->ip;
			nextBp = frame->bp;

			// Count iframe toward skip count
			if (skipIframes > 0) {
				if (--skipIframes == 0)
					skipFrames = 0;
			}
		} else {
			if (get_next_frame_no_debugger(bp, &nextBp, &ip,
				onKernelStack, thread) != B_OK) {
				break;
				}
		}

		if (ip == 0)
			break;

		// Skip requested number of frames
		if (skipFrames > 0) {
			skipFrames--;
		} else {
			returnAddresses[count++] = ip;
		}

		bp = nextBp;
	}

	return count;
}


/*!	Gets program counter of innermost interrupt frame
 *
 * Used to identify where exception/interrupt occurred.
 *
 * \param _isSyscall Output: true if frame is from syscall (optional)
 * \return Instruction pointer, or NULL if no iframe available
 */
void*
arch_debug_get_interrupt_pc(bool* _isSyscall)
{
	iframe* frame = get_current_iframe(debug_get_debugged_thread());
	if (frame == NULL)
		return NULL;

	if (_isSyscall != NULL)
		*_isSyscall = frame->type == IFRAME_TYPE_SYSCALL;

	return (void*)(addr_t)frame->ip;
}


/*!	Clears current thread pointer in kernel debugger
 *
 * Called when entering kernel debugger to prevent thread_get_current_thread()
 * from accessing potentially corrupted memory.
 *
 * Implementation note: Sets GS base to point at NULL pointer rather than
 * clearing GS, since %gs:0 would fault if GS base is 0.
 */
void
arch_debug_unset_current_thread(void)
{
	static Thread* unsetThread = NULL;
	#ifdef __x86_64__
	// On x86_64, use MSR to set GS base
	x86_write_msr(IA32_MSR_GS_BASE, (addr_t)&unsetThread);
	#else
	// On x86, write to GS segment memory directly
	asm volatile("mov %0, %%gs:0" : : "r" (unsetThread) : "memory");
	#endif
}


bool
arch_is_debug_variable_defined(const char* variableName)
{
	bool settable;
	return find_debug_variable(variableName, settable);
}


status_t
arch_set_debug_variable(const char* variableName, uint64 value)
{
	bool settable;
	size_t* variable = find_debug_variable(variableName, settable);
	if (variable == NULL)
		return B_ENTRY_NOT_FOUND;

	if (!settable)
		return B_NOT_ALLOWED;

	*variable = (size_t)value;
	return B_OK;
}


status_t
arch_get_debug_variable(const char* variableName, uint64* value)
{
	bool settable;
	size_t* variable = find_debug_variable(variableName, settable);
	if (variable == NULL)
		return B_ENTRY_NOT_FOUND;

	*value = *variable;
	return B_OK;
}


// #pragma mark - GDB Remote Protocol Support


struct gdb_register {
	int32 type;
	uint64 value;
};


/*!	Formats CPU registers for GDB remote protocol
 *
 * GDB expects registers in specific order with specific sizes.
 * Unfortunately, GDB wants mix of 64-bit and 32-bit values even on x86_64.
 *
 * Reference: GDB Remote Protocol, Appendix E (Register Packet Format)
 *
 * \param buffer Output buffer for formatted register dump
 * \param bufferSize Size of output buffer
 * \return Number of bytes written, or negative error code
 */
ssize_t
arch_debug_gdb_get_registers(char* buffer, size_t bufferSize)
{
	iframe* frame = get_current_iframe(debug_get_debugged_thread());
	if (frame == NULL)
		return B_NOT_SUPPORTED;

	#ifdef __x86_64__
	// x86_64 register order:
	// rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp,
	// r8, r9, r10, r11, r12, r13, r14, r15,
	// rip, rflags, cs, ss, ds, es, fs, gs
	//
	// NOTE: GDB wants 64-bit values for GPRs but 32-bit for RFLAGS and segments
	static const int32 kRegisterCount = 24;
	gdb_register registers[kRegisterCount] = {
		{ B_UINT64_TYPE, frame->ax },  { B_UINT64_TYPE, frame->bx },
		{ B_UINT64_TYPE, frame->cx },  { B_UINT64_TYPE, frame->dx },
		{ B_UINT64_TYPE, frame->si },  { B_UINT64_TYPE, frame->di },
		{ B_UINT64_TYPE, frame->bp },  { B_UINT64_TYPE, frame->sp },
		{ B_UINT64_TYPE, frame->r8 },  { B_UINT64_TYPE, frame->r9 },
		{ B_UINT64_TYPE, frame->r10 }, { B_UINT64_TYPE, frame->r11 },
		{ B_UINT64_TYPE, frame->r12 }, { B_UINT64_TYPE, frame->r13 },
		{ B_UINT64_TYPE, frame->r14 }, { B_UINT64_TYPE, frame->r15 },
		{ B_UINT64_TYPE, frame->ip },  { B_UINT32_TYPE, frame->flags },
		{ B_UINT32_TYPE, frame->cs },  { B_UINT32_TYPE, frame->ss },
		{ B_UINT32_TYPE, 0 },          { B_UINT32_TYPE, 0 },
		{ B_UINT32_TYPE, 0 },          { B_UINT32_TYPE, 0 },
	};
	#else
	// x86 register order:
	// eax, ecx, edx, ebx,
	// esp, ebp, esi, edi,
	// eip, eflags,
	// cs, ss, ds, es, fs, gs
	//
	// NOTE: Segment registers are 16-bit but GDB expects 32-bit integers
	static const int32 kRegisterCount = 16;
	gdb_register registers[kRegisterCount] = {
		{ B_UINT32_TYPE, frame->ax }, { B_UINT32_TYPE, frame->cx },
		{ B_UINT32_TYPE, frame->dx }, { B_UINT32_TYPE, frame->bx },
		{ B_UINT32_TYPE, frame->sp }, { B_UINT32_TYPE, frame->bp },
		{ B_UINT32_TYPE, frame->si }, { B_UINT32_TYPE, frame->di },
		{ B_UINT32_TYPE, frame->ip }, { B_UINT32_TYPE, frame->flags },
		{ B_UINT32_TYPE, frame->cs }, { B_UINT32_TYPE, frame->ds },
		{ B_UINT32_TYPE, frame->ds }, { B_UINT32_TYPE, frame->es },
		{ B_UINT32_TYPE, frame->fs }, { B_UINT32_TYPE, frame->gs },
	};
	#endif

	const char* const bufferStart = buffer;

	// Format each register as big-endian hex string
	// (GDB protocol requires big-endian regardless of target architecture)
	for (int32 i = 0; i < kRegisterCount; i++) {
		int result = 0;
		switch (registers[i].type) {
			case B_UINT64_TYPE:
				result = snprintf(buffer, bufferSize, "%016" B_PRIx64,
								  (uint64)B_HOST_TO_BENDIAN_INT64(registers[i].value));
				break;
			case B_UINT32_TYPE:
				result = snprintf(buffer, bufferSize, "%08" B_PRIx32,
								  (uint32)B_HOST_TO_BENDIAN_INT32((uint32)registers[i].value));
				break;
		}

		if (result >= (int)bufferSize)
			return B_BUFFER_OVERFLOW;

		buffer += result;
		bufferSize -= result;
	}

	return buffer - bufferStart;
}


// #pragma mark - High-Resolution Delay Support


static void (*sDebugSnooze)(uint32) = NULL;
static uint64 sDebugSnoozeConversionFactor = 0;


/*!	High-resolution delay using AMD MONITORX/MWAITX instructions
 *
 * MWAITX can wait for specific number of TSC ticks (time stamp counter).
 * More accurate than spinning in software loop.
 *
 * Reference: AMD64 Architecture Programmer's Manual, Volume 3, MWAITX instruction
 */
static void
debug_snooze_mwaitx(uint32 delay)
{
	// MONITORX: Set up address to monitor (any valid address works)
	// EAX = address, ECX = extensions (0), EDX = hints (0)
	asm volatile(".byte 0x0f, 0x01, 0xfa;"
	:: "a" (sDebugSnooze), "c" (0), "d" (0));

	// MWAITX: Wait for time or event
	// EAX = hints (0xf0 = disable C-states), ECX = extensions (0x2 = timer enable)
	// EBX = timeout in TSC ticks
	asm volatile(".byte 0x0f, 0x01, 0xfb;"
	:: "a" (0xf0), "c" (0x2), "b" (delay));
}


/*!	High-resolution delay using Intel TPAUSE instruction
 *
 * TPAUSE can wait until specific TSC value reached.
 * Introduced with Intel Tremont architecture.
 *
 * Reference: Intel SDM, Volume 2B, TPAUSE instruction
 */
static void
debug_snooze_tpause(uint32 delay)
{
	memory_read_barrier();
	uint64 target = __rdtsc() + delay;

	// TPAUSE: Wait until TSC reaches target or interrupt occurs
	// ECX = options (0x0), EDX:EAX = target TSC value (64-bit)
	uint32 low = target;
	uint32 high = target >> 32;
	asm volatile(".byte 0x66, 0x0f, 0xae, 0xf1;"
	:: "c" (0x0), "a" (low), "d" (high));
}


/*!	Implements microsecond-resolution delay for kernel debugger
 *
 * Kernel debugger cannot use normal timer interrupts, so we need alternative
 * delay mechanism. Uses TSC-based instructions if available, otherwise spins
 * on TSC reads with PAUSE.
 *
 * Conversion: duration (µs) * conversionFactor / 1000 = TSC ticks
 *
 * \param duration Delay in microseconds
 */
void
arch_debug_snooze(bigtime_t duration)
{
	uint32 delay = (duration * sDebugSnoozeConversionFactor) / 1000;
	if (delay == 0)
		delay = 1;

	if (sDebugSnooze != NULL) {
		// Use hardware wait instruction
		sDebugSnooze(delay);
		return;
	}

	// Fallback: spin on RDTSC with PAUSE
	memory_read_barrier();
	uint64 target = __rdtsc() + delay;

	while (__rdtsc() < target)
		arch_cpu_pause();  // Hint to CPU that we're spinning
}


status_t
arch_debug_init(kernel_args* args)
{
	// Calculate TSC conversion factor for arch_debug_snooze
	// system_time_cv_factor converts TSC to microseconds
	// We need inverse: microseconds to TSC
	sDebugSnoozeConversionFactor =
	(uint64(1000) << 32) / args->arch_args.system_time_cv_factor;

	// Check for hardware wait instructions
	if (x86_check_feature(IA32_FEATURE_AMD_EXT_MWAITX, FEATURE_EXT_AMD_ECX))
		sDebugSnooze = debug_snooze_mwaitx;
	if (x86_check_feature(IA32_FEATURE_WAITPKG, FEATURE_7_ECX))
		sDebugSnooze = debug_snooze_tpause;

	// Register debugger commands
	add_debugger_command("where", &stack_trace, "Same as \"sc\"");
	add_debugger_command("bt", &stack_trace, "Same as \"sc\" (as in gdb)");
	add_debugger_command("sc", &stack_trace,
						 "Stack crawl for current thread (or any other)");
	add_debugger_command("iframe", &dump_iframes,
						 "Dump iframes for the specified thread");
	#ifndef __x86_64__
	add_debugger_command("call", &show_call, "Show call with arguments");
	#endif
	add_debugger_command_etc("in_context", &cmd_in_context,
							 "Executes a command in the context of a given thread",
						  "<thread ID> <command> ...\n"
						  "Executes a command in the context of a given thread.\n",
						  B_KDEBUG_DONT_PARSE_ARGUMENTS);

	return B_NO_ERROR;
}
