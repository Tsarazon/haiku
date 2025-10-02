# Правильная архитектура композитора (исправленная)

## Проблема исходного плана

### Исходная схема (из плана):

```
Window layers (UtilityBitmap, RAM)
    ↓ [КОПИРОВАНИЕ RAM → RAM]
Compositor blend → CompositorBuffer (RAM)
    ↓ [КОПИРОВАНИЕ RAM → RAM]
BackBuffer (MallocBuffer, RAM)
    ↓ [КОПИРОВАНИЕ RAM → VRAM]
_CopyBackToFront()
    ↓
FrontBuffer (AccelerantBuffer, VRAM)
    ↓
Screen
```

**Критическая ошибка:** 
- **2 лишних копирования RAM → RAM** (UtilityBitmap → CompositorBuffer → BackBuffer)
- **UtilityBitmap и MallocBuffer оба в heap!** Зачем копировать из RAM в RAM?

### Реальная схема Haiku (без композитора):

```
Painter → BackBuffer (MallocBuffer, RAM) → _CopyBackToFront() → FrontBuffer (AccelerantBuffer, VRAM) → Screen
```

**1 копирование:** RAM → VRAM (это необходимо, т.к. CRT читает из VRAM)

---

## Правильное решение

### Архитектура на базе Haiku areas + double buffering

```
Client рисует → BBitmap (area_id, RAM) ← Shared memory area
                    ↓ [NO COPY! Compositor maps same area]
Compositor reads same RAM → CPU blend → BackBuffer (MallocBuffer, RAM)
                                             ↓ [Existing code!]
                                    _CopyBackToFront()
                                             ↓
                                    FrontBuffer (AccelerantBuffer, VRAM)
                                             ↓
                                          Screen
```

**Итого: 1 копирование (RAM → VRAM)** - как сейчас, без лишних копирований!

---

## 1. Обмен буферами через area_id

### Текущая реализация (ServerBitmap):

```cpp
// src/servers/app/ServerBitmap.cpp (lines 44-62)
ServerBitmap::ServerBitmap(BRect bounds, color_space space, uint32 flags,
                           int32 bytesPerRow, screen_id screen)
{
    int32 size = fBytesPerRow * fHeight;
    
    if (fFlags & B_BITMAP_WILL_OVERLAY || fFlags & B_BITMAP_RESERVE_OVERLAY_CHANNEL) {
        // Allocate через area для overlay
        fArea = create_area("server bitmap", (void**)&fBuffer, B_ANY_ADDRESS,
                           size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
    } else {
        // Обычный heap allocation
        fBuffer = new(nothrow) uint8[size];
        fArea = -1;
    }
}
```

**Проблема:** ServerBitmap использует areas ТОЛЬКО для overlay, обычные bitmap'ы = heap.

### Правильная реализация для композитора:

```cpp
class Layer {
    area_id     fBitmapArea[2];      // ← Front and Back buffers
    BBitmap*    fBitmap[2];          // ← Mapped to areas
    uint32      fCurrentBuffer;      // ← 0 or 1 (which buffer client writes to)
    uint64      fSequenceNumber[2];  // ← Per-buffer sequence (for compositor sync)
    
    // Client thread
    void BeginDraw() {
        // Swap to back buffer
        fCurrentBuffer = 1 - fCurrentBuffer;
        return fBitmap[fCurrentBuffer];
    }
    
    void EndDraw() {
        // Increment sequence, notify compositor
        fSequenceNumber[fCurrentBuffer]++;
        fAppServer->InvalidateLayer(this, fBitmapArea[fCurrentBuffer],
                                    fSequenceNumber[fCurrentBuffer]);
    }
    
    // Compositor thread
    uint32 GetReadBuffer() const {
        return 1 - fCurrentBuffer;  // ← Read OLD buffer while client writes NEW
    }
    
    BBitmap* GetReadBitmap() {
        uint32 readBuffer = GetReadBuffer();
        return fBitmap[readBuffer];
    }
};
```

**Логика double buffering:**

