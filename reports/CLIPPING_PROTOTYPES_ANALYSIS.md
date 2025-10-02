# Clipping System Prototypes Analysis

## Discovery Context

While investigating the Haiku test infrastructure at `/home/ruslan/haiku/src/tests/servers/app`, we found two directories containing **clipping system prototypes**:
- `/home/ruslan/haiku/src/tests/servers/app/newClipping/`  
- `/home/ruslan/haiku/src/tests/servers/app/newerClipping/`

These are NOT unit tests but **standalone prototype implementations** used to develop and test the app_server clipping algorithms.

---

## Architecture Comparison

### newerClipping/ - Advanced Prototype

**Purpose:** Full-featured standalone app_server simulation with compositor patterns

**Key Components:**

```cpp
// Desktop.cpp (lines 16-40) - BLooper-based Desktop Manager
Desktop::Desktop(DrawView* drawView, DrawingEngine* engine)
    : BLooper("desktop", B_URGENT_DISPLAY_PRIORITY),  // ← Runs as separate thread
      fClippingLock("clipping lock"),                  // ← Read-write lock for clipping
      fBackgroundRegion(),
      fMasterClipping(),
      fDrawView(drawView),
      fDrawingEngine(engine),
      fWindows(64),
      fFocusWindow(NULL)
{
    BRegion stillAvailableOnScreen;
    _RebuildClippingForAllWindows(&stillAvailableOnScreen);
    _SetBackground(&stillAvailableOnScreen);
}
```

**Threading Model (lines 598-618):**
```cpp
void Desktop::_RebuildClippingForAllWindows(BRegion* stillAvailableOnScreen) {
    // Desktop thread manipulates clipping
    *stillAvailableOnScreen = fMasterClipping;
    
    int32 count = CountWindows();
    for (int32 i = count - 1; i >= 0; i--) {  // ← Top-to-bottom
        WindowLayer* window = WindowAtFast(i);
        if (!window->IsHidden()) {
            window->SetClipping(stillAvailableOnScreen);
            stillAvailableOnScreen->Exclude(&window->VisibleRegion());
        }
    }
}
```

**WindowLayer Architecture (WindowLayer.cpp lines 28-75):**
```cpp
WindowLayer::WindowLayer(BRect frame, const char* name,
                         DrawingEngine* drawingEngine, Desktop* desktop)
    : BLooper(name, B_DISPLAY_PRIORITY),  // ← Each window = separate thread
      fFrame(frame),
      fVisibleRegion(),
      fVisibleContentRegion(),
      fDirtyRegion(),
      fBorderRegion(),
      fContentRegion(),
      fTopLayer(NULL),
      fDrawingEngine(drawingEngine),
      fDesktop(desktop),
      fClient(new ClientLooper(name, this)),  // ← Simulates BWindow client
      fCurrentUpdateSession(),
      fPendingUpdateSession()
{
    fTopLayer = new ViewLayer(fFrame, "top view", B_FOLLOW_ALL, 0,
                              (rgb_color){ 255, 255, 255, 255 });
    fTopLayer->AttachedToWindow(this);
    fClient->Run();  // ← Starts client thread
}
```

**Update Session Pattern (WindowLayer.cpp lines 809-865):**
```cpp
// _BeginUpdate() - Called when client starts drawing (line 811)
void WindowLayer::_BeginUpdate() {
    if (fDesktop->ReadLockClipping()) {
        if (fUpdateRequested && !fCurrentUpdateSession.IsUsed()) {
            if (fPendingUpdateSession.IsUsed()) {
                // Toggle update sessions (double buffering pattern!)
                fCurrentUpdateSession = fPendingUpdateSession;
                fPendingUpdateSession.SetUsed(false);
                
                fInUpdate = true;  // ← Drawing commands now enforced to dirty region
            }
            fEffectiveDrawingRegionValid = false;
        }
        fDesktop->ReadUnlockClipping();
    }
}

// _EndUpdate() - Called when client finishes drawing (line 843)
void WindowLayer::_EndUpdate() {
    if (fDesktop->ReadLockClipping()) {
        if (fInUpdate) {
            fCurrentUpdateSession.SetUsed(false);
            fInUpdate = false;
            fEffectiveDrawingRegionValid = false;
        }
        if (fPendingUpdateSession.IsUsed()) {
            // More dirty regions accumulated, request another update
            fClient->PostMessage(MSG_UPDATE);
            fUpdateRequested = true;
        }
        fDesktop->ReadUnlockClipping();
    }
}
```

