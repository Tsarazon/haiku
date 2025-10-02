# План удаления поддержки i386/x86 из Haiku OS

## Резюме и стратегия

**Последовательность действий:**
1. **Анализ** - идентификация shared кода, используемого x86_64
2. **Перенос** - миграция shared кода в arch/x86_64/
3. **Удаление** - удаление i386-специфичного кода

**Критические правила:**
- Не удалять код без проверки использования в x86_64
- Весь shared код должен быть явно перенесен
- Сохранить функциональность x86_64
- Не ломать Haiku API/ABI

---

## ФАЗА 1: АНАЛИЗ

### Анализ headers/private/kernel/arch/x86/

**Команда для анализа:**
```bash
find headers/private/kernel/arch/x86/ -type f -name "*.h" | while read f; do
    echo "=== $f ==="
    grep -n "#ifdef __x86_64__\|#ifndef __x86_64__\|#ifdef __i386__\|#ifndef __i386__" "$f"
done
```

#### `headers/private/kernel/arch/x86/arch_thread_types.h`

**Содержимое для анализа (строки 28-102):**
```cpp
// Строка 28-33: i386-only
#ifndef __x86_64__
struct farcall {
	uint32* esp;
	uint32* ss;
};
#endif

// Строки 38-56: MIXED - разные структуры
struct arch_thread {
#ifdef __x86_64__
	BKernel::Thread* thread;
	uint64*			syscall_rsp;
	uint64*			user_rsp;
	uintptr_t*		current_stack;
	uintptr_t		instruction_pointer;
	uint64			user_gs_base;
#else
	struct farcall	current_stack;
	struct farcall	interrupt_stack;
#endif

// Строки 57-67: MIXED - разные размеры FPU state
#ifndef __x86_64__
	uint8			fpu_state[512] _ALIGNED(16);
#else
	uint8			user_fpu_state[2560] _ALIGNED(64);
#endif
```

**Вывод:** Файл содержит ТОЛЬКО x86_64 shared структуры в условной компиляции. i386 части не используются x86_64.

#### `headers/private/kernel/arch/x86/arch_thread.h`

```cpp
// Строка 34-40: SHARED - функция одинакова
static inline Thread* arch_thread_get_current_thread(void)
{
	addr_t addr;
	__asm__("mov %%gs:0, %0" : "=r"(addr));
	return (Thread*)addr;
}

// Строки 43-68: РАЗНЫЕ реализации
#ifdef __x86_64__
static inline void arch_thread_set_current_thread(Thread* t) {
	t->arch_info.thread = t;
	x86_write_msr(IA32_MSR_GS_BASE, (addr_t)&t->arch_info);
}
#else
// override empty macro
#undef arch_syscall_64_bit_return_value
void arch_syscall_64_bit_return_value(void);

static inline void arch_thread_set_current_thread(Thread* t) {
	asm volatile("mov %0, %%gs:0" : : "r" (t) : "memory");
}
#endif
```

**Вывод:** `arch_thread_get_current_thread()` SHARED. `arch_thread_set_current_thread()` имеет разные реализации.

#### `headers/private/kernel/arch/x86/arch_cpu.h`

```cpp
// Строки ~100-110: MIXED
struct arch_cpu_info {
	// ... много общих полей ...
	struct tss			tss;
#ifndef __x86_64__
	struct tss			double_fault_tss;
	void*				kernel_tls;
#endif
} arch_cpu_info;

// Строки ~180: РАЗНЫЕ функции
#ifdef __x86_64__
void __x86_setup_system_time(uint64 conversionFactor, uint64 conversionFactorNsecs);
#else
void __x86_setup_system_time(uint32 conversionFactor, uint32 conversionFactorNsecs, bool conversionFactorNsecsShift);
#endif
```

**Вывод:** Структура `arch_cpu_info` SHARED с i386-специфичными полями. Функция `__x86_setup_system_time` имеет разные сигнатуры.

### Анализ src/system/kernel/arch/x86/

**Команда детального анализа:**
```bash
cd src/system/kernel/arch/x86/
find . -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.S" \) | while read f; do
    if grep -q "#ifdef __x86_64__\|#ifndef __x86_64__\|#ifdef __i386__\|#ifndef __i386__" "$f"; then
        echo "=== $f - MIXED CODE ==="
    else
        echo "=== $f - CHECK MANUALLY ==="
    fi
done
```

