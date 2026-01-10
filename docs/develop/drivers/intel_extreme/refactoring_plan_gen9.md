# План рефакторинга драйвера Intel Extreme для Gen9+

## Цель

Удалить поддержку GPU до Gen9 (Skylake), оставив только:
- **Gen9**: Skylake (SKY), KabyLake (KBY), CoffeeLake (CFL), CometLake (CML)
- **Gen11**: JasperLake (JSL)
- **Gen12**: TigerLake (TGL), AlderLake (ALD)

## Удаляемые поколения

| Поколение | Группы | Семейство | Год выпуска |
|-----------|--------|-----------|-------------|
| Gen2 | 83x, 85x | INTEL_FAMILY_8xx | 2002-2003 |
| Gen3 | 91x, 94x, Gxx, PIN | INTEL_FAMILY_9xx | 2004-2008 |
| Gen4 | 96x, G4x | INTEL_FAMILY_9xx | 2006-2008 |
| Gen5 | ILK (IronLake) | INTEL_FAMILY_SER5 | 2010 |
| Gen6 | SNB (SandyBridge) | INTEL_FAMILY_SER5 | 2011 |
| Gen7 | IVB, HAS, VLV | INTEL_FAMILY_SER5/SOC0 | 2012-2013 |
| Gen8 | CHV, BDW | INTEL_FAMILY_SOC0 | 2014-2015 |

## Ключевые технические изменения

### Что удаляется полностью

1. **FDI (Flexible Display Interface)** — не используется в Gen9+
   - Файлы: `FlexibleDisplayInterface.h`, `FlexibleDisplayInterface.cpp`

2. **Классы портов для старых поколений**:
   - `AnalogPort` — VGA/CRT выход
   - `LVDSPort` — встроенные панели (заменён на eDP через DDI)
   - `DigitalPort` — DVI через старый путь
   - `HDMIPort` — HDMI через старый путь
   - `DisplayPort` — DP через старый путь (до-DDI)
   - `EmbeddedDisplayPort` — eDP через старый путь

3. **PLL-код для старых поколений**:
   - Код для CHV, VLV, PIN, 85x, G4x

### Что упрощается

1. **DeviceType** — удаляются методы `SupportsHDMI()`, упрощается `Generation()`
2. **Проверки поколений** — удаляются все `if` для старых групп/семейств
3. **DDI** — становится единственным путём вывода

---

## Порядок рефакторинга (по фазам)

### Принцип: от листьев графа зависимостей к корню

Рефакторинг выполняется в порядке, обратном топологической сортировке зависимостей:
1. Сначала файлы, от которых никто не зависит
2. Затем файлы, зависящие только от уже обработанных
3. В конце — корневые заголовки

---

## Фаза 0: Подготовка (без изменения кода)

**Цель**: Создать ветку, настроить тестирование

- [ ] Создать ветку `refactor/gen9-only`
- [ ] Подготовить тестовую среду с Gen9+ оборудованием
- [ ] Сделать бэкап текущего состояния

---

## Фаза 1: Таблица PCI ID

**Файл**: `src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp`

**Зависимости**: Нет (листовой файл)

**Изменения**:
```cpp
// УДАЛИТЬ все записи до Gen9:
// - SandyBridge (0x0102, 0x0112, 0x0122, 0x0106, 0x0116, 0x0126, 0x010a)
// - IvyBridge (0x0152, 0x0162, 0x0156, 0x0166, 0x015a, 0x016a)
// - Haswell (0x0a06, 0x0412, 0x0416, 0x0a16, 0x0a2e, 0x0d26)
// - ValleyView (0x0f30, 0x0f31, 0x0f32, 0x0f33)
// - Broadwell (0x1606..0x162d)

// ОСТАВИТЬ:
// - Skylake (0x1902..0x192b)
// - Apollo Lake (0x5a84, 0x5a85)
// - KabyLake (0x5906..0x5927)
// - GeminiLake (0x3185, 0x3184)
// - CoffeeLake (0x3e90..0x3ea6)
// - IceLake (0x8a56..0x8a53)
// - CometLake (0x9ba4..0x9bcc)
// - JasperLake (0x4e55, 0x4e61, 0x4e71)
// - TigerLake (0x9a49..0x9a70)
// - AlderLake (0x46a6, 0x46d1)
```

