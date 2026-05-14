# Haiku Scheduler — Полная карта кодовой базы

## Ядро планировщика (`src/system/kernel/scheduler/`)

| Файл | Строк | Назначение |
|------|-------|------------|
| **scheduler.cpp** | ~875 | Главная реализация: `scheduler_enqueue_in_run_queue()`, `scheduler_reschedule()`, `scheduler_set_thread_priority()`, переключение режимов, инициализация |
| **scheduler_cpu.cpp** | ~823 | Управление CPUEntry/CoreEntry/PackageEntry: отслеживание нагрузки, таймеры квантов, heap-операции для CPU, обнаружение idle, выбор потока |
| **scheduler_thread.cpp** | ~337 | ThreadData: управление квантами, расчёт штрафов (penalty), вычисление нагрузки, расчёт приоритетов, назначение ядер, CPU pinning |
| **scheduler_profiler.cpp** | ~323 | Инфраструктура профилирования вызовов функций планировщика |
| **scheduler_tracing.cpp** | ~316 | Трассировка: EnqueueThread, RemoveThread, ScheduleThread |
| **scheduling_analysis.cpp** | ~879 | Глубокий анализ: время выполнения потоков, задержки, анализ объектов ожидания, отслеживание вытеснения |
| **low_latency.cpp** | ~198 | Режим низкой задержки: будит свежие пакеты, приоритет кэш-локальности, выбирает наименее нагруженные ядра |
| **power_saving.cpp** | ~280 | Режим энергосбережения: упаковка потоков на меньшее число ядер, оптимизация мелких задач |
| **scheduler_load.cpp** | ~62 | Усреднение и отслеживание нагрузки |

## Внутренние заголовки (`src/system/kernel/scheduler/`)

| Файл | Строк | Назначение |
|------|-------|------------|
| **scheduler_cpu.h** | ~599 | Классы CPUEntry, CoreEntry, PackageEntry; иерархия CPU/Core/Package; управление run queue; heap нагрузки |
| **scheduler_thread.h** | ~501 | Структура ThreadData: штрафы приоритетов, кванты, таймеры сна/пробуждения, назначение ядер |
| **scheduler_modes.h** | ~48 | Структура `scheduler_mode_operations`: mode-specific операции (choose_core, rebalance, cache expiry) |
| **scheduler_common.h** | ~57 | Константы нагрузки: kLowLoad, kTargetLoad, kHighLoad; пространство имён; отладка |
| **scheduler_locking.h** | ~161 | Абстракции блокировок: CPURunQueueLocker, CoreRunQueueLocker, CoreCPUHeapLocker, SchedulerModeLocker |
| **scheduler_profiler.h** | ~136 | Макросы SCHEDULER_PROFILING, классы профилирования |
| **scheduler_tracing.h** | ~153 | Классы трассировки: SchedulerTraceEntry, EnqueueThread, RemoveThread, ScheduleThread |
| **RunQueue.h** | ~397 | Шаблонная приоритетная очередь: heap-сортировка, O(log n) вставка, 128 уровней приоритета |

## Публичные/приватные API заголовки

| Файл | Путь | Назначение |
|------|------|------------|
| **kscheduler.h** | `headers/private/kernel/` | Kernel-internal API: enqueue, reschedule, set priority, lifecycle потока (create/init/destroy), startup, mode setting |
| **scheduler.h** | `headers/os/kernel/` | User-facing API: `suggest_thread_priority()`, `estimate_max_scheduling_latency()`, `set_scheduler_mode()`, `get_scheduler_mode()` |
| **scheduler_defs.h** | `headers/private/system/` | Структуры данных: `scheduling_analysis_thread`, `scheduling_analysis_wait_object`, `loadavg`, `scheduling_analysis` |
| **load_tracking.h** | `headers/private/kernel/` | Вычисление нагрузки: `compute_load()`, константы kMaxLoad, kLoadMeasureInterval |
| **scheduling_analysis.h** | `headers/private/kernel/` | Публичный интерфейс анализа |

## Userspace API (`libroot`)

