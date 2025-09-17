# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands for Painter Subsystem

### Building Painter Components
```bash
# Build the main Painter library
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libpainter.a

# Build specific Painter components
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa Painter.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa AGGTextRenderer.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa BitmapPainter.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa Transformable.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa GlobalSubpixelSettings.o

# Build drawing modes and pixel format components
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa PixelFormat.o

# Build with architecture-specific optimizations (x86 only)
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa painter_bilinear_scale.o

# Build dependent libraries
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libagg.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libfreetype.a
```

### Testing and Development
```bash
# Build entire drawing subsystem for testing
jam -qa libpainter.a libasdrawing.a app_server

# For cross-compilation builds (required on Linux)
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libpainter.a
```

## Architecture Overview

### Painter: AGG-Based Software Renderer
The Painter subsystem is Haiku's primary software graphics renderer built on the Anti-Grain Geometry (AGG) library. It provides a BView-compatible drawing API with full anti-aliasing support.

#### Core Design Philosophy
**From README**: Painter implements BView drawing functions using AGG 2D engine, attachable to any frame buffer for anti-aliased rendering operations. It serves as the software rendering backend when hardware acceleration is unavailable or insufficient.

**Performance Characteristics from NOTES**:
- Current performance (B_OP_COPY, ellipse tests):
  - Pen size 1.0: 5.15 units (regular), 13.90 units (anti-aliased)
  - Pen size 2.0: 15.2 units (regular), 11.0 units (anti-aliased)

### Component Architecture

#### Core Painter Class (Painter.h/cpp)
**Main Software Renderer**: Central class providing complete BView drawing API:
- **Vector Graphics**: Lines, rectangles, ellipses, arcs, bezier curves, polygons
- **Text Rendering**: Unicode text with subpixel anti-aliasing and font caching
- **Bitmap Operations**: Scaling, filtering, and color space conversion
- **Gradient Support**: Linear, radial, diamond, and conic gradients
- **Coordinate Transformations**: Rotation, scaling, translation with floating-point precision

**Key Internal Systems**:
- `fInternal` (PainterAggInterface): AGG rendering pipeline management
- `fPatternHandler`: BeOS-compatible pattern rendering
- `fTextRenderer` (AGGTextRenderer): FreeType font integration
- `fTransform` (Transformable): Coordinate transformation matrix

#### AGG Integration Layer

**PainterAggInterface.h**: AGG Pipeline Management
- Rendering buffer and pixel format setup
- Multiple scanline renderers for different quality modes:
  - Regular: Standard anti-aliased rendering
  - Binary: Fast non-anti-aliased mode for UI elements
  - Subpixel: LCD-optimized text rendering
- Rasterizers and path storage for vector graphics
- Alpha masking support for complex clipping

**defines.h**: AGG Type System
Template-based type definitions for AGG rendering pipeline:
- Conditional compilation between aliased/anti-aliased modes
- Subpixel rendering type definitions with averaging filters
- Custom AGG extensions (`agg_*.h`) for Haiku-specific features

#### Text Rendering System

**AGGTextRenderer.h/cpp**: Advanced Font Rendering
- **FreeType Integration**: Vector font loading and glyph outline processing
- **Subpixel Anti-aliasing**: RGB/BGR LCD optimization with configurable filtering
- **Font Caching**: Glyph bitmap caching through FontCacheReference system
- **Hinting Support**: Configurable font hinting for different text rendering quality
- **Kerning**: Advanced text layout with proper character spacing
- **Unicode Support**: Full UTF-8 text rendering with proper baseline positioning

**GlobalSubpixelSettings.h/cpp**: System-wide Text Configuration
- `gSubpixelAntialiasing`: Global subpixel rendering toggle
- `gDefaultHintingMode`: OFF/ON/MONOSPACED_ONLY hinting control
- `gSubpixelAverageWeight`: Color fringe reduction (0=sharp, 255=grayscale)
- `gSubpixelOrderingRGB`: RGB vs BGR LCD panel configuration

#### Coordinate System and Transformations

**Transformable.h/cpp**: Matrix Transformation Engine
- AGG `trans_affine` wrapper with Haiku API compatibility
- Coordinate transformation: rotation, scaling, translation, shearing
- Bounds calculation for transformed rectangles
- Optimized identity transform detection
- BArchivable support for state persistence

**Coordinate Precision Modes**:
- `fSubpixelPrecise`: Enable/disable subpixel coordinate precision
- Automatic alignment for different rendering contexts (UI vs graphics)
- Floating-point coordinates for smooth anti-aliasing

#### Bitmap Rendering System

**bitmap_painter/BitmapPainter.h/cpp**: Optimized Bitmap Operations
Sophisticated bitmap rendering with multiple code paths:

**Rendering Strategies**:
- `DrawBitmapNoScale.h`: Direct 1:1 pixel copying for unscaled bitmaps
- `DrawBitmapNearestNeighbor.h`: Fast integer scaling without filtering
- `DrawBitmapBilinear.h`: High-quality bilinear filtering for smooth scaling
- `DrawBitmapGeneric.h`: Fallback for complex transformations and edge cases

**Architecture-Specific Optimizations**:
- `painter_bilinear_scale.nasm`: x86 assembly optimizations for scaling operations
- SIMD support detection (MMX/SSE) in main Painter class
- Multi-threaded bitmap processing capabilities