**Риск**: Низкий (изолированное изменение)

---

## Фаза 2: Корневые определения

**Файл**: `headers/private/graphics/intel_extreme/intel_extreme.h`

**Зависимости**: `lock.h` (не меняется)

**Изменения**:

1. **Удалить семейства**:
```cpp
// УДАЛИТЬ:
#define INTEL_FAMILY_8xx    0x00020000
#define INTEL_FAMILY_9xx    0x00040000
#define INTEL_FAMILY_SER5   0x00080000
#define INTEL_FAMILY_SOC0   0x00200000
```

2. **Удалить группы**:
```cpp
// УДАЛИТЬ:
#define INTEL_GROUP_83x, 85x, 91x, 94x, 96x, Gxx, G4x, PIN
#define INTEL_GROUP_ILK, SNB, IVB, HAS, VLV, CHV, BDW
```

3. **Удалить модели**:
```cpp
// УДАЛИТЬ все INTEL_MODEL_* до INTEL_MODEL_SKY
```

4. **Удалить старые PCH ID**:
```cpp
// УДАЛИТЬ:
#define INTEL_PCH_IBX_DEVICE_ID  // IronLake
#define INTEL_PCH_CPT_DEVICE_ID  // SandyBridge
#define INTEL_PCH_PPT_DEVICE_ID  // IvyBridge
#define INTEL_PCH_LPT_*          // Haswell
#define INTEL_PCH_WPT_*          // Broadwell
```

5. **Упростить DeviceType**:
```cpp
struct DeviceType {
    uint32 type;

    // УДАЛИТЬ метод SupportsHDMI() — всегда true для Gen9+
    // УДАЛИТЬ метод HasDDI() — всегда true для Gen9+

    int Generation() const {
        if (InGroup(INTEL_GROUP_JSL))
            return 11;
        if (InGroup(INTEL_GROUP_TGL) || InGroup(INTEL_GROUP_ALD))
            return 12;
        // По умолчанию Gen9 для всех INTEL_FAMILY_LAKE
        return 9;
    }
};
```

6. **Удалить старые pch_info**:
```cpp
enum pch_info {
    INTEL_PCH_NONE = 0,
    // УДАЛИТЬ: INTEL_PCH_IBX, CPT, LPT
    INTEL_PCH_SPT,  // SunrisePoint (Skylake+)
    INTEL_PCH_CNP,
    INTEL_PCH_ICP,
    INTEL_PCH_JSP,
    INTEL_PCH_MCC,
    INTEL_PCH_TGP,
    INTEL_PCH_ADP,
    INTEL_PCH_NOP
};
```

**Риск**: Высокий (влияет на все файлы)

**Порядок**: После Фазы 1, перед остальными фазами

---

## Фаза 3: Драйвер ядра

### 3.1 power.cpp / power.h

**Зависимости**: `driver.h` → `intel_extreme_private.h` → `intel_extreme.h`

**Изменения**:
- Удалить весь код для SNB/IVB/VLV
- Функции `gen6_gtt_clear_range()`, `gt_force_wake_get()` — проверить необходимость для Gen9+
- Возможно, файл станет пустым или минимальным

**Строки для удаления**:
```
power.cpp:23-45 — все проверки InGroup(SNB/IVB/VLV)
```

### 3.2 intel_extreme.cpp

**Зависимости**: `intel_extreme.h`, `driver.h`, `power.h`

**Изменения**:

| Строка | Код | Действие |
|--------|-----|----------|
| 268-279 | `InGroup(INTEL_GROUP_SNB/ILK)` | Удалить ветки |
| 294 | `InFamily(INTEL_FAMILY_SER5)` | Удалить |
| 611-624 | Проверки семейств для GTT | Упростить |
| 686-721 | Проверки групп для watermark | Удалить старые ветки |
| 829 | `InGroup(INTEL_GROUP_VLV)` | Удалить |
| 887 | `InFamily(INTEL_FAMILY_8xx)` | Удалить |

