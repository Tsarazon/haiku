# Отчёт о сопоставлении файлов IntelLake с оригинальными файлами Haiku

## Обзор

Директория `IntelLake/` содержит рефакторинг драйвера Intel Extreme Graphics для поддержки **только Gen 9+ GPU** (Skylake 2015 и новее). Это убирает поддержку устаревших поколений (Gen 2-8) и добавляет улучшенную поддержку современных чипов.

## Местоположения оригинальных файлов

- **Акселерант:** `/home/ruslan/haiku/src/add-ons/accelerants/intel_extreme/`
- **Драйвер ядра:** `/home/ruslan/haiku/src/add-ons/kernel/drivers/graphics/intel_extreme/`
- **Общий заголовок:** `/home/ruslan/haiku/headers/private/graphics/intel_extreme/`

---

## Детальное сопоставление файлов

### 1. accelerant.cpp

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/accelerants/intel_extreme/accelerant.cpp` |
| **Тип изменений** | Рефакторинг + удаление |

#### УДАЛЕНО (legacy pre-Gen9):
- Проверка `InGroup(INTEL_GROUP_96x)` и выделение памяти для 3D контекста i965
- Функция `intel_free_memory(gInfo->context_base)` в `uninit_common()`
- Пробинг legacy портов: `DisplayPort`, `HDMIPort`, `LVDSPort`, `AnalogPort`, `DigitalPort`
- Подсчёт pipes для Gen 7 (3 pipes) — теперь Gen 9+ по умолчанию 3, Gen 12+ — 4
- Проверка `gInfo->shared_info->device_type.Generation() >= 7` — заменена на жёсткое 3 pipes
- Проверка `gInfo->shared_info->internal_crt_support` для аналогового порта
- Установка `gInfo->head_mode` флагов (`HEAD_MODE_LVDS_PANEL`, `HEAD_MODE_B_DIGITAL`, `HEAD_MODE_A_ANALOG`)
- Вызов `refclk_activate_ilk()` для PCH IBX/CPT

#### ДОБАВЛЕНО (Gen9+ specific):
- Комментарии с ссылками на Intel PRM Vol 2c, Display Engine
- Упрощённая логика: только DDI порты для Gen 9+
- Автоматическое определение: Gen 12+ поддерживает до порта G
- Название "Intel Iris Xe" для семейства LAKE

#### СОХРАНЕНО:
- Базовая архитектура init_common/uninit_common
- Логика assign_pipes()
- Инициализация ring buffer

---

### 2. accelerant.h

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/accelerants/intel_extreme/accelerant.h` |
| **Тип изменений** | Минимальные изменения |

#### УДАЛЕНО:
- Поле `context_base` и `context_offset` для i965 3D контекста (не нужно для Gen 9+)

#### СОХРАНЕНО:
- Структура `accelerant_info`
- Указатели на pipes и ports
- Overlay регистры

---

### 3. intel_extreme.h (общий заголовок)

| Аспект | Описание |
|--------|----------|
| **Источник** | `headers/private/graphics/intel_extreme/intel_extreme.h` |
| **Тип изменений** | Полный рефакторинг |

#### УДАЛЕНО (legacy families/groups):
```cpp
// Удалены все определения до Gen 9:
INTEL_FAMILY_8xx, INTEL_FAMILY_9xx, INTEL_FAMILY_SER5, INTEL_FAMILY_SOC0
INTEL_GROUP_83x, INTEL_GROUP_85x, INTEL_GROUP_91x, INTEL_GROUP_94x, ...
INTEL_GROUP_ILK, INTEL_GROUP_SNB, INTEL_GROUP_IVB, INTEL_GROUP_HAS, ...
INTEL_MODEL_xxx для всех устройств до Skylake
```

#### УДАЛЕНО (legacy PCH):
```cpp
INTEL_PCH_IBX_DEVICE_ID
INTEL_PCH_CPT_DEVICE_ID
INTEL_PCH_PPT_DEVICE_ID
INTEL_PCH_LPT_DEVICE_ID
INTEL_PCH_WPT_DEVICE_ID
```

#### УДАЛЕНО (legacy pch_info enum):
```cpp
INTEL_PCH_IBX, INTEL_PCH_CPT, INTEL_PCH_LPT
```

