# Отчёт о сопоставлении файлов IntelLake с оригинальными исходниками

## Общая информация

**IntelLake** — это модернизированный драйвер Intel GPU для Haiku OS, поддерживающий ТОЛЬКО поколения Gen 9 и новее (Skylake, Kaby Lake, Coffee Lake, Ice Lake, Tiger Lake, Alder Lake и т.д.).

Из кодовой базы удалена поддержка устаревших чипсетов:
- i830, i845, i855, i865
- i915, i945, i965
- G33, G35, G41, G43, G45
- Ironlake, Sandy Bridge, Ivy Bridge, Haswell, Broadwell

---

## 1. accelerant.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/accelerant.cpp`

### УДАЛЕНО:
- Выделение 3D контекста для i965 (`gInfo->context_base`, `gInfo->context_offset`)
- Пробинг устаревших типов портов: `INTEL_PORT_TYPE_ANALOG`, `INTEL_PORT_TYPE_DVI`, `INTEL_PORT_TYPE_LVDS`, `INTEL_PORT_TYPE_HDMI`, `INTEL_PORT_TYPE_DP`, `INTEL_PORT_TYPE_eDP`
- Классы портов: `AnalogPort`, `LVDSPort`, `DigitalPort`, `HDMIPort`, `DisplayPort`, `EmbeddedDisplayPort`
- Активация опорной частоты для старых чипов (Reference Clock)
- Условная логика для различных поколений GPU в detect_displays()

### ДОБАВЛЕНО:
- Только DDI (Digital Display Interface) порты — единый тип `INTEL_PORT_TYPE_DDI`
- Класс `DigitalDisplayInterface` как единственный тип порта
- Расчёт количества pipes: 3 для Gen9-Gen11, 4 для Gen12+
- Упрощённый probe_ports() только для DDI-A, DDI-B, DDI-C, DDI-D, DDI-E

### ИЗМЕНЕНО:
- Убраны все проверки `gInfo->shared_info->device_type.InGroup(INTEL_GROUP_*)`
- Код стал значительно компактнее (меньше условных ветвлений)

---

## 2. accelerant.h

**Оригинал:** `src/add-ons/accelerants/intel_extreme/accelerant.h`

### УДАЛЕНО:
- Поля для 3D контекста: `context_base`, `context_offset`, `context_set`
- Макрос `HEAD_MODE_A_ANALOG`

### ДОБАВЛЕНО:
- Комментарии о Gen 9+ специфике

### ИЗМЕНЕНО:
- Структура `accelerant_info` упрощена

---

## 3. accelerant_protos.h

**Оригинал:** `src/add-ons/accelerants/intel_extreme/accelerant_protos.h`

### УДАЛЕНО:
- Все функции 2D ускорения:
  - `intel_screen_to_screen_blit()`
  - `intel_fill_rectangle()`
  - `intel_invert_rectangle()`
  - `intel_fill_span()`
- Весь Engine API:
  - `intel_engine_count()`
  - `intel_acquire_engine()`
  - `intel_release_engine()`
  - `intel_get_sync_token()`
  - `intel_sync_to_token()`
- Функция `i965_configure_overlay()`

### ДОБАВЛЕНО:
- Комментарии: "Gen 9+ не использует 2D blitter — он deprecated"
- Комментарии: "Используется Execlist вместо Ring Buffer"

---

## 4. commands.h

**Оригинал:** `src/add-ons/accelerants/intel_extreme/commands.h`

### УДАЛЕНО:
- Весь класс `QueueCommands` (управление кольцевым буфером)
- Структуры `xy_command`, `xy_source_blit_command`, `xy_pattern_command`, `xy_color_command`, `xy_setup_mono_pattern_command`
- Все XY_* команды blitter:
  - `XY_COMMAND_COLOR_BLIT`
  - `XY_COMMAND_SETUP_MONO_PATTERN`
  - `XY_COMMAND_SCANLINE_BLIT`
  - `XY_COMMAND_SOURCE_BLIT`
  - Флаги `XY_COMMAND_NOCLIP`, `XY_COMMAND_PATTERN_*`, `XY_COMMAND_DESTINATION_*`

