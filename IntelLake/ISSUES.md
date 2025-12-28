# Найденные проблемы в IntelLake

Результат полного сравнения файлов IntelLake с оригиналом intel_extreme.

**Статус:** ВСЕ ФАЙЛЫ ПРОВЕРЕНЫ

---

## Критические ошибки (не скомпилируется)

### 1. Ports.cpp - неверные includes

**Файл:** `Ports.cpp`, строки 33-35

```cpp
#include "FlexibleDisplayInterface.h"  // ОШИБКА: файл не существует!
#include "PanelFitter.h"
#include "TigerLakePLL.h"              // ОШИБКА: должен быть LakePLL.h
```

**Исправить на:**
```cpp
#include "PanelFitter.h"
#include "LakePLL.h"
```

---

### 2. Ports.h - неверный include

**Файл:** `Ports.h`, строка 21

```cpp
#include "pll.h"  // ОШИБКА: файл не существует!
```

**Исправить на:**
```cpp
#include "LakePLL.h"
```

---

### 3. Pipes.h - неверный include

**Файл:** `Pipes.h`, строка 19

```cpp
#include "pll.h"  // ОШИБКА: файл не существует!
```

**Исправить на:**
```cpp
#include "LakePLL.h"
```

---

### 4. mode.cpp - неверный include

**Файл:** `mode.cpp`, строка 30

```cpp
#include "pll.h"  // ОШИБКА: файл не существует!
```

**Исправить на:**
```cpp
#include "LakePLL.h"
```

---

### 5. intel_extreme.h - отсутствует TGL_DPLL_SSC_ENABLE

**Файл:** `intel_extreme.h`

**Проблема:** Константа `TGL_DPLL_SSC_ENABLE` используется в LakePLL.cpp:893, но не определена.

**Добавить после TGL_DPLL_LOCK:**
```cpp
#define TGL_DPLL_SSC_ENABLE             (1 << 25)
```

---

### 6. intel_extreme.h - отсутствуют поля overlay в intel_shared_info

**Файл:** `intel_extreme.h`, структура `intel_shared_info`

**Проблема:** planes.cpp использует поля `overlay_channel_used` и `overlay_token`, которых нет в структуре.

**Добавить в struct intel_shared_info:**
```cpp
// Universal Planes overlay state
bool        overlay_channel_used;
uint32      overlay_token;
```

---

### 7. intel_extreme_private.h - отсутствует struct intel_info

**Файл:** `intel_extreme_private.h`

**КРИТИЧЕСКАЯ ПРОБЛЕМА:** Структура `intel_info` отсутствует!

Оригинал содержит:
```cpp
struct intel_info {
    int32           open_count;
    status_t        init_status;
    int32           id;
    pci_info*       pci;
    addr_t          aperture_base;
    aperture_id     aperture;
    addr_t          registers;
    area_id         registers_area;
    struct intel_shared_info* shared_info;
    area_id         shared_area;
    struct overlay_registers* overlay_registers;
    bool            fake_interrupts;
    uint8           irq;
    bool            use_msi;
    const char*     device_identifier;
    DeviceType      device_type;
    enum pch_info   pch_info;
};
```

**Добавить в intel_extreme_private.h** (или проверить, что определена в intel_extreme.h).

---

### 8. intel_extreme_private.h - отсутствуют прототипы функций

**Файл:** `intel_extreme_private.h`

**Отсутствуют:**
```cpp
extern bool parse_vbt_from_bios(struct intel_shared_info* info);
extern status_t intel_free_memory(intel_info& info, addr_t offset);
extern status_t intel_allocate_memory(intel_info& info, size_t size,
    size_t alignment, uint32 flags, addr_t* _offset,
    phys_addr_t* _physicalBase = NULL);
extern status_t intel_extreme_init(intel_info& info);
extern void intel_extreme_uninit(intel_info& info);
```

---

## Проблемы с legacy кодом (средний приоритет)

### 9. accelerant.cpp - legacy overlay_registers

**Файл:** `accelerant.cpp`, строки 120-124

```cpp
if (gInfo->shared_info->overlay_offset != 0) {
    gInfo->overlay_registers = (struct overlay_registers*)
        (gInfo->shared_info->graphics_memory
        + gInfo->shared_info->overlay_offset);
}
```

**Решение:** Удалить этот блок (Gen9+ использует Universal Planes)

---

### 10. accelerant.h - legacy поля и функции

**Legacy overlay поля (удалить):**
```cpp
struct overlay_registers* overlay_registers;
overlay_view last_overlay_view;
overlay_frame last_overlay_frame;
uint32 last_horizontal_overlay_scale;
uint32 last_vertical_overlay_scale;
uint32 overlay_position_buffer_offset;
```

**Ring buffer функции (удалить):**
```cpp
void setup_ring_buffer(ring_buffer &ringBuffer, const char* name);
void uninit_ring_buffer(ring_buffer &ringBuffer);
```

**Добавить для planes.cpp:**
```cpp
// Gen 9+ Universal Planes state
int     current_overlay_pipe;
int     current_overlay_plane;
int     current_overlay_scaler;
uint32  current_plane_ctl;
```

---

## Отсутствующие файлы

### 11. memory.cpp

**Статус:** Отсутствует

**Нужен для:** `intel_allocate_memory()`, `intel_free_memory()` в planes.cpp

**Решение:**
```bash
cp src/add-ons/accelerants/intel_extreme/memory.cpp IntelLake/
```

---

### 12. Jamfile

**Статус:** Отсутствует

