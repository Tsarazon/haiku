# Карта взаимных зависимостей драйвера Intel Extreme

## Архитектурная диаграмма

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           ПОЛЬЗОВАТЕЛЬСКОЕ ПРОСТРАНСТВО                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                    ACCELERANT (intel_extreme.accelerant)             │    │
│  │  ┌────────────────────────────────────────────────────────────────┐ │    │
│  │  │                         accelerant.h                           │ │    │
│  │  │  - gInfo (глобальный accelerant_info)                          │ │    │
│  │  │  - read32/write32 (доступ к регистрам)                         │ │    │
│  │  └───────────────────────────┬────────────────────────────────────┘ │    │
│  │                              │                                      │    │
│  │  ┌───────────────────────────┴────────────────────────────────────┐ │    │
│  │  │                    КЛАССЫ ВИДЕОПОРТОВ                          │ │    │
│  │  │  ┌──────────────────────────────────────────────────────────┐  │ │    │
│  │  │  │                     Ports.h / Ports.cpp                   │  │ │    │
│  │  │  │  ┌─────────────┐                                         │  │ │    │
│  │  │  │  │    Port     │ ◄─── базовый абстрактный класс          │  │ │    │
│  │  │  │  └──────┬──────┘                                         │  │ │    │
│  │  │  │         │                                                │  │ │    │
│  │  │  │  ┌──────┴──────┬──────────┬──────────┬─────────┐        │  │ │    │
│  │  │  │  ▼             ▼          ▼          ▼         ▼        │  │ │    │
│  │  │  │ AnalogPort  LVDSPort  DigitalPort DisplayPort  DDI     │  │ │    │
│  │  │  │                          │           │                  │  │ │    │
│  │  │  │                     ┌────┴────┐ ┌────┴────┐             │  │ │    │
│  │  │  │                     ▼         ▼ ▼         ▼             │  │ │    │
│  │  │  │                  HDMIPort   EmbeddedDisplayPort         │  │ │    │
│  │  │  └──────────────────────────────────────────────────────┘  │ │    │
│  │  └────────────────────────────────────────────────────────────┘ │    │
│  │                              │ использует                        │    │
│  │  ┌───────────────────────────┴────────────────────────────────┐ │    │
│  │  │                    КЛАССЫ КОНВЕЙЕРОВ (PIPES)               │ │    │
│  │  │  ┌──────────────────────────────────────────────────────┐  │ │    │
│  │  │  │                   Pipes.h / Pipes.cpp                 │  │ │    │
│  │  │  │  ┌─────────────────────────────────────────────────┐ │  │ │    │
│  │  │  │  │                     Pipe                        │ │  │ │    │
│  │  │  │  │  - Configure()    - ConfigureTimings()          │ │  │ │    │
│  │  │  │  │  - SetFDILink()   - ConfigureClocks()           │ │  │ │    │
│  │  │  │  │  ┌─────────────┐  ┌─────────────────┐           │ │  │ │    │
│  │  │  │  │  │  FDILink*   │  │  PanelFitter*   │           │ │  │ │    │
│  │  │  │  │  └─────────────┘  └─────────────────┘           │ │  │ │    │
│  │  │  │  └─────────────────────────────────────────────────┘ │  │ │    │
│  │  │  └──────────────────────────────────────────────────────┘  │ │    │
│  │  └────────────────────────────────────────────────────────────┘ │    │
│  │                              │ использует                        │    │
│  │  ┌───────────────────────────┴────────────────────────────────┐ │    │
│  │  │                    ВСПОМОГАТЕЛЬНЫЕ КЛАССЫ                  │ │    │
│  │  │  ┌────────────────────────────────────────────────────────┐│ │    │
│  │  │  │ FlexibleDisplayInterface.h    PanelFitter.h   pll.h    ││ │    │
│  │  │  │  ┌───────────────┐ ┌──────────────┐ ┌─────────────────┐││ │    │
│  │  │  │  │ FDITransmitter│ │ PanelFitter  │ │ pll_divisors    │││ │    │
│  │  │  │  │ FDIReceiver   │ │              │ │ pll_limits      │││ │    │
│  │  │  │  │ FDILink       │ │              │ │ skl_wrpll_params│││ │    │
│  │  │  │  └───────────────┘ └──────────────┘ └─────────────────┘││ │    │
│  │  │  └────────────────────────────────────────────────────────┘│ │    │
│  │  │  ┌────────────────────────────────────────────────────────┐│ │    │
│  │  │  │ TigerLakePLL.cpp  - PLL для Gen12 (TigerLake/AlderLake)││ │    │
│  │  │  └────────────────────────────────────────────────────────┘│ │    │
│  │  └────────────────────────────────────────────────────────────┘ │    │
│  │                              │                                   │    │
│  │  ┌───────────────────────────┴────────────────────────────────┐ │    │
│  │  │               ФУНКЦИОНАЛЬНЫЕ МОДУЛИ                         │ │    │
│  │  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐  │ │    │
│  │  │  │ mode.cpp │ │engine.cpp│ │memory.cpp│ │   dpms.cpp    │  │ │    │
│  │  │  │ режимы   │ │ ring buf │ │ GART mem │ │ питание дисп. │  │ │    │
│  │  │  └──────────┘ └──────────┘ └──────────┘ └───────────────┘  │ │    │
│  │  │  ┌──────────┐ ┌──────────┐ ┌──────────┐                     │ │    │
│  │  │  │cursor.cpp│ │overlay.cp│ │ hooks.cpp│                     │ │    │
│  │  │  │ курсор   │ │ оверлей  │ │ хуки API │                     │ │    │
│  │  │  └──────────┘ └──────────┘ └──────────┘                     │ │    │
│  │  └────────────────────────────────────────────────────────────┘ │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                              │                                           │
│                              │ IOCTL (intel_get_private_data)            │
│                              ▼                                           │
├─────────────────────────────────────────────────────────────────────────┤
│                          ГРАНИЦА ЯДРА                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                           ПРОСТРАНСТВО ЯДРА                              │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                   KERNEL DRIVER (intel_extreme)                   │   │
│  │  ┌─────────────────────────────────────────────────────────────┐ │   │
│  │  │                       driver.h                               │ │   │
│  │  │  - gDeviceInfo[]    - gPCI (pci_module_info)                │ │   │
│  │  │  - gGART (agp_gart) - read32/write32                        │ │   │
│  │  └────────────────────────────┬────────────────────────────────┘ │   │
│  │                               │                                   │   │
│  │  ┌────────────────────────────┴───────────────────────────────┐  │   │
│  │  │              intel_extreme_private.h                        │  │   │
│  │  │  ┌────────────────────────────────────────────────────────┐│  │   │
│  │  │  │                    intel_info                          ││  │   │
│  │  │  │  - pci_info*         - aperture_id                     ││  │   │
│  │  │  │  - registers         - shared_info (intel_shared_info*)││  │   │
│  │  │  │  - device_type       - pch_info                        ││  │   │
│  │  │  │  - irq, use_msi      - overlay_registers               ││  │   │
│  │  │  └────────────────────────────────────────────────────────┘│  │   │
│  │  │  Функции:                                                   │  │   │
│  │  │  - intel_extreme_init()    - intel_extreme_uninit()        │  │   │
│  │  │  - intel_allocate_memory() - intel_free_memory()           │  │   │
│  │  │  - parse_vbt_from_bios()                                    │  │   │
│  │  └────────────────────────────────────────────────────────────┘  │   │
│  │                               │                                   │   │
│  │  ┌────────────────────────────┴───────────────────────────────┐  │   │
│  │  │                  ИСХОДНЫЕ ФАЙЛЫ ЯДРА                        │  │   │
│  │  │  ┌──────────────────┐ ┌────────────────┐ ┌───────────────┐ │  │   │
│  │  │  │    driver.cpp    │ │   device.cpp   │ │intel_extreme. │ │  │   │
│  │  │  │  init/uninit     │ │  open/close    │ │     cpp       │ │  │   │
│  │  │  │  publish_devices │ │  ioctl         │ │ инициализация │ │  │   │
│  │  │  └──────────────────┘ └────────────────┘ └───────────────┘ │  │   │
│  │  │  ┌──────────────────┐ ┌────────────────┐                   │  │   │
│  │  │  │    bios.cpp      │ │   power.cpp    │                   │  │   │
│  │  │  │  VBT parsing     │ │  PM functions  │                   │  │   │
│  │  │  └──────────────────┘ └────────────────┘                   │  │   │
│  │  └────────────────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                              │                                           │
│                              ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                      ВНЕШНИЕ ЗАВИСИМОСТИ ЯДРА                     │   │
│  │  ┌─────────────────┐ ┌──────────────────┐ ┌───────────────────┐  │   │
│  │  │  PCI Module     │ │   AGP/GART       │ │  kernel_cpp.cpp   │  │   │
│  │  │ pci_module_info │ │ agp_gart_module  │ │  (C++ runtime)    │  │   │
│  │  │ read/write_pci  │ │ intel_gart.cpp   │ │                   │  │   │
│  │  └─────────────────┘ └──────────────────┘ └───────────────────┘  │   │
│  │  ┌─────────────────────────────────────────────────────────────┐ │   │
│  │  │                    libgraphicscommon.a                       │ │   │
│  │  │  (общая библиотека для графических драйверов)                │ │   │
│  │  └─────────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Структура файлов и зависимости