#### Подкаталог `arch/x86/32/`

**Необходимо проверить:**
```bash
ls -la src/system/kernel/arch/x86/32/
# Ожидаемые файлы:
# - descriptors.cpp
# - iframe.S  
# - paging/
```

**Анализ каждого файла:**

1. `src/system/kernel/arch/x86/32/descriptors.cpp`
```bash
grep -n "x86_64\|shared\|common" src/system/kernel/arch/x86/32/descriptors.cpp
```
Проверить: используется ли этот код в x86_64? Если функции вызываются из общего кода - это SHARED.

2. `src/system/kernel/arch/x86/32/iframe.S`
```bash
head -20 src/system/kernel/arch/x86/32/iframe.S
```
Проверить комментарии и экспортируемые символы. Если символы используются в x86_64 - SHARED.

#### Подкаталог `arch/x86/paging/`

```bash
ls -la src/system/kernel/arch/x86/paging/
# Структура:
# 32bit/ - проверить на shared
# 64bit/ - базовый код для x86_64
# paging.cpp - вероятно shared диспетчер
```

**Анализ `paging/paging.cpp`:**
```bash
grep -n "arch_vm_translation_map_create_for_kernel\|arch_vm_translation_map_create" src/system/kernel/arch/x86/paging/paging.cpp
```

Если файл содержит:
```cpp
arch_vm_translation_map* arch_vm_translation_map_create_for_kernel() {
#ifdef __x86_64__
    return x86_64_paging_create();
#else
    return x86_paging_create();
#endif
}
```
То это DISPATCHER - нужно сохранить логику x86_64 части.

#### Подкаталог `arch/x86/timers/`

```bash
ls src/system/kernel/arch/x86/timers/
# x86_hpet.cpp
# x86_pit.cpp  
# x86_tsc.cpp
```

**Анализ каждого:**
```bash
grep -c "__x86_64__\|__i386__" src/system/kernel/arch/x86/timers/*.cpp
```

Если счетчик == 0, файл SHARED без условной компиляции.
Если > 0, нужно проанализировать различия.

**Пример анализа `x86_tsc.cpp`:**
```bash
grep -A5 -B5 "__i386__" src/system/kernel/arch/x86/timers/x86_tsc.cpp
```

Типичный паттерн:
```cpp
#ifdef __i386__
    // rdtsc возвращает результат в EDX:EAX
    asm("rdtsc" : "=A" (result));
#else
    // на x86_64 нужно явно указать регистры
    uint32 lo, hi;
    asm("rdtsc" : "=a" (lo), "=d" (hi));
    result = ((uint64)hi << 32) | lo;
#endif
```

**Вывод:** Инструкция одна и та же, но синтаксис ассемблера разный из-за ABI. Код SHARED, нужен перенос с корректировкой.

### Анализ src/system/boot/

#### `src/system/boot/platform/bios_ia32/`

```bash
find src/system/boot/platform/bios_ia32/ -name "*.cpp" -o -name "*.c" -o -name "*.S"
```

**Проверка на x86_64 зависимости:**
```bash
grep -r "x86_64\|used.*by.*64" src/system/boot/platform/bios_ia32/
```

Если вывод пуст - каталог полностью i386-специфичный, можно удалять.

#### `src/system/boot/platform/efi/arch/x86/`

```bash
ls src/system/boot/platform/efi/arch/x86/
# Проверить файлы на MIXED код
```

Эти файлы могут обслуживать и 32-bit и 64-bit EFI. Проверить:
```bash
grep -n "#ifdef.*ARCH_X86_64\|#ifdef.*EFI64" src/system/boot/platform/efi/arch/x86/*.cpp
```

### Анализ headers/config/

#### `headers/config/HaikuConfig.h`

