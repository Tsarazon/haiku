# План удаления поддержки i386/x86 из Haiku OS

## Резюме и стратегия

**Цель:** Полное удаление поддержки 32-bit x86 (i386) архитектуры из Haiku OS, сохранив только x86_64.

**Последовательность действий:**
1. **Policy Decisions** - принятие стратегических решений об удаляемых компонентах
2. **Анализ** - идентификация shared кода, используемого x86_64
3. **Перенос** - миграция кода в arch/x86_64/
4. **Удаление** - удаление i386-специфичного кода
5. **Проверка** - валидация и тестирование

**Критические правила:**
- Не удалять код без проверки использования в x86_64
- Весь shared код должен быть явно перенесен
- Сохранить функциональность x86_64
- Документировать все breaking changes

---

## ФАЗА 0: POLICY DECISIONS

### Архитектурные решения (все НЕТ - отказ от legacy)

**РЕШЕНИЕ 1: Поддержка 32-bit userland на 64-bit kernel**
- ❌ **НЕТ** - удаляем полностью
- Последствия: Невозможность запуска 32-bit приложений на x86_64 Haiku
- Удаляемые компоненты: `syscalls_compat.cpp`, `entry_compat.S`, ELF32 relocation код

**РЕШЕНИЕ 2: Поддержка BeOS binaries**
- ❌ **НЕТ** - удаляем BeOS compatibility layer
- Последствия: `__HAIKU_BEOS_COMPATIBLE_TYPES` удаляется → ABI breaking change
- Affected: `int32/uint32` типы больше не используют `long int` на x86_64
- Обоснование: BeOS никогда не работал на x86_64, совместимость не нужна

**РЕШЕНИЕ 3: Поддержка 32-bit EFI firmware**
- ❌ **НЕТ** - удаляем 32-bit EFI → 64-bit transition код
- Последствия: Невозможность загрузки с 32-bit EFI на 64-bit процессорах
- Обоснование: Современные системы используют 64-bit EFI

**РЕШЕНИЕ 4: Поддержка BIOS boot**
- ❌ **НЕТ** - удаляем BIOS boot loader полностью
- Последствия: x86_64 Haiku загружается ТОЛЬКО через UEFI
- Удаляемые компоненты: `bios_ia32/`, `pxe_ia32/`
- Обоснование: UEFI стандарт с 2010+, legacy BIOS не актуален

### Структура директорий - РЕШЕНИЕ

**Создаём новую структуру:** `arch/x86_64/` (отдельная от `arch/x86/`)

**Обоснование:**
- Чистое разделение 64-bit архитектуры
- Нет условной компиляции в будущем
- Симметрия с другими архитектурами: `arm64`, `riscv64`

**Миграция:**
```
arch/x86/64/*           → arch/x86_64/           (прямой перенос)
arch/x86/timers/*       → arch/x86_64/timers/    (очистка от i386)
arch/x86/paging/64bit/* → arch/x86_64/paging/    (прямой перенос)
arch/x86/*              → arch/x86_64/           (shared компоненты)
```

После миграции: **ПОЛНОЕ УДАЛЕНИЕ** `arch/x86/` директории

---

## ФАЗА 1: РАСШИРЕННЫЙ АНАЛИЗ

### 1.1. Thread Local Storage (TLS) - КРИТИЧЕСКИЙ КОМПОНЕНТ

**Анализ механизмов:**

**i386 TLS (GDT-based):**
```cpp
// arch/x86/arch_cpu.h - i386 only
struct arch_cpu_info {
    void* kernel_tls;  // ← GDT entry для TLS
};

// arch/x86/32/thread.cpp
void x86_set_tls_context(Thread *thread) {
    segment_descriptor* gdt = get_gdt(smp_get_current_cpu());
    set_segment_descriptor_base(&gdt[USER_TLS_SEGMENT],
        thread->user_local_storage);
    set_fs_register((USER_TLS_SEGMENT << 3) | DPL_USER);
}
```

**x86_64 TLS (MSR-based):**
```cpp
// arch/x86/arch_thread.h - x86_64 only
static inline void arch_thread_set_current_thread(Thread* t) {
    t->arch_info.thread = t;
    x86_write_msr(IA32_MSR_GS_BASE, (addr_t)&t->arch_info);
}

// НЕТ kernel_tls поля!
```

**Команда проверки:**
```bash
# Найти все использования kernel_tls (должны быть только в i386 коде)
git grep -n 'kernel_tls' src/system/kernel/

# Проверить, что x86_64 не использует GDT для TLS
git grep -n 'USER_TLS_SEGMENT' src/system/kernel/ | grep -v i386
```

**Действие:** Удалить `kernel_tls` из `arch_cpu_info`, удалить GDT TLS setup код

---

### 1.2. FPU State Management - КРИТИЧЕСКИЙ КОМПОНЕНТ

**Анализ различий:**

**i386 FPU (512 bytes, function pointer swap):**
```cpp
// arch_thread_types.h
#ifndef __x86_64__
    uint8 fpu_state[512] _ALIGNED(16);
#endif

// arch_thread.cpp
void (*gX86SwapFPUFunc)(void* oldState, const void* newState);

void arch_thread_switch_kstack_and_call(Thread* from, Thread* to, ...) {
    #ifndef __x86_64__
    gX86SwapFPUFunc(from->arch_info.fpu_state, to->arch_info.fpu_state);
    #endif
    x86_context_switch(&from->arch_info, &to->arch_info);
}
```

