# План миграции на архитектуру с compositor

## Обзор изменений

План разделён на 5 фаз, каждая тестируется отдельно:

1. **Phase 1:** ServerBitmap всегда использует areas (не только overlay)
2. **Phase 2:** Double buffering для Layer/Window
3. **Phase 3:** Compositor thread + основная логика блендинга
4. **Phase 4:** Интеграция с Desktop (invalidation flow)
5. **Phase 5:** VSync fence (опционально)

---

## Phase 1: ServerBitmap → всегда areas

### Цель:
Убрать heap allocation (`new uint8[]`), ВСЕ bitmap'ы должны использовать shared areas.

### Файлы для изменения:

#### 1.1. `src/servers/app/ServerBitmap.h`

**Текущий код (lines 96-110):**
```cpp
protected:
    ClientMemory    fClientMemory;
    AreaMemory*     fMemory;           // ← NULL для heap bitmaps
    ObjectDeleter< ::Overlay> fOverlay;
    uint8*          fBuffer;           // ← Может быть heap или area
    
    int32           fWidth;
    int32           fHeight;
    int32           fBytesPerRow;
    color_space     fSpace;
    uint32          fFlags;
    
    ServerApp*      fOwner;
    int32           fToken;
};
```

**Изменения:**
```cpp
protected:
    ClientMemory    fClientMemory;
    AreaMemory*     fMemory;           // ← ВСЕГДА не NULL
    ObjectDeleter< ::Overlay> fOverlay;
    uint8*          fBuffer;           // ← ВСЕГДА из area
    
    // NEW: Для double buffering (Phase 2)
    area_id         fAreaID[2];        // ← Front and back areas
    uint8*          fBufferPtr[2];     // ← Pointers to mapped areas
    uint32          fCurrentBuffer;    // ← 0 or 1
    uint64          fSequenceNumber[2];// ← Per-buffer sequence
    
    int32           fWidth;
    int32           fHeight;
    int32           fBytesPerRow;
    color_space     fSpace;
    uint32          fFlags;
    
    ServerApp*      fOwner;
    int32           fToken;
};
```

**Новые методы:**
```cpp
public:
    // Phase 1: Area management
    area_id         AreaID() const { return fAreaID[fCurrentBuffer]; }
    
    // Phase 2: Double buffering
    void            SwapBuffers();
    uint8*          GetBackBuffer();        // For client writing
    uint8*          GetFrontBuffer() const; // For compositor reading
    uint32          GetReadBuffer() const { return 1 - fCurrentBuffer; }
    uint64          GetSequenceNumber(uint32 buffer) const;
```

#### 1.2. `src/servers/app/ServerBitmap.cpp`

**Текущий код (lines 122-130):**
```cpp
void ServerBitmap::AllocateBuffer()
{
    uint32 length = BitsLength();
    if (length > 0) {
        delete[] fBuffer;              // ← HEAP allocation
        fBuffer = new(std::nothrow) uint8[length];
    }
}
```

**Изменить на:**
```cpp
void ServerBitmap::AllocateBuffer()
{
    uint32 length = BitsLength();
    if (length == 0)
        return;
    
    // ALWAYS use areas (не только для overlay)
    char areaName[B_OS_NAME_LENGTH];
    snprintf(areaName, sizeof(areaName), "bitmap_%ldx%ld", fWidth, fHeight);
    
    void* address = NULL;
    fAreaID[0] = create_area(areaName, &address, B_ANY_ADDRESS,
                             length, B_NO_LOCK, 
                             B_READ_AREA | B_WRITE_AREA);
    
    if (fAreaID[0] < 0) {
        printf("ServerBitmap: failed to create area: %s\n", strerror(fAreaID[0]));
        fBuffer = NULL;
        return;
    }
    
    fBufferPtr[0] = (uint8*)address;
    fBuffer = fBufferPtr[0];  // Backward compatibility
    
    // Phase 1: Single buffer только
    fAreaID[1] = -1;
    fBufferPtr[1] = NULL;
    fCurrentBuffer = 0;
    fSequenceNumber[0] = 0;
    fSequenceNumber[1] = 0;
    
    // Create AreaMemory wrapper
    fMemory = new AreaMemory(fAreaID[0], address, length);
}
```

