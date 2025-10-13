/*
 * Copyright 2005-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <arch/user_debugger.h>

#include <string.h>

#include <debugger.h>
#include <driver_settings.h>
#include <interrupts.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>


//#define TRACE_ARCH_USER_DEBUGGER
#ifdef TRACE_ARCH_USER_DEBUGGER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// Error codes for breakpoint/watchpoint operations
#define B_NO_MORE_BREAKPOINTS				B_BUSY
#define B_NO_MORE_WATCHPOINTS				B_BUSY
#define B_BAD_WATCHPOINT_ALIGNMENT			B_BAD_VALUE
#define B_WATCHPOINT_TYPE_NOT_SUPPORTED		B_NOT_SUPPORTED
#define B_WATCHPOINT_LENGTH_NOT_SUPPORTED	B_NOT_SUPPORTED
#define B_BREAKPOINT_NOT_FOUND				B_NAME_NOT_FOUND
#define B_WATCHPOINT_NOT_FOUND				B_NAME_NOT_FOUND


#ifdef __x86_64__
extern bool gHasXsave;
#else
extern bool gHasSSE;
#endif

// Software breakpoint instruction (int3)
const uint8 kX86SoftwareBreakpoint[1] = { 0xcc };

// DR7 bit field mappings (Intel SDM Vol. 3B, Section 17.2.4)
// Maps breakpoint slot index (0-3) to LEN field LSB position in DR7
static const size_t sDR7Len[4] = {
	X86_DR7_LEN0_LSB, X86_DR7_LEN1_LSB, X86_DR7_LEN2_LSB, X86_DR7_LEN3_LSB
};

// Maps breakpoint slot index (0-3) to R/W field LSB position in DR7
static const size_t sDR7RW[4] = {
	X86_DR7_RW0_LSB, X86_DR7_RW1_LSB, X86_DR7_RW2_LSB, X86_DR7_RW3_LSB
};

// Maps breakpoint slot index (0-3) to Local Enable bit position in DR7
static const size_t sDR7L[4] = {
	X86_DR7_L0, X86_DR7_L1, X86_DR7_L2, X86_DR7_L3
};

// Maps breakpoint slot index (0-3) to Global Enable bit position in DR7
static const size_t sDR7G[4] = {
	X86_DR7_G0, X86_DR7_G1, X86_DR7_G2, X86_DR7_G3
};

// Maps breakpoint slot index (0-3) to breakpoint detected bit in DR6
static const size_t sDR6B[4] = {
	X86_DR6_B0, X86_DR6_B1, X86_DR6_B2, X86_DR6_B3
};

// Enables QEMU single-step workaround via kernel driver settings
static bool sQEmuSingleStepHack = false;


// #pragma mark - Helper Functions: CPU State Management


#ifdef __x86_64__

/*!	Copies CPU register state from iframe to debug_cpu_state structure
 * \param frame Interrupt frame containing CPU state
 * \param cpuState Output structure to receive register values
 *
 * Note: Segment registers (DS, ES, FS, GS) are read directly from CPU
 * as they are not saved/restored on x86_64 interrupts (except FS/GS bases).
 */
static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	// General purpose registers
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

	// Control registers
	cpuState->rip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->rflags = frame->flags;
	cpuState->rsp = frame->sp;
	cpuState->ss = frame->ss;

	// Segment registers (not saved in iframe on x86_64)
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


/*!	Updates iframe with CPU register state from debug_cpu_state
 * \param frame Interrupt frame to be modified
 * \param cpuState Source structure containing new register values
 *
 * Note: Only general purpose registers, RIP, RFLAGS and RSP are updated.
 * Segment registers and exception info are not modified.
 */
static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
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
	frame->ip = cpuState->rip;

	// Preserve system flags, only allow user-settable flags to be modified
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
	| (cpuState->rflags & X86_EFLAGS_USER_SETTABLE_FLAGS);

	frame->sp = cpuState->rsp;
}

#else	// __x86_64__

/*!	Copies CPU register state from iframe to debug_cpu_state structure (x86)
 * \param frame Interrupt frame containing CPU state
 * \param cpuState Output structure to receive register values
 */
static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	cpuState->gs = frame->gs;
	cpuState->fs = frame->fs;
	cpuState->es = frame->es;
	cpuState->ds = frame->ds;
	cpuState->edi = frame->di;
	cpuState->esi = frame->si;
	cpuState->ebp = frame->bp;
	cpuState->esp = frame->sp;
	cpuState->ebx = frame->bx;
	cpuState->edx = frame->orig_edx;
	cpuState->ecx = frame->cx;
	cpuState->eax = frame->orig_eax;
	cpuState->vector = frame->vector;
	cpuState->error_code = frame->error_code;
	cpuState->eip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->eflags = frame->flags;
	cpuState->user_esp = frame->user_sp;
	cpuState->user_ss = frame->user_ss;
}