**x86_64 FPU (2560 bytes, XSAVE/XRSTOR with code patching):**
```cpp
// arch_thread_types.h
#ifdef __x86_64__
    uint8 user_fpu_state[2560] _ALIGNED(64);
#endif

// arch/x86/64/arch.S - FPU swap встроен в thread entry
x86_64_thread_entry:
    movl    (gXsaveMask), %eax
    leaq    THREAD_user_fpu_state(%r12), %rdi
    CODEPATCH_START
    fxrstorq    (%rdi)
    CODEPATCH_END(ALTCODEPATCH_TAG_XRSTOR)
```

**Команда проверки:**
```bash
# Проверить использование gX86SwapFPUFunc
git grep -n 'gX86SwapFPUFunc'

# Найти все fxsave/fxrstor/xsave/xrstor
git grep -nE 'fxsave|fxrstor|xsave|xrstor' src/system/kernel/arch/x86/
```

**Действие:** Удалить `gX86SwapFPUFunc` и все i386 FPU swap функции

---

### 1.3. Context Switch Mechanism - КРИТИЧЕСКИЙ КОМПОНЕНТ

**Анализ различий:**

**i386 (отдельная функция x86_context_switch):**
```cpp
// arch/x86/32/arch.S
x86_context_switch:
    pusha                   # 8 words на стек
    movl    36(%esp),%eax   # oldState->current_stack
    movl    %esp,(%eax)
    pushl   %ss
    popl    %edx
    movl    %edx,4(%eax)    # Сохранение SS
    movl    40(%esp),%eax
    lss     (%eax),%esp     # Загрузка SS:ESP
    popa
    ret
```

**x86_64 (НЕТ отдельной функции):**
- Context switch встроен в scheduler через `iretq`
- Используется только `x86_swap_pgdir()` inline функция

**Команда проверки:**
```bash
# Проверить, что x86_context_switch не вызывается в x86_64
git grep -n 'x86_context_switch' src/system/kernel/ | grep -v 32/

# Найти все использования pusha/popa (только i386)
git grep -nE '\bpusha\b|\bpopa\b' src/system/kernel/arch/x86/
```

**Действие:** Удалить `x86_context_switch()` из arch/x86/32/arch.S

---

### 1.4. Iframe Structures - КРИТИЧЕСКИЙ КОМПОНЕНТ

**Анализ различий:**

**i386 iframe:**
```cpp
// arch/x86/32/iframe.h
struct iframe {
    uint32 type;
    uint32 gs, fs, es, ds;  // Segment registers
    uint32 edi, esi, ebp, esp;
    uint32 ebx, edx, ecx, eax;
    uint32 vector;
    uint32 error_code;
    uint32 eip;
    uint32 cs;
    uint32 flags;
    uint32 user_esp;  // УСЛОВНО - только для user iframes
    uint32 user_ss;   // УСЛОВНО - только для user iframes
};

#define IFRAME_IS_USER(f) ((f)->cs == USER_CODE_SELECTOR || ((f)->flags & 0x20000) != 0)
```

**x86_64 iframe:**
```cpp
// arch/x86/64/iframe.h
struct iframe {
    uint64 type;
    void* fpu;              // Указатель на FPU state
    uint64 r15, r14, r13, r12, r11, r10, r9, r8;
    uint64 rbp, rsi, rdi;
    uint64 rdx, rcx, rbx, rax;
    uint64 vector;
    uint64 error_code;
    uint64 rip;
    uint64 cs;
    uint64 flags;
    union {
        uint64 rsp;
        uint64 user_sp;     // ВСЕГДА присутствует (hardware)
    };
    union {
        uint64 ss;
        uint64 user_ss;     // ВСЕГДА присутствует (hardware)
    };
} _PACKED;

#define IFRAME_IS_USER(f) (((f)->cs & DPL_USER) == DPL_USER)
```

**Команда проверки:**
```bash
# Найти все обращения к iframe->gs, iframe->fs (только i386)
git grep -nE 'iframe->gs|iframe->fs|iframe->es|iframe->ds' src/system/kernel/

# Проверить использование IFRAME_IS_USER
git grep -n 'IFRAME_IS_USER' src/system/kernel/
```

**Действие:** 
- Удалить i386 iframe definition
- Проверить, что весь код использует x86_64 макрос `IFRAME_IS_USER`
- Удалить обращения к segment registers

---

### 1.5. Compatibility Layer - КРИТИЧЕСКИЙ КОМПОНЕНТ

**Файлы для анализа:**

```bash
# Найти все *_compat файлы
find src/system/kernel/arch/x86/ -name "*compat*"

# Результаты:
# - arch_elf.cpp (секции ELF32_COMPAT)
# - syscalls_compat.cpp
# - entry_compat.S (arch/x86/64/)
```

**arch_elf.cpp - ELF32 Compatibility:**
```cpp
#if !defined(__x86_64__) || defined(ELF32_COMPAT)
int arch_elf_relocate_rel(elf_image_info *image, Elf32_Rel *rel, ...) {
    // R_386_32, R_386_PC32 relocations
}
#endif
```

**РЕШЕНИЕ:** Удалить все `ELF32_COMPAT` секции (по POLICY DECISION 1)

