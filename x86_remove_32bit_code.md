# Удаление 32-битного кода из x86 ядра Haiku

Цель: оставить только x86-64, сохранив 32-битный BIOS-загрузчик для запуска 64-битного ядра.
Все правки касаются кода самого ядра, не загрузчика.

---

## 1. syscalls_compat.cpp — УДАЛИТЬ ЦЕЛИКОМ

**Файл:** `src/system/kernel/arch/x86/syscalls_compat.cpp`

Весь файл — таблица 32-битных системных вызовов для `_COMPAT_MODE`. Без поддержки 32-битных приложений не нужен.

---

## 2. arch_thread.cpp

**Файл:** `src/system/kernel/arch/x86/arch_thread.cpp`

### 2.1 Удалить extern FPU swap

```cpp
// from arch_cpu.cpp
#ifndef __x86_64__
extern void (*gX86SwapFPUFunc)(void *oldState, const void *newState);
#endif
```

### 2.2 Удалить вызов FPU swap в `arch_thread_context_switch()`

```cpp
#ifndef __x86_64__
	gX86SwapFPUFunc(from->arch_info.fpu_state, to->arch_info.fpu_state);
#endif
```

---

## 3. arch_user_debugger.cpp

**Файл:** `src/system/kernel/arch/x86/arch_user_debugger.cpp`

### 3.1 Удалить 32-битный extern `gHasSSE`

Из блока:
```cpp
#ifdef __x86_64__
extern bool gHasXsave;
#else
extern bool gHasSSE;
#endif
```

Удалить строки (оставив только `extern bool gHasXsave;`):
```cpp
#ifdef __x86_64__
```
```cpp
#else
extern bool gHasSSE;
#endif
```

### 3.2 Удалить 32-битные `get_iframe_registers()` и `set_iframe_registers()`

Весь блок между `#else // __x86_64__` и `#endif // __x86_64__` после 64-битных версий этих функций:

```cpp
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
```

Также удалить `#ifdef __x86_64__` перед 64-битными версиями этих функций (оставив сами 64-битные функции без условной компиляции).

### 3.3 Удалить 32-битную ветку в `get_cpu_state()`

```cpp
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
```

Также удалить окружающий `#ifdef __x86_64__` и оставить только 64-битный код без условной компиляции.

### 3.4 Удалить 32-битную ветку в `arch_set_debug_cpu_state()`

```cpp
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
```

Также удалить окружающий `#ifdef __x86_64__` и оставить только 64-битный код без условной компиляции.

---

## 4. arch_vm_translation_map.cpp

**Файл:** `src/system/kernel/arch/x86/arch_vm_translation_map.cpp`

### 4.1 Удалить 32-битные инклуды

```cpp
#else
#	include "paging/32bit/X86PagingMethod32Bit.h"
#	include "paging/pae/X86PagingMethodPAE.h"
#endif
```

Также удалить `#ifdef __x86_64__` перед инклудом 64-битного заголовка, оставив просто:
```
#include "paging/64bit/X86PagingMethod64Bit.h"
```

### 4.2 Удалить 32-битные поля в `sPagingMethodBuffer`

```cpp
	#else
	char	thirty_two[sizeof(X86PagingMethod32Bit)];
	#if B_HAIKU_PHYSICAL_BITS == 64
	char	pae[sizeof(X86PagingMethodPAE)];
	#endif
	#endif
```

Также удалить `#ifdef __x86_64__` перед `sixty_four`, оставив:
```cpp
static union {
	uint64	align;
	char	sixty_four[sizeof(X86PagingMethod64Bit)];
} sPagingMethodBuffer;
```

### 4.3 Удалить функцию `is_pae_needed()` целиком

```cpp
static bool
is_pae_needed(kernel_args* args)
{
	// PAE required if NX bit is available (security feature)
	if (x86_check_feature(IA32_FEATURE_AMD_EXT_NX, FEATURE_EXT_AMD))
		return true;

	// PAE required if any physical memory above 4GB
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t end = args->physical_memory_range[i].start
		+ args->physical_memory_range[i].size;
		if (end > 0x100000000ULL)
			return true;
	}

	return false;
}
```

