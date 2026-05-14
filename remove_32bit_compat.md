# Удаление 32-bit compat кода из x86-64 ядра Haiku (KosmOS)

Scope: только 64-битная система. Из 32-bit оставляем только загрузку 64-битного ядра через 32-bit BIOS. Весь остальной 32-bit код удаляем.


## Файлы к полному удалению

### 1. `src/system/kernel/arch/x86/64/entry_compat.S`

Весь файл — обработка 32-bit SYSCALL (AMD) и SYSENTER (Intel) от 32-bit user-процессов: `x86_64_syscall32_entry`, `x86_64_sysenter32_entry`, копирование параметров из 32-bit user stack с zero-extension до 64-bit через `kSyscallCompatInfos`/`kExtendedSyscallCompatInfos`, `x86_sysenter32_userspace_thread_exit`. Удалить целиком.


### 2. `src/system/kernel/arch/x86/64/signals_compat.cpp`

Весь файл — копирование 32-bit signal handler в commpage compat (`fill_commpage_compat_entry`). Удалить целиком.


### 3. `src/system/kernel/arch/x86/64/signals_compat_asm.S`

Весь файл — бинарный дамп 32-bit x86 signal handler (raw opcodes, `x86_64_signal_handler_compat`). Удалить целиком.


### 4. `src/system/kernel/arch/x86/64/syscalls_asm.S`

Весь файл — 32-bit user-space syscall stubs для commpage:

```asm
// Intel sysenter/sysexit
FUNCTION(x86_user_syscall_sysenter):
	movl	%esp, %ecx
	sysenter
	ret
FUNCTION_END(x86_user_syscall_sysenter)
SYMBOL(x86_user_syscall_sysenter_end):

// AMD syscall/sysret
FUNCTION(x86_user_syscall_syscall):
	syscall
	ret
FUNCTION_END(x86_user_syscall_syscall)
SYMBOL(x86_user_syscall_syscall_end):
```

Оба символа используются **только** внутри `#ifdef _COMPAT_MODE` в `syscalls.cpp` для заполнения commpage compat. 64-bit процессы вызывают `syscall` напрямую из libc без commpage-stub'а — ядро пишет `x86_64_syscall_entry` в `IA32_MSR_LSTAR`. Удалить целиком.


### 5. `src/system/kernel/arch/x86/paging/x86_physical_page_mapper_large_memory.cpp`

Весь файл — physical page mapper для 32-bit ядра, где виртуального адресного пространства не хватает для прямого маппинга всей физической памяти. Реализует пул слотов с LRU-маппингом физических страниц. На x86-64 используется mapped-вариант (`x86_physical_page_mapper_mapped`) с прямым доступом через `KERNEL_PMAP_BASE`. Удалить целиком.


### 6. `src/system/kernel/arch/x86/paging/x86_physical_page_mapper_large_memory.h`

Весь файл — заголовок для large_memory physical page mapper (`PhysicalPageSlot`, `PhysicalPageSlotPool`, константы `USER_SLOTS_PER_CPU`, `KERNEL_SLOTS_PER_CPU`). Удалить целиком.


## Правки внутри файлов

### 7. `src/system/kernel/arch/x86/64/syscalls.cpp`

#### 7a. Удалить compat includes

```cpp
#ifdef _COMPAT_MODE
#	include <commpage_compat.h>
#endif
```

```cpp
#ifdef _COMPAT_MODE
#	include <elf.h>
#endif
```

#### 7b. Удалить compat extern-объявления и глобалы

```cpp
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

#### 7c. В `init_syscall_registers()` удалить запись CSTAR MSR

```cpp
#ifdef _COMPAT_MODE
	// Syscall compat entry point address.
	x86_write_msr(IA32_MSR_CSTAR, (addr_t)x86_64_syscall32_entry);
#endif
```

#### 7d. Удалить весь блок compat-инициализации в конце файла

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


### 8. `src/system/kernel/arch/x86/64/descriptors.cpp`

#### 8a. Заменить USER32_CODE_SELECTOR на placeholder в GDT

В конструкторе `GlobalDescriptorTable` entry #3 — это 32-bit compat code segment:

```cpp
constexpr
GlobalDescriptorTable::GlobalDescriptorTable()
	:
	fTable {
		Descriptor(),
		Descriptor(DescriptorType::CodeExecuteOnly, true),
		Descriptor(DescriptorType::DataWritable, true),
		Descriptor(DescriptorType::CodeExecuteOnly, false, true),   // <-- USER32_CODE_SELECTOR
		Descriptor(DescriptorType::DataWritable, false),
		Descriptor(DescriptorType::CodeExecuteOnly, false),
	}
```

**Нельзя просто удалить** — это сломает SYSRET. Intel SDM:
- SYSRET CS = `STAR[63:48] + 16`
- SYSRET SS = `STAR[63:48] + 8`

Текущие значения:
- STAR[63:48] = `USER32_CODE_SELECTOR` = 0x1B (index 3, RPL 3)
- Return CS = 0x1B + 16 = 0x2B = USER_CODE_SELECTOR (index 5)
- Return SS = 0x1B + 8 = 0x23 = USER_DATA_SELECTOR (index 4)

Заменить на пустой дескриптор, сохранив индексы:

```cpp
		Descriptor(DescriptorType::CodeExecuteOnly, false, true),