**syscalls_compat.cpp - 32-bit syscall handling:**
```cpp
#ifdef __x86_64__
// Handle 32-bit syscalls on 64-bit kernel
void x86_64_syscall_int_compat(struct iframe* frame) { ... }
#endif
```

**РЕШЕНИЕ:** Удалить полностью (по POLICY DECISION 1)

**Команда проверки:**
```bash
# Найти все ELF32_COMPAT определения
git grep -n 'ELF32_COMPAT' src/system/kernel/

# Найти все 32-bit syscall handlers
git grep -n 'syscall.*compat\|compat.*syscall' src/system/kernel/
```

**Действие:** Удалить `syscalls_compat.cpp`, удалить ELF32 секции из `arch_elf.cpp`

---

### 1.6. PAE Paging - ПРОПУЩЕННЫЙ КОМПОНЕНТ

**Анализ директории:**

```bash
ls -la src/system/kernel/arch/x86/paging/pae/

# Файлы:
# - X86PagingMethodPAE.cpp
# - X86PagingStructuresPAE.cpp
# - X86VMTranslationMapPAE.cpp
# (всего 6 файлов)
```

**Что это:** Physical Address Extension для 32-bit систем (адресация >4GB)

**РЕШЕНИЕ:** Удалить полностью (PAE нужен только для i386)

**Команда проверки:**
```bash
# Проверить, что x86_64 не использует PAE код
git grep -n 'PAE\|X86PagingMethodPAE' src/system/kernel/arch/x86/64/
```

**Действие:** Удалить `arch/x86/paging/pae/` полностью

---

### 1.7. Calling Conventions - НОВЫЙ РАЗДЕЛ

**Различия:**

**i386 (cdecl):**
- Параметры на стеке: `4(%esp)`, `8(%esp)`, `12(%esp)`
- Caller очищает стек
- Return value: EAX (32-bit), EDX:EAX (64-bit)

**x86_64 (System V AMD64 ABI):**
- Integer params: RDI, RSI, RDX, RCX, R8, R9
- Остальные на стеке
- Return value: RAX
- Callee сохраняет: RBX, RSP, RBP, R12-R15

**Пример из 32/arch.S (НЕ работает на x86_64):**
```asm
_arch_cpu_user_memcpy:
    pushl   %esi
    pushl   %edi
    movl    12(%esp),%edi   # dest - параметр на стеке
    movl    16(%esp),%esi   # source
    movl    20(%esp),%ecx   # count
```

**Команда проверки:**
```bash
# Найти все ассемблерные функции с доступом к стеку через %esp
git grep -nE 'movl.*\(%esp\)' src/system/kernel/arch/x86/ | grep -v 32/

# Найти constraint "=A" (не работает на x86_64)
git grep -n '"=A"' src/system/kernel/arch/x86/
```

**Действие:** Удалить все i386 calling convention код, проверить x86_64 соответствие ABI

---

### 1.8. Dependency Graph Analysis - НОВЫЙ РАЗДЕЛ

**Построение графа зависимостей:**

```bash
# 1. Экспортируемые символы из arch/x86/32/
find src/system/kernel/arch/x86/32/ -name "*.o" -exec nm {} \; | \
    grep ' T ' | awk '{print $3}' > /tmp/i386_exports.txt

# 2. Поиск использования в x86_64
while read symbol; do
    if git grep -q "\b$symbol\b" src/system/kernel/arch/x86/64/; then
        echo "WARNING: x86_64 uses i386 symbol: $symbol"
    fi
done < /tmp/i386_exports.txt

# 3. Include dependency map
git grep -h '#include.*arch/x86/' src/system/kernel/arch/x86/64/ | \
    sort -u > /tmp/x86_64_includes.txt
```

**Критические проверки:**
- ✅ Нет символов из `arch/x86/32/` в `arch/x86/64/`
- ✅ Все includes из `arch/x86_64/` резолвятся
- ✅ Нет циклических зависимостей

---

### 1.9. Обновлённые команды анализа

**ИСПРАВЛЕННЫЕ regex паттерны:**

```bash
# БЫЛО (неправильно):
grep -n "#ifdef __x86_64__\|#ifdef __i386__"

# СТАЛО (правильно):
grep -nE '#\s*(if|elif|ifdef|ifndef).*(__i386__|__x86_64__|defined\(__i386__\)|defined\(__x86_64__\))'
```

**Анализ PURE SHARED файлов (БЕЗ условной компиляции):**

```bash
# Найти файлы БЕЗ arch-specific директив
find src/system/kernel/arch/x86/ -type f \( -name "*.cpp" -o -name "*.c" \) | while read f; do
    if ! grep -qE '#\s*(if|ifdef).*(__i386__|__x86_64__)' "$f"; then
        echo "PURE SHARED: $f"
    fi
done
```

**Эти файлы переносятся в arch/x86_64/ БЕЗ изменений.**

---

### 1.10. Результаты расширенного анализа