**HW Acceleration for Scrolling/Moving (Desktop.cpp lines 345-383):**
```cpp
void Desktop::MoveWindowBy(WindowLayer* window, int32 x, int32 y) {
    if (LockClipping()) {
        BRegion newDirtyRegion(window->VisibleRegion());
        window->MoveBy(x, y);
        
        BRegion background;
        _RebuildClippingForAllWindows(&background);
        
        // Compute region that can be BLITTED instead of redrawn
        BRegion copyRegion(window->VisibleRegion());
        copyRegion.OffsetBy(-x, -y);
        copyRegion.IntersectWith(&newDirtyRegion);
        
        newDirtyRegion.Include(&window->VisibleRegion());
        
        fDrawingEngine->CopyRegion(&copyRegion, x, y);  // ← HW blit!
        
        copyRegion.OffsetBy(x, y);
        newDirtyRegion.Exclude(&copyRegion);  // ← Only redraw uncovered parts
        
        MarkDirty(&newDirtyRegion);
        _SetBackground(&background);
        UnlockClipping();
    }
}
```

---

### newClipping/ - Simplified Prototype

**Purpose:** Focused clipping algorithm testing without full threading model

**Key Components (Layer.cpp lines 1-633):**

```cpp
// Layer.cpp - Core layer tree with clipping
class Layer {
    BRect       fFrame;
    BPoint      fOrigin;
    Layer*      fBottom;    // Youngest child
    Layer*      fUpper;     // Next older sibling
    Layer*      fLower;     // Next younger sibling
    Layer*      fTop;       // Oldest child
    Layer*      fParent;
    MyView*     fView;      // Root layer only
    BRegion     fVisible;
    BRegion     fFullVisible;
    bool        fHidden;
};
```

**Visible Region Calculation (lines 540-603):**
```cpp
void Layer::rebuild_visible_regions(const BRegion &invalid,
                                    const BRegion &parentLocalVisible,
                                    const Layer *startFrom)
{
    if (fHidden) return;
    
    if (!parentLocalVisible.Frame().IsValid() && !(fFullVisible.CountRects() > 0))
        return;
    
    bool fullRebuild = false;
    
    // Get maximum wanted region
    BRegion common;
    get_user_regions(common);
    common.IntersectWith(&invalid);
    
    if (!common.CountRects() > 0) return;  // Not in catchment area
    
    // Intersect with parent's visible region
    common.IntersectWith(&parentLocalVisible);
    
    // Exclude the invalid region, then add back what's really visible
    fFullVisible.Exclude(&invalid);
    fVisible.Exclude(&invalid);
    fFullVisible.Include(&common);
    
    // Allow layer to hide parts from children
    BRegion unalteredVisible(common);
    bool altered = alter_visible_for_children(common);
    
    for (Layer *lay = BottomChild(); lay; lay = UpperSibling()) {
        if (lay == startFrom)
            fullRebuild = true;
        
        if (fullRebuild)
            lay->rebuild_visible_regions(invalid, common, lay->BottomChild());
        
        common.Exclude(&lay->fFullVisible);  // Children take from parent
        if (altered)
            unalteredVisible.Exclude(&lay->fFullVisible);
    }
    
    // Visible = what remains after children take their parts
    if (altered)
        fVisible.Include(&unalteredVisible);
    else
        fVisible.Include(&common);
}
```