```

заменить на:

```cpp
		Descriptor(),  // reserved (was USER32_CODE_SELECTOR, slot needed for SYSRET arithmetic)
```

#### 8b. Удалить параметр `compatMode` из `Descriptor`

Объявление:

```cpp
	constexpr				Descriptor(DescriptorType type, bool kernelOnly,
								bool compatMode = false);
```

заменить на:

```cpp
	constexpr				Descriptor(DescriptorType type, bool kernelOnly);
```

Реализация — удалить:

```cpp
constexpr
Descriptor::Descriptor(DescriptorType type, bool kernelOnly, bool compatMode)
	:
	fLimit0(-1),
	fBase0(0),
	fType(static_cast<unsigned>(type)),
	fSystem(1),
	fDPL(kernelOnly ? 0 : 3),
	fPresent(1),
	fLimit1(0xf),
	fUnused(0),
	fLong(is_code_segment(type) && !compatMode ? 1 : 0),
	fDB(is_code_segment(type) && !compatMode ? 0 : 1),
	fGranularity(1),
	fBase1(0)
{
}
```

Заменить на:

```cpp
constexpr
Descriptor::Descriptor(DescriptorType type, bool kernelOnly)
	:
	fLimit0(-1),
	fBase0(0),
	fType(static_cast<unsigned>(type)),
	fSystem(1),
	fDPL(kernelOnly ? 0 : 3),
	fPresent(1),
	fLimit1(0xf),
	fUnused(0),
	fLong(is_code_segment(type) ? 1 : 0),
	fDB(is_code_segment(type) ? 0 : 1),
	fGranularity(1),
	fBase1(0)
{
}
```


### 9. `src/system/kernel/arch/x86/paging/x86_physical_page_mapper.h`

Удалить 32-bit блок с условной компиляцией:

```cpp
#ifndef __x86_64__


class TranslationMapPhysicalPageMapper {
public:
	virtual						~TranslationMapPhysicalPageMapper() { }

	virtual	void				Delete() = 0;

	virtual	void*				GetPageTableAt(phys_addr_t physicalAddress) = 0;
		// Must be invoked with thread pinned to current CPU.
};


class X86PhysicalPageMapper : public VMPhysicalPageMapper {
public:
	virtual	status_t			CreateTranslationMapPhysicalPageMapper(
									TranslationMapPhysicalPageMapper** _mapper)
										= 0;

	virtual	void*				InterruptGetPageTableAt(
									phys_addr_t physicalAddress) = 0;
};


#else
```

И в конце файла:

```cpp
#include "paging/x86_physical_page_mapper_mapped.h"


#endif	// __x86_64__
```

Оставить только x86-64 классы напрямую без `#ifdef`, безусловно подключая `x86_physical_page_mapper_mapped.h`.


## Сводка

| Действие | Файл | Описание |
|----------|------|----------|
| Удалить файл | `64/entry_compat.S` | 32-bit SYSCALL/SYSENTER entry point |
| Удалить файл | `64/signals_compat.cpp` | 32-bit signal handler commpage |
| Удалить файл | `64/signals_compat_asm.S` | 32-bit signal handler binary dump |
| Удалить файл | `64/syscalls_asm.S` | 32-bit user syscall stubs для commpage |
| Удалить файл | `paging/x86_physical_page_mapper_large_memory.cpp` | 32-bit physical page mapper |
| Удалить файл | `paging/x86_physical_page_mapper_large_memory.h` | 32-bit physical page mapper header |
| Правки | `64/syscalls.cpp` | Убрать все `_COMPAT_MODE` блоки, CSTAR MSR, `gX86SetSyscallStack` |
| Правки | `64/descriptors.cpp` | USER32_CODE → пустой placeholder, убрать `compatMode` |
| Правки | `paging/x86_physical_page_mapper.h` | Убрать `#ifndef __x86_64__` условную компиляцию |


## Зависимости и что проверить дополнительно

1. **GDT / STAR MSR**: USER32_CODE_SELECTOR заменяем на пустой placeholder, не удаляем — SYSRET арифметика завязана на порядок сегментов в GDT.

2. **Build system**: Убрать удалённые файлы из Jamfile / build rules.

3. **`_COMPAT_MODE` define**: Grep по всему дереву — в общих x86 файлах (не только `64/`) могут быть дополнительные блоки для удаления: commpage_compat, runtime_loader compat, syscall table generation.

4. **`commpage_compat.h` / `commpage_compat.cpp`**: После удаления compat-кода проверить, остались ли другие потребители API commpage_compat. Если нет — удалить тоже.

5. **`gX86SetSyscallStack`**: Глобальный указатель на функцию, используется при переключении контекста для обновления `IA32_MSR_SYSENTER_ESP`. После удаления compat-кода переменная всегда `NULL` — убрать её и все проверки на неё в коде переключения контекста.

6. **`kSyscallCompatInfos` / `kExtendedSyscallCompatInfos`**: Таблицы syscall для 32-bit приложений. Генерируются build-системой. После удаления `entry_compat.S` их можно не генерировать.

7. **`UserTLSDescriptor` / `SetUserTLS`**: Используется для TLS compat 32-bit приложений через GDT-сегменты. Проверить, используется ли для 64-bit (64-bit использует FS/GS base через MSR, но GDT TLS может быть нужен для совместимости с некоторыми pthread-реализациями).
