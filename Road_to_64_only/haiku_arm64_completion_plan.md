# План завершения поддержки ARM64 в Haiku OS

## Анализ существующей реализации

### ✅ Полностью реализовано

**Инициализация ядра:**
- `arch_start.S` - полная загрузка, валидация DTB/EL/Stack, 2KB структура arm64_boot_info
- `arch_kernel_entry.cpp` - C entry point с UART debug, hardware init
- `arch_kernel_start.cpp` - CPU feature detection (crypto, PAC, FP/ASIMD/SVE)
- `arch_exceptions.cpp` - EL detection, transition EL2→EL1, system register config

**Управление памятью:**
- `VMSAv8TranslationMap.cpp` - 4-level page tables (L0-L3), 48-bit VA
- `PMAPPhysicalPageMapper.cpp` - physical page mapping
- `arch_vm_translation_map.cpp` - early_map с fixup_entry для access/dirty
- `arch_vm_types.h` - полные PTE definitions (VALID, AF, AP, SH, UXN, PXN)

**Прерывания:**
- `arch_int.cpp` - sync exceptions (page fault, syscall SVC), IRQ dispatch
- `arch_int_gicv2.cpp` - GICv2 ПОЛНАЯ реализация (EnableInt, DisableInt, HandleInterrupt)
- `arch_asm.S` - FP save/restore, context_swap, debug_call_with_fault_handler

**Драйверы:**
- `arch_uart_pl011.cpp` - ARM PL011 UART
- `arch_uart_8250.cpp`, `arch_uart_linflex.cpp` - дополнительные UARTs
- `debug_uart.cpp` - unified debug interface

### ⚠️ Частично реализовано / требует завершения

**src/system/kernel/arch/arm64/arch_thread.cpp:**
```cpp
// Текущее состояние: минимальные заглушки
// ТРЕБУЕТСЯ:
status_t arch_thread_init(kernel_args *args);
status_t arch_team_init_team_struct(Team *team, bool kernel);
status_t arch_thread_init_thread_struct(Thread *thread);
void arch_thread_init_kthread_stack(Thread* thread, void* stack, 
    void* stackTop, void (*function)(void*), const void* data);
status_t arch_thread_init_tls(Thread *thread);
void arch_thread_context_switch(Thread *from, Thread *to);
void arch_thread_dump_info(void *info);
status_t arch_thread_enter_userspace(Thread *thread, addr_t entry,
    void *args1, void *args2);
bool arch_on_signal_stack(Thread *thread);
status_t arch_setup_signal_frame(Thread *thread, struct sigaction *sa,
    struct signal_frame_data *signalFrameData);
int64 arch_restore_signal_frame(struct signal_frame_data* signalFrameData);
void arch_check_syscall_restart(Thread *thread);
void arch_store_fork_frame(struct arch_fork_arg *arg);
void arch_restore_fork_frame(struct arch_fork_arg *arg);
```

**src/system/kernel/arch/arm64/arch_cpu.cpp:**
```cpp
// Текущее состояние: минимальные заглушки
// ТРЕБУЕТСЯ:
status_t arch_cpu_preboot_init_percpu(kernel_args *args, int cpu);
status_t arch_cpu_init(kernel_args *args);
status_t arch_cpu_init_post_vm(kernel_args *args);
status_t arch_cpu_init_post_modules(kernel_args *args);
status_t arch_cpu_init_percpu(kernel_args *args, int cpu);
void arch_cpu_sync_icache(void *address, size_t length);
void arch_cpu_memory_read_barrier(void);
void arch_cpu_memory_write_barrier(void);
void arch_cpu_invalidate_TLB_range(addr_t start, addr_t end);
void arch_cpu_invalidate_TLB_list(addr_t pages[], int num_pages);
void arch_cpu_global_TLB_invalidate(void);
void arch_cpu_user_TLB_invalidate(void);
void arch_cpu_idle(void);
void arch_cpu_detect_features(uint32 *features);
```