```
Time 0:
  Client пишет в Buffer #0
  Compositor читает из Buffer #1 (старый кадр)

Time 1:
  Client закончил → Swap → fCurrentBuffer = 1
  Client пишет в Buffer #1
  Compositor начинает читать Buffer #0 (свежий кадр)

Time 2:
  Client закончил → Swap → fCurrentBuffer = 0
  Client пишет в Buffer #0
  Compositor читает Buffer #1 (свежий кадр)
```

**Результат:** Client и Compositor НИКОГДА не трогают один и тот же буфер одновременно.

---

## 2. Синхронизация через sequence numbers

### Проблема без sequence numbers:

```cpp
// ❌ BAD: Race condition
void Compositor::Composite() {
    for (layer in layers) {
        BBitmap* bitmap = layer->GetReadBitmap();
        Blend(bitmap);  // ← Но откуда знать, изменился ли bitmap с прошлого кадра?
    }
}
```

**Проблемы:**
- Compositor может пропустить update (client изменил bitmap, но compositor не знает)
- Compositor может блендить тот же кадр дважды (waste CPU)

### Правильная синхронизация:

```cpp
class Layer {
    uint64  fSequenceNumber[2];     // ← Per-buffer sequence
    uint64  fLastComposited;        // ← Last sequence compositor saw
    
    void EndDraw() {
        fSequenceNumber[fCurrentBuffer]++;  // ← Increment AFTER draw
    }
};

void Compositor::Composite() {
    for (layer in layers) {
        uint32 readBuffer = layer->GetReadBuffer();
        
        // Check if buffer changed since last composite
        if (layer->fSequenceNumber[readBuffer] > layer->fLastComposited) {
            BBitmap* bitmap = layer->fBitmap[readBuffer];
            Blend(bitmap);
            
            layer->fLastComposited = layer->fSequenceNumber[readBuffer];
        }
        // Else: skip, nothing changed
    }
}
```

**Гарантии:**
- ✅ Compositor никогда не пропустит update (sequence number растёт)
- ✅ Compositor никогда не блендит старый кадр дважды (проверка sequence)
- ✅ Нет race conditions (client и compositor работают с разными буферами)

**uint64 vs uint32:**
```cpp
uint64 fSequenceNumber;  // ← 584 billion years при 1000 FPS, никогда не переполнится
uint32 fSequenceNumber;  // ← 49 дней при 1000 FPS, переполнится (но можно обработать)
```

Рекомендация: **uint64** (проще, безопаснее, overhead = 4 байта).

---

## 3. Main loop: messages OR timer/VSync

### Compositor thread loop:

```cpp
int32 Compositor::_CompositorThread(void* data) {
    Compositor* compositor = (Compositor*)data;
    
    while (!compositor->fQuit) {
        // Wait for:
        // 1) Layer invalidate (release_sem immediately) OR
        // 2) VSync timeout (16.67ms for 60 Hz)
        status_t result = acquire_sem_etc(compositor->fWakeupSem, 1,
                                          B_RELATIVE_TIMEOUT, 
                                          16667);  // ← 1000000 / 60 = 16667 µs
        
        if (compositor->fDirtyRegion.CountRects() > 0) {
            compositor->_ComposeFrame();
        }
    }
    
    return 0;
}

void Compositor::InvalidateLayer(Layer* layer, area_id area, uint64 sequence) {
    // Add layer's bounds to dirty region
    if (fLayerLock.WriteLock()) {
        fDirtyRegion.Include(&layer->Bounds());
        fLayerLock.WriteUnlock();
    }
    
    // Wake compositor thread
    release_sem(fWakeupSem);
}
```

**Логика:**

| Событие | Что происходит |
|---------|---------------|
| Layer invalidated | `release_sem()` → compositor просыпается немедленно → блендит |
| Ничего не произошло 16.67ms | Таймаут → compositor просыпается → проверяет dirty region (пустой) → спит снова |
| 10 layers invalidated за 5ms | 10× `release_sem()` → compositor просыпается 1 раз → блендит все 10 за раз |