```cpp
// Строки 21-25: i386 определения
#if defined(__i386__)
#	define __HAIKU_ARCH					x86
#	define __HAIKU_ARCH_ABI				"x86"
#	define __HAIKU_ARCH_X86				1
#	define __HAIKU_ARCH_PHYSICAL_BITS	64

// Строки 26-30: x86_64 определения  
#elif defined(__x86_64__)
#	define __HAIKU_ARCH					x86_64
#	define __HAIKU_ARCH_ABI				"x86_64"
#	define __HAIKU_ARCH_X86_64			1
#	define __HAIKU_ARCH_BITS			64
```

**Проверка использования `__HAIKU_ARCH_X86`:**
```bash
git grep -n "__HAIKU_ARCH_X86[^_]" | wc -l
```

Все места использования должны быть проверены на возможность замены на `__HAIKU_ARCH_X86_64`.

#### `headers/config/types.h`

```cpp
// Строки 35-41: BEOS COMPATIBLE TYPES
#ifdef __HAIKU_BEOS_COMPATIBLE_TYPES
typedef signed long int		__haiku_int32;
typedef unsigned long int	__haiku_uint32;
#else
typedef __haiku_std_int32	__haiku_int32;
typedef __haiku_std_uint32	__haiku_uint32;
#endif
```

**Проверка зависимостей:**
```bash
git grep "__HAIKU_BEOS_COMPATIBLE_TYPES" | grep -v "HaikuConfig.h"
```

Определяется только для `__HAIKU_ARCH_X86` в user mode. Если удаляем i386, нужно решить судьбу BeOS compatibility.

### Результаты анализа (таблица)

| Путь | Тип кода | Действие на фазе 2 |
|------|----------|-------------------|
| `headers/private/kernel/arch/x86/arch_thread_types.h` | MIXED | Экстракт x86_64, перенос |
| `headers/private/kernel/arch/x86/arch_thread.h` | MIXED + SHARED функция | Экстракт x86_64, перенос shared |
| `headers/private/kernel/arch/x86/arch_cpu.h` | MIXED структура | Экстракт x86_64 части |
| `headers/private/kernel/arch/x86/32/` | **ПРОВЕРИТЬ** | Анализ каждого файла |
| `headers/private/kernel/arch/x86/64/` | x86_64 only | База для переноса |
| `src/system/kernel/arch/x86/timers/*.cpp` | SHARED + MIXED | Перенос с удалением i386 веток |
| `src/system/kernel/arch/x86/paging/paging.cpp` | DISPATCHER | Перенос только x86_64 dispatch |
| `src/system/kernel/arch/x86/paging/32bit/` | **ПРОВЕРИТЬ** | Может иметь shared helpers |
| `src/system/kernel/arch/x86/paging/64bit/` | x86_64 only | Прямой перенос |
| `src/system/boot/platform/bios_ia32/` | **ПРОВЕРИТЬ** | Если нет x86_64 deps - удалить |
| `headers/posix/arch/x86/` | i386 only | Удалить после переноса shared |

---

## ФАЗА 2: ПЕРЕНОС

### 2.1. Создание структуры arch/x86_64

Создать следующие директории, если их нет:
- `headers/private/kernel/arch/x86_64/`
- `src/system/kernel/arch/x86_64/timers/`
- `src/system/kernel/arch/x86_64/paging/`

### 2.2. Перенос headers

#### `headers/private/kernel/arch/x86/arch_thread_types.h` → `arch/x86_64/`

**Создать `headers/private/kernel/arch/x86_64/arch_thread_types.h`:**
```cpp
#ifndef _KERNEL_ARCH_X86_64_THREAD_TYPES_H
#define _KERNEL_ARCH_X86_64_THREAD_TYPES_H

#include <SupportDefs.h>
#include <arch/x86/64/iframe.h>

namespace BKernel {
    struct Thread;
}

#define _ALIGNED(bytes) __attribute__((aligned(bytes)))

// architecture specific thread info
struct arch_thread {
	// Back pointer to the containing Thread structure. The GS segment base is
	// pointed here, used to get the current thread.
	BKernel::Thread* thread;

	// RSP for kernel entry used by SYSCALL, and temporary scratch space.
	uint64*			syscall_rsp;
	uint64*			user_rsp;

	uintptr_t*		current_stack;
	uintptr_t		instruction_pointer;

	uint64			user_gs_base;

	// floating point save point - this must be 64 byte aligned for xsave and
	// have enough space for all the registers, at least 2560 bytes according
	// to Intel Architecture Instruction Set Extensions Programming Reference
	uint8			user_fpu_state[2560] _ALIGNED(64);

	addr_t GetFramePointer() const { return current_stack[0]; }
} _ALIGNED(16);

struct arch_team {
	char			dummy;
};

struct arch_fork_arg {
	struct iframe	iframe;
};

#endif	// _KERNEL_ARCH_X86_64_THREAD_TYPES_H
```

