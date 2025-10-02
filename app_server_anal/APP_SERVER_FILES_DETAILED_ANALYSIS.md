# ДЕТАЛИЗИРОВАННЫЙ АНАЛИЗ ФАЙЛОВ APP SERVER HAIKU OS

## КОРНЕВЫЕ ФАЙЛЫ APP_SERVER (ГРУППА 1) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/AppServer.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `AppServer`  
**Назначение:** Главный класс сервера приложений, наследуется от BServer (или TestServerLoopAdapter в тестовом режиме). Управляет множественными рабочими столами для разных пользователей, координирует запуск input_server, обрабатывает сообщения между приложениями и графической подсистемой.  
**Ключевые функции:**
- `MessageReceived()` - обработка сообщений
- `_CreateDesktop()` - создание рабочего стола для пользователя
- `_FindDesktop()` - поиск существующего рабочего стола
- `_LaunchInputServer()` - запуск сервера ввода
**Глобальные переменные:** `gBitmapManager`, `gAppServerPort`
**Связи:** Desktop, BitmapManager, MessageLooper, ServerConfig

### /home/ruslan/haiku/src/servers/app/AppServer.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `AppServer`  
**Назначение:** Реализация главного сервера приложений. Инициализирует GlobalFontManager, ScreenManager, InputManager, BitmapManager, создает токенное пространство, обрабатывает SIMD флаги процессора, управляет жизненным циклом рабочих столов.  
**Ключевые алгоритмы:**
- Инициализация глобальных менеджеров ресурсов
- Создание и управление портами коммуникации
- Обработка многопользовательских сессий
**Глобальные переменные:** `gAppServerPort`, `gTokenSpace`, `gAppServerSIMDFlags`
**Связи:** Desktop, BitmapManager, GlobalFontManager, InputManager, ScreenManager

### /home/ruslan/haiku/src/servers/app/Desktop.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Desktop`, `WindowStack`  
**Назначение:** Центральный контроллер рабочего стола. Управляет окнами, рабочими пространствами, событиями ввода, курсорами, декораторами, настройками рабочего стола. Реализует многоуровневую блокировку для безопасного доступа к окнам.  
**Ключевые интерфейсы:**
- `DesktopObservable` - паттерн наблюдатель
- `MessageLooper` - обработка сообщений
- `ScreenOwner` - владение экранами
**Ключевые функции:**
- `LockSingleWindow()/UnlockSingleWindow()` - блокировка одного окна
- `LockAllWindows()/UnlockAllWindows()` - блокировка всех окон
- `BroadcastToAllApps()/BroadcastToAllWindows()` - рассылка сообщений
- `KeyEvent()` - обработка клавиатурных событий
**Связи:** EventDispatcher, ScreenManager, WindowList, Workspace, StackAndTile

### /home/ruslan/haiku/src/servers/app/ServerApp.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `ServerApp`  
**Назначение:** Серверное представление клиентского приложения. Управляет коммуникацией с BApplication через порты, отслеживает окна приложения, управляет курсорами, шрифтами, изображениями, памятью. Каждое BApplication имеет соответствующий ServerApp.  
**Ключевые функции:**
- `IsActive()/Activate()` - управление активностью приложения
- `SendMessageToClient()` - отправка сообщений клиенту
- `SetCurrentCursor()/CurrentCursor()` - управление курсором
- `AddWindow()/RemoveWindow()` - управление окнами
- `InWorkspace()` - проверка принадлежности рабочему пространству
**Ключевые структуры данных:**
- TokenSpace для управления объектами
- ClientMemoryAllocator для управления памятью
- AppFontManager для шрифтов
**Связи:** Desktop, ServerWindow, ServerCursor, ServerBitmap, MessageLooper

### /home/ruslan/haiku/src/servers/app/Window.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Window`, `WindowStack`  
**Назначение:** Серверное представление BWindow. WindowStack управляет группой окон с общим декоратором (для stack&tile). Window управляет областями отрисовки, обрезкой, z-порядком, взаимодействием с декораторами.  
**Ключевые структуры:**
- `StackWindows` - список окон в стеке
- WindowStack с LayerOrder для управления слоями
**Ключевые функции WindowStack:**
- `TopLayerWindow()` - получение верхнего окна
- `AddWindow()/RemoveWindow()` - управление окнами в стеке
- `MoveToTopLayer()` - перемещение окна наверх
- `SetDecorator()/Decorator()` - управление декоратором
**Связи:** ServerWindow, Decorator, View, RegionPool, WindowList

### /home/ruslan/haiku/src/servers/app/View.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `View`  
**Назначение:** Серверное представление BView. Наследуется от Canvas, управляет отрисовкой, координатами, обрезкой, иерархией view. Обрабатывает трансформации координат, прокрутку, привязку к окнам, флаги и режимы изменения размера.  
**Ключевые функции:**
- `Frame()/Bounds()` - управление геометрией
- `Token()` - уникальный идентификатор
- `ConvertToVisibleInTopView()` - преобразование координат с обрезкой
- `AttachedToWindow()/DetachedFromWindow()` - жизненный цикл
- `ScrollingOffset()` - управление прокруткой
**Ключевые структуры:** IntRect, IntPoint для целочисленных координат  
**Связи:** Canvas, Window, ServerBitmap, DrawingEngine, Overlay

### /home/ruslan/haiku/src/servers/app/Canvas.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Canvas`  
**Назначение:** Базовый класс для отрисовки. Управляет стеком состояний отрисовки (DrawState), трансформациями, обрезкой, происхождением координат, масштабированием. Предоставляет унифицированный интерфейс для рисования геометрических примитивов.  
**Ключевые функции:**
- `PushState()/PopState()` - стек состояний отрисовки
- `SetDrawingOrigin()/DrawingOrigin()` - управление началом координат
- `SetScale()/Scale()` - масштабирование
- `SetUserClipping()` - пользовательская обрезка
- `ClipToRect()/ClipToShape()` - геометрическая обрезка
**Ключевые структуры:** DrawState, SimpleTransform, AlphaMask  
**Связи:** DrawState, DrawingEngine, ServerPicture, BGradient

### /home/ruslan/haiku/src/servers/app/ScreenManager.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `ScreenManager`, `ScreenOwner`  
**Назначение:** Управляет множественными экранами системы. ScreenOwner - интерфейс для объектов, владеющих экранами. ScreenManager координирует захват/освобождение экранов, обрабатывает уведомления об изменениях конфигурации мониторов.  
**Ключевые функции:**
- `AcquireScreens()/ReleaseScreens()` - захват/освобождение экранов
- `ScreenAt()/CountScreens()` - доступ к экранам
- `ScreenChanged()` - обработка изменений конфигурации
**Интерфейс ScreenOwner:**
- `ScreenRemoved()/ScreenAdded()/ScreenChanged()` - уведомления
- `ReleaseScreen()` - принудительное освобождение
**Связи:** Screen, HWInterface, DrawingEngine, BLooper

### /home/ruslan/haiku/src/servers/app/BitmapManager.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `BitmapManager`  
**Назначение:** Централизованное управление растровыми изображениями в app_server. Создает ServerBitmap объекты, управляет памятью через ClientMemoryAllocator, поддерживает оверлеи, клонирование изображений из областей памяти клиентов.  
**Ключевые функции:**
- `CreateBitmap()` - создание изображения с параметрами
- `CloneFromClient()` - клонирование из клиентской памяти
- `BitmapRemoved()` - уведомление об удалении
- `SuspendOverlays()/ResumeOverlays()` - управление оверлеями
**Ключевые структуры:** BList для хранения изображений и оверлеев  
**Связи:** ServerBitmap, ClientMemoryAllocator, HWInterface

### /home/ruslan/haiku/src/servers/app/EventDispatcher.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `EventDispatcher`, `EventTarget`, `EventFilter`  
**Назначение:** Центральная система диспетчеризации событий ввода. EventTarget представляет получателя событий с токенами слушателей. EventFilter позволяет фильтровать события перед доставкой. EventDispatcher координирует маршрутизацию событий между компонентами.  
**Ключевые функции EventTarget:**
- `AddListener()/RemoveListener()` - управление слушателями событий
- `FindListener()` - поиск слушателя по токену
- `RemoveTemporaryListeners()` - очистка временных слушателей
**EventFilter:** `Filter()` - фильтрация событий с изменением целей  
**Связи:** Desktop, EventStream, HWInterface, BMessenger

---

## ГРУППА УПРАВЛЕНИЯ ОКНАМИ (ГРУППА 2) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/Window.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `Window`, `WindowStack`, `UpdateSession`  
**Назначение:** Основная реализация серверного окна. Управляет геометрией окон, регионами отрисовки, обрезкой, стеком окон для Stack&Tile функциональности, сессиями обновления для синхронизации с клиентом, декораторами, рабочими пространствами.