/*!	Updates iframe with CPU register state from debug_cpu_state (x86)
 * \param frame Interrupt frame to be modified
 * \param cpuState Source structure containing new register values
 */
static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
	// Note: Segment registers are not updated for safety reasons
	frame->di = cpuState->edi;
	frame->si = cpuState->esi;
	frame->bp = cpuState->ebp;
	frame->bx = cpuState->ebx;
	frame->dx = cpuState->edx;
	frame->cx = cpuState->ecx;
	frame->ax = cpuState->eax;
	frame->ip = cpuState->eip;

	// Preserve system flags, only allow user-settable flags
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
	| (cpuState->eflags & X86_EFLAGS_USER_SETTABLE_FLAGS);

	frame->user_sp = cpuState->user_esp;
}

#endif	// __x86_64__


/*!	Retrieves complete CPU state including FPU/SSE/AVX registers
 * \param thread Thread whose state to capture
 * \param frame Interrupt frame containing general purpose registers
 * \param cpuState Output structure to receive complete CPU state
 *
 * Important: Caller must not use FPU/SSE registers even indirectly,
 * as this function captures their current state.
 */
static void
get_cpu_state(Thread* thread, iframe* frame, debug_cpu_state* cpuState)
{
	#ifdef __x86_64__
	// Initialize extended registers area
	memset(&cpuState->extended_registers, 0,
		   sizeof(cpuState->extended_registers));

	if (frame->fpu != nullptr) {
		if (gHasXsave) {
			// XSAVE format includes AVX and potentially other extensions
			// TODO: Parse XSAVE header to determine actual saved state size
			// Currently assumes YMM (AVX) registers are present
			memcpy(&cpuState->extended_registers, frame->fpu,
				   sizeof(cpuState->extended_registers));
		} else {
			// FXSAVE format (legacy area only)
			memcpy(&cpuState->extended_registers, frame->fpu,
				   sizeof(cpuState->extended_registers.fp_fxsave));
		}
	}
	#else
	Thread* thisThread = thread_get_current_thread();
	if (gHasSSE) {
		if (thread == thisThread) {
			// FXSAVE requires 16-byte alignment. Use thread's fpu_state buffer
			// which is guaranteed to be aligned. Disable interrupts to safely
			// use this buffer.
			Thread* thread = thread_get_current_thread();
			InterruptsLocker locker;
			x86_fxsave(thread->arch_info.fpu_state);
			// FXSAVE does not reinit FPU state (unlike FNSAVE)
		}
		memcpy(&cpuState->extended_registers, thread->arch_info.fpu_state,
			   sizeof(cpuState->extended_registers));
	} else {
		if (thread == thisThread) {
			x86_fnsave(&cpuState->extended_registers);
			// FNSAVE reinitializes FPU state, so reload it
			x86_frstor(&cpuState->extended_registers);
		} else {
			memcpy(&cpuState->extended_registers, thread->arch_info.fpu_state,
				   sizeof(cpuState->extended_registers));
		}
		// TODO: Convert to FXSAVE format for consistency!
	}
	#endif
	get_iframe_registers(frame, cpuState);
}


// #pragma mark - Helper Functions: Breakpoint Management


/*!	Installs hardware breakpoints from team debug info into CPU debug registers
 * \param teamInfo Team's debug configuration containing breakpoint settings
 *
 * Interrupts must be disabled. Directly writes to DR0-DR3 (addresses)
 * and DR7 (control register). See Intel SDM Vol. 3B, Section 17.2.
 */
static inline void
install_breakpoints(const arch_team_debug_info& teamInfo)
{
	// Set breakpoint addresses in DR0-DR3
	asm("mov %0, %%dr0" : : "r"(teamInfo.breakpoints[0].address));
	asm("mov %0, %%dr1" : : "r"(teamInfo.breakpoints[1].address));
	asm("mov %0, %%dr2" : : "r"(teamInfo.breakpoints[2].address));
	asm("mov %0, %%dr3" : : "r"(teamInfo.breakpoints[3].address));

	// Enable breakpoints via DR7 control register
	asm("mov %0, %%dr7" : : "r"(teamInfo.dr7));
}