**Обновить `headers/private/kernel/arch/x86/arch_thread_types.h`:**
```cpp
// В начале файла добавить:
#ifdef __x86_64__
#  include <arch/x86_64/arch_thread_types.h>
#else
// ... оставить старый код для i386 ...
#endif
```

#### `headers/private/kernel/arch/x86/arch_thread.h` → экстракция x86_64

**Создать `headers/private/kernel/arch/x86_64/arch_thread.h`:**
```cpp
#ifndef _KERNEL_ARCH_X86_64_THREAD_H
#define _KERNEL_ARCH_X86_64_THREAD_H

#include <arch/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sigaction;

struct iframe* x86_get_user_iframe(void);
struct iframe* x86_get_current_iframe(void);
struct iframe* x86_get_thread_user_iframe(Thread* thread);

phys_addr_t x86_next_page_directory(Thread* from, Thread* to);
void x86_initial_return_to_userland(Thread* thread, struct iframe* iframe);

void x86_restart_syscall(struct iframe* frame);
void x86_set_tls_context(Thread* thread);

static inline Thread* arch_thread_get_current_thread(void)
{
	addr_t addr;
	__asm__("mov %%gs:0, %0" : "=r"(addr));
	return (Thread*)addr;
}

static inline void arch_thread_set_current_thread(Thread* t)
{
	// Point GS segment base at thread architecture data.
	t->arch_info.thread = t;
	x86_write_msr(IA32_MSR_GS_BASE, (addr_t)&t->arch_info);
}

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_X86_64_THREAD_H */
```

#### `headers/private/kernel/arch/x86/arch_cpu.h` → экстракция

**Создать `headers/private/kernel/arch/x86_64/arch_cpu.h`:**
```cpp
#ifndef _KERNEL_ARCH_X86_64_CPU_H
#define _KERNEL_ARCH_X86_64_CPU_H

#include <module.h>
#include <arch/x86/arch_altcodepatch.h>
#include <arch/x86/arch_cpuasm.h>
#include <arch/x86/descriptors.h>
#include <arch/x86/64/cpu.h>

// Скопировать все определения структур, но БЕЗ i386 полей:
struct arch_cpu_info {
	// ... все общие поля ...
	struct X86PagingStructures* active_paging_structures;
	size_t				dr6;
	size_t				dr7;
	
	// local TSS for this cpu
	struct tss			tss;
	// НЕ КОПИРОВАТЬ: double_fault_tss, kernel_tls
} arch_cpu_info;

// Функции - только x86_64 сигнатуры:
void __x86_setup_system_time(uint64 conversionFactor, uint64 conversionFactorNsecs);

// ... остальные функции ...

#endif /* _KERNEL_ARCH_X86_64_CPU_H */
```

### 2.3. Перенос timers

#### `src/system/kernel/arch/x86/timers/x86_tsc.cpp`

**Анализ различий:**
```bash
diff -u <(grep -A10 "rdtsc" src/system/kernel/arch/x86/timers/x86_tsc.cpp | grep -A10 "__i386__") \
        <(grep -A10 "rdtsc" src/system/kernel/arch/x86/timers/x86_tsc.cpp | grep -A10 "__x86_64__")
```

**Создать `src/system/kernel/arch/x86_64/timers/x86_tsc.cpp`:**

1. Скопировать файл целиком
2. Найти все `#ifdef __i386__` блоки
3. Удалить i386 ветки, оставить только x86_64
4. Убрать все `#ifdef __x86_64__` обертки