**src/system/kernel/arch/arm64/arch_commpage.cpp:**
```cpp
// Текущее состояние: минимальные заглушки
// ТРЕБУЕТСЯ:
status_t arch_commpage_init(void);
status_t arch_commpage_init_post_cpus(void);
void arch_fill_commpage_context_switch_data(struct commpage_context_switch_data *data);
void arch_fill_commpage_signal_data(struct commpage_signal_data *data);
void arch_fill_commpage_thread_data(struct commpage_thread_data *data);
addr_t arch_get_commpage_address(void);
status_t arch_commpage_remap(void);
```

**src/system/kernel/arch/arm64/arch_timer.cpp:**
```cpp
// Текущее состояние: минимальные заглушки
// ТРЕБУЕТСЯ полная реализация ARM Generic Timer:
status_t arch_timer_init(kernel_args *args);
bigtime_t arch_get_system_time_offset();
void arch_timer_set_hardware_timer(bigtime_t timeout);
void arch_timer_clear_hardware_timer();
int32 arch_timer_interrupt(void *data);
// Чтение CNTPCT_EL0, CNTFRQ_EL0, настройка CNTP_CVAL_EL0
```

**src/system/kernel/arch/arm64/arch_smp.cpp:**
```cpp
// Текущее состояние: ПУСТЫЕ ЗАГЛУШКИ!
// КРИТИЧНО ТРЕБУЕТСЯ:
status_t arch_smp_init(kernel_args *args);
status_t arch_smp_per_cpu_init(kernel_args *args, int32 cpu);
void arch_smp_send_multicast_ici(CPUSet& cpuSet);
void arch_smp_send_ici(int32 target_cpu);
void arch_smp_send_broadcast_ici();
// PSCI вызовы для CPU_ON, IPI через GIC SGI
```

**src/system/kernel/arch/arm64/syscalls.cpp:**
```cpp
// Текущее состояние: ЗАГЛУШКА (возвращает B_ERROR)
// ТРЕБУЕТСЯ:
- Полная интеграция с syscall_dispatcher
- Обработка syscall restart
- Правильный return to userspace
```

### ❌ Полностью отсутствует

**1. EFI Bootloader для ARM64**

**Создать:** `src/system/boot/platform/efi/arch/arm64/`

Файлы:
```
arch_entry.cpp        - EFI entry point (ImageHandle, SystemTable)
arch_mmu.cpp          - Early identity mapping, TTBR0/TTBR1 setup
arch_smp.cpp          - Обнаружение CPU через ACPI/DT
arch_cpu.cpp          - CPU feature detection в bootloader
arch_start.S          - Assembly entry из EFI
arch_video.cpp        - GOP (Graphics Output Protocol) для framebuffer
arch_devices.cpp      - Обнаружение устройств через DT/ACPI
arch_debug.cpp        - Early console через UART или GOP
```

**arch_entry.cpp ключевые функции:**
```cpp
efi_status efi_main(efi_handle ImageHandle, efi_system_table *SystemTable) {
    // 1. Инициализировать консоль (Serial или GOP)
    // 2. Распарсить Device Tree или ACPI tables
    // 3. Определить memory map через GetMemoryMap()
    // 4. Загрузить ядро из filesystem (LoadImage)
    // 5. Настроить kernel_args структуру
    // 6. Перейти в ядро с ExitBootServices()
}

void arch_mmu_init() {
    // Настроить identity mapping для loader
    // Настроить kernel mapping в upper half
    // Настроить TTBR0_EL1 и TTBR1_EL1
}
```

**2. Device Tree (FDT) Parser**

**Создать:** `src/system/kernel/lib/fdt/` (можно использовать libfdt)

Файлы:
```
fdt.cpp             - Wrapper для libfdt
fdt_serial.cpp      - Парсинг /chosen/stdout-path, /soc/uart@*
fdt_memory.cpp      - Парсинг /memory@*
fdt_cpu.cpp         - Парсинг /cpus/cpu@*
fdt_pci.cpp         - Парсинг /soc/pcie@*
fdt_gic.cpp         - Парсинг /soc/interrupt-controller@*
```

**fdt.cpp ключевые функции:**
```cpp
class FDTParser {
public:
    status_t Init(void* dtb);
    int FindNode(const char* path);
    const void* GetProperty(int node, const char* name, int* len);
    uint64 ReadReg(int node, int index, uint64* size);
    uint32 ReadU32(int node, const char* prop, int index);
    const char* ReadString(int node, const char* prop);
    status_t IterateChildren(int parent_node, 
        status_t (*callback)(int node, void* cookie), void* cookie);
};
```

