# Анализ точки входа в ядро x86_64

## Обзор

Данный документ анализирует реализацию точки входа в ядро Haiku для архитектуры x86_64, включая состояние регистров, настройку стека и процедуры инициализации ядра.

## Структура входа в ядро

### 1. Загрузчик → Ядро (Boot Loader → Kernel)

**Файлы:**
- `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp` - C++ часть подготовки
- `src/system/boot/platform/efi/arch/x86_64/entry.S` - Ассемблерная точка входа
- `src/system/kernel/main.cpp` - Главная функция ядра

### 2. Последовательность загрузки

```
EFI Boot Loader (arch_start_kernel) 
    ↓
arch_enter_kernel (entry.S)
    ↓
kernel _start (main.cpp)
    ↓
Kernel initialization
```

## Функция arch_start_kernel (C++)

**Местоположение:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:87`

### Параметры входа:
- `addr_t kernelEntry` - адрес точки входа в ядро

### Основные этапы:

1. **Получение карты памяти EFI**
   ```cpp
   kBootServices->GetMemoryMap(&memory_map_size, &dummy, &map_key, 
                               &descriptor_size, &descriptor_version)
   ```

2. **Генерация таблиц страниц**
   ```cpp
   uint64_t final_pml4 = arch_mmu_generate_post_efi_page_tables(
       memory_map_size, memory_map, descriptor_size, descriptor_version);
   ```

3. **Выход из служб EFI**
   ```cpp
   kBootServices->ExitBootServices(kImage, map_key)
   ```

4. **Запуск SMP процессоров**
   ```cpp
   smp_boot_other_cpus(final_pml4, kernelEntry, (addr_t)&gKernelArgs);
   ```

5. **Передача управления ядру**
   ```cpp
   arch_enter_kernel(final_pml4, kernelEntry,
       gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size);
   ```

## Функция arch_enter_kernel (Assembly)

**Местоположение:** `src/system/boot/platform/efi/arch/x86_64/entry.S:20`

### Параметры регистров при входе:
- `%rdi` - PML4 (Page Map Level 4) адрес таблиц страниц
- `%rsi` - Адрес точки входа в ядро  
- `%rdx` - Адрес вершины стека

### Последовательность операций:

1. **Настройка MMU (CR3)**
   ```asm
   movq    %rdi, %cr3              // Загрузить PML4 в CR3
   ```

2. **Загрузка GDT**
   ```asm
   lgdtq   gLongGDTR(%rip)        // Загрузить 64-битную GDT
   ```

3. **Переход в 64-битный сегмент кода**
   ```asm
   push    $KERNEL_CODE_SELECTOR   // Селектор кода ядра
   lea     .Llmode(%rip), %rax     // Адрес перехода
   push    %rax
   lretq                           // Дальний возврат для смены CS
   ```

4. **Настройка сегментов данных**
   ```asm
   mov     $KERNEL_DATA_SELECTOR, %ax
   mov     %ax, %ss                // Сегмент стека
   xor     %ax, %ax                // Обнулить
   mov     %ax, %ds                // Сегмент данных
   mov     %ax, %es                // Дополнительный сегмент
   mov     %ax, %fs                // FS сегмент
   mov     %ax, %gs                // GS сегмент
   ```

5. **Настройка стека**
   ```asm
   movq    %rdx, %rsp              // Установить указатель стека
   xorq    %rbp, %rbp              // Обнулить базовый указатель
   push    $2                      // Установить RFLAGS
   popf
   ```

6. **Подготовка аргументов для ядра**
   ```asm
   mov     %rsi, %rax              // entry point в RAX
   leaq    gKernelArgs(%rip), %rdi // kernel_args в RDI
   xorl    %esi, %esi              // currentCPU = 0 в RSI
   call    *%rax                   // Вызов ядра
   ```

## Функция _start (Kernel Main)

**Местоположение:** `src/system/kernel/main.cpp:98`

### Параметры при входе:
- `%rdi` - `kernel_args *bootKernelArgs` - структура с параметрами загрузки
- `%rsi` - `int currentCPU` - номер текущего CPU (0 для boot CPU)

### Состояние регистров при входе в ядро:

| Регистр | Содержимое | Описание |
|---------|------------|----------|
| RDI | kernel_args* | Указатель на структуру kernel_args |
| RSI | 0 | Номер текущего CPU (boot CPU) |
| RAX | - | Не определено |
| RBX-R15 | - | Не определено |
| RSP | Stack Top | Вершина стека ядра |
| RBP | 0 | Обнулен |
| CR3 | PML4 | Таблицы страниц ядра |
| CS | KERNEL_CODE_SELECTOR | Сегмент кода ядра |
| SS | KERNEL_DATA_SELECTOR | Сегмент стека ядра |
| DS,ES,FS,GS | 0 | Обнулены |

### Структура kernel_args:

Основные поля структуры kernel_args:
```cpp
typedef struct kernel_args {
    uint32  kernel_args_size;           // Размер структуры
    uint32  version;                    // Версия
    
    // Образы ядра и модулей
    struct preloaded_image* kernel_image;
    struct preloaded_image* preloaded_images;
    
    // Карта физической памяти
    uint32  num_physical_memory_ranges;
    addr_range physical_memory_range[MAX_PHYSICAL_MEMORY_RANGE];
    
    // Выделенная память
    uint32  num_physical_allocated_ranges;
    addr_range physical_allocated_range[MAX_PHYSICAL_ALLOCATED_RANGE];
    
    // Виртуальная память
    uint32  num_virtual_allocated_ranges;
    addr_range virtual_allocated_range[MAX_VIRTUAL_ALLOCATED_RANGE];
    
    // CPU информация
    uint32  num_cpus;                   // Количество CPU
    addr_range cpu_kstack[SMP_MAX_CPUS]; // Стеки ядра для каждого CPU
    
    // Кадровый буфер
    struct {
        addr_range  physical_buffer;
        uint32      bytes_per_row;
        uint16      width, height;
        uint8       depth;
        bool        enabled;
    } frame_buffer;
    
    // Архитектурно-специфичные аргументы
    arch_kernel_args    arch_args;
    platform_kernel_args platform_args;
} kernel_args;
```

## Настройка стека

### Стек ядра для boot CPU:
- **Размер:** Определяется загрузчиком (обычно 16KB-64KB)
- **Расположение:** `kernel_args.cpu_kstack[0].start + cpu_kstack[0].size`
- **Выравнивание:** 16-байтовое выравнивание для x86_64
- **Направление роста:** Вниз (к меньшим адресам)

### Состояние стека при входе:
```
High Address
+------------------+ <- RSP (Stack Top)
|   (empty stack)  |
|                  |
|                  |
|                  |
+------------------+ <- Stack Base
Low Address
```

## Инициализация ядра

### Основные этапы инициализации в _start():

1. **Проверка версии kernel_args**
   ```cpp
   if (bootKernelArgs->version != CURRENT_KERNEL_ARGS_VERSION)
       return -1;
   ```

2. **Синхронизация SMP**
   ```cpp
   smp_cpu_rendezvous(&sCpuRendezvous);  // Ожидание всех CPU
   ```

3. **Копирование kernel_args**
   ```cpp
   if (currentCPU == 0)
       memcpy(&sKernelArgs, bootKernelArgs, bootKernelArgs->kernel_args_size);
   ```

4. **Инициализация подсистем** (только для boot CPU):
   - Platform initialization
   - Debug output setup
   - CPU initialization
   - Interrupt initialization
   - Virtual memory initialization
   - Module system initialization
   - SMP initialization
   - Timer initialization

### Порядок инициализации подсистем:
```
arch_platform_init() → debug_init() → cpu_init() → interrupts_init() → 
vm_init() → module_init() → smp_init() → timer_init() → scheduler_init() → 
thread_init() → ...
```

## Ключевые особенности для ARM64

### Различия в точке входа:

1. **Регистры:**
   - ARM64 использует X0, X1 вместо RDI, RSI
   - Нет сегментной модели (CS, DS, etc.)
   - CR3 заменяется на TTBR0_EL1/TTBR1_EL1

2. **Exception Levels:**
   - ARM64 имеет EL0-EL3 вместо колец защиты x86
   - Переход EL2→EL1 требуется для гипервизора

3. **MMU настройка:**
   - TCR_EL1 вместо CR3/CR4
   - MAIR_EL1 для атрибутов памяти
   - SCTLR_EL1 для управления MMU

4. **Стек:**
   - 16-байтовое выравнивание (как x86_64)
   - SP регистр вместо RSP

### Рекомендации для ARM64 реализации:

1. **arch_enter_kernel для ARM64:**
   ```asm
   // X0 = kernel_args*, X1 = kernelEntry, X2 = stackTop
   mov     sp, x2                  // Установить стек
   mov     x4, x1                  // Сохранить entry point
   // X0 уже содержит kernel_args
   mov     x1, #0                  // currentCPU = 0
   br      x4                      // Переход в ядро
   ```

2. **Состояние регистров при входе в ядро ARM64:**
   - X0: kernel_args*
   - X1: currentCPU (0)
   - SP: Stack top
   - ELR_EL1: Return address (не используется)
   - SPSR_EL1: Processor state

3. **MMU состояние:**
   - TTBR0_EL1/TTBR1_EL1: Таблицы страниц
   - TCR_EL1: Конфигурация трансляции
   - MAIR_EL1: Атрибуты памяти
   - SCTLR_EL1: MMU включено

## Заключение

Анализ показывает четкую структуру передачи управления от загрузчика к ядру в x86_64:

1. **Загрузчик** подготавливает память, таблицы страниц и kernel_args
2. **arch_enter_kernel** настраивает CPU состояние и регистры  
3. **Ядро _start** инициализирует все подсистемы

Эта архитектура может быть адаптирована для ARM64 с учетом особенностей архитектуры ARM64, таких как exception levels, MMU регистры и отсутствие сегментной модели.