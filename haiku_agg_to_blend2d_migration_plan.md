# План миграции Haiku OS: AGG → Blend2D

## Обзор задачи

Заменить библиотеку 2D-рендеринга в Haiku OS с Anti-Grain Geometry (AGG) на Blend2D при сохранении 100% совместимости публичного API Haiku OS. Blend2D и AsmJIT уже включены в сборку, AGG будет полностью удален.

## Анализ текущей архитектуры

### Карта файлов и их назначение

Анализируемая папка: `haiku/src/servers/app/drawing/Painter/drawing_modes`

| Файл | Назначение | Тип | Зависимости AGG |
|------|-----------|-----|-----------------|
| **defines.h** | Центральные определения типов AGG pipeline | Ключевой | Все AGG типы |
| **PixelFormat.h/cpp** | Адаптер AGG pixel format для Haiku | Ключевой | agg::rendering_buffer, composition ops |
| **DrawingMode*.h** | Реализации режимов рисования Haiku | Основной | AGG типы и буферы |
| **PatternHandler.h/cpp** | Обработка паттернов рисования | Вспомогательный | Использует AGG буферы |
| **AggCompOpAdapter.h** | Адаптер композитных операций AGG | Специализированный | agg::comp_op_* |
| **GlobalSubpixelSettings.h/cpp** | Настройки субпиксельного рендеринга | Конфигурационный | Независим от AGG |
| **drawing_support.h** | Вспомогательные функции рисования | Утилитарный | Частично использует AGG типы |

### Анализ режимов рисования

**Обычные режимы (без субпикселей):**
- DrawingModeAdd.h
- DrawingModeAlphaCC.h (Constant Composite)
- DrawingModeAlphaCO.h (Constant Overlay) 
- DrawingModeAlphaPC.h (Pixel Composite)
- DrawingModeAlphaPO.h (Pixel Overlay)
- DrawingModeBlend.h
- DrawingModeCopy.h
- DrawingModeErase.h
- DrawingModeInvert.h
- DrawingModeMax.h
- DrawingModeMin.h
- DrawingModeOver.h
- DrawingModeSelect.h
- DrawingModeSubtract.h

**Субпиксельные версии:**
- DrawingMode*SUBPIX.h (15 файлов)

**Solid версии (без паттернов):**
- DrawingMode*Solid*.h (8 файлов)

## Ключевые концепции Blend2D

### Основные компоненты
- **BLContext** - основной объект рендеринга (аналог AGG renderer)
- **BLImage** - целевое изображение
- **BLPath** - векторные пути
- **BLStyle** - стили заливки и обводки (цвета, градиенты, паттерны)

### Композитные операторы
```cpp
enum BLCompOp {
    BL_COMP_OP_SRC_OVER = 0,    // source-over [default]
    BL_COMP_OP_SRC_COPY = 1,    // source-copy
    BL_COMP_OP_SRC_IN = 2,      // source-in
    BL_COMP_OP_SRC_OUT = 3,     // source-out
    BL_COMP_OP_SRC_ATOP = 4,    // source-atop
    BL_COMP_OP_DST_OVER = 5,    // destination-over
    BL_COMP_OP_DST_COPY = 6,    // destination-copy [nop]
    BL_COMP_OP_DST_IN = 7,      // destination-in
    BL_COMP_OP_DST_OUT = 8,     // destination-out
    BL_COMP_OP_DST_ATOP = 9,    // destination-atop
    BL_COMP_OP_XOR = 10,        // xor
    BL_COMP_OP_CLEAR = 11,      // clear
    BL_COMP_OP_PLUS = 12,       // plus
    BL_COMP_OP_MINUS = 13,      // minus
    BL_COMP_OP_MODULATE = 14,   // modulate
    BL_COMP_OP_MULTIPLY = 15,   // multiply
    BL_COMP_OP_SCREEN = 16,     // screen
    BL_COMP_OP_OVERLAY = 17,    // overlay
    BL_COMP_OP_DARKEN = 18,     // darken
    BL_COMP_OP_LIGHTEN = 19     // lighten
    // ... другие blend modes
};
```

### Рендеринг API
```cpp
// Создание контекста
BLImage image(width, height, BL_FORMAT_PRGB32);
BLContext ctx(image);

// Установка композитного оператора
ctx.setCompOp(BL_COMP_OP_SRC_OVER);

// Заливка прямоугольника
ctx.fillRect(x, y, w, h, BLRgba32(color));

// Отрисовка с паттерном
BLPattern pattern;
ctx.setFillStyle(pattern);
ctx.fillRect(x, y, w, h);
```