**Интеграция в arch_platform.cpp:**
```cpp
// Добавить в arch_platform_init():
status_t arch_platform_init(kernel_args *args) {
    gFDT = args->arch_args.fdt;
    
    // NEW: Parse FDT
    gFDTParser = new FDTParser();
    gFDTParser->Init(gFDT);
    
    // Parse UART
    parse_fdt_serial();
    
    // Parse memory
    parse_fdt_memory();
    
    // Parse GIC
    parse_fdt_gic();
    
    // Parse PCI
    parse_fdt_pci();
    
    return B_OK;
}
```

**3. GICv3 Driver**

**Создать:** `src/system/kernel/arch/arm64/arch_int_gicv3.cpp`

```cpp
class GICv3InterruptController : public InterruptController {
public:
    status_t Init(addr_t dist_base, addr_t redist_base);
    void EnableInterrupt(int32 irq) override;
    void DisableInterrupt(int32 irq) override;
    void HandleInterrupt() override;
    void SendIPI(int32 target_cpu, int32 ipi);
    
private:
    addr_t fDistBase;     // GICD_ registers
    addr_t fRedistBase;   // GICR_ registers per-CPU
    
    void WriteDistReg(uint32 offset, uint32 value);
    uint32 ReadDistReg(uint32 offset);
    void WriteRedistReg(int cpu, uint32 offset, uint32 value);
    
    // ICC_* system register access через MSR/MRS
    void WriteICCSGI1R(uint64 value) {
        asm volatile("msr ICC_SGI1R_EL1, %0" :: "r"(value));
    }
    
    uint32 ReadICCIAR1() {
        uint64 val;
        asm volatile("mrs %0, ICC_IAR1_EL1" : "=r"(val));
        return val & 0xFFFFFF;
    }
    
    void WriteICCEOIR1(uint32 irq) {
        asm volatile("msr ICC_EOIR1_EL1, %0" :: "r"((uint64)irq));
    }
};

// Инициализация в arch_int_init_post_vm():
if (gFDTParser->FindNode("/soc/interrupt-controller@*")) {
    uint64 dist_base, redist_base;
    // Read from FDT
    gIC = new GICv3InterruptController();
    gIC->Init(dist_base, redist_base);
}
```

---

## Детальные задачи для Claude Code

### Задача 1: Завершить arch_thread.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_thread.cpp`

**Референс:** Посмотреть `src/system/kernel/arch/riscv64/arch_thread.cpp` и `src/system/kernel/arch/x86/arch_thread.cpp`

**Добавить:**

1. **arch_thread_context_switch** - переключение контекста:
```cpp
void arch_thread_context_switch(Thread *from, Thread *to) {
    // Уже есть _arch_context_swap в arch_asm.S
    // Просто вызвать:
    _arch_context_swap(&from->arch_info.context, &to->arch_info.context);
    
    // Переключить ASID если нужно
    if (from->team != to->team) {
        uint64 ttbr0 = to->team->arch_info.pgdir_phys;
        // Добавить ASID в биты [63:48] если используется
        asm("msr ttbr0_el1, %0; isb" :: "r"(ttbr0));
        // TLB invalidation по ASID
    }
}
```

2. **arch_thread_init_kthread_stack** - инициализация стека:
```cpp
void arch_thread_init_kthread_stack(Thread* thread, void* stack,
    void* stackTop, void (*function)(void*), const void* data)
{
    addr_t *sp = (addr_t*)stackTop;
    
    // ARM64 AAPCS: стек должен быть 16-byte aligned
    // Положить аргумент (data) в x0 позицию
    // Положить function address в LR (x30) позицию
    // _arch_context_swap восстановит их
    
    // Смотри как это сделано в arch_asm.S _arch_context_swap
    // Нужно заполнить структуру arm64_context
}
```