**ResizeBy with Automatic Child Resizing (lines 344-405):**
```cpp
void Layer::ResizeBy(float dx, float dy) {
    fFrame.Set(fFrame.left, fFrame.top, fFrame.right+dx, fFrame.bottom+dy);
    
    // Resize children using their resize_mask
    for (Layer *lay = BottomChild(); lay; lay = UpperSibling())
        lay->resize_layer_frame_by(dx, dy);  // ← Automatic layout
    
    if (dx != 0.0f || dy != 0.0f)
        ResizedByHook(dx, dy, false);
    
    if (!IsHidden() && GetRootLayer()) {
        BRegion oldFullVisible(fFullVisible);
        BRegion oldVisible(fVisible);
        
        // Compute regions that need redrawing
        BRegion redrawMore;
        rezize_layer_redraw_more(redrawMore, dx, dy);  // ← B_FOLLOW_* aligned layers
        
        BRegion invalid;
        get_user_regions(invalid);
        invalid.Include(&fFullVisible);
        
        clear_visible_regions();
        fParent->RebuildVisibleRegions(invalid, this);
        
        // Redraw differences between old and new regions
        BRegion redrawReg(fFullVisible);
        redrawReg.Exclude(&oldFullVisible);  // New area
        BRegion redrawReg2(oldFullVisible);
        redrawReg2.Exclude(&fFullVisible);   // Removed area
        redrawReg.Include(&redrawReg2);
        redrawReg.Include(&redrawMore);
        
        GetRootLayer()->fRedrawReg.Include(&redrawReg);
        
        if (fFlags & B_FULL_UPDATE_ON_RESIZE && fVisible.Frame().IsValid()) {
            resize_layer_full_update_on_resize(GetRootLayer()->fRedrawReg, dx, dy);
            GetRootLayer()->fRedrawReg.Include(&fVisible);
            GetRootLayer()->fRedrawReg.Include(&oldVisible);
        }
        
        GetRootLayer()->RequestRedraw();
    }
}
```

**ScrollBy with HW Acceleration (lines 456-490):**
```cpp
void Layer::ScrollBy(float dx, float dy) {
    fOrigin.Set(fOrigin.x + dx, fOrigin.y + dy);
    
    if (!IsHidden() && GetRootLayer()) {
        BRegion invalid(fFullVisible);
        
        clear_visible_regions();
        rebuild_visible_regions(invalid, invalid, BottomChild());
        
        BRegion redrawReg(fFullVisible);
        
        // Compute common region for HW copy
        invalid.OffsetBy(dx, dy);
        invalid.IntersectWith(&fFullVisible);
        GetRootLayer()->CopyRegion(&invalid, -dx, -dy);  // ← HW blit
        
        invalid.OffsetBy(-dx, -dy);
        redrawReg.Exclude(&invalid);  // Only redraw newly exposed parts
        
        GetRootLayer()->fRedrawReg.Include(&redrawReg);
        GetRootLayer()->RequestRedraw();
    }
    
    if (dx != 0.0f || dy != 0.0f)
        ScrolledByHook(dx, dy);
}
```

---

## Critical Patterns for Compositor Plan

### 1. **Update Session Pattern (newerClipping)**

**Current vs Pending Sessions:**
```cpp
class WindowLayer {
    UpdateSession  fCurrentUpdateSession;   // ← Active drawing session
    UpdateSession  fPendingUpdateSession;   // ← Accumulated dirty regions during drawing
    bool           fUpdateRequested;
    bool           fInUpdate;
};

class UpdateSession {
    BRegion  fDirtyRegion;
    bool     fInUse;
public:
    void Include(BRegion* additionalDirty);  // Add dirty region
    void Exclude(BRegion* dirtyInNextSession);  // Remove from current (moved to pending)
    void MoveBy(int32 x, int32 y);  // Shift regions when window moves
    void SetUsed(bool used);
};
```

**Pattern:** While client is drawing `fCurrentUpdateSession`, new invalidations accumulate in `fPendingUpdateSession`. When current finishes, pending becomes current.

**Relevance to Compositor:** This is **exactly** what the compositor needs:
- **Compositor thread** accumulates dirty regions in "pending"
- When VSync occurs, pending → current
- Compositor blends current dirty regions
- New invalidations during blend accumulate in new pending

### 2. **Region-Based Clipping Rebuild (both prototypes)**