### Заголовочные файлы (Shared Headers)

**Путь:** `headers/private/graphics/intel_extreme/`

| Файл | Зависимости | Описание |
|------|-------------|----------|
| `intel_extreme.h` | `lock.h`, `<Accelerant.h>`, `<Drivers.h>`, `<PCI.h>`, `<edid.h>` | Основной заголовок: структуры данных, регистры, константы |
| `lock.h` | `<OS.h>` | Примитивы синхронизации |

### Драйвер ядра (Kernel Driver)

**Путь:** `src/add-ons/kernel/drivers/graphics/intel_extreme/`

| Файл | Зависит от | Предоставляет |
|------|------------|---------------|
| `driver.cpp` | `driver.h`, PCI, GART | `init_driver()`, `publish_devices()` |
| `device.cpp` | `driver.h`, `intel_extreme_private.h` | `open_hook()`, `close_hook()`, `ioctl()` |
| `intel_extreme.cpp` | `intel_extreme_private.h`, GART | `intel_extreme_init()`, `intel_extreme_uninit()` |
| `bios.cpp` | `intel_extreme_private.h` | `parse_vbt_from_bios()` |
| `power.cpp` | `power.h` | Управление питанием устройства |

### Акселерант (Accelerant)

**Путь:** `src/add-ons/accelerants/intel_extreme/`