**Деструктор изменить (lines 107-114):**
```cpp
ServerBitmap::~ServerBitmap()
{
    // Delete areas (not heap!)
    if (fAreaID[0] >= 0)
        delete_area(fAreaID[0]);
    if (fAreaID[1] >= 0)
        delete_area(fAreaID[1]);
    
    if (fMemory != NULL) {
        if (fMemory != &fClientMemory)
            delete fMemory;
    }
    // УБРАТЬ: delete[] fBuffer; ← больше не heap
}
```

#### 1.3. `src/servers/app/UtilityBitmap` (наследник ServerBitmap)

**Текущий код (lines 218-224):**
```cpp
UtilityBitmap::UtilityBitmap(BRect rect, color_space space, uint32 flags,
        int32 bytesPerRow, screen_id screen)
    : ServerBitmap(rect, space, flags, bytesPerRow, screen)
{
    AllocateBuffer();  // ← Вызывает новый AllocateBuffer() с areas
}
```

**Изменения:** НЕ НУЖНЫ, `AllocateBuffer()` уже изменён.

#### 1.4. Тестирование Phase 1:

```cpp
// tests/servers/app/ServerBitmapTest.cpp
TEST(ServerBitmap, AlwaysUsesAreas) {
    UtilityBitmap* bitmap = new UtilityBitmap(BRect(0, 0, 99, 99), 
                                               B_RGBA32, 0);
    
    // Check area was created
    EXPECT_GE(bitmap->AreaID(), 0);
    
    // Check buffer is valid
    EXPECT_NE(bitmap->Bits(), nullptr);
    
    // Check we can write to buffer
    memset(bitmap->Bits(), 0xFF, bitmap->BitsLength());
    EXPECT_EQ(bitmap->Bits()[0], 0xFF);
    
    delete bitmap;
}
```

