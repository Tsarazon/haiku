/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/thread.h>

#include <string.h>

#include <arch_thread_defs.h>
#include <commpage.h>
#include <cpu.h>
#include <debug.h>
#include <generic_syscall.h>
#include <kernel.h>
#include <ksignal.h>
#include <interrupts.h>
#include <team.h>
#include <thread.h>
#include <tls.h>
#include <tracing.h>
#include <util/Random.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

#include "paging/X86PagingStructures.h"
#include "paging/X86VMTranslationMap.h"


#define TRACE_ARCH_THREAD
#ifdef TRACE_ARCH_THREAD
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


#ifdef SYSCALL_TRACING

namespace SyscallTracing {

class RestartSyscall : public AbstractTraceEntry {
public:
	RestartSyscall()
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("syscall restart");
	}
};

}

#	define TSYSCALL(x)	new(std::nothrow) SyscallTracing::x

#else
#	define TSYSCALL(x)
#endif


static constexpr addr_t X86_64_RED_ZONE_SIZE = 128;
static constexpr addr_t X86_64_STACK_ALIGNMENT = 16;
static constexpr size_t MAX_SIGNAL_FRAME_SIZE = 8192;
static constexpr uint32 MAX_NESTED_SIGNALS = 16;
static constexpr size_t MIN_KERNEL_STACK_RESERVE = 512;

extern "C" void x86_64_thread_entry();

static arch_thread sInitialState _ALIGNED(64);
extern uint64 gFPUSaveLength;
extern bool gHasXsave;
extern bool gHasXsavec;


// RAII guard for signal_delivery_depth.
// Decrements the counter on destruction unless Commit() was called.
class SignalDepthGuard {
public:
	explicit SignalDepthGuard(Thread* thread)
		:
		fThread(thread),
		fCommitted(false)
	{
	}

	~SignalDepthGuard()
	{
		if (!fCommitted)
			atomic_add((int32*)&fThread->arch_info.signal_delivery_depth, -1);
	}

	void Commit() { fCommitted = true; }

private:
	Thread*	fThread;
	bool	fCommitted;

	SignalDepthGuard(const SignalDepthGuard&) = delete;
	SignalDepthGuard& operator=(const SignalDepthGuard&) = delete;
};


// #pragma mark - address and stack validation


static inline bool
is_user_address_valid(addr_t address)
{
	return address >= USER_BASE && address < USER_BASE + USER_SIZE;
}


static inline bool
is_user_range_valid(addr_t address, size_t size)
{
	if (address < USER_BASE)
		return false;
	if (size > USER_SIZE)
		return false;
	if (address > USER_BASE + USER_SIZE - size)
		return false;
	return true;
}


static inline bool
is_stack_aligned(addr_t sp)
{
	return (sp & (X86_64_STACK_ALIGNMENT - 1)) == 8;
}


static void
validate_kernel_stack(Thread* thread)
{
	ASSERT(thread != NULL);

	if (thread->kernel_stack_base == 0 || thread->kernel_stack_top == 0)
		return;

	addr_t current_sp;
	asm volatile("movq %%rsp, %0" : "=r"(current_sp));

	addr_t stack_base = (addr_t)thread->kernel_stack_base;
	addr_t stack_top = (addr_t)thread->kernel_stack_top;

	if (current_sp < stack_base + MIN_KERNEL_STACK_RESERVE) {
		panic("kernel stack overflow: thread %d sp=%#lx base=%#lx "
			"(used %zu/%zu bytes)",
			thread->id, current_sp, stack_base,
			stack_top - current_sp, stack_top - stack_base);
	}

	if (current_sp >= stack_top) {
		panic("kernel stack underflow: thread %d sp=%#lx top=%#lx",
			thread->id, current_sp, stack_top);
	}
}


// #pragma mark - FPU state validation