**Ключевые функции:**
- `MoveBy()/ResizeBy()` - изменение размера и позиции с поддержкой стеков
- `SetClipping()/GetEffectiveDrawingRegion()` - управление областями отрисовки
- `BeginUpdate()/EndUpdate()` - синхронизация обновлений с клиентом
- `MouseDown()/MouseUp()/MouseMoved()` - обработка событий мыши с делегированием в WindowBehaviour
- `AddWindowToStack()/DetachFromWindowStack()` - управление Stack&Tile группировкой
- `ProcessDirtyRegion()/RedrawDirtyRegion()` - обработка грязных регионов

**Архитектурные особенности:** Window использует двойную буферизацию сессий обновления (fCurrentUpdateSession/fPendingUpdateSession), сложную систему блокировок для многопоточного доступа, региональный пул для эффективного управления памятью. WindowStack обеспечивает группировку окон с общими декораторами и синхронизированными операциями изменения размера.

**Связи:** ServerWindow, Decorator, WindowBehaviour, View, Desktop, DrawingEngine, RegionPool

### /home/ruslan/haiku/src/servers/app/View.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `View`  
**Назначение:** Серверная реализация BView. Наследуется от Canvas, управляет иерархией представлений, координатным пространством, обрезкой, отрисовкой, событиями мыши, курсорами, изображениями фона, флагами видимости.

**Ключевые функции:**
- `AttachedToWindow()/DetachedFromWindow()` - жизненный цикл привязки к окну
- `ConvertToVisibleInTopView()` - преобразование координат с обрезкой
- `ScrollBy()` - прокрутка содержимого с инвалидацией
- `MoveBy()/ResizeBy()` - изменение геометрии с поддержкой режимов изменения размера
- `MouseDown()/MouseUp()/MouseMoved()` - обработка событий мыши в иерархии
- `Draw()` - отрисовка представления и фона
- `FindViews()` - поиск дочерних представлений по критериям

**Архитектурные особенности:** View поддерживает сложную иерархию дочерних представлений с эффективным управлением связанными списками, прокрутку с оптимизированным копированием содержимого, систему режимов изменения размера (B_FOLLOW_*), интеграцию с токеновым пространством для идентификации.

**Связи:** Window, Canvas, DrawingEngine, ServerBitmap, ServerCursor, AlphaMask, Overlay

### /home/ruslan/haiku/src/servers/app/WindowList.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `WindowList`, `window_anchor`  
**Назначение:** Управляет связанными списками окон для рабочих пространств и z-порядка. window_anchor содержит указатели next/previous для создания двусвязных списков и позицию для восстановления положения окон.

**Ключевые функции:**
- `AddWindow()` - добавление окна в список с опциональной вставкой перед указанным окном
- `RemoveWindow()` - удаление окна из списка с корректировкой связей
- `HasWindow()/ValidateWindow()` - проверка принадлежности окна списку
- `Count()` - подсчет окон в списке

**Архитектурные особенности:** WindowList использует индексы для различных типов списков (рабочие пространства, z-порядок), автоматически управляет битовыми масками рабочих пространств при добавлении/удалении окон, поддерживает быструю валидацию указателей для безопасности.

**Связи:** Window, DesktopSettings, workspace management

### /home/ruslan/haiku/src/servers/app/OffscreenWindow.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `OffscreenWindow`  
**Назначение:** Специализированная реализация Window для отрисовки в ServerBitmap вместо экрана. Используется для BBitmap с B_BITMAP_ACCEPTS_VIEWS флагом, обеспечивая полную функциональность отрисовки в память.

**Ключевые функции:**
- Конструктор принимает ServerBitmap и создает BitmapHWInterface
- Деструктор корректно завершает работу HWInterface
- Наследует всю функциональность Window для отрисовки

**Архитектурные особенности:** OffscreenWindow использует BitmapHWInterface вместо экранного интерфейса, автоматически устанавливает регионы видимости равными границам изображения, обеспечивает изоляцию от основной системы окон при сохранении совместимости API.

**Связи:** Window, ServerBitmap, BitmapHWInterface, DrawingEngine

### /home/ruslan/haiku/src/servers/app/OffscreenServerWindow.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `OffscreenServerWindow`  
**Назначение:** Серверная оболочка для offscreen-окон. Переопределяет коммуникацию с клиентом, поскольку клиентский поток BWindow не запущен для offscreen-изображений.

**Ключевые функции:**
- `SendMessageToClient()` - заглушка, не отправляет сообщения клиенту
- `MakeWindow()` - создает OffscreenWindow вместо обычного Window

**Архитектурные особенности:** OffscreenServerWindow обеспечивает совместимость API при отсутствии клиентского потока, автоматически управляет жизненным циклом ServerBitmap, изолирует offscreen-функциональность от основной системы сообщений.

**Связи:** ServerWindow, OffscreenWindow, ServerBitmap

---

## ГРУППА ЯДРА ПОДСИСТЕМЫ ОТРИСОВКИ (ГРУППА 3) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/drawing/DrawingEngine.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `DrawingEngine`, `HWInterfaceListener`, `DrawTransaction`  
**Назначение:** Абстрактный интерфейс между высокоуровневыми командами отрисовки и низкоуровневым hardware interface. Управляет Painter объектом для программной растеризации, обеспечивает блокировки для многопоточного доступа, координирует копирование в передний буфер, управляет курсорами, оверлеями, клиппингом.

**Ключевые функции:**
- `LockParallelAccess()/LockExclusiveAccess()` - система блокировок для безопасного многопоточного доступа
- `SetDrawState()/SetHighColor()/SetLowColor()/SetFont()` - установка состояния отрисовки
- `ConstrainClippingRegion()` - управление областями обрезки
- `CopyRegion()` - оптимизированное копирование регионов
- `DrawBitmap()/DrawArc()/DrawBezier()/DrawEllipse()` - примитивы отрисовки геометрических фигур
- `StrokeRect()/FillRect()/FillRegion()` - базовые операции заливки
- `DrawString()/StringWidth()` - текстовые операции
- `SetCopyToFrontEnabled()/CopyToFront()` - управление двойной буферизацией

**Архитектурные особенности:** DrawingEngine использует паттерн Strategy с подключаемыми HWInterface реализациями, автоматическое управление оверлеями через AutoFloatingOverlaysHider, DrawTransaction для атомарных операций отрисовки с автоматическим восстановлением оверлеев, систему уведомлений HWInterfaceListener для изменений буфера кадра, интеграцию с Painter для программной растеризации сложных операций.

**Связи:** HWInterface, Painter, ServerFont, ServerBitmap, DrawState, Overlay, ServerCursor

### /home/ruslan/haiku/src/servers/app/drawing/DrawingEngine.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `DrawingEngine`, `DrawTransaction`, `AutoFloatingOverlaysHider`  
**Назначение:** Основная реализация движка отрисовки. Координирует между Painter (программная растеризация) и HWInterface (аппаратное ускорение), обеспечивает блокировки, управляет транзакциями отрисовки, обрабатывает курсоры и оверлеи, реализует все примитивы отрисовки.

**Ключевые алгоритмы:**
- Система двойных блокировок (parallel/exclusive) для оптимального многопоточного доступа
- DrawTransaction обеспечивает атомарность операций отрисовки с автоматическим управлением dirty regions
- AutoFloatingOverlaysHider временно скрывает курсоры и оверлеи во время отрисовки
- Оптимизированное копирование регионов с поддержкой hardware acceleration
- Интеграция текстового движка с поддержкой escapement deltas и positioning arrays

**Технические детали:** DrawingEngine делегирует сложные операции растеризации в Painter, использует утилиты make_rect_valid() и extend_by_stroke_width() для нормализации геометрии, поддерживает градиенты, паттерны, альфа-смешивание, трансформации координат, subpixel positioning для текста.

**Связи:** Painter, HWInterface, DrawState, ServerFont, ServerBitmap, BRegion

### /home/ruslan/haiku/src/servers/app/drawing/HWInterface.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `HWInterface`, `HWInterfaceListener`  
**Назначение:** Абстрактный базовый класс для аппаратных интерфейсов отображения. Определяет унифицированный API для взаимодействия с видеокартами, управления режимами экрана, курсорами, оверлеями, DPMS, яркостью, буферизацией кадров. Наследуется от MultiLocker для безопасного многопоточного доступа.

**Ключевые интерфейсы:**
- **Управление режимами:** `SetMode()/GetMode()/GetModeList()/ProposeMode()/GetPreferredMode()`
- **Информация об устройстве:** `GetDeviceInfo()/GetFrameBufferConfig()/GetPixelClockLimits()/GetTimingConstraints()`
- **Синхронизация:** `RetraceSemaphore()/WaitForRetrace()`
- **Энергосбережение:** `SetDPMSMode()/DPMSMode()/DPMSCapabilities()/SetBrightness()/GetBrightness()`
- **Курсоры:** `SetCursor()/SetCursorVisible()/MoveCursorTo()/ObscureCursor()/SetDragBitmap()`
- **Оверлеи:** `AcquireOverlayChannel()/ConfigureOverlay()/HideOverlay()/GetOverlayRestrictions()`
- **Буферизация:** `FrontBuffer()/BackBuffer()/IsDoubleBuffered()/CopyBackToFront()/InvalidateRegion()`

