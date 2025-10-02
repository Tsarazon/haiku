# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands for app_server Drawing Subsystem

### Building the Drawing Subsystem
```bash
# Build the main drawing library
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libasdrawing.a

# Build the Painter library (AGG-based renderer)
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libpainter.a

# Build specific components for debugging
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa DrawingEngine.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa Painter.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa BitmapPainter.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa AGGTextRenderer.o

# Build graphics backend libraries
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libagg.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libblend2d.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libasmjit.a

# Build the complete app_server with drawing dependencies
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa app_server
```

### Testing and Development
```bash
# Build all drawing-related libraries at once
jam -qa libasdrawing.a libpainter.a libagg.a

# For testing interface implementations
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa AccelerantHWInterface.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa BitmapHWInterface.o
```

## Architecture Overview

### Drawing Subsystem Architecture
This directory contains Haiku's graphics rendering subsystem, which provides hardware abstraction and software rendering for the app_server.

#### Core Components

**DrawingEngine (DrawingEngine.cpp/h)**
- Main interface between app_server and graphics backends
- Manages hardware interface connections and rendering state
- Coordinates between software and hardware rendering paths
- Implements clipping, transformations, and rendering primitives

**HWInterface (HWInterface.cpp/h)**
- Hardware abstraction layer for frame buffer access
- Manages cursor handling, overlay support, and display modes
- Provides locking mechanisms for thread-safe graphics access
- Base class for various hardware implementations

**Painter (Painter/)**
- AGG-based software rendering implementation
- Provides anti-aliased graphics primitives and text rendering
- Implements BeOS-compatible drawing modes and blending
- Handles bitmap drawing, vector graphics, and complex paths

#### Hardware Interface Implementations