static bool
validate_fpu_state(const savefpu* state)
{
	ASSERT(state != NULL);

	uint16 fpu_control = state->fp_fxsave.control;
	if ((fpu_control & 0x003F) == 0)
		return false;

	uint16 fpu_status = state->fp_fxsave.status;
	if (fpu_status & 0x8080)
		return false;

	uint32 mxcsr = state->fp_fxsave.mxcsr;
	if (mxcsr & 0xFFFF0000)
		return false;

	if ((mxcsr & 0x1F80) == 0)
		return false;

	return true;
}


// #pragma mark - debug output


static void
dump_thread_context(Thread* thread, iframe* frame)
{
	if (thread == NULL) {
		kprintf("Thread: NULL\n");
		return;
	}

	kprintf("\n=== Thread %d (%s) ===\n", thread->id,
		thread->name[0] != '\0' ? thread->name : "<unnamed>");
	kprintf("Team: %d  State: %d  Priority: %d  Flags: %#x\n",
		thread->team != NULL ? thread->team->id : -1,
		thread->state, thread->priority, thread->flags);

	kprintf("Kernel stack: %#lx - %#lx (%zu bytes)\n",
		thread->kernel_stack_base, thread->kernel_stack_top,
		thread->kernel_stack_top - thread->kernel_stack_base);
	kprintf("User stack: %#lx - %#lx (%zu bytes)\n",
		thread->user_stack_base,
		thread->user_stack_base + thread->user_stack_size,
		thread->user_stack_size);

	if (thread->signal_stack_enabled) {
		kprintf("Signal stack: %#lx - %#lx (%zu bytes)\n",
			thread->signal_stack_base,
			thread->signal_stack_base + thread->signal_stack_size,
			thread->signal_stack_size);
	}

	kprintf("Signal delivery depth: %u\n",
		thread->arch_info.signal_delivery_depth);

	if (frame != NULL) {
		kprintf("\nIframe type: %lu\n", (uint64)frame->type);
		kprintf("RIP: %#018lx  RSP: %#018lx  RBP: %#018lx\n",
			frame->ip, frame->user_sp, frame->bp);
		kprintf("RAX: %#018lx  RBX: %#018lx  RCX: %#018lx\n",
			frame->ax, frame->bx, frame->cx);
		kprintf("RDX: %#018lx  RSI: %#018lx  RDI: %#018lx\n",
			frame->dx, frame->si, frame->di);
		kprintf("R8:  %#018lx  R9:  %#018lx  R10: %#018lx\n",
			frame->r8, frame->r9, frame->r10);
		kprintf("R11: %#018lx  R12: %#018lx  R13: %#018lx\n",
			frame->r11, frame->r12, frame->r13);
		kprintf("R14: %#018lx  R15: %#018lx\n",
			frame->r14, frame->r15);
		kprintf("CS: %#06lx  SS: %#06lx  FLAGS: %#018lx  "
			"ERR: %#lx  VEC: %ld\n",
			(uint64)frame->cs, (uint64)frame->ss, frame->flags,
			frame->error_code, frame->vector);
	}

	kprintf("\n");
}


// #pragma mark - commpage helpers


static status_t
lookup_commpage_entry(const Thread* thread, int32 commpageIndex,
	addr_t* _address)
{
	ASSERT(thread != NULL);
	ASSERT(thread->team != NULL);
	ASSERT(_address != NULL);

	addr_t commPageAddress = (addr_t)thread->team->commpage_address;
	if (!is_user_address_valid(commPageAddress))
		return B_BAD_ADDRESS;

	arch_cpu_enable_user_access();
	*_address = ((addr_t*)commPageAddress)[commpageIndex] + commPageAddress;
	arch_cpu_disable_user_access();

	if (!is_user_address_valid(*_address))
		return B_BAD_ADDRESS;

	return B_OK;
}


// #pragma mark - iframe <-> signal context conversion