| Файл | Зависит от | Предоставляет |
|------|------------|---------------|
| `accelerant.cpp` | `accelerant.h`, `Ports.h`, `Pipes.h` | `init_accelerant()`, `clone_accelerant()` |
| `Ports.cpp` | `Ports.h`, `Pipes.h`, `pll.h`, `<dp.h>` | Классы портов (HDMI, DP, LVDS и др.) |
| `Pipes.cpp` | `Pipes.h`, `FlexibleDisplayInterface.h`, `PanelFitter.h` | Класс `Pipe` |
| `FlexibleDisplayInterface.cpp` | `FlexibleDisplayInterface.h` | `FDILink`, `FDITransmitter`, `FDIReceiver` |
| `PanelFitter.cpp` | `PanelFitter.h` | Масштабирование панели |
| `pll.cpp` | `pll.h`, `accelerant.h` | Расчёт делителей PLL |
| `TigerLakePLL.cpp` | `TigerLakePLL.h`, `pll.h` | PLL для Gen12 |
| `mode.cpp` | `accelerant.h` | Управление видеорежимами |
| `engine.cpp` | `accelerant.h` | Ring buffer для команд GPU |
| `memory.cpp` | `accelerant.h` | Выделение видеопамяти |
| `cursor.cpp` | `accelerant.h` | Аппаратный курсор |
| `dpms.cpp` | `accelerant.h` | Управление питанием дисплея |
| `overlay.cpp` | `accelerant.h` | Видеооверлей |
| `hooks.cpp` | `accelerant_protos.h` | Экспорт функций API |