**Архитектурные особенности:** HWInterface использует паттерн Observer для уведомления слушателей об изменениях экрана, автоматическое управление backup областями для курсора через buffer_clip, поддержку hardware cursor vs software cursor, интеграцию с accelerant driver API, систему floating overlays с автоматическим скрытием/показом.

**Связи:** DrawingEngine, EventStream, ServerCursor, ServerBitmap, Overlay, RenderingBuffer

### /home/ruslan/haiku/src/servers/app/drawing/drawing_support.h
**Тип:** Заголовочный файл C++ (утилиты)  
**Ключевые функции:** `gfxset32()`, `blend_line32()`, `align_rect_to_pixels()`  
**Назначение:** Набор высокооптимизированных низкоуровневых утилит для графических операций. Предоставляет быстрые функции заливки памяти, альфа-смешивания, выравнивания геометрии для пиксельной точности.

**Технические детали:**
- `gfxset32()` - использует 64-битные операции для быстрой заливки 32-битными цветами, автоматически оптимизирует для различных размеров блоков
- `blend_line32()` - реализует альфа-смешивание для строк пикселей с предварительным умножением альфы для оптимизации
- `pixel32` union - обеспечивает эффективный доступ к компонентам цвета как к 32-битному значению или отдельным байтам
- `align_rect_to_pixels()` - корректирует прямоугольники для точного пиксельного выравнивания

**Оптимизации:** Функции используют SIMD-подобные оптимизации с 64-битными операциями, временные буферы для избежания артефактов смешивания, inline реализации для устранения overhead вызовов функций.

**Связи:** BRect, графические примитивы, pixel manipulation utilities

### /home/ruslan/haiku/src/servers/app/drawing/PatternHandler.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Pattern`, `PatternHandler`  
**Назначение:** Эффективная реализация BeOS/Haiku паттернов для заливки. Pattern предоставляет типобезопасную обертку для 64-битных паттернов, PatternHandler реализует логику применения паттернов с поддержкой смещений, цветов переднего/заднего плана, быстрого определения типа паттерна.

**Ключевые функции:**
- **Pattern:** конструкторы для различных источников данных, операторы сравнения и присваивания, методы доступа к данным
- **PatternHandler:** `SetPattern()/SetColors()/ColorAt()/IsHighColor()` - управление паттернами и цветами
- `IsSolidHigh()/IsSolidLow()/IsSolid()` - быстрые проверки для оптимизации сплошных заливок
- `SetOffsets()` - поддержка смещения паттерна для сложных операций отрисовки

**Архитектурные особенности:** Pattern использует union для эффективного доступа к данным как к 64-битному значению или массиву байтов, PatternHandler реализует inline функции для максимальной производительности в критических циклах отрисовки, предопределенные константы kSolidHigh/kSolidLow/kMixedColors для стандартных паттернов, битовые операции для быстрого определения цвета пикселя в паттерне.

**Связи:** DisplayDriver implementations, графические примитивы, color management

### /home/ruslan/haiku/src/servers/app/drawing/PatternHandler.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `PatternHandler`  
**Назначение:** Реализация обработчика паттернов с полным набором конструкторов, методов управления цветами, функций определения цвета пикселя. Предоставляет эффективные алгоритмы для работы с 8x8 битовыми паттернами BeOS/Haiku.

**Ключевые алгоритмы:**
- Битовые операции для определения значения пикселя в паттерне: `ptr[y & 7] & (1 << (7 - (x & 7)))`
- Модульная арифметика для циклического повторения паттерна
- Оптимизированные inline функции для критических операций
- Поддержка смещений паттерна для сложных операций заливки

**Предопределенные константы:** kSolidHigh (все биты 1), kSolidLow (все биты 0), kMixedColors (шахматный паттерн 0xAAAAAAAAAAAAAAAALL), стандартные цвета kBlack/kWhite.

**Связи:** Pattern class, BPoint, rgb_color, graphics primitives

---

## ГРУППА PAINTER ПОДСИСТЕМЫ (ГРУППА 4) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/drawing/Painter/Painter.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Painter`, `BitmapPainter`  
**Назначение:** Основной класс программной растеризации на основе Anti-Grain Geometry (AGG). Обеспечивает высококачественную отрисовку геометрических примитивов, текста, изображений с полной поддержкой антиалиасинга, subpixel precision, трансформаций, градиентов, альфа-смешивания, всех drawing modes BeOS/Haiku.

**Ключевые интерфейсы:**
- **Геометрические примитивы:** `StrokeLine()/StrokeRect()/FillRect()/DrawEllipse()/DrawArc()/DrawBezier()/DrawPolygon()/DrawShape()`
- **Градиентная заливка:** `FillRect()/FillEllipse()/FillArc()` с BGradient параметром
- **Текстовые операции:** `DrawString()/BoundingBox()/StringWidth()` с escapement deltas и positioning arrays
- **Оптимизации:** `StraightLine()` для быстрой отрисовки пиксельно выровненных линий
- **Специальные:** `FillRectVerticalGradient()/FillRectNoClipping()` для критических операций
- **Трансформации:** `TransformAndClipRect()/AlignAndClipRect()` inline функции для оптимальной обработки координат

**AGG интеграция и архитектура:** Painter содержит PainterAggInterface для инкапсуляции AGG pipeline (растерайзеры, сканлайны, рендереры), AGGTextRenderer для сложной текстовой обработки, Transformable для математических трансформаций, PatternHandler для паттернов, шаблонные методы _StrokePath()/_FillPath()/_RasterizePath() для генерической обработки vertex sources, SIMD оптимизации (флаги APPSERVER_SIMD_MMX/SSE), сложную систему градиентных трансформаций.

**Связи:** AGGTextRenderer, Transformable, PatternHandler, PainterAggInterface, RenderingBuffer, ServerFont, BGradient, BRegion

### /home/ruslan/haiku/src/servers/app/drawing/Painter/Painter.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `Painter`, `BitmapPainter`  
**Назначение:** Полная реализация программного растерайзера на основе AGG. Содержит сложную логику SIMD детекции, алгоритмы градиентных трансформаций, оптимизированные пути для критических операций, шаблонные реализации для AGG vertex sources, сложные алгоритмы bitmap scaling и filtering.

**Ключевые алгоритмы и оптимизации:**
- **SIMD детекция:** `detect_simd()` сканирует все CPU в системе, поддерживает Intel/AMD/VIA/Rise/Cyrix/Transmeta, выбирает минимальный набор инструкций
- **Градиентные трансформации:** `_CalcLinearGradientTransform()/_CalcRadialGradientTransform()` с математическими вычислениями agg::trans_affine
- **Template методы:** `_StrokePath<>()/_FillPath<>()/_RasterizePath<>()` для генерической обработки AGG vertex sources
- **Оптимизированные пути:** `StraightLine()` для пиксель-perfect линий, `FillRectNoClipping()` без оверхеда
- **AGG pipeline:** Макросы доступа к fRasterizer/fRenderer/fScanline/fSubpixRasterizer/fSubpixRenderer
- **Координатные трансформации:** `_Align()` с subpixel precision поддержкой, center offset коррекция

**Технические детали:** Использует множество AGG компонентов: agg::path_storage, agg::conv_curve, agg::conv_stroke, agg::ellipse, agg::rounded_rect, agg::bezier_arc, различные pixfmt и renderers, span interpolators для image filtering, поддержку shape iteration, сложную логику bounding box вычислений.

**Связи:** AGGTextRenderer, PatternHandler, BitmapPainter, DrawState, GlobalSubpixelSettings, AGG components

---

## ГРУППА ДЕКОРАТОРОВ И STACK&TILE (ГРУППА 5) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/decorator/Decorator.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `Decorator`, `Decorator::Tab`  
**Назначение:** Абстрактный базовый класс для всех декораторов окон. Определяет единый интерфейс для отрисовки рамок, заголовков, кнопок управления окнами. Реализует паттерн Template Method для структурированной отрисовки и паттерн Strategy для различных типов декораторов.

**Архитектурные паттерны:**
- **Template Method:** `Draw()` определяет алгоритм отрисовки, делегируя конкретные шаги виртуальным методам `_DrawFrame()`, `_DrawTab()`, `_DrawTitle()`
- **Strategy:** Различные декораторы (Default, SAT) реализуют единый интерфейс с различным поведением
- **Composite:** `Tab` содержит все элементы вкладки как единый объект
- **Observer:** `Region` enum и `RegionAt()` для hit-testing и event handling