**Риски Phase 1:**
- ⚠️ Может сломать cursors (они используют heap allocation для маленьких bitmap'ов)
- ⚠️ Может исчерпать address space при большом количестве маленьких bitmap'ов

**Решение:**
Добавить threshold: bitmap'ы < 4 KB остаются на heap, остальные → areas.

```cpp
void ServerBitmap::AllocateBuffer()
{
    uint32 length = BitsLength();
    if (length == 0)
        return;
    
    // Small bitmaps (< 4KB) stay on heap (for cursors)
    if (length < 4096) {
        fBuffer = new(std::nothrow) uint8[length];
        fAreaID[0] = -1;
        fMemory = NULL;
        return;
    }
    
    // Large bitmaps → areas (as above)
    // ...
}
```

---

## Phase 2: Double buffering для Window

### Цель:
Каждый Window имеет 2 буфера (front + back), client пишет в back, compositor читает из front.

### Файлы для изменения:

#### 2.1. `src/servers/app/ServerBitmap.cpp`

**Добавить в `AllocateBuffer()`:**
```cpp
void ServerBitmap::AllocateBuffer()
{
    uint32 length = BitsLength();
    if (length == 0 || length < 4096)
        return; // (см. Phase 1)
    
    // Allocate buffer 0
    char areaName[B_OS_NAME_LENGTH];
    snprintf(areaName, sizeof(areaName), "bitmap_%ldx%ld_buf0", fWidth, fHeight);
    
    void* address0 = NULL;
    fAreaID[0] = create_area(areaName, &address0, B_ANY_ADDRESS,
                             length, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
    if (fAreaID[0] < 0)
        return; // Error
    
    fBufferPtr[0] = (uint8*)address0;
    
    // Allocate buffer 1 (for double buffering)
    snprintf(areaName, sizeof(areaName), "bitmap_%ldx%ld_buf1", fWidth, fHeight);
    
    void* address1 = NULL;
    fAreaID[1] = create_area(areaName, &address1, B_ANY_ADDRESS,
                             length, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
    if (fAreaID[1] < 0) {
        // Fallback to single buffer
        fAreaID[1] = -1;
        fBufferPtr[1] = NULL;
    } else {
        fBufferPtr[1] = (uint8*)address1;
    }
    
    fCurrentBuffer = 0;
    fBuffer = fBufferPtr[0];  // Client writes here
    fSequenceNumber[0] = 0;
    fSequenceNumber[1] = 0;
    
    fMemory = new AreaMemory(fAreaID[0], address0, length);
}

void ServerBitmap::SwapBuffers()
{
    if (fAreaID[1] < 0)
        return;  // Single buffer mode
    
    fCurrentBuffer = 1 - fCurrentBuffer;
    fBuffer = fBufferPtr[fCurrentBuffer];
    fSequenceNumber[fCurrentBuffer]++;
}

uint8* ServerBitmap::GetBackBuffer()
{
    return fBufferPtr[fCurrentBuffer];  // Client writes here
}

uint8* ServerBitmap::GetFrontBuffer() const
{
    uint32 readBuffer = 1 - fCurrentBuffer;
    return fBufferPtr[readBuffer];  // Compositor reads here
}

uint64 ServerBitmap::GetSequenceNumber(uint32 buffer) const
{
    return fSequenceNumber[buffer];
}
```

#### 2.2. `src/servers/app/View.h`

**Добавить:**
```cpp
class View {
public:
    // ...existing...
    
    // NEW: Double buffering support
    void            BeginDraw();   // Swap buffers
    void            EndDraw();     // Increment sequence
    ServerBitmap*   GetBackBitmap();   // For drawing
    ServerBitmap*   GetFrontBitmap();  // For compositing
    
private:
    // ...existing...
    
    // NEW: Offscreen bitmap for layer
    ServerBitmap*   fOffscreenBitmap;  // ← Replaces DrawView direct drawing
    uint64          fLastCompositedSequence;
};
```

#### 2.3. `src/servers/app/View.cpp`

**Добавить:**
```cpp
void View::BeginDraw()
{
    if (fOffscreenBitmap) {
        fOffscreenBitmap->SwapBuffers();
    }
}

void View::EndDraw()
{
    if (fOffscreenBitmap) {
        // Sequence number already incremented in SwapBuffers()
        // Notify compositor
        Window* window = Window();
        if (window && window->Desktop()) {
            window->Desktop()->GetCompositor()->InvalidateLayer(this);
        }
    }
}

ServerBitmap* View::GetBackBitmap()
{
    return fOffscreenBitmap;  // Client writes here
}

ServerBitmap* View::GetFrontBitmap()
{
    return fOffscreenBitmap;  // Compositor reads from front buffer inside
}
```

#### 2.4. Тестирование Phase 2:

```cpp
TEST(ServerBitmap, DoubleBuffering) {
    UtilityBitmap* bitmap = new UtilityBitmap(BRect(0, 0, 999, 999), B_RGBA32, 0);
    
    // Check both buffers allocated
    EXPECT_GE(bitmap->fAreaID[0], 0);
    EXPECT_GE(bitmap->fAreaID[1], 0);
    EXPECT_NE(bitmap->fBufferPtr[0], nullptr);
    EXPECT_NE(bitmap->fBufferPtr[1], nullptr);
    
    // Check buffers are different
    EXPECT_NE(bitmap->fBufferPtr[0], bitmap->fBufferPtr[1]);
    
    // Write to back buffer
    uint8* back = bitmap->GetBackBuffer();
    memset(back, 0xAA, 100);
    
    // Swap
    uint64 seq1 = bitmap->GetSequenceNumber(bitmap->fCurrentBuffer);
    bitmap->SwapBuffers();
    uint64 seq2 = bitmap->GetSequenceNumber(bitmap->fCurrentBuffer);
    
    // Check sequence incremented
    EXPECT_EQ(seq2, seq1 + 1);
    
    // Check front buffer has old data
    uint8* front = bitmap->GetFrontBuffer();
    EXPECT_EQ(front[0], 0xAA);  // Old data now in front
    
    // Check back buffer is different
    uint8* newBack = bitmap->GetBackBuffer();
    EXPECT_NE(newBack, front);
    
    delete bitmap;
}
```

---

## Phase 3: Compositor thread

### Цель:
Создать Compositor class с отдельным потоком, который блендит layers в BackBuffer.

### Файлы для создания:

#### 3.1. `src/servers/app/Compositor.h` (НОВЫЙ ФАЙЛ)

```cpp
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <OS.h>
#include <Region.h>
#include <ObjectList.h>

class Desktop;
class View;
class Window;
class DrawingEngine;
class HWInterface;
class Painter;

class Compositor {
public:
                            Compositor(Desktop* desktop);
                            ~Compositor();
    
    status_t                Start();
    void                    Quit();
    
    // Called by Desktop when layer invalidated
    void                    InvalidateLayer(View* view);
    void                    InvalidateWindow(Window* window);
    
    // Main compositing loop
    static int32            _CompositorThread(void* data);
    
private:
    void                    _ComposeFrame();
    void                    _BlendLayer(View* view, BRegion& dirty);
    
    Desktop*                fDesktop;
    thread_id               fThread;
    sem_id                  fWakeupSem;
    bool                    fQuit;
    
    // Dirty tracking
    BRegion                 fDirtyRegion;
    BObjectList<View>       fDirtyLayers;
    
    // Locking
    MultiLocker             fLayerLock;  // Protect fDirtyLayers
};

#endif // COMPOSITOR_H
```

#### 3.2. `src/servers/app/Compositor.cpp` (НОВЫЙ ФАЙЛ)

```cpp
#include "Compositor.h"
#include "Desktop.h"
#include "Window.h"
#include "View.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "drawing/Painter/Painter.h"

#include <stdio.h>
#include <AutoLocker.h>

Compositor::Compositor(Desktop* desktop)
    :
    fDesktop(desktop),
    fThread(-1),
    fWakeupSem(-1),
    fQuit(false),
    fDirtyLayers(20, true)  // Owns objects = false
{
    fWakeupSem = create_sem(0, "compositor wakeup");
}

Compositor::~Compositor()
{
    Quit();
    
    if (fWakeupSem >= 0)
        delete_sem(fWakeupSem);
}

status_t Compositor::Start()
{
    if (fThread >= 0)
        return B_OK;  // Already started
    
    fQuit = false;
    fThread = spawn_thread(_CompositorThread, "compositor",
                          B_DISPLAY_PRIORITY, this);
    
    if (fThread < 0)
        return fThread;
    
    return resume_thread(fThread);
}

void Compositor::Quit()
{
    if (fThread < 0)
        return;
    
    fQuit = true;
    release_sem(fWakeupSem);  // Wake thread
    
    status_t returnCode;
    wait_for_thread(fThread, &returnCode);
    
    fThread = -1;
}

void Compositor::InvalidateLayer(View* view)
{
    if (!view)
        return;
    
    AutoLocker<MultiLocker> locker(fLayerLock);
    
    // Add view's bounds to dirty region
    Window* window = view->Window();
    if (window) {
        BRegion region;
        view->GetScreenClipping(&region);
        fDirtyRegion.Include(&region);
    }
    
    // Add to dirty layers list
    if (!fDirtyLayers.HasItem(view))
        fDirtyLayers.AddItem(view);
    
    // Wake compositor thread
    release_sem(fWakeupSem);
}

void Compositor::InvalidateWindow(Window* window)
{
    if (!window)
        return;
    
    View* topView = window->TopView();
    if (topView)
        InvalidateLayer(topView);
}

int32 Compositor::_CompositorThread(void* data)
{
    Compositor* compositor = (Compositor*)data;
    
    while (!compositor->fQuit) {
        // Wait for invalidate OR VSync timeout (16.67ms = 60 Hz)
        status_t result = acquire_sem_etc(compositor->fWakeupSem, 1,
                                          B_RELATIVE_TIMEOUT, 16667);
        
        if (compositor->fQuit)
            break;
        
        // Compose frame if dirty
        if (compositor->fDirtyRegion.CountRects() > 0) {
            compositor->_ComposeFrame();
        }
    }
    
    return 0;
}

void Compositor::_ComposeFrame()
{
    // Lock compositor state (read-only)
    if (!fLayerLock.ReadLock())
        return;
    
    HWInterface* hwInterface = fDesktop->HWInterface();
    if (!hwInterface) {
        fLayerLock.ReadUnlock();
        return;
    }
    
    RenderingBuffer* backBuffer = hwInterface->BackBuffer();
    if (!backBuffer) {
        fLayerLock.ReadUnlock();
        return;
    }
    
    // Create painter attached to BackBuffer
    Painter painter;
    painter.AttachToBuffer(backBuffer);
    painter.ConstrainClipping(&fDirtyRegion);
    
    // Blend layers bottom-to-top
    for (int32 i = 0; i < fDirtyLayers.CountItems(); i++) {
        View* view = fDirtyLayers.ItemAt(i);
        
        // Get view's screen clipping
        BRegion viewRegion;
        view->GetScreenClipping(&viewRegion);
        viewRegion.IntersectWith(&fDirtyRegion);
        
        if (viewRegion.CountRects() == 0)
            continue;  // View not in dirty region
        
        _BlendLayer(view, viewRegion);
    }
    
    fLayerLock.ReadUnlock();
    
    // Copy BackBuffer → FrontBuffer (EXISTING CODE!)
    DrawingEngine* engine = fDesktop->GetDrawingEngine();
    if (engine && engine->Lock()) {
        hwInterface->_CopyBackToFront(&fDirtyRegion);
        engine->Unlock();
    }
    
    // Clear dirty tracking
    fDirtyRegion.MakeEmpty();
    fDirtyLayers.MakeEmpty();
}

void Compositor::_BlendLayer(View* view, BRegion& dirty)
{
    ServerBitmap* bitmap = view->GetFrontBitmap();
    if (!bitmap)
        return;
    
    // Check if layer changed since last composite
    uint32 readBuffer = bitmap->GetReadBuffer();
    uint64 currentSeq = bitmap->GetSequenceNumber(readBuffer);
    
    if (currentSeq <= view->fLastCompositedSequence)
        return;  // Layer unchanged
    
    // Get front buffer (compositor reads, client writes to back)
    uint8* bits = bitmap->GetFrontBuffer();
    if (!bits)
        return;
    
    // Blend layer into BackBuffer
    HWInterface* hwInterface = fDesktop->HWInterface();
    Painter painter;
    painter.AttachToBuffer(hwInterface->BackBuffer());
    painter.SetDrawingMode(B_OP_ALPHA);
    painter.SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
    
    for (int32 j = 0; j < dirty.CountRects(); j++) {
        BRect rect = dirty.RectAt(j);
        
        // Create temp BBitmap wrapper for Painter
        BBitmap tempBitmap(bitmap->Bounds(), bitmap->ColorSpace(),
                          bits, bitmap->BytesPerRow());
        
        painter.DrawBitmap(&tempBitmap, rect, rect);
    }
    
    // Mark as composited
    view->fLastCompositedSequence = currentSeq;
}
```

#### 3.3. `src/servers/app/Desktop.h`

**Добавить:**
```cpp
class Desktop {
public:
    // ...existing...
    
    // NEW: Compositor support
    Compositor*     GetCompositor() { return fCompositor; }
    
private:
    // ...existing...
    
    // NEW:
    Compositor*     fCompositor;
};
```

#### 3.4. `src/servers/app/Desktop.cpp`

**Инициализация:**
```cpp
Desktop::Desktop(uid_t userID, const char* targetScreen)
    :
    // ...existing...
    fCompositor(NULL)
{
    // ...existing init...
}

status_t Desktop::Init()
{
    // ...existing init...
    
    // Create and start compositor
    fCompositor = new Compositor(this);
    status_t status = fCompositor->Start();
    if (status != B_OK) {
        printf("Desktop: failed to start compositor: %s\n", strerror(status));
        delete fCompositor;
        fCompositor = NULL;
        // Continue without compositor (fallback to direct drawing)
    }
    
    return B_OK;
}

Desktop::~Desktop()
{
    if (fCompositor) {
        fCompositor->Quit();
        delete fCompositor;
    }
    
    // ...existing cleanup...
}
```

#### 3.5. Тестирование Phase 3:

```cpp
TEST(Compositor, StartsAndStops) {
    MockDesktop desktop;
    Compositor compositor(&desktop);
    
    status_t status = compositor.Start();
    EXPECT_EQ(status, B_OK);
    
    // Wait a bit
    snooze(100000);  // 100ms
    
    compositor.Quit();
    
    // Check thread stopped
    EXPECT_LT(compositor.fThread, 0);
}

TEST(Compositor, InvalidateWakesThread) {
    MockDesktop desktop;
    Compositor compositor(&desktop);
    compositor.Start();
    
    MockView view(BRect(0, 0, 100, 100));
    
    bigtime_t start = system_time();
    compositor.InvalidateLayer(&view);
    
    // Wait for compositor to process
    snooze(5000);  // 5ms
    
    bigtime_t elapsed = system_time() - start;
    
    // Should wake quickly (< 10ms), not wait for VSync (16.67ms)
    EXPECT_LT(elapsed, 10000);
    
    compositor.Quit();
}
```

---

## Phase 4: Интеграция с Desktop

### Цель:
Перенаправить invalidation flow через compositor вместо прямого рисования.

### Файлы для изменения:

#### 4.1. `src/servers/app/Window.cpp`

**Текущий код (ProcessDirtyRegion):**
```cpp
void Window::ProcessDirtyRegion(const BRegion& dirtyRegion,
                                const BRegion& exposeRegion)
{
    // ...existing code draws directly...
    GetDrawingEngine()->Lock();
    // ...draw to BackBuffer...
    GetDrawingEngine()->Unlock();
}
```

**Изменить на:**
```cpp
void Window::ProcessDirtyRegion(const BRegion& dirtyRegion,
                                const BRegion& exposeRegion)
{
    // Check if compositor enabled
    if (fDesktop && fDesktop->GetCompositor()) {
        // NEW PATH: Notify compositor
        fDesktop->GetCompositor()->InvalidateWindow(this);
    } else {
        // OLD PATH: Direct drawing (fallback)
        // ...existing code...
    }
}
```

#### 4.2. `src/servers/app/View.cpp`

**Текущий код (Draw):**
```cpp
void View::Draw(DrawingEngine* engine, BRegion* effectiveClipping, ...)
{
    engine->FillRegion(effectiveClipping, fViewColor);
    // ...draw view...
}
```

**Изменить на:**
```cpp
void View::Draw(DrawingEngine* engine, BRegion* effectiveClipping, ...)
{
    // Check if should draw to offscreen bitmap
    if (fOffscreenBitmap) {
        // NEW PATH: Draw to offscreen bitmap
        BeginDraw();  // Swap buffers
        
        Painter painter;
        RenderingBuffer buffer(fOffscreenBitmap->GetBackBuffer(),
                              fOffscreenBitmap->Width(),
                              fOffscreenBitmap->Height(),
                              fOffscreenBitmap->BytesPerRow());
        painter.AttachToBuffer(&buffer);
        painter.ConstrainClipping(effectiveClipping);
        painter.FillRegion(effectiveClipping, fViewColor);
        // ...draw view to offscreen bitmap...
        
        EndDraw();  // Increment sequence, notify compositor
    } else {
        // OLD PATH: Direct drawing (fallback)
        engine->FillRegion(effectiveClipping, fViewColor);
        // ...draw view...
    }
}
```

#### 4.3. Тестирование Phase 4:

```cpp
TEST(Desktop, InvalidationGoesToCompositor) {
    Desktop desktop(0, "test");
    desktop.Init();
    
    Window* window = CreateTestWindow(BRect(0, 0, 100, 100));
    desktop.AddWindow(window);
    
    // Invalidate window
    BRegion dirty(window->Frame());
    window->ProcessDirtyRegion(dirty, dirty);
    
    // Check compositor received invalidation
    Compositor* compositor = desktop.GetCompositor();
    ASSERT_NE(compositor, nullptr);
    
    AutoLocker<MultiLocker> locker(compositor->fLayerLock);
    EXPECT_GT(compositor->fDirtyRegion.CountRects(), 0);
    EXPECT_GT(compositor->fDirtyLayers.CountItems(), 0);
}
```

---

## Phase 5: VSync fence (опционально)

### Цель:
Заменить таймер (16.67ms) на hardware VSync event от драйвера.

### Файлы для изменения:

#### 5.1. `src/servers/app/drawing/interface/HWInterface.h`

**Добавить:**
```cpp
class HWInterface {
public:
    // ...existing...
    
    // NEW: VSync support
    virtual bigtime_t   WaitForVBlank();
    virtual sem_id      VBlankSemaphore();
};
```

#### 5.2. `src/servers/app/drawing/interface/local/AccelerantHWInterface.h`

**Добавить:**
```cpp
class AccelerantHWInterface : public HWInterface {
public:
    // ...existing...
    
    // NEW: VSync implementation
    virtual bigtime_t   WaitForVBlank();
    virtual sem_id      VBlankSemaphore();
    
private:
    sem_id              fVBlankSem;
};
```

#### 5.3. `src/servers/app/drawing/interface/local/AccelerantHWInterface.cpp`

**Добавить:**
```cpp
bigtime_t AccelerantHWInterface::WaitForVBlank()
{
    if (fVBlankSem < 0) {
        // Fallback to timer
        snooze(16667);  // 60 Hz
        return system_time();
    }
    
    // Wait for hardware VBlank interrupt
    acquire_sem(fVBlankSem);
    return system_time();
}

sem_id AccelerantHWInterface::VBlankSemaphore()
{
    if (fVBlankSem >= 0)
        return fVBlankSem;
    
    // Try to get retrace semaphore from accelerant
    if (fAccelerantHook && fAccelerantHook(ACCELERANT_RETRACE_SEMAPHORE, NULL)) {
        sem_id* sem = (sem_id*)fAccelerantHook(ACCELERANT_RETRACE_SEMAPHORE, NULL);
        if (sem) {
            fVBlankSem = *sem;
            return fVBlankSem;
        }
    }
    
    return -1;  // Not supported
}
```

#### 5.4. `src/servers/app/Compositor.cpp`

**Изменить _CompositorThread:**
```cpp
int32 Compositor::_CompositorThread(void* data)
{
    Compositor* compositor = (Compositor*)data;
    
    // Try to get VSync semaphore
    HWInterface* hwInterface = compositor->fDesktop->HWInterface();
    sem_id vblankSem = hwInterface ? hwInterface->VBlankSemaphore() : -1;
    
    while (!compositor->fQuit) {
        if (vblankSem >= 0) {
            // NEW: Wait for hardware VSync
            acquire_sem(vblankSem);
        } else {
            // OLD: Wait for invalidate OR timer (16.67ms)
            acquire_sem_etc(compositor->fWakeupSem, 1,
                           B_RELATIVE_TIMEOUT, 16667);
        }
        
        if (compositor->fQuit)
            break;
        
        // Compose frame if dirty
        if (compositor->fDirtyRegion.CountRects() > 0) {
            compositor->_ComposeFrame();
        }
    }
    
    return 0;
}
```

---

## Порядок внедрения

### Week 1: Phase 1
- [ ] Изменить `ServerBitmap.h` (добавить `fAreaID[]`, `fBufferPtr[]`)
- [ ] Изменить `ServerBitmap::AllocateBuffer()` (всегда areas)
- [ ] Добавить threshold (< 4KB → heap, >= 4KB → areas)
- [ ] Изменить деструктор (`delete_area` вместо `delete[]`)
- [ ] Тестировать: `ServerBitmapTest::AlwaysUsesAreas`
- [ ] Проверить cursors работают (маленькие bitmap'ы < 4KB на heap)

### Week 2: Phase 2
- [ ] Изменить `ServerBitmap::AllocateBuffer()` (создавать 2 areas)
- [ ] Добавить `ServerBitmap::SwapBuffers()`
- [ ] Добавить `ServerBitmap::GetFrontBuffer()` / `GetBackBuffer()`
- [ ] Добавить `View::BeginDraw()` / `EndDraw()`
- [ ] Тестировать: `ServerBitmapTest::DoubleBuffering`

### Week 3-4: Phase 3
- [ ] Создать `Compositor.h` / `Compositor.cpp`
- [ ] Добавить `_CompositorThread()` с semaphore + timer
- [ ] Добавить `InvalidateLayer()` / `InvalidateWindow()`
- [ ] Добавить `_ComposeFrame()` + `_BlendLayer()`
- [ ] Интегрировать в `Desktop::Init()`
- [ ] Тестировать: `CompositorTest::StartsAndStops`
- [ ] Тестировать: `CompositorTest::InvalidateWakesThread`

### Week 5: Phase 4
- [ ] Изменить `Window::ProcessDirtyRegion()` (через compositor)
- [ ] Изменить `View::Draw()` (в offscreen bitmap)
- [ ] Добавить fallback (если compositor = NULL, старый путь)
- [ ] Тестировать: `DesktopTest::InvalidationGoesToCompositor`
- [ ] Regression тесты (проверить ничего не сломалось)

### Week 6: Phase 5 (опционально)
- [ ] Добавить `HWInterface::WaitForVBlank()`
- [ ] Реализовать `AccelerantHWInterface::VBlankSemaphore()`
- [ ] Изменить `Compositor::_CompositorThread()` (использовать VSync)
- [ ] Тестировать на real hardware с VSync support

---

## Риски и митигации

| Риск | Вероятность | Митигация |
|------|------------|-----------|
| **Address space exhaustion** (много маленьких areas) | Средняя | Threshold: < 4KB → heap, >= 4KB → areas |
| **Memory overhead** (double buffering = 2× RAM) | Низкая | Приемлемо для 2025 года (160 MB для 10 окон) |
| **Performance regression** (compositor медленнее прямого рисования) | Средняя | Профилирование, fallback на старый путь если compositor = NULL |
| **Race conditions** (client/compositor трогают один буфер) | Низкая | Double buffering + sequence numbers предотвращают это |
| **Deadlock** (fLayerLock + fWindowLock) | Средняя | Чёткий порядок захвата: всегда fWindowLock → fLayerLock |
| **VSync не поддерживается драйвером** | Высокая | Fallback на timer (16.67ms) |
| **Cursors сломаются** (маленькие bitmap'ы) | Средняя | Threshold: cursors (< 4KB) остаются на heap |
| **Existing code ломается** (зависимость от heap pointer) | Низкая | Backward compatibility: `fBuffer` остаётся указателем на текущий буфер |

---

## Критерии успеха

### Функциональные:
- ✅ ВСЕ bitmap'ы (>= 4KB) используют areas
- ✅ ВСЕ top-level windows имеют double buffering
- ✅ Compositor thread запускается и обрабатывает invalidations
- ✅ Sequence numbers предотвращают "lost update"
- ✅ Нет race conditions (client/compositor в разных буферах)
- ✅ Fallback работает (compositor = NULL → старый путь)

### Производительность:
- ✅ 1 layer invalidate → composite < 1ms
- ✅ 10 layers full screen composite < 10ms
- ✅ Compositor wakeup latency < 1ms
- ✅ Нет регрессии FPS (60 FPS с compositor >= 60 FPS без)

### Память:
- ✅ Memory overhead приемлемый (160 MB для 10 Full HD окон)
- ✅ Маленькие bitmap'ы (< 4KB) остаются на heap (нет exhaustion)
- ✅ Areas корректно освобождаются (нет утечек)

---

## Откат (Rollback)

Если Phase 3-4 провалились, можно откатить:

```cpp
// Desktop.cpp
status_t Desktop::Init()
{
    // ...existing...
    
    // TRY to create compositor
    fCompositor = new Compositor(this);
    status_t status = fCompositor->Start();
    if (status != B_OK) {
        printf("Desktop: compositor failed, falling back to direct drawing\n");
        delete fCompositor;
        fCompositor = NULL;
        // Continue with old path (direct drawing)
    }
    
    return B_OK;
}

// Window.cpp
void Window::ProcessDirtyRegion(...)
{
    if (fDesktop && fDesktop->GetCompositor()) {
        // NEW: Compositor path
        fDesktop->GetCompositor()->InvalidateWindow(this);
    } else {
        // OLD: Direct drawing (FALLBACK)
        // ...existing code untouched...
    }
}
```

Phase 1-2 (areas + double buffering) НЕ откатываются, т.к. они не влияют на функциональность (только подготовка).

---

## Следующие шаги

После завершения всех фаз:

1. **GPU compositor** (OpenGL/Vulkan)
   - Заменить `area_id` на `fd` (DMA-BUF)
   - Заменить `Painter` на `glDrawTexture()`
   - Всё в VRAM, 0 копирований RAM ↔ VRAM

2. **Layer caching optimization**
   - Не перерисовывать layer если не изменился (уже есть: sequence numbers)
   - Кешировать результат блендинга (composite cache)

3. **Advanced effects**
   - Shadows, blur (только в compositor, без изменения client code)
   - Transitions, animations (compositor управляет)

4. **Multi-monitor compositor**
   - Один compositor per monitor
   - Или один compositor, multiple BackBuffers

---

## Вопросы для обсуждения

1. **Threshold для areas:** 4 KB правильный порог? Может быть 8 KB?
2. **Double buffering для child views:** Нужен ли? Или только top-level windows?
3. **Fallback стратегия:** Всегда держать старый путь или удалить после стабилизации?
4. **VSync priority:** Phase 5 обязательна или опциональна?
5. **Memory limit:** Нужен ли лимит на количество double-buffered windows? (например, max 20)

---

**Этот план готов к выполнению. Начинать с Phase 1?**