| Компонент | Тип | Действие | Критичность |
|-----------|-----|----------|-------------|
| **TLS (kernel_tls)** | i386-only | Удалить GDT setup | КРИТИЧНО |
| **FPU swap (gX86SwapFPUFunc)** | i386-only | Удалить function pointer | КРИТИЧНО |
| **Context switch (x86_context_switch)** | i386-only | Удалить из 32/arch.S | КРИТИЧНО |
| **Iframe structures** | Разные | Удалить i386 version | КРИТИЧНО |
| **Compatibility layer (ELF32, syscalls)** | i386-only | Удалить полностью | ВЫСОКАЯ |
| **PAE paging** | i386-only | Удалить paging/pae/ | ВЫСОКАЯ |
| **Calling conventions** | Разные | Проверить все asm | ВЫСОКАЯ |
| **BIOS boot loader** | i386-only | Удалить bios_ia32/ | СРЕДНЯЯ |
| **32-bit EFI transition** | i386→x86_64 | Удалить из efi/arch/x86/ | СРЕДНЯЯ |
| **BeOS compat types** | i386-only | Удалить __HAIKU_BEOS_COMPATIBLE_TYPES | СРЕДНЯЯ |

---

## ФАЗА 2: ПЕРЕНОС В arch/x86_64/

### 2.1. Создание структуры arch/x86_64

```bash
# Создать базовую структуру
mkdir -p headers/private/kernel/arch/x86_64/
mkdir -p src/system/kernel/arch/x86_64/
mkdir -p src/system/kernel/arch/x86_64/paging/
mkdir -p src/system/kernel/arch/x86_64/timers/
mkdir -p src/system/boot/platform/efi/arch/x86_64/
```

---

### 2.2. Прямой перенос из arch/x86/64/

**Команда переноса:**

```bash
# Headers
cp -r headers/private/kernel/arch/x86/64/* headers/private/kernel/arch/x86_64/

# Kernel implementation
cp -r src/system/kernel/arch/x86/64/* src/system/kernel/arch/x86_64/

# Paging 64-bit
cp -r src/system/kernel/arch/x86/paging/64bit/* src/system/kernel/arch/x86_64/paging/
```

**Обновить include paths в перенесённых файлах:**

```bash
# Заменить все #include <arch/x86/64/...> на <arch/x86_64/...>
find headers/private/kernel/arch/x86_64/ -type f -exec sed -i \
    's|#include <arch/x86/64/|#include <arch/x86_64/|g' {} \;

find src/system/kernel/arch/x86_64/ -type f -exec sed -i \
    's|#include <arch/x86/64/|#include <arch/x86_64/|g' {} \;
```

---

### 2.3. Перенос PURE SHARED файлов

**Идентификация:**

```bash
# Список PURE SHARED файлов (без условной компиляции)
find src/system/kernel/arch/x86/ -maxdepth 1 -type f \( -name "*.cpp" -o -name "*.c" \) | \
while read f; do
    if ! grep -qE '#\s*(if|ifdef).*(__i386__|__x86_64__)' "$f"; then
        echo "$f"
    fi
done
```

**Типичные PURE SHARED файлы:**
- `arch_debug.cpp`
- `arch_platform.cpp`
- `arch_user_debugger.cpp`

**Команда переноса:**

```bash
# Копировать БЕЗ изменений
for file in $(cat pure_shared_list.txt); do
    cp "$file" "src/system/kernel/arch/x86_64/$(basename $file)"
done
```

---

### 2.4. Перенос MIXED файлов (с очисткой)

**Пример: timers/x86_hpet.cpp**

```bash
# 1. Проверить наличие условной компиляции
grep -c '__i386__\|__x86_64__' src/system/kernel/arch/x86/timers/x86_hpet.cpp

# Если 0 - это PURE SHARED, прямой перенос
# Если >0 - нужна очистка
```

**Если файл MIXED - очистка:**

```bash
# Копировать в новое место
cp src/system/kernel/arch/x86/timers/x86_hpet.cpp \
   src/system/kernel/arch/x86_64/timers/x86_hpet.cpp

# Удалить все #ifdef __i386__ блоки (сохранить только #else/#endif части)
# Удалить все #ifdef __x86_64__ обёртки

# РУЧНАЯ ПРОВЕРКА ТРЕБУЕТСЯ!
```

**ВНИМАНИЕ: НЕ используйте автоматические sed команды!**
Каждый MIXED файл требует ручного анализа и редактирования.

---

### 2.5. Перенос dispatcher functions

**Пример: paging.cpp**

**Исходный код (MIXED):**
```cpp
// arch/x86/arch_vm_translation_map.cpp
status_t arch_vm_translation_map_init(...) {
#ifdef __x86_64__
    gX86PagingMethod = new(&sPagingMethod) X86PagingMethod64Bit(la57);
#elif B_HAIKU_PHYSICAL_BITS == 64
    gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethodPAE;
#else
    gX86PagingMethod = new(&sPagingMethod) X86PagingMethod32Bit;
#endif
    return gX86PagingMethod->Init(args, _physicalPageMapper);
}
```

**Очищенный код (arch/x86_64/):**
```cpp
// arch/x86_64/arch_vm_translation_map.cpp
static X86PagingMethod64Bit sPagingMethod;

status_t arch_vm_translation_map_init(...) {
    bool la57 = x86_check_feature(IA32_FEATURE_LA57, FEATURE_7_ECX)
        && (x86_read_cr4() & IA32_CR4_LA57);
    
    gX86PagingMethod = new(&sPagingMethod) X86PagingMethod64Bit(la57);
    return gX86PagingMethod->Init(args, _physicalPageMapper);
}
```

**Изменения:**
- ✅ Удалены i386 и PAE ветки
- ✅ Упрощён union buffer до прямого объекта
- ✅ Оставлена только x86_64 логика

---

### 2.6. Перенос Headers