**Нужен для:** сборки accelerant и driver

---

## Сводная таблица

| Файл | Проблема | Критичность |
|------|----------|-------------|
| Ports.cpp:33 | `#include "FlexibleDisplayInterface.h"` | :red_circle: КРИТИЧЕСКАЯ |
| Ports.cpp:35 | `#include "TigerLakePLL.h"` | :red_circle: КРИТИЧЕСКАЯ |
| Ports.h:21 | `#include "pll.h"` | :red_circle: КРИТИЧЕСКАЯ |
| Pipes.h:19 | `#include "pll.h"` | :red_circle: КРИТИЧЕСКАЯ |
| mode.cpp:30 | `#include "pll.h"` | :red_circle: КРИТИЧЕСКАЯ |
| intel_extreme.h | `TGL_DPLL_SSC_ENABLE` отсутствует | :red_circle: КРИТИЧЕСКАЯ |
| intel_extreme.h | `overlay_channel_used` отсутствует | :red_circle: КРИТИЧЕСКАЯ |
| intel_extreme.h | `overlay_token` отсутствует | :red_circle: КРИТИЧЕСКАЯ |
| intel_extreme_private.h | `struct intel_info` отсутствует | :red_circle: КРИТИЧЕСКАЯ |
| intel_extreme_private.h | function prototypes отсутствуют | :red_circle: КРИТИЧЕСКАЯ |
| accelerant.cpp:120-124 | legacy overlay_registers | :yellow_circle: Средняя |
| accelerant.h | legacy overlay поля | :yellow_circle: Средняя |
| accelerant.h:106-107 | ring_buffer функции | :yellow_circle: Средняя |
| memory.cpp | отсутствует | :yellow_circle: Средняя |
| Jamfile | отсутствует | :yellow_circle: Средняя |

---

## Положительные изменения (корректно рефакторено)

### Accelerant

- **dpms.cpp** - удалён legacy PLL код, добавлены PRM ссылки
- **engine.cpp** - удалён QueueCommands и 2D blitter
- **hooks.cpp** - исправлен fall-through баг, удалены 2D хуки
- **cursor.cpp** - переписан для Gen9+ ARGB курсоров
- **commands.h** - упрощён до MI_* команд
- **planes.cpp** - новая реализация Universal Planes (687 строк)
- **LakePLL.cpp** - унифицированный PLL для Gen 9/11/12 (979 строк)
- **PanelFitter.cpp/h** - обновлён для Gen9+ Panel Scaler
- **accelerant_protos.h** - удалены legacy 2D/overlay прототипы

### Driver

- **driver.cpp** - только Gen9+ устройства, удалены legacy PCH
- **intel_extreme.cpp** - Gen9+ проверка, удалён legacy interrupt код
- **power.cpp/h** - Gen9+ power management регистры
- **intel_extreme_private.h** - Gen9+ хелперы (но отсутствует struct intel_info!)

---

## Команды для быстрого исправления

```bash
cd /home/ruslan/haiku/IntelLake

# 1. Скопировать memory.cpp
cp ../src/add-ons/accelerants/intel_extreme/memory.cpp .

# 2. Исправить includes
sed -i 's/#include "FlexibleDisplayInterface.h"//' Ports.cpp
sed -i 's/#include "TigerLakePLL.h"/#include "LakePLL.h"/' Ports.cpp
sed -i 's/#include "pll.h"/#include "LakePLL.h"/' Ports.h
sed -i 's/#include "pll.h"/#include "LakePLL.h"/' Pipes.h
sed -i 's/#include "pll.h"/#include "LakePLL.h"/' mode.cpp

# 3. РУЧНОЕ ИСПРАВЛЕНИЕ ТРЕБУЕТСЯ:
#    - intel_extreme.h: добавить TGL_DPLL_SSC_ENABLE, overlay_channel_used, overlay_token
#    - intel_extreme_private.h: добавить struct intel_info и прототипы функций
#    - accelerant.h: удалить legacy overlay поля, добавить planes state
#    - Создать Jamfile
```

---

## Сравнение файлов - ЗАВЕРШЕНО

| Файл | Статус |
|------|--------|
| accelerant.cpp | :white_check_mark: Проверен |
| accelerant.h | :white_check_mark: Проверен |
| accelerant_protos.h | :white_check_mark: Проверен |
| Ports.cpp | :white_check_mark: Проверен |
| Ports.h | :white_check_mark: Проверен |
| Pipes.cpp | :white_check_mark: Проверен |
| Pipes.h | :white_check_mark: Проверен |
| mode.cpp | :white_check_mark: Проверен |
| dpms.cpp | :white_check_mark: Проверен |
| engine.cpp | :white_check_mark: Проверен |
| hooks.cpp | :white_check_mark: Проверен |
| cursor.cpp | :white_check_mark: Проверен |
| commands.h | :white_check_mark: Проверен |
| planes.cpp | :white_check_mark: Проверен |
| LakePLL.cpp | :white_check_mark: Проверен |
| LakePLL.h | :white_check_mark: Проверен |
| PanelFitter.cpp | :white_check_mark: Проверен |
| PanelFitter.h | :white_check_mark: Проверен |
| driver.cpp | :white_check_mark: Проверен |
| intel_extreme.cpp | :white_check_mark: Проверен |
| intel_extreme.h | :white_check_mark: Проверен |
| intel_extreme_private.h | :white_check_mark: Проверен |
| power.cpp | :white_check_mark: Проверен |
| power.h | :white_check_mark: Проверен |