## Граф зависимостей классов

```
                          intel_extreme.h
                               │
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
         ▼                     ▼                     ▼
    DeviceType            intel_shared_info     Enumerations
         │                     │                (port_index,
         │           ┌─────────┼─────────┐       pipe_index,
         │           │         │         │       pch_info)
         │           ▼         ▼         ▼
         │      ring_buffer  pll_info  child_device_config
         │
         └────────────────────┬───────────────────────┐
                              │                       │
              ┌───────────────┴───────────────┐       │
              ▼                               ▼       │
       [KERNEL DRIVER]                 [ACCELERANT]   │
              │                               │       │
              ▼                               ▼       │
        intel_info                    accelerant_info │
              │                               │       │
              │ shared_info*                  │       │
              └─────────────►─────────────────┘       │
                                              │       │
                                ┌─────────────┼───────┘
                                ▼             ▼
                              Port*[]       Pipe*[]
                                │             │
                    ┌───────────┼─────────────┼────────────┐
                    ▼           ▼             ▼            ▼
               AnalogPort   LVDSPort     DigitalPort   DisplayPort
                                              │            │
                                         ┌────┴────┐       ▼
                                         ▼         ▼   EmbeddedDP
                                      HDMIPort    DDI
                                              │
                                              ▼
                                           Pipe
                                              │
                                     ┌────────┴────────┐
                                     ▼                 ▼
                                 FDILink          PanelFitter
                                     │
                              ┌──────┴──────┐
                              ▼             ▼
                       FDITransmitter   FDIReceiver
```

## Поток данных между компонентами

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            ПОТОК ДАННЫХ                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  1. ИНИЦИАЛИЗАЦИЯ                                                        │
│     ┌─────────────┐    ┌──────────────┐    ┌─────────────────────────┐  │
│     │  PCI Bus    │───►│ driver.cpp   │───►│ intel_extreme_init()    │  │
│     │  Probe      │    │ find_device  │    │ - маппинг регистров     │  │
│     └─────────────┘    └──────────────┘    │ - создание shared_area  │  │
│                                             │ - парсинг VBT           │  │
│                                             └───────────┬─────────────┘  │
│                                                         │                │
│  2. ПОДКЛЮЧЕНИЕ АКСЕЛЕРАНТА                             ▼                │
│     ┌─────────────────┐   IOCTL    ┌──────────────────────────────────┐ │
│     │ accelerant.cpp  │◄──────────►│  intel_shared_info (shared mem)  │ │
│     │ init_accelerant │            │  - режимы, регистры, состояние   │ │
│     └────────┬────────┘            └──────────────────────────────────┘ │
│              │                                                           │
│  3. ОБНАРУЖЕНИЕ ПОРТОВ                                                   │
│              ▼                                                           │
│     ┌─────────────────┐                                                  │
│     │   Ports.cpp     │                                                  │
│     │ - проверка VBT  │                                                  │
│     │ - чтение EDID   │                                                  │
│     │ - I2C/DP AUX    │                                                  │
│     └────────┬────────┘                                                  │
│              │                                                           │
│  4. НАСТРОЙКА РЕЖИМА                                                     │
│              ▼                                                           │
│     ┌─────────────────┐    ┌─────────────┐    ┌───────────────────────┐ │
│     │   mode.cpp      │───►│  pll.cpp    │───►│    Pipes.cpp          │ │
│     │ set_display_mode│    │ вычисление  │    │ - ConfigureTimings()  │ │
│     └─────────────────┘    │ делителей   │    │ - ConfigureClocks()   │ │
│                             └─────────────┘    └───────────┬───────────┘ │
│                                                            │             │
│  5. АКТИВАЦИЯ ВЫВОДА                                       ▼             │
│     ┌─────────────────────────────────────────────────────────────────┐ │
│     │                    Port::SetDisplayMode()                        │ │
│     │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │ │
│     │  │  FDILink    │  │ PanelFitter │  │ Регистры трансмиттера  │  │ │
│     │  │  Train()    │  │ Enable()    │  │ (HDMI/DP/LVDS)         │  │ │
│     │  └─────────────┘  └─────────────┘  └─────────────────────────┘  │ │
│     └─────────────────────────────────────────────────────────────────┘ │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Поддержка поколений GPU