### ДОБАВЛЕНО:
- MI_* команды (Memory Interface) для Gen 9+:
  - `MI_NOOP` (0x00)
  - `MI_FLUSH` (0x04)
  - `MI_BATCH_BUFFER_END` (0x0A)
  - `MI_STORE_DATA_IMM` (0x20)
  - `MI_LOAD_REGISTER_IMM` (0x22)
  - `MI_FLUSH_DW` (0x26)

### ИЗМЕНЕНО:
- Файл стал минимальным — только базовые MI команды для синхронизации

---

## 5. cursor.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/cursor.cpp`

### УДАЛЕНО:
- Поддержка 2-цветных курсоров (режим AND/XOR mask)
- Макросы `CURSOR_MODE`, `CURSOR_ENABLED`, `CURSOR_FORMAT_*`, `CURSOR_PIPE_*`
- Функция `write_cursor_mode()` для управления режимами
- Вся логика конвертации 2-bit cursor в формат Intel

### ДОБАВЛЕНО:
- GEN9_CUR_CTL_* макросы:
  - `GEN9_CUR_CTL_ENABLE`
  - `GEN9_CUR_CTL_FORMAT_ARGB8888`
  - `GEN9_CUR_CTL_SIZE_64`, `GEN9_CUR_CTL_SIZE_128`, `GEN9_CUR_CTL_SIZE_256`
  - `GEN9_CUR_CTL_PIPE(n)`
- Прямая поддержка ARGB8888 курсоров (32-bit с альфа-каналом)
- Регистры `GEN9_CUR_POS`, `GEN9_CUR_BASE`, `GEN9_CUR_CTL`

### ИЗМЕНЕНО:
- `intel_set_cursor_shape()` теперь копирует ARGB данные напрямую
- Курсор всегда 64x64 ARGB8888 (нет выбора формата)

---

## 6. dpms.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/dpms.cpp`

### УДАЛЕНО:
- Управление PLL: `INTEL_DISPLAY_A_PLL`, `INTEL_DISPLAY_B_PLL`
- DPMS для аналоговых портов (VGA DAC)
- Управление LVDS панелью (`INTEL_PANEL_CONTROL`, `INTEL_PANEL_STATUS`)
- Функция `lvds_panel_on()` / `lvds_panel_off()`
- Проверки `device_type.HasDPMS()`

### ДОБАВЛЕНО:
- DPMS через DDI (Digital Display Interface)
- Управление питанием eDP панели
- Регистры `PP_CONTROL`, `PP_STATUS` для Panel Power Sequencing

### ИЗМЕНЕНО:
- `set_display_power_mode()` упрощена для DDI-only архитектуры

---

## 7. engine.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/engine.cpp`

### УДАЛЕНО:
- Весь Ring Buffer код:
  - `intel_allocate_ring_buffer()`
  - `intel_setup_ring_buffer()`
  - `RING_BUFFER_SIZE`, `PAGE_ALIGN()`
- Класс `QueueCommands` и его использование
- Все функции 2D ускорения:
  - `intel_screen_to_screen_blit()`
  - `intel_fill_rectangle()`
  - `intel_invert_rectangle()`
  - `intel_fill_span()`
- Engine API реализация

### ДОБАВЛЕНО:
- Заглушки с комментариями TODO для Gen 9+ синхронизации
- `intel_wait_for_scanline()` — stub для VSync

### ИЗМЕНЕНО:
- Файл теперь минимален (~50 строк вместо ~300)
- 2D ускорение полностью отключено (Gen 9 использует GPU compute или CPU)

---

## 8. hooks.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/hooks.cpp`

### УДАЛЕНО:
- Хуки 2D ускорения:
  - `B_SCREEN_TO_SCREEN_BLIT`
  - `B_FILL_RECTANGLE`
  - `B_INVERT_RECTANGLE`
  - `B_FILL_SPAN`
- Хуки Engine:
  - `B_ACCELERANT_ENGINE_COUNT`
  - `B_ACQUIRE_ENGINE`
  - `B_RELEASE_ENGINE`
  - `B_GET_SYNC_TOKEN`
  - `B_SYNC_TO_TOKEN`
- Проверки совместимости overlay для старых чипов
- Условный возврат хуков в зависимости от поколения GPU

