# Список файлов для разделения рендеринга и композитинга в Haiku app_server

## Обоснование архитектурного решения

Разделение рендеринга и композитинга - это фундаментальное архитектурное изменение, которое позволит:
- Независимо оптимизировать процессы рисования и компоновки окон
- Упростить добавление аппаратного композитинга (GPU compositing)
- Улучшить производительность многооконных сценариев
- Подготовить базу для современных графических API

---

## 1. CORE APP_SERVER (корневая директория)

### 1.1 Основные классы сервера

**AppServer.cpp/.h**
- **Функционал**: Главный класс приложения, точка входа app_server
- **Связи**: Создает и управляет Desktop-объектами для каждого пользователя
- **Описание**: Наследуется от BServer, инициализирует GlobalFontManager, BitmapManager, ScreenManager, InputManager. Обрабатывает подключения клиентских приложений через порт `gAppServerPort`
- **API/ABI**: Публичный интерфейс через `AS_*` message codes из ServerProtocol.h
- **Ключевые методы**:
  - `_CreateDesktop()` - создание Desktop для пользователя
  - `_FindDesktop()` - поиск существующего Desktop
  - `MessageReceived()` - обработка системных сообщений

**Desktop.cpp/.h**
- **Функционал**: Управление рабочим столом, окнами, воркспейсами
- **Связи**: Содержит WindowList, EventDispatcher, ScreenManager, StackAndTile
- **Описание**: Центральный класс координации всех окон и экранов. Управляет фокусом, порядком отрисовки (Z-order), переключением workspace. Наследуется от MessageLooper для асинхронной обработки сообщений
- **Лок-стратегия**: 
  - `fWindowLock` (MultiLocker) - контроль модификации списка окон
  - `fScreenLock` (MultiLocker) - синхронизация доступа к HWInterface
  - `fDirectScreenLock` - специальная блокировка для DirectWindow
- **Ключевые структуры**:
  - `fAllWindows` - все окна во всех workspace
  - `fWorkspaces[kMaxWorkspaces]` - массив workspace (по умолчанию 32)
  - `fFocusList`, `fSubsetWindows` - специальные списки окон
  - `fStackAndTile` - система тайлинга окон

**DesktopSettings.cpp/.h**
- **Функционал**: Управление настройками Desktop (шрифты, цвета, декораторы)
- **Связи**: Используется Desktop, ServerApp, декораторами
- **Описание**: Хранит UI colors, system fonts, decorator settings. Поддерживает live update через `DesktopObservable` pattern
- **Server Read-Only Memory**: Настройки дублируются в shared memory для быстрого доступа клиентов

### 1.2 Window Management

**Window.cpp/.h**
- **Функционал**: Внутреннее представление окна в app_server
- **Связи**: Содержит View hierarchy, Decorator, DrawingEngine, связан с ServerWindow
- **Описание**: Управляет видимостью, геометрией, dirty regions, UpdateSession. Координирует отрисовку между клиентом и сервером
- **Критические регионы**:
  - `fVisibleRegion` - область, видимая на экране
  - `fDirtyRegion` - области, требующие перерисовки
  - `fExposeRegion` - новые области (заполняются фоном без ожидания клиента)
  - `fContentRegion` - полная область содержимого окна
- **UpdateSession**: Механизм синхронизации между клиентскими Draw() и серверной перерисовкой
  - `fCurrentUpdateSession` - активная сессия обновления
  - `fPendingUpdateSession` - следующая сессия

**WindowList.cpp/.h**
- **Функционал**: Управление списком окон с учетом Z-order и workspace
- **Связи**: Используется Desktop для упорядочивания окон
- **Описание**: Двусвязный список окон с быстрым доступом по workspace. Поддерживает итерацию в порядке отрисовки (back-to-front, front-to-back)

**Workspace.cpp/.h**
- **Функционал**: Представление виртуального рабочего стола
- **Связи**: Содержит упорядоченные списки окон для конкретного workspace
- **Описание**: Каждый workspace имеет собственный список окон и настройки цвета фона

**WorkspacesView.cpp/.h**
- **Функционал**: Визуализация миниатюр workspace (для апплета переключения)
- **Связи**: Регистрируется в Desktop как listener для изменений
- **Описание**: Отрисовывает уменьшенные копии окон для навигации между workspace

### 1.3 View System

**View.cpp/.h**
- **Функционал**: Серверное представление BView
- **Связи**: Иерархическая структура, связь с Window, содержит DrawState
- **Описание**: Управляет локальной системой координат, clipping, иерархией view. Каждая View имеет:
  - `fDrawState` - состояние рисования (цвета, шрифт, pen position)
  - `fLocalClipping` - локальное ограничение видимости
  - `fScreenClipping` - финальная область отрисовки в экранных координатах
- **Coordinate Transforms**:
  - `LocalToScreenTransform()` - трансформация локальных в экранные координаты
  - `PenToScreenTransform()` - для операций рисования с учетом pen location

**Canvas.cpp/.h**
- **Функционал**: Абстракция для рисования (базовый класс)
- **Связи**: Используется ServerWindow, Layer, OffscreenWindow
- **Описание**: Предоставляет унифицированный интерфейс для рисования независимо от destination (window, offscreen, layer). Управляет DrawState stack (Push/PopState)
- **Важные методы**:
  - `GetDrawingEngine()` - получение DrawingEngine для текущего контекста
  - `LocalToScreenTransform()` - система координат
  - `BlendLayer()` - композиция Layer с прозрачностью

### 1.4 Server-Client Communication

**ServerWindow.cpp/.h**
- **Функционал**: Обработка сообщений от клиентского BWindow
- **Связи**: Создает и управляет Window, общается с ServerApp
- **Описание**: Message dispatcher для BWindow. Декодирует протокол `AS_*` команд, преобразует в вызовы Window/View. Наследуется от MessageLooper
- **Протокол**:
  - Получает команды через `fMessagePort` (BPortLink)
  - Отправляет ответы через `fClientReplyPort`
  - Обрабатывает drawing commands, window manipulation, view hierarchy changes
- **View Management**:
  - `fCurrentView` - активная View для drawing commands
  - `fCurrentDrawingRegion` - кэш clipping для производительности
  - `_SetCurrentView()`, `_UpdateDrawState()` - переключение контекста

**ServerApp.cpp/.h**
- **Функционал**: Представление клиентского приложения в app_server
- **Связи**: Управляет ServerWindow, ServerBitmap, ServerPicture для приложения
- **Описание**: Создается при подключении BApplication. Управляет:
  - `fWindowList` - все окна приложения
  - `fBitmapMap` - ServerBitmap объекты
  - `fPictureMap` - ServerPicture объекты
  - `fAppFontManager` - per-application шрифты
- **Lifecycle**: Создается при `AS_CREATE_APP`, уничтожается при `AS_DELETE_APP`

**MessageLooper.cpp/.h**
- **Функционал**: Базовый класс для message-driven компонентов
- **Связи**: Базовый класс для ServerApp, ServerWindow, Desktop
- **Описание**: Предоставляет асинхронную обработку BMessage через отдельный thread

### 1.5 Offscreen Windows

**OffscreenWindow.cpp/.h**
- **Функционал**: Окна, рисующие в ServerBitmap вместо экрана
- **Связи**: Использует BitmapHWInterface, создается через BBitmap
- **Описание**: Специальный тип Window для offscreen rendering. Используется BBitmap для `B_BITMAP_ACCEPTS_VIEWS`. Имеет собственный DrawingEngine и BitmapHWInterface

