# Архитектурный анализ 2D-рендеринга Haiku для миграции AGG→Blend2D

## СХЕМА ТЕКУЩЕЙ АРХИТЕКТУРЫ

```
╔═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗
║                                                    ПУБЛИЧНЫЙ BeAPI (НЕИЗМЕНЯЕМЫЙ)                                                                ║
╠═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣
║ BView:                                                                                                                                            ║
║   • Draw(), StrokeRect(), FillRect(), DrawString(), DrawBitmap()                                                                                  ║
║   • SetDrawingMode(), SetBlendingMode(), SetPenSize()                                                                                             ║
║   • StrokePolygon(), FillPolygon(), StrokeEllipse(), FillEllipse()                                                                               ║
║   • StrokeArc(), FillArc(), StrokeBezier(), FillBezier()                                                                                          ║
║                                                                                                                                                   ║
║ BBitmap: CreateObject(), SetBits(), LockBits()                                                                                                   ║
║ BPicture: Play(), RecordState()                                                                                                                  ║
║ BGradient: Linear, Radial, Diamond, Conic градиенты                                                                                              ║
╚═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝
                                                         ▲
                                                         │ BMessage protocol
                                                         ▼
╔═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗
║                                              APP_SERVER (SERVER-CLIENT АРХИТЕКТУРА)                                                             ║
╠═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣
║ ServerWindow → View → Canvas                                                                                                                      ║
║ ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │                                       СЛОЙ АБСТРАКЦИИ (СОХРАНЯЕМЫЙ)                                                                          │ ║
║ │ DrawState: трансформации, клиппинг, состояние рисования                                                                                       │ ║
║ │ DrawingEngine: основной интерфейс рендеринга                                                                                                  │ ║
║ │ Canvas: управление состоянием, координатные преобразования                                                                                    │ ║
║ └─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ ║
║                                                         ▲                                                                                        ║
║                                                         │                                                                                        ║
║                                                         ▼                                                                                        ║
║ ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │                                       PAINTER (ПОЛНАЯ ЗАМЕНА)                                                                                 │ ║
║ │ ┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │ ║
║ │ │                              AGG-СПЕЦИФИЧНЫЙ КОД (ЗАМЕНЯЕТСЯ НА BLEND2D)                                                                  │ │ ║
║ │ │ • PainterAggInterface: AGG rendering buffer, rasterizer, renderer                                                                        │ │ ║
║ │ │ • AGGTextRenderer: растеризация шрифтов через AGG                                                                                        │ │ ║
║ │ │ • defines.h: типы AGG pipeline (scanline, rasterizer, renderer)                                                                         │ │ ║
║ │ │ • Все agg_*.h файлы: кастомные AGG расширения                                                                                           │ │ ║
║ │ └──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │ ║
║ │ ┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │ ║
║ │ │                                ЛОГИКА РЕНДЕРИНГА (ЧАСТИЧНО СОХРАНЯЕТСЯ)                                                                   │ │ ║
║ │ │ • PatternHandler: обработка паттернов BeOS                                                                                               │ │ ║
║ │ │ • DrawingMode система: B_COPY, B_OVER, B_BLEND etc                                                                                       │ │ ║
║ │ │ • Transformable: матрицы трансформаций                                                                                                   │ │ ║
║ │ │ • Градиенты: BGradient абстракция                                                                                                        │ │ ║
║ │ └──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │ ║
║ └─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ ║
║                                                         ▲                                                                                        ║
║                                                         │                                                                                        ║
║                                                         ▼                                                                                        ║
║ ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │                                     HARDWARE INTERFACE (ОСТАЕТСЯ)                                                                            │ ║
║ │ HWInterface → BitmapHWInterface/AccelerantHWInterface                                                                                         │ ║
║ │ RenderingBuffer: доступ к framebuffer                                                                                                        │ ║
║ └─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ ║
╚═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝
```

## КОМПОНЕНТЫ ПО КАТЕГОРИЯМ

### ПУБЛИЧНЫЕ API (НЕИЗМЕНЯЕМЫЕ - КРИТИЧНО!)

**BView методы рисования:**
- `StrokeRect()`, `FillRect()`, `StrokeEllipse()`, `FillEllipse()`
- `StrokePolygon()`, `FillPolygon()`, `StrokeArc()`, `FillArc()`
- `StrokeBezier()`, `FillBezier()`, `StrokeShape()`, `FillShape()`
- `DrawString()`, `DrawBitmap()`, `DrawPicture()`
- `SetDrawingMode()`, `SetBlendingMode()`, `SetPenSize()`