/*!	Disables all hardware breakpoints
 *
 * Interrupts must be disabled. Writes a safe value to DR7 that masks
 * all breakpoints. See Intel SDM Vol. 3B, Section 17.2.4.
 */
static inline void
disable_breakpoints()
{
	asm("mov %0, %%dr7" : : "r"((size_t)X86_BREAKPOINTS_DISABLED_DR7));
}


/*!	Sets a hardware breakpoint in team debug info
 * \param info Team debug info structure to modify
 * \param address Breakpoint address
 * \param type Breakpoint type (instruction/data write/data r/w)
 * \param length Breakpoint length (1/2/4/8 bytes)
 * \param setGlobalFlag If true, set global enable (survives task switch)
 * \return B_OK on success, B_NO_MORE_BREAKPOINTS if all 4 slots occupied
 *
 * Interrupts must be disabled and team debug info lock held.
 * Updates the DR7 configuration but does not write to hardware.
 */
static inline status_t
set_breakpoint(arch_team_debug_info& info, void* address, size_t type,
			   size_t length, bool setGlobalFlag)
{
	// Check if breakpoint already exists at this address/type
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (info.breakpoints[i].address == address
			&& info.breakpoints[i].type == type) {
			return B_OK;  // Already set
			}
	}

	// Find free slot (slot with NULL address)
	int32 slot = -1;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (!info.breakpoints[i].address) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		return (type == X86_INSTRUCTION_BREAKPOINT)
		? B_NO_MORE_BREAKPOINTS : B_NO_MORE_WATCHPOINTS;
	}

	// Configure breakpoint slot
	info.breakpoints[slot].address = address;
	info.breakpoints[slot].type = type;
	info.breakpoints[slot].length = length;

	// Build DR7 configuration for this slot
	// LEN field (2 bits) + R/W field (2 bits) + Local Enable (1 bit)
	info.dr7 |= (length << sDR7Len[slot])
	| (type << sDR7RW[slot])
	| (1 << sDR7L[slot]);

	if (setGlobalFlag)
		info.dr7 |= (1 << sDR7G[slot]);

	return B_OK;
}


/*!	Clears a hardware breakpoint from team debug info
 * \param info Team debug info structure to modify
 * \param address Breakpoint address
 * \param watchpoint True if clearing watchpoint, false for breakpoint
 * \return B_OK on success, B_BREAKPOINT_NOT_FOUND or B_WATCHPOINT_NOT_FOUND
 *
 * Interrupts must be disabled and team debug info lock held.
 * Updates the DR7 configuration but does not write to hardware.
 */
static inline status_t
clear_breakpoint(arch_team_debug_info& info, void* address, bool watchpoint)
{
	// Find the breakpoint slot
	int32 slot = -1;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		bool isWatchpoint = (info.breakpoints[i].type != X86_INSTRUCTION_BREAKPOINT);
		if (info.breakpoints[i].address == address && watchpoint == isWatchpoint) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		return watchpoint ? B_WATCHPOINT_NOT_FOUND : B_BREAKPOINT_NOT_FOUND;
	}

	// Clear the slot
	info.breakpoints[slot].address = NULL;

	// Clear all DR7 fields for this slot (LEN + R/W + Local + Global enable)
	info.dr7 &= ~((0x3 << sDR7Len[slot])
	| (0x3 << sDR7RW[slot])
	| (1 << sDR7L[slot])
	| (1 << sDR7G[slot]));

	return B_OK;
}


/*!	Sets a userland breakpoint/watchpoint
 * \param address Breakpoint address
 * \param type Breakpoint type (X86_INSTRUCTION_BREAKPOINT, etc.)
 * \param length Length in bytes (1, 2, 4, 8)
 * \return B_OK on success, error code on failure
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


/*!	Clears a userland breakpoint/watchpoint
 * \param address Breakpoint address
 * \param watchpoint True if clearing watchpoint, false for breakpoint
 * \return B_OK on success, error code on failure
 */
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


#if KERNEL_BREAKPOINTS

// #pragma mark - Kernel Breakpoint Support


/*!	Installs kernel breakpoints on current CPU
 * \param cookie Unused
 * \param cpu CPU number (unused, operates on current CPU)
 *
 * Called via call_all_cpus() to install kernel breakpoints on all CPUs.
 */
static void
install_breakpoints_per_cpu(void* /*cookie*/, int cpu)
{
	Team* kernelTeam = team_get_kernel_team();

	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
	install_breakpoints(kernelTeam->debug_info.arch_info);
	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
}