3. **arch_thread_enter_userspace** - вход в userspace:
```cpp
void arch_thread_enter_userspace(Thread *thread, addr_t entry,
    void *args1, void *args2)
{
    // Создать iframe на стеке
    iframe *frame = /* allocate on kernel stack */;
    
    frame->elr = entry;        // User PC
    frame->sp = thread->user_stack_base + thread->user_stack_size;
    frame->spsr = 0;           // User mode EL0t, interrupts enabled
    frame->x[0] = (uint64)args1;
    frame->x[1] = (uint64)args2;
    
    // Jump to arch_asm.S return_to_userspace
    // которое сделает ERET
}
```

4. **Signal handling** - установка signal frame:
```cpp
status_t arch_setup_signal_frame(Thread *thread, struct sigaction *sa,
    struct signal_frame_data *data)
{
    // Посмотреть x86/arm implementation
    // Положить на user stack:
    // - сохраненные регистры
    // - siginfo_t
    // - ucontext_t
    // Установить PC на signal handler
    // Установить LR на signal trampoline (в commpage)
}
```

### Задача 2: Завершить arch_cpu.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_cpu.cpp`

**Референс:** `src/system/kernel/arch/riscv64/arch_cpu.cpp`

**Добавить:**

1. **TLB invalidation:**
```cpp
void arch_cpu_invalidate_TLB_range(addr_t start, addr_t end) {
    // Использовать TLBI VAE1 для каждой страницы
    for (addr_t va = start; va < end; va += B_PAGE_SIZE) {
        uint64 page = va >> 12;
        asm volatile(
            "dsb ishst\n"
            "tlbi vae1, %0\n"
            "dsb ish\n"
            "isb"
            :: "r"(page));
    }
}

void arch_cpu_global_TLB_invalidate() {
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb");
}
```

2. **Cache management:**
```cpp
void arch_cpu_sync_icache(void *address, size_t len) {
    // DC CVAU (clean to PoU)
    // IC IVAU (invalidate I-cache)
    // DSB + ISB
    
    uint64 ctr;
    asm("mrs %0, ctr_el0" : "=r"(ctr));
    size_t dcache_line = 4 << (ctr & 0xF);
    size_t icache_line = 4 << ((ctr >> 16) & 0xF);
    
    addr_t start = (addr_t)address & ~(dcache_line - 1);
    addr_t end = ((addr_t)address + len + dcache_line - 1) & ~(dcache_line - 1);
    
    for (addr_t va = start; va < end; va += dcache_line)
        asm("dc cvau, %0" :: "r"(va));
    
    asm("dsb ish");
    
    for (addr_t va = start; va < end; va += icache_line)
        asm("ic ivau, %0" :: "r"(va));
    
    asm("dsb ish; isb");
}
```

3. **CPU idle:**
```cpp
void arch_cpu_idle() {
    // Включить interrupts и ждать
    asm volatile(
        "msr daifclr, #2\n"  // Enable IRQ
        "wfi\n"              // Wait for interrupt
        "msr daifset, #2"    // Disable IRQ
        ::: "memory");
}
```

4. **Barriers:**
```cpp
void arch_cpu_memory_read_barrier() {
    asm volatile("dsb ld" ::: "memory");
}

void arch_cpu_memory_write_barrier() {
    asm volatile("dsb st" ::: "memory");
}
```

### Задача 3: Завершить arch_timer.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_timer.cpp`

**Референс:** Уже частично есть, завершить

**Реализация ARM Generic Timer:**

```cpp
static uint64 sTimerFrequency;

status_t arch_timer_init(kernel_args *args) {
    // Прочитать частоту таймера
    asm("mrs %0, cntfrq_el0" : "=r"(sTimerFrequency));
    
    dprintf("ARM Generic Timer: frequency %llu Hz\n", sTimerFrequency);
    
    // Зарегистрировать interrupt handler для PPI 30 (physical timer)
    install_io_interrupt_handler(30, arch_timer_interrupt, NULL, 0);
    
    // Включить таймер
    uint64 ctl = 1; // ENABLE bit
    asm("msr cntp_ctl_el1, %0" :: "r"(ctl));
    
    return B_OK;
}

void arch_timer_set_hardware_timer(bigtime_t timeout) {
    // timeout в микросекундах
    // Конвертировать в тики таймера
    uint64 ticks = (timeout * sTimerFrequency) / 1000000;
    
    // Прочитать текущий счетчик
    uint64 now;
    asm("mrs %0, cntpct_el0" : "=r"(now));
    
    // Установить compare value
    uint64 compare = now + ticks;
    asm("msr cntp_cval_el0, %0" :: "r"(compare));
}

bigtime_t system_time() {
    uint64 counter;
    asm("mrs %0, cntpct_el0" : "=r"(counter));
    
    // Конвертировать в микросекунды
    return (counter * 1000000) / sTimerFrequency;
}

int32 arch_timer_interrupt(void *data) {
    // Отключить таймер
    asm("msr cntp_ctl_el1, xzr");
    
    // Вызвать scheduler timer
    return timer_interrupt();
}
```