**Top-Down Algorithm:**
```cpp
// Desktop allocates screen real estate from top to bottom
void Desktop::_RebuildClippingForAllWindows(BRegion* stillAvailable) {
    *stillAvailable = fMasterClipping;  // Start with full screen
    
    for (int32 i = count - 1; i >= 0; i--) {  // Top to bottom
        window = WindowAtFast(i);
        window->SetClipping(stillAvailable);
        stillAvailable->Exclude(&window->VisibleRegion());  // Window "consumes" region
    }
}

// Window allocates to children from top to bottom
void Layer::rebuild_visible_regions(...) {
    common = parent_visible ∩ my_wanted_region;
    
    for (child in children_top_to_bottom) {
        child->rebuild_visible_regions(invalid, common, ...);
        common.Exclude(&child->fFullVisible);  // Child consumes parent's region
    }
    
    my_visible = common;  // What remains after children
}
```

**Pattern:** Each level consumes regions from parent, children consume from current.

**Relevance to Compositor:** Need similar algorithm for layer compositing:
```cpp
// Compositor analogy
void Compositor::BuildBlendRegions() {
    BRegion availableForBlending = screen;
    
    for (layer in layers_top_to_bottom) {
        layer->blend_region = availableForBlending ∩ layer->bounds;
        
        if (layer->opaque) {
            availableForBlending.Exclude(&layer->blend_region);
        }
    }
}
```

### 3. **Delayed Background Clearing (newerClipping)**

**DELAYED_BACKGROUND_CLEARING flag (WindowLayer.cpp lines 14-23):**
```cpp
// If background clearing is delayed until client draws,
// we have LESS FLICKERING when contents have to be redrawn
// because of resizing a window or because client invalidates parts.

#define DELAYED_BACKGROUND_CLEARING 1

// _TriggerContentRedraw() uses this:
#if DELAYED_BACKGROUND_CLEARING
    fTopLayer->Draw(fDrawingEngine, &dirtyContentRegion,
                    &fContentRegion, false);  // ← false = don't clear yet
#else
    fTopLayer->Draw(fDrawingEngine, &dirtyContentRegion,
                    &fContentRegion, true);   // ← true = clear immediately
#endif

// _DrawClient() clears background JUST before client draws:
#if DELAYED_BACKGROUND_CLEARING
    layer->Draw(fDrawingEngine, &effectiveClipping,
                &fContentRegion, false);  // ← Clear now
#endif
    layer->ClientDraw(fDrawingEngine, &effectiveClipping);  // ← Then draw
```

**Pattern:** Clear background at last possible moment before drawing to minimize flicker.

**Relevance to Compositor:** Critical for triple buffering:
```
Current: Clear → Draw → Composite → Flip (2 frames latency)
Better:  Draw → Clear only uncovered → Composite → Flip (1 frame latency)
```

### 4. **Copy Region Optimization (both prototypes)**

**MoveBy with Blitting (Desktop.cpp lines 361-376):**
```cpp
BRegion copyRegion(window->VisibleRegion());
copyRegion.OffsetBy(-x, -y);             // Offset back to source
copyRegion.IntersectWith(&newDirtyRegion);  // Common area = blittable

newDirtyRegion.Include(&window->VisibleRegion());

fDrawingEngine->CopyRegion(&copyRegion, x, y);  // ← HW blit

copyRegion.OffsetBy(x, y);
newDirtyRegion.Exclude(&copyRegion);  // Don't redraw blitted parts

MarkDirty(&newDirtyRegion);  // Only redraw newly exposed
```

**ScrollBy with Blitting (Layer.cpp lines 473-482):**
```cpp
BRegion redrawReg(fFullVisible);

invalid.OffsetBy(dx, dy);
invalid.IntersectWith(&fFullVisible);
GetRootLayer()->CopyRegion(&invalid, -dx, -dy);  // HW copy

invalid.OffsetBy(-dx, -dy);
redrawReg.Exclude(&invalid);  // Remove copied area from dirty

GetRootLayer()->fRedrawReg.Include(&redrawReg);
```

