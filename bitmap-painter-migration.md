# Инструкция: Портирование BitmapPainter с AGG на Blend2D

## КОНТЕКСТ

Мы переносим графический рендеринг Haiku OS с библиотеки AGG (Anti-Grain Geometry) на Blend2D. 
Текущая задача: портировать систему рисования bitmap-изображений.

**Расположение файлов:**
- `src/servers/app/drawing/Painter/bitmap_painter/BitmapPainter.cpp`
- `src/servers/app/drawing/Painter/bitmap_painter/BitmapPainter.h`
- `src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapBilinear.h`
- `src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapNearestNeighbor.h`
- `src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapNoScale.h`
- `src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapGeneric.h`
- `src/servers/app/drawing/Painter/bitmap_painter/painter_bilinear_scale.nasm`

## ПРИНЦИПИАЛЬНОЕ ОТЛИЧИЕ

**AGG подход:**
- Ручная реализация всех оптимизаций
- Специализированные шаблоны для каждого случая (bilinear, nearest neighbor, no scale)
- Ручной SIMD код (MMX/SSE) в ассемблере
- ~2000+ строк сложного кода

**Blend2D подход:**
- JIT-компиляция пайплайнов во время выполнения
- Автоматический выбор оптимального пути
- Автоматическая SIMD оптимизация (SSE2 → AVX-512)
- ~200 строк простого кода

## ШАГ 1: УДАЛИТЬ ФАЙЛЫ

Эти файлы больше не нужны, так как Blend2D делает всё автоматически:

```bash
# Удалить следующие файлы:
rm src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapBilinear.h
rm src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapNearestNeighbor.h
rm src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapNoScale.h
rm src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapGeneric.h
rm src/servers/app/drawing/Painter/bitmap_painter/painter_bilinear_scale.nasm
```

**Причина:** Blend2D автоматически генерирует оптимизированный код через JIT для всех этих случаев.

## ШАГ 2: ОБНОВИТЬ BitmapPainter.h

**Заменить includes в начале файла:**

```cpp
// УДАЛИТЬ эти includes:
// #include <agg_image_accessors.h>
// #include <agg_pixfmt_rgba.h>
// #include <agg_span_image_filter_rgba.h>

// ДОБАВИТЬ:
#include <blend2d.h>
```

**Обновить определение класса:**

```cpp
class Painter::BitmapPainter {
public:
    BitmapPainter(const Painter* painter,
                  const ServerBitmap* bitmap,
                  uint32 options);

    void Draw(const BRect& sourceRect,
              const BRect& destinationRect);

private:
    bool _DetermineTransform(BRect sourceRect,
                             const BRect& destinationRect);
    
    BLFormat _ConvertToBLFormat(color_space cs);
    void _ConvertColorSpace(BLImage& outImage);
    void _SetupCompOp(BLContext& ctx);
    
    template<typename sourcePixel>
    void _TransparentMagicToAlpha(sourcePixel* buffer,
                                  uint32 width, uint32 height,
                                  uint32 sourceBytesPerRow,
                                  sourcePixel transparentMagic,
                                  BBitmap* output);

private:
    const Painter* fPainter;
    status_t fStatus;
    BLImage fBLImage;          // ИЗМЕНЕНО: было agg::rendering_buffer
    BRect fBitmapBounds;
    color_space fColorSpace;
    uint32 fOptions;

    BRect fDestinationRect;
    double fScaleX;
    double fScaleY;
    BPoint fOffset;
};
```

## ШАГ 3: ПЕРЕПИСАТЬ BitmapPainter.cpp

### 3.1 Конструктор

```cpp
Painter::BitmapPainter::BitmapPainter(const Painter* painter,
    const ServerBitmap* bitmap, uint32 options)
    :
    fPainter(painter),
    fStatus(B_NO_INIT),
    fOptions(options)
{
    if (bitmap == NULL || !bitmap->IsValid())
        return;

    fBitmapBounds = bitmap->Bounds();
    fBitmapBounds.OffsetBy(-fBitmapBounds.left, -fBitmapBounds.top);
    fColorSpace = bitmap->ColorSpace();

    // Создаём BLImage из существующих данных bitmap
    // BL_DATA_ACCESS_READ означает read-only, Blend2D создаст копию при модификации
    BLResult result = fBLImage.createFromData(
        bitmap->Width(),
        bitmap->Height(),
        _ConvertToBLFormat(fColorSpace),
        (void*)bitmap->Bits(),
        bitmap->BytesPerRow(),
        BL_DATA_ACCESS_READ,  // read-only access
        nullptr,              // destroyFunc - не нужна, данные принадлежат ServerBitmap
        nullptr               // userData
    );

    if (result == BL_SUCCESS)
        fStatus = B_OK;
}
```