### Задача 4: Реализовать arch_smp.cpp с PSCI

**Файл:** `src/system/kernel/arch/arm64/arch_smp.cpp`

**КРИТИЧНО: сейчас только заглушки!**

```cpp
// PSCI function IDs
#define PSCI_CPU_ON    0xC4000003
#define PSCI_CPU_OFF   0x84000002

static int64 psci_call(uint32 function, uint64 arg0, uint64 arg1, uint64 arg2) {
    register uint64 x0 asm("x0") = function;
    register uint64 x1 asm("x1") = arg0;
    register uint64 x2 asm("x2") = arg1;
    register uint64 x3 asm("x3") = arg2;
    
    // Вызов через SMC или HVC зависит от EL
    // Определить из FDT /psci/method = "smc" или "hvc"
    asm volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3));
    
    return x0;
}

status_t arch_smp_init(kernel_args *args) {
    // Парсим CPU из FDT
    int cpus_node = gFDTParser->FindNode("/cpus");
    
    for (int cpu = 1; cpu < args->num_cpus; cpu++) {
        // Получить MPIDR для каждого CPU из FDT
        uint64 mpidr = /* parse from DT */;
        
        // Выделить стек для CPU
        void *stack = memalign(16, KERNEL_STACK_SIZE);
        gCPU[cpu].arch.stack_top = (addr_t)stack + KERNEL_STACK_SIZE;
        
        // Запустить CPU через PSCI
        int64 result = psci_call(PSCI_CPU_ON, mpidr,
            (addr_t)&_start_secondary_cpu, // из arch_start.S
            (addr_t)&gCPU[cpu]);
        
        if (result != 0) {
            dprintf("Failed to start CPU %d: %lld\n", cpu, result);
        }
    }
    
    return B_OK;
}

void arch_smp_send_ici(int32 target_cpu) {
    // Использовать GIC SGI (Software Generated Interrupt)
    // Через GICv3: ICC_SGI1R_EL1 system register
    
    uint64 mpidr = gCPU[target_cpu].arch.mpidr;
    uint64 aff3 = (mpidr >> 32) & 0xFF;
    uint64 aff2 = (mpidr >> 16) & 0xFF;
    uint64 aff1 = (mpidr >> 8) & 0xFF;
    uint64 aff0 = mpidr & 0xFF;
    
    // Сформировать SGI1R value
    // INTID в битах [27:24], target list в битах [15:0]
    uint64 sgi1r = (0ULL << 24) |  // SGI 0 для IPI
                   (aff3 << 48) |
                   (aff2 << 32) |
                   (aff1 << 16) |
                   (1UL << aff0);  // Target CPU bitmap
    
    asm("msr ICC_SGI1R_EL1, %0" :: "r"(sgi1r));
}
```

**Добавить в arch_start.S:**
```asm
.globl _start_secondary_cpu
_start_secondary_cpu:
    // X0 = cpu_ent*
    
    // Load stack
    ldr x1, [x0, #CPU_ENT_STACK_TOP_OFFSET]
    mov sp, x1
    
    // Copy system registers from primary CPU
    // (TCR_EL1, MAIR_EL1, SCTLR_EL1, TTBR1_EL1)
    
    // Enable MMU
    mrs x1, sctlr_el1
    orr x1, x1, #1  // M bit
    msr sctlr_el1, x1
    isb
    
    // Initialize GIC CPU interface for this CPU
    bl gicv3_init_secondary
    
    // Call C++ secondary init
    bl arch_cpu_secondary_init
    
    // Should not return
1:  wfi
    b 1b
```

### Задача 5: Завершить syscalls.cpp