static void
save_iframe_to_mcontext(const iframe* frame, signal_frame_data* data)
{
	auto& mc = data->context.uc_mcontext;
	mc.rax = frame->ax;
	mc.rbx = frame->bx;
	mc.rcx = frame->cx;
	mc.rdx = frame->dx;
	mc.rdi = frame->di;
	mc.rsi = frame->si;
	mc.rbp = frame->bp;
	mc.r8 = frame->r8;
	mc.r9 = frame->r9;
	mc.r10 = frame->r10;
	mc.r11 = frame->r11;
	mc.r12 = frame->r12;
	mc.r13 = frame->r13;
	mc.r14 = frame->r14;
	mc.r15 = frame->r15;
	mc.rsp = frame->user_sp;
	mc.rip = frame->ip;
	mc.rflags = frame->flags;
}


static void
restore_mcontext_to_iframe(iframe* frame, const signal_frame_data* data)
{
	const auto& mc = data->context.uc_mcontext;
	frame->ax = mc.rax;
	frame->bx = mc.rbx;
	frame->cx = mc.rcx;
	frame->dx = mc.rdx;
	frame->di = mc.rdi;
	frame->si = mc.rsi;
	frame->bp = mc.rbp;
	frame->r8 = mc.r8;
	frame->r9 = mc.r9;
	frame->r10 = mc.r10;
	frame->r11 = mc.r11;
	frame->r12 = mc.r12;
	frame->r13 = mc.r13;
	frame->r14 = mc.r14;
	frame->r15 = mc.r15;
	frame->user_sp = mc.rsp;
	frame->ip = mc.rip;
	frame->flags = (frame->flags & ~(uint64)X86_EFLAGS_USER_FLAGS)
		| (mc.rflags & X86_EFLAGS_USER_FLAGS);
}


static status_t
save_fpu_to_signal_context(const iframe* frame, signal_frame_data* data)
{
	ASSERT(gFPUSaveLength > 0
		&& gFPUSaveLength <= sizeof(data->context.uc_mcontext.fpu));

	if (frame->fpu != NULL) {
		ASSERT(((addr_t)frame->fpu & 63) == 0);

		if (!validate_fpu_state((const savefpu*)frame->fpu))
			return B_BAD_DATA;

		memcpy((void*)&data->context.uc_mcontext.fpu, frame->fpu,
			gFPUSaveLength);
	} else {
		memcpy((void*)&data->context.uc_mcontext.fpu,
			sInitialState.user_fpu_state, gFPUSaveLength);
	}

	auto& fxsave = data->context.uc_mcontext.fpu.fp_fxsave;
	fxsave.fault_address = x86_read_cr2();
	fxsave.error_code = frame->error_code;
	fxsave.cs = frame->cs;
	fxsave.ss = frame->ss;
	fxsave.trap_number = frame->vector;

	return B_OK;
}


// #pragma mark - stack helpers


static addr_t
arch_randomize_stack_pointer(addr_t value)
{
	static_assert(MAX_RANDOM_VALUE >= B_PAGE_SIZE - 1,
		"randomization range too large");

	value -= random_value() & (B_PAGE_SIZE - 1);
	value = (value & ~addr_t(X86_64_STACK_ALIGNMENT - 1)) - 8;

	ASSERT(is_stack_aligned(value));
	return value;
}


