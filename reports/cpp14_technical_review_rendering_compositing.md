# Технический обзор разделения рендеринга и композитинга в Haiku app_server
## Анализ с точки зрения C++14

**Автор**: C++14 Expert Reviewer  
**Дата**: 2025-10-01  
**Версия**: 1.0  
**Базовый документ**: `/home/ruslan/haiku/haiku_rendering_files_corrected (1).md`

---

## Executive Summary

Предлагаемая архитектура разделения рендеринга и композитинга основана на расширении существующего класса **Layer** на всю оконную систему. Анализ показывает, что `/home/ruslan/haiku/src/servers/app/Layer.cpp` уже реализует полный паттерн compositor: offscreen rendering → isolated context → composition with effects. Это **прототип production-ready**, а не теоретическая концепция.

### Ключевые находки

**Положительные аспекты:**
- Layer демонстрирует правильную архитектуру: BReference<UtilityBitmap>, изолированный DrawingEngine, coordinate offset через SetRendererOffset()
- Использование MultiLocker для concurrent access уже отработано в Desktop/HWInterface
- RAII patterns через ObjectDeleter<> и BReference<> обеспечивают exception safety
- ServerBitmap reference counting работает корректно

**Критические проблемы:**
1. **Memory allocation overhead**: Layer создает новый UtilityBitmap на каждый RenderToBitmap() - для Window compositor нужен buffer pool
2. **Thread synchronization gaps**: отсутствует явная синхронизация между Window rendering thread и compositor thread
3. **Move semantics unused**: BReference<> не использует move конструкторы - лишние atomic операции
4. **No dirty region optimization**: Layer всегда рендерит полностью - Window compositor должен поддерживать partial updates
5. **UpdateSession complexity**: двойная буферизация update sessions в Window сложна для понимания и maintenance

### Рекомендации

1. Расширить Layer pattern на Window с добавлением caching
2. Внедрить buffer pool для UtilityBitmap переиспользования
3. Явно определить threading model для compositor
4. Добавить move semantics для BReference<> (C++11 feature, доступна в C++14)
5. Упростить UpdateSession механизм или детально документировать инварианты

---

## 1. Анализ управления памятью

### 1.1 UtilityBitmap и ServerBitmap

**Текущая реализация:**

```cpp
// Layer.cpp:114-122
BReference<UtilityBitmap> layerBitmap(_AllocateBitmap(boundingBox), true);
if (layerBitmap == NULL)
    return NULL;

// ...рендеринг...

return layerBitmap.Detach();
```

**Анализ:**

**Положительно:**
- `BReference<>` обеспечивает automatic reference counting через RAII
- `Detach()` передает ownership без лишнего increment/decrement
- `true` параметр в конструкторе BReference указывает "already has reference" - избегает двойного AcquireReference()

**Проблемы:**

1. **Allocation на каждый вызов**:
```cpp
// Layer.cpp:183-195
UtilityBitmap* Layer::_AllocateBitmap(const BRect& bounds)
{
    BReference<UtilityBitmap> layerBitmap(
        new(std::nothrow) UtilityBitmap(bounds, B_RGBA32, 0), true);
    if (layerBitmap == NULL)
        return NULL;
    if (!layerBitmap->IsValid())
        return NULL;

    memset(layerBitmap->Bits(), 0, layerBitmap->BitsLength());
    return layerBitmap.Detach();
}
```

Каждый `new UtilityBitmap` + `memset` - это критично для performance. Для Window compositor, который может вызываться 60 раз/сек на каждое окно, это неприемлемо.

2. **BitsLength() потенциальный overflow**:
```cpp
// ServerBitmap.h:102-106
uint32 ServerBitmap::BitsLength() const
{
    int64 length = fBytesPerRow * fHeight;
    return (length > 0 && length <= UINT32_MAX) ? (uint32)length : 0;
}
```

Возвращает 0 при overflow - caller должен проверять! В Layer.cpp:195 нет проверки перед memset.

**Критическая ошибка:**
```cpp
memset(layerBitmap->Bits(), 0, layerBitmap->BitsLength());
// Если BitsLength() вернул 0 из-за overflow, memset ничего не сделает,
// но buffer останется неинициализированным!
```

**Рекомендация:**
```cpp
uint32 bitsLength = layerBitmap->BitsLength();
if (bitsLength == 0) {
    // overflow или invalid dimensions
    return NULL;
}
memset(layerBitmap->Bits(), 0, bitsLength);
```