**Пример изменения:**
```cpp
// БЫЛО:
#ifdef __i386__
static inline uint64 rdtsc() {
    uint64 tsc;
    __asm__ __volatile__("rdtsc" : "=A" (tsc));
    return tsc;
}
#else
static inline uint64 rdtsc() {
    uint32 hi, lo;
    __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64)hi << 32) | lo;
}
#endif

// СТАЛО:
static inline uint64 rdtsc() {
    uint32 hi, lo;
    __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64)hi << 32) | lo;
}
```

**Повторить для:** `x86_hpet.cpp`, `x86_pit.cpp`

### 2.4. Перенос paging

#### `src/system/kernel/arch/x86/paging/64bit/*` → прямой перенос

Все файлы из `src/system/kernel/arch/x86/paging/64bit/` скопировать в `src/system/kernel/arch/x86_64/paging/`

Это x86_64-специфичный код, который можно копировать без изменений.

#### `src/system/kernel/arch/x86/paging/paging.cpp` → экстракция dispatcher

**Создать `src/system/kernel/arch/x86_64/paging/paging.cpp`:**

1. Скопировать файл
2. Найти все dispatch функции типа:
```cpp
arch_vm_translation_map* arch_vm_translation_map_create() {
#ifdef __x86_64__
    return x86_64_translation_map_create();
#else
    return x86_translation_map_create();
#endif
}
```

3. Оставить только x86_64 ветки:
```cpp
arch_vm_translation_map* arch_vm_translation_map_create() {
    return x86_64_translation_map_create();
}
```

### 2.5. Перенос boot loader EFI

#### Проверка `src/system/boot/platform/efi/arch/x86/`

```bash
cd src/system/boot/platform/efi/arch/x86/
for f in *.cpp; do
    echo "=== $f ==="
    grep -n "#ifdef.*__x86_64__\|#ifdef.*ARCH_X86_64" "$f"
done
```

**Если файлы содержат MIXED код:**

Создать `src/system/boot/platform/efi/arch/x86_64/` и переместить туда x86_64 части.

**Пример `arch_start.cpp`:**
```cpp
// БЫЛО:
void arch_start_kernel() {
#ifdef __i386__
    // Переход в long mode
    enable_long_mode();
    // Создание identity mapping
    setup_32bit_paging();
    jump_to_long_mode();
#endif
    
    // Прыжок в kernel
    kernel_entry(...);
}

// СТАЛО (в arch/x86_64/):
void arch_start_kernel() {
    // x86_64 EFI уже в long mode
    kernel_entry(...);
}
```

### 2.6. Обновление include paths

После переноса нужно обновить includes в других файлах.

**Найти все includes arch/x86:**
```bash
git grep -n '#include.*arch/x86/' | grep -v "arch/x86_64"
```

**Для каждого файла решить:**
- Если файл нужен только x86_64 → изменить на `#include <arch/x86_64/...>`
- Если файл еще поддерживает i386 → оставить условную компиляцию

---

## ФАЗА 3: УДАЛЕНИЕ

### 3.1. Удаление i386 headers

**После проверки что весь x86_64 код использует новые headers:**

```bash
# Удалить i386-only headers
rm headers/posix/arch/x86/signal.h
rm headers/posix/arch/x86/arch_setjmp.h
rm headers/posix/arch/x86/fenv.h

# Для MIXED headers - удалить i386 блоки
```

#### `headers/config/HaikuConfig.h` - удалить i386 блок

**Строки 21-25** удалить:
```cpp
#if defined(__i386__)
#	define __HAIKU_ARCH					x86
#	define __HAIKU_ARCH_ABI				"x86"
#	define __HAIKU_ARCH_X86				1
#	define __HAIKU_ARCH_PHYSICAL_BITS	64
#elif defined(__x86_64__)
```

**Изменить на:**
```cpp
#if defined(__x86_64__)
#	define __HAIKU_ARCH					x86_64
#	define __HAIKU_ARCH_ABI				"x86_64"
#	define __HAIKU_ARCH_X86_64			1
#	define __HAIKU_ARCH_BITS			64
#elif defined(__ARMEL__) || defined(__arm__)
```

#### `headers/os/kernel/debugger.h` - удалить i386 typedef

**Строки 15-16** удалить:
```cpp
#include <arch/x86/arch_debugger.h>  // УДАЛИТЬ
```