static uint8*
get_signal_stack(Thread* thread, iframe* frame, struct sigaction* action,
	size_t spaceNeeded)
{
	ASSERT(thread != NULL);
	ASSERT(frame != NULL);
	ASSERT(action != NULL);
	ASSERT(IFRAME_IS_USER(frame));

	if (spaceNeeded > MAX_SIGNAL_FRAME_SIZE) {
		dump_thread_context(thread, frame);
		panic("get_signal_stack: excessive frame size %zu", spaceNeeded);
		return NULL;
	}

	addr_t stackTop;
	addr_t stackBase;

	bool useSignalStack = thread->signal_stack_enabled
		&& (action->sa_flags & SA_ONSTACK) != 0
		&& (frame->user_sp < thread->signal_stack_base
			|| frame->user_sp >= thread->signal_stack_base
				+ thread->signal_stack_size);

	if (useSignalStack) {
		stackTop = thread->signal_stack_base + thread->signal_stack_size;
		stackBase = thread->signal_stack_base;
	} else {
		stackTop = frame->user_sp;
		stackBase = thread->user_stack_base;
	}

	if (stackTop < stackBase + spaceNeeded + X86_64_RED_ZONE_SIZE) {
		dump_thread_context(thread, frame);
		panic("get_signal_stack: insufficient stack space "
			"(need %zu, have %zu)",
			spaceNeeded + X86_64_RED_ZONE_SIZE, stackTop - stackBase);
		return NULL;
	}

	addr_t result = ((stackTop - X86_64_RED_ZONE_SIZE - spaceNeeded)
		& ~addr_t(X86_64_STACK_ALIGNMENT - 1)) - 8;

	if (!is_user_address_valid(result)
		|| !is_user_range_valid(result, spaceNeeded)) {
		dump_thread_context(thread, frame);
		panic("get_signal_stack: result address invalid: %#lx", result);
		return NULL;
	}

	ASSERT(is_stack_aligned(result));
	return (uint8*)result;
}


// #pragma mark - generic syscall


static status_t
arch_thread_control(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	if (subsystem == NULL)
		return B_BAD_VALUE;

	switch (function) {
		case THREAD_SET_GS_BASE:
		{
			if (bufferSize != sizeof(uint64))
				return B_BAD_VALUE;

			if (!is_user_address_valid((addr_t)buffer))
				return B_BAD_ADDRESS;

			uint64 base;
			if (user_memcpy(&base, buffer, sizeof(base)) < B_OK)
				return B_BAD_ADDRESS;

			Thread* thread = thread_get_current_thread();
			ASSERT(thread != NULL);

			thread->arch_info.user_gs_base = base;
			x86_write_msr(IA32_MSR_KERNEL_GS_BASE, base);
			return B_OK;
		}
	}

	return B_BAD_HANDLER;
}


// #pragma mark - public API


status_t
arch_thread_init(kernel_args* args)
{
	ASSERT(args != NULL);
	ASSERT(gFPUSaveLength > 0
		&& gFPUSaveLength <= sizeof(sInitialState.user_fpu_state));

	if (gHasXsave || gHasXsavec) {
		if (gHasXsavec) {
			asm volatile (
				"clts;"
				"fninit;"
				"fnclex;"
				"movl $0x7,%%eax;"
				"movl $0x0,%%edx;"
				"xsavec64 %0"
				:: "m" (sInitialState.user_fpu_state)
				: "rax", "rdx");
		} else {
			asm volatile (
				"clts;"
				"fninit;"
				"fnclex;"
				"movl $0x7,%%eax;"
				"movl $0x0,%%edx;"
				"xsave64 %0"
				:: "m" (sInitialState.user_fpu_state)
				: "rax", "rdx");
		}
	} else {
		asm volatile (
			"clts;"
			"fninit;"
			"fnclex;"
			"fxsaveq %0"
			:: "m" (sInitialState.user_fpu_state)
			: "memory");
	}

	savefpu* initialState = ((savefpu*)&sInitialState.user_fpu_state);
	initialState->fp_fxsave.mxcsr = 0x1F80;
	memset(initialState->fp_fxsave.fp, 0, sizeof(initialState->fp_fxsave.fp));
	memset(initialState->fp_fxsave.xmm, 0,
		sizeof(initialState->fp_fxsave.xmm));
	memset(initialState->fp_ymm, 0, sizeof(initialState->fp_ymm));

	if (!validate_fpu_state(initialState)) {
		panic("arch_thread_init: initial FPU state validation failed");
		return B_ERROR;
	}

	register_generic_syscall(THREAD_SYSCALLS, arch_thread_control, 1, 0);
	return B_OK;
}