**Файл:** `src/system/kernel/arch/arm64/syscalls.cpp`

**Текущее состояние:** заглушка, возвращает B_ERROR

**Завершить:**

```cpp
// arch_int.cpp уже обрабатывает SVC в do_sync_handler
// Нужно завершить handle_syscall:

extern "C" void handle_syscall(iframe* frame) {
    Thread* thread = thread_get_current_thread();
    
    // Syscall number в x8 (frame->x[8])
    uint32 syscall = frame->x[8];
    
    // Аргументы в x0-x7
    uint64 args[8];
    for (int i = 0; i < 8; i++)
        args[i] = frame->x[i];
    
    // Включить interrupts
    arch_int_enable_interrupts();
    
    // Вызвать generic dispatcher
    uint64 return_value;
    syscall_dispatcher(syscall, (void*)args, &return_value);
    
    // Отключить interrupts
    arch_int_disable_interrupts();
    
    // Return value в x0
    frame->x[0] = return_value;
    
    // Check for signals/reschedule
    if ((thread->flags & THREAD_FLAGS_SIGNALS_PENDING) != 0) {
        // Handle signals before returning to userspace
    }
    
    if (thread->cpu->invoke_scheduler) {
        scheduler_reschedule();
    }
}
```

### Задача 6: Реализовать arch_commpage.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_commpage.cpp`

**Референс:** `src/system/kernel/arch/x86/arch_commpage.cpp`

```cpp
static addr_t sCommPageAddress;

status_t arch_commpage_init() {
    // Выделить область для commpage
    area_id area = create_area("commpage", (void**)&sCommPageAddress,
        B_ANY_KERNEL_ADDRESS, COMMPAGE_SIZE,
        B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
    
    if (area < 0)
        return area;
    
    // Очистить страницу
    memset((void*)sCommPageAddress, 0, COMMPAGE_SIZE);
    
    // Заполнить commpage данными
    // - system_time() vDSO
    // - signal trampolines
    // - atomic helpers
    
    return B_OK;
}

addr_t arch_get_commpage_address() {
    return sCommPageAddress;
}

status_t arch_commpage_remap() {
    // Переотобразить commpage в user space
    // По адресу COMMPAGE_ADDRESS (определен в headers)
    
    return B_OK;
}
```

### Задача 7: Реализовать arch_real_time_clock.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_real_time_clock.cpp`

**Текущее состояние:** пустые заглушки

```cpp
status_t arch_rtc_init(kernel_args *args, real_time_data *data) {
    // ARM Generic Timer как источник времени
    // Уже реализовано в arch_timer.cpp
    
    // Установить conversion factor для user space
    uint64 freq;
    asm("mrs %0, cntfrq_el0" : "=r"(freq));
    
    data->arch_data.system_time_conversion_factor =
        (1LL << 32) * 1000000LL / freq;
    
    return B_OK;
}

uint32 arch_rtc_get_hw_time() {
    // Прочитать текущее время из Generic Timer
    uint64 counter;
    asm("mrs %0, cntpct_el0" : "=r"(counter));
    
    uint64 freq;
    asm("mrs %0, cntfrq_el0" : "=r"(freq));
    
    return (uint32)(counter / freq); // секунды
}

void arch_rtc_set_hw_time(uint32 seconds) {
    // ARM Generic Timer read-only, RTC может быть отдельное устройство
    // Пока ничего не делаем
}

bigtime_t arch_rtc_get_system_time_offset(real_time_data *data) {
    return atomic_get64(&data->arch_data.system_time_offset);
}

void arch_rtc_set_system_time_offset(real_time_data *data, bigtime_t offset) {
    atomic_set64(&data->arch_data.system_time_offset, offset);
}
```

### Задача 8: Реализовать arch_system_info.cpp

**Файл:** `src/system/kernel/arch/arm64/arch_system_info.cpp`