**Строки 22-24** удалить:
```cpp
#elif defined(__i386__)
	typedef struct x86_debug_cpu_state debug_cpu_state;
```

#### `headers/os/kernel/OS.h` - удалить B_CPU_x86

**В enum cpu_platform** удалить:
```cpp
B_CPU_x86,  // УДАЛИТЬ
```

### 3.2. Удаление директорий arch/x86

**КРИТИЧНО: Удалять только после переноса всего shared кода!**

Проверить что x86_64 код не импортирует из старых путей:
```bash
git grep '#include.*arch/x86/' | grep -v x86_64 | grep -v "^#"
```

Если вывод пуст, следующие директории можно удалить:

**К удалению:**
- `src/system/kernel/arch/x86/32/` - после переноса shared кода
- `src/system/kernel/arch/x86/timers/` - после переноса в arch/x86_64/timers/
- `src/system/kernel/arch/x86/paging/32bit/` - после переноса shared helpers

**ВАЖНО:** Некоторые файлы в `arch/x86/` могут быть PURE SHARED (без условной компиляции). 

Найти такие файлы:
```bash
find src/system/kernel/arch/x86/ -name "*.cpp" | while read f; do
    if ! grep -q "#ifdef.*__i386__\|#ifdef.*__x86_64__" "$f"; then
        echo "PURE SHARED: $f"
    fi
done
```

PURE SHARED файлы нужно переместить в `arch/x86_64/` перед удалением директории.

После переноса всех SHARED и PURE SHARED файлов:
- `headers/private/kernel/arch/x86/` - можно удалять
- `src/system/kernel/arch/x86/` - можно удалять (но проверить каждый файл)

### 3.3. Удаление bios_ia32 boot loader

**Проверка зависимостей:**
```bash
git grep "bios_ia32" build/
git grep "bios_ia32" src/
```

Если вывод показывает только определения в build system, директория к удалению:
- `src/system/boot/platform/bios_ia32/` - целиком

Затем удалить все ссылки в build/jam/ файлах.

### 3.4. Удаление arch/x86 из posix

**Проверить содержимое `headers/posix/arch/x86/`:**

Список файлов для анализа:
- `headers/posix/arch/x86/signal.h` - структуры для signal handling i386
- `headers/posix/arch/x86/arch_setjmp.h` - jmp_buf для i386 (6 регистров)
- `headers/posix/arch/x86/fenv.h` - FPU environment для i386

Проверить каждый файл:
```bash
# Анализ signal.h
grep -n "x86_64\|shared" headers/posix/arch/x86/signal.h
```

Если файл содержит ТОЛЬКО i386 структуры (например `old_extended_regs`, 32-bit указатели) - пометить к удалению.

**Файлы к удалению после проверки:**
- `headers/posix/arch/x86/signal.h` - только i386 signal structures
- `headers/posix/arch/x86/arch_setjmp.h` - только i386 setjmp (__jmp_buf[6])
- `headers/posix/arch/x86/fenv.h` - если нет shared кода

**Обновить `headers/posix/fenv.h`:**
```cpp
// БЫЛО:
#if defined(__i386__)
#  include <arch/x86/fenv.h>
#elif defined(__x86_64__)

// СТАЛО:
#if defined(__x86_64__)
```

Аналогично для других includes в posix headers.

### 3.5. Очистка build system

#### Build/jam/BuildSetup

**Найти и удалить:**
```jam
case x86 :
    HAIKU_BOOT_PLATFORM = bios_ia32 ;
    HAIKU_ARCH_BITS = 32 ;
    // ... все содержимое ...
    break ;
```

#### Build/jam/ArchitectureRules

**Удалить case x86:**
```jam
rule SetUpSubDirBuildSettings {
    switch $(HAIKU_ARCH) {
        case x86 :          # УДАЛИТЬ ВЕСЬ CASE
            // ...
            break ;
    }
}
```

#### Корневой Jamfile

**Обновить список архитектур:**
```jam
# БЫЛО:
HAIKU_SUPPORTED_ARCHS = x86 x86_64 arm arm64 riscv64 ;

# СТАЛО:
HAIKU_SUPPORTED_ARCHS = x86_64 arm arm64 riscv64 ;
```

### 3.6. Очистка в package definitions

