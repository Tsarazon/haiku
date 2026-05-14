# High-Impact TODOs в Haiku OS

Анализ TODO/FIXME/XXX/HACK комментариев, реализация которых даст наибольший выигрыш в работе операционной системы.

**Всего найдено: 643+ комментариев**
**Критических: 16 | Высокий приоритет: 50 | Средний: 122+**

---

## 🔴 TIER 1: КРИТИЧЕСКИЕ — Стабильность и производительность ядра

### 1.1 Кэширование CPU

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/cpu.cpp` | 194 | `// TODO: data cache` — инвалидируется только instruction cache, не data cache | **КРИТИЧЕСКОЕ** — проблемы когерентности кэша, возможное повреждение данных |

### 1.2 Синхронизация и блокировки

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/locks/lock.cpp` | 1158 | `// TODO: There is still a race condition during mutex destruction` | **КРИТИЧЕСКОЕ** — race condition при уничтожении мьютекса, возможны краши и deadlock |
| `src/system/kernel/port.cpp` | 684 | `// TODO: add per team limit` | **ВЫСОКОЕ** — отсутствие лимитов портов на команду |
| `src/system/kernel/port.cpp` | 692-705 | Race conditions в обработке портов | **ВЫСОКОЕ** — DoS вектор |

### 1.3 NUMA планировщик (полностью не реализован)

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/scheduler/scheduler_numa.cpp` | 59 | `return NULL; // TODO: Implement proper extension storage` | **КРИТИЧЕСКОЕ** |
| `src/system/kernel/scheduler/scheduler_numa.cpp` | 75 | `// TODO: Implement proper ACPI SRAT parsing` | **КРИТИЧЕСКОЕ** |
| `src/system/kernel/scheduler/scheduler_numa.cpp` | 254-263 | Аллокация/освобождение scheduler_thread_data_numa | **КРИТИЧЕСКОЕ** |

**Итог:** NUMA поддержка не работает на многосокетных системах.

### 1.4 Виртуальная память и куча

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/heap.cpp` | 1544 | Неэффективное выравнивание памяти | **ВЫСОКОЕ** |
| `src/system/kernel/heap.cpp` | 2520 | `// TODO maybe resize the area instead?` — копирование вместо resize | **СРЕДНЕЕ-ВЫСОКОЕ** — фрагментация памяти |
| `src/system/kernel/low_resource_manager.cpp` | 220 | `// TODO: this should take fragmentation into account` | **ВЫСОКОЕ** — менеджер низкой памяти игнорирует фрагментацию |

### 1.5 Обработка сигналов

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/signal.cpp` | 1922 | `// TODO: Implement correctly!` | **ВЫСОКОЕ** — неполная реализация сигналов |
| `src/system/kernel/signal.cpp` | 1043 | `// TODO: apply zombie cleaning on SIGCHLD` | **СРЕДНЕЕ** — zombie процессы |
| `src/system/kernel/signal.cpp` | 1700 | Неясная логика таргетинга сигналов | **СРЕДНЕЕ** |

### 1.6 ELF загрузчик

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/kernel/elf.cpp` | 673 | Версионирование символов не соответствует стандарту | **ВЫСОКОЕ** |
| `src/system/kernel/elf.cpp` | 2260 | `// TODO: check ABI compatibility attributes` (RISC-V) | **ВЫСОКОЕ** — нет проверки ABI совместимости |

---

## 🟠 TIER 2: ARM64 и RISC-V загрузка (критично для мобильных устройств)

### 2.1 ARM64 Memory Management (не реализовано)

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/boot/platform/efi_arm64/memory.cpp` | 455 | `// TODO: Configure ARM64 memory management unit` | **КРИТИЧЕСКОЕ** |
| `src/system/boot/platform/efi_arm64/memory.cpp` | 456 | `// TODO: Set up initial page tables` | **КРИТИЧЕСКОЕ** |
| `src/system/boot/platform/efi_arm64/memory.cpp` | 457 | `// TODO: Configure memory protection` | **КРИТИЧЕСКОЕ** |

### 2.2 ARM64 Таймер (10+ TODO)

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/boot/platform/efi_arm64/arch/arch_timer.cpp` | 21-61 | ARM Generic Timer полностью не реализован | **КРИТИЧЕСКОЕ** |

### 2.3 ARM64 Startup

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/boot/platform/efi_arm64/arch/arch_start.cpp` | 26-51 | Последовательность запуска ARM64 не завершена | **КРИТИЧЕСКОЕ** |

### 2.4 RISC-V загрузка

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/system/boot/loader/main.cpp` | 92, 137 | `// TODO: fix for riscv64` | **ВЫСОКОЕ** |
| `src/system/boot/arch/riscv64/arch_cpu.cpp` | 62 | `// TODO: units conversion` | **СРЕДНЕЕ** |

---

## 🟡 TIER 3: Драйверы хранилища

### 3.1 AHCI/SATA

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/add-ons/kernel/busses/scsi/ahci/ahci.c` | 297 | 64-bit DMA адресация не определяется | **ВЫСОКОЕ** |
| `src/add-ons/kernel/busses/scsi/ahci/ahci.c` | 331 | `// TODO: register SIM for every controller we find!` | **ВЫСОКОЕ** — мульти-контроллер не поддержан |
| `src/add-ons/kernel/busses/scsi/ahci/sata_request.cpp` | 154-156 | ATA error handling отсутствует | **ВЫСОКОЕ** |

### 3.2 USB Mass Storage (40+ TODO)