### ДОБАВЛЕНО:
- Комментарии: "Gen 9+ не имеет 2D blitter"

### ИСПРАВЛЕНО:
- Баг fall-through в switch для cursor hooks (добавлен break)

---

## 9. mode.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/mode.cpp`

### УДАЛЕНО:
- Условная логика для разных поколений в `get_color_space_format()`
- Специальная обработка stride для pre-Gen 9 (выравнивание по 64 байта)
- Проверки `InGroup(INTEL_GROUP_HAS*)` для feature detection

### ДОБАВЛЕНО:
- Константы для Gen 9+: `PLANE_STRIDE_MASK`, `PLANE_CTL_FORMAT_*`
- Упрощённый расчёт stride (всегда 64-tile aligned)

### ИЗМЕНЕНО:
- `create_mode_list()` использует только Gen 9+ ограничения
- Максимальное разрешение: 8192x8192 (Gen 9+)

---

## 10. memory.cpp

**Оригинал:** `src/add-ons/accelerants/intel_extreme/memory.cpp`

### УДАЛЕНО:
- Выделение памяти для Ring Buffer

### ДОБАВЛЕНО:
- Константы выравнивания:
  - `INTEL_ALIGNMENT_4K` (4096)
  - `INTEL_ALIGNMENT_64K` (65536)
  - `INTEL_ALIGNMENT_1M` (1048576)
- Функция `intel_allocate_tiled_memory()` для Y-tiled поверхностей
- Поддержка 64KB страниц для Gen 9+

### ИЗМЕНЕНО:
- `intel_allocate_memory()` учитывает Gen 9+ требования к выравниванию

---

## 11. PanelFitter.cpp / PanelFitter.h

**Оригинал:** `src/add-ons/accelerants/intel_extreme/PanelFitter.cpp`, `PanelFitter.h`

### УДАЛЕНО:
- Условная логика для выбора offset: Panel Fitter vs Panel Scaler
- Проверки `device_type.InGeneration(INTEL_GROUP_ILK)` и т.д.
- Старые регистры `PF_CTL`, `PF_WIN_SZ`, `PF_WIN_POS`

### ДОБАВЛЕНО:
- Всегда используется Panel Scaler (offset +0x100)
- Регистры `PS_CTRL`, `PS_WIN_SZ`, `PS_WIN_POS`

### ИЗМЕНЕНО:
- Упрощённый конструктор без определения поколения
- `Enable()` / `Disable()` работают только с PS_* регистрами

---

## 12. Pipes.cpp / Pipes.h

**Оригинал:** `src/add-ons/accelerants/intel_extreme/Pipes.cpp`, `Pipes.h`

### УДАЛЕНО:
- Класс `FDILink` и вся FDI (Flexible Display Interface) логика
- Метод `Pipe::SetFDILink()`
- Метод `Pipe::ConfigureClocks()` для legacy PLL
- Поддержка PIPE_A / PIPE_B / PIPE_C для pre-Gen 9
- Регистры `FDI_RX_CTL`, `FDI_TX_CTL`, `FDI_RX_IIR`

### ДОБАВЛЕНО:
- `Pipe::ConfigureClocksSKL()` для Skylake DPLL
- Поддержка `PIPE_D` для Gen 12+
- Регистры `DPLL_CTRL1`, `DPLL_CFGCR1`, `DPLL_CFGCR2`

### ИЗМЕНЕНО:
- Pipes массив увеличен до 4 элементов
- `Configure()` использует только DDI transcoder

---

## 13. Ports.h (Ports.cpp в IntelLake отсутствует — код встроен в accelerant.cpp)

**Оригинал:** `src/add-ons/accelerants/intel_extreme/Ports.h`, `Ports.cpp`

### УДАЛЕНО:
- Все классы устаревших портов:
  - `class AnalogPort` (VGA)
  - `class LVDSPort`
  - `class DigitalPort` (DVI)
  - `class HDMIPort`
  - `class DisplayPort`
  - `class EmbeddedDisplayPort`
- Типы портов:
  - `INTEL_PORT_TYPE_ANALOG`
  - `INTEL_PORT_TYPE_DVI`
  - `INTEL_PORT_TYPE_LVDS`
  - `INTEL_PORT_TYPE_HDMI`
  - `INTEL_PORT_TYPE_DP`
  - `INTEL_PORT_TYPE_eDP`