**Ключевые структуры:** 
- `Tab` - инкапсулирует всю информацию о вкладке (title, buttons, state, bitmaps)
- `Region` enum - определяет интерактивные области декоратора
- `ComponentColors[7]` - цветовая схема для элементов интерфейса

**Связи:** Desktop, DesktopSettings, DrawingEngine, ServerBitmap, ServerFont, BRegion

### /home/ruslan/haiku/src/servers/app/decorator/Decorator.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `Decorator`, `Decorator::Tab`  
**Назначение:** Базовая реализация системы декораторов с управлением вкладками, геометрией, фокусом, событиями. Обеспечивает thread-safe доступ через MultiLocker, кеширование footprint для оптимизации, систему highlight для визуальной обратной связи.

**Ключевые алгоритмы:**
- **Multi-tab management:** `AddTab()`, `RemoveTab()`, `MoveTab()` с поддержкой update regions
- **Geometry management:** `MoveBy()`, `ResizeBy()` с синхронизацией всех внутренних прямоугольников
- **Event handling:** `RegionAt()` для hit-testing с возвратом tab index
- **Footprint caching:** `GetFootprint()` с lazy evaluation и invalidation
- **Settings persistence:** `SetSettings()`/`GetSettings()` для сохранения позиций вкладок

**Thread Safety:** Все публичные методы защищены `AutoReadLocker`/`AutoWriteLocker`, обеспечивая безопасный concurrent доступ из разных потоков app_server.

**Связи:** DecorManager, DrawingEngine, Window, ServerBitmap, BRegion

### /home/ruslan/haiku/src/servers/app/decorator/DecorManager.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `DecorManager`, `DecorAddOn`  
**Назначение:** Центральная система управления декораторами. DecorAddOn - абстракция для загружаемых декораторов, DecorManager - синглтон для управления жизненным циклом декораторов, их загрузки, переключения, preview режима.

**Архитектурные паттерны:**
- **Singleton:** `gDecorManager` - глобальный экземпляр менеджера
- **Factory Method:** `DecorAddOn::AllocateDecorator()` создает конкретные декораторы
- **Plugin Architecture:** Поддержка загружаемых декораторов через image_id
- **Strategy:** DecorAddOn инкапсулирует создание декораторов и поведений окон
- **Observer:** `DesktopListenerList` для уведомлений об изменениях декораторов

**Ключевые возможности:**
- **Plugin loading:** `_LoadDecor()` с проверкой символов и инициализацией
- **Preview mode:** `PreviewDecorator()` для временного показа декоратора
- **Settings persistence:** автоматическое сохранение/загрузка настроек декоратора
- **Fallback mechanism:** DefaultDecorator как резервный вариант

**Связи:** Desktop, Window, DrawingEngine, DesktopSettings, DecorAddOn

### /home/ruslan/haiku/src/servers/app/decorator/DecorManager.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `DecorManager`, `DecorAddOn`  
**Назначение:** Полная реализация системы управления декораторами с загрузкой add-on, управлением жизненным циклом, settings persistence, preview functionality, error handling.

**Ключевые алгоритмы:**
- **Add-on loading:** `_LoadDecor()` с проверкой `instantiate_decor_addon` символа
- **Preview management:** переключение между current/preview декораторами с automatic cleanup
- **Settings persistence:** `/boot/home/config/settings/system/app_server/decorator_settings`
- **Error handling:** graceful fallback к default декоратору при ошибках загрузки
- **Memory management:** правильная очистка image_id при смене декораторов

**Plugin Architecture:** Поддерживает полный жизненный цикл add-on: загрузка → инициализация → использование → cleanup → выгрузка, с проверкой версий и совместимости.

**Связи:** BEntry, BPath, BFile, BMessage, BDirectory, image management

---

## EVENT SYSTEM & INPUT MANAGEMENT (ГРУППА 7) - ЗАВЕРШЕНО

### /home/ruslan/haiku/src/servers/app/EventDispatcher.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `EventDispatcher`, `EventTarget`, `EventFilter`  
**Назначение:** Центральная система диспетчеризации событий ввода в Desktop окружении. EventTarget представляет получателя событий с токенами слушателей для различных view. EventFilter предоставляет hook для фильтрации/модификации событий перед доставкой. EventDispatcher координирует весь pipeline событий между input_server и client applications.

**Архитектурные паттерны:**
- **Observer Pattern:** EventTarget содержит список event_listener с токенами для уведомления multiple views
- **Chain of Responsibility:** EventFilter позволяет цепочку фильтров для preprocessing событий
- **Mediator Pattern:** EventDispatcher центрально управляет всей коммуникацией событий
- **Strategy Pattern:** Различные filters (mouse/keyboard) реализуют EventFilter interface

**Ключевые функции EventTarget:**
- `AddListener()/RemoveListener()` - управление постоянными слушателями событий
- `AddTemporaryListener()/RemoveTemporaryListener()` - временные слушатели для mouse tracking
- `FindListener()` - поиск слушателя по token для efficient event delivery
- `RemoveTemporaryListeners()` - cleanup при завершении mouse operations

**Сложная система listener management:** event_listener структура содержит отдельные маски для permanent и temporary events, что позволяет fine-grained control над event delivery и automatic cleanup при button release.

**Связи:** Desktop, EventStream, HWInterface, BMessenger, view tokens

### /home/ruslan/haiku/src/servers/app/EventDispatcher.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `EventDispatcher`, `event_listener`  
**Назначение:** Полная реализация event dispatching system с sophisticated threading model, mouse tracking, keyboard focus management, temporary listeners, message routing, drag&drop support.

**Threading Model и Architecture:**
- **Dual-thread approach:** Основной event loop thread (B_REAL_TIME_DISPLAY_PRIORITY - 10) и опциональный cursor thread (priority - 5)
- **Real-time priorities:** Критически важные приоритеты для responsive user interaction
- **Thread coordination:** Безопасная коммуникация через BAutolock и proper cleanup в destructor
- **Cursor thread optimization:** Separate thread для hardware cursor updates когда поддерживается hardware

**Сложные алгоритмы Event Processing:**
- **Mouse transition handling:** `_SendFakeMouseMoved()` для proper B_EXITED_VIEW/B_ENTERED_VIEW transit
- **Drag&drop coordination:** Complete drag message lifecycle с bitmap management и drop point calculation
- **Focus management:** Keyboard focus tracking с suspend mechanism для mouse operations
- **Temporary listeners:** Automatic cleanup при button release для preventing memory leaks
- **Message importance system:** Различные importance levels (kMouseMovedImportance, kStandardImportance) для priority delivery

**Message Routing Architecture:** Сложная система token addition/removal для efficient multi-view delivery, support для B_NO_POINTER_HISTORY option, view_token добавление для focus targeting.

**Performance Optimizations:**
- **Latest mouse moved filtering:** `fNextLatestMouseMoved` для dropping outdated mouse events при server lag
- **Rate limiting:** 100ms threshold для old mouse moved events
- **Efficient token management:** `_AddTokens()/_RemoveTokens()` для bulk operations

**Связи:** Desktop, EventStream, EventFilter, ServerBitmap, InputManager, токеновая система

### /home/ruslan/haiku/src/servers/app/EventStream.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `EventStream`, `InputServerStream`  
**Назначение:** Абстракция для получения событий от input_server. EventStream - базовый interface, InputServerStream - конкретная реализация для IPC коммуникации с input_server через ports и shared memory.

**Архитектурные паттерны:**
- **Abstract Factory:** EventStream предоставляет unified interface для различных источников событий
- **Strategy Pattern:** InputServerStream vs потенциальные network/test implementations
- **Template Method:** Базовые virtual methods с default implementations для optional functionality
- **Producer-Consumer:** EventStream производит события для EventDispatcher consumption

**Key Abstractions:**
- `GetNextEvent()` - core method для event retrieval с blocking behavior
- `SupportsCursorThread()` - capability detection для hardware cursor optimization
- `GetNextCursorPosition()` - dedicated cursor tracking для separate thread
- `InsertEvent()` - injection mechanism для synthetic events (fake mouse moves)
- `PeekLatestMouseMoved()` - optimization для mouse moved event filtering

**InputServerStream Specifics:**
- **Shared memory cursor tracking:** `shared_cursor` structure для high-performance cursor updates
- **Port-based messaging:** Reliable IPC mechanism с input_server
- **Cursor semaphore coordination:** Synchronization для cursor thread operations
- **Screen bounds updates:** Notification mechanism для input_server о resolution changes

**Связи:** EventDispatcher, InputManager, shared_cursor, BMessageQueue, port communication

### /home/ruslan/haiku/src/servers/app/EventStream.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `InputServerStream`  
**Назначение:** Полная реализация input_server communication с sophisticated shared memory management, port communication, cursor tracking, message queueing, error handling.