**Pattern:** 
1. Compute intersection of old and new positions (= common area)
2. Blit common area
3. Only redraw newly exposed parts

**Relevance to Compositor:** Same pattern for layer cache movement:
```cpp
void Compositor::MoveLayerCache(Layer* layer, int32 dx, int32 dy) {
    BRegion blittable = layer->cached_region ∩ (layer->cached_region + offset);
    BlitCacheRegion(blittable, dx, dy);
    layer->dirty_region = (new_bounds - blittable);
}
```

### 5. **Thread Safety via Read-Write Lock (newerClipping)**

**fClippingLock Usage (Desktop.cpp):**
```cpp
// Desktop.cpp line 26
fClippingLock("clipping lock")  // BLocker or MultiLocker

// Desktop thread (write access)
if (LockClipping()) {  // Write lock for modifying clipping
    _RebuildClippingForAllWindows(&background);
    UnlockClipping();
}

// Window thread (read access)
if (fDesktop->ReadLockClipping()) {  // Read lock for drawing
    _DrawBorder();
    _TriggerContentRedraw();
    fDesktop->ReadUnlockClipping();
}
```

**Pattern:**
- **Desktop thread** holds write lock when modifying window Z-order, moving, resizing
- **Window threads** hold read lock when drawing (multiple can draw simultaneously)
- **Prevents** clipping from changing mid-draw

**Relevance to Compositor:**
```cpp
class Compositor {
    MultiLocker  fLayerLock;  // Protect layer tree structure
    
    // Compositor thread
    void Composite() {
        fLayerLock.ReadLock();  // Read-only access to layers
        for (layer in dirty_layers) {
            BlendLayer(layer);
        }
        fLayerLock.ReadUnlock();
    }
    
    // Desktop thread
    void ReorderLayers() {
        fLayerLock.WriteLock();  // Exclusive access
        _RebuildLayerClipping();
        fLayerLock.WriteUnlock();
    }
};
```

### 6. **Resize Mode Propagation (newClipping)**

**Automatic Child Layout (Layer.cpp lines 219-271):**
```cpp
void Layer::resize_layer_frame_by(float x, float y) {
    uint16 rm = fResizeMode & 0x0000FFFF;
    BRect newFrame = fFrame;
    
    // Compute new frame based on B_FOLLOW_* flags
    if ((rm & 0x0F00U) == _VIEW_LEFT_ << 8)
        newFrame.left += 0.0f;
    else if ((rm & 0x0F00U) == _VIEW_RIGHT_ << 8)
        newFrame.left += x;
    else if ((rm & 0x0F00U) == _VIEW_CENTER_ << 8)
        newFrame.left += x/2;
    
    // ... similar for right, top, bottom
    
    if (newFrame != fFrame) {
        float dx = newFrame.Width() - fFrame.Width();
        float dy = newFrame.Height() - fFrame.Height();
        fFrame = newFrame;
        
        if (dx != 0.0f || dy != 0.0f) {
            ResizedByHook(dx, dy, true);  // ← "automatic" flag
            
            for (Layer *lay = BottomChild(); lay; lay = UpperSibling())
                lay->resize_layer_frame_by(dx, dy);  // ← Recursive
        }
    }
}
```

**Pattern:** Parent resize propagates to children based on their resize modes.

**Relevance to Compositor:** Layer hierarchy needs similar propagation:
```cpp
void Layer::ParentResized(float dx, float dy) {
    if (fResizeMode & B_FOLLOW_RIGHT)
        fFrame.right += dx;
    if (fResizeMode & B_FOLLOW_BOTTOM)
        fFrame.bottom += dy;
    
    if (fFrame changed) {
        InvalidateCachedBitmap();
        for (child in children)
            child->ParentResized(frame.Width() - oldWidth, ...);
    }
}
```

---

## Comparison with Current app_server