| Файл | Назначение |
|------|------------|
| **src/system/libroot/os/scheduler.c** | Реализация userspace API: `suggest_thread_priority()` (таблица приоритетов для медиа-задач), `estimate_max_scheduling_latency()` (syscall `_kern_estimate_max_scheduling_latency`), `set_scheduler_mode()` / `get_scheduler_mode()` (syscalls `_kern_set/get_scheduler_mode`) |
| **src/system/libroot/stubbed/libroot_stubs.c** | Заглушки scheduler-функций для сборки host-инструментов |

## Потребители scheduler API (приложения и add-ons)

### Power Management (вызывают `set_scheduler_mode` / `get_scheduler_mode`)

| Файл | Назначение |
|------|------------|
| **src/add-ons/kernel/power/cpufreq/intel_pstates/intel_pstates.cpp** | Intel P-States: адаптирует частоту CPU на основе режима планировщика |
| **src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp** | AMD P-States: адаптирует частоту CPU на основе режима планировщика |
| **src/add-ons/kernel/power/cpuidle/x86_cstates/x86_cstates.cpp** | x86 C-States: управление idle-состояниями CPU |
| **src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp** | ACPI C-States: ACPI-based idle управление |

### Приложения и медиа-киты (вызывают `suggest_thread_priority` / `estimate_max_scheduling_latency`)

| Файл | Назначение |
|------|------------|
| **src/apps/processcontroller/ProcessController.cpp** | ProcessController: управление приоритетами потоков |
| **src/apps/codycam/CodyCam.cpp** | Камера: приоритет для видеозахвата |
| **src/apps/codycam/VideoConsumer.cpp** | Камера: приоритет видеопотребителя |
| **src/apps/showimage/Filter.cpp** | ShowImage: приоритет для обработки изображений |
| **src/apps/mediaplayer/media_node_framework/NodeManager.cpp** | MediaPlayer: приоритеты медиа-нод |
| **src/apps/mediaplayer/media_node_framework/video/VideoConsumer.cpp** | MediaPlayer: приоритет видеопотребителя |
| **src/kits/media/MediaEventLooper.cpp** | Media Kit: приоритет event loop |
| **src/kits/media/MediaRecorderNode.cpp** | Media Kit: приоритет записи |
| **src/kits/media/experimental/MediaClientNode.cpp** | Media Kit: приоритет клиентских нод |
| **src/kits/game/FileGameSound.cpp** | Game Kit: приоритет для воспроизведения звуков |
| **src/add-ons/media/media-add-ons/opensound/OpenSoundNode.cpp** | OpenSound media add-on: приоритет аудио-ноды |

### Заголовки-агрегаторы (включают `scheduler.h`)

| Файл | Назначение |
|------|------------|
| **headers/os/KernelKit.h** | Umbrella-заголовок Kernel Kit — экспортирует scheduler API всем потребителям KernelKit |
| **headers/os/drivers/cpufreq.h** | API драйверов масштабирования частоты CPU |
| **headers/os/drivers/cpuidle.h** | API драйверов idle-состояний CPU |

### Тесты

| Файл | Назначение |
|------|------------|
| **src/tests/system/kernel/scheduler/main.cpp** | Тесты планировщика ядра |
| **src/tests/add-ons/kernel/file_systems/bfs/btree/stubs.cpp** | BFS-тесты: заглушки планировщика |

## Ключевые точки интеграции в ядре