| Поколение | Группы | Особенности |
|-----------|--------|-------------|
| Gen2 | 83x, 85x | Базовый драйвер, без HDMI |
| Gen3 | 91x, 94x, G33, PIN | Улучшенная производительность |
| Gen4 | 96x, G4x | Первая поддержка HDMI |
| Gen5 | ILK (IronLake) | FDI Link, PCH |
| Gen6 | SNB (SandyBridge) | Улучшенный FDI training |
| Gen7 | IVB, HAS, VLV | DDI (Digital Display Interface) |
| Gen8 | CHV, BDW | Новая архитектура DDI |
| Gen9 | SKY, KBY, CFL, CML | WRPLL для DDI |
| Gen11 | JSL | Новые регистры |
| Gen12 | TGL, ALD | TigerLakePLL, новая архитектура |

## Внешние зависимости

### Системные заголовки Haiku

- `<Accelerant.h>` - API акселеранта
- `<Drivers.h>` - API драйверов
- `<PCI.h>` - работа с PCI
- `<AGP.h>` - управление GART
- `<KernelExport.h>` - экспорт функций ядра
- `<edid.h>` - парсинг EDID
- `<dp.h>` - протокол DisplayPort

### Библиотеки

- `libgraphicscommon.a` - общие функции графических драйверов (ядро)
- `libaccelerantscommon.a` - общие функции акселерантов
- `be` - библиотека BeAPI
- `libstdc++` - C++ runtime

## Ключевые структуры данных

### intel_shared_info (разделяемая память ядро ↔ акселерант)

```cpp
struct intel_shared_info {
    // Режимы отображения
    area_id mode_list_area;
    uint32 mode_count;
    display_mode current_mode;
    display_timing panel_timing;

    // Фреймбуфер
    addr_t frame_buffer;
    uint32 frame_buffer_offset;
    uint32 bytes_per_row;
    uint32 bits_per_pixel;

    // Регистры
    area_id registers_area;
    uint32 register_blocks[REGISTER_BLOCK_COUNT];

    // Видеопамять
    uint8* graphics_memory;
    phys_addr_t physical_graphics_memory;
    uint32 graphics_memory_size;

    // Синхронизация
    struct lock accelerant_lock;
    struct lock engine_lock;
    ring_buffer primary_ring_buffer;

    // Курсор и оверлей
    bool hardware_cursor_enabled;
    sem_id vblank_sem;

    // Информация об устройстве
    DeviceType device_type;
    enum pch_info pch_info;
    struct pll_info pll_info;

    // VBT конфигурация
    bool got_vbt;
    uint32 device_config_count;
    child_device_config device_configs[10];
};
```

### intel_info (приватная структура драйвера ядра)

```cpp
struct intel_info {
    int32 open_count;
    status_t init_status;
    pci_info* pci;

    // Память и регистры
    addr_t aperture_base;
    aperture_id aperture;
    addr_t registers;
    area_id registers_area;

    // Разделяемые данные
    struct intel_shared_info* shared_info;
    area_id shared_area;

    // Прерывания
    bool fake_interrupts;
    uint8 irq;
    bool use_msi;

    // Тип устройства
    DeviceType device_type;
    enum pch_info pch_info;
};
```