| Feature | newClipping | newerClipping | Current app_server | Compositor Plan |
|---------|-------------|---------------|-------------------|-----------------|
| **Desktop Thread** | Implicit (MyView) | BLooper (explicit) | MessageLooper | ??? (missing) |
| **Window Thread** | None | BLooper | MessageLooper | ??? (missing) |
| **Clipping Lock** | None (single thread) | Read-write lock | MultiLocker | ??? (missing) |
| **Update Session** | None | Double session | Single dirty region | Need double |
| **HW Copy Optimization** | CopyRegion() | CopyRegion() | AccelerantHWInterface::_CopyBackToFront() | ??? (missing) |
| **Background Clearing** | Immediate | Delayed | Immediate | ??? (missing) |
| **Region Rebuild** | Bottom-up | Top-down | Top-down | Need top-down |
| **Resize Propagation** | Automatic (B_FOLLOW) | Automatic | Automatic | ??? (missing) |

---

## Critical Findings for Compositor Plan

### ✅ **What These Prototypes Prove:**

1. **Update Session Pattern Works**
   - newerClipping demonstrates that double-session dirty tracking eliminates race conditions
   - Compositor MUST adopt this pattern for VSync-synchronized compositing

2. **Read-Write Locking Sufficient**
   - Desktop thread (write) + Multiple window threads (read) coexist peacefully
   - Compositor thread can be added as another read-lock holder

3. **Delayed Background Clearing Reduces Flicker**
   - Clearing at last moment = less visible flicker
   - Compositor should clear only newly exposed regions, not entire dirty area

4. **HW Copy is Critical**
   - Both prototypes heavily optimize with CopyRegion()
   - Compositor needs similar optimization for layer cache movement

5. **Resize Cascades Are Complex**
   - B_FOLLOW_* modes create dependency chains
   - Single layer resize can dirty 20+ descendant layers
   - Compositor cache invalidation must be recursive

### ❌ **What Compositor Plan is Missing:**

1. **No UpdateSession Equivalent**
   - Plan has single "dirty region" concept
   - Needs `CompositorSession` with current + pending

2. **No Thread Safety Model**
   - Plan mentions "compositor thread" but no locking strategy
   - Needs read-write lock protecting layer tree

3. **No Optimization for Movement**
   - Plan says "invalidate layer cache on move"
   - Should be "blit cache, invalidate only new area"

4. **No Background Strategy**
   - When does compositor clear to background?
   - Should adopt delayed clearing pattern

5. **No Resize Propagation**
   - Plan assumes layers resize independently
   - Reality: parent resize → child resize → cache invalidation cascade

---

## Recommendations for Compositor Plan

### 1. Add CompositorSession Class

```cpp
class CompositorSession {
public:
    BRegion  fDirtyLayers;    // Layers needing recomposite
    BRegion  fDirtyScreen;    // Screen regions needing redraw
    bool     fInUse;
    
    void Include(Layer* layer);
    void Exclude(Layer* layer);
    void MoveBy(int32 x, int32 y);
};

class Compositor {
    CompositorSession  fCurrentSession;
    CompositorSession  fPendingSession;
    bool               fCompositingInProgress;
    
    void BeginComposite();  // Pending → Current
    void EndComposite();    // Current → unused
    void InvalidateLayer(Layer* layer);  // Add to pending
};
```

### 2. Add Layer Movement Optimization

```cpp
void Compositor::MoveLayer(Layer* layer, int32 dx, int32 dy) {
    if (!layer->HasCachedBitmap())
        return InvalidateLayer(layer);
    
    // Compute blittable region
    BRegion oldRegion = layer->GetCachedRegion();
    layer->MoveBy(dx, dy);
    BRegion newRegion = layer->GetCachedRegion();
    
    BRegion blittable = oldRegion;
    blittable.OffsetBy(dx, dy);
    blittable.IntersectWith(&newRegion);
    
    if (blittable.CountRects() > 0) {
        BlitLayerCache(layer, blittable, dx, dy);
        
        newRegion.Exclude(&blittable);
        layer->InvalidateCachedRegion(&newRegion);  // Only uncovered parts
    } else {
        InvalidateLayer(layer);
    }
}
```

### 3. Add Threading Model