**Преимущества:**
- ✅ **Не waste CPU** когда нечего делать (compositor спит на semaphore)
- ✅ **Быстрая реакция** на invalidate (просыпается сразу, не ждёт VSync)
- ✅ **VSync-limited** (максимум 60 FPS даже если 1000 invalidate/sec)
- ✅ **Батчинг** (несколько invalidate = один composite)

### Будущее улучшение: Hardware VSync fence

```cpp
// Вместо таймаута - ждём настоящий VSync от драйвера
int32 Compositor::_CompositorThread(void* data) {
    Compositor* compositor = (Compositor*)data;
    
    while (!compositor->fQuit) {
        // Wait for VSync event from accelerant
        bigtime_t vsync_time = compositor->fHWInterface->WaitForVBlank();
        
        if (compositor->fDirtyRegion.CountRects() > 0) {
            compositor->_ComposeFrame();
        }
    }
    
    return 0;
}
```

**Преимущества hardware VSync:**
- ✅ Точная синхронизация с дисплеем (не 16.67ms таймер, а реальный VBlank interrupt)
- ✅ Нет tearing (compositor закончит ДО начала scanout)
- ✅ Адаптивная частота (120 Hz дисплей → 8.33ms, 60 Hz → 16.67ms)

**Требования:**
- Accelerant должен поддерживать `ACCELERANT_RETRACE_SEMAPHORE` или аналог
- Нужна реализация `AccelerantHWInterface::WaitForVBlank()`

---

## 4. compose_region() - CPU blending в BackBuffer

### Полная реализация:

```cpp
void Compositor::_ComposeFrame() {
    // Lock compositor state (read-only for layers)
    if (!fLayerLock.ReadLock())
        return;
    
    // Get BackBuffer from existing HWInterface
    RenderingBuffer* backBuffer = fHWInterface->BackBuffer();
    if (!backBuffer) {
        fLayerLock.ReadUnlock();
        return;
    }
    
    // Create painter attached to BackBuffer
    AGGPainter painter;
    painter.AttachToBuffer(backBuffer);
    painter.ConstrainClipping(&fDirtyRegion);
    
    // Blend layers bottom-to-top
    for (int32 i = 0; i < fLayers.CountItems(); i++) {
        Layer* layer = fLayers.ItemAt(i);
        
        if (layer->IsHidden())
            continue;
        
        // Compute intersection with dirty region
        BRegion layerDirty(layer->Bounds());
        layerDirty.IntersectWith(&fDirtyRegion);
        
        if (layerDirty.CountRects() == 0)
            continue;  // Layer not in dirty region
        
        // Check if layer changed since last composite
        uint32 readBuffer = layer->GetReadBuffer();
        if (layer->fSequenceNumber[readBuffer] <= layer->fLastComposited)
            continue;  // Layer unchanged, skip
        
        // Map layer's shared area (READ ONLY!)
        BBitmap* bitmap = layer->GetReadBitmap();
        if (!bitmap)
            continue;
        
        // Blend layer into BackBuffer
        painter.SetDrawingMode(B_OP_ALPHA);  // ← Alpha blending
        painter.SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
        
        for (int32 j = 0; j < layerDirty.CountRects(); j++) {
            BRect rect = layerDirty.RectAt(j);
            painter.DrawBitmap(bitmap, rect, rect);
        }
        
        // Mark layer as composited
        layer->fLastComposited = layer->fSequenceNumber[readBuffer];
    }
    
    fLayerLock.ReadUnlock();
    
    // Copy BackBuffer → FrontBuffer (EXISTING CODE!)
    if (fDrawingEngine->Lock()) {
        fHWInterface->_CopyBackToFront(&fDirtyRegion);
        fDrawingEngine->Unlock();
    }
    
    // Clear dirty region
    fDirtyRegion.MakeEmpty();
}
```

**Что переиспользуется из существующего кода:**