/*!	Sets a kernel-space breakpoint/watchpoint
 * \param address Breakpoint address
 * \param type Breakpoint type
 * \param length Length in bytes
 * \return B_OK on success, error code on failure
 *
 * Kernel breakpoints use global enable flag and are installed on all CPUs.
 */
static status_t
set_kernel_breakpoint(void* address, size_t type, size_t length)
{
	if (!address)
		return B_BAD_VALUE;

	Team* kernelTeam = team_get_kernel_team();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	status_t error = set_breakpoint(kernelTeam->debug_info.arch_info, address,
									type, length, true);  // setGlobalFlag = true

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	// Install on all CPUs
	call_all_cpus(install_breakpoints_per_cpu, NULL);

	restore_interrupts(state);

	return error;
}


/*!	Clears a kernel-space breakpoint/watchpoint
 * \param address Breakpoint address
 * \param watchpoint True if clearing watchpoint
 * \return B_OK on success, error code on failure
 */
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

	// Update all CPUs
	call_all_cpus(install_breakpoints_per_cpu, NULL);

	restore_interrupts(state);

	return error;
}

#endif	// KERNEL_BREAKPOINTS


/*!	Validates watchpoint parameters and converts to x86 architecture values
 * \param address Watchpoint address
 * \param type Watchpoint type (B_DATA_WRITE_WATCHPOINT, etc.)
 * \param length Watchpoint length in bytes
 * \param archType Output: x86-specific type value
 * \param archLength Output: x86-specific length value
 * \return B_OK on success, error code if parameters invalid
 *
 * Checks alignment requirements and converts generic watchpoint types
 * to x86 R/W field values. See Intel SDM Vol. 3B, Section 17.2.5.
 */
static inline status_t
check_watch_point_parameters(void* address, uint32 type, int32 length,
							 size_t& archType, size_t& archLength)
{
	// Validate and convert type
	switch (type) {
		case B_DATA_WRITE_WATCHPOINT:
			archType = X86_DATA_WRITE_BREAKPOINT;
			break;
		case B_DATA_READ_WRITE_WATCHPOINT:
			archType = X86_DATA_READ_WRITE_BREAKPOINT;
			break;
		case B_DATA_READ_WATCHPOINT:
		default:
			return B_WATCHPOINT_TYPE_NOT_SUPPORTED;
	}

	// Validate and convert length with alignment check
	switch (length) {
		case 1:
			archLength = X86_BREAKPOINT_LENGTH_1;
			break;
		case 2:
			if ((addr_t)address & 0x1)
				return B_BAD_WATCHPOINT_ALIGNMENT;
		archLength = X86_BREAKPOINT_LENGTH_2;
		break;
		case 4:
			if ((addr_t)address & 0x3)
				return B_BAD_WATCHPOINT_ALIGNMENT;
		archLength = X86_BREAKPOINT_LENGTH_4;
		break;
		default:
			return B_WATCHPOINT_LENGTH_NOT_SUPPORTED;
	}

	return B_OK;
}


// #pragma mark - Kernel Debugger Commands


#if KERNEL_BREAKPOINTS

/*!	Debugger command: List all kernel breakpoints
 * \return 0
 */
static int
debugger_breakpoints(int argc, char** argv)
{
	Team* kernelTeam = team_get_kernel_team();
	arch_team_debug_info& info = kernelTeam->debug_info.arch_info;

	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		kprintf("breakpoint[%" B_PRId32 "] ", i);

		if (info.breakpoints[i].address != NULL) {
			kprintf("%p ", info.breakpoints[i].address);

			// Print breakpoint type
			switch (info.breakpoints[i].type) {
				case X86_INSTRUCTION_BREAKPOINT:
					kprintf("instruction");
					break;
				case X86_IO_READ_WRITE_BREAKPOINT:
					kprintf("io read/write");
					break;
				case X86_DATA_WRITE_BREAKPOINT:
					kprintf("data write");
					break;
				case X86_DATA_READ_WRITE_BREAKPOINT:
					kprintf("data read/write");
					break;
			}

			// Print length for data breakpoints
			if (info.breakpoints[i].type != X86_INSTRUCTION_BREAKPOINT) {
				int length = 1;
				switch (info.breakpoints[i].length) {
					case X86_BREAKPOINT_LENGTH_1:
						length = 1;
						break;
					case X86_BREAKPOINT_LENGTH_2:
						length = 2;
						break;
					case X86_BREAKPOINT_LENGTH_4:
						length = 4;
						break;
				}
				kprintf(" %d byte%s", length, (length > 1 ? "s" : ""));
			}
		} else {
			kprintf("unused");
		}

		kprintf("\n");
	}

	return 0;
}