**IPC Architecture Details:**
- **Port acquisition:** `IS_ACQUIRE_INPUT` protocol для establishing communication channel с input_server
- **Shared cursor area:** `create_area()` с B_CLONEABLE_AREA для cross-team cursor sharing
- **Cursor semaphore:** Real-time synchronization mechanism для cursor position updates
- **Message queue management:** Internal BMessageQueue для buffering events before delivery

**Performance Critical Implementation:**
- **Atomic cursor operations:** `atomic_get()` for lock-free cursor position reading на Haiku target
- **Cursor acknowledgment:** `atomic_and(&fCursorBuffer->read, 0)` signaling для input_server
- **Port queue optimization:** `port_count()` bulk reading для emptying port queue efficiently
- **Latest mouse moved tracking:** `fLatestMouseMoved` pointer для immediate access к most recent mouse event

**Robust Error Handling:**
- **Port validation:** `get_port_info()` для checking port validity before operations
- **Graceful degradation:** Proper cleanup при input_server death или port deletion
- **Timeout handling:** `B_RELATIVE_TIMEOUT` для non-blocking operations где appropriate
- **Semaphore safety:** Detection и handling of invalid semaphores

**Message Processing Pipeline:**
- **Unflatten operations:** Proper BMessage reconstruction от port data
- **Event type tracking:** Specialized handling для B_MOUSE_MOVED events
- **Insert event capability:** Synthetic event injection для EventDispatcher requests
- **Quit coordination:** Clean shutdown protocol с input_server

**Связи:** BMessenger, shared_cursor, BMessageQueue, port system, atomic operations

### /home/ruslan/haiku/src/servers/app/InputManager.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `InputManager`  
**Назначение:** Централизованное управление EventStream объектами. Pool manager для connection к input_server, handling multiple concurrent streams, resource management, screen bounds propagation ко всем active streams.

**Resource Management Pattern:**
- **Object Pool:** `fFreeStreams`/`fUsedStreams` для efficient stream reuse
- **Factory Pattern:** Stream creation и validation через centralized interface
- **Singleton Pattern:** `gInputManager` global instance для system-wide access
- **RAII Pattern:** Automatic cleanup и resource management

**Stream Lifecycle Management:**
- `GetStream()` - retrieves validated stream от pool, creates new если necessary
- `PutStream()` - returns stream к pool или deletes если invalid
- `AddStream()/RemoveStream()` - manual pool management для custom streams
- **Validation loop:** Automatic cleanup of invalid streams в GetStream()

**System-wide Coordination:**
- `UpdateScreenBounds()` - propagates screen resolution changes ко всем streams
- Thread-safe operations через BLocker inheritance
- Global accessibility через `gInputManager` extern

**Связи:** EventStream pool, BLocker, system initialization, screen management

### /home/ruslan/haiku/src/servers/app/InputManager.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `InputManager`  
**Назначение:** Полная реализация EventStream pool manager с robust validation, lifecycle management, concurrent access protection, screen bounds coordination.

**Pool Management Implementation:**
- **Stream validation loop:** `GetStream()` iteratively validates streams, deleting invalid ones
- **Automatic cleanup:** Invalid streams automatically removed от pools
- **Thread-safe operations:** `BAutolock` protection для all pool modifications
- **Resource optimization:** Stream reuse prevents constant creation/destruction overhead

**Validation Strategy:**
- **Lazy validation:** Streams validated only when retrieved от pool
- **Cascading cleanup:** Invalid stream deletion triggers retry loop
- **Fail-safe design:** Returns NULL если no valid streams available

**Screen Bounds Propagation:**
- **Broadcast mechanism:** Updates sent ко всем active и pooled streams
- **State synchronization:** Ensures all streams have current screen configuration
- **Thread safety:** Protected updates через locking mechanism

**Global Singleton Management:**
- **Initialization:** `gInputManager` created by AppServer during startup
- **System integration:** Central point для all input stream management
- **Cleanup coordination:** Proper shutdown handling для all managed streams

**Связи:** EventStream implementations, BAutolock, BObjectList, global system state

### /home/ruslan/haiku/src/servers/app/MessageLooper.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `MessageLooper`  
**Назначение:** Базовый класс для threaded message processing в app_server. Абстрактная реализация thread-safe message loop с PortLink integration, lifecycle management, graceful shutdown support.

**Threading Architecture:**
- **Template Method Pattern:** `_MessageLooper()` определяет algorithm, `_DispatchMessage()` customizable by subclasses
- **Abstract Factory:** `MessagePort()` pure virtual для subclass-specific port provision
- **State Management:** `fQuitting` flag для graceful shutdown coordination
- **Priority Control:** B_DISPLAY_PRIORITY по умолчанию для UI-responsive operations

**Lifecycle Management Methods:**
- `Run()` - spawns message thread с proper error handling
- `Quit()` - initiates graceful shutdown с automatic cleanup
- `PostMessage()` - thread-safe message posting с timeout support
- **Thread synchronization:** `WaitForQuit()` utility for external thread coordination

**Safety Features:**
- **Thread identity checking:** Different behavior если called from own thread vs external
- **Port validation:** Error detection и reporting для port issues
- **Resource cleanup:** Automatic thread termination и memory management
- **Death semaphore:** Optional synchronization mechanism для external waiters

**Связи:** BLocker inheritance, PortLink, thread management, message dispatch interface

### /home/ruslan/haiku/src/servers/app/MessageLooper.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `MessageLooper`  
**Назначение:** Полная реализация threaded message loop infrastructure с sophisticated thread management, PortLink integration, error handling, graceful shutdown semantics.

**Thread Management Implementation:**
- **Safe thread spawning:** `spawn_thread()` with proper priority setting и error checking
- **Controlled lifecycle:** Thread suspension until `resume_thread()` для atomic initialization
- **Self-deletion safety:** Different quit paths depending on calling thread identity
- **Resource coordination:** Proper cleanup порядок для avoiding race conditions

**Message Processing Core:**
- **Message loop:** `_MessageLooper()` core loop с LinkReceiver integration
- **Error resilience:** Port deletion detection с proper error reporting
- **Graceful shutdown:** `kMsgQuitLooper` handling для clean thread termination
- **Lock coordination:** Message processing под lock protection

**PortLink Integration:**
- **LinkReceiver usage:** `GetNextMessage()` для structured message receipt
- **Message code handling:** Extensible dispatch через virtual `_DispatchMessage()`
- **Error reporting:** Detailed port error messages с thread identification

**Safety и Robustness:**
- **Thread identity awareness:** `find_thread(NULL)` для self-detection
- **Proper exit semantics:** `exit_thread(0)` для self-initiated termination
- **Name management:** Dynamic looper naming с fallback defaults
- **Lock state management:** Consistent lock/unlock patterns в message processing

**Связи:** PortLink infrastructure, BPrivate::LinkReceiver, thread coordination, BLocker

### /home/ruslan/haiku/src/servers/app/DelayedMessage.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `DelayedMessage`, `DelayedMessageData`, enum definitions  
**Назначение:** Sophisticated delayed message scheduling system с merge capabilities, failure callbacks, template-based data attachment, priority-based delivery. Предназначен для reducing redundant messages и providing efficient batching.

**Advanced Merge System:**
- **DMMergeMode enum:** Comprehensive merge strategies (NO_MERGE, REPLACE, CANCEL, DUPLICATES)
- **Data matching mask:** Fine-grained control over which data must match для merging (DM_DATA_1 through DM_DATA_6)
- **Smart merging logic:** Prevents duplicate work и reduces message spam в high-frequency scenarios

**Template-Based Data Attachment:**
- `Attach<Type>()` - type-safe data attachment с automatic sizeof calculation
- `AttachList<Type>()` - bulk attachment для object lists с optional filtering
- **Lazy memory allocation:** Memory только allocated при actual scheduling для efficiency
- **RAII design:** Stack-based usage с automatic cleanup

**Timing Control Constants:**
- **Granular timing:** От DM_MINIMUM_DELAY (500μs) до DM_ONE_HOUR_DELAY
- **Frame rate aligned:** DM_120HZ_DELAY, DM_60HZ_DELAY, DM_30HZ_DELAY для animation timing
- **UI responsive:** Short delays для immediate feedback, longer для background tasks

**Safety Features:**
- **Forbidden heap allocation:** Private new operators prevent dynamic allocation
- **Single-use design:** `fHandedOff` flag prevents reuse после scheduling
- **Type safety:** Template methods prevent data corruption
- **Failure callbacks:** Custom error handling для delivery failures

**Связи:** DelayedMessageData, template system, timing constants, callback mechanisms

### /home/ruslan/haiku/src/servers/app/DelayedMessage.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `DelayedMessage`, `DelayedMessageData`, `ScheduledMessage`, `DelayedMessageSender`  
**Назначение:** Complete delayed message scheduling infrastructure с global sender thread, sophisticated merge algorithms, memory management, priority-based delivery, failure handling.