- Методы: `SetDisplayMode()`, `IsConnected()` для каждого типа порта

### ДОБАВЛЕНО:
- Единственный тип: `INTEL_PORT_TYPE_DDI`
- Единственный класс: `class DigitalDisplayInterface`
- DDI регистры: `DDI_BUF_CTL`, `DDI_AUX_CTL`, `DDI_DP_*`

### ИЗМЕНЕНО:
- Код DDI объединяет функциональность HDMI, DisplayPort и eDP
- Детекция типа сигнала (HDMI vs DP) происходит runtime через AUX channel

---

## 14. planes.cpp (НОВЫЙ — заменяет overlay.cpp)

**Оригинал:** `src/add-ons/accelerants/intel_extreme/overlay.cpp`

### УДАЛЕНО (из overlay.cpp):
- Весь legacy Overlay Unit код:
  - `overlay_registers` структура
  - `intel_allocate_overlay()`
  - `intel_configure_overlay()`
  - `i965_configure_overlay()`
- Регистры: `INTEL_OVERLAY_*`, `OV_*`
- Поддержка форматов: UYVY, YV12, I420

### ДОБАВЛЕНО:
- Universal Planes API для Gen 9+:
  - `intel_configure_plane()`
  - `intel_disable_plane()`
- Регистры:
  - `PLANE_CTL`, `PLANE_SURF`, `PLANE_OFFSET`
  - `PLANE_SIZE`, `PLANE_STRIDE`, `PLANE_POS`
  - `PLANE_KEYVAL`, `PLANE_KEYMSK`
- Поддержка форматов: NV12, P010, Y210, Y410
- Интеграция с Panel Scaler для масштабирования
- Регистры PS_CTRL, PS_WIN_SZ для каждого plane

### ИЗМЕНЕНО:
- Полная переработка: overlay → universal planes
- До 7 planes на pipe (1 primary + 6 sprite/overlay)

---

## 15. LakePLL.cpp / LakePLL.h (НОВЫЕ — заменяют pll.cpp/pll.h + TigerLakePLL.cpp/TigerLakePLL.h)

**Оригиналы:**
- `src/add-ons/accelerants/intel_extreme/pll.cpp`
- `src/add-ons/accelerants/intel_extreme/pll.h`
- Частично: `FlexibleDisplayInterface.cpp` (FDI link rate)

### УДАЛЕНО (из pll.cpp/pll.h):
- Все legacy PLL:
  - `G45_* PLL limits`
  - `IRONLAKE_* PLL limits`
  - `SANDYBRIDGE_* PLL limits`
  - `pll_divisors` структура для VCO расчёта
- `RefClkSel()` для разных поколений
- `compute_pll_divisors()` с N, M1, M2, P1, P2

### ДОБАВЛЕНО:
- Унифицированный PLL для всех Lake поколений:
  - `skl_wrpll_params` — Skylake Wrapped Ring PLL
  - `icl_*` функции — Ice Lake Combo PHY PLL
  - `tgl_*` функции — Tiger Lake PLL
- Новая структура `lake_pll_params`:
  - `dco_frequency`, `dco_divider`
  - `pdiv`, `qdiv`, `kdiv`
  - `central_frequency`, `deviation`
- Функции:
  - `skl_compute_wrpll()` — Skylake
  - `icl_compute_combo_pll()` — Ice Lake
  - `tgl_compute_combo_pll()` — Tiger Lake
  - `compute_display_pll()` — унифицированный API
- Регистры: `DPLL_CTRL1`, `DPLL_CFGCR1`, `DPLL_CFGCR2`
- Port Clock таблицы для стандартных частот (1.62, 2.7, 5.4, 8.1 GHz)

### ОБЪЕДИНЕНО:
- pll.cpp + pll.h → LakePLL.cpp/h
- Логика TigerLakePLL → LakePLL.cpp/h
- FDI link rate расчёт → не нужен (FDI удалён)

---

## 16. power.cpp / power.h

**Оригиналы:**
- `src/add-ons/kernel/drivers/graphics/intel_extreme/power.cpp`
- `src/add-ons/kernel/drivers/graphics/intel_extreme/power.h`