/*!	Debugger command: Set or clear an instruction breakpoint
 * Usage: breakpoint <address> [clear]
 * \return 0
 */
static int
debugger_breakpoint(int argc, char** argv)
{
	if (argc < 2 || argc > 3)
		return print_debugger_command_usage(argv[0]);

	addr_t address = strtoul(argv[1], NULL, 0);
	if (address == 0)
		return print_debugger_command_usage(argv[0]);

	bool clear = false;
	if (argc == 3) {
		if (strcmp(argv[2], "clear") == 0)
			clear = true;
		else
			return print_debugger_command_usage(argv[0]);
	}

	arch_team_debug_info& info = team_get_kernel_team()->debug_info.arch_info;

	status_t error;
	if (clear) {
		error = clear_breakpoint(info, (void*)address, false);
	} else {
		error = set_breakpoint(info, (void*)address, X86_INSTRUCTION_BREAKPOINT,
							   X86_BREAKPOINT_LENGTH_1, true);
	}

	if (error == B_OK)
		call_all_cpus_sync(install_breakpoints_per_cpu, NULL);
	else
		kprintf("Failed to %s breakpoint: %s\n", clear ? "clear" : "install",
				strerror(error));

		return 0;
}


/*!	Debugger command: Set or clear a data watchpoint
 * Usage: watchpoint <address> [rw|clear] [<length>]
 * \return 0
 */
static int
debugger_watchpoint(int argc, char** argv)
{
	if (argc < 2 || argc > 4)
		return print_debugger_command_usage(argv[0]);

	addr_t address = strtoul(argv[1], NULL, 0);
	if (address == 0)
		return print_debugger_command_usage(argv[0]);

	bool clear = false;
	bool readWrite = false;
	int argi = 2;
	int length = 1;

	if (argc >= 3) {
		if (strcmp(argv[argi], "clear") == 0) {
			clear = true;
			argi++;
		} else if (strcmp(argv[argi], "rw") == 0) {
			readWrite = true;
			argi++;
		}

		if (!clear && argi < argc) {
			length = strtoul(argv[argi++], NULL, 0);
		}

		if (length == 0 || argi < argc)
			return print_debugger_command_usage(argv[0]);
	}

	arch_team_debug_info& info = team_get_kernel_team()->debug_info.arch_info;

	status_t error;
	if (clear) {
		error = clear_breakpoint(info, (void*)address, true);
	} else {
		uint32 type = readWrite ? B_DATA_READ_WRITE_WATCHPOINT
		: B_DATA_WRITE_WATCHPOINT;

		size_t archType, archLength;
		error = check_watch_point_parameters((void*)address, type, length,
											 archType, archLength);

		if (error == B_OK) {
			error = set_breakpoint(info, (void*)address, archType, archLength,
								   true);
		}
	}

	if (error == B_OK)
		call_all_cpus_sync(install_breakpoints_per_cpu, NULL);
	else
		kprintf("Failed to %s watchpoint: %s\n", clear ? "clear" : "install",
				strerror(error));

		return 0;
}


/*!	Debugger command: Enable single-step mode and exit debugger
 * \return B_KDEBUG_QUIT to exit debugger
 */
static int
debugger_single_step(int argc, char** argv)
{
	iframe* frame = x86_get_current_iframe();
	if (frame == NULL) {
		kprintf("Failed to get the current iframe!\n");
		return 0;
	}

	// Set Trap Flag (TF) in EFLAGS to enable single-step mode
	// See Intel SDM Vol. 3A, Section 2.3
	frame->flags |= (1 << X86_EFLAGS_TF);

	return B_KDEBUG_QUIT;
}

#endif	// KERNEL_BREAKPOINTS


// #pragma mark - Public API: Architecture Interface


void
arch_clear_team_debug_info(arch_team_debug_info* info)
{
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++)
		info->breakpoints[i].address = NULL;

	info->dr7 = X86_BREAKPOINTS_DISABLED_DR7;
}


void
arch_destroy_team_debug_info(arch_team_debug_info* info)
{
	arch_clear_team_debug_info(info);
}


void
arch_clear_thread_debug_info(arch_thread_debug_info* info)
{
	info->flags = 0;
}


void
arch_destroy_thread_debug_info(arch_thread_debug_info* info)
{
	arch_clear_thread_debug_info(info);
}