## Иерархия классов портов

```
Port (абстрактный базовый класс)
├── AnalogPort (VGA/CRT)
├── LVDSPort (встроенные панели ноутбуков)
├── DigitalPort (DVI)
│   └── HDMIPort
├── DisplayPort
│   └── EmbeddedDisplayPort (eDP)
└── DigitalDisplayInterface (DDI, Gen8+)
```

## Взаимодействие Port ↔ Pipe

```
     Port                           Pipe
       │                              │
       │  SetPipe(pipe)               │
       ├─────────────────────────────►│
       │                              │
       │  GetPipe()                   │
       │◄─────────────────────────────┤
       │                              │
       │  SetDisplayMode()            │
       ├──────────┐                   │
       │          │ Configure()       │
       │          ├──────────────────►│
       │          │ ConfigureTimings()│
       │          ├──────────────────►│
       │          │ ConfigureClocks() │
       │          ├──────────────────►│
       │          │                   │
       │          │ FDI()->Train()    │
       │          ├──────────────────►│ (если PCH)
       │          │                   │
       │          │ PFT()->Enable()   │
       │          ├──────────────────►│ (если нужно масштабирование)
       │◄─────────┘                   │
```

## Файлы сборки

### Драйвер ядра (Jamfile)

```jam
KernelAddon intel_extreme :
    bios.cpp
    driver.cpp
    device.cpp
    intel_extreme.cpp
    power.cpp
    kernel_cpp.cpp
    : libgraphicscommon.a
;
```

### Акселерант (Jamfile)

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
    FlexibleDisplayInterface.cpp
    PanelFitter.cpp
    Ports.cpp
    Pipes.cpp
    TigerLakePLL.cpp
    : be $(TARGET_LIBSTDC++) libaccelerantscommon.a
;
```

## Плоская структура включений (#include)

### Общие заголовочные файлы

**lock.h**
- `<OS.h>`

**intel_extreme.h**
- `"lock.h"`
- `<Accelerant.h>`
- `<Drivers.h>`
- `<PCI.h>`
- `<edid.h>`

---

### Драйвер ядра (Kernel Driver)

#### Заголовочные файлы

**intel_extreme_private.h**
- `<AGP.h>`
- `<KernelExport.h>`
- `<PCI.h>`
- `"intel_extreme.h"` → `"lock.h"` → `<OS.h>`
- `"lock.h"`

**driver.h**
- `<KernelExport.h>`
- `<PCI.h>`
- `<kernel/lock.h>`
- `"intel_extreme_private.h"` → `"intel_extreme.h"` → `"lock.h"`

**device.h**
- `<Drivers.h>`

**power.h**
- `<string.h>`
- `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"` → `"lock.h"`

#### Исходные файлы

**driver.cpp**
- `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"` → `"lock.h"`
- `"device.h"`
- `"lock.h"`
- `<stdlib.h>`, `<stdio.h>`, `<string.h>`
- `<AGP.h>`, `<KernelExport.h>`, `<OS.h>`, `<PCI.h>`, `<SupportDefs.h>`

**device.cpp**
- `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"` → `"lock.h"`
- `"device.h"`
- `"intel_extreme.h"`
- `<OS.h>`, `<KernelExport.h>`, `<Drivers.h>`, `<PCI.h>`, `<SupportDefs.h>`
- `<graphic_driver.h>`, `<image.h>`
- `<stdlib.h>`, `<string.h>`

**intel_extreme.cpp**
- `"intel_extreme.h"` → `"lock.h"`
- `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"`
- `"power.h"` → `"driver.h"` → `"intel_extreme_private.h"`
- `<unistd.h>`, `<stdio.h>`, `<string.h>`, `<errno.h>`
- `<AreaKeeper.h>`, `<boot_item.h>`, `<driver_settings.h>`
- `<util/kernel_cpp.h>`, `<vesa_info.h>`