**interface/local/** - Direct Hardware Access
- `AccelerantHWInterface`: Graphics card driver integration
- `AccelerantBuffer`: Hardware frame buffer management
- Direct video memory access and hardware acceleration

**interface/remote/** - Network Graphics
- `RemoteHWInterface`: Thin client graphics over network
- `NetSender/NetReceiver`: Network protocol for graphics commands
- `StreamingRingBuffer`: Efficient data transfer for remote display

**interface/virtual/** - Software Rendering
- `ViewHWInterface`: Rendering into BView for testing
- `DWindowHWInterface`: DirectWindow-compatible interface

#### Buffer Management
- **BitmapBuffer**: Software rendering into BBitmap objects
- **MallocBuffer**: Generic memory-based rendering buffers
- **BBitmapBuffer**: Haiku BBitmap integration

### Painter Subsystem Detail

#### Core Rendering (Painter/)
**Painter.cpp/h**: Main AGG-based renderer implementing:
- Vector graphics primitives (lines, curves, polygons, ellipses)
- Anti-aliased rendering with subpixel precision
- BeOS drawing mode compatibility (copy, blend, invert, etc.)
- Gradient fills and pattern support
- Coordinate transformations and clipping

**AGGTextRenderer.cpp/h**: Text rendering system:
- FreeType font integration with AGG rasterization
- Subpixel anti-aliasing for LCD displays
- Font caching and glyph management
- Unicode text layout and rendering

#### Drawing Mode Compatibility (drawing_modes/)
Comprehensive implementation of BeOS drawing modes:
- **B_OP_COPY**: Direct pixel copying
- **B_OP_OVER**: Alpha blending operations
- **B_OP_ALPHA**: Various alpha compositing modes
- **B_OP_INVERT**: Pixel inversion operations
- **SUBPIX variants**: Subpixel-optimized versions for text

Each mode has specialized implementations for performance:
- Solid color versions for simple fills
- Pattern-aware versions for textured drawing
- Subpixel-optimized versions for text rendering

#### Bitmap Operations (bitmap_painter/)
**BitmapPainter.cpp/h**: Optimized bitmap drawing:
- **DrawBitmapNoScale.h**: 1:1 pixel copying for unscaled bitmaps
- **DrawBitmapNearestNeighbor.h**: Fast scaling with nearest neighbor
- **DrawBitmapBilinear.h**: High-quality bilinear filtering
- **DrawBitmapGeneric.h**: Fallback for complex transformations
- **painter_bilinear_scale.nasm**: x86 assembly optimizations

### Pattern and Mask Support
**PatternHandler.cpp/h**: BeOS pattern compatibility
- 8x8 pixel pattern support for fills and strokes
- High/low color pattern rendering
- Pattern coordinate alignment and tiling

**AlphaMask.cpp/h**: Clipping and transparency
- Region-based clipping mask generation
- Alpha channel masking for complex shapes
- Cache management for frequently used masks

## Development Patterns

### Coordinate System and Transformations
- **Painter coordinates**: Floating-point precision for anti-aliasing
- **DrawingEngine coordinates**: Integer pixel coordinates for hardware
- **Transformable class**: Handles rotation, scaling, and translation
- **Subpixel precision**: Enabled for text rendering, disabled for UI elements

### AGG Integration Patterns
- Use AGG path storage for complex shapes
- Coordinate transformation via `agg::trans_affine`
- Rasterization through AGG scanline renderers
- Custom AGG extensions in `agg_*.h` files provide Haiku-specific functionality

### Drawing Mode Implementation
When adding new drawing modes:
1. Create base implementation in `drawing_modes/DrawingModeXXX.h`
2. Add solid color variant `DrawingModeXXXSolid.h` for performance
3. Create subpixel variant `DrawingModeXXXSUBPIX.h` for text
4. Update mode selection in `Painter.cpp`

### Buffer Management
- Always check buffer validity before drawing operations
- Use appropriate locking (ReadLock for drawing, WriteLock for mode changes)
- Handle both direct frame buffer and bitmap buffer targets
- Coordinate with hardware cursor hiding/showing

### Performance Optimization
- **Fast paths**: Unscaled bitmap drawing, solid rectangle fills
- **SIMD support**: MMX/SSE optimizations where available
- **Caching**: Font glyphs, alpha masks, gradient calculations
- **Clipping optimization**: Use precise regions to minimize overdraw

## Current Migration Status

### AGG to Blend2D Transition
The drawing subsystem is being migrated from AGG to Blend2D for improved performance:
- **Current status**: AGG implementation stable and fully functional
- **Migration target**: Blend2D-based rendering pipeline
- **Compatibility**: Both backends maintained during transition
- **Files affected**: Core Painter classes, text rendering, bitmap operations

When working on graphics code:
1. Check if Blend2D equivalents exist in parent directories
2. Maintain AGG compatibility during migration period
3. Test changes with existing AGG-based rendering
4. Consider performance implications of both backends

## Testing and Validation

### Common Issues
- **Clipping problems**: Check region calculations and coordinate transformations
- **Drawing mode compatibility**: Verify against BeOS reference behavior
- **Memory management**: Ensure proper buffer cleanup and reference counting
- **Thread safety**: Verify locking in multi-threaded drawing scenarios

### Key Files for Different Components
- **Basic drawing**: `DrawingEngine.cpp`, `Painter.cpp`
- **Text rendering**: `AGGTextRenderer.cpp`
- **Bitmap operations**: `bitmap_painter/BitmapPainter.cpp`
- **Hardware access**: `interface/local/AccelerantHWInterface.cpp`
- **Drawing modes**: `drawing_modes/DrawingMode*.h`
- **Buffer management**: `BitmapBuffer.cpp`, `MallocBuffer.cpp`

## Build System Notes

### JAM Build Requirements
- **ALWAYS use HAIKU_OUTPUT_DIR** for cross-compilation builds
- **JAM path**: Use `./buildtools/jam/bin.linuxx86/jam` on Linux
- **Dependencies**: Drawing subsystem requires AGG headers and libraries
- **Library order**: libasdrawing.a depends on libpainter.a and libagg.a

### External Dependencies
- **AGG library**: Anti-Grain Geometry for vector graphics
- **FreeType**: Font loading and glyph outline processing
- **Blend2D**: Next-generation graphics library (migration target)
- **AsmJIT**: JIT compilation support for Blend2D