void
arch_update_thread_single_step()
{
	iframe* frame = x86_get_user_iframe();
	if (frame == NULL)
		return;

	Thread* thread = thread_get_current_thread();

	// Set or clear Trap Flag in EFLAGS based on single-step state
	if (thread->debug_info.flags & B_THREAD_DEBUG_SINGLE_STEP)
		frame->flags |= (1 << X86_EFLAGS_TF);
	else
		frame->flags &= ~(1 << X86_EFLAGS_TF);
}


void
arch_set_debug_cpu_state(const debug_cpu_state* cpuState)
{
	iframe* frame = x86_get_user_iframe();
	if (frame == NULL)
		return;

	#ifdef __x86_64__
	Thread* thread = thread_get_current_thread();
	memcpy(thread->arch_info.user_fpu_state, &cpuState->extended_registers,
		   sizeof(cpuState->extended_registers));
	frame->fpu = &thread->arch_info.user_fpu_state;
	#else
	if (gHasSSE) {
		// FXRSTOR requires 16-byte alignment. Use thread's fpu_state buffer
		// temporarily. Disable interrupts for safe access.
		Thread* thread = thread_get_current_thread();
		InterruptsLocker locker;
		memcpy(thread->arch_info.fpu_state, &cpuState->extended_registers,
			   sizeof(cpuState->extended_registers));
		x86_fxrstor(thread->arch_info.fpu_state);
	} else {
		// TODO: Implement! Need to convert from FXSAVE format to FNSAVE format
		// Currently not supported
	}
	#endif
	set_iframe_registers(frame, cpuState);
}


void
arch_get_debug_cpu_state(debug_cpu_state* cpuState)
{
	iframe* frame = x86_get_user_iframe();
	if (frame != NULL)
		get_cpu_state(thread_get_current_thread(), frame, cpuState);
}


status_t
arch_get_thread_debug_cpu_state(Thread* thread, debug_cpu_state* cpuState)
{
	iframe* frame = x86_get_thread_user_iframe(thread);
	if (frame == NULL)
		return B_BAD_VALUE;

	get_cpu_state(thread, frame, cpuState);
	return B_OK;
}


status_t
arch_set_breakpoint(void* address)
{
	return set_breakpoint(address, X86_INSTRUCTION_BREAKPOINT,
						  X86_BREAKPOINT_LENGTH_1);
}


status_t
arch_clear_breakpoint(void* address)
{
	return clear_breakpoint(address, false);
}


status_t
arch_set_watchpoint(void* address, uint32 type, int32 length)
{
	size_t archType, archLength;
	status_t error = check_watch_point_parameters(address, type, length,
												  archType, archLength);
	if (error != B_OK)
		return error;

	return set_breakpoint(address, archType, archLength);
}


status_t
arch_clear_watchpoint(void* address)
{
	return clear_breakpoint(address, true);
}


bool
arch_has_breakpoints(arch_team_debug_info* info)
{
	// Reading dr7 is atomic, no lock needed
	// Caller ensures info doesn't disappear
	return (info->dr7 != X86_BREAKPOINTS_DISABLED_DR7);
}


#if KERNEL_BREAKPOINTS

status_t
arch_set_kernel_breakpoint(void* address)
{
	status_t error = set_kernel_breakpoint(address, X86_INSTRUCTION_BREAKPOINT,
										   X86_BREAKPOINT_LENGTH_1);

	if (error != B_OK) {
		panic("arch_set_kernel_breakpoint() failed to set breakpoint: %s",
			  strerror(error));
	}

	return error;
}


status_t
arch_clear_kernel_breakpoint(void* address)
{
	status_t error = clear_kernel_breakpoint(address, false);

	if (error != B_OK) {
		panic("arch_clear_kernel_breakpoint() failed to clear breakpoint: %s",
			  strerror(error));
	}

	return error;
}


status_t
arch_set_kernel_watchpoint(void* address, uint32 type, int32 length)
{
	size_t archType, archLength;
	status_t error = check_watch_point_parameters(address, type, length,
												  archType, archLength);

	if (error == B_OK)
		error = set_kernel_breakpoint(address, archType, archLength);

	if (error != B_OK) {
		panic("arch_set_kernel_watchpoint() failed to set watchpoint: %s",
			  strerror(error));
	}

	return error;
}


status_t
arch_clear_kernel_watchpoint(void* address)
{
	status_t error = clear_kernel_breakpoint(address, true);

	if (error != B_OK) {
		panic("arch_clear_kernel_watchpoint() failed to clear watchpoint: %s",
			  strerror(error));
	}

	return error;
}