status_t
arch_thread_init_thread_struct(Thread* thread)
{
	ASSERT(thread != NULL);
	ASSERT(gFPUSaveLength <= sizeof(arch_thread));

	memcpy(&thread->arch_info, &sInitialState, sizeof(arch_thread));
	thread->arch_info.thread = thread;
	thread->arch_info.signal_delivery_depth = 0;

	return B_OK;
}


void
arch_thread_init_kthread_stack(Thread* thread, void* _stack, void* _stackTop,
	void (*function)(void*), const void* data)
{
	ASSERT(thread != NULL);
	ASSERT(_stack != NULL);
	ASSERT(_stackTop != NULL);
	ASSERT(function != NULL);
	ASSERT((addr_t)_stackTop > (addr_t)_stack);

	uintptr_t* stackTop = static_cast<uintptr_t*>(_stackTop);

	TRACE("arch_thread_init_kthread_stack: stack top %p, function %p, data: "
		"%p\n", _stackTop, function, data);

	thread->arch_info.syscall_rsp = (uint64*)thread->kernel_stack_top;
	ASSERT(thread->arch_info.syscall_rsp != NULL);

	thread->arch_info.instruction_pointer
		= reinterpret_cast<uintptr_t>(x86_64_thread_entry);

	*--stackTop = uintptr_t(data);
	*--stackTop = uintptr_t(function);
	*--stackTop = uintptr_t(thread);

	thread->arch_info.current_stack = stackTop;
}


void
arch_thread_dump_info(void* info)
{
	if (info == NULL) {
		kprintf("\tNULL thread info\n");
		return;
	}

	arch_thread* thread = (arch_thread*)info;

	kprintf("\trsp: %p\n", thread->current_stack);
	kprintf("\tsyscall_rsp: %p\n", thread->syscall_rsp);
	kprintf("\tuser_rsp: %p\n", thread->user_rsp);
	kprintf("\tuser_fpu_state at %p\n", thread->user_fpu_state);
	kprintf("\tsignal_delivery_depth: %u\n", thread->signal_delivery_depth);
}


void
x86_restart_syscall(iframe* frame)
{
	ASSERT(frame != NULL);
	ASSERT(IFRAME_IS_USER(frame));

	Thread* thread = thread_get_current_thread();
	ASSERT(thread != NULL);

	atomic_and((int32*)&thread->flags, ~THREAD_FLAGS_RESTART_SYSCALL);
	atomic_or((int32*)&thread->flags, THREAD_FLAGS_SYSCALL_RESTARTED);

	frame->ax = frame->orig_rax;

	if (frame->ip < 2) {
		dump_thread_context(thread, frame);
		panic("x86_restart_syscall: invalid ip %#lx", frame->ip);
		return;
	}
	frame->ip -= 2;

	TSYSCALL(RestartSyscall());
}


void
x86_set_tls_context(Thread* thread)
{
	ASSERT(thread != NULL);

	x86_write_msr(IA32_MSR_FS_BASE, thread->user_local_storage);
	x86_write_msr(IA32_MSR_KERNEL_GS_BASE, thread->arch_info.user_gs_base);
}