## Таблица соответствия AGG ↔ Blend2D

### Композитные операторы

| Haiku Drawing Mode | AGG | Blend2D | Примечания |
|-------------------|-----|---------|------------|
| B_OP_COPY | agg::comp_op_src | BL_COMP_OP_SRC_COPY | Прямое соответствие |
| B_OP_OVER | agg::comp_op_src_over | BL_COMP_OP_SRC_OVER | Прямое соответствие |
| B_OP_ERASE | agg::comp_op_dst_out | BL_COMP_OP_DST_OUT | Требует логику паттерна |
| B_OP_INVERT | Кастомная реализация | BL_COMP_OP_XOR + логика | Требует адаптацию |
| B_OP_ADD | Кастомная реализация | BL_COMP_OP_PLUS | Прямое соответствие |
| B_OP_SUBTRACT | Кастомная реализация | BL_COMP_OP_MINUS | Прямое соответствие |
| B_OP_BLEND | Кастомная реализация | BL_COMP_OP_MULTIPLY | Требует логику |
| B_OP_MIN | Кастомная реализация | BL_COMP_OP_DARKEN | Аналогичная логика |
| B_OP_MAX | Кастомная реализация | BL_COMP_OP_LIGHTEN | Аналогичная логика |
| B_OP_SELECT | Кастомная реализация | Кастомная + BL_COMP_OP_XOR | Требует портирование |
| B_OP_ALPHA | agg::comp_op_* | BL_COMP_OP_* | Зависит от alpha mode |

### Alpha режимы

| Haiku Alpha Mode | AGG | Blend2D | Маппинг |
|------------------|-----|---------|---------|
| B_CONSTANT_ALPHA + B_ALPHA_OVERLAY | AGG render + blending | BL_COMP_OP_SRC_OVER + setGlobalAlpha() | Через глобальную прозрачность |
| B_CONSTANT_ALPHA + B_ALPHA_COMPOSITE | comp_op_src_over | BL_COMP_OP_SRC_OVER | Стандартный composite |
| B_PIXEL_ALPHA + B_ALPHA_OVERLAY | Per-pixel alpha | BL_COMP_OP_SRC_OVER | Pixel-based alpha |
| B_PIXEL_ALPHA + B_ALPHA_COMPOSITE | comp_op_src_over | BL_COMP_OP_SRC_OVER | Стандартный composite |
| B_ALPHA_COMPOSITE_* | agg::comp_op_* | BL_COMP_OP_* | Porter-Duff операторы |

### Основные типы данных


 AGG Component | Blend2D Equivalent | Примечания |
|--------------|-------------------|------------|
| `agg::rendering_buffer` | `BLImage` + `BLImageData` | Прямой доступ к пикселям через getData() |
| `agg::pixfmt_rgba32` | `BL_FORMAT_PRGB32` | Premultiplied RGBA |
| `agg::renderer_base` | `BLContext` | Основной контекст рендеринга |
| `agg::rasterizer_scanline_aa` | Встроено в `BLContext` | Автоматический антиалиасинг |
| `agg::path_storage` | `BLPath` | Хранение путей |
| `agg::conv_stroke` | `ctx.strokePath()` | Stroke операции |
| `agg::trans_affine` | `BLMatrix2D` | 2D трансформации |
| `agg::rgba` | `BLRgba32` / `BLRgba64` | Цвета |

## Детальный план по файлам

### 1. Файл: defines.h
**Текущее назначение:** Центральные определения типов для AGG pipeline  
**Анализ зависимостей:** Импортирует все основные AGG типы и создает typedef'ы  
**Стратегия миграции:** Полная замена на Blend2D типы  

**Детальные изменения:**
- **Замена includes:**
  - Удалить все `#include <agg_*>` 
  - Добавить `#include <blend2d.h>`
- **Замена типов:**
  ```cpp
  // Было
  typedef PixelFormat pixfmt;
  typedef agg::renderer_region<pixfmt> renderer_base;
  typedef agg::rasterizer_scanline_aa<> rasterizer_type;
  
  // Станет
  typedef BLContext renderer_base;
  typedef BLImage target_buffer;
  typedef BLPath path_type;
  ```
- **Сохранение совместимости:** Создать wrapper типы для сохранения интерфейса
- **Особые требования:** Критически важен для всех других файлов