#endif	// KERNEL_BREAKPOINTS


// #pragma mark - x86 Implementation Interface


/*!	Disables kernel breakpoints and installs user breakpoints on kernel exit
 * \param frame Unused (can be NULL)
 *
 * Interrupts must be disabled. Called before returning to userland to
 * ensure user-mode debug state is active.
 */
void
x86_init_user_debug_at_kernel_exit(iframe* frame)
{
	Thread* thread = thread_get_current_thread();

	if (!(thread->flags & THREAD_FLAGS_BREAKPOINTS_DEFINED))
		return;

	// Disable kernel breakpoints
	disable_breakpoints();

	// Install user breakpoints
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	arch_team_debug_info &teamInfo = thread->team->debug_info.arch_info;
	install_breakpoints(teamInfo);

	atomic_or(&thread->flags, THREAD_FLAGS_BREAKPOINTS_INSTALLED);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
}


/*!	Saves debug register state and switches to kernel breakpoints on entry
 *
 * Interrupts must be disabled. Saves DR6 (Debug Status) and DR7 (Control)
 * to CPU structure for later processing, then installs kernel breakpoints.
 */
void
x86_exit_user_debug_at_kernel_entry()
{
	Thread* thread = thread_get_current_thread();

	// Save DR6 and DR7 before they might be overwritten by subsequent
	// debug exceptions. These are needed by x86_handle_debug_exception().
	// See Intel SDM Vol. 3B, Section 17.2.
	asm("mov %%dr6, %0" : "=r"(thread->cpu->arch.dr6));
	asm("mov %%dr7, %0" : "=r"(thread->cpu->arch.dr7));

	if (!(thread->flags & THREAD_FLAGS_BREAKPOINTS_INSTALLED))
		return;

	// Disable user breakpoints
	disable_breakpoints();

	// Install kernel breakpoints
	Team* kernelTeam = team_get_kernel_team();

	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
	install_breakpoints(kernelTeam->debug_info.arch_info);
	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	atomic_and(&thread->flags, ~THREAD_FLAGS_BREAKPOINTS_INSTALLED);
}


/*!	Handles debug exceptions (#DB, vector 1)
 * \param frame Interrupt frame from exception
 *
 * Interrupts disabled on entry, may be enabled during processing.
 * Processes hardware breakpoints, watchpoints, and single-step exceptions.
 * See Intel SDM Vol. 3B, Chapter 17.
 */