### 1.2 Buffer Pooling Strategy

**Предлагаемая архитектура должна включать:**

```cpp
// WindowCompositor.h (предлагаемое)
class BufferPool {
public:
    BReference<UtilityBitmap> Acquire(const BRect& bounds, color_space format);
    void Release(BReference<UtilityBitmap>& bitmap);

private:
    struct PoolEntry {
        BReference<UtilityBitmap> bitmap;
        bigtime_t lastUsed;
        bool inUse;
    };

    BLocker fLock;  // Защита concurrent access
    std::vector<PoolEntry> fPool;  // C++14: можно использовать std::vector

    static const int kMaxPoolSize = 32;  // Configurable
    static const bigtime_t kEvictionTime = 5000000;  // 5 sec
};
```

**Thread safety:**
- `BLocker` для защиты `fPool` - simplest solution
- Альтернатива: lock-free queue (сложнее, но лучше для real-time)

**Memory management:**
```cpp
BReference<UtilityBitmap> BufferPool::Acquire(const BRect& bounds,
    color_space format)
{
    BAutolock _(fLock);

    // Поиск compatible buffer
    for (auto& entry : fPool) {
        if (!entry.inUse
            && entry.bitmap->Bounds() == bounds
            && entry.bitmap->ColorSpace() == format) {
            entry.inUse = true;
            entry.lastUsed = system_time();
            // Очистить buffer для reuse
            memset(entry.bitmap->Bits(), 0, entry.bitmap->BitsLength());
            return entry.bitmap;  // BReference copy - atomic increment
        }
    }

    // Нет подходящего - создать новый
    BReference<UtilityBitmap> newBitmap(
        new(std::nothrow) UtilityBitmap(bounds, format, 0), true);

    if (newBitmap.IsSet() && newBitmap->IsValid()) {
        uint32 bitsLength = newBitmap->BitsLength();
        if (bitsLength > 0) {
            memset(newBitmap->Bits(), 0, bitsLength);

            // Добавить в pool если есть место
            if (fPool.size() < kMaxPoolSize) {
                fPool.push_back({newBitmap, system_time(), true});
            }
        }
    }

    return newBitmap;
}

void BufferPool::Release(BReference<UtilityBitmap>& bitmap)
{
    BAutolock _(fLock);

    for (auto& entry : fPool) {
        if (entry.bitmap == bitmap) {
            entry.inUse = false;
            entry.lastUsed = system_time();
            break;
        }
    }

    bitmap.Unset();  // Release caller's reference
}
```

**Eviction policy:**
```cpp
void BufferPool::EvictOldEntries()
{
    // Вызывается периодически из background thread
    BAutolock _(fLock);
    bigtime_t now = system_time();

    // Удалить неиспользуемые buffers старше kEvictionTime
    fPool.erase(
        std::remove_if(fPool.begin(), fPool.end(),
            [now](const PoolEntry& e) {
                return !e.inUse && (now - e.lastUsed) > kEvictionTime;
            }),
        fPool.end()
    );
}
```

**C++14 considerations:**
- `auto` для iterator types - cleaner code
- Lambda с capture для eviction - readable
- `std::vector` - standard, exception-safe container
- Но: Haiku kernel code избегает exceptions, поэтому `std::nothrow` везде

### 1.3 Reference Counting - BReference<>

**Текущая реализация:**

```cpp
// Referenceable.h:47-90
template<typename Type = BReferenceable>
class BReference {
public:
    void SetTo(Type* object, bool alreadyHasReference = false)
    {
        if (object != NULL && !alreadyHasReference)
            object->AcquireReference();

        Unset();

        fObject = object;
    }

    void Unset()
    {
        if (fObject) {
            fObject->ReleaseReference();
            fObject = NULL;
        }
    }

    // Copy constructor
    BReference(const BReference<Type>& other)
        : fObject(NULL)
    {
        SetTo(other.Get());  // Atomic increment
    }

    // Copy assignment
    BReference& operator=(const BReference<Type>& other)
    {
        SetTo(other.fObject);
        return *this;
    }
};
```

**Анализ:**

**Хорошо:**
- Exception-safe через RAII destructor
- `Detach()` для transfer of ownership
- Template-based, работает с любым BReferenceable