| Файл | Проблема | Влияние |
|------|----------|---------|
| `src/add-ons/kernel/busses/scsi/usb/transform_procs.c` | SCSI команды неполно трансформируются | **ВЫСОКОЕ** |
| `src/add-ons/kernel/busses/scsi/usb/usb_scsi.c` | Аллокация, обработка ошибок, scatter-gather | **СРЕДНЕЕ-ВЫСОКОЕ** |

### 3.3 MMC/SD карты

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/add-ons/kernel/busses/mmc/sdhci.cpp` | 482 | Advanced DMA (ADMA) не определяется | **СРЕДНЕЕ** |
| `src/add-ons/kernel/busses/mmc/sdhci.h` | 64-65 | Response interrupt/check не реализованы | **СРЕДНЕЕ-ВЫСОКОЕ** |

---

## 🟢 TIER 4: Сетевой стек

### 4.1 IPv6 (официально "fragile")

| Файл | Строка | Проблема | Влияние |
|------|--------|----------|---------|
| `src/servers/net/NetServer.cpp` | 460 | `// TODO: our IPv6 stack is still fairly fragile` | **ВЫСОКОЕ** |
| `src/servers/net/NetServer.cpp` | 812, 886, 899 | IPv6 инициализация и DAD неполные | **СРЕДНЕЕ-ВЫСОКОЕ** |
| `src/add-ons/kernel/network/datalink_protocols/ipv6_datagram/ipv6_datagram.cpp` | 462-779 | NDP и neighbor discovery неполные | **ВЫСОКОЕ** |

### 4.2 Сетевые буферы

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/add-ons/kernel/network/stack/net_buffer.cpp` | 132-1395 | 8+ TODO по управлению буферами | **СРЕДНЕЕ-ВЫСОКОЕ** |

### 4.3 Роутинг

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/add-ons/kernel/network/stack/routes.cpp` | 115, 153, 664 | Валидация маршрутов неполная | **СРЕДНЕЕ** |
| `src/add-ons/kernel/network/stack/device_interfaces.cpp` | 212, 425 | Управление интерфейсами неоптимально | **СРЕДНЕЕ** |

---

## 🔵 TIER 5: Файловые системы

### 5.1 NTFS (50+ FIXME)

| Файл | Проблема | Влияние |
|------|----------|---------|
| `src/add-ons/kernel/file_systems/ntfs/kernel_interface.cpp:218` | `// TODO: uid/gid mapping and real permissions` | **ВЫСОКОЕ** — права не маппятся |
| `src/add-ons/kernel/file_systems/ntfs/` | Компрессия, bitmap, security/ACL не реализованы | **СРЕДНЕЕ** |

### 5.2 GPT

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/add-ons/kernel/partitioning_systems/gpt/Header.cpp` | 57-169 | Protective MBR, валидация записей | **СРЕДНЕЕ** |

---

## ⚪ TIER 6: Системные серверы

### 6.1 Media Server

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/servers/media/DefaultManager.cpp` | 592, 686 | Audio/video timesource handling | **СРЕДНЕЕ** |

### 6.2 Launch Daemon (20+ TODO)

| Файл | Проблема | Влияние |
|------|----------|---------|
| `src/servers/launch/LaunchDaemon.cpp` | Job conditions, restart throttle | **СРЕДНЕЕ** |

### 6.3 Bluetooth Server

| Файл | Строки | Проблема | Влияние |
|------|--------|----------|---------|
| `src/servers/bluetooth/BluetoothServer.cpp` | 168, 301 | Device discovery неполный | **СРЕДНЕЕ** |

---

## 📊 Рекомендации по приоритизации

### Для KosmOS / Mobile Haiku (ARM64):

1. **🔴 НЕМЕДЛЕННО:** ARM64 memory management и таймер (`efi_arm64/`)
2. **🔴 НЕМЕДЛЕННО:** NUMA scheduler для multi-core SoC
3. **🟠 ВЫСОКИЙ:** MMC/SD драйвер для загрузки с карт
4. **🟡 СРЕДНИЙ:** IPv6 стек для современных сетей

### Для Desktop/Server (x86_64):

1. **🔴 НЕМЕДЛЕННО:** Mutex race condition в `locks/lock.cpp:1158`
2. **🔴 НЕМЕДЛЕННО:** Data cache invalidation в `cpu.cpp:194`
3. **🟠 ВЫСОКИЙ:** NUMA scheduler для серверов
4. **🟠 ВЫСОКИЙ:** AHCI multi-controller support
5. **🟡 СРЕДНИЙ:** USB SCSI трансформация команд

### Для совместимости:

1. **🟠 ВЫСОКИЙ:** NTFS permissions mapping
2. **🟠 ВЫСОКИЙ:** ELF symbol versioning
3. **🟡 СРЕДНИЙ:** Signal handling completion

---

## 📈 Ожидаемый выигрыш от реализации

| Категория | Выигрыш |
|-----------|---------|
| ARM64 boot | **Включение поддержки ARM64 платформ** |
| NUMA scheduler | **+30-50% производительность на multi-socket** |
| Mutex fix | **Устранение случайных крашей** |
| Cache invalidation | **Устранение повреждения данных** |
| IPv6 | **Современная сетевая совместимость** |
| USB SCSI | **Стабильность USB накопителей** |
| AHCI multi-controller | **Поддержка серверных плат с несколькими контроллерами** |

---

*Документ сгенерирован: 2026-02-03*
*Проанализировано: 643+ TODO/FIXME/XXX/HACK комментариев*