### 3.3 bios.cpp

**Зависимости**: `intel_extreme.h`, `driver.h`

**Изменения**:
- Строка 521: удалить проверку `InGroup(INTEL_GROUP_VLV)`

### 3.4 device.cpp

**Изменения**: Минимальные или отсутствуют

---

## Фаза 4: Удаление FDI

**Файлы**:
- `src/add-ons/accelerants/intel_extreme/FlexibleDisplayInterface.h` — **УДАЛИТЬ**
- `src/add-ons/accelerants/intel_extreme/FlexibleDisplayInterface.cpp` — **УДАЛИТЬ**

**Зависимости**:
- `Pipes.h` включает `FlexibleDisplayInterface.h`
- `Pipe` содержит `FDILink* fFDILink`

**Изменения в Pipes.h**:
```cpp
// УДАЛИТЬ:
#include "FlexibleDisplayInterface.h"

class Pipe {
    // УДАЛИТЬ:
    FDILink* fFDILink;
    void SetFDILink(FDILink* link);
};
```

**Изменения в Pipes.cpp**:
- Удалить все вызовы `fFDILink->...`
- Удалить создание FDILink

**Изменения в Jamfile**:
```jam
# УДАЛИТЬ:
FlexibleDisplayInterface.cpp
```

**Риск**: Средний (требует изменений в Pipes)

---

## Фаза 5: PLL (Phase-Locked Loop)

### 5.1 pll.cpp

**Зависимости**: `pll.h` → `intel_extreme.h`

**Строки для удаления/изменения**:

| Строка | Код | Действие |
|--------|-----|----------|
| 243-249 | CHV/VLV/PIN limits | Удалить |
| 274 | G4x limits | Удалить |
| 367-394 | PIN/85x compute_pll | Удалить |
| 444-449 | G4x/CHV/VLV p2 | Удалить |

**Функции, которые могут быть удалены полностью**:
- `compute_pll_divisors()` — возможно не используется для DDI
- Код для не-WRPLL/не-DPLL расчётов

**Сохранить**:
- `hsw_ddi_calculate_wrpll()` — используется для Haswell+, но проверить
- `skl_ddi_calculate_wrpll()` — используется для Skylake+
- Код в `TigerLakePLL.cpp`

### 5.2 TigerLakePLL.cpp

**Изменения**: Нет (оставить как есть)

---

## Фаза 6: PanelFitter

**Файлы**: `PanelFitter.h`, `PanelFitter.cpp`

**Зависимости**: `intel_extreme.h`

**Изменения**:
- Проверить, используется ли для Gen9+
- Если да — оставить
- Если нет — удалить

---

## Фаза 7: Pipes

**Файл**: `src/add-ons/accelerants/intel_extreme/Pipes.cpp`

**Зависимости**: `Pipes.h` → `intel_extreme.h`, `pll.h`, `FlexibleDisplayInterface.h`, `PanelFitter.h`

**Строки для удаления/изменения**:

| Строка | Код | Действие |
|--------|-----|----------|
| 34 | `InFamily(INTEL_FAMILY_LAKE)` | Упростить (всегда true) |
| 381-382 | SNB/IVB проверки | Удалить |
| 417-477 | 96x/PIN проверки | Удалить |

**После удаления FDI** (Фаза 4):
- Удалить `FDILink* fFDILink` из класса
- Удалить методы работы с FDI

---

## Фаза 8: Порты (ОСНОВНАЯ)

### 8.1 Удаление классов портов

**Файл**: `src/add-ons/accelerants/intel_extreme/Ports.h`

**Удалить классы**:
```cpp
class AnalogPort : public Port { ... };      // УДАЛИТЬ
class LVDSPort : public Port { ... };        // УДАЛИТЬ
class DigitalPort : public Port { ... };     // УДАЛИТЬ
class HDMIPort : public DigitalPort { ... }; // УДАЛИТЬ
class DisplayPort : public Port { ... };     // УДАЛИТЬ
class EmbeddedDisplayPort : public DisplayPort { ... }; // УДАЛИТЬ
```