### 4.4 Удалить функцию `init_32bit_paging_method()` целиком

```cpp
static status_t
init_32bit_paging_method(kernel_args* args)
{
	bool paeAvailable = x86_check_feature(IA32_FEATURE_PAE, FEATURE_COMMON);
	bool paeNeeded = is_pae_needed(args);
	bool paeDisabled = get_safemode_boolean_early(args,
												  B_SAFEMODE_4_GB_MEMORY_LIMIT, false);

	bool usePAE = paeAvailable && paeNeeded && !paeDisabled;

	#if B_HAIKU_PHYSICAL_BITS == 64
	if (usePAE) {
		// Verify PAE actually works by checking CR4
		uint64 cr4 = x86_read_cr4();
		if ((cr4 & IA32_CR4_PAE) != 0) {
			// Already enabled, verify it reads back
			uint64 testCr4 = x86_read_cr4();
			if ((testCr4 & IA32_CR4_PAE) == 0) {
				dprintf("WARNING: PAE enabled but CR4.PAE reads back as 0\n");
				usePAE = false;
			}
		}
	}

	if (usePAE) {
		dprintf("using PAE paging\n");
		gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethodPAE;
	} else {
		if (paeNeeded && !paeAvailable) {
			dprintf("ERROR: PAE needed but not available\n");
			return B_NOT_SUPPORTED;
		}
		if (paeNeeded && paeDisabled) {
			dprintf("WARNING: PAE needed but disabled via safemode\n");
		}
		dprintf("using 32-bit paging\n");
		gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethod32Bit;
	}
	#else
	dprintf("using 32-bit paging\n");
	gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethod32Bit;
	#endif

	if (gX86PagingMethod == NULL || *(void**)gX86PagingMethod == NULL) {
		panic("failed to construct paging method");
		return B_NO_MEMORY;
	}

	return B_OK;
}
```

### 4.5 Удалить 32-битную ветку в `arch_vm_translation_map_init()`

```cpp
	#else
	status = init_32bit_paging_method(args);
	#endif
```

Также удалить `#ifdef __x86_64__` перед вызовом `init_64bit_paging_method`.

---

## 5. arch_vm.cpp

**Файл:** `src/system/kernel/arch/x86/arch_vm.cpp`

### 5.1 Удалить инклуд BIOS

```cpp
#include <arch/x86/bios.h>
```

### 5.2 Удалить вызов `bios_init()` в `arch_vm_init_post_area()`

Заменить блок:
```cpp
#ifndef __x86_64__
	return bios_init();
#else
	return B_OK;
#endif
```

Удалить строки:
```cpp
#ifndef __x86_64__
	return bios_init();
#else
```
и
```cpp
#endif
```

Оставив только `return B_OK;`

---

## 6. asm_offsets.cpp

**Файл:** `src/system/kernel/arch/x86/asm_offsets.cpp`

### 6.1 Удалить 32-битный offset `orig_eax`

```cpp
	#else
	// x86 (32-bit) specific saved registers
	DEFINE_OFFSET_MACRO(IFRAME, iframe, orig_eax);
	#endif
```

Также удалить окружающий `#ifdef __x86_64__` и его `#endif`, оставив 64-битные макросы без условной компиляции.

### 6.2 Удалить `#ifdef __x86_64__` вокруг 64-битных блоков

Следующие блоки оставить, но убрать условные обёртки:

```cpp
	#ifdef __x86_64__
	// x86_64 requires 16-byte stack alignment before call instructions
	// TODO: ...
	#endif
```

```cpp
	#ifdef __x86_64__
	// x86_64-specific thread state
	DEFINE_MACRO(THREAD_user_fpu_state, offsetof(Thread, arch_info.user_fpu_state));
	// ...
	#endif
```

```cpp
	#ifdef __x86_64__
	// Additional x86_64 registers
	DEFINE_OFFSET_MACRO(IFRAME, iframe, r8);
	// ...
	#endif
```

Строки `#ifdef __x86_64__` и соответствующие `#endif` удалить, содержимое оставить.

---

## 7. arch_system_info.cpp