### 3.2 Главный метод Draw()

```cpp
void Painter::BitmapPainter::Draw(const BRect& sourceRect,
    const BRect& destinationRect)
{
    if (fStatus != B_OK)
        return;

    TRACE("BitmapPainter::Draw()\n");
    TRACE("   bitmapBounds = (%.1f, %.1f) - (%.1f, %.1f)\n",
        fBitmapBounds.left, fBitmapBounds.top,
        fBitmapBounds.right, fBitmapBounds.bottom);
    TRACE("   sourceRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
        sourceRect.left, sourceRect.top,
        sourceRect.right, sourceRect.bottom);
    TRACE("   destinationRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
        destinationRect.left, destinationRect.top,
        destinationRect.right, destinationRect.bottom);

    bool success = _DetermineTransform(sourceRect, destinationRect);
    if (!success)
        return;

    // Конвертация цветового пространства если требуется
    BLImage workingImage;
    if (fColorSpace != B_RGBA32 && fColorSpace != B_RGB32) {
        _ConvertColorSpace(workingImage);
    } else {
        workingImage = fBLImage;  // shallow copy (увеличивает reference count)
    }

    // Получаем контекст Blend2D из Painter
    BLContext& ctx = fPainter->fInternal.fBLContext;

    // КРИТИЧНО: Настройка качества фильтрации изображений
    // В Blend2D это делается через context hints
    if ((fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
        // Bilinear фильтрация для качественного масштабирования
        ctx.setHint(BL_CONTEXT_HINT_RENDERING_QUALITY, 
                    BL_RENDERING_QUALITY_ANTIALIAS);
        ctx.setHint(BL_CONTEXT_HINT_PATTERN_QUALITY,
                    BL_PATTERN_QUALITY_BILINEAR);
    } else {
        // Nearest neighbor для пиксельной графики (без сглаживания)
        ctx.setHint(BL_CONTEXT_HINT_PATTERN_QUALITY,
                    BL_PATTERN_QUALITY_NEAREST);
    }

    // Настройка composition operator (SRC_COPY, SRC_OVER, и т.д.)
    _SetupCompOp(ctx);

    if ((fOptions & B_TILE_BITMAP) != 0) {
        // ===== TILING РЕЖИМ =====
        // Используем BLPattern для повторения изображения
        BLPattern pattern(workingImage);
        
        // Установка repeat mode для бесшовного tiling
        pattern.setExtendMode(BL_EXTEND_MODE_REPEAT);
        
        // Применяем трансформацию к pattern (масштаб и смещение)
        BLMatrix2D matrix;
        matrix.translate(fOffset.x, fOffset.y);
        if (fScaleX != 1.0 || fScaleY != 1.0) {
            matrix.scale(fScaleX, fScaleY);
        }
        pattern.setMatrix(matrix);
        
        // Заполняем прямоугольник паттерном
        ctx.fillRect(BLRect(
            fDestinationRect.left,
            fDestinationRect.top,
            fDestinationRect.Width() + 1,
            fDestinationRect.Height() + 1
        ), pattern);
        
    } else {
        // ===== ОБЫЧНЫЙ РЕЖИМ (NON-TILING) =====
        // Используем blitImage для рисования (с автоматическим масштабированием)
        
        BLRectI srcArea(
            (int)sourceRect.left,
            (int)sourceRect.top,
            (int)(sourceRect.Width() + 1),
            (int)(sourceRect.Height() + 1)
        );
        
        BLRect dstRect(
            fDestinationRect.left,
            fDestinationRect.top,
            fDestinationRect.Width() + 1,
            fDestinationRect.Height() + 1
        );
        
        // blitImage автоматически выполняет масштабирование если srcArea != dstRect
        // Качество фильтрации определяется установленными выше hints
        BLResult result = ctx.blitImage(dstRect, workingImage, srcArea);
        
        if (result != BL_SUCCESS) {
            fprintf(stderr, "BitmapPainter::Draw() - blitImage failed: %d\n", 
                    (int)result);
        }
    }
}
```

### 3.3 Метод _ConvertToBLFormat