**Color Space Handling**:
- Transparent magic color to alpha conversion
- Automatic color space conversion through intermediate 32-bit buffers
- B_CMAP8 bitmap fast path for indexed color modes

#### Drawing Modes Implementation

**drawing_modes/ Subsystem**: BeOS Compatibility Layer
Comprehensive implementation of BeOS drawing modes with template-based architecture:

**Drawing Mode Categories**:
- **Copy Modes**: B_OP_COPY for direct pixel replacement
- **Alpha Modes**: B_OP_OVER, B_OP_ALPHA for transparency blending
- **Arithmetic Modes**: B_OP_ADD, B_OP_SUBTRACT, B_OP_MIN, B_OP_MAX
- **Logical Modes**: B_OP_INVERT, B_OP_SELECT for pixel manipulation

**Performance Optimizations**:
- Solid color variants (`*Solid.h`) for single-color operations
- Subpixel variants (`*SUBPIX.h`) optimized for text rendering
- Template specialization for common pattern types

### AGG Customizations and Extensions

#### Custom AGG Components
The Painter includes several custom AGG extensions for Haiku-specific requirements:

**Subpixel Rendering Extensions**:
- `agg_scanline_*_subpix.h`: LCD-optimized scanline renderers
- `agg_rasterizer_scanline_aa_subpix.h`: Subpixel-aware rasterization
- `agg_renderer_scanline_subpix.h`: RGB component-based rendering

**Clipping and Masking**:
- `agg_clipped_alpha_mask.h`: Complex shape clipping for ClipToPicture operations
- `agg_renderer_region.h`: Region-based clipping optimization

**Performance Optimizations**:
- `agg_scanline_*_avrg_filtering.h`: Configurable subpixel averaging
- Custom memory management for reduced allocation overhead

## Development Patterns

### AGG Pipeline Architecture
Understanding AGG's rendering pipeline is crucial for Painter development:

**Typical Rendering Flow**:
1. **Path Definition**: Vector paths stored in `agg::path_storage`
2. **Curve Conversion**: Bezier curves flattened to line segments
3. **Transformation**: Coordinate transformation via `agg::trans_affine`
4. **Rasterization**: Vector to pixel conversion with anti-aliasing
5. **Scanline Generation**: Horizontal pixel spans with coverage information
6. **Rendering**: Final pixel composition with drawing modes

### Performance Optimization Strategies

**Rendering Mode Selection**:
- Use binary renderer for 1-pixel lines and simple UI elements
- Enable subpixel renderer only for text rendering
- Prefer solid color variants when applicable
- Cache transformed paths for repeated drawing operations

**Memory Management**:
- Reuse AGG path storage objects to minimize allocations
- Pre-allocate scanline buffers for consistent performance
- Use object pools for frequently created temporary objects

**Coordinate Optimization**:
- Detect identity transforms to bypass transformation overhead
- Align rectangles to pixel boundaries when precision isn't required
- Use integer coordinates for UI elements, floating-point for graphics

### Drawing State Management

**State Synchronization**: Painter maintains drawing state compatibility with BView:
- High/low colors for pattern rendering
- Pen size with floating-point precision
- Drawing mode with BeOS compatibility
- Font settings with caching integration
- Clipping regions with efficient intersection

**Performance Considerations from NOTES**:
- Dest alpha handling optimized for frame buffer vs BBitmap targets
- Pattern rendering can be optimized for solid patterns (TODO item)
- Drawing mode selection impacts performance significantly

### Known Issues and Limitations

**Current Limitations from README**:
- Only 32-bit frame buffers fully supported
- Limited color space support requires conversion to 32-bit intermediate buffers
- Bitmap drawing optimized primarily for 32-bit sources

**Drawing Mode Quirks from NOTES**:
- B_OP_INVERT affects destination alpha unexpectedly
- B_OP_SELECT behavior differs from BeBook specification for bitmaps
- B_OP_ALPHA with B_ALPHA_OVERLAY has unusual alpha assignment behavior
- Integer pen coordinates affect rotated text positioning

## Testing and Debugging

### Performance Testing
- Ellipse rendering benchmarks in NOTES provide baseline performance metrics
- Compare aliased vs anti-aliased rendering performance
- Monitor memory allocation patterns in AGG pipeline
- Test coordinate precision with various transformation matrices

### Drawing Mode Validation
- Verify BeOS compatibility with reference implementations
- Test alpha channel handling in bitmap vs frame buffer contexts
- Validate subpixel rendering with different LCD configurations
- Check pattern alignment and tiling correctness

### Text Rendering Validation
- Test Unicode text layout with complex scripts
- Verify subpixel anti-aliasing across different font sizes
- Check kerning accuracy with various font families
- Validate font caching behavior under memory pressure

## External Dependencies

### Required Libraries
- **AGG**: Anti-Grain Geometry 2D graphics library for vector rendering
- **FreeType**: Font loading and glyph outline processing
- **FontManager**: Haiku font discovery and caching system

### Build System Integration
- Architecture-specific assembly files (x86 NASM optimization)
- Conditional FreeType headers inclusion
- Cross-compilation support with HAIKU_OUTPUT_DIR
- Template instantiation requires careful header organization

### Performance Tuning
The Painter system provides several tuning opportunities:
- Subpixel averaging weight adjustment for LCD optimization
- Hinting mode selection based on font characteristics
- Drawing mode selection for optimal performance vs quality trade-offs
- Memory allocation patterns in AGG pipeline management