| Файл | Роль |
|------|------|
| `src/system/kernel/thread.cpp` | Вызывает: `scheduler_on_thread_create()`, `scheduler_on_thread_init()`, `scheduler_enqueue_in_run_queue()`, `scheduler_set_thread_priority()` |
| `headers/private/kernel/thread.h` | Определяет struct Thread с `scheduler_lock` и полями планировщика; включает `kscheduler.h` |
| `headers/private/kernel/cpu.h` | Структура CPU содержит флаг `invoke_scheduler` для сигнализации вытеснения; включает `scheduler.h` |
| `src/system/kernel/cpu.cpp` | Вызывает `scheduler_set_cpu_enabled()` при включении/отключении CPU |
| `src/system/kernel/main.cpp` | Инициализация планировщика при загрузке ядра |
| `src/system/kernel/timer.cpp` | Прерывания таймера запускают `scheduler_reschedule()` при истечении кванта |
| `src/system/kernel/interrupts.cpp` | Обработчик прерываний проверяет `invoke_scheduler` и вызывает `scheduler_reschedule()` |
| `src/system/kernel/team.cpp` | Создание процессов интегрируется с инициализацией планировщика потоков |
| `src/system/kernel/signal.cpp` | Доставка сигналов вызывает функции планировщика для пересчёта приоритетов |
| `src/system/kernel/sem.cpp` | Операции семафоров взаимодействуют с планировщиком для готовности потоков |
| `src/system/kernel/condition_variable.cpp` | Пробуждение потоков при изменении condition variable |
| `src/system/kernel/kosm_mutex.cpp` | Операции мьютексов вызывают функции планировщика для изменения состояния потоков |
| `src/system/kernel/image.cpp` | Управление образами с использованием планировщика |
| `src/system/kernel/UserTimer.cpp` | Пользовательские таймеры взаимодействуют с планировщиком |
| `src/system/kernel/debug/user_debugger.cpp` | Отладчик использует kscheduler для управления потоками при отладке |
| `src/system/kernel/debug/system_profiler.cpp` | Системный профайлер: сэмплирование потоков через планировщик |
| `src/add-ons/kernel/debugger/invalidate_on_exit/invalidate_on_exit.cpp` | Kernel debugger add-on: инвалидация при выходе |

## Архитектурно-специфичный код

### Обработчики прерываний (вызывают `scheduler_reschedule()`)

| Файл | Архитектура |
|------|-------------|
| `src/system/kernel/arch/x86/arch_int.cpp` | x86/x86_64 |
| `src/system/kernel/arch/arm64/arch_int.cpp` | ARM64 |
| `src/system/kernel/arch/arm/arch_int.cpp` | ARM |
| `src/system/kernel/arch/riscv64/arch_int.cpp` | RISC-V |

### Таймеры (запускают истечение кванта)

| Файл | Архитектура |
|------|-------------|
| `src/system/kernel/arch/x86/arch_timer.cpp` | x86 |
| `src/system/kernel/arch/x86/timers/x86_apic.cpp` | x86 — APIC таймер |
| `src/system/kernel/arch/x86/timers/x86_hpet.cpp` | x86 — HPET таймер |
| `src/system/kernel/arch/x86/timers/x86_pit.cpp` | x86 — PIT таймер |
| `src/system/kernel/arch/arm64/arch_timer.cpp` | ARM64 — Generic Timer |
| `src/system/kernel/arch/arm/arch_timer.cpp` | ARM |
| `src/system/kernel/arch/arm/arch_timer_generic.cpp` | ARM Generic Timer |
| `src/system/kernel/arch/arm/soc_pxa.cpp` | ARM SoC PXA |
| `src/system/kernel/arch/arm/soc_omap3.cpp` | ARM SoC OMAP3 |
| `src/system/kernel/arch/riscv64/arch_timer.cpp` | RISC-V |

### Управление CPU/SMP

| Файл | Архитектура |
|------|-------------|
| `src/system/kernel/arch/x86/arch_smp.cpp` | x86 — инициализация SMP |
| `src/system/kernel/arch/x86/arch_cpu.cpp` | x86 — состояние CPU, масштабирование частоты |
| `src/system/kernel/arch/x86/64/thread.cpp` | x86_64 — операции потоков |
| `src/system/kernel/arch/arm64/arch_smp.cpp` | ARM64 — SMP |
| `src/system/kernel/arch/arm64/arch_cpu.cpp` | ARM64 — CPU |
| `src/system/kernel/arch/arm/arch_smp.cpp` | ARM — SMP |
| `src/system/kernel/arch/arm/arch_cpu.cpp` | ARM — CPU |
| `src/system/kernel/arch/riscv64/arch_smp.cpp` | RISC-V — SMP |
| `src/system/kernel/arch/riscv64/arch_cpu.cpp` | RISC-V — CPU |

## Система сборки