**Global Singleton Architecture:**
- **DelayedMessageSender:** Global `gDelayedMessageSender` instance с dedicated thread
- **High priority thread:** B_URGENT_DISPLAY_PRIORITY для time-critical message delivery
- **Message port:** Dedicated port для thread communication и wakeup signals
- **Atomic operations:** Lock-free counters для performance tracking

**Sophisticated Merge Implementation:**
- **Data comparison algorithms:** `Compare()` method с mode-specific logic для DUPLICATES vs REPLACE
- **Target merging:** Automatic target list combination при successful merges
- **Memory optimization:** Merging avoids duplicate memory allocation
- **Complex merge rules:** Attachment count, size, и content matching requirements

**Advanced Scheduling Algorithm:**
- **Priority queue:** `BObjectList` с custom sort function для time-ordered delivery
- **Wakeup optimization:** Intelligent thread wakeup только when necessary
- **Batch sending:** Multiple message processing в single wakeup cycle
- **Timeout calculation:** Precise timeout computation с margin для timing accuracy

**Memory Management Strategy:**
- **Lazy allocation:** `CopyData()` delays memory allocation until absolutely necessary
- **Efficient cleanup:** Automatic removal of fully-delivered messages
- **Failed target handling:** Targets removed после delivery failures
- **Reference counting:** Safe cleanup при message completion

**Threading и Performance:**
- **Rate limiting:** Lock timeout (30ms) prevention от contention blocking
- **Atomic scheduling:** Lock-free wakeup time management
- **Retry mechanism:** Wakeup retry handling для port congestion
- **Efficient sorting:** Only resort when necessary после additions

**Robust Error Handling:**
- **Port failure detection:** B_BAD_PORT_ID handling с target cleanup
- **Timeout handling:** 1-second send timeout prevention от blocking
- **Lock contention:** Graceful fallback при lock acquisition failure
- **Partial failure recovery:** Continue operation даже при some target failures

**Связи:** BObjectList, PortLink, atomic operations, threading infrastructure, memory management

### /home/ruslan/haiku/src/servers/app/decorator/DefaultDecorator.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `DefaultDecorator`  
**Назначение:** Конкретная реализация декоратора - классический желтый BeOS/Haiku look. Наследуется от TabDecorator, реализует специфичную отрисовку кнопок, рамок, цветовых схем, bitmap generation для кнопок.

**Архитектурные особенности:**
- **Template Method implementation:** Переопределяет `_DrawFrame()`, `_DrawTab()`, `_DrawTitle()`, `_DrawClose()`, `_DrawZoom()`
- **Component-based colors:** `GetComponentColors()` для настройки цветов по компонентам
- **Bitmap caching:** `_GetBitmapForButton()` с общим кешем для всех экземпляров
- **Gradient rendering:** Использует `BGradientLinear` для современного look

**Ключевые методы:**
- `GetComponentColors()` - возвращает цвета для focused/unfocused состояний
- `_DrawBlendedRect()` - отрисовка кнопок с градиентами
- `_GetBitmapForButton()` - создание/кеширование bitmap кнопок
- `_DrawResizeKnob()` - специальная отрисовка resize handle

**Связи:** TabDecorator, BitmapDrawingEngine, BGradientLinear, ServerBitmap

### /home/ruslan/haiku/src/servers/app/decorator/DefaultDecorator.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `DefaultDecorator`  
**Назначение:** Полная реализация классического Haiku декоратора с детализированной отрисовкой всех элементов, bitmap generation, gradient effects, BeOS-compatible appearance.

**Сложные алгоритмы отрисовки:**
- **Multi-layer borders:** `_DrawFrame()` с различными стилями для разных window looks
- **Gradient buttons:** `_DrawBlendedRect()` с расчетом start/end цветов на основе pressed state
- **Resize knob rendering:** `_DrawResizeKnob()` с dots pattern для B_DOCUMENT_WINDOW_LOOK
- **Tab gradient filling:** Сложная логика для kLeftTitledWindowLook vs обычных вкладок
- **Bitmap generation:** Статический кеш с `decorator_bitmap` linked list

**Bitmap Management:** `_GetBitmapForButton()` создает shared bitmap cache с проверкой размеров, цветов, состояний кнопок. Использует `BitmapDrawingEngine` для off-screen rendering.

**Цветовая система:** Сложная иерархия цветов с tinting, separate schemes для focused/unfocused состояний, highlight support для resize borders.

**Связи:** BitmapDrawingEngine, BGradientLinear, ServerBitmap, PatternHandler

### /home/ruslan/haiku/src/servers/app/decorator/TabDecorator.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `TabDecorator`  
**Назначение:** Промежуточная абстракция между базовым Decorator и конкретными реализациями. Добавляет поддержку табов, layout management, color management, region handling. Предоставляет framework для tab-based декораторов.

**Архитектурные расширения:**
- **Layout Management:** `_DoLayout()`, `_DoTabLayout()`, `_DistributeTabSize()` для сложного размещения вкладок
- **Color Framework:** Enum константы для цветовых компонентов и их индексов
- **Component System:** `Component` enum для различных частей декоратора
- **Settings Support:** Расширенная поддержка tab locations и preferences

**Ключевые структуры:**
- `COLOR_*` константы для индексации цветов компонентов
- `Component` enum для идентификации частей декоратора
- `ComponentColors[7]` typedef для цветовых схем
- Tab size calculation с `minTabSize`/`maxTabSize`

**Layout Algorithm:** Сложная система распределения пространства между вкладками с поддержкой minimum/maximum размеров, truncation текста, dynamic repositioning.

**Связи:** Decorator, DesktopSettings, DrawingEngine, ServerFont

### /home/ruslan/haiku/src/servers/app/decorator/TabDecorator.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `TabDecorator`  
**Назначение:** Полная реализация tab-based decorator framework с sophisticated layout algorithms, dynamic tab management, settings persistence, multi-tab support.

**Сложные алгоритмы layout:**
- **Tab distribution:** `_DistributeTabSize()` рекурсивно перераспределяет пространство между вкладками
- **Dynamic positioning:** `_SetTabLocation()` с real-time preview и collision detection
- **Text truncation:** Smart truncation с `B_TRUNCATE_MIDDLE`/`B_TRUNCATE_END` в зависимости от размера
- **Button layout:** `_LayoutTabItems()` размещает кнопки с учетом доступного пространства
- **Stacking support:** Специальная логика для stacked windows с shared tabs

**Region Management:** Расширенный `RegionAt()` с поддержкой borders, corners, resize areas. Умная обработка hit-testing для различных window looks.

**Settings Persistence:** `_SetSettings()`/`GetSettings()` сохраняет tab locations, border width, frame dimensions в BMessage формате.

**Multi-workspace Support:** Корректная обработка geometry при переключении рабочих пространств.

**Связи:** BGradientLinear, DesktopSettings, ServerFont, BMessage

### /home/ruslan/haiku/src/servers/app/decorator/WindowBehaviour.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `WindowBehaviour`  
**Назначение:** Абстрактный базовый класс для поведения окон при пользовательском взаимодействии. Определяет интерфейс для обработки mouse events, keyboard input, window manipulation с поддержкой magnetic snapping.

**Архитектурные паттерны:**
- **Strategy Pattern:** Различные поведения (Default, SAT) реализуют единый интерфейс
- **State Pattern:** `fIsDragging`, `fIsResizing` отслеживают текущее состояние
- **Template Method:** `AlterDeltaForSnap()` как hook для наследников
- **Command Pattern:** Mouse events инкапсулируют действия пользователя

**Ключевые методы:**
- `MouseDown()`/`MouseUp()`/`MouseMoved()` - core event handling
- `ModifiersChanged()` - реакция на изменение модификаторов клавиатуры
- `AlterDeltaForSnap()` - virtual hook для magnetic border effects

**Event Handling Contract:** Детальная документация для `MouseDown()` с click counting, region validation, multi-click handling.

**Связи:** BMessage, BPoint, ClickTarget, Window

### /home/ruslan/haiku/src/servers/app/decorator/WindowBehaviour.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `WindowBehaviour`  
**Назначение:** Базовая реализация window behaviour с минимальной функциональностью и документированными hooks для наследников.

**Minimalist Implementation:** Предоставляет базовую структуру без специфичного поведения, позволяя наследникам полностью переопределить interaction logic.

**Связи:** Base implementation для DefaultWindowBehaviour, SATWindowBehaviour

### /home/ruslan/haiku/src/servers/app/decorator/DefaultWindowBehaviour.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `DefaultWindowBehaviour`  
**Назначение:** Стандартное поведение окон Haiku с полной поддержкой dragging, resizing, stacking, window management. Реализует сложную state machine для различных режимов взаимодействия.

**Архитектурные паттерны:**
- **State Machine:** Сложная иерархия состояний через State классы
- **Strategy Pattern:** Различные Action enum для различных типов взаимодействия
- **Observer Pattern:** Слежение за modifiers для window management mode
- **Command Pattern:** Инкапсуляция действий в State объекты