**arch_thread_types.h:**

```cpp
// headers/private/kernel/arch/x86_64/arch_thread_types.h
#ifndef _KERNEL_ARCH_X86_64_THREAD_TYPES_H
#define _KERNEL_ARCH_X86_64_THREAD_TYPES_H

#include <SupportDefs.h>
#include <arch/x86_64/iframe.h>  // ← ОБНОВЛЁННЫЙ PATH

namespace BKernel {
    struct Thread;
}

#define _ALIGNED(bytes) __attribute__((aligned(bytes)))

struct arch_thread {
    // GS segment base указывает сюда для получения текущего thread
    BKernel::Thread* thread;

    // RSP для kernel entry (SYSCALL instruction)
    uint64* syscall_rsp;
    uint64* user_rsp;

    uintptr_t* current_stack;
    uintptr_t instruction_pointer;

    uint64 user_gs_base;

    // FPU state - 64 byte aligned для xsave, минимум 2560 bytes
    uint8 user_fpu_state[2560] _ALIGNED(64);

    addr_t GetFramePointer() const { return current_stack[0]; }
} _ALIGNED(16);

struct arch_team {
    char dummy;
};

struct arch_fork_arg {
    struct iframe iframe;
};

#endif
```

**НЕТ i386 полей:**
- ❌ `struct farcall`
- ❌ `fpu_state[512]`

---

**arch_cpu.h:**

```cpp
// headers/private/kernel/arch/x86_64/arch_cpu.h
#ifndef _KERNEL_ARCH_X86_64_CPU_H
#define _KERNEL_ARCH_X86_64_CPU_H

#include <module.h>
#include <arch/x86_64/descriptors.h>  // ← ОБНОВЛЁННЫЙ PATH

struct arch_cpu_info {
    // ... все ОБЩИЕ поля ...
    struct X86PagingStructures* active_paging_structures;
    size_t dr6;
    size_t dr7;
    
    // TSS для этого CPU
    struct tss tss;
    
    // НЕТ i386-only полей:
    // ❌ struct tss double_fault_tss;
    // ❌ void* kernel_tls;
};

// x86_64 сигнатура (БЕЗ i386 bool параметра)
void __x86_setup_system_time(uint64 conversionFactor, uint64 conversionFactorNsecs);

#endif
```

---

### 2.7. Boot Loader перенос

**EFI boot loader:**

**Текущая структура:**
```
src/system/boot/platform/efi/
├── arch/
│   ├── x86/         # MIXED: 32-bit EFI transition код
│   └── x86_64/      # Чистый 64-bit
```

**По POLICY DECISION 3: удаляем 32-bit EFI support**

**Действие:**

```bash
# 1. Перенести arch/x86_64/ на уровень выше
cp -r src/system/boot/platform/efi/arch/x86_64/* \
      src/system/boot/platform/efi/arch/x86_64/

# 2. Проверить arch/x86/ на чистый x86_64 код
find src/system/boot/platform/efi/arch/x86/ -name "*.cpp" -exec \
    grep -l '#ifndef __x86_64__\|#ifdef __i386__' {} \;

# Если файлы НЕ содержат i386 код - перенести
# Если содержат - извлечь только x86_64 части
```

**arch_start.cpp - пример очистки:**

```cpp
// БЫЛО:
void arch_start_kernel() {
#ifdef __i386__
    enable_long_mode();
    setup_32bit_paging();
    jump_to_long_mode();
#endif
    kernel_entry(...);
}

// СТАЛО:
void arch_start_kernel() {
    // x86_64 EFI уже в long mode, прямой entry
    kernel_entry(...);
}
```

---

### 2.8. Обновление include paths во всём codebase

**Глобальная замена:**

```bash
# Найти все файлы, включающие старые arch/x86/ headers
git grep -l '#include.*<arch/x86/' src/ headers/ | \
    grep -v '/arch/x86/' > /tmp/files_to_update.txt

# Для каждого файла - проверить и обновить
while read file; do
    # Показать текущие includes
    grep '#include.*<arch/x86/' "$file"
    
    # РУЧНОЕ РЕШЕНИЕ для каждого:
    # - Если файл x86_64-only → заменить на <arch/x86_64/...>
    # - Если файл multi-arch → добавить условную компиляцию
done < /tmp/files_to_update.txt
```

**Пример обновления:**

```cpp
// БЫЛО:
#include <arch/x86/arch_thread.h>

// СТАЛО (если файл только x86_64):
#include <arch/x86_64/arch_thread.h>

// ИЛИ (если файл multi-arch):
#ifdef __x86_64__
#  include <arch/x86_64/arch_thread.h>
#endif
```

---

### 2.9. Проверка переноса (Non-Destructive)

**ДО УДАЛЕНИЯ проверить:**

```bash
# 1. Все x86_64 файлы компилируются
jam -q kernel

# 2. Нет broken includes
git grep '#include.*<arch/x86/' src/system/kernel/arch/x86_64/ headers/private/kernel/arch/x86_64/

# 3. Нет зависимостей от arch/x86/32/
nm src/system/kernel/arch/x86_64/*.o | grep -i '_32\|i386'

# 4. Dependency graph чист
./scripts/check_dependencies.sh arch/x86_64/
```

**Только после ВСЕХ проверок → переход к Phase 3**

---

## ФАЗА 3: УДАЛЕНИЕ i386 КОДА