**Оставить**:
```cpp
class Port { ... };                              // Базовый класс
class DigitalDisplayInterface : public Port { ... }; // DDI
```

### 8.2 Ports.cpp

**Удалить реализации**:
- `AnalogPort::*`
- `LVDSPort::*`
- `DigitalPort::*`
- `HDMIPort::*`
- `DisplayPort::*`
- `EmbeddedDisplayPort::*`

**Строки для удаления** (приблизительно):
- 378: `InGroup(INTEL_GROUP_83x)` — удалить
- 550: `InGroup(INTEL_GROUP_BDW)` — удалить
- 878: `InGroup(INTEL_GROUP_BDW)` — удалить
- 1143-2156: Множество проверок старых групп
- 1565-1823: VLV/CHV код
- 2205, 2332: SKY проверки — упростить

### 8.3 port_type enum

```cpp
enum port_type {
    // УДАЛИТЬ:
    INTEL_PORT_TYPE_ANALOG,
    INTEL_PORT_TYPE_LVDS,

    // ОСТАВИТЬ:
    INTEL_PORT_TYPE_ANY,
    INTEL_PORT_TYPE_DVI,
    INTEL_PORT_TYPE_DP,
    INTEL_PORT_TYPE_eDP,
    INTEL_PORT_TYPE_HDMI
};
```

**Риск**: Высокий (много кода)

---

## Фаза 9: Accelerant

**Файл**: `src/add-ons/accelerants/intel_extreme/accelerant.cpp`

**Зависимости**: `accelerant.h` → `intel_extreme.h`, `Ports.h`, `Pipes.h`

**Изменения**:

| Строка | Код | Действие |
|--------|-----|----------|
| 128 | `InGroup(INTEL_GROUP_96x)` | Удалить |
| 281-294 | `!HasDDI()` DisplayPort | Удалить блок |
| 320-334 | `!HasDDI()` eDP | Удалить блок (закомментирован) |
| 336-354 | `!HasDDI()` HDMI | Удалить блок |
| 357-369 | `!HasDDI()` LVDS | Удалить блок |
| 371-385 | `!HasDDI()` DVI | Удалить блок |
| 611-617 | Семейства для версии | Упростить |

**После изменений**: останется только создание `DigitalDisplayInterface`

---

## Фаза 10: Функциональные модули

### 10.1 mode.cpp

**Строки для удаления/изменения**:

| Строка | Код | Действие |
|--------|-----|----------|
| 44-72 | `InFamily(INTEL_FAMILY_LAKE)` | Упростить (всегда true) |
| 95-99 | Gxx/96x/94x/91x/8xx | Удалить |
| 129-136 | 96x/G4x/ILK/SER5/LAKE/SOC0/HAS | Упростить |
| 496 | `InFamily(INTEL_FAMILY_LAKE)` | Упростить |
| 540-633 | Различные модели | Удалить старые |
| 692-693 | 915M/945M | Удалить |

### 10.2 overlay.cpp → planes.cpp (ЗАМЕНА)

**Проблема**: Hardware Overlay **не поддерживается в Gen9+**!

Intel удалила аппаратный overlay в Gen5 и заменила на **Universal Planes**.

**Решение**: Использовать готовую реализацию `/home/ruslan/haiku/planes.cpp`

Файл уже реализует полноценный overlay API через Gen 9+ Universal Planes:
- Использует `PLANE_CTL`, `PLANE_SURF`, `PLANE_SIZE` регистры
- Поддерживает **Gen 9/11/12** с учётом различий (PLANE_COLOR_CTL для Gen11+)
- Использует **Pipe Scaler** для аппаратного масштабирования (1/8x — 8x)
- Поддерживает RGB565, RGB8888, YCbCr422, color key, horizontal flip

#### Шаг 1: Обновить struct overlay в accelerant.h

```cpp
struct overlay {
    overlay_buffer  buffer;
    addr_t          buffer_base;
    uint32          buffer_offset;
    // Gen 9+ Universal Planes fields:
    int             pipe;       // 0, 1, 2, (3 for Gen12)
    int             plane;      // 0=primary, 1+=sprites
    int             scaler;     // 0 or 1
    uint32          plane_ctl;  // pre-computed PLANE_CTL value
};
```