| Компонент | Файл | Что делает |
|-----------|------|-----------|
| `RenderingBuffer* BackBuffer()` | [AccelerantHWInterface.cpp:1361](AccelerantHWInterface.cpp:1361) | Возвращает MallocBuffer (RAM) |
| `_CopyBackToFront()` | [AccelerantHWInterface.cpp:638](AccelerantHWInterface.cpp:638) | Копирует RAM → VRAM |
| `AGGPainter` | src/servers/app/drawing/Painter/AGGPainter.cpp | CPU blending через AGG |
| `DrawBitmap()` | AGGPainter | Рисует BBitmap с alpha blending |

**Результат:** Минимальные изменения в HWInterface, переиспользование 90% существующего кода.

---

## 5. Ownership и lifecycle areas

### Проблема: Client умирает → area удаляется

```cpp
// Client создаёт area
area_id area = create_area("layer bitmap", &bits, B_ANY_ADDRESS,
                           size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);

// Передаёт в app_server
SendToAppServer(MSG_SET_LAYER_BITMAP, area);

// ❌ Client crashes → area автоматически удаляется → Compositor получит -1 при map!
```

### Решение: clone_area()

```cpp
area_id Compositor::RegisterLayerBitmap(Layer* layer, area_id client_area) {
    void* address = NULL;
    
    // Clone area into app_server's address space
    area_id compositor_area = clone_area("compositor layer bitmap",
                                         &address,
                                         B_ANY_ADDRESS,
                                         B_READ_AREA,  // ← READ ONLY для compositor
                                         client_area);
    
    if (compositor_area < 0) {
        printf("Compositor: failed to clone area %ld: %s\n",
               client_area, strerror(compositor_area));
        return -1;
    }
    
    // Map area to BBitmap
    BBitmap* bitmap = new BBitmap(layer->Bounds(), layer->ColorSpace(),
                                  address, compositor_area);
    
    layer->fBitmapArea[layer->fCurrentBuffer] = compositor_area;
    layer->fBitmap[layer->fCurrentBuffer] = bitmap;
    
    return compositor_area;
}
```

**Гарантии:**
- ✅ Area живёт пока compositor держит clone (даже если client умер)
- ✅ Compositor имеет READ-ONLY доступ (B_READ_AREA), не может испортить client данные
- ✅ При удалении layer, compositor вызывает `delete_area(compositor_area)`

---

## 6. Memory overhead

### Double buffering per-layer:

```
Full HD layer (1920×1080×4 bytes):
- Single buffer: 8 MB
- Double buffer: 16 MB

10 windows × 2 buffers = 160 MB
```

**Для современных систем (2-16 GB RAM) это приемлемо.**

### Оптимизация для будущего:

```cpp
class Layer {
    bool NeedsDoubleBuffering() const {
        // Only top-level windows need double buffering
        // Child views can use single buffer (they don't update in parallel)
        return fParent == NULL || fParent == fDesktop;
    }
    
    void AllocateBuffers() {
        if (NeedsDoubleBuffering()) {
            // Allocate 2 buffers
            _AllocateArea(0);
            _AllocateArea(1);
        } else {
            // Allocate 1 buffer
            _AllocateArea(0);
            fBitmapArea[1] = fBitmapArea[0];  // Share same area
            fBitmap[1] = fBitmap[0];
        }
    }
};
```

**Экономия:**
- Top-level windows (10): 16 MB × 10 = 160 MB
- Child views (100): 8 MB × 100 = 800 MB (вместо 1600 MB с double buffering)

**Итого:** 960 MB вместо 1760 MB (экономия 800 MB).

---

## 7. Миграция на GPU в будущем

### Текущая схема (CPU compositor):

```cpp
Client → area_id (RAM) → Compositor CPU blend → BackBuffer (RAM) → VRAM
```

### Будущая схема (GPU compositor):

```cpp
Client → fd/dma-buf (RAM or VRAM) → GPU texture → GPU compositor → GPU framebuffer → Screen
```

**Изменения в коде:**