```bash
# Найти все .PackageInfo с x86
find . -name "*.PackageInfo" -exec grep -l "architecture.*x86[^_]" {} \;

# Удалить строки:
# architecture: x86
# architecture: x86_gcc2
```

### 3.7. Проверка после удаления

Вручную проверить:

1. **Нет упоминаний i386:**
```bash
git grep -i "__i386__" | grep -v ".git\|Binary"
```

2. **Нет упоминаний старых путей:**
```bash
git grep "arch/x86/" | grep -v "arch/x86_64"
```

3. **Нет bios_ia32:**
```bash
git grep "bios_ia32"
```

4. **Нет case x86 в Jam:**
```bash
grep -r "case x86 :" build/jam/
```

5. **Попытка сборки:**
```bash
jam -q kernel
```

Если kernel собирается - базовая проверка пройдена.

---

## Таблица изменений

| Путь | Действие | Обоснование |
|------|----------|-------------|
| `headers/config/HaikuConfig.h` | modify | Удалить `#if defined(__i386__)` блок (строки 21-25) |
| `headers/config/types.h` | modify | Удалить `__HAIKU_BEOS_COMPATIBLE_TYPES` для i386 |
| `headers/os/kernel/debugger.h` | modify | Удалить include arch/x86/arch_debugger.h, удалить i386 typedef (строки 15, 22-24) |
| `headers/os/kernel/OS.h` | modify | Удалить `B_CPU_x86` из enum cpu_platform |
| `headers/os/package/PackageArchitecture.h` | modify | Проверить использование `B_PACKAGE_ARCHITECTURE_X86` |
| `headers/posix/fenv.h` | modify | Удалить `#if defined(__i386__)` блок |
| `headers/posix/arch/x86/` | remove | Удалить после переноса shared в x86_64 |
| `headers/posix/arch/x86_64/` | keep | Основные x86_64 определения |
| `headers/private/kernel/arch/x86/arch_thread_types.h` | move | → `arch/x86_64/`, удалить i386 части |
| `headers/private/kernel/arch/x86/arch_thread.h` | move | → `arch/x86_64/`, экстракт x86_64 |
| `headers/private/kernel/arch/x86/arch_cpu.h` | move | → `arch/x86_64/`, удалить i386 поля |
| `headers/private/kernel/arch/x86/arch_kernel.h` | move | → `arch/x86_64/`, только x86_64 memory layout |
| `headers/private/kernel/arch/x86/apm.h` | remove | APM только для BIOS i386 |
| `headers/private/kernel/arch/x86/32/` | remove | После проверки на shared код |
| `headers/private/kernel/arch/x86/64/` | move | → `arch/x86_64/` как база |
| `src/system/kernel/arch/x86/32/` | analyze→remove | Проверить каждый файл на shared, перенести нужное |
| `src/system/kernel/arch/x86/64/` | move | → `arch/x86_64/` прямой перенос |
| `src/system/kernel/arch/x86/timers/` | move | → `arch/x86_64/timers/`, удалить i386 ветки |
| `src/system/kernel/arch/x86/paging/paging.cpp` | move | → `arch/x86_64/`, только x86_64 dispatch |
| `src/system/kernel/arch/x86/paging/32bit/` | analyze→remove | Проверить на shared helpers |
| `src/system/kernel/arch/x86/paging/64bit/` | move | → `arch/x86_64/paging/` прямой перенос |
| `src/system/kernel/arch/x86/*.cpp` | analyze | Каждый файл проверить на MIXED/SHARED |
| `src/system/boot/platform/bios_ia32/` | remove | Целиком удалить после проверки deps |
| `src/system/boot/platform/efi/arch/x86/` | analyze | Может иметь MIXED код для 32/64 EFI |
| `src/system/libroot/posix/arch/x86/` | analyze | Ассемблерные оптимизации, проверить каждый файл |
| `build/jam/BuildSetup` | modify | Удалить case x86 |
| `build/jam/ArchitectureRules` | modify | Удалить x86 правила |
| `build/jam/KernelRules` | modify | Удалить i386 флаги компиляции |
| `Jamfile` | modify | Удалить x86 из HAIKU_SUPPORTED_ARCHS |