```cpp
BLFormat Painter::BitmapPainter::_ConvertToBLFormat(color_space cs)
{
    switch (cs) {
        case B_RGBA32:
        case B_RGB32:
            // Blend2D использует premultiplied alpha по умолчанию
            return BL_FORMAT_PRGB32;
        
        case B_RGB24:
            // 24-bit RGB без alpha
            // Blend2D конвертирует в PRGB32 автоматически
            return BL_FORMAT_XRGB32;
        
        case B_CMAP8:
            // 8-bit indexed color - нужна конвертация через палитру
            // Конвертируем в PRGB32
            return BL_FORMAT_PRGB32;
        
        case B_RGB15:
        case B_RGBA15:
            // 15/16-bit форматы - конвертируем в PRGB32
            return BL_FORMAT_PRGB32;
        
        default:
            // По умолчанию используем PRGB32
            return BL_FORMAT_PRGB32;
    }
}
```

### 3.4 Метод _SetupCompOp

```cpp
void Painter::BitmapPainter::_SetupCompOp(BLContext& ctx)
{
    switch (fPainter->fDrawingMode) {
        case B_OP_COPY:
            // Простое копирование (замена пикселей)
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            break;
        
        case B_OP_OVER:
            // Alpha blending (наложение с учетом прозрачности)
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            break;
        
        case B_OP_ALPHA:
            if (fPainter->fAlphaSrcMode == B_PIXEL_ALPHA &&
                fPainter->fAlphaFncMode == B_ALPHA_OVERLAY) {
                // Pixel alpha overlay mode
                ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            } else {
                // Другие alpha режимы - использовать SRC_OVER как fallback
                ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            }
            break;
        
        default:
            // По умолчанию - standard alpha blending
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            break;
    }
    
    // Установка глобальной alpha если нужно
    if (fPainter->fAlphaSrcMode != B_PIXEL_ALPHA) {
        // Константная alpha
        double alpha = fPainter->fPatternHandler.GetAlpha() / 255.0;
        ctx.setGlobalAlpha(alpha);
    }
}
```

### 3.5 Метод _ConvertColorSpace (упрощенная версия)

```cpp
void Painter::BitmapPainter::_ConvertColorSpace(BLImage& outImage)
{
    // Для форматов, требующих конвертации
    if (fColorSpace == B_RGBA32 || fColorSpace == B_RGB32) {
        outImage = fBLImage;
        return;
    }

    // Создаём временный bitmap для конвертации
    BBitmap* conversionBitmap = new(std::nothrow) BBitmap(
        fBitmapBounds,
        B_BITMAP_NO_SERVER_LINK,
        B_RGBA32
    );
    
    if (conversionBitmap == NULL) {
        fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
            "out of memory\n");
        outImage = fBLImage;
        return;
    }

    // Конвертация через ImportBits
    BLImageData srcData;
    fBLImage.getData(&srcData);
    
    status_t err = conversionBitmap->ImportBits(
        srcData.pixelData,
        srcData.size.h * srcData.stride,
        srcData.stride,
        0,
        fColorSpace
    );
    
    if (err < B_OK) {
        fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
            "conversion failed: %s\n", strerror(err));
        delete conversionBitmap;
        outImage = fBLImage;
        return;
    }

    // Обработка transparent magic colors
    switch (fColorSpace) {
        case B_RGB32: {
            uint32* bits = (uint32*)conversionBitmap->Bits();
            _TransparentMagicToAlpha(bits,
                conversionBitmap->Bounds().IntegerWidth() + 1,
                conversionBitmap->Bounds().IntegerHeight() + 1,
                conversionBitmap->BytesPerRow(),
                B_TRANSPARENT_MAGIC_RGBA32,
                conversionBitmap);
            break;
        }
        case B_RGB15: {
            uint16* bits = (uint16*)srcData.pixelData;
            _TransparentMagicToAlpha(bits,
                srcData.size.w,
                srcData.size.h,
                srcData.stride,
                B_TRANSPARENT_MAGIC_RGBA15,
                conversionBitmap);
            break;
        }
        default:
            break;
    }

    // Создаём BLImage из конвертированного bitmap
    BLResult result = outImage.createFromData(
        conversionBitmap->Bounds().IntegerWidth() + 1,
        conversionBitmap->Bounds().IntegerHeight() + 1,
        BL_FORMAT_PRGB32,
        conversionBitmap->Bits(),
        conversionBitmap->BytesPerRow(),
        BL_DATA_ACCESS_READ
    );
    
    delete conversionBitmap;
    
    if (result != BL_SUCCESS) {
        fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
            "BLImage creation failed\n");
        outImage = fBLImage;
    }
}
```