| Файл | Роль |
|------|------|
| `src/system/kernel/Jamfile` | Включает исходники планировщика (строки ~70-80): scheduler.cpp, scheduler_cpu.cpp, scheduler_load.cpp, scheduler_profiler.cpp, scheduler_thread.cpp, scheduler_tracing.cpp, scheduling_analysis.cpp |

## Архитектура планировщика

### Иерархия планирования

```
Package (физический сокет CPU)
  ├─ Core (физическое/логическое ядро)
  │   ├─ CPU (логический процессор)
  │   │   └─ RunQueue (приоритетная очередь потоков, 128 уровней)
  │   └─ RunQueue (общая очередь ядра)
  └─ IdleList (idle ядра, ожидающие пробуждения)
```

### Режимы планирования

- **SCHEDULER_MODE_LOW_LATENCY** — приоритет отзывчивости: будит свежие пакеты, использует наименее нагруженные ядра
- **SCHEDULER_MODE_POWER_SAVING** — приоритет энергосбережения: упаковка потоков на меньшее число ядер, пакеты в idle

### Ключевые операции

1. **Enqueue** (`scheduler_enqueue_in_run_queue`) — добавление готового потока в run queue
2. **Reschedule** (`scheduler_reschedule`) — выбор следующего потока, переключение контекста
3. **Rebalance** — перемещение потоков между ядрами (mode-specific)
4. **Quantum Expiry** — вытеснение по таймеру после истечения кванта времени
5. **Priority Adjustment** — динамический штраф для голодающих потоков
6. **CPU Affinity** — соблюдение маски CPU для потока
7. **Cache Locality** — отслеживание привязки потока к ядру, истечение через 100ms idle

### Стратегия блокировок

- Per-CPU read/write lock режима планировщика (read для быстрого пути, write для смены режима)
- Per-CPU/Core spinlock очереди выполнения
- Per-Core spinlock heap CPU (выбор ядра)
- Per-Core read/write lock отслеживания нагрузки
- Глобальный read/write lock списка idle пакетов

### Отслеживание нагрузки

- Вычисление в реальном времени: active time / interval
- Пороги: Low (20%), Target (55%), High (70%), Very High (85%), Medium (62.5%)
- Min-Max heap ядер по нагрузке (gCoreLoadHeap, gCoreHighLoadHeap)
- Per-thread "needed load" на основе приоритета и длительности сна

### Приоритеты потоков

- Диапазон: B_IDLE_PRIORITY ... B_URGENT_DISPLAY_PRIORITY
- Real-time потоки: >= B_FIRST_REAL_TIME_PRIORITY
- Штрафы приоритетов: временное снижение эффективного приоритета голодающих потоков
- CPU pinning: `pinned_to_cpu > 0` принудительная привязка к одному CPU
- CPU mask: поток может указать допустимые CPU через маску аффинности

### Цепочка вызовов (пример: прерывание таймера)

```
timer_interrupt (arch/*/arch_timer.cpp)
  → interrupt_handler (arch/*/arch_int.cpp)
    → scheduler_reschedule (scheduler/scheduler.cpp)
      → ThreadData::ChooseCoreAndCPU()
      → gCurrentMode->rebalance()  [mode-specific]
      → CPUEntry::ChooseNextThread()
      → context_switch()  [arch-specific]
```

## Статистика

- **Ядро планировщика**: ~6,145 строк (23 файла в scheduler/)
- **Основная реализация**: scheduler.cpp (~875) + scheduler_cpu.cpp (~823) ≈ 1,698 строк
- **Инфраструктура заголовков**: ~1,500+ строк
- **Анализ и трассировка**: ~1,500+ строк
- **Userspace API**: scheduler.c (~84 строки) + заглушки
- **Потребители**: 4 power add-ons + 9 приложений/медиа-компонентов + 3 заголовка-агрегатора + 2 теста
- **Интеграция в ядре**: thread.cpp, timer.cpp, interrupts.cpp, cpu.cpp, main.cpp, condition_variable.cpp, image.cpp, signal.cpp, sem.cpp, team.cpp, kosm_mutex.cpp, UserTimer.cpp, debug/user_debugger.cpp, debug/system_profiler.cpp, debugger add-on, и все arch/*/arch_int.cpp + arch/*/arch_timer.cpp