### 2. Файл: PixelFormat.h
**Текущее назначение:** AGG pixel format адаптер с функциями блендинга  
**Анализ зависимостей:** agg::rendering_buffer, composition operators, PatternHandler  
**Стратегия миграции:** Создать BLContext wrapper с сохранением методов  

**Детальные изменения:**
- **Замена core типов:**
  ```cpp
  // Было
  typedef agg::rgba8 color_type;
  typedef agg::rendering_buffer agg_buffer;
  
  // Станет 
  typedef BLRgba32 color_type;
  typedef BLImage target_buffer;
  ```
- **Методы класса PixelFormat:**
  - `blend_pixel()` → `ctx.fillRect(x, y, 1, 1, color)`
  - `blend_hline()` → `ctx.fillRect(x, y, len, 1, color)`  
  - `blend_solid_hspan()` → Итерация `ctx.fillRect()` с covers
- **Маппинг composition ops:**
  ```cpp
  void SetDrawingMode(drawing_mode mode, source_alpha alphaSrcMode, alpha_function alphaFncMode) {
      BLCompOp blendOp = mapHaikuModeToBlend2D(mode, alphaSrcMode, alphaFncMode);
      fBlendContext->setCompOp(blendOp);
  }
  ```
- **Сохранение совместимости:** Все публичные методы остаются неизменными
- **Особые требования:** Центральный класс для всех drawing modes

### 3. Файл: PixelFormat.cpp
**Текущее назначение:** Реализация SetDrawingMode с маппингом на функции блендинга  
**Анализ зависимостей:** Все DrawingMode*.h файлы  
**Стратегия миграции:** Замена указателей на функции на BLCompOp установку  

**Детальные изменения:**
- **Замена switch statement:**
  ```cpp
  void PixelFormat::SetDrawingMode(drawing_mode mode, source_alpha alphaSrcMode, alpha_function alphaFncMode) {
      switch (mode) {
          case B_OP_OVER:
              fBlendContext->setCompOp(BL_COMP_OP_SRC_OVER);
              break;
          case B_OP_COPY:
              fBlendContext->setCompOp(BL_COMP_OP_SRC_COPY);
              break;
          // ... остальные режимы
      }
  }
  ```
- **Удаление функциональных указателей:** Заменить на прямые вызовы BLContext
- **Alpha режимы:** Мапить на соответствующие BL_COMP_OP_* + setGlobalAlpha()
- **Сохранение совместимости:** Интерфейс метода остается неизменным

### 4. Файлы: DrawingMode*.h (основные режимы)
**Текущее назначение:** Inline функции для конкретных режимов рисования  
**Анализ зависимостей:** Макросы BLEND*, ASSIGN*, pixel32, agg_buffer  
**Стратегия миграции:** Замена на BLContext вызовы через адаптеры  

**Детальные изменения для каждого файла:**

#### DrawingModeOver.h
- **Заменить функции:**
  ```cpp
  // Было
  void blend_pixel_over(int x, int y, const color_type& c, uint8 cover,
                        agg_buffer* buffer, const PatternHandler* pattern)
  
  // Станет
  void blend_pixel_over(int x, int y, const color_type& c, uint8 cover,
                        BLContext* ctx, const PatternHandler* pattern) {
      ctx->setCompOp(BL_COMP_OP_SRC_OVER);
      ctx->fillRect(x, y, 1, 1, BLRgba32(c.r, c.g, c.b, c.a * cover / 255));
  }
  ```

#### DrawingModeAlphaCC.h
- **Маппинг:** B_ALPHA_COMPOSITE → BL_COMP_OP_SRC_OVER
- **Изменения:** Использовать pattern->HighColor().alpha для глобальной прозрачности

#### DrawingModeAdd.h  
- **Маппинг:** B_OP_ADD → BL_COMP_OP_PLUS
- **Изменения:** Прямая замена макросов на BLContext вызовы

#### DrawingModeBlend.h
- **Маппинг:** B_OP_BLEND → BL_COMP_OP_MULTIPLY (приблизительно)
- **Изменения:** Возможно требует кастомную реализацию

#### DrawingModeInvert.h
- **Маппинг:** B_OP_INVERT → Кастомная логика с BL_COMP_OP_XOR
- **Изменения:** Требует специальную реализацию

### 5. Файлы: DrawingMode*SUBPIX.h
**Текущее назначение:** Субпиксельные версии режимов рисования  
**Анализ зависимостей:** GlobalSubpixelSettings, BLEND_*_SUBPIX макросы  
**Стратегия миграции:** Blend2D не имеет встроенной субпиксельной поддержки  