**OffscreenServerWindow.cpp/.h**
- **Функционал**: ServerWindow counterpart для OffscreenWindow
- **Связи**: Работает с OffscreenWindow и ServerBitmap
- **Описание**: Обрабатывает сообщения от BBitmap->Window, создает OffscreenWindow с BitmapHWInterface

### 1.6 Drawing State Management

**DrawState.cpp/.h**
- **Функционал**: Инкапсуляция состояния рисования (pen, colors, font, transform)
- **Связи**: Используется View, Canvas, DrawingEngine
- **Описание**: Содержит все параметры, влияющие на отрисовку:
  - Pen (location, size)
  - Colors (high, low, view color)
  - Font (ServerFont)
  - Drawing mode (B_OP_COPY, B_OP_OVER, etc.)
  - Clipping regions (user + view clipping)
  - Transform (BAffineTransform, origin, scale)
- **Composite Patterns**: `CombinedTransform()`, `CombinedClippingRegion()`

**Layer.cpp/.h**
- **Функционал**: Offscreen layer для сложной композиции с opacity
- **Связи**: Наследуется от ServerPicture, используется Canvas
- **Описание**: Временный bitmap для отрисовки с последующим blend с заданной прозрачностью. Используется для эффекта opacity групп. Методы:
  - `RenderToBitmap()` - отрисовка содержимого в UtilityBitmap
  - Opacity parameter для alpha-blending

### 1.7 Region Management

**RegionPool.cpp/.h**
- **Функционал**: Пул переиспользуемых BRegion объектов
- **Связи**: Используется Window для оптимизации аллокаций
- **Описание**: Кэширует BRegion для избежания частых new/delete. Критично для производительности clipping calculations

### 1.8 Screen Management

**Screen.cpp/.h**
- **Функционал**: Представление физического монитора
- **Связи**: Содержит HWInterface, управляется ScreenManager
- **Описание**: Абстракция монитора с display_mode, HWInterface, workspace configuration. Управляет:
  - Display modes (resolution, refresh rate, color depth)
  - HWInterface lifetime
  - Screen configuration (position in virtual screen space)

**ScreenManager.cpp/.h**
- **Функционал**: Управление всеми подключенными мониторами
- **Связи**: Владеет Screen объектами, использует node monitor для hot-plug
- **Описание**: Сканирует `/dev/graphics` для accelerant drivers. Поддерживает:
  - Hot-plug мониторов (через B_WATCH_DIRECTORY)
  - AcquireScreens()/ReleaseScreens() для Desktop
  - Screen configuration events

**ScreenConfigurations.cpp/.h**
- **Функционал**: Сохранение и восстановление конфигураций мониторов
- **Связи**: Используется ScreenManager и Desktop
- **Описание**: Хранение пользовательских настроек display modes для различных конфигураций мониторов

**VirtualScreen.cpp/.h**
- **Функционал**: Объединение нескольких Screen в единое координатное пространство
- **Связи**: Используется Desktop для multi-monitor setup
- **Описание**: Создает виртуальный экран из нескольких физических, управляет координатами и переходами между мониторами

### 1.9 Bitmap Management

**BitmapManager.cpp/.h**
- **Функционал**: Глобальный менеджер всех ServerBitmap объектов
- **Связи**: Используется ServerApp для создания/удаления bitmap
- **Описание**: Управляет выделением памяти для bitmap (heap, shared memory, video memory). Singleton `gBitmapManager`. Методы:
  - `CreateBitmap()` - создание нового ServerBitmap
  - `CloneFromClient()` - клонирование из client area
  - Tracking overlay bitmaps

**ServerBitmap.cpp/.h**
- **Функционал**: Серверное представление BBitmap
- **Связи**: Может содержать Overlay, используется DrawingEngine
- **Описание**: Управляет pixel data в различных форматах памяти:
  - Regular heap memory
  - Shared memory (для BBitmap с B_BITMAP_ACCEPTS_VIEWS)
  - Video memory overlay buffers
- **Overlay Support**: Интеграция с hardware overlay через Overlay class

### 1.10 Cursor Management

**CursorManager.cpp/.h**
- **Функционал**: Управление системными и application cursors
- **Связи**: Используется Desktop, обновляет HWInterface
- **Описание**: Каталог курсоров (по умолчанию, I-beam, crosshair, etc.). Координирует hardware/software cursor rendering

**CursorSet.cpp/.h**
- **Функционал**: Набор курсоров для различных состояний
- **Связи**: Используется CursorManager
- **Описание**: Определяет стандартные системные курсоры (B_CURSOR_*)

**ServerCursor.cpp/.h**
- **Функционал**: Серверное представление BCursor
- **Связи**: Содержит bitmap данные курсора, hot spot
- **Описание**: Reference-counted cursor data. Поддерживает hardware и software cursors

### 1.11 Picture System

**ServerPicture.cpp/.h**
- **Функционал**: Серверное представление BPicture (vector recording)
- **Связи**: Используется ServerApp, может содержать вложенные pictures
- **Описание**: Записывает drawing commands для последующего воспроизведения. Использует BPicturePlayer callbacks. Важные методы:
  - `Play()` - воспроизведение на Canvas
  - `SyncState()` - синхронизация DrawState
  - `PushPicture()/PopPicture()` - вложенные pictures
- **Layer Support**: Базовый класс для Layer (offscreen rendering)

**PictureBoundingBoxPlayer.cpp/.h**
- **Функционал**: Вычисление bounding box recorded picture
- **Связи**: Используется Layer для определения размера render target
- **Описание**: Специальный picture player, который не рисует, а только вычисляет bounds

### 1.12 Event System

**EventDispatcher.cpp/.h**
- **Функционал**: Маршрутизация input events к целевым window/view
- **Связи**: Использует EventStream, обновляет EventTarget objects
- **Описание**: Центральный dispatcher всех input events. Управляет:
  - Mouse event routing (tracking, hover, drag)
  - Keyboard event delivery (focus management)
  - Event filtering (KeyboardFilter, MouseFilter)
  - Temporary listeners (для mouse tracking)
- **Важные концепции**:
  - `EventTarget` - destination для events (window + token list)
  - `fFocus` - текущее keyboard focus
  - `fPreviousMouseTarget` - для B_EXITED_VIEW events
  - Fake mouse moved для tooltip handling

**EventStream.cpp/.h**
- **Функционал**: Абстракция источника input events
- **Связи**: Предоставляется InputManager, используется EventDispatcher
- **Описание**: Интерфейс для получения events от input_server или других источников. Поддерживает cursor thread для smooth cursor updates

**InputManager.cpp/.h**
- **Функционал**: Управление EventStream объектами
- **Связи**: Создает/удаляет EventStream, global singleton `gInputManager`
- **Описание**: Пул EventStream для различных input sources. UpdateScreenBounds() для coordinate transformation

### 1.13 Direct Window Support

**DirectWindowInfo.cpp/.h**
- **Функционал**: Информация для BDirectWindow API
- **Связи**: Используется ServerWindow для games/video applications
- **Описание**: Управляет direct framebuffer access для приложений. Синхронизирует:
  - Buffer geometry changes
  - Clipping updates
  - Driver state changes
- **Safety**: Timeout-based semaphore control для защиты от misbehaving apps

### 1.14 Utility Classes

**IntPoint.cpp/.h, IntRect.cpp/.h**
- **Функционал**: Integer coordinate classes (оптимизированные версии BPoint/BRect)
- **Связи**: Используются везде для пиксельных координат
- **Описание**: Избегают float overhead для дискретных координат. Важны для clipping calculations

**Angle.cpp/.h**
- **Функционал**: Представление углов в различных единицах
- **Связи**: Используется для arc/ellipse drawing
- **Описание**: Конвертация между degrees, radians, и internal format