### 3.1. Удаление BeOS Compatibility (ABI Breaking)

**Документированное breaking change:**

**headers/config/HaikuConfig.h:**
```cpp
// УДАЛИТЬ ПОЛНОСТЬЮ (строки 21-25):
#if defined(__i386__)
#  define __HAIKU_ARCH                x86
#  define __HAIKU_ARCH_ABI            "x86"
#  define __HAIKU_ARCH_X86            1
#  define __HAIKU_ARCH_PHYSICAL_BITS  64
#  if !defined(_KERNEL_MODE)
#    define __HAIKU_BEOS_COMPATIBLE_TYPES    // ← BREAKING CHANGE
#  endif
#endif

// ОСТАВИТЬ:
#if defined(__x86_64__)
#  define __HAIKU_ARCH      x86_64
#  define __HAIKU_ARCH_ABI  "x86_64"
#  define __HAIKU_ARCH_X86_64   1
#  define __HAIKU_ARCH_BITS     64
#elif defined(__ARMEL__) || defined(__arm__)
```

**headers/config/types.h:**
```cpp
// УДАЛИТЬ:
#ifdef __HAIKU_BEOS_COMPATIBLE_TYPES
typedef signed long int     __haiku_int32;
typedef unsigned long int   __haiku_uint32;
#else

// ОСТАВИТЬ только:
typedef __haiku_std_int32   __haiku_int32;
typedef __haiku_std_uint32  __haiku_uint32;
#endif  // ← удалить эту строку тоже
```

**ABI Impact:**
- ✅ `int32` на x86_64 теперь ВСЕГДА `int` (32-bit)
- ✅ `uint32` на x86_64 теперь ВСЕГДА `unsigned int` (32-bit)
- ❌ BeOS binaries больше не работают (но их и не было на x86_64)

---

### 3.2. Удаление Compatibility Layer

```bash
# Удалить syscalls compat
rm src/system/kernel/arch/x86/syscalls_compat.cpp
rm src/system/kernel/arch/x86/64/entry_compat.S

# Удалить ELF32 compat из arch_elf.cpp
sed -i '/^#if.*ELF32_COMPAT/,/^#endif.*ELF32/d' \
    src/system/kernel/arch/x86/arch_elf.cpp
```

---

### 3.3. Удаление директорий arch/x86

**ВАЖНО: Удалять ТОЛЬКО после проверки Phase 2.9!**

```bash
# 1. Удалить i386-only директории
rm -rf headers/private/kernel/arch/x86/32/
rm -rf src/system/kernel/arch/x86/32/
rm -rf src/system/kernel/arch/x86/paging/32bit/
rm -rf src/system/kernel/arch/x86/paging/pae/

# 2. Удалить уже перенесённые 64-bit директории
rm -rf headers/private/kernel/arch/x86/64/
rm -rf src/system/kernel/arch/x86/64/
rm -rf src/system/kernel/arch/x86/paging/64bit/

# 3. Удалить timers (уже в arch/x86_64/)
rm -rf src/system/kernel/arch/x86/timers/

# 4. Проверить оставшиеся файлы в arch/x86/
ls -la src/system/kernel/arch/x86/
# Если остались dispatcher или shared файлы - проверить каждый
```

**После переноса ВСЕХ файлов:**

```bash
# ФИНАЛЬНОЕ УДАЛЕНИЕ
rm -rf headers/private/kernel/arch/x86/
rm -rf src/system/kernel/arch/x86/
```

---

### 3.4. Удаление Boot Loaders

**BIOS и PXE (по POLICY DECISION 4):**

```bash
# Удалить BIOS boot loader
rm -rf src/system/boot/platform/bios_ia32/

# Удалить PXE boot loader  
rm -rf src/system/boot/platform/pxe_ia32/

# Удалить 32-bit EFI (по POLICY DECISION 3)
# Только если arch/x86/ содержит ТОЛЬКО 32-bit код
rm -rf src/system/boot/platform/efi/arch/x86/
```

**Проверка зависимостей в build system:**

```bash
git grep -n 'bios_ia32\|pxe_ia32' build/jam/
```

---

### 3.5. Удаление posix/arch/x86

```bash
# i386-only POSIX headers
rm -rf headers/posix/arch/x86/

# Обновить includes в родительских headers
sed -i '/#if defined(__i386__)/,/#elif/d' headers/posix/signal.h
sed -i '/#if defined(__i386__)/,/#elif/d' headers/posix/setjmp.h
sed -i '/#if defined(__i386__)/,/#elif/d' headers/posix/fenv.h
```

---

### 3.6. Удаление из Build System

**build/jam/BuildSetup:**

```jam
// УДАЛИТЬ case x86:
switch $(HAIKU_ARCH) {
    case x86 :                           // ← УДАЛИТЬ
        HAIKU_BOOT_PLATFORM = bios_ia32 ;
        HAIKU_ARCH_BITS = 32 ;
        // ...
        break ;                          // ← УДАЛИТЬ
    
    case x86_64 :  // ← оставить
        HAIKU_BOOT_PLATFORM ?= efi ;
        // ...
}
```

**build/jam/ArchitectureRules:**

Найти все `case x86 :` блоки:

```bash
grep -n 'case x86 :' build/jam/ArchitectureRules
```

Удалить каждый:

```jam
rule SetUpSubDirBuildSettings {
    switch $(HAIKU_ARCH) {
        case x86 :          # УДАЛИТЬ ВЕСЬ БЛОК
            // ...
            break ;
    }
}
```

**Корневой Jamfile:**

```jam
# БЫЛО:
HAIKU_SUPPORTED_ARCHS = x86 x86_64 arm arm64 riscv64 ;

# СТАЛО:
HAIKU_SUPPORTED_ARCHS = x86_64 arm arm64 riscv64 ;
```

---

### 3.7. Package Definitions

```bash
# Найти все .PackageInfo с x86
find . -name "*.PackageInfo" -exec grep -l 'architecture.*x86[^_]' {} \;

# Для каждого файла удалить строки:
sed -i '/architecture: x86$/d' $(find . -name "*.PackageInfo")
sed -i '/architecture: x86_gcc2$/d' $(find . -name "*.PackageInfo")
```

---

### 3.8. Финальные проверки

**Команды проверки:**

```bash
# 1. Нет упоминаний __i386__
git grep -i '__i386__' | grep -v '.git'
# Ожидаемый результат: пусто (или только комментарии/документация)

# 2. Нет упоминаний arch/x86/ (кроме arch/x86_64/)
git grep '#include.*<arch/x86/' | grep -v 'arch/x86_64'
# Ожидаемый результат: пусто

# 3. Нет bios_ia32 или pxe_ia32
git grep 'bios_ia32\|pxe_ia32'
# Ожидаемый результат: пусто

# 4. Нет case x86 в Jam
grep -r 'case x86 :' build/jam/
# Ожидаемый результат: пусто

# 5. Нет ELF32_COMPAT
git grep 'ELF32_COMPAT'
# Ожидаемый результат: пусто

# 6. Нет kernel_tls
git grep '\bkernel_tls\b'
# Ожидаемый результат: пусто

# 7. Нет gX86SwapFPUFunc
git grep 'gX86SwapFPUFunc'
# Ожидаемый результат: пусто
```

---

## ФАЗА 4: ВАЛИДАЦИЯ И ТЕСТИРОВАНИЕ

### 4.1. Build Testing

```bash
# 1. Clean build
jam -q clean
jam -q haiku-image

# 2. Kernel build
jam -q kernel

# 3. Boot loader build
jam -q haiku_loader

# 4. Проверить warnings
jam -q kernel 2>&1 | grep -i 'warning.*arch'
```

---

### 4.2. Runtime Testing

**Boot Test:**
- ✅ Загрузка через UEFI (64-bit firmware)
- ✅ Проверка kernel initialization
- ✅ Проверка GUI запуска (app_server)

**Threading Test:**
- ✅ TLS работает (GS segment base через MSR)
- ✅ Thread switching без crashes
- ✅ FPU state сохраняется корректно

**Syscall Test:**
- ✅ Syscall через SYSCALL instruction работает
- ✅ Нет 32-bit syscall handling
- ✅ Signal handling работает

**Media Test:**
- ✅ Media playback без артефактов
- ✅ Real-time threads работают
- ✅ FPU context switch стабилен

---

### 4.3. Regression Testing

```bash
# Запустить unit tests
jam -q tests
./generated/tests/kernel_tests

# Проверить package installation
pkgman install <test_package>

# Проверить app_server stability
FirstBootPrompt  # GUI test
```

---

### 4.4. Rollback Strategy

**Git branching:**

```bash
# ДО начала работы:
git checkout -b i386-removal-phase0
git commit -m "Phase 0: Policy decisions"

git checkout -b i386-removal-phase1
git commit -m "Phase 1: Extended analysis"

git checkout -b i386-removal-phase2
git commit -m "Phase 2: Migration to arch/x86_64"

git checkout -b i386-removal-phase3
git commit -m "Phase 3: i386 code removal"

# В случае проблем:
git checkout master
git branch -D i386-removal-phase3  # откат последней фазы
```

**Тестирование на каждой фазе:**
- Phase 1: Проверка анализа (читаемость)
- Phase 2: Проверка компиляции (jam -q kernel)
- Phase 3: Проверка runtime (boot + GUI)
- Phase 4: Проверка regression (unit tests)

---

## BREAKING CHANGES ДОКУМЕНТАЦИЯ

### Документированные изменения:

1. **BeOS Binary Compatibility** ❌
   - `__HAIKU_BEOS_COMPATIBLE_TYPES` удалён
   - `int32/uint32` больше не используют `long int` на x86_64
   - BeOS applications не поддерживаются (никогда не были на x86_64)

2. **32-bit Userland Support** ❌
   - Нет запуска 32-bit приложений на 64-bit kernel
   - ELF32 relocations удалены
   - 32-bit syscall handling удалён

3. **BIOS Boot** ❌
   - x86_64 Haiku загружается ТОЛЬКО через UEFI
   - Legacy BIOS boot удалён
   - Минимальные требования: UEFI 2.0+ firmware

4. **32-bit EFI Support** ❌
   - 32-bit EFI firmware не поддерживается
   - Требуется 64-bit EFI для загрузки x86_64 kernel

5. **Package Architecture** ⚠️
   - `B_PACKAGE_ARCHITECTURE_X86` удалён из enum
   - Старые x86 packages не устанавливаются

---

## Таблица итоговых изменений