```cpp
status_t arch_get_system_info(system_info *info, size_t size) {
    // Заполнить CPU info
    // Использовать данные из arch_kernel_start.cpp (CPU detection)
    
    info->cpu_type = B_CPU_ARM_64;
    info->cpu_count = smp_get_num_cpus();
    info->cpu_clock_speed = /* from Generic Timer freq */;
    
    return B_OK;
}

void arch_fill_topology_node(cpu_topology_node_info* node, int32 cpu) {
    switch (node->type) {
    case B_TOPOLOGY_ROOT:
        node->data.root.platform = B_CPU_ARM_64;
        break;
    case B_TOPOLOGY_PACKAGE:
        node->data.package.vendor = /* detect from MIDR */;
        node->data.package.cache_line_size = /* detect from CTR_EL0 */;
        break;
    case B_TOPOLOGY_CORE:
        node->data.core.model = /* from MIDR */;
        node->data.core.default_frequency = /* from timer */;
        break;
    }
}

status_t arch_system_info_init(kernel_args *args) {
    // Initialize topology detection
    return B_OK;
}

status_t arch_get_frequency(uint64 *frequency, int32 cpu) {
    // Read from Generic Timer
    asm("mrs %0, cntfrq_el0" : "=r"(*frequency));
    return B_OK;
}
```

---

## Порядок выполнения задач для Claude Code

**Фаза 1: Критические компоненты (без них система не загрузится)**
1. ✅ Завершить `arch_timer.cpp` - нужен для system_time()
2. ✅ Завершить `arch_cpu.cpp` - нужен для TLB/cache management
3. ✅ Завершить `syscalls.cpp` - нужен для userspace
4. ✅ Завершить `arch_thread.cpp` - нужен для многозадачности

**Фаза 2: SMP поддержка**
5. ✅ Реализовать `arch_smp.cpp` с PSCI
6. ✅ Добавить GICv3 driver (`arch_int_gicv3.cpp`)
7. ✅ Добавить secondary CPU entry в `arch_start.S`

**Фаза 3: Device Tree и платформа**
8. ✅ Интегрировать libfdt parser
9. ✅ Дополнить `arch_platform.cpp` парсингом FDT
10. ✅ Реализовать `arch_real_time_clock.cpp`
11. ✅ Реализовать `arch_system_info.cpp`

**Фаза 4: Userspace support**
12. ✅ Реализовать `arch_commpage.cpp` с vDSO
13. ✅ Завершить signal handling в `arch_thread.cpp`
14. ✅ Дополнить `arch_debug.cpp` и `arch_user_debugger.cpp`

**Фаза 5: Bootloader**
15. ✅ Создать EFI bootloader для ARM64
16. ✅ Интегрировать с kernel handoff

**Фаза 6: Дополнительные драйверы**
17. ⚠️ PCI/PCIe support
18. ⚠️ Additional UART drivers
19. ⚠️ Framebuffer/video support

---

## Важные ссылки на существующий код для reference

**Для каждой задачи смотреть:**
- **RISCV64** реализацию (самая свежая, похожая архитектура)
  - `src/system/kernel/arch/riscv64/arch_*.cpp`
- **x86_64** реализацию (наиболее полная)
  - `src/system/kernel/arch/x86/arch_*.cpp`
- **ARM 32-bit** реализацию (родственная архитектура)
  - `src/system/kernel/arch/arm/arch_*.cpp`

**Ключевые существующие ARM64 файлы для понимания:**
- `arch_exceptions.cpp` - отличный пример работы с system registers
- `arch_int.cpp` - обработка exceptions/interrupts
- `VMSAv8TranslationMap.cpp` - page table management
- `arch_asm.S` - assembly helpers

---

## Структура для Claude Code в VSCode

**Для каждого файла предоставить:**

1. **Текущее состояние** - что уже реализовано
2. **Что нужно добавить** - конкретные функции
3. **Референсы** - какие файлы смотреть для примера
4. **Тесты** - как проверить что работает

**Пример задачи для Claude Code:**

```
FILE: src/system/kernel/arch/arm64/arch_timer.cpp
STATUS: Partially implemented, missing interrupt handler
TODO:
  - Complete arch_timer_interrupt() function
  - Add proper CNTPCT_EL0 reading in system_time()
  - Register timer interrupt (PPI 30)
REFERENCE:
  - src/system/kernel/arch/riscv64/arch_timer.cpp
  - ARM Architecture Reference Manual, section D13
TEST:
  - Boot kernel, check dprintf() timestamps increase
  - Run timer_test from userspace
```