void
x86_handle_debug_exception(iframe* frame)
{
	Thread* thread = thread_get_current_thread();

	// Get debug registers. For userland exceptions, they were saved by
	// x86_exit_user_debug_at_kernel_entry(). For kernel exceptions,
	// read them directly.
	size_t dr6;
	size_t dr7;
	if (IFRAME_IS_USER(frame)) {
		dr6 = thread->cpu->arch.dr6;
		dr7 = thread->cpu->arch.dr7;
	} else {
		asm("mov %%dr6, %0" : "=r"(dr6));
		asm("mov %%dr7, %0" : "=r"(dr7));
	}

	TRACE(("x86_handle_debug_exception(): DR6: %lx, DR7: %lx\n", dr6, dr7));

	// Check exception condition (Intel SDM Vol. 3B, Section 17.2.3)
	if (dr6 & X86_DR6_BREAKPOINT_MASK) {
		// Hardware breakpoint/watchpoint hit (B0-B3 bits in DR6)

		// Determine if it's a watchpoint or instruction breakpoint
		bool watchpoint = true;
		for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
			if (dr6 & (1 << sDR6B[i])) {
				size_t type = (dr7 >> sDR7RW[i]) & 0x3;
				if (type == X86_INSTRUCTION_BREAKPOINT)
					watchpoint = false;
			}
		}

		if (IFRAME_IS_USER(frame)) {
			enable_interrupts();

			if (watchpoint)
				user_debug_watchpoint_hit();
			else
				user_debug_breakpoint_hit(false);
		} else {
			panic("hit kernel %spoint: dr6: 0x%lx, dr7: 0x%lx",
				  watchpoint ? "watch" : "break", dr6, dr7);
		}
	} else if (dr6 & (1 << X86_DR6_BD)) {
		// General Detect Exception (GD bit in DR7 set and DR access attempted)
		// We don't use GD, so this is spurious
		if (IFRAME_IS_USER(frame)) {
			dprintf("x86_handle_debug_exception(): ignoring spurious general "
			"detect exception\n");
			enable_interrupts();
		} else {
			panic("spurious general detect exception in kernel mode");
		}
	} else if ((dr6 & (1 << X86_DR6_BS)) || sQEmuSingleStepHack) {
		// Single-step exception (BS bit in DR6 or QEMU workaround)

		if (IFRAME_IS_USER(frame)) {
			enable_interrupts();
			user_debug_single_stepped();
		} else {
			// Kernel single-step

			// Disable single-stepping for safety (next "step" command re-enables)
			frame->flags &= ~(1 << X86_EFLAGS_TF);

			// Check if this is syscall entry single-step (common case)
			// or genuine kernel single-stepping (rare, usually in KDL)
			bool inKernel = true;
			if (thread->team != team_get_kernel_team()
				&& x86_get_user_iframe() == NULL) {
				inKernel = false;
				}

				if (inKernel) {
					panic("kernel single step");
				} else {
					// Single-step exception at syscall/interrupt entry point.
					// Happens when userland calls syscall with TF set.
					// Defer notification until kernel exit.
					InterruptsSpinLocker threadDebugInfoLocker(
						thread->debug_info.lock);

					int32 teamDebugFlags
					= atomic_get(&thread->team->debug_info.flags);
					if (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
						atomic_or(&thread->debug_info.flags,
								  B_THREAD_DEBUG_NOTIFY_SINGLE_STEP
								  | B_THREAD_DEBUG_STOP);

						atomic_or(&thread->flags, THREAD_FLAGS_DEBUG_THREAD);
					}
				}
		}
	} else if (dr6 & (1 << X86_DR6_BT)) {
		// Task switch breakpoint (T bit in TSS set)
		// We don't use this feature
		if (IFRAME_IS_USER(frame)) {
			dprintf("x86_handle_debug_exception(): ignoring spurious task switch "
			"exception\n");
			enable_interrupts();
		} else {
			panic("spurious task switch exception in kernel mode");
		}
	} else {
		// No recognized condition - spurious exception
		if (IFRAME_IS_USER(frame)) {
			TRACE(("x86_handle_debug_exception(): ignoring spurious debug "
			"exception (no condition recognized)\n"));
			enable_interrupts();
		} else {
			panic("spurious debug exception in kernel mode (no condition "
			"recognized)");
		}
	}
}


/*!	Handles breakpoint exceptions (#BP, vector 3, int3 instruction)
 * \param frame Interrupt frame from exception
 *
 * Interrupts disabled on entry, enabled during processing.
 * Adjusts return address to point to int3 instruction for debugger.
 */
void
x86_handle_breakpoint_exception(iframe* frame)
{
	TRACE(("x86_handle_breakpoint_exception()\n"));

	// Reset EIP/RIP to point to int3 instruction (it currently points after)
	frame->ip--;

	if (!IFRAME_IS_USER(frame)) {
		panic("breakpoint exception in kernel mode");
		return;
	}

	enable_interrupts();
	user_debug_breakpoint_hit(true);  // software breakpoint
}


/*!	Initializes user debugging support
 *
 * Reads kernel settings and registers debugger commands.
 */
void
x86_init_user_debug()
{
	// Load QEMU single-step workaround setting
	if (void* handle = load_driver_settings("kernel")) {
		sQEmuSingleStepHack = get_driver_boolean_parameter(handle,
														   "qemu_single_step_hack", false, false);

		unload_driver_settings(handle);
	}

	#if KERNEL_BREAKPOINTS
	// Register kernel debugger commands
	add_debugger_command_etc("breakpoints", &debugger_breakpoints,
							 "Lists current break-/watchpoints",
						  "\n"
						  "Lists the current kernel break-/watchpoints.\n", 0);
	add_debugger_command_alias("watchpoints", "breakpoints", NULL);
	add_debugger_command_etc("breakpoint", &debugger_breakpoint,
							 "Set/clears a breakpoint",
						  "<address> [ clear ]\n"
						  "Sets respectively clears the breakpoint at address <address>.\n", 0);
	add_debugger_command_etc("watchpoint", &debugger_watchpoint,
							 "Set/clears a watchpoint",
						  "<address> <address> ( [ rw ] [ <size> ] | clear )\n"
						  "Sets respectively clears the watchpoint at address <address>.\n"
						  "If \"rw\" is given the new watchpoint is a read/write watchpoint\n"
						  "otherwise a write watchpoint only.\n", 0);
	add_debugger_command_etc("step", &debugger_single_step,
							 "Single-steps to the next instruction",
						  "\n"
						  "Single-steps to the next instruction.\n", 0);
	#endif
}