**Детальные изменения:**
- **Эмуляция субпикселей:** Использовать отдельные BLContext вызовы для R, G, B компонентов
- **Замена макросов SUBPIX:** 
  ```cpp
  // Эмуляция субпиксельного рендеринга
  void blend_solid_hspan_over_subpix(int x, int y, unsigned len,
                                     const color_type& c, const uint8* covers,
                                     BLContext* ctx, const PatternHandler* pattern) {
      for (unsigned i = 0; i < len; i += 3) {
          uint8 r_alpha = covers[gSubpixelOrderingRGB ? i+2 : i];  
          uint8 g_alpha = covers[i+1];
          uint8 b_alpha = covers[gSubpixelOrderingRGB ? i : i+2];
          
          // Отдельные операции для каждого субпикселя
          // Требует дополнительную логику композитинга
      }
  }
  ```
- **Особые требования:** Самая сложная часть миграции, может требовать fallback на CPU рендеринг

### 6. Файлы: DrawingMode*Solid*.h  
**Текущее назначение:** Оптимизированные версии для сплошных цветов  
**Анализ зависимостей:** Соответствующие основные DrawingMode файлы  
**Стратегия миграции:** Использовать BLRgba32 вместо паттернов  

**Детальные изменения:**
- **Упрощение:** Убрать pattern параметр, использовать color напрямую
- **Оптимизация:** Blend2D автоматически оптимизирует сплошные цвета
- **Совместимость:** Сохранить сигнатуры функций

### 7. Файл: AggCompOpAdapter.h
**Текущее назначение:** Адаптер для AGG composition операторов  
**Анализ зависимостей:** agg::comp_op_*, PatternHandler  
**Стратегия миграции:** Полная замена на BLContext wrapper  

**Детальные изменения:**
- **Удаление template класса AggCompOpAdapter**
- **Создание BlendCompOpAdapter:**
  ```cpp
  template<BLCompOp CompOp>
  struct BlendCompOpAdapter {
      static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
                             BLContext* ctx, const PatternHandler* pattern) {
          ctx->setCompOp(CompOp);
          RGB_color color = pattern->ColorAt(x, y);
          ctx->fillRect(x, y, 1, 1, BLRgba32(color.red, color.green, color.blue, cover));
      }
      // ... остальные методы
  };
  ```

### 8. Файл: PatternHandler.h/cpp  
**Текущее назначение:** Обработка паттернов рисования Haiku  
**Анализ зависимостей:** Независим от AGG, использует только rgb_color  
**Стратегия миграции:** Минимальные изменения  

**Детальные изменения:**
- **Сохранение класса:** PatternHandler остается практически неизменным  
- **Добавление Blend2D интеграции:**
  ```cpp
  class PatternHandler {
      // Существующий код остается
      
      // Новые методы для Blend2D
      BLPattern CreateBlend2DPattern() const;
      BLGradient CreateBlend2DGradient() const;
  };
  ```
- **Сохранение совместимости:** 100% обратная совместимость

### 9. Файл: GlobalSubpixelSettings.h/cpp
**Текущее назначение:** Глобальные настройки субпиксельного рендеринга  
**Анализ зависимостей:** Независим от AGG  
**Стратегия миграции:** Без изменений  

**Детальные изменения:**
- **Сохранение без изменений:** Файл не требует модификации
- **Использование:** Будет использоваться кастомной реализацией субпикселей

### 10. Файл: drawing_support.h
**Текущее назначение:** Вспомогательные функции для рисования  
**Анализ зависимостей:** pixel32 union, функции блендинга  
**Стратегия миграции:** Адаптация под BLRgba32  

**Детальные изменения:**
- **Замена pixel32 union:**
  ```cpp
  // Было
  union pixel32 {
      uint32 data32;
      uint8  data8[4];
  };
  
  // Станет (или адаптер)
  union pixel32 {
      BLRgba32 bl_color;
      uint32   data32; 
      uint8    data8[4];
  };
  ```
- **Функция blend_line32:** Заменить на BLContext::fillRect() с градиентом

## Порядок выполнения

### Фаза 1: Подготовительная (критическая инфраструктура)
1. **defines.h** - Создать Blend2D typedef'ы и wrapper'ы
2. **GlobalSubpixelSettings.h/cpp** - Проверить совместимость (без изменений)
3. **drawing_support.h** - Адаптировать вспомогательные типы
4. **PatternHandler.h/cpp** - Добавить Blend2D методы