**bios.cpp**
- `"intel_extreme.h"` → `"lock.h"`
- `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"`
- `<string.h>`, `<stdio.h>`
- `<KernelExport.h>`, `<directories.h>`, `<driver_settings.h>`

**power.cpp**
- `"power.h"` → `"driver.h"` → `"intel_extreme_private.h"` → `"intel_extreme.h"` → `"lock.h"`

---

### Акселерант (Accelerant)

#### Заголовочные файлы

**accelerant_protos.h**
- `<Accelerant.h>`
- `"video_overlay.h"`

**pll.h**
- `"intel_extreme.h"` → `"lock.h"`

**FlexibleDisplayInterface.h**
- `"intel_extreme.h"` → `"lock.h"`

**PanelFitter.h**
- `"intel_extreme.h"` → `"lock.h"`

**TigerLakePLL.h**
- `"intel_extreme.h"` → `"lock.h"`
- `<SupportDefs.h>`

**Pipes.h**
- `<edid.h>`
- `"intel_extreme.h"` → `"lock.h"`
- `"pll.h"` → `"intel_extreme.h"`
- `"FlexibleDisplayInterface.h"` → `"intel_extreme.h"`
- `"PanelFitter.h"` → `"intel_extreme.h"`

**Ports.h**
- `<dp.h>`
- `<edid.h>`
- `"intel_extreme.h"` → `"lock.h"`
- `"Pipes.h"` → `"pll.h"`, `"FlexibleDisplayInterface.h"`, `"PanelFitter.h"`
- `"pll.h"` → `"intel_extreme.h"`

**accelerant.h**
- `"intel_extreme.h"` → `"lock.h"`
- `<Debug.h>`
- `<edid.h>`
- `<video_overlay.h>`
- `"Ports.h"` → `"Pipes.h"` → `"pll.h"`, `"FlexibleDisplayInterface.h"`, `"PanelFitter.h"`
- `"Pipes.h"` → `"pll.h"`, `"FlexibleDisplayInterface.h"`, `"PanelFitter.h"`

**commands.h**
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`

#### Исходные файлы

**accelerant.cpp**
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `<Debug.h>`, `<AGP.h>`, `<AutoDeleterOS.h>`
- `<errno.h>`, `<stdlib.h>`, `<string.h>`, `<unistd.h>`, `<syslog.h>`
- `<new>`

**Ports.cpp**
- `"Ports.h"` → `"intel_extreme.h"`, `"Pipes.h"`, `"pll.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"accelerant_protos.h"`
- `"intel_extreme.h"`
- `"FlexibleDisplayInterface.h"` → `"intel_extreme.h"`
- `"PanelFitter.h"` → `"intel_extreme.h"`
- `"TigerLakePLL.h"` → `"intel_extreme.h"`
- `<ddc.h>`, `<dp_raw.h>`
- `<stdlib.h>`, `<string.h>`, `<Debug.h>`, `<KernelExport.h>`
- `<new>`

**Pipes.cpp**
- `"Pipes.h"` → `"intel_extreme.h"`, `"pll.h"`, `"FlexibleDisplayInterface.h"`, `"PanelFitter.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"accelerant_protos.h"`
- `"intel_extreme.h"`
- `<stdlib.h>`, `<string.h>`
- `<new>`

**FlexibleDisplayInterface.cpp**
- `"FlexibleDisplayInterface.h"` → `"intel_extreme.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"intel_extreme.h"`
- `<stdlib.h>`, `<string.h>`, `<Debug.h>`, `<KernelExport.h>`

**PanelFitter.cpp**
- `"PanelFitter.h"` → `"intel_extreme.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"intel_extreme.h"`
- `<stdlib.h>`, `<string.h>`, `<Debug.h>`