### УДАЛЕНО (из power.h):
- Все INTEL6_* регистры (Sandy Bridge / Ivy Bridge):
  - `INTEL6_RP_*` — Render P-state
  - `INTEL6_RC_*` — Render C-state
  - `INTEL6_PM*` — Power Management
- Макросы:
  - `INTEL6_PCODE_*`
  - `INTEL6_PM_RP_*`
  - ~100 определений регистров

### ДОБАВЛЕНО:
- GEN9_* регистры:
  - `GEN9_MEDIA_PG_IDLE_HYSTERESIS`
  - `GEN9_RENDER_PG_IDLE_HYSTERESIS`
  - `GEN9_DC_STATE_EN`
  - `GEN9_DC_STATE_DEBUG`
- GEN11_* регистры:
  - `GEN11_GT_SLICE_ENABLE`
  - `GEN11_GT_SUBSLICE_INFO`
- GEN12_* регистры:
  - `GEN12_RPSTAT`
  - `GEN12_GT_SLICE_INFO`
- Display Power Wells:
  - `PWR_WELL_CTL_*`
  - `DC_STATE_*`
  - `FUSE_STATUS`

### ИЗМЕНЕНО:
- Forcewake механизм упрощён для Gen 9+
- Новые DC states: DC5, DC6, DC9

---

## 17. firmware.cpp / firmware.h (НОВЫЕ ФАЙЛЫ)

**Оригинал:** Нет (новая функциональность)

### ДОБАВЛЕНО:
- DMC (Display Microcontroller) firmware loading:
  - `intel_load_dmc_firmware()`
  - Имена файлов: `skl_dmc_*.bin`, `kbl_dmc_*.bin`, `icl_dmc_*.bin`, `tgl_dmc_*.bin`
- GuC (Graphics Microcontroller) firmware:
  - `intel_load_guc_firmware()`
  - Имена: `skl_guc_*.bin`, `kbl_guc_*.bin`, etc.
- HuC (HEVC Microcontroller) firmware:
  - `intel_load_huc_firmware()`
  - Для аппаратного декодирования HEVC
- Структуры:
  - `intel_fw_header` — заголовок firmware
  - `intel_dmc_header` — специфичный для DMC
  - `firmware_info` — информация о версии
- Функции:
  - `intel_fw_path()` — путь к firmware файлам
  - `intel_parse_fw_header()` — парсинг заголовка
  - `intel_upload_fw()` — загрузка в GPU
- Регистры:
  - `DMC_PROGRAM(pipe)`
  - `GUC_WOPCM_SIZE`
  - `GUC_SHIM_CONTROL`
  - `HUC_LOADING_AGENT_GUC`

---

## 18. intel_extreme.cpp (драйвер)

**Оригинал:** `src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp`

### УДАЛЕНО:
- Инициализация для pre-Gen 9:
  - Ring Buffer setup
  - Legacy interrupt handlers
  - 2D blitter configuration
- Определение device_type для старых чипов
- Регистры: `RING_BUFFER_*`, `INSTPM`, `MI_MODE`

### ДОБАВЛЕНО:
- Execlist инициализация
- Display interrupt handlers для Gen 9+
- Forcewake acquire/release
- DMC firmware upload при загрузке драйвера

### ИЗМЕНЕНО:
- `intel_extreme_init()` упрощена
- PCI ID table содержит только Gen 9+ устройства

---

## 19. intel_extreme.h (shared header)

**Оригинал:** `headers/private/graphics/intel_extreme/intel_extreme.h`

### УДАЛЕНО:
- Все PCI ID для pre-Gen 9:
  - i830, i845, i855, i865
  - i915, i945, i965
  - G33, G35, G41, G43, G45
  - Ironlake, Sandy Bridge, Ivy Bridge, Haswell, Broadwell
- Макросы `INTEL_GROUP_*` для старых поколений
- Регистры:
  - `RING_*` (ring buffer)
  - `FENCE_*` (legacy fencing)
  - VGA_* (VGA plane)
  - `INTEL_OVERLAY_*`
- Структуры:
  - `overlay_registers`
  - `ring_buffer`