### Фаза 2: Основная архитектура  
5. **PixelFormat.h** - Создать BLContext wrapper класс
6. **AggCompOpAdapter.h** - Заменить на BlendCompOpAdapter
7. **PixelFormat.cpp** - Реализовать маппинг режимов на BL_COMP_OP

### Фаза 3: Основные режимы рисования
8. **DrawingModeOver.h** - Базовый режим, тестирование архитектуры  
9. **DrawingModeCopy.h** - Простой режим
10. **DrawingModeAdd.h** - Прямой маппинг на BL_COMP_OP_PLUS
11. **DrawingModeSubtract.h** - Прямой маппинг на BL_COMP_OP_MINUS
12. **DrawingModeBlend.h** - Кастомная логика
13. **DrawingModeMin.h** - Маппинг на BL_COMP_OP_DARKEN
14. **DrawingModeMax.h** - Маппинг на BL_COMP_OP_LIGHTEN

### Фаза 4: Alpha режимы
15. **DrawingModeAlphaCC.h** - Constant Composite  
16. **DrawingModeAlphaCO.h** - Constant Overlay
17. **DrawingModeAlphaPC.h** - Pixel Composite
18. **DrawingModeAlphaPO.h** - Pixel Overlay

### Фаза 5: Специальные режимы
19. **DrawingModeErase.h** - Кастомная логика с паттернами
20. **DrawingModeInvert.h** - Кастомная логика с XOR
21. **DrawingModeSelect.h** - Самый сложный режим

### Фаза 6: Solid оптимизации  
22-29. **DrawingMode*Solid*.h** - Оптимизированные версии

### Фаза 7: Субпиксельный рендеринг (наиболее сложная)
30-44. **DrawingMode*SUBPIX.h** - Кастомная эмуляция субпикселей

## Стратегия тестирования

### Unit тесты
- **Pixel-level тестирование:** Сравнение результатов AGG vs Blend2D для каждого режима
- **Pattern тестирование:** Проверка корректности паттернов и градиентов  
- **Alpha compositing:** Верификация всех alpha режимов

### Integration тесты  
- **Haiku UI рендеринг:** Тестирование на реальном UI Haiku
- **Performance тесты:** Сравнение производительности AGG vs Blend2D
- **Memory usage:** Контроль потребления памяти

### Regression тесты
- **Screenshot comparison:** Пиксель-в-пиксель сравнение с эталонными изображениями
- **API compatibility:** Проверка, что весь публичный API работает идентично

## Потенциальные риски и решения

### Высокие риски
1. **Субпиксельный рендеринг** 
   - Риск: Blend2D не поддерживает субпиксели нативно
   - Решение: Кастомная реализация через отдельные R,G,B рендеринги или fallback на CPU

2. **Режим B_OP_SELECT**
   - Риск: Очень специфичная логика Haiku
   - Решение: Полное портирование логики на Blend2D примитивы

3. **Performance регрессия**  
   - Риск: Blend2D может быть медленнее AGG на некоторых операциях
   - Решение: Профилирование и оптимизация, возможно selective fallback

### Средние риски  
4. **Pattern handling различия**
   - Риск: Разная логика обработки паттернов
   - Решение: Адаптер слой в PatternHandler

5. **Memory layout изменения**
   - Риск: Разный формат пикселей внутри
   - Решение: Конверсионные функции

### Низкие риски
6. **API изменения Blend2D**
   - Риск: Blend2D еще в beta
   - Решение: Заморозить версию Blend2D, wrapper слой

## Критерии успеха

1. ✅ **100% API совместимость** - все существующие Haiku приложения работают без изменений
2. ✅ **Pixel-perfect рендеринг** - идентичные визуальные результаты для всех режимов  
3. ✅ **Performance паритет** - не хуже производительности AGG
4. ✅ **Субпиксельная поддержка** - корректная работа текстового рендеринга
5. ✅ **Стабильность** - отсутствие регрессий в existing functionality

## Временная оценка
- **Фаза 1-2 (инфраструктура):** 2-3 недели
- **Фаза 3-4 (основные режимы):** 3-4 недели  
- **Фаза 5 (специальные режимы):** 2-3 недели
- **Фаза 6 (solid оптимизации):** 1-2 недели
- **Фаза 7 (субпиксели):** 4-6 недель
- **Тестирование и отладка:** 3-4 недели

**Общая оценка: 15-22 недели**

Наиболее критичными и трудозатратными являются субпиксельный рендеринг и специальные режимы B_OP_SELECT, B_OP_INVERT, требующие кастомной реализации поверх Blend2D.