**Файл:** `src/system/kernel/arch/x86/arch_system_info.cpp`

### 7.1 Удалить 32-битную платформу в `arch_fill_topology_node()`

```cpp
			#if __i386__
			node->data.root.platform = B_CPU_x86;
			#elif __x86_64__
```
и
```cpp
			#else
			node->data.root.platform = B_CPU_UNKNOWN;
			#endif
```

Оставив только `node->data.root.platform = B_CPU_x86_64;`

---

## 8. x86_syscalls.h

**Файл:** `src/system/kernel/arch/x86/x86_syscalls.h`

### 8.1 Удалить объявление compat-функции

```cpp
#if defined(__x86_64__) && defined(_COMPAT_MODE)
void	x86_compat_initialize_syscall();
#endif
```

### 8.2 Удалить `gX86SetSyscallStack` и тело `x86_set_syscall_stack()`

```cpp
extern void (*gX86SetSyscallStack)(addr_t stackTop);


static inline void
x86_set_syscall_stack(addr_t stackTop)
{
#if !defined(__x86_64__) || defined(_COMPAT_MODE)
	// TODO on x86_64, only necessary for 32-bit threads
	if (gX86SetSyscallStack != NULL)
		gX86SetSyscallStack(stackTop);
#endif
}
```

Заменить на пустую inline-функцию:
```cpp
static inline void
x86_set_syscall_stack(addr_t stackTop)
{
}
```

---

## 9. x86_signals.h

**Файл:** `src/system/kernel/arch/x86/x86_signals.h`

### 9.1 Удалить 32-битный signal wrapper

```cpp
#ifndef __x86_64__
addr_t	x86_get_user_signal_handler_wrapper(bool beosHandler,
	void* commPageAddress);
#endif
```

---

## Сводка

| Файл | Действие |
|------|----------|
| `syscalls_compat.cpp` | Удалить целиком |
| `arch_thread.cpp` | Удалить FPU swap (2 блока) |
| `arch_user_debugger.cpp` | Удалить 32-bit register functions, FPU ветки, gHasSSE |
| `arch_vm_translation_map.cpp` | Удалить PAE/32bit paging (инклуды, 2 функции, ветки) |
| `arch_vm.cpp` | Удалить bios.h инклуд и bios_init() вызов |
| `asm_offsets.cpp` | Удалить orig_eax, снять ifdef-обёртки |
| `arch_system_info.cpp` | Удалить i386 платформу |
| `x86_syscalls.h` | Удалить compat syscall, gX86SetSyscallStack |
| `x86_signals.h` | Удалить 32-bit signal wrapper |

После этих правок все оставшиеся `#ifdef __x86_64__` / `#ifndef __x86_64__` в этих файлах можно убрать, оставив только 64-битный код.

---

## 10. arch_cpu.cpp

**Файл:** `src/system/kernel/arch/x86/arch_cpu.cpp`

### 10.1 Удалить определения gX86SwapFPUFunc и gHasSSE

Строки 101-104:
```cpp
#ifndef __x86_64__
void (*gX86SwapFPUFunc)(void* oldState, const void* newState) = x86_noop_swap;
bool gHasSSE = false;
#endif
```

### 10.2 Удалить 32-битный код в функции инициализации FPU

В функции (около строк 350-375), удалить все ветки, использующие `gX86SwapFPUFunc` и `gHasSSE`:

```cpp
		// No FPU... time to install one in your 386?
		dprintf("%s: Warning: CPU has no reported FPU.\n", __func__);
		gX86SwapFPUFunc = x86_noop_swap;
		return;
	}

	if (!x86_check_feature(IA32_FEATURE_SSE, FEATURE_COMMON)
		|| !x86_check_feature(IA32_FEATURE_FXSR, FEATURE_COMMON)) {
		// we don't have proper SSE support, just enable FPU
		x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));
		gX86SwapFPUFunc = x86_fnsave_swap;
		return;
	}
#endif
	...
	gX86SwapFPUFunc = x86_fxsave_swap;
	gHasSSE = true;
#endif
```

Удалить все строки с `gX86SwapFPUFunc` и `gHasSSE`, оставив только 64-битный код.

---