#### ДОБАВЛЕНО (Gen9+ specific):
- Новое семейство: `INTEL_FAMILY_LAKE`
- Группы: `INTEL_GROUP_SKY`, `INTEL_GROUP_KBY`, `INTEL_GROUP_CFL`, `INTEL_GROUP_CML`, `INTEL_GROUP_BXT`, `INTEL_GROUP_GLK`, `INTEL_GROUP_ICL`, `INTEL_GROUP_JSL`, `INTEL_GROUP_EHL`, `INTEL_GROUP_TGL`, `INTEL_GROUP_RKL`, `INTEL_GROUP_ALD`
- Rocket Lake и полный Alder Lake device ID
- Улучшенный `DeviceType::Generation()` для Gen 9/11/12
- `IsAtom()` метод для Broxton/Gemini Lake/Jasper Lake/Elkhart Lake
- Поля прошивки в `intel_shared_info`: `dmc_version`, `guc_version`, `huc_version`
- Расширенные PCH device IDs для Gen 9+
- TGL DPLL регистры (полный набор)
- Gen11+ interrupt регистры
- Gen9+ Universal Plane регистры

---

### 4. LakePLL.cpp / LakePLL.h

| Аспект | Описание |
|--------|----------|
| **Источники** | `pll.cpp`, `pll.h`, `TigerLakePLL.cpp`, `TigerLakePLL.h` |
| **Тип изменений** | **ОБЪЕДИНЕНИЕ** трёх поколений PLL |

#### УДАЛЕНО из pll.cpp/pll.h:
- Структура `pll_divisors` (m, n, p делители для legacy)
- Структура `pll_limits` (min/max VCO для legacy)
- Функция `valid_pll_divisors()` — не нужна для DCO
- Функция `compute_pll_divisors()` — legacy PLL алгоритм
- Функция `refclk_activate_ilk()` — IronLake reference clock
- Функция `hsw_ddi_calculate_wrpll()` — Haswell WRPLL

#### ОБЪЕДИНЕНО из TigerLakePLL:
- `ComputeHdmiDpll()` → `tgl_compute_hdmi_dpll()`
- `ComputeDisplayPortDpll()` → `tgl_compute_dp_dpll()`
- `ProgramPLL()` → `tgl_program_pll()`

#### ДОБАВЛЕНО (новая унифицированная архитектура):

**Skylake WRPLL (Gen 9/9.5):**
```cpp
skl_wrpll_context — контекст для поиска делителей
skl_wrpll_try_divider() — проверка делителя
skl_wrpll_get_multipliers() — P0/P1/P2 разложение
skl_wrpll_params_populate() — заполнение параметров
skl_ddi_calculate_wrpll() — расчёт WRPLL
skl_program_dpll() — программирование DPLL
```

**Ice Lake Combo PLL (Gen 11):**
```cpp
icl_dp_combo_pll_24MHz_values[] — таблица для 24 МГц
icl_dp_combo_pll_19_2MHz_values[] — таблица для 19.2 МГц
icl_dp_rate_to_index() — индекс DP link rate
icl_compute_combo_pll_hdmi() — расчёт для HDMI
icl_compute_combo_pll_dp() — расчёт для DP
icl_program_combo_pll() — программирование combo PLL
```

**Tiger Lake (Gen 12+):**
- Полностью интегрирован код из TigerLakePLL.cpp

**Унифицированный интерфейс:**
```cpp
compute_display_pll() — автоматический выбор алгоритма по поколению
get_effective_ref_clock() — учёт auto-divide для Gen 11+
pll_port_type enum — PLL_PORT_HDMI/DVI/DP/EDP
```

---

### 5. Pipes.h / Pipes.cpp

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/accelerants/intel_extreme/Pipes.h` |
| **Тип изменений** | Упрощение |

#### УДАЛЕНО:
- `#include "FlexibleDisplayInterface.h"` — FDI не используется в Gen 9+
- `FDILink* fFDILink` член класса
- Метод `FDI()` accessor
- Метод `Disable()` — не используется
- Метод `SetFDILink()` — FDI только для Sandy Bridge/Ivy Bridge
- Метод `ConfigureClocks()` с legacy `pll_divisors` параметрами

#### СОХРАНЕНО:
- `ConfigureClocksSKL()` для skl_wrpll_params
- Базовые методы: `Enable()`, `Configure()`, `ConfigureTimings()`

#### ИЗМЕНЕНО:
- Комментарий с датой рефакторинга
- `MAX_PIPES = 4` с уточнением: Gen 9 = 3, Gen 11+ = 4

---