**Проблемы:**

**1. Отсутствие move semantics (C++11/14 feature):**

```cpp
// Текущий код при возврате из функции:
BReference<UtilityBitmap> CreateBitmap() {
    BReference<UtilityBitmap> result(new UtilityBitmap(...), true);
    return result;  // Copy! Atomic increment + decrement
}

// Caller:
BReference<UtilityBitmap> bitmap = CreateBitmap();
// Копирование: AcquireReference() в copy constructor
// Потом: ReleaseReference() от temporary
// Итого: 2 atomic операции вместо 0
```

**Рекомендация - добавить move constructor/assignment:**

```cpp
// Добавить в BReference (C++11/14):
BReference(BReference<Type>&& other) noexcept
    : fObject(other.fObject)
{
    other.fObject = NULL;  // Transfer ownership, no atomic ops
}

BReference& operator=(BReference<Type>&& other) noexcept
{
    if (this != &other) {
        Unset();  // Release current
        fObject = other.fObject;
        other.fObject = NULL;
    }
    return *this;
}

// Теперь:
BReference<UtilityBitmap> bitmap = CreateBitmap();
// Move constructor - 0 atomic операций!
```

**Impact:** На горячем пути compositor (60 fps * N windows) экономия atomic операций критична.

**2. AcquireReference/ReleaseReference implementation:**

Проблема: `atomic_add` - это full memory barrier на всех архитектурах Haiku. Для read-heavy scenarios (много копий BReference, редко меняется ownership) можно оптимизировать с relaxed ordering, но требуется проверка наличия `atomic_add_rel/acq` в Haiku.

---

## 2. Потоковая безопасность и синхронизация

### 2.1 MultiLocker Pattern

**Текущее использование:**

```cpp
// Desktop.h:291-299
MultiLocker fWindowLock;

bool LockSingleWindow() { return fWindowLock.ReadLock(); }
void UnlockSingleWindow() { fWindowLock.ReadUnlock(); }

bool LockAllWindows() { return fWindowLock.WriteLock(); }
void UnlockAllWindows() { fWindowLock.WriteUnlock(); }
```

**MultiLocker.h анализ:**

**Важно:**
- **Nested read locks НЕ поддерживаются**: комментарий в коде
- **Writer может брать read lock**: допустимо
- **Nested write locks поддерживаются**

**Критическая проблема для compositor:**

```cpp
// Desktop thread (writer lock held):
void Desktop::ComposeFrame() {
    AutoWriteLocker locker(fWindowLock);  // Write lock

    for (Window* w : fAllWindows) {
        w->RenderContent();  // Если внутри есть ReadLock - DEADLOCK!
    }
}
```

**Решение 1: Render without lock, compose with lock:**

```cpp
void Desktop::ComposeFrame() {
    // Phase 1: Trigger rendering (no lock needed, windows own their buffers)
    for (Window* w : fAllWindows) {
        if (w->IsVisible() && w->IsDirty()) {
            w->AsyncRenderContent();  // Запускает render в window thread
        }
    }

    // Phase 2: Wait for completion + compose (need read lock)
    AutoReadLocker locker(fWindowLock);
    for (Window* w : fAllWindows) {
        w->WaitForRenderComplete();
        if (w->HasContentBuffer()) {
            BlendWindowLayer(w);
        }
    }
}
```

**Решение 2: Per-window locks:**

```cpp
class Window {
    BLocker fContentLock;  // Отдельная блокировка для content buffer
    BReference<UtilityBitmap> fContentBuffer;

    void RenderContent() {
        BAutolock _(fContentLock);
        // Render into fContentBuffer
    }

    BReference<UtilityBitmap> GetContentBuffer() {
        BAutolock _(fContentLock);
        return fContentBuffer;  // BReference copy - safe
    }
};
```

**Рекомендация:** Combination обоих подходов:
- Per-window lock для content buffer access
- Desktop WindowLock только для window list modifications
- Compositor читает stable snapshot window list, потом обращается к window buffers через per-window locks

### 2.2 Concurrent Access to Compositor Layers

**Проблема:** Compositor thread читает window content buffers, window threads пишут в них.

**Race condition пример:**