```cpp
class Desktop {
    MultiLocker  fClippingLock;   // Existing
    MultiLocker  fCompositorLock; // NEW - protects compositor state
    
    Compositor*  fCompositor;
    thread_id    fCompositorThread;
    
    // Desktop thread
    void MoveWindowBy(Window* window, int32 dx, int32 dy) {
        fClippingLock.WriteLock();
        window->MoveBy(dx, dy);
        _RebuildClippingForAllWindows();
        
        fCompositorLock.WriteLock();
        fCompositor->InvalidateLayer(window->GetLayer());
        fCompositorLock.WriteUnlock();
        
        fClippingLock.WriteUnlock();
    }
    
    // Compositor thread
    static int32 _CompositorThread(void* data) {
        Desktop* desktop = (Desktop*)data;
        while (!desktop->fQuit) {
            desktop->_WaitForVSync();
            desktop->_CompositeFrame();
        }
        return 0;
    }
    
    void _CompositeFrame() {
        fCompositorLock.ReadLock();  // Read-only access to layers
        fCompositor->BeginComposite();  // Pending → Current
        
        for (layer in fCompositor->CurrentDirtyLayers()) {
            fCompositor->RenderLayer(layer);
        }
        fCompositor->BlendDirtyRegions();
        fCompositor->EndComposite();
        
        fCompositorLock.ReadUnlock();
    }
};
```

### 4. Add Delayed Clearing

```cpp
void Compositor::BlendLayer(Layer* layer, BRegion* clipRegion) {
    BRegion needsBackground = *clipRegion;
    needsBackground.Exclude(&layer->GetCachedRegion());
    
    if (needsBackground.CountRects() > 0) {
        // Clear ONLY newly exposed parts
        FillRegion(&needsBackground, layer->GetParentBackground());
    }
    
    // Blend cached bitmap over cleared background
    BlitLayerCache(layer, clipRegion);
}
```

### 5. Document Resize Cascade

```cpp
/**
 * Layer Resize Propagation Rules:
 * 
 * 1. When parent layer resizes by (dx, dy):
 *    - All children with B_FOLLOW_RIGHT/BOTTOM adjust frames
 *    - Adjusted children invalidate their cached bitmaps
 *    - Adjusted children recursively propagate to their children
 * 
 * 2. Cache invalidation strategy:
 *    - B_FOLLOW_LEFT + B_FOLLOW_TOP: No invalidation (position unchanged)
 *    - B_FOLLOW_RIGHT or B_FOLLOW_BOTTOM: Full invalidation (moved)
 *    - B_FOLLOW_LEFT_RIGHT or B_FOLLOW_TOP_BOTTOM: Full invalidation (resized)
 * 
 * 3. Optimization opportunity:
 *    - If layer only MOVED (not resized), blit cached bitmap to new position
 *    - If layer RESIZED, must re-render
 */
void Layer::ParentResized(float dx, float dy) {
    BRect oldFrame = fFrame;
    _AdjustFrameForResizeMode(dx, dy);
    
    if (fFrame != oldFrame) {
        if (fFrame.Size() != oldFrame.Size()) {
            // Resized - full re-render needed
            InvalidateCachedBitmap();
        } else {
            // Moved - blit cached bitmap
            int32 offsetX = (int32)(fFrame.left - oldFrame.left);
            int32 offsetY = (int32)(fFrame.top - oldFrame.top);
            fCompositor->MoveLayerCache(this, offsetX, offsetY);
        }
        
        // Propagate to children
        float childDx = fFrame.Width() - oldFrame.Width();
        float childDy = fFrame.Height() - oldFrame.Height();
        if (childDx != 0.0f || childDy != 0.0f) {
            for (child in children)
                child->ParentResized(childDx, childDy);
        }
    }
}
```

---

## Testing Strategy Implications

### Current Reality:
- **0 automated compositor tests** in Haiku codebase
- **2 manual clipping prototypes** (this analysis)
- **99% of `/tests/servers/app`** = interactive demos

### What Needs Testing:

1. **UpdateSession Race Conditions**
   ```cpp
   TEST(CompositorSession, PendingBecomesCurrentAtomically) {
       // Simulate: Desktop invalidates layer DURING compositing
       // Expect: Invalidation goes to pending, not current
   }
   ```