### 6. Ports.h / Ports.cpp

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/accelerants/intel_extreme/Ports.h` |
| **Тип изменений** | Значительное упрощение |

#### УДАЛЕНО (legacy port classes):
- `class AnalogPort` — VGA (не поддерживается Gen 9+)
- `class LVDSPort` — заменён на eDP через DDI
- `class DigitalPort` — legacy DVI
- `class HDMIPort` — теперь через DDI
- `class DisplayPort` — теперь через DDI
- `class EmbeddedDisplayPort` — теперь через DDI

#### УДАЛЕНО (legacy port types):
```cpp
INTEL_PORT_TYPE_ANALOG  // удалён
INTEL_PORT_TYPE_DVI     // удалён
INTEL_PORT_TYPE_LVDS    // удалён
```

#### ДОБАВЛЕНО:
```cpp
INTEL_PORT_TYPE_DDI     // Digital Display Interface (Gen 9+)
```

#### ИЗМЕНЕНО:
- `DigitalDisplayInterface::Type()` теперь возвращает `INTEL_PORT_TYPE_DDI` вместо `INTEL_PORT_TYPE_DVI`
- Комментарий: "Refactored for Gen 9+ only support"

---

### 7. driver.cpp (драйвер ядра)

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp` |
| **Тип изменений** | Сокращение списка устройств |

#### УДАЛЕНО (legacy device IDs):
```cpp
// Все устройства до Gen 9:
Sandy Bridge (0x0102, 0x0112, 0x0122, 0x0106, ...)
Ivy Bridge (0x0152, 0x0162, 0x0156, ...)
Haswell (0x0a06, 0x0412, 0x0416, ...)
ValleyView (0x0f30, 0x0f31, ...)
Broadwell (0x1606, 0x160b, 0x1616, ...)
```

#### УДАЛЕНО (legacy PCH detection):
```cpp
case INTEL_PCH_IBX_DEVICE_ID:
case INTEL_PCH_CPT_DEVICE_ID:
case INTEL_PCH_PPT_DEVICE_ID:
case INTEL_PCH_LPT_DEVICE_ID:
case INTEL_PCH_WPT_DEVICE_ID:
```

#### ДОБАВЛЕНО:
- Расширенный список Skylake device IDs
- Полный список Kaby Lake device IDs
- Полный список Coffee Lake device IDs
- Полный список Comet Lake device IDs
- Полный список Ice Lake device IDs
- Полный список Tiger Lake device IDs
- Полный список Rocket Lake device IDs
- Полный список Alder Lake device IDs (включая P, N варианты)
- Apollo Lake / Broxton device IDs
- Gemini Lake device IDs
- Jasper Lake device IDs
- Elkhart Lake device IDs

#### ИЗМЕНЕНО:
- Комментарий: "SUPPORTED: Gen 9+ only (Skylake 2015 and newer)"
- Верификация против Intel PRM Vol 2c

---

### 8. power.h / power.cpp

| Аспект | Описание |
|--------|----------|
| **Источник** | `src/add-ons/kernel/drivers/graphics/intel_extreme/power.h` |
| **Тип изменений** | Полная переработка регистров |

#### УДАЛЕНО (Gen6-8 регистры):
```cpp
// Все INTEL6_* регистры удалены:
INTEL6_GT_THREAD_STATUS_REG
INTEL6_GT_PERF_STATUS
INTEL6_RP_STATE_LIMITS
INTEL6_RPNSWREQ
INTEL6_RC_CONTROL
INTEL6_RC_VIDEO_FREQ
INTEL6_RP_DOWN_TIMEOUT
INTEL6_RPSTAT1
INTEL6_RP_CONTROL
INTEL6_RP_UP_THRESHOLD
INTEL6_RP_DOWN_THRESHOLD
... (порядка 50 регистров)
```

#### ДОБАВЛЕНО (Gen9+ регистры):
```cpp
// Render P-state:
GEN9_RP_STATE_CAP, GEN9_RP_STATE_LIMITS, GEN9_RPSTAT1, GEN9_RPNSWREQ

// RC6 state:
GEN9_GT_CORE_STATUS, GEN9_RC_CONTROL, GEN9_RC_STATE
GEN9_RC6_RESIDENCY_COUNTER, GEN9_RC6_THRESHOLD

// Forcewake:
GEN9_FORCEWAKE_RENDER_GEN9, GEN9_FORCEWAKE_MEDIA_GEN9
GEN9_FORCEWAKE_BLITTER_GEN9, GEN9_FORCEWAKE_ACK_*

// Clock gating:
GEN9_CLKGATE_DIS_0/1/4

// Gen11+ specific:
GEN11_GT_INTR_DW0/1, GEN11_EU_PERF_CNTL*

// Gen12+ specific:
GEN12_RC_CG_CONTROL

// Display Power Wells:
SKL_PW_CTL_IDX_*, ICL_PW_CTL_IDX_*
```