#### Шаг 2: Заменить overlay.cpp

```bash
# Вариант A: Заменить содержимое
cp /home/ruslan/haiku/planes.cpp \
   src/add-ons/accelerants/intel_extreme/overlay.cpp

# Вариант B: Переименовать и обновить Jamfile
mv planes.cpp src/add-ons/accelerants/intel_extreme/planes.cpp
# В Jamfile: overlay.cpp → planes.cpp
```

#### Шаг 3: Удалить legacy overlay код

| Файл | Что удалить |
|------|-------------|
| `accelerant.h` | `struct overlay_registers* overlay_registers;` |
| `accelerant.h` | `last_overlay_view`, `last_overlay_frame`, `last_*_overlay_scale`, `overlay_position_buffer_offset` |
| `accelerant.cpp` | инициализацию overlay_registers (строки 123-126) |
| `engine.cpp` | `physical_overlay_registers` (строка 97) |
| `intel_extreme.cpp` | выделение памяти для overlay (строки 905-913) |
| `intel_extreme_private.h` | `struct overlay_registers* overlay_registers;` |
| `intel_extreme.h` | структуру `overlay_registers` (~100 строк) |
| `commands.h` | `QueueCommands`, `PutOverlayFlip()` — **ВОЗМОЖНО ВЕСЬ ФАЙЛ** |

#### Шаг 4: Проверить commands.h

`commands.h` содержит Ring Buffer команды для legacy overlay.
Проверить, используется ли для другого:
```bash
grep -r "QueueCommands\|PutFlush\|PutWaitFor" src/add-ons/accelerants/intel_extreme/
```

Если используется только в overlay.cpp — удалить весь commands.h.

#### Сравнение реализаций

| Legacy overlay.cpp | planes.cpp (Universal Planes) |
|--------------------|-------------------------------|
| Ring Buffer команды | Прямая запись в MMIO регистры |
| Только до Gen5 | Gen 9/11/12+ |
| Фиксированный скейлер | Pipe Scaler (1/8x — 8x) |
| Сложные фазовые коэффициенты | Аппаратный scaler |
| `overlay_registers` структура | `PLANE_*` регистры |
| ~700 строк legacy | ~700 строк современного кода |

### 10.3 hooks.cpp

**Строки для удаления**:

| Строка | Код | Действие |
|--------|-----|----------|
| 126-133 | 91x/94x/965M/G4x/PIN/ILK/SER5/SOC0 | Удалить |

### 10.4 engine.cpp

| Строка | Код | Действие |
|--------|-----|----------|
| 229-230 | LAKE/SOC0 | Упростить |

### 10.5 cursor.cpp, dpms.cpp, memory.cpp

**Изменения**: Минимальные или отсутствуют

---

## Фаза 11: AGP GART (внешняя зависимость)

**Файл**: `src/add-ons/kernel/busses/agp_gart/intel_gart.cpp`

**Строки для удаления**:

| Строка | Код | Действие |
|--------|-----|----------|
| 492 | `InFamily(INTEL_FAMILY_9xx)` | Удалить/упростить |
| 622-722 | `InFamily(INTEL_FAMILY_8xx)` | Удалить |

**Примечание**: Этот файл находится вне драйвера intel_extreme, изменять с осторожностью.

---

## Фаза 12: Jamfiles

### Accelerant Jamfile

```jam
Addon intel_extreme.accelerant :
    accelerant.cpp
    cursor.cpp
    dpms.cpp
    engine.cpp
    hooks.cpp
    memory.cpp
    mode.cpp
    overlay.cpp
    pll.cpp
    # УДАЛИТЬ: FlexibleDisplayInterface.cpp
    PanelFitter.cpp
    Ports.cpp
    Pipes.cpp
    TigerLakePLL.cpp
    : be $(TARGET_LIBSTDC++) libaccelerantscommon.a
;
```

### Kernel Driver Jamfile

Без изменений (если power.cpp не удаляется полностью).