**pll.cpp**
- `"pll.h"` → `"intel_extreme.h"`
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `<math.h>`, `<stdio.h>`, `<string.h>`, `<Debug.h>`
- `<create_display_modes.h>`, `<ddc.h>`, `<edid.h>`, `<validate_display_mode.h>`

**TigerLakePLL.cpp**
- `"TigerLakePLL.h"` → `"intel_extreme.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `<math.h>`

**mode.cpp**
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"pll.h"` → `"intel_extreme.h"`
- `"Ports.h"` → `"intel_extreme.h"`, `"Pipes.h"`, `"pll.h"`
- `<algorithm>`, `<math.h>`, `<stdio.h>`, `<string.h>`, `<Debug.h>`
- `<create_display_modes.h>`, `<ddc.h>`, `<edid.h>`, `<validate_display_mode.h>`

**engine.cpp**
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"accelerant_protos.h"`
- `"commands.h"` → `"accelerant.h"`
- `<Debug.h>`

**memory.cpp**
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"intel_extreme.h"`
- `<errno.h>`, `<unistd.h>`

**cursor.cpp**
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `<string.h>`

**dpms.cpp**
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`

**overlay.cpp**
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`
- `"accelerant_protos.h"`
- `"commands.h"` → `"accelerant.h"`
- `<Debug.h>`, `<AGP.h>`
- `<math.h>`, `<stdlib.h>`, `<string.h>`

**hooks.cpp**
- `"accelerant_protos.h"`
- `"accelerant.h"` → `"intel_extreme.h"`, `"Ports.h"`, `"Pipes.h"`

---

### Сводная таблица: что включает что

| Файл | Прямые включения | Транзитивные зависимости |
|------|------------------|--------------------------|
| `lock.h` | `<OS.h>` | — |
| `intel_extreme.h` | `lock.h` | `<OS.h>` |
| `intel_extreme_private.h` | `intel_extreme.h`, `lock.h` | `<OS.h>` |
| `driver.h` | `intel_extreme_private.h` | `intel_extreme.h` → `lock.h` → `<OS.h>` |
| `power.h` | `driver.h` | `intel_extreme_private.h` → `intel_extreme.h` → `lock.h` |
| `pll.h` | `intel_extreme.h` | `lock.h` → `<OS.h>` |
| `FlexibleDisplayInterface.h` | `intel_extreme.h` | `lock.h` → `<OS.h>` |
| `PanelFitter.h` | `intel_extreme.h` | `lock.h` → `<OS.h>` |
| `TigerLakePLL.h` | `intel_extreme.h` | `lock.h` → `<OS.h>` |
| `Pipes.h` | `intel_extreme.h`, `pll.h`, `FlexibleDisplayInterface.h`, `PanelFitter.h` | `lock.h` → `<OS.h>` |
| `Ports.h` | `intel_extreme.h`, `Pipes.h`, `pll.h` | все выше |
| `accelerant.h` | `intel_extreme.h`, `Ports.h`, `Pipes.h` | все выше |
| `commands.h` | `accelerant.h` | все выше |

### Корневые зависимости (листья графа)

Эти файлы не зависят ни от каких локальных заголовков драйвера:

- `lock.h` — зависит только от `<OS.h>`
- `accelerant_protos.h` — зависит только от `<Accelerant.h>`, `<video_overlay.h>`
- `device.h` — зависит только от `<Drivers.h>`

### Порядок включения (топологическая сортировка)

Для корректной компиляции заголовки должны включаться в следующем порядке:

1. `lock.h`
2. `intel_extreme.h`
3. `intel_extreme_private.h` (только kernel)
4. `driver.h` (только kernel)
5. `power.h` (только kernel)
6. `pll.h`
7. `FlexibleDisplayInterface.h`
8. `PanelFitter.h`
9. `TigerLakePLL.h`
10. `Pipes.h`
11. `Ports.h`
12. `accelerant.h`
13. `commands.h`