---

### 9. firmware.cpp / firmware.h (НОВЫЙ)

| Аспект | Описание |
|--------|----------|
| **Источник** | НЕТ оригинала — полностью новый код |
| **Тип изменений** | Новая функциональность |

#### ДОБАВЛЕНО:
- Загрузка DMC (Display Microcontroller) firmware
- Загрузка GuC (Graphics Microcontroller) firmware
- Загрузка HuC (HEVC Microcontroller) firmware
- Структуры заголовков прошивок: `intel_uc_fw_header`, `intel_dmc_header_v1`
- Регистры DMC: `DMC_PROGRAM_BASE`, `SKL_DMC_*`, `ICL_DMC_*`, `TGL_DMC_*`
- Регистры GuC: `GUC_STATUS`, `GUC_WOPCM_SIZE/OFFSET`, `DMA_*`
- DC state управление: `DC_STATE_DC5/6/9_ENABLE`
- API функции: `intel_firmware_init()`, `intel_load_dmc/guc/huc_firmware()`

---

### 10. planes.cpp (НОВЫЙ)

| Аспект | Описание |
|--------|----------|
| **Источник** | Частично заменяет `overlay.cpp` |
| **Тип изменений** | Новая реализация overlay через Universal Planes |

#### ЗАМЕНЕНО (legacy overlay):
- Оригинальный `overlay.cpp` использовал legacy overlay регистры (до Gen 9)
- Gen 9+ не имеет legacy overlay hardware

#### ДОБАВЛЕНО (Universal Planes):
- Полная реализация Gen 9+ Universal Planes для overlay API
- Регистры PLANE_CTL, PLANE_STRIDE, PLANE_POS, PLANE_SIZE, PLANE_SURF
- Регистры PS_CTRL (Pipe Scaler) для аппаратного масштабирования
- Поддержка форматов: RGB565, XRGB8888, YUV422
- Color keying через PLANE_KEYVAL/KEYMSK
- Разные offsets для Gen 9 (3 planes/pipe) и Gen 11+ (7 planes/pipe)
- Макросы PLANE_CTL(), PLANE_SURF() и т.д. для вычисления адресов

---

### 11. Остальные файлы (минимальные изменения)

| Файл | Изменения |
|------|-----------|
| `accelerant_protos.h` | Без изменений |
| `commands.h` | Без изменений |
| `cursor.cpp` | Минимальные (удалены legacy проверки) |
| `dpms.cpp` | Минимальные (удалены legacy PCH проверки) |
| `engine.cpp` | Без изменений |
| `hooks.cpp` | Без изменений |
| `mode.cpp` | Минимальные (удалены legacy PLL пути) |
| `memory.cpp` | Без изменений |
| `PanelFitter.cpp/h` | Без изменений |

---

## Сводная таблица изменений

| Категория | Удалено | Добавлено | Объединено |
|-----------|---------|-----------|------------|
| Device Families | 4 | 1 (LAKE) | - |
| Device Groups | ~15 | 12 | - |
| Device IDs | ~50 | ~80 | - |
| Port Classes | 6 | 1 (DDI) | - |
| PLL алгоритмов | 2 (legacy, ILK) | 3 (SKL, ICL, TGL) | 2→1 |
| Регистров power | ~50 | ~40 | - |
| Файлов | - | 2 (firmware, planes) | 2→1 (pll) |

---

## Ключевые архитектурные изменения

1. **Единый DDI интерфейс** — все дисплейные выходы через Digital Display Interface
2. **DCO-based PLL** — унифицированная PLL архитектура вместо legacy делителей
3. **Universal Planes** — новый overlay через universal plane вместо legacy overlay unit
4. **Firmware support** — добавлена загрузка DMC/GuC/HuC прошивок
5. **Упрощённый PCH** — только Gen 9+ PCH (SPT, CNP, ICP, TGP, ADP)

---

## Верификация

Все регистры и device IDs верифицированы против:
- Intel PRM Vol 2c Part 1/2 — Device Identification
- Intel PRM Vol 12 — Display Engine
- Linux i915 driver source code

Дата создания отчёта: 2025-12-28