**RGBColor.cpp/.h**
- **Функционал**: Обертка rgb_color с дополнительными операциями
- **Связи**: Используется для color manipulations
- **Описание**: Blend operations, color space conversions

**SystemPalette.cpp/.h**
- **Функционал**: System color palette для indexed color modes (B_CMAP8)
- **Связи**: Используется для color matching и conversions
- **Описание**: 256-color palette, global singleton

**ClientMemoryAllocator.cpp/.h**
- **Функционал**: Shared memory allocator для client-server data
- **Связи**: Используется для ServerBitmap, overlay client data
- **Описание**: Управляет areas для shared memory между app_server и clients. Reduces copying для больших данных (bitmaps)

**MultiLocker.cpp/.h**
- **Функционал**: Read-write lock (множественные читатели, один писатель)
- **Связи**: Используется Desktop, Screen, HWInterface
- **Описание**: Оптимизированный RW lock для lock contention reduction. Критичен для concurrent window updates и screen access

**DelayedMessage.cpp/.h**
- **Функционал**: Отложенная доставка сообщений
- **Связи**: Используется Desktop для debouncing и batching
- **Описание**: Планирование message delivery с задержкой (для throttling)

**DesktopListener.cpp/.h**
- **Функционал**: Observer pattern для Desktop events
- **Связи**: Базовый класс для listeners (decorators, workspace view)
- **Описание**: Уведомления о изменениях screen, colors, workspace

**ProfileMessageSupport.cpp/.h**
- **Функционал**: Profiling и debugging support для message handling
- **Связи**: Опционально используется для performance analysis
- **Описание**: Статистика времени обработки различных `AS_*` messages

### 1.15 Build System

**Jamfile**
- **Зависимости библиотек**:
  ```
  libasdrawing.a    - Drawing subsystem
  libpainter.a      - Painter (AGG/Blend2D)
  libagg.a          - AGG library
  libblend2d.a      - Blend2D library
  libasmjit.a       - JIT для Blend2D
  libstackandtile.a - Window tiling
  liblinprog.a      - Linear programming для tiling
  ```
- **Optional dependencies**: FreeType (required), Fontconfig (optional)

**app_server.rdef**
- **Функционал**: Resource definition для app_server binary
- **Описание**: Application signature, version info, icons

---

## 2. DRAWING SUBSYSTEM (drawing/)

### 2.1 Core Drawing Engine

**DrawingEngine.cpp/.h**
- **Функционал**: Главный интерфейс между app_server и graphics backends
- **Связи**: Использует HWInterface, Painter, координируется с Canvas
- **Описание**: Предоставляет высокоуровневый API для рисования. Управляет:
  - Clipping region constraints
  - DrawState application (SetDrawState)
  - Locking для parallel access
  - Coordinate offsets для layers
- **Rendering Pipeline**: Commands → Painter → HWInterface → Hardware/Software
- **Важные методы**:
  - Primitives: `DrawLine()`, `FillRect()`, `DrawEllipse()`, etc.
  - `DrawString()` - text rendering
  - `DrawBitmap()` - bitmap blitting
  - `SetDrawState()` - apply pen, colors, transform
  - `ConstrainClippingRegion()` - set clipping

**BitmapDrawingEngine.cpp/.h**
- **Функционал**: Специализация DrawingEngine для offscreen rendering
- **Связи**: Используется OffscreenWindow для BBitmap rendering
- **Описание**: DrawingEngine, который рисует в ServerBitmap вместо экрана. Создается с BitmapHWInterface

### 2.2 Hardware Interface Base

**HWInterface.cpp/.h**
- **Функционал**: Абстрактный базовый класс для hardware abstraction
- **Связи**: Базовый для AccelerantHWInterface, RemoteHWInterface, BitmapHWInterface
- **Описание**: Предоставляет унифицированный интерфейс к framebuffer. Управляет:
  - Display modes
  - Cursor rendering (hardware/software)
  - Overlay support
  - Retrace synchronization
  - DPMS (power management)
  - Locking (ReadLock/WriteLock) для thread safety
- **MultiLocker pattern**: 
  - ReadLock - множественные читатели (drawing)
  - WriteLock - эксклюзивный доступ (mode change)
- **Key methods**:
  - `FrontBuffer()/BackBuffer()` - RenderingBuffer access
  - `Invalidate()` - mark region for update
  - `CopyBackToFront()` - double-buffer swap

**RenderingBuffer.h**
- **Функционал**: Абстракция pixel buffer (header-only)
- **Связи**: Используется HWInterface, DrawingEngine, Painter
- **Описание**: Тонкая обертка над raw pixel data. Предоставляет:
  - `Bits()` - указатель на pixel data
  - `BytesPerRow()` - stride
  - `Width()/Height()` - dimensions
  - `Bounds()` - BRect области

### 2.3 Buffer Management

**BitmapBuffer.cpp/.h**
- **Функционал**: Базовый класс для bitmap-backed buffers
- **Связи**: Базовый для BBitmapBuffer, MallocBuffer
- **Описание**: Общая логика для различных типов bitmap buffers

**BBitmapBuffer.cpp/.h**
- **Функционал**: Buffer backed by BBitmap
- **Связи**: Используется BitmapHWInterface для offscreen rendering
- **Описание**: Wraps BBitmap для использования в качестве render target

**MallocBuffer.cpp/.h**
- **Функционал**: Buffer с malloc-allocated memory
- **Связи**: Используется для temporary buffers
- **Описание**: Простой heap-allocated pixel buffer

### 2.4 Alpha Masking

**AlphaMask.cpp/.h**
- **Функционал**: Alpha channel маскирование для complex clipping
- **Связи**: Используется View для ClipToPicture, Layer
- **Описание**: Поддерживает несколько типов масок:
  - `UniformAlphaMask` - постоянная прозрачность
  - `PictureAlphaMask` - маска из BPicture rendering
  - `BitmapAlphaMask` - маска из bitmap
- **SetCanvasGeometry()** - позиционирование маски

**AlphaMaskCache.cpp/.h**
- **Функционал**: Кэширование alpha masks для performance
- **Связи**: Используется AlphaMask для избежания перевычислений
- **Описание**: Кэш растеризованных масок, важен для PictureAlphaMask

### 2.5 Pattern Handling

**PatternHandler.cpp/.h**
- **Функционал**: Обработка B_SOLID_*/pattern fills
- **Связи**: Используется Painter, PixelFormat
- **Описание**: Реализация pattern fills (B_SOLID_HIGH, B_SOLID_LOW, bitmap patterns). Mapping pattern coordinates к pixels

### 2.6 Overlay Support

**Overlay.cpp/.h**
- **Функционал**: Hardware overlay management (для video)
- **Связи**: Используется ServerBitmap с B_BITMAP_WILL_OVERLAY
- **Описание**: Управляет hardware overlay buffer. Методы:
  - `Configure()` - set source/destination rects
  - `Hide()` - hide overlay
  - `SetColorSpace()` - update pixel format
- **Synchronization**: Semaphore для client notification

### 2.7 Drawing Support

**drawing_support.cpp/.h**
- **Функционал**: Utility функции для рисования
- **Связи**: Используются Painter и DrawingEngine
- **Описание**: Математические helpers (line clipping, polygon bounding box, etc.)

### 2.8 Hardware Interfaces - Local

**AccelerantHWInterface.cpp/.h**
- **Функционал**: Интеграция с graphics card accelerant drivers
- **Связи**: Основной HWInterface для real hardware
- **Описание**: Загружает accelerant из `/dev/graphics`, инициализирует hardware. Управляет:
  - Display mode setting через accelerant hooks
  - Hardware cursor
  - Hardware overlay
  - DPMS states
  - Retrace semaphore