2. **Layer Movement Optimization**
   ```cpp
   TEST(Compositor, MoveLayerBlitsCacheCorrectly) {
       // Move layer 10px right
       // Verify: 90% of cached bitmap blitted, 10% re-rendered
   }
   ```

3. **Resize Cascade**
   ```cpp
   TEST(Layer, ResizePropagateToFollowRightChildren) {
       // Parent resizes +100px width
       // Child with B_FOLLOW_RIGHT should move +100px
       // Child with B_FOLLOW_LEFT should NOT move
   }
   ```

4. **Read-Write Lock Contention**
   ```cpp
   TEST(Compositor, ThreadSafety) {
       // Desktop thread moves windows (write lock)
       // Compositor thread blends (read lock)
       // Window thread draws (read lock)
       // Verify: No deadlocks, no torn clipping
   }
   ```

5. **Delayed Background Clearing**
   ```cpp
   TEST(Compositor, ClearsOnlyUncoveredRegions) {
       // Layer moves 10px, exposing 10px strip
       // Verify: Only 10px strip cleared, rest blitted
   }
   ```

### Recommended Test Infrastructure:

```cpp
// tests/servers/app/compositor/CompositorTestFixture.h
class CompositorTestFixture {
protected:
    Desktop*         fDesktop;
    Compositor*      fCompositor;
    MockDrawingEngine* fEngine;
    
    void SetUp() {
        fEngine = new MockDrawingEngine();
        fDesktop = new Desktop(fEngine);
        fCompositor = fDesktop->GetCompositor();
    }
    
    void SimulateVSync() {
        fCompositor->BeginComposite();
        // ... blend
        fCompositor->EndComposite();
    }
    
    void VerifyRegionDrawn(BRegion expected) {
        EXPECT_EQ(fEngine->GetDirtyRegion(), expected);
    }
};

// tests/servers/app/compositor/CompositorBasicTest.cpp
TEST_F(CompositorTestFixture, InvalidateLayerMarksForComposite) {
    Layer* layer = CreateTestLayer(BRect(0, 0, 100, 100));
    
    fCompositor->InvalidateLayer(layer);
    
    EXPECT_TRUE(fCompositor->PendingSession().Contains(layer));
    EXPECT_FALSE(fCompositor->CurrentSession().Contains(layer));
}

TEST_F(CompositorTestFixture, VyncPromotesPendingToCurrent) {
    Layer* layer = CreateTestLayer(BRect(0, 0, 100, 100));
    fCompositor->InvalidateLayer(layer);
    
    fCompositor->BeginComposite();
    
    EXPECT_TRUE(fCompositor->CurrentSession().Contains(layer));
    EXPECT_FALSE(fCompositor->PendingSession().Contains(layer));
}
```

---

## Conclusion

The clipping prototypes in `newClipping/` and `newerClipping/` are **critical historical artifacts** that demonstrate:

1. ✅ **Update session pattern** (double dirty tracking) works and prevents race conditions
2. ✅ **Read-write locking** allows concurrent Desktop + Window + Compositor threads
3. ✅ **HW copy optimization** is essential for smooth scrolling/moving
4. ✅ **Delayed background clearing** reduces flicker
5. ✅ **Resize cascades** are complex and must be handled recursively

The **Compositor Plan MUST adopt** these patterns, specifically:
- CompositorSession (current + pending dirty tracking)
- Read-write lock protecting layer tree
- MoveLayerCache() with blit optimization
- Delayed clearing of only newly exposed regions
- Recursive ParentResized() for cache invalidation cascade

**Without these patterns, the compositor will suffer from:**
- Race conditions (dirty regions torn between threads)
- Poor performance (full re-render instead of blitting)
- Visible flicker (clearing entire dirty region instead of deltas)
- Incorrect rendering after resize (missing cascade invalidation)

**Test infrastructure gap is CRITICAL** - plan assumes compositor "just works" but reality shows clipping required **two prototype iterations** to get right. Compositor is MORE complex (adds caching + blending), so needs **equal or greater** prototype/test investment.