```cpp
// Compositor thread:
void Compositor::BlendWindow(Window* window) {
    BReference<UtilityBitmap> buffer = window->GetContentBuffer();
    if (buffer.IsSet()) {
        fDrawingEngine->DrawBitmap(buffer, ...);
    }
}

// Window render thread (параллельно):
void Window::RenderContent() {
    BReference<UtilityBitmap> buffer = AcquireNewBuffer();
    DrawIntoBuffer(buffer);
    fContentBuffer = buffer;  // RACE!
}
```

**Решение: Double buffering per window:**

```cpp
class Window {
public:
    BReference<UtilityBitmap> GetCompositorBuffer() {
        BAutolock _(fBufferLock);
        return fCompositorBuffer;  // Stable, только compositor читает
    }

    void SwapBuffers() {
        BAutolock _(fBufferLock);
        std::swap(fCompositorBuffer, fRenderBuffer);
        fBufferSwapped = true;
    }

private:
    BLocker fBufferLock;
    BReference<UtilityBitmap> fCompositorBuffer;  // Compositor reads
    BReference<UtilityBitmap> fRenderBuffer;      // Render thread writes
    bool fBufferSwapped;
};
```

### 2.3 DrawingEngine Thread Safety

**Layer использует:**

```cpp
// Layer.cpp:140-147
if (layerEngine->LockParallelAccess()) {
    layerCanvas.UpdateCurrentDrawingRegion();
    Play(&layerCanvas);
    layerEngine->UnlockParallelAccess();
}
```

**Для compositor:**

Каждое окно рендерится в свой собственный DrawingEngine instance (offscreen), Desktop compositor использует ДРУГОЙ DrawingEngine (screen). Separation of DrawingEngine instances избегает lock contention.

### 2.4 UpdateSession Synchronization

**Текущая сложная схема:**

```cpp
// Window.h:234-255
UpdateSession fUpdateSessions[2];
UpdateSession* fCurrentUpdateSession;
UpdateSession* fPendingUpdateSession;
```

**Проблемы:**
1. Сложная логика переключения
2. Thread safety unclear
3. BRegion не поддерживает move - всегда copying

**Рекомендация:** Упростить до single UpdateSession с proper locking или использовать lock-free optimization для hot path.

---

## 3. Производительность

### 3.1 Caching Strategy

**Layer не использует caching** - каждый вызов RenderToBitmap() рендерит заново все содержимое.

**Для Window compositor это неприемлемо.** Предлагаемая стратегия:

```cpp
class Window {
public:
    void InvalidateContent(const BRegion& region) {
        BAutolock _(fContentLock);
        fContentDirty.Include(&region);
        fHasValidContent = false;
    }

    BReference<UtilityBitmap> GetContentBuffer() {
        BAutolock _(fContentLock);

        if (!fHasValidContent) {
            RenderContentLocked();  // Только dirty region
            fHasValidContent = true;
        }

        return fContentBuffer;
    }

private:
    void RenderContentLocked() {
        if (fContentBuffer == NULL) {
            fContentBuffer = gBufferPool.Acquire(fFrame.Size(), B_RGBA32);
            fContentDirty = fFrame;
        }

        if (fContentDirty.CountRects() == 0)
            return;

        // Render только dirty region
        BitmapHWInterface hwInterface(fContentBuffer);
        ObjectDeleter<DrawingEngine> engine(hwInterface.CreateDrawingEngine());

        engine->ConstrainClippingRegion(&fContentDirty);
        fTopView->Draw(fContentDirty, engine.Get());

        fContentDirty.MakeEmpty();
    }

    BLocker fContentLock;
    BReference<UtilityBitmap> fContentBuffer;
    BRegion fContentDirty;
    bool fHasValidContent;
};
```

### 3.2 Dirty Region Tracking

**Current Window implementation:**

```cpp
BRegion fDirtyRegion;     // Что нужно перерисовать
BRegion fExposeRegion;    // Subset of dirty - newly exposed
```

**Expose vs Dirty distinction:**
- **fExposeRegion**: новые пиксели (были под другим окном) - заполняются background color немедленно
- **fDirtyRegion**: все области requiring redraw - ждут client Draw()

Separate expose from dirty позволяет избежать visual artifacts.

### 3.3 Allocation Optimization

**Текущие allocation hotspots:**
1. UtilityBitmap creation - решается buffer pool
2. DrawingEngine creation - можно pool
3. BRegion operations - уже есть RegionPool в Window

**BRegion optimization:**