- **Accelerant Hooks**: Функции из driver (set_display_mode, move_cursor, etc.)
- **Double Buffering**: Поддержка hardware page flipping если available

**AccelerantBuffer.cpp/.h**
- **Функционал**: Buffer для video memory framebuffer
- **Связи**: Используется AccelerantHWInterface
- **Описание**: Обертка над video memory area. Direct memory mapping от video card

### 2.9 Hardware Interfaces - Remote

**RemoteHWInterface.cpp/.h**
- **Функционал**: Thin client graphics over network
- **Связи**: Альтернатива AccelerantHWInterface для remote displays
- **Описание**: Посылает drawing commands по сети вместо local rendering. Используется для remote app_server sessions

**RemoteDrawingEngine.cpp/.h**
- **Функционал**: DrawingEngine для remote rendering
- **Связи**: Работает с RemoteHWInterface
- **Описание**: Сериализует drawing commands для network transmission

**RemoteEventStream.cpp/.h**
- **Функционал**: EventStream для remote input
- **Связи**: Получает input events от remote client
- **Описание**: Десериализует input events от network

**RemoteMessage.cpp/.h**
- **Функционал**: Message protocol для remote graphics
- **Связи**: Используется Remote* классами для communication
- **Описание**: Encoding/decoding drawing commands и events

**NetReceiver.cpp/.h / NetSender.cpp/.h**
- **Функционал**: Network communication для remote rendering
- **Связи**: Используются RemoteHWInterface
- **Описание**: Socket-based bidirectional communication

**StreamingRingBuffer.cpp/.h**
- **Функционал**: Efficient buffer для streaming data
- **Связи**: Используется NetSender/NetReceiver
- **Описание**: Кольцевой буфер для низколатентной передачи

### 2.10 Hardware Interfaces - Virtual

**DWindowHWInterface.cpp/.h**
- **Функционал**: DirectWindow-compatible interface для testing
- **Связи**: Альтернатива AccelerantHWInterface в test mode
- **Описание**: Рисует в BDirectWindow для debugging app_server в user space

**DWindowBuffer.cpp/.h**
- **Функционал**: Buffer для DWindow framebuffer
- **Связи**: Используется DWindowHWInterface
- **Описание**: Wraps DWindow buffer area

**ViewHWInterface.cpp/.h**
- **Функционал**: Rendering в BView для testing
- **Связи**: Используется в test builds
- **Описание**: Простейший HWInterface, который рисует в BView. Для unit testing без реального hardware

### 2.11 Bitmap Hardware Interface

**BitmapHWInterface.cpp/.h**
- **Функционал**: HWInterface для offscreen ServerBitmap
- **Связи**: Используется OffscreenWindow
- **Описание**: HWInterface, который использует ServerBitmap как framebuffer. Для BBitmap с B_BITMAP_ACCEPTS_VIEWS

### 2.12 Build System

**drawing/Jamfile**
- **Собирает**: `libasdrawing.a`
- **Включает**: AlphaMask, DrawingEngine, HWInterface, buffer classes, Overlay