**Критические типы:**
- `drawing_mode`: B_COPY, B_OVER, B_BLEND, B_ALPHA, B_ADD, B_SUBTRACT
- `source_alpha`: B_PIXEL_ALPHA, B_CONSTANT_ALPHA
- `alpha_function`: B_ALPHA_OVERLAY, B_ALPHA_COMPOSITE
- `pattern`: битовые маски для паттернов
- `BGradient`: все типы градиентов

### СЛОЙ АБСТРАКЦИИ (СОХРАНЯЕМЫЙ)

**DrawingEngine:**
- Основной интерфейс между Canvas и Painter
- Управление состоянием рисования
- Координация с HWInterface
- **Статус:** Остается без изменений

**Canvas/View:**
- Иерархия видов и клиппинг
- Координатные трансформации
- Управление состоянием
- **Статус:** Остается без изменений

**DrawState:**
- Стек состояний рисования
- Трансформации, цвета, шрифты
- **Статус:** Остается без изменений

### PAINTER - ПОЛНАЯ ЗАМЕНА

**AGG-специфичные компоненты (УДАЛЯЮТСЯ):**

`PainterAggInterface.h`:
```cpp
// ЗАМЕНЯЕТСЯ НА PainterBlend2DInterface
agg::rendering_buffer fBuffer;
pixfmt fPixelFormat;              → bl::BLImage
renderer_base fBaseRenderer;      → bl::BLContext
scanline_unpacked_type fUnpackedScanline;
rasterizer_type fRasterizer;      → встроено в BLContext
```

`AGGTextRenderer`:
```cpp
// ЗАМЕНЯЕТСЯ НА Blend2DTextRenderer
FontCacheEntry::GlyphPathAdapter  → BLGlyphBuffer
agg::trans_affine                 → BLMatrix2D
rasterizer_type fRasterizer       → BLContext::fillGlyphRun
```

`defines.h`:
```cpp
// ВСЕ AGG ТИПЫ ЗАМЕНЯЮТСЯ
typedef agg::scanline_u8          → typedef bl::uint8_t
typedef agg::rasterizer_scanline_aa → не нужно (встроено в BLContext)
typedef agg::renderer_scanline_aa_solid → не нужно
```

**Логика рендеринга (АДАПТИРУЕТСЯ):**

`PatternHandler`:
- Обработка BeOS паттернов
- **Статус:** Адаптируется для Blend2D

`DrawingMode система`:
- Соответствие drawing_mode → BLCompOp
- **Статус:** Создается mapping таблица

`Transformable`:
- AGG agg::trans_affine → Blend2D BLMatrix2D
- **Статус:** Интерфейс остается, реализация меняется

## КРИТИЧЕСКИЕ ТОЧКИ ИНТЕГРАЦИИ

### 1. Server-Client Messaging
- **Локация:** `ServerWindow.cpp`, протокол BMessage
- **Критичность:** ВЫСОКАЯ - изменения ломают совместимость
- **Действие:** БЕЗ ИЗМЕНЕНИЙ

### 2. BeAPI совместимость
- **Локация:** Все публичные заголовки `headers/os/interface/`
- **Критичность:** КРИТИЧЕСКАЯ - бинарная совместимость
- **Действие:** АБСОЛЮТНО БЕЗ ИЗМЕНЕНИЙ

### 3. Drawing Modes
- **Локация:** `drawing_modes/` директория
- **Критичность:** ВЫСОКАЯ - поведение рисования
- **Действие:** Создать точный mapping AGG modes → Blend2D

### 4. Font Rendering
- **Локация:** `AGGTextRenderer`, `FontEngine`
- **Критичность:** ВЫСОКАЯ - качество текста
- **Действие:** Полная замена с сохранением результата

### 5. Subpixel Rendering
- **Локация:** Все `*_subpix*` файлы
- **Критичность:** СРЕДНЯЯ - качество на LCD
- **Действие:** Реализация через Blend2D LCD optimization

## ПЛАН СОХРАНЕНИЯ СОВМЕСТИМОСТИ API

### Уровень 1: Бинарная совместимость BeAPI
```cpp
// headers/os/interface/View.h - НЕ ТРОГАТЬ!
class BView {
public:
    void StrokeRect(BRect rect, pattern p = B_SOLID_HIGH);
    void SetDrawingMode(drawing_mode mode);
    // ... все остальные методы ОСТАЮТСЯ
};
```