### 3.6 Метод _DetermineTransform (без изменений)

```cpp
// Этот метод остаётся БЕЗ ИЗМЕНЕНИЙ - он вычисляет параметры трансформации
bool Painter::BitmapPainter::_DetermineTransform(BRect sourceRect,
    const BRect& destinationRect)
{
    if (!fPainter->fValidClipping
        || !sourceRect.IsValid()
        || ((fOptions & B_TILE_BITMAP) == 0
            && !sourceRect.Intersects(fBitmapBounds))
        || !destinationRect.IsValid()) {
        return false;
    }

    fDestinationRect = destinationRect;

    if (!fPainter->fSubpixelPrecise) {
        align_rect_to_pixels(&sourceRect);
        align_rect_to_pixels(&fDestinationRect);
    }

    if ((fOptions & B_TILE_BITMAP) == 0) {
        fScaleX = (fDestinationRect.Width() + 1) / (sourceRect.Width() + 1);
        fScaleY = (fDestinationRect.Height() + 1) / (sourceRect.Height() + 1);

        if (fScaleX == 0.0 || fScaleY == 0.0)
            return false;

        // Constrain source rect to bitmap bounds
        if (sourceRect.left < fBitmapBounds.left) {
            float diff = fBitmapBounds.left - sourceRect.left;
            fDestinationRect.left += diff * fScaleX;
            sourceRect.left = fBitmapBounds.left;
        }
        if (sourceRect.top < fBitmapBounds.top) {
            float diff = fBitmapBounds.top - sourceRect.top;
            fDestinationRect.top += diff * fScaleY;
            sourceRect.top = fBitmapBounds.top;
        }
        if (sourceRect.right > fBitmapBounds.right) {
            float diff = sourceRect.right - fBitmapBounds.right;
            fDestinationRect.right -= diff * fScaleX;
            sourceRect.right = fBitmapBounds.right;
        }
        if (sourceRect.bottom > fBitmapBounds.bottom) {
            float diff = sourceRect.bottom - fBitmapBounds.bottom;
            fDestinationRect.bottom -= diff * fScaleY;
            sourceRect.bottom = fBitmapBounds.bottom;
        }
    } else {
        fScaleX = 1.0;
        fScaleY = 1.0;
    }

    fOffset.x = fDestinationRect.left - sourceRect.left;
    fOffset.y = fDestinationRect.top - sourceRect.top;

    return true;
}
```

### 3.7 Метод _TransparentMagicToAlpha (без изменений)

```cpp
// Этот метод остаётся БЕЗ ИЗМЕНЕНИЙ
template<typename sourcePixel>
void Painter::BitmapPainter::_TransparentMagicToAlpha(sourcePixel* buffer,
    uint32 width, uint32 height, uint32 sourceBytesPerRow,
    sourcePixel transparentMagic, BBitmap* output)
{
    uint8* sourceRow = (uint8*)buffer;
    uint8* destRow = (uint8*)output->Bits();
    uint32 destBytesPerRow = output->BytesPerRow();

    for (uint32 y = 0; y < height; y++) {
        sourcePixel* pixel = (sourcePixel*)sourceRow;
        uint32* destPixel = (uint32*)destRow;
        for (uint32 x = 0; x < width; x++, pixel++, destPixel++) {
            if (*pixel == transparentMagic)
                *destPixel &= 0x00ffffff;
        }

        sourceRow += sourceBytesPerRow;
        destRow += destBytesPerRow;
    }
}
```

## ШАГ 4: ОБНОВИТЬ INCLUDES В ДРУГИХ ФАЙЛАХ

В файлах, которые используют BitmapPainter, удалить includes на удалённые файлы:

```cpp
// УДАЛИТЬ:
// #include "DrawBitmapBilinear.h"
// #include "DrawBitmapGeneric.h"
// #include "DrawBitmapNearestNeighbor.h"
// #include "DrawBitmapNoScale.h"

// Оставить только:
#include "BitmapPainter.h"
```

## ШАГ 5: ОБНОВИТЬ BUILD-СИСТЕМУ

В соответствующем Jamfile или Makefile:

```jam
# УДАЛИТЬ из списка исходников:
# DrawBitmapBilinear.h
# DrawBitmapNearestNeighbor.h
# DrawBitmapNoScale.h
# DrawBitmapGeneric.h
# painter_bilinear_scale.nasm (или .o файл)

# Оставить только:
BitmapPainter.cpp
```