**State Hierarchy:**
- `State` - базовый класс состояния
- `MouseTrackingState` - отслеживание mouse с rate limiting
- `DragState` - перетаскивание окон с magnetic snapping
- `ResizeState` - изменение размера с outline support
- `SlideTabState` - перемещение вкладок
- `ResizeBorderState` - resize through borders
- `DecoratorButtonState` - обработка кнопок декоратора
- `ManageWindowState` - window management mode

**Ключевые возможности:**
- **Magnetic borders:** `MagneticBorder` для snapping к краям экрана
- **Modifier detection:** `_IsWindowModifier()` для Cmd+Ctrl комбинации
- **Cursor management:** `_ResizeCursorFor()` для directional resize cursors
- **Border highlighting:** Visual feedback для resize regions

**Связи:** Window, Desktop, ServerCursor, MagneticBorder, Decorator

### /home/ruslan/haiku/src/servers/app/decorator/DefaultWindowBehaviour.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `DefaultWindowBehaviour`, State hierarchy  
**Назначение:** Полная реализация стандартного window behaviour с sophisticated state management, mouse tracking, keyboard shortcuts, window manipulation.

**State Machine Implementation:** Каждое состояние инкапсулирует specific behavior:
- **MouseTrackingState:** Rate limiting (13333 μs), distance tracking, timeout handling
- **DragState:** Right-click behind/front switching, magnetic snapping integration
- **ResizeState:** Outline support, constraint handling, delta calculations
- **SlideTabState:** Real-time tab positioning с multi-tab coordination
- **ResizeBorderState:** 8-directional resizing с cursor feedback
- **DecoratorButtonState:** Button press визуализация с proper redraw
- **ManageWindowState:** Window modifier mode с border highlighting

**Сложные алгоритмы:**
- **Mouse tracking:** Smooth movement с rate limiting и distance calculation
- **Resize direction computation:** Математическое определение direction через atan2f
- **Tab sliding:** Multi-tab coordination с collision detection
- **Window activation:** Smart activation logic с timeout handling
- **Cursor management:** Dynamic cursor switching для resize operations

**Integration Points:** Тесная интеграция с Desktop для window operations, ClickTarget для region detection, MagneticBorder для snapping effects.

**Связи:** Desktop, Window, DrawingEngine, ServerCursor, ClickTarget

### /home/ruslan/haiku/src/servers/app/decorator/MagneticBorder.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `MagneticBorder`  
**Назначение:** Реализация "magnetic" snapping окон к границам экрана. Алгоритм притягивания окон при приближении к краям экрана с настраиваемыми параметрами силы притяжения и временными интервалами.

**Ключевые возможности:**
- `AlterDeltaForSnap()` - основной алгоритм snapping с двумя перегрузками
- Поддержка как Window objects, так и generic BRect + Screen
- Temporal control с pause intervals между snapping events
- Distance-based snapping с configurable threshold

**Архитектурная роль:** Utility класс для DefaultWindowBehaviour, обеспечивает smooth user experience при window management.

**Связи:** Window, Screen, BPoint, BRect

### /home/ruslan/haiku/src/servers/app/decorator/MagneticBorder.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `MagneticBorder`  
**Назначение:** Полная реализация magnetic snapping с sophisticated timing и distance algorithms.

**Snapping Algorithm:**
- **Distance calculation:** Вычисление расстояния до всех четырех краев экрана
- **Threshold check:** kSnapDistance (8.0px) для активации snapping
- **Closest edge selection:** Автоматический выбор ближайшего края
- **Temporal control:** kSnappingDuration (1.5s) и kSnappingPause (3s) для prevent spam
- **Delta modification:** Корректировка movement delta для точного выравнивания

**Timing Control:** Предотвращает repeated snapping через временные интервалы, обеспечивая natural feel при window manipulation.

**Связи:** Window, Screen, Desktop, mathematical calculations

### /home/ruslan/haiku/src/servers/app/stackandtile/SATDecorator.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `SATDecorator`, `SATWindowBehaviour`  
**Назначение:** Stack And Tile расширение декоратора и поведения. SATDecorator добавляет специальный highlighting для SAT operations, SATWindowBehaviour интегрирует StackAndTile snapping в window behaviour.

**Архитектурные расширения:**
- **Decorator Enhancement:** `HIGHLIGHT_STACK_AND_TILE` для visual feedback
- **Behaviour Integration:** `SATWindowBehaviour` расширяет `DefaultWindowBehaviour`
- **Color Customization:** Специальные цвета для SAT highlighting
- **Snapping Integration:** `AlterDeltaForSnap()` для group-aware snapping

**SAT-Specific Features:**
- Highlight colors для stack and tile operations
- Group-aware magnetic snapping через StackAndTile integration
- Enhanced component colors для SAT visual feedback

**Связи:** DefaultDecorator, DefaultWindowBehaviour, StackAndTile

### /home/ruslan/haiku/src/servers/app/stackandtile/SATDecorator.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `SATDecorator`, `SATWindowBehaviour`  
**Назначение:** Полная реализация SAT декоратора с специализированными цветовыми схемами и group-aware snapping behaviour.

**Color Enhancement:** Специальные цветовые константы `kFrameColors[4]` и `kHighlightFrameColors[6]` для SAT highlighting, создающие визуальную обратную связь при stack and tile operations.

**Group Snapping:** `SATWindowBehaviour::AlterDeltaForSnap()` вычисляет group frame и применяет magnetic snapping ко всей группе, а не только к отдельному окну.

**Архитектурная интеграция:** Seamless extension существующего decorator framework без нарушения base functionality.

**Связи:** DefaultDecorator, StackAndTile, SATGroup, SATWindow

### /home/ruslan/haiku/src/servers/app/stackandtile/SATGroup.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `SATGroup`, `WindowArea`, `Tab`, `Crossing`, `Corner`  
**Назначение:** Ядро Stack And Tile архитектуры. Реализует сложную constraint-based layout систему для группировки и размещения окон с использованием linear programming solver.

**Архитектурные паттерны:**
- **Constraint Solving:** LinearSpec для автоматического layout решения
- **Graph Structure:** Tab/Crossing/Corner образуют constraint graph
- **Composite Pattern:** WindowArea содержит множество SATWindow
- **Observer Pattern:** Reference counting и automatic cleanup
- **Strategy Pattern:** Different corner positions и layout strategies

**Ключевые структуры:**
- **Corner:** Состояние угла constraint grid (Free/Used/NotDockable)
- **Crossing:** Пересечение вертикального и горизонтального Tab
- **Tab:** Constraint variable для позиции линии
- **WindowArea:** Прямоугольная область для размещения окон
- **SATGroup:** Коллекция связанных WindowArea

**Linear Programming Integration:** Использует LinearSpec constraint solver для автоматического вычисления optimal layout с support для min/max размеров окон.

**Complex Algorithms:**
- Group splitting при удалении окон
- Constraint propagation между группами
- Automatic layout optimization
- Screen boundary enforcement

**Связи:** LinearSpec, SATWindow, Constraint solver, BReferenceable

### /home/ruslan/haiku/src/servers/app/stackandtile/SATGroup.cpp
**Тип:** Реализация C++  
**Ключевые классы:** SATGroup hierarchy  
**Назначение:** Полная реализация constraint-based window layout system с sophisticated graph management, constraint solving, group operations.

**Constraint System Implementation:**
- **WindowArea constraints:** min/max width/height, preferred size с penalty functions
- **Tab positioning:** Linear variables с kMakePositiveOffset для solver compatibility
- **Constraint penalties:** kExtentPenalty, kHighPenalty, kInequalityPenalty для prioritization
- **Layout solving:** Iterative solving до 15 попыток с fallback strategies

**Group Management Algorithms:**
- **Window addition:** `AddWindow()` с constraint creation и area management
- **Window removal:** `RemoveWindow()` с automatic group splitting
- **Group splitting:** `_SplitGroupIfNecessary()` находит connected components
- **Neighbour detection:** `_FillNeighbourList()` для spatial relationships
- **Screen enforcement:** `_EnsureGroupIsOnScreen()` предотвращает off-screen groups

**Persistence System:** `ArchiveGroup()`/`RestoreGroup()` для сохранения групповой структуры между сессиями.

**Mathematical Foundation:** Extensive использование constraint solving с automatic variable management, penalty-based optimization, iterative refinement.

**Связи:** LinearSpec, Variable, Constraint, Desktop, BMessage

### /home/ruslan/haiku/src/servers/app/stackandtile/SATWindow.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `SATWindow`  
**Назначение:** SAT обертка для Window объектов. Добавляет SAT-specific functionality: snapping behaviour, group management, размерные ограничения, visual highlighting, settings persistence.

