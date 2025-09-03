# Render Engine Migration Guide

## Architecture Overview

### Current Haiku Architecture
```
app_server → DrawingEngine → Painter → PainterAggInterface → AGG
```

### Target Architecture
```
app_server → IRenderEngine → AGGRender → AGG (direct)
```

### Future Extensibility
```
app_server → IRenderEngine → BlendRender → Blend2D
app_server → IRenderEngine → SkiaRender → Skia
```

## Core Design Principles

### 1. Clean Interface Separation
- **IRenderEngine**: Pure abstract interface, no implementation details
- **AGGRender**: Concrete implementation using AGG library directly
- **No intermediate layers**: Remove PainterAggInterface wrapper

### 2. Header/Implementation Split

#### Headers (AGGRender.h)
```cpp
// ALLOWED in headers:
- #include "IRenderEngine.h"           // Interface we implement
- #include <agg_*.h>                   // AGG API headers are OK
- #include <GraphicsDefs.h>            // Public Haiku API
- Forward declarations for internal classes

// NOT ALLOWED in headers:
- #include "../DrawState.h"            // Use forward declaration
- #include "../ServerBitmap.h"         // Use forward declaration
- #include "../ServerFont.h"           // Use forward declaration
```

#### Implementation Files (AGGRender_*.cpp)
```cpp
// ALLOWED in implementation:
- #include "../DrawState.h"            // Full includes
- #include "../ServerBitmap.h"         // Full includes  
- #include "../ServerFont.h"           // Full includes
- #include "../PatternHandler.h"       // Full includes
- Direct AGG API usage
- All conversion logic between Haiku and AGG types
```

## Migration Strategy

### Step 1: Extract Rendering Logic from Painter
The existing Painter class contains all rendering logic mixed with AGG wrapper code. We need to:

1. **Identify core rendering algorithms** in Painter.cpp
2. **Remove PainterAggInterface layer** 
3. **Preserve coordinate transformations** and clipping logic
4. **Maintain color/pattern handling** from PatternHandler

### Step 2: Organize into Logical Components

#### AGGRender.cpp - Core Management
- Constructor/Destructor
- Buffer attachment/detachment
- State management (push/pop)
- Error handling

#### AGGRender_Strokes.cpp - Line/Stroke Operations
```cpp
// From Painter::StrokeLine, Painter::StrokeRect, etc.
BRect AGGRender::StrokeLine(BPoint a, BPoint b) {
    // Transform points
    fTransform.transform(&a.x, &a.y);
    fTransform.transform(&b.x, &b.y);
    
    // Direct AGG usage (no PainterAggInterface)
    agg::path_storage path;
    path.move_to(a.x, a.y);
    path.line_to(b.x, b.y);
    
    // Apply stroke settings
    agg::conv_stroke<agg::path_storage> stroke(path);
    stroke.width(fPenSize);
    stroke.line_cap(fLineCapMode);
    stroke.line_join(fLineJoinMode);
    
    // Rasterize
    agg::rasterizer_scanline_aa<> ras;
    ras.add_path(stroke);
    
    // Render
    agg::scanline_p8 sl;
    agg::render_scanlines_aa_solid(ras, sl, *fBaseRenderer, 
        agg::rgba8(fHighColor.red, fHighColor.green, fHighColor.blue, fHighColor.alpha));
    
    return _CalculateBounds(path);
}
```

#### AGGRender_Output.cpp - Fill Operations
- FillRect, FillRegion, FillPolygon
- Pattern fills
- Gradient fills

#### AGGRender_Text.cpp - Text Rendering
- Font management
- Glyph rendering
- Text metrics
- Subpixel antialiasing

#### AGGRender_ImageBitmap.cpp - Bitmap Operations
- DrawBitmap with transforms
- Bitmap scaling/filtering
- Alpha blending

#### AGGRender_PathGeometry.cpp - Path Operations
- Bezier curves
- Complex shapes
- Path boolean operations

#### AGGRender_Rasterization.cpp - Scanline Processing
- Rasterizer setup
- Scanline generation
- Clipping region handling

#### AGGRender_Transform.cpp - Coordinate Transformations
- Affine transforms
- Perspective transforms
- Coordinate alignment

#### AGGRender_ColorGradient.cpp - Gradient Support
- Linear gradients
- Radial gradients
- Custom gradient types

#### AGGRender_Composition.cpp - Blending Modes
- Porter-Duff operations
- Custom blending modes
- Alpha channel handling

### Step 3: Direct AGG Integration

Replace PainterAggInterface members with direct AGG objects:

```cpp
// OLD (with PainterAggInterface):
class Painter {
    PainterAggInterface fInternal;
    // Access through: fInternal.fRasterizer, fInternal.fRenderer, etc.
};

// NEW (direct AGG):
class AGGRender {
private:
    // Direct AGG members
    agg::rendering_buffer fAGGBuffer;
    agg::pixfmt_rgba32* fPixelFormat;
    agg::renderer_base<agg::pixfmt_rgba32>* fBaseRenderer;
    agg::trans_affine fTransform;
    
    // Rasterizers (created on demand)
    agg::rasterizer_scanline_aa<> fRasterizer;
    agg::scanline_p8 fScanline;
};
```

### Step 4: State Management

Preserve DrawState integration but simplify:

```cpp
status_t AGGRender::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset) {
    // Extract what we need from DrawState
    fPenSize = state->PenSize();
    fHighColor = state->HighColor();
    fLowColor = state->LowColor();
    fDrawingMode = state->GetDrawingMode();
    fLineCapMode = state->LineCapMode();
    fLineJoinMode = state->LineJoinMode();
    fMiterLimit = state->MiterLimit();
    
    // Set transform with offsets
    BAffineTransform transform = state->CombinedTransform();
    SetTransform(transform, xOffset, yOffset);
    
    // Update clipping
    SetClippingRegion(state->GetCombinedClippingRegion());
    
    return B_OK;
}
```

## Key Conversion Patterns

### Haiku to AGG Color
```cpp
agg::rgba8 ConvertToAGGColor(const rgb_color& color) {
    return agg::rgba8(color.red, color.green, color.blue, color.alpha);
}
```

### Haiku to AGG Transform
```cpp
void SetTransform(const BAffineTransform& transform) {
    fTransform = agg::trans_affine(
        transform.sx, transform.shy, transform.shx,
        transform.sy, transform.tx, transform.ty
    );
}
```

### Haiku Drawing Mode to AGG Blending
```cpp
void ApplyDrawingMode(drawing_mode mode) {
    switch (mode) {
        case B_OP_COPY:
            // Direct copy, no blending
            break;
        case B_OP_OVER:
            // Alpha blending
            break;
        case B_OP_ADD:
            // Additive blending
            break;
        // ... other modes
    }
}
```

## Testing Strategy

### 1. Unit Tests
- Test each AGGRender method independently
- Verify coordinate transformations
- Check clipping boundaries
- Validate color conversions

### 2. Compatibility Tests
- Ensure AGGRender produces identical output to Painter
- Compare rendered bitmaps pixel-by-pixel
- Test all drawing modes and blend operations

### 3. Performance Tests
- Measure rendering speed vs current Painter
- Profile memory usage
- Optimize hot paths

## Common Pitfalls to Avoid

1. **Don't expose AGG types in public interface** - Keep them in private section
2. **Don't forget coordinate alignment** - Haiku uses pixel centers at .5
3. **Preserve clipping region handling** - Critical for window management
4. **Maintain DrawState compatibility** - app_server depends on this
5. **Handle memory management carefully** - AGG objects need proper cleanup

## Build Integration

### Jamfile Updates
```jam
StaticLibrary libasrender.a :
    AGGRender.cpp
    AGGRender_Text.cpp
    AGGRender_Output.cpp
    AGGRender_Strokes.cpp
    AGGRender_PathGeometry.cpp
    AGGRender_Rasterization.cpp
    AGGRender_ImageBitmap.cpp
    AGGRender_ColorGradient.cpp
    AGGRender_Transform.cpp
    AGGRender_Composition.cpp
    ;
```

### Incremental Migration
1. Start with simple operations (StrokeLine, FillRect)
2. Add complex shapes and paths
3. Implement text rendering
4. Add bitmap operations
5. Complete with advanced features (gradients, patterns)

## Success Metrics

- [ ] All Painter functionality migrated to AGGRender
- [ ] No PainterAggInterface dependency
- [ ] Clean IRenderEngine implementation
- [ ] Passing all existing app_server tests
- [ ] No performance regression
- [ ] Memory usage equal or better
- [ ] Code is ready for alternative backends (Blend2D, Skia)

## Future Considerations

### Adding New Backends
To add a new rendering backend (e.g., Blend2D):

1. Create `BlendRender` class implementing `IRenderEngine`
2. Implement all pure virtual methods
3. Add to render engine factory
4. No changes needed in app_server

### Performance Optimizations
- Consider SIMD optimizations for common operations
- Implement caching for frequently used paths
- Add fast paths for axis-aligned rectangles
- Optimize scanline generation for simple shapes

## References

- AGG Documentation: http://antigrain.com/doc/index.html
- Haiku Rendering Architecture: https://www.haiku-os.org/docs/api/
- Porter-Duff Compositing: https://en.wikipedia.org/wiki/Alpha_compositing

---

This migration maintains backward compatibility while creating a clean, extensible architecture for future rendering backends.