## 11. arch_cpu.h

**Файл:** `headers/private/kernel/arch/x86/arch_cpu.h`

### 11.1 Удалить 32-битные поля в `arch_cpu_info`

Строки 580-583:
```cpp
#ifndef __x86_64__
	struct tss			double_fault_tss;
	void*				kernel_tls;
#endif
```

### 11.2 Удалить 32-битные объявления функций

Строки 699-716:
```cpp
#ifndef __x86_64__

void x86_swap_pgdir(addr_t newPageDir);

void x86_context_switch(struct arch_thread* oldState,
	struct arch_thread* newState);

void x86_fnsave(void* fpuState);
void x86_frstor(const void* fpuState);

void x86_fxsave(void* fpuState);
void x86_fxrstor(const void* fpuState);

void x86_noop_swap(void* oldFpuState, const void* newFpuState);
void x86_fnsave_swap(void* oldFpuState, const void* newFpuState);
void x86_fxsave_swap(void* oldFpuState, const void* newFpuState);

#endif
```

---

## 12. 64/syscalls.cpp — Удалить _COMPAT_MODE код

**Файл:** `src/system/kernel/arch/x86/64/syscalls.cpp`

### 12.1 Удалить compat инклуды и extern

Строки 12-47:
```cpp
#ifdef _COMPAT_MODE
#	include <commpage_compat.h>
#endif
...
#ifdef _COMPAT_MODE
#	include <elf.h>
#endif
...
#ifdef _COMPAT_MODE

// SYSCALL/SYSENTER handlers (in entry_compat.S).
extern "C" {
	void x86_64_syscall32_entry(void);
	void x86_64_sysenter32_entry(void);
}


void (*gX86SetSyscallStack)(addr_t stackTop) = NULL;


// user syscall assembly stubs
extern "C" void x86_user_syscall_sysenter(void);
extern unsigned int x86_user_syscall_sysenter_end;
extern "C" void x86_user_syscall_syscall(void);
extern unsigned int x86_user_syscall_syscall_end;

extern "C" void x86_sysenter32_userspace_thread_exit(void);
extern unsigned int x86_sysenter32_userspace_thread_exit_end;

#endif // _COMPAT_MODE
```

### 12.2 Удалить compat код в `init_syscall_registers()`

Строки 66-69:
```cpp
#ifdef _COMPAT_MODE
	// Syscall compat entry point address.
	x86_write_msr(IA32_MSR_CSTAR, (addr_t)x86_64_syscall32_entry);
#endif
```

### 12.3 Удалить функцию `x86_compat_initialize_syscall()` целиком

Строки 100-151:
```cpp
#ifdef _COMPAT_MODE

static void
set_intel_syscall_stack(addr_t stackTop)
{
	x86_write_msr(IA32_MSR_SYSENTER_ESP, stackTop);
}


static void
init_intel_syscall_registers(void* dummy, int cpuNum)
{
	x86_write_msr(IA32_MSR_SYSENTER_CS, KERNEL_CODE_SELECTOR);
	x86_write_msr(IA32_MSR_SYSENTER_ESP, 0);
	x86_write_msr(IA32_MSR_SYSENTER_EIP, (addr_t)x86_64_sysenter32_entry);

	gX86SetSyscallStack = &set_intel_syscall_stack;
}


void
x86_compat_initialize_syscall(void)
{
	// for 32-bit syscalls, fill the commpage with the right mechanism
	call_all_cpus_sync(&init_intel_syscall_registers, NULL);

	void* syscallCode = (void *)&x86_user_syscall_sysenter;
	void* syscallCodeEnd = &x86_user_syscall_sysenter_end;

	// TODO check AMD for sysenter

	// fill in the table entry
	size_t len = (size_t)((addr_t)syscallCodeEnd - (addr_t)syscallCode);
	addr_t position = fill_commpage_compat_entry(COMMPAGE_ENTRY_X86_SYSCALL,
		syscallCode, len);

	image_id image = get_commpage_compat_image();
	elf_add_memory_image_symbol(image, "commpage_compat_syscall", position,
		len, B_SYMBOL_TYPE_TEXT);

	void* threadExitCode = (void *)&x86_sysenter32_userspace_thread_exit;
	void* threadExitCodeEnd = &x86_sysenter32_userspace_thread_exit_end;

	len = (size_t)((addr_t)threadExitCodeEnd - (addr_t)threadExitCode);
	position = fill_commpage_compat_entry(COMMPAGE_ENTRY_X86_THREAD_EXIT,
		threadExitCode, len);

	elf_add_memory_image_symbol(image, "commpage_compat_thread_exit",
		position, len, B_SYMBOL_TYPE_TEXT);
}

#endif // _COMPAT_MODE
```