Window уже имеет RegionPool:

```cpp
// Window.h:192
RegionPool fRegionPool;

BRegion* GetRegion() { return fRegionPool.GetRegion(); }
void RecycleRegion(BRegion* region) { fRegionPool.Recycle(region); }
```

Использовать этот паттерн везде.

### 3.4 Virtual Call Overhead

Layer наследуется от ServerPicture, что приводит к virtual call chain. Для compositor pipeline: Window → Layer → ServerPicture → virtual Play().

**Optimization opportunities:**
1. Devirtualization через templates (ломает ABI)
2. Final specifier (C++11/14)
3. Profile-guided optimization

**Рекомендация:** Не оптимизировать преждевременно. Profiling сначала.

---

## 4. C++ идиомы и рекомендации

### 4.1 RAII Usage

**Текущее использование - отлично:**

```cpp
// Layer.cpp:129
ObjectDeleter<DrawingEngine> const layerEngine(layerInterface.CreateDrawingEngine());

// Automatic cleanup при выходе из scope
```

**Рекомендации:**
1. Consistent naming для smart pointers
2. const correctness для RAII objects
3. Prefer initialization over assignment

### 4.2 Move Semantics Opportunities

**C++14 поддерживает move semantics.** Кандидаты для move support:

1. **BReference<T>** - уже обсуждалось
2. **BRegion** - для избежания копирования массива rects
3. **ObjectDeleter<T>** - для transfer of ownership

**Impact:** Экономия копирований и atomic операций на горячем пути.

### 4.3 Template Metaprogramming - DrawingMode System

**Текущая система** - template-based drawing modes - хорошая архитектура для compile-time selection и zero overhead.

**Рекомендации:**
1. Explicit instantiation для reduce compile time
2. SFINAE constraints для type safety
3. Compositor compatibility - bridge layer между templates и high-level API

### 4.4 Exception Safety

**Haiku policy: No exceptions в critical paths.**

**Текущий код:**
- Все allocations используют std::nothrow
- Error handling через NULL checks
- RAII обеспечивает cleanup

**Рекомендации:**
1. Consistent error handling через status_t
2. Optional<T> для return values (можно реализовать свой)
3. Продолжить no-exceptions policy

### 4.5 Const Correctness

**Текущее состояние - mixed.**

**Рекомендации:**
1. Const methods где возможно
2. Const parameters для references
3. Mutable для cache fields в const methods

---

## 5. Критические технические проблемы

### 5.1 Object Lifecycles

**Layer pattern:**

```cpp
BReference<UtilityBitmap> bitmap(layer->RenderToBitmap(this), true);
// bitmap автоматически удаляется при выходе из scope
```

**Для Window compositor:**

```cpp
class Window {
    BReference<UtilityBitmap> fContentBuffer;  // Кешируется!
};
```

**Lifecycle:** BReference<> ref counting **prevents use-after-free**. Хорошо!

**Потенциальная проблема:** Memory bloat если compositor медленный. **Mitigation:** Profiling и monitoring.

### 5.2 Buffer Pool Management Complexity

**Problem:** Buffer pool может grow unbounded при разных размерах окон.

**Solutions:**
1. Size-based bucketing
2. Simple eviction policy (LRU)
3. Monitoring и limits

### 5.3 Coordinate Mapping - SetRendererOffset

**Layer использует:** `layerEngine->SetRendererOffset(boundingBox.left, boundingBox.top)`

**Для compositor:** Separation of coordinate spaces:
- Window rendering: local (0,0)
- Compositor: screen coordinates

Упрощает код - нет сложных transforms в render path.

### 5.4 Layer Template System Integration

**Plan:** "Layer as prototype - any duplication concerns?"

**Analysis:** Window НЕ должен наследоваться от Layer (разные data sources).

**Solution: Composition over inheritance:**

```cpp
class Window {
    BReference<UtilityBitmap> RenderToLayer() {
        // SIMILAR to Layer::RenderToBitmap(), но draws View hierarchy
    }
};
```

**No duplication** - код похож, но не дублирован. Можно extract common logic в helper template:

```cpp
class OffscreenRenderer {
    template<typename DrawFunc>
    static BReference<UtilityBitmap> Render(
        const BRect& bounds, DrawFunc drawFunc) { ... }
};
```

**C++14 lambda с capture** - perfect для этого use case!