### Уровень 2: Протокол Server-Client
```cpp
// ServerWindow.cpp - message handling ОСТАЕТСЯ
case AS_STROKE_RECT:
    link.Read<BRect>(&rect);
    link.Read<pattern>(&pat);
    // преобразование в внутренние вызовы БЕЗ ИЗМЕНЕНИЙ
```

### Уровень 3: DrawingEngine интерфейс
```cpp
// DrawingEngine.h - интерфейс ОСТАЕТСЯ
class DrawingEngine {
public:
    virtual void StrokeRect(BRect rect);
    virtual void SetDrawingMode(drawing_mode mode);
    // реализация меняется ВНУТРИ Painter
};
```

### Уровень 4: Painter - ПОЛНАЯ ЗАМЕНА
```cpp
// Painter.h - интерфейс ОСТАЕТСЯ, реализация ПОЛНОСТЬЮ МЕНЯЕТСЯ
class Painter {
private:
    // БЫЛО: PainterAggInterface fInternal;
    // СТАНЕТ: PainterBlend2DInterface fInternal;

    // БЫЛО: AGGTextRenderer fTextRenderer;
    // СТАНЕТ: Blend2DTextRenderer fTextRenderer;
};
```

## СТРАТЕГИЯ ПОЭТАПНОЙ МИГРАЦИИ

### Этап 1: Создание Blend2D Backend (4-6 недель)
1. **PainterBlend2DInterface** - замена PainterAggInterface
2. **Blend2DTextRenderer** - замена AGGTextRenderer
3. **Базовые примитивы** - rect, ellipse, polygon
4. **Pattern mapping** - BeOS patterns → Blend2D

### Этап 2: Drawing Modes (2-3 недели)
1. **Создание mapping таблицы** AGG comp ops → Blend2D comp ops
2. **Тестирование совместимости** всех 13 drawing modes
3. **Субпиксельный рендеринг** адаптация

### Этап 3: Расширенные возможности (3-4 недели)
1. **Градиенты** - все типы BGradient
2. **Bezier кривые** и **BShape**
3. **Bitmap rendering** с фильтрацией
4. **Optimizations** - SIMD, multithreading

### Этап 4: Интеграция и тестирование (2-3 недели)
1. **Сборка с Blend2D** - линковка
2. **Regression testing** - визуальное сравнение
3. **Performance benchmarks**
4. **Замена по умолчанию**

## КРИТИЧЕСКИЕ РИСКИ И РЕШЕНИЯ

### Риск 1: Нарушение бинарной совместимости
**Причина:** Изменение размеров структур или виртуальных таблиц
**Решение:** Использовать PIMPL паттерн для внутренних изменений
```cpp
// DrawingEngine остается тем же размером
class DrawingEngine {
private:
    void* fImplementation; // указатель на Blend2D backend
};
```

### Риск 2: Различия в drawing modes
**Причина:** AGG и Blend2D по-разному интерпретируют blending
**Решение:** Создать precision-тесты и кастомные comp ops при необходимости

### Риск 3: Производительность текста
**Причина:** Различия в кешировании глифов
**Решение:** Сохранить существующий FontCache, адаптировать для Blend2D

### Риск 4: Субпиксельный рендеринг
**Причина:** AGG имеет кастомную реализацию subpixel AA
**Решение:** Использовать Blend2D LCD optimization + fallback на AGG алгоритм

### Риск 5: Зависимости сборки
**Причина:** Добавление внешней библиотеки Blend2D
**Решение:** Статическая линковка или включение в дерево исходников

## ЗАКЛЮЧЕНИЕ

Архитектура Haiku позволяет **ПОЛНУЮ ЗАМЕНУ AGG на Blend2D** без нарушения совместимости благодаря:

1. **Четкому разделению** публичного API и приватной реализации
2. **Слою абстракции** через DrawingEngine
3. **Изоляции AGG кода** в Painter компоненте

**Ключевые принципы миграции:**
- Публичный BeAPI остается **НЕИЗМЕННЫМ**
- Server-Client протокол остается **НЕИЗМЕННЫМ**
- Изменения **ТОЛЬКО** в Painter и ниже
- **100% функциональная совместимость** всех drawing modes
- **Сохранение или улучшение** производительности

Миграция выполнима за **11-16 недель** с минимальными рисками при соблюдении архитектурных границ.