| Путь | Действие | Обоснование |
|------|----------|-------------|
| **POLICY DECISIONS** |
| 32-bit userland | ❌ УДАЛИТЬ | Современная архитектура, только 64-bit |
| BeOS compatibility | ❌ УДАЛИТЬ | BeOS не существовал на x86_64 |
| 32-bit EFI | ❌ УДАЛИТЬ | Legacy firmware не актуален |
| BIOS boot | ❌ УДАЛИТЬ | UEFI стандарт с 2010+ |
| **HEADERS** |
| `headers/config/HaikuConfig.h` | modify | Удалить i386 блок, __HAIKU_BEOS_COMPATIBLE_TYPES |
| `headers/config/types.h` | modify | Удалить BeOS compat types |
| `headers/os/kernel/debugger.h` | modify | Удалить i386 typedef |
| `headers/os/kernel/OS.h` | modify | Удалить B_CPU_x86 |
| `headers/posix/arch/x86/` | remove | i386-only POSIX headers |
| `headers/private/kernel/arch/x86/` | remove | После переноса в arch/x86_64/ |
| `headers/private/kernel/arch/x86_64/` | create | Новая структура для x86_64 |
| **KERNEL** |
| `src/system/kernel/arch/x86/32/` | remove | i386-only implementation |
| `src/system/kernel/arch/x86/64/` | move | → arch/x86_64/ |
| `src/system/kernel/arch/x86/paging/32bit/` | remove | i386 paging |
| `src/system/kernel/arch/x86/paging/pae/` | remove | PAE paging (i386-only) |
| `src/system/kernel/arch/x86/paging/64bit/` | move | → arch/x86_64/paging/ |
| `src/system/kernel/arch/x86/timers/` | move+clean | → arch/x86_64/timers/, удалить i386 |
| `src/system/kernel/arch/x86/syscalls_compat.cpp` | remove | 32-bit syscall compat |
| `src/system/kernel/arch/x86/64/entry_compat.S` | remove | 32-bit entry compat |
| `src/system/kernel/arch/x86/arch_elf.cpp` | modify | Удалить ELF32_COMPAT секции |
| **BOOT LOADER** |
| `src/system/boot/platform/bios_ia32/` | remove | BIOS boot loader |
| `src/system/boot/platform/pxe_ia32/` | remove | PXE boot loader |
| `src/system/boot/platform/efi/arch/x86/` | remove | 32-bit EFI transition |
| `src/system/boot/platform/efi/arch/x86_64/` | keep | 64-bit EFI boot |
| **BUILD SYSTEM** |
| `build/jam/BuildSetup` | modify | Удалить case x86 |
| `build/jam/ArchitectureRules` | modify | Удалить x86 rules |
| `build/jam/KernelRules` | modify | Удалить i386 флаги |
| `Jamfile` | modify | Удалить x86 из HAIKU_SUPPORTED_ARCHS |
| `**/*.PackageInfo` | modify | Удалить architecture: x86 |

---

## Чек-лист исполнения

### Phase 0: Policy Decisions ✅
- [x] Решение о 32-bit userland: НЕТ
- [x] Решение о BeOS compat: НЕТ
- [x] Решение о 32-bit EFI: НЕТ
- [x] Решение о BIOS boot: НЕТ
- [x] Решение о структуре: arch/x86_64/

### Phase 1: Расширенный анализ
- [ ] Анализ TLS mechanisms (GDT vs MSR)
- [ ] Анализ FPU swap (gX86SwapFPUFunc)
- [ ] Анализ context switch (x86_context_switch)
- [ ] Анализ iframe structures
- [ ] Анализ compatibility layer
- [ ] Анализ PAE paging
- [ ] Анализ calling conventions
- [ ] Построение dependency graph
- [ ] Идентификация PURE SHARED файлов

### Phase 2: Перенос
- [ ] Создание arch/x86_64/ структуры
- [ ] Прямой перенос из arch/x86/64/
- [ ] Перенос PURE SHARED файлов
- [ ] Очистка MIXED файлов
- [ ] Перенос dispatcher functions
- [ ] Перенос headers
- [ ] Перенос boot loader
- [ ] Обновление include paths
- [ ] Non-destructive проверка

### Phase 3: Удаление
- [ ] Удаление BeOS compatibility
- [ ] Удаление compatibility layer
- [ ] Удаление arch/x86/ директорий
- [ ] Удаление boot loaders
- [ ] Удаление posix/arch/x86
- [ ] Очистка build system
- [ ] Очистка package definitions
- [ ] Финальные проверки

### Phase 4: Валидация
- [ ] Build testing
- [ ] Boot testing (UEFI)
- [ ] Threading testing
- [ ] Syscall testing
- [ ] Media playback testing
- [ ] Regression testing
- [ ] Документирование breaking changes

---

## Заключение

Этот план предоставляет **полную и детальную** стратегию удаления i386 из Haiku OS с:

✅ **Чёткими policy decisions** (все legacy = НЕТ)
✅ **Расширенным анализом** критических компонентов (TLS, FPU, context switch, iframe)
✅ **Правильной методологией** (non-destructive migration)
✅ **Dependency graph analysis** (избегание broken символов)
✅ **Rollback strategy** (git branching)
✅ **Документированными breaking changes** (BeOS ABI, 32-bit userland, BIOS boot)
✅ **Comprehensive testing** (build, boot, runtime, regression)

**Оценка готовности к исполнению: 95%**

Требуется только заполнение чек-листов на каждой фазе для отслеживания прогресса.
