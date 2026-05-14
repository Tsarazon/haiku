- always doublecheck your decisions
- before any edit, always read at least 100 lines of surrounding context

# Mobile Haiku (KosmOS)

Мобильная операционная система на базе Haiku OS для ARM64, ARM, RISC-V и x86_64 устройств .

## Архитектура

Модульная iOS-подобная архитектура с заменой традиционных app_server и Interface Kit на современный графический стек:

- **Surface Kit** — управление буферами через area_id, RAII
- **Render Kit** — 2D рендеринг через ThorVG (KosmCanvas, KosmPainter, KosmPath)
- **Animation Kit** — анимации и переходы
- **Spektr UI Kit** — UI компоненты (элементы LVGL + Ark UI) с **KosmLayout** — система разметки (flexbox/grid)
- **Orbita** — композитор
- **Stancia** — лаунчер

## Ключевые решения

- ThorVG для векторной графики
- Exclusive-координаты (KosmPoint, KosmSize, KosmRect) вместо inclusive из Haiku
- Децентрализованная архитектура демонов (iOS-стиль)
- Сборка через xmake
- Философия "C++ with classes" в стиле Haiku

## Целевое железо

Поддерживается только современное железо **2014+ года выпуска**:

- **x86_64** (Broadwell и новее): UEFI, AHCI/NVMe storage, ксHCI USB, IOMMU. Reference target — Intel N150 планшеты (Alder Lake-N), также Atom Z8500 (Cherry Trail, eMMC), мобильные Core i-series.
- **ARM64** (ARMv8, Cortex-A53/A57+): Raspberry Pi 4/5, Apple Silicon, Rockchip RK3399/RK3588, Allwinner H6/H616, NXP i.MX 8 и т.п.
- **RISC-V** (RV64GC): VisionFive 2, HiFive Unmatched, QEMU virt.

**Явно НЕ поддерживается legacy:**
- PATA / PCI IDE (нет ни на одном target'е 2014+)
- SATA в legacy IDE-compat mode (UEFI 2014+ даёт AHCI)
- ISA device discovery (кроме минимального для ACPI legacy I/O вроде PS/2 KBC)
- Firmware pre-UEFI (CSM, BIOS-only) — не boot-target
- 32-битный x86 / ARMv7

Хардверная граница означает: если драйвер или подсистема таргетит железо старше 2014 — её можно удалять без оглядки на "а вдруг у кого-то будет". Пример: ATA bus manager + `generic_ide_pci` + `legacy_sata` + `ata_adapter` удалены целиком (~4700 строк) — ни один 2014+ SoC/chipset не использует PATA или SATA-в-IDE-режиме. AHCI+NVMe+eMMC(SDHCI) покрывают весь actual storage; `ata_types.h` (protocol-level константы для SATA FIS) остался, т.к. его использует AHCI.

## Соглашения

- Пространство имён с префиксом Kosm*
- Космическая тематика в названиях компонентов
- Без ASCII-рамок

## Сборка

```bash
# Перейти в generated
cd /home/ruslan/haiku/generated

# Сборка nightly-anyboot ISO (1 ГБ)
/home/ruslan/haiku/buildtools/jam/jam0 -q -j8 @nightly-anyboot

# Полная пересборка с нуля
/home/ruslan/haiku/buildtools/jam/jam0 -a -q -j8 @nightly-anyboot

# С логгированием
/home/ruslan/haiku/buildtools/jam/jam0 -a -q -j8 @nightly-anyboot 2>&1 | tee /home/ruslan/haiku/build_log.txt
```

**Важно:**
- Использовать jam из `buildtools/jam/jam0`, НЕ системный jam
- Запускать из папки `generated`
- `-a` — пересборка всех целей
- `-q` — быстрый выход при ошибке
- `-j8` — 8 потоков

**Размеры образов** (настраиваются в `build/jam/`):
- `haiku-anyboot.iso` — 2 ГБ (BuildSetup:66)
- `haiku-nightly-anyboot.iso` — 1 ГБ (DefaultBuildProfiles:129)

**Известная проблема:** инкрементальная сборка (без `-a`) падает на этапе `BuildAnybootImageEfi1` с ошибкой `failed to open ISO file: No such file or directory` — `haiku-boot-cd.iso` не пересоздаётся. Обходное решение — полная пересборка с `-a`. TODO: разобраться почему boot CD ISO не попадает в зависимости инкрементальной сборки.

## Запуск QEMU

```bash
cd /home/ruslan/haiku/generated && qemu-system-x86_64 \
  -enable-kvm -cpu host -smp 4 -m 3G \
  -drive file=/home/ruslan/haiku/generated/haiku-nightly-anyboot.iso,format=raw,id=disk0,if=none \
  -device ahci,id=ahci -device ide-hd,drive=disk0,bus=ahci.0 \
  -vga virtio -bios /usr/share/ovmf/OVMF.fd \
  -net nic,model=virtio -net user \
  -display sdl -usb -device usb-tablet \
  -snapshot \
  -serial file:/tmp/qemu_serial.log \
  -d guest_errors 2>&1 &
```

**Важно:**
- `-snapshot` — обязательно! Без него QEMU пишет в ISO и повреждает boot sector
- AHCI (`-device ahci`) — эмулирует реальное железо (как на N150). Virtio не работает с текущей сборкой
- Serial лог в `/tmp/qemu_serial.log` — для диагностики kernel panic и тестов
- НЕ вызывать `kill` перед запуском — пользователь закрывает QEMU вручную