status_t
arch_thread_enter_userspace(Thread* thread, addr_t entry, void* args1,
	void* args2)
{
	ASSERT(thread != NULL);
	ASSERT(thread->team != NULL);

	if (!is_user_address_valid(entry)) {
		dump_thread_context(thread, NULL);
		panic("arch_thread_enter_userspace: invalid entry point %#lx", entry);
		return B_BAD_ADDRESS;
	}

	addr_t stackTop = thread->user_stack_base + thread->user_stack_size;

	if (!is_user_address_valid(stackTop)) {
		dump_thread_context(thread, NULL);
		panic("arch_thread_enter_userspace: invalid stack %#lx", stackTop);
		return B_BAD_ADDRESS;
	}

	TRACE("arch_thread_enter_userspace: entry %#lx, args %p %p, "
		"stackTop %#lx\n", entry, args1, args2, stackTop);

	addr_t codeAddr;
	stackTop = arch_randomize_stack_pointer(stackTop - sizeof(codeAddr));

	if (!is_user_range_valid(stackTop, sizeof(codeAddr))) {
		dump_thread_context(thread, NULL);
		panic("arch_thread_enter_userspace: invalid stack after randomization");
		return B_BAD_ADDRESS;
	}

	if (lookup_commpage_entry(thread, COMMPAGE_ENTRY_X86_THREAD_EXIT,
			&codeAddr) != B_OK) {
		dump_thread_context(thread, NULL);
		panic("arch_thread_enter_userspace: invalid commpage "
			"or thread exit stub");
		return B_BAD_ADDRESS;
	}

	if (user_memcpy((void*)stackTop, (const void*)&codeAddr,
			sizeof(codeAddr)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	iframe frame = {};
	frame.type = IFRAME_TYPE_SYSCALL;
	frame.si = (uint64)args2;
	frame.di = (uint64)args1;
	frame.ip = entry;
	frame.cs = USER_CODE_SELECTOR;
	frame.flags = X86_EFLAGS_RESERVED1 | X86_EFLAGS_INTERRUPT;
	frame.sp = stackTop;
	frame.ss = USER_DATA_SELECTOR;

	x86_initial_return_to_userland(thread, &frame);

	return B_OK;
}


status_t
arch_setup_signal_frame(Thread* thread, struct sigaction* action,
	struct signal_frame_data* signalFrameData)
{
	ASSERT(thread != NULL);
	ASSERT(action != NULL);
	ASSERT(signalFrameData != NULL);

	validate_kernel_stack(thread);

	// Increment signal depth; the guard will roll it back on any error path.
	uint32 depth = atomic_add(
		(int32*)&thread->arch_info.signal_delivery_depth, 1);
	SignalDepthGuard depthGuard(thread);

	if (depth >= MAX_NESTED_SIGNALS) {
		dump_thread_context(thread, NULL);
		panic("signal storm detected: %u nested signals in thread %d",
			depth + 1, thread->id);
		return B_NOT_ALLOWED;
	}

	iframe* frame = x86_get_current_iframe();
	if (frame == NULL) {
		panic("arch_setup_signal_frame: no iframe");
		return B_BAD_VALUE;
	}

	if (!IFRAME_IS_USER(frame)) {
		dump_thread_context(thread, frame);
		panic("arch_setup_signal_frame: not user iframe, type %lu",
			(uint64)frame->type);
		return B_BAD_VALUE;
	}

	if (!is_user_address_valid(frame->user_sp)) {
		dump_thread_context(thread, frame);
		panic("arch_setup_signal_frame: invalid user sp %#lx",
			frame->user_sp);
		return B_BAD_ADDRESS;
	}

	// Save general-purpose registers.
	save_iframe_to_mcontext(frame, signalFrameData);

	// Save FPU state and exception metadata.
	status_t fpuStatus = save_fpu_to_signal_context(frame, signalFrameData);
	if (fpuStatus != B_OK) {
		dump_thread_context(thread, frame);
		panic("arch_setup_signal_frame: corrupted FPU state in iframe");
		return fpuStatus;
	}

	signal_get_user_stack(frame->user_sp,
		&signalFrameData->context.uc_stack);
	signalFrameData->syscall_restart_return_value = frame->orig_rax;

	// Allocate space on the user signal stack.
	size_t frameSize = sizeof(*signalFrameData) + sizeof(frame->ip);
	uint8* userStack = get_signal_stack(thread, frame, action, frameSize);
	if (userStack == NULL)
		return B_NO_MEMORY;

	ASSERT(is_stack_aligned((addr_t)userStack));

	signal_frame_data* userSignalFrameData
		= (signal_frame_data*)(userStack + sizeof(frame->ip));

	// Copy signal frame data and return address to user stack.
	if (user_memcpy(userSignalFrameData, signalFrameData,
			sizeof(*signalFrameData)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	if (user_memcpy(userStack, &frame->ip, sizeof(frame->ip)) != B_OK)
		return B_BAD_ADDRESS;

	thread->user_signal_context = &userSignalFrameData->context;

	// Resolve the signal handler entry point from the commpage.
	addr_t handlerAddress;
	if (lookup_commpage_entry(thread, COMMPAGE_ENTRY_X86_SIGNAL_HANDLER,
			&handlerAddress) != B_OK) {
		dump_thread_context(thread, frame);
		panic("arch_setup_signal_frame: invalid commpage or handler address");
		return B_BAD_ADDRESS;
	}

	// Redirect execution to the signal handler.
	frame->user_sp = (addr_t)userStack;
	frame->ip = handlerAddress;
	frame->di = (addr_t)userSignalFrameData;
	frame->flags &= ~(uint64)(X86_EFLAGS_TRAP | X86_EFLAGS_DIRECTION);

	ASSERT(is_stack_aligned(frame->user_sp));

	depthGuard.Commit();
	return B_OK;
}


int64
arch_restore_signal_frame(struct signal_frame_data* signalFrameData)
{
	ASSERT(signalFrameData != NULL);

	Thread* thread = thread_get_current_thread();
	ASSERT(thread != NULL);

	if (thread->arch_info.signal_delivery_depth == 0) {
		dump_thread_context(thread, NULL);
		panic("arch_restore_signal_frame: depth underflow in thread %d",
			thread->id);
		return B_BAD_VALUE;
	}

	atomic_add((int32*)&thread->arch_info.signal_delivery_depth, -1);

	iframe* frame = x86_get_current_iframe();
	if (frame == NULL) {
		panic("arch_restore_signal_frame: no iframe");
		return B_BAD_VALUE;
	}

	if (!IFRAME_IS_USER(frame)) {
		dump_thread_context(thread, frame);
		panic("arch_restore_signal_frame: not user iframe");
		return B_BAD_VALUE;
	}

	if (!is_user_address_valid(signalFrameData->context.uc_mcontext.rip)) {
		dump_thread_context(thread, frame);
		panic("arch_restore_signal_frame: invalid return ip %#lx",
			signalFrameData->context.uc_mcontext.rip);
		return B_BAD_ADDRESS;
	}

	if (!is_user_address_valid(signalFrameData->context.uc_mcontext.rsp)) {
		dump_thread_context(thread, frame);
		panic("arch_restore_signal_frame: invalid return sp %#lx",
			signalFrameData->context.uc_mcontext.rsp);
		return B_BAD_ADDRESS;
	}

	if (!validate_fpu_state(
			(const savefpu*)&signalFrameData->context.uc_mcontext.fpu)) {
		dump_thread_context(thread, frame);
		panic("arch_restore_signal_frame: corrupted FPU state from userspace, "
			"thread %d", thread->id);
		return B_BAD_DATA;
	}

	// Restore general-purpose registers and flags.
	frame->orig_rax = signalFrameData->syscall_restart_return_value;
	restore_mcontext_to_iframe(frame, signalFrameData);

	// Restore segment registers from the FPU metadata area.
	frame->cs = signalFrameData->context.uc_mcontext.fpu.fp_fxsave.cs;
	frame->ss = signalFrameData->context.uc_mcontext.fpu.fp_fxsave.ss;

	// Restore FPU state.
	ASSERT(gFPUSaveLength > 0
		&& gFPUSaveLength <= sizeof(thread->arch_info.user_fpu_state));

	memcpy(thread->arch_info.user_fpu_state,
		(void*)&signalFrameData->context.uc_mcontext.fpu, gFPUSaveLength);
	frame->fpu = &thread->arch_info.user_fpu_state;

	return frame->ax;
}