---

## Сводная таблица: что удалить

| Файл | Действие | Фаза |
|------|----------|------|
| `FlexibleDisplayInterface.h` | УДАЛИТЬ | 4 |
| `FlexibleDisplayInterface.cpp` | УДАЛИТЬ | 4 |
| `AnalogPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |
| `LVDSPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |
| `DigitalPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |
| `HDMIPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |
| `DisplayPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |
| `EmbeddedDisplayPort` (в Ports.h/cpp) | УДАЛИТЬ | 8 |

---

## Порядок работы с файлами

### Рекомендуемый порядок (от простого к сложному)

1. **driver.cpp** — удалить PCI ID (легко, изолированно)
2. **intel_extreme.h** — удалить старые определения
3. **power.cpp** — удалить/упростить
4. **bios.cpp** — минимальные изменения
5. **intel_extreme.cpp** — удалить ветки
6. **pll.cpp** — удалить старый код
7. **FlexibleDisplayInterface.h/.cpp** — УДАЛИТЬ
8. **Pipes.h/.cpp** — удалить FDI, упростить
9. **overlay.cpp** — удалить старый код
10. **mode.cpp** — упростить
11. **engine.cpp** — упростить
12. **hooks.cpp** — упростить
13. **Ports.h/.cpp** — УДАЛИТЬ классы (самое сложное)
14. **accelerant.cpp** — упростить создание портов
15. **Jamfile** — удалить FlexibleDisplayInterface.cpp
16. **intel_gart.cpp** — упростить (осторожно!)

### Альтернативный порядок (по минимизации риска)

Если предпочитаете минимизировать риск сломать компиляцию:

1. Начните с файлов без зависимостей (`pll.cpp`, `overlay.cpp`)
2. Затем средний уровень (`Pipes.cpp`, `mode.cpp`)
3. В конце корневые файлы (`intel_extreme.h`, `Ports.h`)

---

## Оценка объёма работ

| Фаза | Файлы | Строк к удалению (прибл.) | Сложность |
|------|-------|---------------------------|-----------|
| 1 | driver.cpp | ~100 | Низкая |
| 2 | intel_extreme.h | ~100 | Средняя |
| 3 | power/intel_extreme/bios.cpp | ~200 | Средняя |
| 4 | FDI файлы | ~800 | Низкая (удаление) |
| 5 | pll.cpp | ~150 | Средняя |
| 6 | PanelFitter | ~50 | Низкая |
| 7 | Pipes.cpp | ~100 | Средняя |
| 8 | Ports.h/.cpp | ~1500 | **Высокая** |
| 9 | accelerant.cpp | ~150 | Средняя |
| 10 | mode/overlay/hooks/engine | ~200 | Средняя |
| 11 | intel_gart.cpp | ~100 | Низкая |

**Итого**: ~3500 строк к удалению/изменению

---

## Тестирование

### После каждой фазы

- [ ] Компиляция драйвера ядра
- [ ] Компиляция акселеранта
- [ ] Загрузка на Gen9+ оборудовании
- [ ] Проверка вывода на дисплей

### Тестовое оборудование

Минимум одно устройство из каждого поколения:
- [ ] Skylake (Gen9)
- [ ] KabyLake/CoffeeLake (Gen9.5)
- [ ] JasperLake (Gen11)
- [ ] TigerLake/AlderLake (Gen12)

---

## Риски и митигация

| Риск | Вероятность | Митигация |
|------|-------------|-----------|
| Сломать компиляцию | Высокая | Инкрементальные коммиты |
| Пропустить нужный код | Средняя | Code review, тестирование |
| Регрессии на Gen9+ | Средняя | Тестирование на реальном железе |
| Конфликты при merge | Низкая | Частая синхронизация с master |

---

## Чеклист готовности

- [ ] Фаза 0 выполнена (ветка создана)
- [ ] Все фазы 1-12 выполнены
- [ ] Компиляция без ошибок
- [ ] Тестирование на Skylake пройдено
- [ ] Тестирование на TigerLake пройдено
- [ ] Code review пройден
- [ ] Документация обновлена