**Архитектурные роли:**
- **Adapter Pattern:** Адаптирует Window для SAT system
- **Decorator Pattern:** Добавляет SAT функциональность к существующим Window
- **Strategy Pattern:** `SATSnappingBehaviourList` для различных snapping стратегий
- **State Pattern:** Tracking состояния в группах и areas

**SAT-Specific Features:**
- **Snapping behaviours:** `SATStacking`, `SATTiling` для различных типов snapping
- **Group integration:** Seamless integration с SATGroup constraint system
- **Size management:** Original size limits preservation и restoration
- **Visual feedback:** Tab/border highlighting для SAT operations
- **Settings persistence:** Unique ID generation и settings storage

**Complex Interactions:**
- Automatic group creation при first SAT operation
- Constraint updates при window resize
- Tab position management для stacked windows
- Decorator integration для size calculations

**Связи:** Window, SATGroup, WindowArea, StackAndTile, SATDecorator

### /home/ruslan/haiku/src/servers/app/stackandtile/SATWindow.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `SATWindow`  
**Назначение:** Полная реализация SAT window wrapper с comprehensive group management, snapping coordination, size limit handling, visual feedback.

**Group Lifecycle Management:**
- **Automatic group creation:** `GetGroup()` создает группу при первом access
- **Tab positioning:** Manual tab positioning для single windows
- **Group integration:** `AddedToGroup()`/`RemovedFromGroup()` hooks
- **Size restoration:** `_RestoreOriginalSize()` при удалении из группы

**Snapping Coordination:**
- **Candidate detection:** `FindSnappingCandidates()` через behaviour list
- **Behaviour management:** `SATStacking` и `SATTiling` в unified list
- **Visual feedback:** Real-time highlighting через `HighlightTab()`/`HighlightBorders()`
- **Join operations:** `JoinCandidates()` для finalization

**Size Management:**
- **Decorator integration:** `AddDecorator()` для accurate size calculations
- **Constraint updates:** `AdjustSizeLimits()` для constraint solver
- **Original preservation:** Restoration при SAT exit
- **Resize handling:** Dynamic size updates в группах

**Settings System:** Unique ID generation через timestamp+random, persistent settings storage.

**Связи:** StackAndTile, SATGroup, SATDecorator, ServerApp, Desktop

### /home/ruslan/haiku/src/servers/app/stackandtile/StackAndTile.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `StackAndTile`, `GroupIterator`, `WindowIterator`, `SATSnappingBehaviour`  
**Назначение:** Центральная система Stack And Tile как DesktopListener. Координирует SAT operations, keyboard shortcuts, group management, settings persistence.

**Архитектурные паттерны:**
- **Observer Pattern:** DesktopListener для window lifecycle events
- **Mediator Pattern:** Координирует взаимодействие между SAT компонентами
- **Command Pattern:** Keyboard shortcuts как commands
- **Iterator Pattern:** GroupIterator, WindowIterator для traversal
- **Strategy Pattern:** `SATSnappingBehaviour` для different snapping types

**Key Management Systems:**
- **Window mapping:** `SATWindowMap` для Window→SATWindow mapping
- **Group iteration:** Smart group traversal с workspace filtering
- **State management:** SAT key state tracking и mode switching
- **Settings coordination:** Group archival/restoration

**Keyboard Interface:**
- Option key для SAT mode activation
- Arrow keys для group/window navigation
- Tab key для window switching в группах
- Page Up/Down для group switching

**Iterator Infrastructure:** Sophisticated iteration через groups и windows с support для layer ordering, workspace filtering, reverse traversal.

**Связи:** Desktop, SATWindow, SATGroup, WindowArea, BMessage

### /home/ruslan/haiku/src/servers/app/stackandtile/StackAndTile.cpp
**Тип:** Реализация C++  
**Ключевые классы:** StackAndTile system  
**Назначение:** Полная реализация Stack And Tile system с comprehensive event handling, keyboard navigation, group management, settings persistence.

**Event Handling System:**
- **Mouse events:** `MouseDown()`/`MouseUp()` для SAT activation
- **Key handling:** Comprehensive keyboard shortcuts с modifier detection
- **Window events:** Lifecycle tracking через DesktopListener interface
- **Workspace coordination:** Multi-workspace SAT operations

**Navigation Implementation:**
- **Group navigation:** Page Up/Down с wrap-around через all groups
- **Window navigation:** Arrow keys/Tab для switching внутри группы
- **Smart activation:** Preservation последнего active window per group
- **Focus coordination:** Complex focus management через multiple areas

**Settings Architecture:**
- **Group archival:** Complete group state serialization
- **Window restoration:** ID-based window matching для restored groups
- **Persistence coordination:** Integration с app_server settings system

**Iterator Implementation:** Sophisticated traversal algorithms с filtering, workspace awareness, proper ordering maintenance.

**Message Handling:** `BPrivate::kSaveAllGroups`, `kRestoreGroup` для external SAT control.

**Связи:** Desktop, SATWindow, SATGroup, BMessage, window management

### /home/ruslan/haiku/src/servers/app/stackandtile/Stacking.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `SATStacking`, `StackingEventHandler`  
**Назначение:** Реализация window stacking behaviour для SAT system. SATStacking - snapping behaviour для создания stacked windows, StackingEventHandler - message handling для programmatic stacking control.

**Stacking Architecture:**
- **Snapping Implementation:** `SATSnappingBehaviour` для tab-based stacking
- **Event Handling:** `StackingEventHandler` для external API calls
- **Window Validation:** `_IsStackableWindow()` для compatibility checking
- **Visual Feedback:** Highlighting coordination для stacking preview

**API Messages:**
- `kAddWindowToStack` - добавление окна в stack
- `kRemoveWindowFromStack` - удаление окна из stack
- `kCountWindowsOnStack` - получение количества окон
- `kWindowOnStackAt` - доступ к окну по индексу
- `kStackHasWindow` - проверка принадлежности stack

**Связи:** SATWindow, WindowArea, SATSnappingBehaviour

### /home/ruslan/haiku/src/servers/app/stackandtile/Stacking.cpp
**Тип:** Реализация C++  
**Ключевые классы:** Stacking system  
**Назначение:** Полная реализация window stacking с comprehensive API, validation, visual feedback, error handling.

**Stacking Algorithm:**
- **Target detection:** Mouse position analysis для finding stacking target
- **Tab hit testing:** Precise tab rectangle intersection checking
- **Window validation:** Compatibility checking через `_IsStackableWindow()`
- **Visual coordination:** Synchronized highlighting между parent/child

**API Implementation:** Complete message handling с proper error codes, parameter validation, team/port verification для security.

**Window Lifecycle:** Proper cleanup при area removal, window look changes, stack modifications.

**Связи:** Desktop, ServerWindow, SATGroup, WindowArea

### /home/ruslan/haiku/src/servers/app/stackandtile/Tiling.h
**Тип:** Заголовочный файл C++  
**Ключевые классы:** `SATTiling`  
**Назначение:** Реализация window tiling behaviour для автоматического размещения окон в constraint grid. Находит свободные области в existing groups и размещает новые окна с optimal fit.

**Tiling Architecture:**
- **Free Area Detection:** Complex algorithms для finding available space
- **Corner Analysis:** Four-corner approach для area placement
- **Error Minimization:** Best-fit algorithm с size matching
- **Overlap Prevention:** Collision detection с existing windows
- **Visual Preview:** Border highlighting для tiling preview

**Complex Algorithms:**
- `_FindFreeAreaInGroup()` - поиск свободных областей
- `_InteresstingCrossing()` - анализ constraint intersections
- `_CheckArea()` - validation area для placement
- `_FreeAreaError()` - size matching optimization

**Связи:** SATGroup, WindowArea, Tab, Crossing, Corner

### /home/ruslan/haiku/src/servers/app/stackandtile/Tiling.cpp
**Тип:** Реализация C++  
**Ключевые классы:** `SATTiling`  
**Назначение:** Полная реализация sophisticated tiling system с complex geometric analysis, constraint solving, optimization algorithms.

**Free Area Detection Algorithm:**
- **Corner enumeration:** Systematic checking всех четырех corners
- **Crossing analysis:** Detailed constraint intersection evaluation
- **Distance calculation:** Precise proximity matching с kMaxMatchingDistance
- **Area validation:** Multi-criteria checking для valid placement
- **Optimization:** Best-fit selection с error minimization

**Geometric Calculations:**
- **Border matching:** Pixel-perfect alignment с existing windows
- **Size optimization:** Minimal error между window size и available area
- **Overlap detection:** Comprehensive collision checking
- **Constraint satisfaction:** Integration с constraint solver

**Visual Feedback System:** Four-directional border highlighting с precise targeting для affected windows.

**Error Handling:** Graceful fallback при inability to find suitable areas, proper cleanup on failures.

**Связи:** SATWindow, SATGroup, Tab, Corner, constraint mathematics