## КРИТИЧЕСКИЕ МОМЕНТЫ

### ⚠️ ВАЖНО: Качество фильтрации

**Правильно:**
```cpp
// Устанавливать hints ПЕРЕД вызовом blitImage/fillRect
ctx.setHint(BL_CONTEXT_HINT_PATTERN_QUALITY, BL_PATTERN_QUALITY_BILINEAR);
ctx.blitImage(...);
```

**Неправильно:**
```cpp
// НЕ СУЩЕСТВУЕТ в Blend2D:
ctx.setImageQuality(...)  // ❌ Такого метода НЕТ
```

### ⚠️ ВАЖНО: Координаты

Blend2D использует **inclusive** координаты по-другому:
- `BLRectI(x, y, w, h)` - это (x, y, ширина, высота)
- `BLRect(x, y, w, h)` - это (x, y, ширина, высота)

НЕ путать с `BRect(left, top, right, bottom)` в Haiku!

### ⚠️ ВАЖНО: Reference counting

```cpp
BLImage img1(bitmap);
BLImage img2 = img1;  // Shallow copy - увеличивает ref count
// Обе переменные указывают на одни данные
// Автоматически освобождается при выходе из scope
```

### ⚠️ ВАЖНО: Tiling vs Non-tiling

- **Tiling:** Использовать `BLPattern` с `BL_EXTEND_MODE_REPEAT`
- **Non-tiling:** Использовать `ctx.blitImage()`

НЕ использовать pattern для non-tiling случаев!

## ТЕСТИРОВАНИЕ

### Тест 1: Простое копирование без масштабирования
```cpp
// Должно работать быстро и точно
painter->DrawBitmap(bitmap, BRect(0,0,100,100), BRect(0,0,100,100));
```

### Тест 2: Масштабирование с bilinear
```cpp
// Должно быть гладкое масштабирование
painter->SetDrawingMode(B_OP_OVER);
painter->DrawBitmap(bitmap, BRect(0,0,100,100), BRect(0,0,200,200), 
                    B_FILTER_BITMAP_BILINEAR);
```

### Тест 3: Nearest neighbor для пиксельной графики
```cpp
// Должны быть четкие пиксели без размытия
painter->DrawBitmap(bitmap, BRect(0,0,16,16), BRect(0,0,64,64), 0);
```

### Тест 4: Tiling
```cpp
// Должен повторяться без швов
painter->DrawBitmap(bitmap, sourceRect, destinationRect, 
                    B_TILE_BITMAP | B_FILTER_BITMAP_BILINEAR);
```

## ПРОВЕРКА ПРОИЗВОДИТЕЛЬНОСТИ

После миграции проверить:
1. **Скорость:** Blend2D должен быть быстрее или сопоставимо с AGG
2. **Качество:** Визуально идентичное качество рендеринга
3. **Память:** Отсутствие утечек памяти (valgrind/asan)

## ЧАСТО ВСТРЕЧАЮЩИЕСЯ ОШИБКИ

### ❌ Ошибка: "Unknown method setImageQuality"
**Причина:** Такого метода нет в Blend2D  
**Решение:** Использовать `setHint(BL_CONTEXT_HINT_PATTERN_QUALITY, ...)`

### ❌ Ошибка: Размытое изображение при nearest neighbor
**Причина:** Не установлен hint перед рисованием  
**Решение:** Установить `BL_PATTERN_QUALITY_NEAREST` перед `blitImage()`

### ❌ Ошибка: Швы при tiling
**Причина:** Используется `blitImage` вместо pattern  
**Решение:** Использовать `BLPattern` с `BL_EXTEND_MODE_REPEAT`

### ❌ Ошибка: Неправильные координаты
**Причина:** Путаница между `BRect(left,top,right,bottom)` и `BLRect(x,y,w,h)`  
**Решение:** Конвертировать: `BLRect(r.left, r.top, r.Width()+1, r.Height()+1)`

## ИТОГОВАЯ СТАТИСТИКА

**Было:**
- 7 файлов
- ~2000+ строк кода
- Ручные SIMD оптимизации
- Сложная система шаблонов

**Стало:**
- 2 файла (BitmapPainter.cpp/h)
- ~400 строк кода
- Автоматические JIT оптимизации
- Простой и понятный код

**Выигрыш:**
- Уменьшение кода на 80%
- Проще поддержка
- Лучшая производительность на современных CPU
- Автоматическая поддержка новых SIMD инструкций