### ДОБАВЛЕНО:
- Только PCI ID для Gen 9+:
  - Skylake (SKL)
  - Kaby Lake (KBL)
  - Coffee Lake (CFL)
  - Cannon Lake (CNL)
  - Ice Lake (ICL)
  - Tiger Lake (TGL)
  - Alder Lake (ADL)
  - Rocket Lake (RKL)
  - DG1
- Макросы `INTEL_SKYLAKE`, `INTEL_KABYLAKE`, etc.
- Регистры:
  - `GEN9_*` (power, display)
  - `GEN11_*`, `GEN12_*`
  - Universal Planes registers
  - DDI registers
- Структуры:
  - `ddi_info`
  - `plane_info`

### ИЗМЕНЕНО:
- `intel_shared_info` структура упрощена
- Максимум pipes: 4 (вместо 3)
- Максимум planes per pipe: 7

---

## 20. intel_extreme_private.h (драйвер)

**Оригинал:** `src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme_private.h`

### УДАЛЕНО:
- `ring_buffer ring`
- `overlay_registers* overlay_area`
- Legacy interrupt masks

### ДОБАВЛЕНО:
- `dmc_payload* dmc`
- `guc_info* guc`
- Forcewake tracking

---

## 21. driver.cpp

**Оригинал:** `src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp`

### УДАЛЕНО:
- PCI ID matching для pre-Gen 9

### ДОБАВЛЕНО:
- Только Gen 9+ PCI ID в `kSupportedDevices[]`
- Новые имена устройств для Lake series

---

## Сводная таблица изменений

| Файл | Оригинал | Статус | Основные изменения |
|------|----------|--------|-------------------|
| accelerant.cpp | accelerant.cpp | Модифицирован | DDI-only, убраны legacy ports |
| accelerant.h | accelerant.h | Модифицирован | Убраны поля 3D context |
| accelerant_protos.h | accelerant_protos.h | Модифицирован | Убраны 2D/Engine функции |
| commands.h | commands.h | Переписан | XY_* → MI_* команды |
| cursor.cpp | cursor.cpp | Переписан | 2-color → ARGB8888 |
| dpms.cpp | dpms.cpp | Модифицирован | DDI power management |
| engine.cpp | engine.cpp | Минимизирован | Убран Ring Buffer |
| hooks.cpp | hooks.cpp | Модифицирован | Убраны 2D hooks |
| mode.cpp | mode.cpp | Упрощён | Gen 9+ only |
| memory.cpp | memory.cpp | Расширен | 64KB alignment |
| PanelFitter.* | PanelFitter.* | Упрощён | Только Panel Scaler |
| Pipes.* | Pipes.* | Модифицирован | Убран FDILink |
| Ports.h | Ports.h | Переписан | Только DDI class |
| planes.cpp | overlay.cpp | **НОВЫЙ** | Universal Planes |
| LakePLL.* | pll.* + TigerLakePLL.* | **ОБЪЕДИНЁН** | Все Lake PLL |
| power.* | power.* | Модифицирован | GEN9+/GEN11+/GEN12+ |
| firmware.* | — | **НОВЫЙ** | DMC/GuC/HuC loading |
| intel_extreme.cpp | intel_extreme.cpp | Модифицирован | Gen 9+ init |
| intel_extreme.h | intel_extreme.h | Модифицирован | Gen 9+ PCI IDs & regs |
| intel_extreme_private.h | intel_extreme_private.h | Модифицирован | Firmware structs |
| driver.cpp | driver.cpp | Модифицирован | Gen 9+ device list |

---

## Итого

**Удалено поколений GPU:** 15 (i830 → Broadwell)
**Поддерживается поколений:** 8+ (Skylake → Alder Lake+)

**Ключевые архитектурные изменения:**
1. DDI как единственный тип порта (вместо 6 типов)
2. Universal Planes вместо Overlay Unit
3. Execlist вместо Ring Buffer
4. ARGB8888 курсоры вместо 2-color
5. Panel Scaler вместо Panel Fitter
6. Унифицированный LakePLL для всех поколений
7. Firmware loading (DMC, GuC, HuC)
8. Современное power management (DC states)

**Результат:** Значительно упрощённый код, ~40% меньше строк, легче поддерживать.