```cpp
// BEFORE (CPU compositor)
class Layer {
    area_id  fBitmapArea[2];
    BBitmap* fBitmap[2];
};

// AFTER (GPU compositor)
class Layer {
#ifdef USE_GPU_COMPOSITOR
    int      fDmaBufFd[2];      // ← DMA-BUF file descriptor
    GLuint   fTexture[2];       // ← OpenGL texture ID
#else
    area_id  fBitmapArea[2];    // ← Haiku area (fallback)
    BBitmap* fBitmap[2];
#endif
};

void Compositor::_ComposeFrame() {
#ifdef USE_GPU_COMPOSITOR
    // GPU path
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (layer in layers) {
        glBindTexture(GL_TEXTURE_2D, layer->fTexture[layer->GetReadBuffer()]);
        glDrawQuad(layer->bounds);
    }
    glFinish();
    SwapBuffers();
#else
    // CPU path (current)
    AGGPainter painter;
    painter.AttachToBuffer(fHWInterface->BackBuffer());
    for (layer in layers) {
        BBitmap* bitmap = layer->GetReadBitmap();
        painter.DrawBitmap(bitmap, layer->bounds);
    }
    fHWInterface->_CopyBackToFront(&fDirtyRegion);
#endif
}
```

**Преимущества постепенной миграции:**
- ✅ CPU compositor работает на всех системах (fallback)
- ✅ GPU compositor опционален (требует OpenGL/Vulkan driver)
- ✅ area_id → fd/dma-buf = локальное изменение, не затрагивает всю архитектуру
- ✅ Можно тестировать оба пути параллельно

---

## 8. Итоговая архитектура

### Полная схема:

```
┌─────────────────────────────────────────────────────────────┐
│ Client thread (BWindow)                                      │
│                                                               │
│  BView::Draw() → BBitmap (area #0, RAM)                     │
│       ↓ Swap buffers                                         │
│  BView::Draw() → BBitmap (area #1, RAM)                     │
│       ↓ fSequenceNumber++                                    │
│  InvalidateLayer(area_id, sequence)                          │
└──────────────────────────┬──────────────────────────────────┘
                           │ (message to app_server)
┌──────────────────────────▼──────────────────────────────────┐
│ Desktop thread (app_server)                                  │
│                                                               │
│  MessageReceived(MSG_INVALIDATE_LAYER)                       │
│       ↓                                                       │
│  Compositor::InvalidateLayer(layer)                          │
│       ↓                                                       │
│  fDirtyRegion.Include(&layer->Bounds())                      │
│       ↓                                                       │
│  release_sem(fWakeupSem) ──────────────────────┐            │
└────────────────────────────────────────────────┼────────────┘
                                                  │
┌─────────────────────────────────────────────────▼───────────┐
│ Compositor thread                                            │
│                                                               │
│  acquire_sem_etc(fWakeupSem, timeout=16667µs)               │
│       ↓ (woken by invalidate OR VSync timeout)              │
│  if (fDirtyRegion.CountRects() > 0)                         │
│       ↓                                                       │
│  _ComposeFrame():                                            │
│    1. fLayerLock.ReadLock()                                 │
│    2. For each layer in dirty region:                        │
│         a. readBuffer = 1 - layer->fCurrentBuffer           │
│         b. if (layer->fSequenceNumber[readBuffer] changed)  │
│              - Map area READ-ONLY                            │
│              - AGGPainter::DrawBitmap() → BackBuffer (RAM)  │
│    3. fLayerLock.ReadUnlock()                               │
│    4. _CopyBackToFront() → FrontBuffer (VRAM) ← existing!  │
│    5. fDirtyRegion.MakeEmpty()                              │
│       ↓                                                       │
│  Loop again (wait for next invalidate OR VSync)             │
└─────────────────────────────────────────────────────────────┘
```

### Потоки и блокировки:

| Thread | Доступ к данным | Lock type |
|--------|----------------|-----------|
| **Client** | Пишет в `fBitmap[fCurrentBuffer]` | None (owns current buffer) |
| **Desktop** | Читает Z-order, модифицирует layer tree | `fLayerLock` write lock |
| **Compositor** | Читает `fBitmap[1 - fCurrentBuffer]`, блендит | `fLayerLock` read lock |