---

## 13. Файлы для ПОЛНОГО УДАЛЕНИЯ (_COMPAT_MODE)

> **Примечание:** Эти файлы компилируются только при `_COMPAT_MODE` (когда x86 — вторичная архитектура).
> Сейчас они не включены в Jamfile и не компилируются, но должны быть удалены для чистоты кодовой базы.

Следующие файлы предназначены исключительно для поддержки 32-битных приложений на 64-битном ядре и должны быть удалены целиком:

| Файл | Назначение |
|------|------------|
| `src/system/kernel/arch/x86/syscalls_compat.cpp` | 32-битная таблица syscalls |
| `src/system/kernel/arch/x86/arch_commpage_compat.cpp` | Compat commpage инициализация |
| `src/system/kernel/arch/x86/arch_elf_compat.cpp` | ELF32 загрузчик для compat |
| `src/system/kernel/arch/x86/64/entry_compat.S` | 32-битные точки входа syscall/sysenter |
| `src/system/kernel/arch/x86/64/signals_compat.cpp` | Compat signal handler |
| `src/system/kernel/arch/x86/64/signals_compat_asm.S` | Compat signal asm код |

---

## 14. arch_elf.cpp — НЕ ТРОГАТЬ!

**Файл:** `src/system/kernel/arch/x86/arch_elf.cpp`

> ⚠️ **ВАЖНО:** Этот файл используется И ядром, И BIOS загрузчиком!
>
> BIOS загрузчик компилирует этот файл с `BOOT_SUPPORT_ELF32` и `BOOT_SUPPORT_ELF64`
> (см. `src/system/boot/arch/x86/Jamfile:21-22`).
>
> ELF32 код нужен загрузчику, поэтому **НЕ УДАЛЯТЬ** условия с `_BOOT_MODE && BOOT_SUPPORT_ELF32`.
>
> После удаления `arch_elf_compat.cpp` условие `ELF32_COMPAT` никогда не будет истинным для ядра,
> но ELF32 код останется доступным для загрузчика через `_BOOT_MODE`.

---

## Обновлённая сводка

| Файл | Действие |
|------|----------|
| `syscalls_compat.cpp` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `arch_commpage_compat.cpp` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `arch_elf_compat.cpp` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `64/entry_compat.S` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `64/signals_compat.cpp` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `64/signals_compat_asm.S` | **УДАЛИТЬ ЦЕЛИКОМ** |
| `arch_thread.cpp` | Удалить FPU swap (2 блока) |
| `arch_user_debugger.cpp` | Удалить 32-bit register functions, FPU ветки, gHasSSE |
| `arch_vm_translation_map.cpp` | Удалить PAE/32bit paging (инклуды, 2 функции, ветки) |
| `arch_vm.cpp` | Удалить bios.h инклуд и bios_init() вызов |
| `asm_offsets.cpp` | Удалить orig_eax, снять ifdef-обёртки |
| `arch_system_info.cpp` | Удалить i386 платформу |
| `x86_syscalls.h` | Удалить compat syscall, gX86SetSyscallStack |
| `x86_signals.h` | Удалить 32-bit signal wrapper |
| `arch_cpu.cpp` | Удалить gX86SwapFPUFunc, gHasSSE и их использование |
| `arch_cpu.h` | Удалить 32-bit поля в arch_cpu_info, объявления swap-функций |
| `64/syscalls.cpp` | Удалить весь _COMPAT_MODE код |
| `arch_elf.cpp` | **НЕ ТРОГАТЬ** (нужен BIOS загрузчику) |