**drawing/interface/*/Jamfile**
- **Собирает**: Subdirectory libraries (local, remote, virtual interfaces)

---

## 3. PAINTER SUBSYSTEM (drawing/Painter/)

### 3.1 Core Painter (AGG Backend)

**Painter.cpp/.h**
- **Функционал**: Главный software renderer с AGG (Anti-Grain Geometry)
- **Связи**: Используется DrawingEngine, содержит PainterAggInterface
- **Описание**: Реализует все drawing primitives с anti-aliasing:
  - Vector graphics (lines, beziers, polygons, ellipses, arcs)
  - Text rendering через AGGTextRenderer
  - Bitmap operations через BitmapPainter
  - Gradient fills (linear, radial, diamond, conic)
  - Pattern fills через PatternHandler
- **AGG Integration**: `fInternal` (PainterAggInterface) содержит AGG pipeline
- **Drawing Modes**: Template-based system в drawing_modes/
- **Transform Support**: `fTransform` (Transformable) для affine transforms

**PainterAggInterface.h**
- **Функционал**: AGG rendering pipeline configuration
- **Связи**: Используется Painter internally
- **Описание**: Содержит все AGG rendering objects:
  - `fPixelFormat` - pixel accessor и blending
  - `fBaseRenderer` - базовый renderer
  - `fRasterizer` - vector to pixels conversion
  - `fRenderer` - anti-aliased scanline renderer
  - `fRendererBin` - fast non-AA renderer
  - `fSubpixRenderer` - subpixel text rendering
  - `fPath`, `fCurve` - vector path storage

### 3.2 Blend2D Backend (NEW)

**PainterBlend2D.cpp/.h**
- **Функционал**: Альтернативный renderer using Blend2D library
- **Связи**: Параллельная реализация Painter API
- **Описание**: Modern replacement для AGG. Преимущества:
  - JIT compilation через AsmJIT для performance
  - Native GPU acceleration support (future)
  - Better vector performance
  - Modern API design
- **Migration Status**: В процессе, оба backends поддерживаются
- **API Compatibility**: Тот же интерфейс что Painter для drop-in replacement

### 3.3 Transformations

**Transformable.cpp/.h**
- **Функционал**: Affine transformation management
- **Связи**: Используется Painter, DrawState
- **Описание**: Обертка BAffineTransform с дополнительной логикой:
  - Transform composition
  - Inverse transforms
  - Point/rect transformation
  - Transform stack (LocalTransform, CombinedTransform)

### 3.4 AGG Text Rendering

**AGGTextRenderer.cpp/.h**
- **Функционал**: Font rendering с AGG и FreeType
- **Связи**: Используется Painter, интегрируется с FontEngine
- **Описание**: Продвинутый text rendering:
  - FreeType glyph outline loading
  - Subpixel anti-aliasing (RGB, BGR layouts)
  - Hinting support
  - Kerning
  - Glyph caching через FontCache
- **Subpixel Rendering**: Специализированные AGG scanlines для LCD rendering

### 3.5 Blend2D Text Rendering (NEW - в разработке)

**TextRenderer.cpp/.h**
- **Функционал**: Text rendering для Blend2D backend
- **Связи**: Альтернатива AGGTextRenderer
- **Описание**: Использует Blend2D text API напрямую. Пока в разработке
- **Note**: BlendServerFont пока не реализован, используется стандартный ServerFont

### 3.6 Global Settings

**GlobalSubpixelSettings.cpp/.h**
- **Функционал**: Глобальные настройки subpixel rendering
- **Связи**: Используется text renderers
- **Описание**: Системные настройки LCD subpixel (RGB vs BGR order, averaging filter)

### 3.7 Drawing Modes - AGG

Все файлы в `drawing_modes/DrawingMode*.h`:

**DrawingMode.h**
- **Функционал**: Базовые макросы для drawing mode implementations
- **Связи**: Включается всеми mode implementations
- **Описание**: Template system для различных blend modes

**DrawingModeXXX.h** (где XXX = Add, AlphaCC, Blend, Copy, etc.)
- **Функционал**: Реализация конкретного BeOS drawing mode
- **Описание**: Каждый mode имеет template pixel operation функцию
- **Variants**:
  - Regular version - полный blend
  - `*Solid.h` - оптимизация для solid colors
  - `*SUBPIX.h` - subpixel text rendering variant

**Список основных modes**:
- **Copy**: Прямое копирование (B_OP_COPY)
- **Over**: Alpha blending (B_OP_OVER)
- **Blend**: Color blending (B_OP_BLEND)
- **Add/Subtract**: Arithmetic operations
- **Min/Max**: Comparison-based operations
- **AlphaXX**: Различные alpha composition modes
  - AlphaCC - composite over, source alpha
  - AlphaCO - composite over, view alpha
  - AlphaPC - premultiplied composite
  - AlphaPO - premultiplied over
- **Erase**: Transparency operations
- **Invert**: Color inversion
- **Select**: Conditional copy

### 3.8 Drawing Modes - Blend2D

Файлы `drawing_modes/Blend2DDrawingMode*.h`:

- **Функционал**: Те же drawing modes для Blend2D backend
- **Описание**: Параллельная реализация используя Blend2D API
- **Mapping**: Прямой mapping к Blend2D composition operators где возможно

### 3.9 Pixel Format

**PixelFormat.cpp/.h**
- **Функционал**: Pixel accessor для AGG rendering
- **Связи**: Используется PainterAggInterface
- **Описание**: Предоставляет функции для чтения/записи pixels с учетом drawing mode. Template-based dispatch к конкретным blend functions

**Blend2DPixelFormat.cpp/.h**
- **Функционал**: Pixel format abstraction для Blend2D
- **Описание**: Аналог PixelFormat но для Blend2D backend

### 3.10 Composition Adapters

**AggCompOpAdapter.h**
- **Функционал**: Adapter для AGG composition operators
- **Связи**: Используется drawing mode implementations
- **Описание**: Wraps AGG blend operators для использования в drawing mode system

**Blend2DCompOpAdapter.h**
- **Функционал**: Adapter для Blend2D composition operators
- **Описание**: То же для Blend2D backend

### 3.11 Bitmap Painter - AGG

**BitmapPainter.cpp/.h**
- **Функционал**: Bitmap drawing implementation для AGG
- **Связи**: Используется Painter для DrawBitmap operations
- **Описание**: Оптимизированные пути для различных сценариев:
  - Unscaled (no transformation)
  - Nearest neighbor (fast low-quality)
  - Bilinear filtering (smooth scaling)
  - Generic path (arbitrary transforms)
- **Color Space Conversion**: Автоматическая конвертация между форматами

**DrawBitmapBilinear.h**
- **Функционал**: Bilinear filtering для bitmap scaling
- **Описание**: High-quality image scaling using bilinear interpolation

**DrawBitmapGeneric.h**
- **Функционал**: Generic bitmap drawing с AGG pipeline
- **Описание**: Fallback для сложных transforms (rotation, perspective)

**DrawBitmapNearestNeighbor.h**
- **Функционал**: Fast low-quality bitmap scaling
- **Описание**: Pixel replication без filtering

**DrawBitmapNoScale.h**
- **Функционал**: Оптимизация для unscaled bitmaps
- **Описание**: Fast path для 1:1 pixel copy

### 3.12 Bitmap Painter - Blend2D

**BitmapPainterBlend2D.cpp/.h**
- **Функционал**: Bitmap operations для Blend2D backend
- **Описание**: Использует native Blend2D image rendering

### 3.13 Architecture Specific

**bitmap_painter/painter_bilinear_scale.nasm** (x86)
- **Функционал**: SIMD-optimized bilinear scaling
- **Описание**: Hand-written assembly для maximum performance на x86/x64

### 3.14 AGG Custom Extensions

**agg_clipped_alpha_mask.h**
- **Функционал**: Alpha mask с region clipping
- **Связи**: Используется PictureAlphaMask
- **Описание**: Custom AGG component для ClipToPicture support

**agg_renderer_region.h**
- **Функционал**: Region-based clipping для AGG renderer
- **Описание**: Эффективное ограничение rendering к BRegion

**agg_scanline_packed_subpix.h / agg_scanline_unpacked_subpix.h**
- **Функционал**: Scanline storage для subpixel rendering
- **Описание**: Хранит RGB компоненты раздельно для LCD text

**agg_rasterizer_scanline_aa_subpix.h**
- **Функционал**: Rasterizer для subpixel AA
- **Описание**: Генерирует subpixel coverage для text glyphs

**agg_renderer_scanline_subpix.h**
- **Функционал**: Scanline renderer для subpixel text
- **Описание**: Рисует glyphs с RGB subpixel anti-aliasing

### 3.15 Build System

**Painter/Jamfile**
- **Собирает**: `libpainter.a`
- **Зависимости**: AGG, Blend2D, AsmJIT libraries
- **Включает**: Painter, text renderers, bitmap painters, drawing modes

---

## 4. DECORATOR SYSTEM (decorator/)

### 4.1 Decorator Management

**DecorManager.cpp/.h**
- **Функционал**: Менеджер window decorators (add-on система)
- **Связи**: Глобальный singleton `gDecorManager`, используется Window
- **Описание**: Загружает и управляет decorator add-ons:
  - `AllocateDecorator()` - создание decorator для Window
  - `AllocateWindowBehaviour()` - создание WindowBehaviour
  - `SetDecorator()` - смена текущего decorator
  - Default decorator всегда доступен (built-in)
- **Add-on Loading**: Сканирует decorator directories, загружает image_id
- **Settings**: Сохранение/восстановление decorator preferences

**Decorator.cpp/.h**
- **Функционал**: Абстрактный базовый класс для window decorations
- **Связи**: Базовый для DefaultDecorator, SATDecorator
- **Описание**: Интерфейс для рисования и hit-testing window chrome:
  - Tab (title bar)
  - Borders
  - Buttons (close, zoom, minimize)
  - Resize corners
- **Multi-tab Support**: Поддержка stacked windows (несколько tabs)
- **Regions**: `REGION_TAB`, `REGION_CLOSE_BUTTON`, etc. для hit-testing
- **Drawing**: Использует DrawingEngine для rendering

### 4.2 Default Decorator

**DefaultDecorator.cpp/.h**
- **Функционал**: Стандартный Haiku window decorator (желтые табы)
- **Связи**: Наследуется от TabDecorator
- **Описание**: Реализует classic Haiku look:
  - Gradient title bars
  - Rounded corners
  - Standard button icons
  - Border drawing
- **Customization**: Цвета через DesktopSettings

**TabDecorator.cpp/.h**
- **Функционал**: Базовый класс для tab-based decorators
- **Связи**: Наследуется от Decorator, базовый для DefaultDecorator
- **Описание**: Общая логика для decorators с tabs:
  - Tab management (add/remove/move)
  - Tab layout computation
  - Tab focus handling
- **Stacking Support**: Координация нескольких tabs для window stacks

### 4.3 Window Behaviour

**WindowBehaviour.cpp/.h**
- **Функционал**: Абстрактный базовый класс для window interaction
- **Связи**: Базовый для DefaultWindowBehaviour
- **Описание**: Определяет window manipulation logic:
  - Mouse tracking (move, resize, tab drag)
  - Keyboard shortcuts
  - Snapping behaviour
  - Decorator interaction

**DefaultWindowBehaviour.cpp/.h**
- **Функционал**: Стандартное поведение окон Haiku
- **Связи**: Используется по умолчанию, может быть заменен add-ons
- **Описание**: Реализует:
  - Window dragging (title bar, borders)
  - Resizing (borders, corners)
  - Button clicks (close, zoom, minimize)
  - Tab management в stacks
  - Double-click на tab для collapse
- **State Machines**: Отдельные states для различных tracking operations

### 4.4 Additional Decorator Features

**MagneticBorder.cpp/.h**
- **Функционал**: Magnetic window snapping
- **Связи**: Используется DefaultWindowBehaviour
- **Описание**: Логика для window snapping к:
  - Screen edges
  - Other windows
  - Tab groups
- **AlterDeltaForSnap()**: Модификация move delta для snapping effect

---

## 5. FONT SYSTEM (font/)

### 5.1 Font Management

**FontManager.cpp/.h**
- **Функционал**: Базовый класс для font managers
- **Связи**: Базовый для GlobalFontManager, AppFontManager
- **Описание**: Управляет FontFamily и FontStyle objects. Методы:
  - `CountFamilies()/FamilyAt()` - enumerate fonts
  - `GetStyle()` - find specific style
  - Font list revision tracking

**GlobalFontManager.cpp/.h**
- **Функционал**: Система-wide font manager
- **Связи**: Singleton `gFontManager`, используется всеми приложениями
- **Описание**: Сканирует system font directories:
  - `/boot/system/data/fonts`
  - `/boot/home/config/non-packaged/data/fonts`
  - User fonts
- **Font Scanning**: Использует FontEngine для parsing font files
- **Notifications**: Уведомляет о изменениях font list

**AppFontManager.cpp/.h**
- **Функционал**: Per-application font manager
- **Связи**: Каждый ServerApp имеет собственный instance
- **Описание**: Управляет application-specific fonts:
  - Fonts, добавленные через BFont API
  - Fonts из application resources
  - Overlay над GlobalFontManager

### 5.2 Font Engine

**FontEngine.cpp/.h**
- **Функционал**: FreeType integration layer
- **Связи**: Используется FontManager для parsing, FontCache для rendering
- **Описание**: Обертка FreeType library:
  - Font file loading (TrueType, OpenType, Type1)
  - Glyph outline extraction
  - Font metrics (ascent, descent, line gap)
  - Kerning tables
  - Unicode character mapping
- **Singleton**: Один FT_Library instance для всего app_server

### 5.3 Font Cache

**FontCache.cpp/.h**
- **Функционал**: Глобальный кэш rendered glyph bitmaps
- **Связи**: Используется AGGTextRenderer для избежания re-rendering
- **Описание**: LRU cache растеризованных glyphs:
  - Ключ: font + size + glyph code + subpixel position
  - Значение: bitmap + metrics
- **Memory Management**: Автоматическое удаление LRU entries при переполнении

**FontCacheEntry.cpp/.h**
- **Функционал**: Одна entry в FontCache
- **Описание**: Содержит rendered glyph bitmap и positioning info

### 5.4 Font Structure

**FontFamily.cpp/.h**
- **Функционал**: Представление font family (e.g., "DejaVu Sans")
- **Связи**: Содержит список FontStyle objects
- **Описание**: Группирует все styles одного family (Regular, Bold, Italic)

**FontStyle.cpp/.h**
- **Функционал**: Конкретный font style (e.g., "Bold Italic")
- **Связи**: Ссылается на font file, используется ServerFont
- **Описание**: Хранит:
  - Font file path
  - Face index (для TTC collections)
  - Style name
  - Font flags (fixed width, has kerning, etc.)

### 5.5 Server Font

**ServerFont.cpp/.h**
- **Функционал**: Серверное представление BFont
- **Связи**: Используется DrawState, text rendering code
- **Описание**: Font instance с specific parameters:
  - FontStyle reference
  - Size
  - Shear (italic angle)
  - Rotation
  - Spacing mode
  - Rendering flags (anti-alias, hinting)
- **Metrics**: Computed height, ascent, descent
- **Glyph Operations**: `StringWidth()`, `GetEscapements()`, `GetBoundingBoxes()`

---

## 6. STACK AND TILE (stackandtile/)

### 6.1 Stack and Tile Core

**StackAndTile.cpp/.h**
- **Функционал**: Главный контроллер window stacking/tiling
- **Связи**: Один instance per Desktop, управляет SATWindow/SATGroup
- **Описание**: Координирует:
  - Window grouping
  - Automatic tiling
  - Stack management
  - Snapping candidates
- **Integration**: Подключается к window move/resize operations

**Stacking.cpp/.h**
- **Функционал**: Window stacking logic (tabs)
- **Связи**: Компонент StackAndTile
- **Описание**: Управляет stacking windows в single tab group:
  - Stack creation
  - Adding/removing from stack
  - Tab order management

**Tiling.cpp/.h**
- **Функционал**: Window tiling logic (grid layout)
- **Связи**: Компонент StackAndTile
- **Описание**: Automatic window layout:
  - Grid-based positioning
  - Constraint solving (linear programming через liblinprog)
  - Resize propagation

### 6.2 SAT Components

**SATGroup.cpp/.h**
- **Функционал**: Группа windows в tiling configuration
- **Связи**: Содержит SATWindow objects, WindowArea
- **Описание**: Логическая группировка tiled windows:
  - Window layout computation
  - Size constraint solving
  - Group-wide operations (move, resize all)
- **Reference Counting**: Reference-counted для shared ownership

**SATWindow.cpp/.h**
- **Функционал**: SAT wrapper вокруг Window
- **Связи**: Один per Window participating в SAT
- **Описание**: Дополнительная SAT metadata для окна:
  - Original size limits (для restore)
  - Group membership
  - WindowArea reference
  - Snapping behavior
- **Event Handling**: Обработка SAT-specific messages

**SATDecorator.cpp/.h**
- **Функционал**: Decorator с SAT visual feedback
- **Связи**: Наследуется от DefaultDecorator
- **Описание**: Визуальные индикаторы SAT:
  - Highlighted borders для snapping
  - Special tab colors для groups
  - Drag handles
- **HIGHLIGHT_STACK_AND_TILE**: Custom highlight mode

### 6.3 Build System

**stackandtile/Jamfile**
- **Собирает**: `libstackandtile.a`
- **Зависимости**: `liblinprog.a` (linear programming solver)

---

## ИТОГО

**Общее количество файлов для модификации: ~270 файлов**

Распределение по категориям:
- Core app_server: ~60 файлов
- Drawing subsystem: ~35 файлов  
- Painter subsystem: ~115 файлов (включая drawing modes)
- Decorator system: ~10 файлов
- Font system: ~12 файлов
- Stack and Tile: ~8 файлов
- Build system: ~10 Jamfile
- Headers и utilities: ~20 файлов

---

## КРИТИЧЕСКИ ВАЖНЫЕ ФАЙЛЫ

Для начала работы над разделением рендеринга и композитинга следует сосредоточиться на следующих ключевых файлах:

### Приоритет 1 (архитектурные изменения):

1. **Layer.cpp/.h** ⭐ **КРИТИЧЕСКИ ВАЖНО**
   - **Почему**: УЖЕ РЕАЛИЗУЕТ концепцию offscreen rendering + composition!
   - **Что уже есть**:
     - `RenderToBitmap()` - рендеринг содержимого в offscreen UtilityBitmap
     - `Canvas::BlendLayer()` - композиция с opacity
     - `fLeftTopOffset` - offset management для positioning
     - Использование `LayerCanvas` для isolated rendering context
   - **Изменения**: Расширить Layer концепцию на все Window objects:
     - Window layers с caching (не перерисовывать если content не изменился)
     - Layer pooling/reuse для performance
     - Hardware texture upload hooks для GPU compositor
   - **Это прототип compositor!** Layer показывает точную архитектуру: render → offscreen buffer → composite with effects

2. **Desktop.cpp/.h**
   - **Почему**: Координирует все окна и композитинг
   - **Связь с Layer**: Desktop будет управлять window layers и compositor
   - **Изменения**: 
     - Добавить CompositorEngine/CompositorThread
     - Window rendering → layers (как Layer делает для opacity groups)
     - Compositor собирает layers и выводит на screen

3. **Window.cpp/.h**
   - **Почему**: Управляет рендерингом окна и dirty regions
   - **Связь с Layer**: Window станет "большим Layer" - own offscreen buffer
   - **Изменения**: 
     - `RenderToLayer()` метод (по аналогии с Layer::RenderToBitmap)
     - Content buffer caching
     - Integration с compositor для final blend

4. **Canvas.cpp/.h**
   - **Почему**: Canvas::BlendLayer() УЖЕ показывает compositor pattern
   - **Что уже работает**: Offscreen rendering → opacity blend → screen
   - **Изменения**: Обобщить BlendLayer logic на window composition

5. **DrawingEngine.cpp/.h**
   - **Почему**: Основной интерфейс рисования
   - **Связь с Layer**: DrawingEngine.SetRendererOffset() используется Layer
   - **Изменения**: Убедиться поддерживает offscreen targets efficiently

6. **HWInterface.cpp/.h**
   - **Почему**: Абстракция hardware, где происходит финальный вывод
   - **Изменения**: Compositor-specific методы (blend multiple layers)

7. **Painter.cpp/.h / PainterBlend2D.cpp/.h**
   - **Почему**: Основной рендерер, будет рисовать в offscreen
   - **Связь с Layer**: УЖЕ работает с offscreen через LayerCanvas
   - **Изменения**: Оптимизации для layer rendering, если нужны

### Приоритет 2 (интеграция):

6. **View.cpp/.h**
   - **Изменения**: Рендеринг в layer buffer вместо direct screen drawing

7. **PictureBoundingBoxPlayer.cpp/.h**
   - **Почему**: Используется Layer для вычисления bounds offscreen content
   - **Изменения**: Может понадобиться для Window layer sizing

8. **AccelerantHWInterface.cpp/.h**
   - **Изменения**: Поддержка hardware composition если accelerant provides hooks

9. **BitmapDrawingEngine.cpp/.h** / **BitmapHWInterface.cpp/.h**
   - **Почему**: Уже реализуют offscreen rendering pattern
   - **Изменения**: Могут использоваться для window content buffers (как Layer использует UtilityBitmap)

10. **ServerWindow.cpp/.h**
    - **Изменения**: Координация между client drawing и compositor updates

### Приоритет 3 (оптимизация):

11. Все файлы в `drawing/interface/` - для различных HW backends
12. Bitmap painter файлы - для efficient texture operations
13. Drawing modes - убедиться работают с compositor buffers
14. Buffer management классы - для layer allocation/reuse

---

## LAYER КАК ПРОТОТИП COMPOSITOR

### Почему Layer.cpp/.h критически важен

**Layer УЖЕ реализует полный compositor pattern:**

```cpp
// Layer::RenderToBitmap() - OFFSCREEN RENDERING
UtilityBitmap* Layer::RenderToBitmap(Canvas* canvas)
{
    // 1. Определяет bounding box содержимого
    BRect boundingBox = _DetermineBoundingBox(canvas);
    
    // 2. Создает offscreen bitmap
    BReference<UtilityBitmap> layerBitmap(_AllocateBitmap(boundingBox), true);
    
    // 3. Создает изолированный rendering context
    BitmapHWInterface layerInterface(layerBitmap);
    DrawingEngine* layerEngine = layerInterface.CreateDrawingEngine();
    
    // 4. Устанавливает offset для coordinate mapping
    layerEngine->SetRendererOffset(boundingBox.left, boundingBox.top);
    
    // 5. Рисует содержимое в offscreen buffer
    LayerCanvas layerCanvas(layerEngine, canvas->DetachDrawState(), boundingBox);
    Play(&layerCanvas);  // Воспроизводит recorded drawing commands
    
    return layerBitmap.Detach();
}

// Canvas::BlendLayer() - COMPOSITION
void Canvas::BlendLayer(Layer* layerPtr)
{
    // 1. Рендерит содержимое в offscreen bitmap
    BReference<UtilityBitmap> layerBitmap(layer->RenderToBitmap(this), true);
    
    // 2. Вычисляет позицию на экране
    BRect destination = layerBitmap->Bounds();
    destination.OffsetBy(layer->LeftTopOffset());
    LocalToScreenTransform().Apply(&destination);
    
    // 3. Применяет opacity через alpha mask
    BReference<AlphaMask> mask(new UniformAlphaMask(layer->Opacity()), true);
    SetAlphaMask(mask);
    
    // 4. Композитит на screen с B_OP_ALPHA
    GetDrawingEngine()->DrawBitmap(layerBitmap, layerBitmap->Bounds(),
        destination, 0);
}
```

### Что Layer демонстрирует:

1. ✅ **Offscreen rendering** - контент рисуется в изолированный bitmap
2. ✅ **Coordinate mapping** - `SetRendererOffset()` для правильного позиционирования
3. ✅ **Isolated context** - `LayerCanvas` с собственным DrawingEngine
4. ✅ **Effects** - opacity через AlphaMask
5. ✅ **Composition** - финальный blend на destination
6. ✅ **Memory management** - UtilityBitmap с proper cleanup

### Как расширить на Window Compositor:

```cpp
// Предлагаемая архитектура (базируется на Layer pattern):

// 1. Window получает persistent layer buffer
class Window {
    BReference<UtilityBitmap> fContentLayer;  // Кэшируется между frames
    bool fContentDirty;
    
    void RenderContent() {
        if (!fContentDirty && fContentLayer.IsSet())
            return;  // Переиспользуем cached content
            
        // Точно как Layer::RenderToBitmap()
        fContentLayer = _RenderToOffscreenBitmap();
        fContentDirty = false;
    }
};

// 2. Desktop compositor собирает layers
class Desktop {
    void ComposeFrame() {
        for (Window* window : fVisibleWindows) {
            window->RenderContent();  // Offscreen rendering
            
            // Точно как Canvas::BlendLayer()
            fCompositor->BlendWindowLayer(
                window->fContentLayer,
                window->Frame(),
                window->Opacity(),
                window->Transform()
            );
        }
        
        fHWInterface->Invalidate(fDirtyRegion);
    }
};
```

### Ключевые insights из Layer:

1. **BitmapHWInterface** - идеальный для offscreen rendering
   - Уже используется Layer и OffscreenWindow
   - Поддерживает полный DrawingEngine API
   - Efficient memory-based rendering

2. **DrawingEngine::SetRendererOffset()** - критичен для coordinate mapping
   - AGG renderer offset для правильного positioning
   - Применяется ПОСЛЕ всех transforms
   - Layer уже использует для boundingBox.LeftTop()

3. **UtilityBitmap** - хороший выбор для layer buffers
   - Reference counted
   - Поддерживает различные color spaces
   - Уже интегрирован с DrawingEngine

4. **AlphaMask integration** - mechanism для effects
   - UniformAlphaMask для простой opacity
   - Можно расширить для shadows, blur
   - Уже работает с композицией

### Layer limitations (что нужно улучшить для Window compositor):

1. **No caching** - Layer рендерит каждый раз
   - Window compositor должен кэшировать unchanged content
   - Dirty tracking для partial updates

2. **Memory allocation** - новый bitmap на каждый RenderToBitmap()
   - Нужен buffer pool для переиспользования
   - Avoid allocation на каждом frame

3. **Synchronous only** - Layer рендерит в caller thread
   - Window compositor может рендерить async
   - Parallel window rendering

4. **No hardware acceleration** - pure software
   - Будущий GPU compositor нужно учесть
   - Texture upload path

### Почему начать с Layer:

1. **Проверенный код** - Layer работает в production
2. **Правильная архитектура** - offscreen → compose pattern
3. **Minimal changes** - расширить существующее вместо rewrite
4. **Incremental migration** - Window постепенно становится "big Layer"
5. **BeOS compatibility** - Layer не ломает API/ABI

### Конкретные шаги:

**Фаза 1**: Window inherits Layer pattern
```cpp
// Window.cpp - добавить методы как у Layer
class Window {
    UtilityBitmap* RenderContentToLayer();  // Как Layer::RenderToBitmap()
    void InvalidateContent() { fContentDirty = true; }
};

// Desktop.cpp - compositor loop
void Desktop::_ComposeScreen() {
    for (Window* w : _VisibleWindows(fCurrentWorkspace)) {
        if (w->ContentDirty()) {
            w->RenderContentToLayer();  // Offscreen render
        }
        _BlendWindowLayer(w);  // Как Canvas::BlendLayer()
    }
}
```

**Фаза 2**: Оптимизация
- Buffer pooling
- Dirty region tracking  
- Async rendering threads

**Фаза 3**: Effects
- Per-window shadows, blur
- Smooth animations
- Workspace transitions

---

## АРХИТЕКТУРНЫЕ СВЯЗИ

### Основной поток рендеринга (текущий):

```
Client BWindow::Draw()
       ↓
ServerWindow (message handling)
       ↓
Window::ProcessDirtyRegion()
       ↓
View::Draw() → DrawingEngine → Painter → HWInterface → Screen

// Layer (для opacity groups):
PushState() + BeginLayer(opacity)
       ↓
Drawing commands recorded в ServerPicture
       ↓
PopState() → Layer::RenderToBitmap()
       ↓
LayerCanvas + BitmapHWInterface + DrawingEngine → UtilityBitmap
       ↓
Canvas::BlendLayer() → DrawingEngine::DrawBitmap() → Screen
```

### Предлагаемый поток с разделением (расширение Layer pattern):

```
Client BWindow::Draw()
       ↓
ServerWindow (message handling)
       ↓
Window::RenderContentToLayer() ← КАК Layer::RenderToBitmap()
       ↓
       ├→ BitmapHWInterface + DrawingEngine
       ├→ View::Draw() → Painter
       └→ UtilityBitmap (window content layer) [CACHED!]
       ↓
Desktop::Compositor::ComposeFrame()
       ↓
       └→ ForEach visible window:
              └→ BlendWindowLayer() ← КАК Canvas::BlendLayer()
                     ├→ Transform (position, scale, rotation)
                     ├→ Effects (opacity, shadows, blur)
                     └→ DrawingEngine::DrawBitmap()
       ↓
HWInterface → Screen

// Ключевое отличие:
// - Window content layer КЭШИРУЕТСЯ (не перерисовывается если не dirty)
// - Compositor может рендерить windows в parallel
// - Effects применяются at composition time, не rendering time
```

---

## КРИТИЧЕСКИЕ ЗАМЕЧАНИЯ

### API/ABI Совместимость

**НЕЛЬЗЯ ЛОМАТЬ**:
- Протокол `AS_*` messages между client и server
- Публичные интерфейсы BWindow, BView, BBitmap
- Accelerant driver API
- Decorator add-on interface (пока)

**МОЖНО МЕНЯТЬ**:
- Внутреннюю реализацию Desktop, Window, DrawingEngine
- Private классы (ServerWindow, View internals)
- HWInterface implementations (если сохраняется базовый API)

### Haiku-Specific Подходы

1. **MultiLocker Pattern**: Использовать везде для concurrent access
   - Desktop window management: MultiLocker для read/write access
   - Screen updates: ReadLock для drawing, WriteLock для mode changes

2. **Reference Counting**: BReference<> для shared ownership
   - Screen, ServerBitmap, ServerFont используют reference counting
   - Compositor layers должны использовать тот же подход

3. **Message-Based Architecture**: Асинхронная координация
   - Не блокировать Desktop thread на рендеринге
   - Использовать BMessage для notification between compositor и windows

4. **Region-Based Clipping**: BRegion везде
   - Не использовать rect arrays, всегда BRegion
   - Критично для performance с сложными окнами

5. **DrawState Stack**: Иерархическое состояние
   - Push/PopState semantics из BeOS API
   - Сохранять через композитор

### Performance Considerations

1. **Dirty Region Tracking**: Минимизировать recomposite
   - Только пересоставлять области, где окна изменились
   - Использовать existing Window::fDirtyRegion logic

2. **Layer Caching**: Переиспользовать buffers
   - Pool переиспользуемых offscreen buffers
   - Избегать re-allocation на каждом frame

3. **Async Rendering**: Параллельный рендеринг окон
   - Windows могут рисовать в свои buffers параллельно
   - Compositor собирает результаты

4. **Hardware Acceleration**: Подготовка к GPU
   - Структура должна поддерживать future hardware compositor
   - Vulkan backend возможен без major redesign

### Migration Strategy

1. **Фаза 1**: Offscreen window rendering (без visual changes)
   - Windows рисуют в offscreen buffers
   - Simple compositor копирует на экран
   - Same visual result, но infrastructure готова

2. **Фаза 2**: Compositor effects
   - Per-window opacity
   - Shadows, blur effects
   - Smooth animations

3. **Фаза 3**: Hardware acceleration
   - GPU compositor backend
   - Texture upload optimization
   - Shader-based effects

---


## ДОКУМЕНТАЦИЯ К ОБНОВЛЕНИЮ

При внесении изменений обязательно обновить:

1. **Architecture docs** в Haiku repository
2. **Inline comments** в изменяемых файлах
3. **CLAUDE.md** files в соответствующих директориях
4. **Build docs** если меняется Jamfile структура
5. **API docs** если публичные интерфейсы затронуты

---

## ЗАКЛЮЧЕНИЕ

Этот документ описывает полную структуру app_server с акцентом на файлы, критичные для разделения рендеринга и композитинга. Важно:

1. **Layer.cpp/.h - ключевая отправная точка** - УЖЕ реализует offscreen rendering + composition pattern. Это не теория, а работающий код который можно расширить на Window compositor.

2. **Сохранять API/ABI совместимость** - BeOS compatibility и client apps не должны меняться. Layer показывает как это делать - BeginLayer()/EndLayer() API остался unchanged.

3. **Следовать Haiku patterns** - MultiLocker, BReference, BMessage, BRegion. Layer использует все эти patterns корректно.

4. **Тестировать incremental** - Layer работает в production, значит его pattern proven. Поэтапная миграция с regression testing против Layer behavior.

5. **Документировать изменения** - Layer хорошо документирован, продолжить этот уровень.

6. **Профилировать** - performance критичен. Layer показывает overhead offscreen rendering, Window compositor должен добавить caching для улучшения.

Архитектурное разделение (расширение Layer pattern) позволит:
- GPU acceleration в будущем (Layer buffers → Vulakn)
- Per-window effects (shadows, blur, opacity) - Layer уже показывает opacity
- Smooth animations and transitions - compositor может interpolate между frames
- Better multi-monitor support - independent composition per screen
- Легче поддерживать и тестировать - Layer isolated, Window compositor isolated

**Следующие шаги**: 
1. Изучить Layer.cpp детально - это reference implementation
2. Window::RenderContentToLayer() по образцу Layer::RenderToBitmap()
3. Desktop::ComposeFrame() по образцу Canvas::BlendLayer()
4. Добавить caching (Layer не имеет) для performance
5. Тестировать против Layer behavior как baseline