---

## 6. Примеры кода - проблемы и решения

### 6.1 Проблема: Memory Leak в Error Path

**Current code pattern:**

```cpp
BReference<UtilityBitmap> layerBitmap(_AllocateBitmap(boundingBox), true);
if (layerBitmap == NULL)
    return NULL;

ObjectDeleter<DrawingEngine> const layerEngine(...);
if (!layerEngine.IsSet())
    return NULL;  // Фактически НЕТ leak - RAII
```

**Но код неочевиден.** Better: Early return pattern с helper methods.

### 6.2 Проблема: Race Condition в Window Content Update

**Scenario:** Множество threads accessing buffer одновременно.

**Solutions:**
1. Read-Write Lock для buffer
2. Double buffering (лучше для real-time)

**Trade-off:**
- RW lock: меньше памяти, но serialization
- Double buffer: больше памяти, но true parallelism

**Рекомендация:** Double buffering - frame rate важнее.

### 6.3 Проблема: UpdateSession Complexity

**Current code:** Два UpdateSession с swap logic - сложно.

**Упрощенная альтернатива:** Single lock с atomic для fast path.

---

## 7. Заключение

### 7.1 Архитектурные выводы

**Layer.cpp является образцовым прототипом compositor:**

1. ✅ Offscreen rendering через BitmapHWInterface
2. ✅ Isolated DrawingEngine per render target
3. ✅ Coordinate mapping через SetRendererOffset
4. ✅ Composition через DrawBitmap с effects
5. ✅ Memory management через BReference<UtilityBitmap>

**Расширение на Window compositor требует:**

1. ❌ Caching - Layer рендерит каждый раз
2. ❌ Buffer pooling - избежать constant allocation
3. ❌ Threading model - явно определить
4. ❌ Dirty region optimization - partial updates

### 7.2 Критические изменения

**Must have:**

1. BufferPool implementation
2. Double buffering
3. Move semantics для BReference/BRegion
4. Simplified UpdateSession
5. Explicit threading model

**Nice to have:**

1. Per-window render threads
2. Compositor effects (shadows, blur)
3. Hardware acceleration path
4. Profiling infrastructure

### 7.3 C++14 Best Practices Checklist

**Текущий код:**
- ✅ RAII через ObjectDeleter/BReference
- ✅ const correctness (частично)
- ✅ No exceptions в critical paths
- ✅ Template system для drawing modes
- ⚠️ Move semantics отсутствуют
- ⚠️ Auto type deduction мало используется
- ⚠️ Lambda functions редко

**Рекомендации:**
1. Внедрить move semantics
2. Использовать auto где уместно
3. Lambda для callbacks
4. Range-based for loops

### 7.4 Приоритеты внедрения

**Фаза 1 (критично):**
1. Внедрить BufferPool
2. Добавить Window::RenderContent() по образцу Layer
3. Создать Desktop::Compositor с simple logic
4. Тестировать на simple cases

**Фаза 2 (оптимизация):**
1. Double buffering
2. Move semantics
3. Dirty region optimization
4. Упростить UpdateSession

**Фаза 3 (production):**
1. Parallel rendering
2. Compositor effects
3. Profiling
4. Hardware acceleration hooks

### 7.5 Риски и митигации

**Риски:**
1. Lock contention → Per-window locks, profiling
2. Memory bloat → Eviction policy, monitoring
3. Breaking API/ABI → Все изменения internal
4. Performance regression → Benchmarking, incremental rollout
5. Threading bugs → ThreadSanitizer, careful design

### 7.6 Финальная рекомендация

**Предложенный план технически обоснован** и базируется на proven Layer implementation.

**Strengths:**
- Опирается на working code
- Минимальные изменения в public API
- Поэтапное внедрение возможно
- C++14 features достаточны

**Gaps для заполнения:**
- Buffer pool implementation
- Threading model specification
- Move semantics additions
- UpdateSession simplification

**Рекомендуется начать с prototype** с фокусом на buffer pooling и thread safety. Измерить performance перед full rollout.

**Ожидаемые результаты:**
- Улучшение responsiveness
- Поддержка compositor effects
- Подготовка к GPU acceleration
- Cleaner architecture

---

**Конец технического обзора**

*Все примеры кода следуют C++14 стандарту и Haiku coding conventions (no exceptions, explicit memory management, const correctness).*