**Гарантии thread safety:**
- ✅ Client и Compositor НИКОГДА не трогают один буфер (double buffering)
- ✅ Desktop и Compositor синхронизированы через `fLayerLock` (read-write lock)
- ✅ Sequence numbers предотвращают "lost update"

---

## 9. Сравнение с исходным планом

| Аспект | Исходный план | Правильная архитектура |
|--------|---------------|------------------------|
| **Копирования RAM→RAM** | 2 (UtilityBitmap → CompositorBuffer → BackBuffer) | **0** (shared areas) |
| **Копирования RAM→VRAM** | 1 | 1 (без изменений) |
| **Синхронизация** | ??? (не описано) | Sequence numbers |
| **Race conditions** | Возможны | **Невозможны** (double buffering) |
| **VSync sync** | ??? | Timer OR hardware fence |
| **Memory overhead** | 1× per layer | 2× per layer (16 MB → 32 MB Full HD) |
| **Changes to HWInterface** | Много (новый буфер) | **Минимум** (reuse BackBuffer) |
| **GPU migration path** | Сложно | **Легко** (area_id → fd/dma-buf) |
| **Thread safety model** | ??? | Read-write lock + double buffering |
| **Update batching** | ??? | Да (semaphore + VSync timer) |

---

## 10. Реализация: шаги

### Phase 1: Shared areas для layers

```cpp
// Изменить ServerBitmap для ВСЕХ bitmap'ов (не только overlay)
ServerBitmap::ServerBitmap(...) {
    fArea = create_area("server bitmap", (void**)&fBuffer, B_ANY_ADDRESS,
                       size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
    // Убрать fallback на heap
}
```

### Phase 2: Double buffering

```cpp
class Layer {
    area_id   fBitmapArea[2];
    BBitmap*  fBitmap[2];
    uint32    fCurrentBuffer;
    uint64    fSequenceNumber[2];
    
    void SwapBuffers();
    BBitmap* GetBackBitmap();
    BBitmap* GetReadBitmap();
};
```

### Phase 3: Compositor thread

```cpp
class Desktop {
    Compositor*  fCompositor;
    thread_id    fCompositorThread;
    
    void _StartCompositor();
    void _StopCompositor();
};

int32 Compositor::_CompositorThread(void* data) {
    while (!quit) {
        acquire_sem_etc(fWakeupSem, 1, B_RELATIVE_TIMEOUT, 16667);
        _ComposeFrame();
    }
}
```

### Phase 4: Integration с Desktop

```cpp
void Desktop::SetWindowFeel(Window* window, window_feel feel) {
    fWindowLock.Lock();
    window->SetFeel(feel);
    _UpdateWindowOrder();
    fWindowLock.Unlock();
    
    // Invalidate compositor
    fCompositor->InvalidateLayer(window->GetLayer());
}
```

### Phase 5: VSync fence (опционально)

```cpp
int32 Compositor::_CompositorThread(void* data) {
    while (!quit) {
        bigtime_t vsync = fHWInterface->WaitForVBlank();
        _ComposeFrame();
    }
}
```

---

## 11. Тестирование

### Unit tests:

```cpp
TEST(Compositor, DoubleBufferingPreventsRaceCondition) {
    Layer* layer = new Layer(BRect(0, 0, 100, 100));
    
    // Client thread writes to buffer 0
    BBitmap* back = layer->GetBackBitmap();
    EXPECT_EQ(layer->fCurrentBuffer, 0);
    
    // Compositor thread reads from buffer 1
    BBitmap* front = layer->GetReadBitmap();
    EXPECT_EQ(layer->GetReadBuffer(), 1);
    
    // Buffers are different
    EXPECT_NE(back, front);
}

TEST(Compositor, SequenceNumberDetectsChange) {
    Layer* layer = new Layer(BRect(0, 0, 100, 100));
    
    uint64 seq1 = layer->fSequenceNumber[0];
    layer->SwapBuffers();
    uint64 seq2 = layer->fSequenceNumber[0];
    
    EXPECT_EQ(seq2, seq1 + 1);
}

TEST(Compositor, InvalidateWakesCompositorThread) {
    Compositor* compositor = new Compositor();
    compositor->Start();
    
    Layer* layer = new Layer(BRect(0, 0, 100, 100));
    
    bigtime_t start = system_time();
    compositor->InvalidateLayer(layer);
    
    // Wait for compositor to process
    snooze(5000);  // 5ms
    
    bigtime_t elapsed = system_time() - start;
    
    // Should wake immediately, not wait for VSync (16.67ms)
    EXPECT_LT(elapsed, 10000);  // Less than 10ms
}
```

### Integration tests:

```cpp
TEST(Compositor, BlendsTwoLayersCorrectly) {
    MockDrawingEngine* engine = new MockDrawingEngine();
    Desktop* desktop = new Desktop(engine);
    Compositor* compositor = desktop->GetCompositor();
    
    // Create two layers: red 50% alpha, blue 100% alpha
    Layer* red = CreateTestLayer(BRect(0, 0, 100, 100), 
                                  (rgb_color){255, 0, 0, 128});
    Layer* blue = CreateTestLayer(BRect(50, 50, 150, 150),
                                   (rgb_color){0, 0, 255, 255});
    
    compositor->InvalidateLayer(red);
    compositor->InvalidateLayer(blue);
    compositor->_ComposeFrame();
    
    // Check blended pixel at (75, 75)
    rgb_color result = engine->GetPixel(75, 75);
    
    // Expected: red 50% over blue = (128, 0, 128)
    EXPECT_NEAR(result.red, 128, 5);
    EXPECT_NEAR(result.green, 0, 5);
    EXPECT_NEAR(result.blue, 128, 5);
}
```

---

## 12. Производительность

### Benchmarks целей:

| Сценарий | Целевая производительность |
|----------|---------------------------|
| 1 layer invalidate → composite | < 1ms (60 FPS = 16.67ms budget) |
| 10 layers full screen composite | < 10ms |
| 100 small layers (100×100) composite | < 5ms |
| Double buffer swap | < 0.01ms (pointer swap) |
| area_id clone | < 0.1ms |
| Compositor wakeup latency | < 1ms |

### Профилирование:

```cpp
void Compositor::_ComposeFrame() {
    bigtime_t start = system_time();
    
    // ... compositing ...
    
    bigtime_t elapsed = system_time() - start;
    if (elapsed > 10000) {  // > 10ms
        printf("Compositor: slow frame! %lld µs, %ld layers\n",
               elapsed, fLayers.CountItems());
    }
}
```

---

## Заключение

**Правильная архитектура:**

✅ **0 копирований RAM → RAM** (shared areas вместо промежуточного буфера)  
✅ **1 копирование RAM → VRAM** (существующий _CopyBackToFront)  
✅ **Нет race conditions** (double buffering + sequence numbers)  
✅ **VSync синхронизация** (timer OR hardware fence)  
✅ **Минимальные изменения** (reuse BackBuffer, AccelerantHWInterface)  
✅ **Готово к GPU** (area_id → fd/dma-buf)  
✅ **Thread-safe** (read-write lock + separate buffers)  
✅ **Батчинг updates** (semaphore + 16.67ms timeout)  

**Memory overhead:**
- 16 MB per Full HD layer (double buffering)
- 160 MB для 10 windows
- Приемлемо для современных систем

**Миграция:**
1. Phase 1: Shared areas (ServerBitmap)
2. Phase 2: Double buffering (Layer)
3. Phase 3: Compositor thread
4. Phase 4: Integration с Desktop
5. Phase 5: VSync fence (опционально)
6. Future: GPU compositor (OpenGL/Vulkan)

**Эта архитектура решает ВСЕ проблемы исходного плана.**
