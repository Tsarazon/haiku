# AGG to Blend2D Migration: Detailed Implementation Plan

## Overview
This document provides a **COMPLETE DISCRETE LIST** of specific prompts and file changes required for migrating from AGG to Blend2D in the Haiku graphics stack. Each prompt is designed for parallel execution with clear, exhaustive instructions.

---

## PHASE 1: Infrastructure Setup (Parallel Execution)

### 1.1 Blend2D Library Integration - Core Setup
**Prompt**: Set up Blend2D library infrastructure in Haiku build system
**Thread**: Infrastructure-1
**Files to Create**:
- `/src/libs/blend2d/meson.build` - Create Blend2D Meson build configuration with source files, include paths, and compiler flags matching Haiku standards
- `/src/libs/blend2d/Jamfile` - Create JAM build file for libblend2d.a static library with architecture-specific objects
- `/src/libs/blend2d/README` - Document Blend2D integration and build instructions
**Dependencies**: None
**Estimated Time**: 2-3 days

### 1.2 Blend2D Source Integration
**Prompt**: Download and integrate Blend2D source code into Haiku tree
**Thread**: Infrastructure-2
**Actions**:
- Create directory structure `/src/libs/blend2d/`
- Add Blend2D headers to `/headers/libs/blend2d/`
- Configure build flags: `-O2`, `-fno-strict-aliasing`, `-DHAIKU_TARGET_PLATFORM_HAIKU`
- Set up include directories and preprocessor definitions
**Files to Create**:
- `/headers/libs/blend2d/` (complete Blend2D header tree)
- `/src/libs/blend2d/src/` (Blend2D source files)
**Dependencies**: None
**Estimated Time**: 1-2 days

### 1.3 Compatibility Layer Foundation
**Prompt**: Create Blend2D compatibility layer and interface headers
**Thread**: Compatibility-1
**Files to Create**:
- `/src/servers/app/drawing/Blend2D/Blend2DInterface.h` - Main Blend2D pipeline interface replacing PainterAggInterface
- `/src/servers/app/drawing/Blend2D/Blend2DPainter.h` - Painter compatibility layer with BLContext wrapper
- `/src/servers/app/drawing/Blend2D/Blend2DTypes.h` - Type aliases mapping AGG types to Blend2D equivalents
- `/src/servers/app/drawing/Blend2D/Blend2DDefines.h` - Rendering pipeline definitions for Blend2D
**Dependencies**: 1.2 (Blend2D headers)
**Estimated Time**: 3-4 days

### 1.4 Build System Integration
**Prompt**: Update main build system to include Blend2D library
**Thread**: Build-1
**Files to Modify**:
- `/src/libs/meson.build` - Add blend2d subdirectory
- `/meson.build` - Include libblend2d in global build
- `/src/kits/meson.build` - Link against libblend2d
- `/src/servers/app/Jamfile` - Add Blend2D dependency
**Dependencies**: 1.1, 1.2
**Estimated Time**: 1-2 days

---

## PHASE 2: Core Painter Migration (Parallel Execution)

### 2.1 PainterAggInterface Replacement
**Prompt**: Replace PainterAggInterface.h with Blend2D equivalent
**Thread**: Core-1
**File to Replace**: `/src/servers/app/drawing/Painter/PainterAggInterface.h` (66 lines)
**New File**: `/src/servers/app/drawing/Painter/PainterBlend2DInterface.h`
**Specific Changes**:
- Replace `agg::rendering_buffer fBuffer` with `BLImage fImage`
- Replace `pixfmt fPixelFormat` with `BLFormat fPixelFormat`
- Replace `renderer_base fBaseRenderer` with `BLContext fContext`
- Replace `agg::path_storage fPath` with `BLPath fPath`
- Replace `agg::conv_curve<agg::path_storage> fCurve` with built-in BLPath curve support
- Remove all AGG scanline and rasterizer objects (handled internally by BLContext)
**Dependencies**: 1.3 (Blend2D types)
**Estimated Time**: 2-3 days

### 2.2 Defines.h Type System Migration
**Prompt**: Migrate AGG type definitions to Blend2D equivalents
**Thread**: Core-2
**File to Modify**: `/src/servers/app/drawing/Painter/defines.h` (80 lines)
**Specific Changes**:
- Replace `typedef PixelFormat pixfmt` with `typedef BLFormat pixfmt`
- Replace `typedef agg::renderer_region<pixfmt> renderer_base` with `typedef BLContext renderer_base`
- Replace all scanline typedefs with BLContext internal types
- Replace `typedef agg::rasterizer_scanline_aa<> rasterizer_type` with `typedef BLContext rasterizer_type`
- Replace `typedef agg::renderer_scanline_aa_solid<renderer_base> renderer_type` with `typedef BLContext renderer_type`
- Remove all AGG-specific includes, add Blend2D includes
**Dependencies**: 1.3 (Blend2D types)
**Estimated Time**: 2 days

### 2.3 Painter.h Header Migration
**Prompt**: Update Painter.h header file for Blend2D compatibility
**Thread**: Core-3
**File to Modify**: `/src/servers/app/drawing/Painter/Painter.h` (358 lines)
**Specific Changes**:
- Replace `#include "AGGTextRenderer.h"` with `#include "Blend2DTextRenderer.h"`
- Replace `#include "PainterAggInterface.h"` with `#include "PainterBlend2DInterface.h"`
- Replace `#include <agg_conv_curve.h>` with Blend2D path includes
- Update method signatures:
  - `void _CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint, BLMatrix2D& mtx, float gradient_d2 = 100.0f) const`
  - `void _CalcRadialGradientTransform(BPoint center, BLMatrix2D& mtx, float gradient_d2 = 100.0f) const`
- Replace template methods with Blend2D specific implementations
- Update member variables:
  - `mutable Blend2DTextRenderer fTextRenderer`
  - `mutable PainterBlend2DInterface fInternal`
**Dependencies**: 2.1, 2.2, 3.1
**Estimated Time**: 1-2 days

### 2.4 Painter.cpp Core Methods - Part 1 (Basic Rendering)
**Prompt**: Migrate basic rendering methods in Painter.cpp from AGG to Blend2D
**Thread**: Core-4A
**File to Modify**: `/src/servers/app/drawing/Painter/Painter.cpp` (Lines 1-150)
**Specific Changes**:
- Replace AGG initialization:
  ```cpp
  // OLD: fRasterizer.gamma(agg::gamma_threshold(0.5));
  // NEW: fInternal.fContext.setCompOp(BL_COMP_OP_SRC_OVER);
  ```
- Update `AttachToBuffer()` method:
  - Replace `agg::rendering_buffer` setup with `BLImage` creation
  - Replace pixel format setup with `BLFormat` configuration
- Update `SetDrawState()` method:
  - Replace AGG transformation setup with `BLMatrix2D`
  - Update clipping region setup for BLContext
**Dependencies**: 2.1, 2.2, 2.3
**Estimated Time**: 3-4 days

### 2.5 Painter.cpp Core Methods - Part 2 (Path Rendering)
**Prompt**: Migrate path rendering methods in Painter.cpp from AGG to Blend2D
**Thread**: Core-4B
**File to Modify**: `/src/servers/app/drawing/Painter/Painter.cpp` (Lines 151-300)
**Specific Changes**:
- Replace `_StrokePath()` template methods:
  ```cpp
  // OLD: template<class VertexSource> BRect _StrokePath(VertexSource& path) const;
  // NEW: BRect _StrokePath(const BLPath& path) const;
  ```
- Replace `_FillPath()` template methods:
  ```cpp
  // OLD: template<class VertexSource> BRect _FillPath(VertexSource& path) const;
  // NEW: BRect _FillPath(const BLPath& path) const;
  ```
- Update path iteration:
  - Replace AGG vertex source iteration with BLPath operations
  - Replace curve conversion with built-in BLPath curve support
**Dependencies**: 2.1, 2.2, 2.3
**Estimated Time**: 4-5 days

### 2.6 Painter.cpp Core Methods - Part 3 (Gradient Rendering)
**Prompt**: Migrate gradient rendering methods in Painter.cpp from AGG to Blend2D
**Thread**: Core-4C
**File to Modify**: `/src/servers/app/drawing/Painter/Painter.cpp` (Lines 301-421)
**Specific Changes**:
- Replace gradient transformation methods:
  ```cpp
  // OLD: void _CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint, agg::trans_affine& mtx, float gradient_d2 = 100.0f) const;
  // NEW: void _CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint, BLMatrix2D& mtx, float gradient_d2 = 100.0f) const;
  ```
- Replace AGG gradient span generation with BLGradient:
  - Replace `agg::span_gradient<>` with `BLGradient` objects
  - Replace gradient color array generation with `BLGradient::addStop()`
  - Replace gradient rasterization with `BLContext::fillPath()` using gradient style
- Update `_RasterizePath()` template methods for gradients
**Dependencies**: 2.1, 2.2, 2.3
**Estimated Time**: 4-5 days

### 2.7 Transformable System Migration
**Prompt**: Migrate Transformable class from AGG inheritance to Blend2D composition
**Thread**: Core-5
**Files to Modify**:
- `/src/servers/app/drawing/Painter/Transformable.h` (inheritance change)
- `/src/servers/app/drawing/Painter/Transformable.cpp` (method implementations)
**Specific Changes in Transformable.h**:
- Change `class Transformable : public agg::trans_affine` to `class Transformable`
- Add `BLMatrix2D fMatrix` member variable
- Update method signatures to use BLMatrix2D operations
- Add `const BLMatrix2D& Matrix() const` accessor
- Replace `operator=(const agg::trans_affine& other)` with `operator=(const BLMatrix2D& other)`
**Specific Changes in Transformable.cpp**:
- Replace `agg::trans_affine::operator=()` calls with `fMatrix = other`
- Replace `multiply(agg::trans_affine_translation())` with `fMatrix.translate()`
- Replace `multiply(agg::trans_affine_rotation())` with `fMatrix.rotate()`
- Replace `multiply(agg::trans_affine_scaling())` with `fMatrix.scale()`
- Replace `multiply(agg::trans_affine_skewing())` with `fMatrix.skew()`
**Dependencies**: 1.3 (BLMatrix2D types)
**Estimated Time**: 2-3 days

---

## PHASE 3: Text Rendering Migration (Parallel Execution)

### 3.1 AGGTextRenderer Replacement - Header
**Prompt**: Create Blend2DTextRenderer.h to replace AGGTextRenderer.h
**Thread**: Text-1
**File to Create**: `/src/servers/app/drawing/Painter/Blend2DTextRenderer.h`
**File to Replace**: `/src/servers/app/drawing/Painter/AGGTextRenderer.h` (100 lines)
**Specific Changes**:
- Replace AGG renderer references with BLContext:
  ```cpp
  // OLD: renderer_subpix_type& subpixRenderer, renderer_type& solidRenderer, renderer_bin_type& binRenderer
  // NEW: BLContext& context
  ```
- Replace AGG scanline types with BLContext internal handling
- Replace AGG rasterizer with BLContext operations
- Update constructor:
  ```cpp
  // OLD: AGGTextRenderer(renderer_subpix_type& subpixRenderer, ...)
  // NEW: Blend2DTextRenderer(BLContext& context, BLMatrix2D& viewTransformation)
  ```
- Replace glyph pipeline with BLFont and BLGlyphBuffer:
  ```cpp
  BLFont fFont;
  BLGlyphBuffer fGlyphBuffer;
  BLContext& fContext;
  ```
**Dependencies**: 1.3 (Blend2D types)
**Estimated Time**: 2-3 days

### 3.2 AGGTextRenderer Replacement - Implementation
**Prompt**: Create Blend2DTextRenderer.cpp to replace AGGTextRenderer.cpp
**Thread**: Text-2
**File to Create**: `/src/servers/app/drawing/Painter/Blend2DTextRenderer.cpp`
**File to Replace**: `/src/servers/app/drawing/Painter/AGGTextRenderer.cpp` (estimated 300+ lines)
**Specific Changes**:
- Replace AGG glyph rendering pipeline with Blend2D text rendering:
  ```cpp
  // OLD: AGG pipeline with FontCacheEntry::GlyphPathAdapter, curves, contours
  // NEW: BLGlyphBuffer and BLContext::fillGlyphRun()
  ```
- Update `RenderString()` methods:
  - Replace AGG scanline rendering with `fContext.fillGlyphRun()`
  - Replace glyph path generation with BLFont glyph buffer operations
  - Update clipping frame handling for BLContext
- Replace subpixel rendering:
  - Replace AGG subpixel scanlines with BLContext render quality settings
  - Update antialiasing handling with BLContext render quality
- Update transformation handling:
  ```cpp
  // OLD: agg::trans_affine& fViewTransformation
  // NEW: BLMatrix2D& fViewTransformation
  ```
**Dependencies**: 3.1, 1.3
**Estimated Time**: 5-6 days

### 3.3 Font Integration Updates - FontEngine
**Prompt**: Update FontEngine.cpp for Blend2D text rendering compatibility
**Thread**: Text-3
**File to Modify**: `/src/servers/app/font/FontEngine.cpp`
**Specific Changes**:
- Replace AGG font loading with Blend2D font creation:
  ```cpp
  // OLD: AGG FreeType font adapter setup
  // NEW: BLFont creation from FreeType face
  ```
- Update font metrics handling for BLFont compatibility
- Replace glyph loading pipeline with BLFont glyph access
- Update font caching to work with BLFont objects
- Maintain ServerFont API compatibility while using BLFont internally
**Dependencies**: 3.1, 3.2
**Estimated Time**: 3-4 days

### 3.4 Font Integration Updates - FontCacheEntry
**Prompt**: Update FontCacheEntry.cpp for Blend2D font caching
**Thread**: Text-4
**File to Modify**: `/src/servers/app/font/FontCacheEntry.cpp`
**Specific Changes**:
- Replace AGG glyph cache with BLFont glyph buffer caching:
  ```cpp
  // OLD: FontCacheEntry::GlyphPathAdapter, GlyphGray8Adapter
  // NEW: BLGlyphBuffer caching and management
  ```
- Update glyph metrics extraction for BLFont
- Replace glyph rasterization with BLFont native operations
- Update cache management for BLGlyphBuffer objects
- Maintain existing cache key and lookup mechanisms
**Dependencies**: 3.1, 3.2, 3.3
**Estimated Time**: 2-3 days

### 3.5 ServerFont Integration
**Prompt**: Update ServerFont.cpp to work with Blend2D text renderer
**Thread**: Text-5
**File to Modify**: `/src/servers/app/ServerFont.cpp`
**Specific Changes**:
- Update text measurement methods to use BLFont metrics
- Replace AGG-specific font setup with BLFont configuration
- Update font loading and face management for Blend2D compatibility
- Ensure existing ServerFont API remains unchanged for application compatibility
- Update font family and style handling to work with BLFont
**Dependencies**: 3.1, 3.2, 3.3
**Estimated Time**: 2-3 days

---

## PHASE 4: Icon System Migration (Parallel Execution)

### 4.1 IconRenderer Core Migration
**Prompt**: Migrate IconRenderer.h and IconRenderer.cpp from AGG to Blend2D
**Thread**: Icon-1
**Files to Modify**:
- `/src/libs/icon/IconRenderer.h` (120 lines)
- `/src/libs/icon/IconRenderer.cpp` (estimated 200+ lines)
**Specific Changes in IconRenderer.h**:
- Replace AGG includes with Blend2D includes:
  ```cpp
  // OLD: #include <agg_gamma_lut.h>, <agg_pixfmt_rgba.h>, <agg_rasterizer_compound_aa.h>
  // NEW: #include <blend2d.h>
  ```
- Replace AGG typedefs with Blend2D equivalents:
  ```cpp
  // OLD: typedef agg::gamma_lut<agg::int8u, agg::int8u> GammaTable;
  // NEW: // Gamma handled internally by BLContext
  
  // OLD: typedef agg::rendering_buffer RenderingBuffer;
  // NEW: typedef BLImage RenderingBuffer;
  
  // OLD: typedef agg::pixfmt_bgra32 PixelFormat;
  // NEW: typedef BLFormat PixelFormat;
  
  // OLD: typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_dbl> CompoundRasterizer;
  // NEW: typedef BLContext CompoundRasterizer;
  ```
- Update member variables:
  ```cpp
  BLContext fContext;
  BLImage fRenderingImage;
  BLFormat fPixelFormat;
  // Remove AGG-specific members like fGammaTable, fBaseRenderer, etc.
  ```
**Specific Changes in IconRenderer.cpp**:
- Replace AGG rendering pipeline with BLContext:
  ```cpp
  // OLD: Complex AGG pipeline setup with rasterizer, scanlines, renderers
  // NEW: Single BLContext configuration
  ```
- Replace `_Render()` method implementation:
  - Replace AGG compound rasterizer with BLContext shape rendering
  - Replace gamma correction with BLContext render quality settings
  - Replace AGG style handler with BLGradient/solid color styles
- Update shape rendering loop:
  ```cpp
  // OLD: AGG vertex source iteration and rasterization
  // NEW: BLPath creation and BLContext.fillPath()/strokePath()
  ```
**Dependencies**: 1.3 (Blend2D types), 4.2, 4.3
**Estimated Time**: 4-5 days

### 4.2 Icon Shape System Migration
**Prompt**: Migrate icon shape handling from AGG to Blend2D
**Thread**: Icon-2
**Files to Modify**:
- `/src/libs/icon/shape/PathSourceShape.cpp`
- `/src/libs/icon/shape/Shape.cpp`
- `/src/libs/icon/shape/VectorPath.cpp`
- `/src/libs/icon/shape/VectorPath.h`
**Specific Changes**:
- **VectorPath.h/cpp**: Replace AGG path storage with BLPath:
  ```cpp
  // OLD: agg::path_storage integration
  // NEW: BLPath integration with move_to, line_to, curve_to methods
  ```
- **PathSourceShape.cpp**: Update path source to work with BLPath
- **Shape.cpp**: Replace AGG shape rendering with BLPath operations
- Update path building methods to use BLPath API:
  ```cpp
  // OLD: agg::path_storage::move_to(), line_to(), curve4()
  // NEW: BLPath::moveTo(), lineTo(), cubicTo()
  ```
- Update path iteration for BLPath compatibility
**Dependencies**: 1.3 (BLPath)
**Estimated Time**: 3-4 days

### 4.3 Icon Transformer System Migration
**Prompt**: Migrate icon transformation system from AGG to Blend2D
**Thread**: Icon-3
**Files to Modify**:
- `/src/libs/icon/transformer/AffineTransformer.h`
- `/src/libs/icon/transformer/ContourTransformer.h`
- `/src/libs/icon/transformer/PerspectiveTransformer.cpp`
- `/src/libs/icon/transformer/PerspectiveTransformer.h`
- `/src/libs/icon/transformer/StrokeTransformer.h`
- `/src/libs/icon/transformer/PathSource.h`
**Specific Changes**:
- **AffineTransformer.h**: Replace `agg::trans_affine` with `BLMatrix2D`:
  ```cpp
  // OLD: agg::trans_affine based transformations
  // NEW: BLMatrix2D based transformations
  ```
- **ContourTransformer.h**: Replace AGG contour generation with BLPath operations
- **StrokeTransformer.h**: Replace AGG stroke conversion with BLStrokeOptions
- **PerspectiveTransformer.cpp/h**: 
  - Replace AGG perspective transformation with BLMatrix2D
  - Update perspective calculation methods for BLMatrix2D
- **PathSource.h**: Update path source interface for BLPath compatibility
**Dependencies**: 1.3 (BLMatrix2D, BLPath), 2.7 (Transformable)
**Estimated Time**: 3-4 days

### 4.4 Icon Style System Migration
**Prompt**: Migrate icon styling system from AGG to Blend2D
**Thread**: Icon-4
**Files to Modify**:
- `/src/libs/icon/style/Style.h`
**Specific Changes**:
- Replace AGG gradient and pattern references with Blend2D equivalents:
  ```cpp
  // OLD: AGG gradient span generators
  // NEW: BLGradient objects and BLPattern objects
  ```
- Update style rendering to use BLContext style setting:
  ```cpp
  // OLD: AGG span allocator and renderer setup
  // NEW: BLContext.setFillStyle() and BLContext.setStrokeStyle()
  ```
- Update color handling for BLRgba compatibility
- Update gradient stop handling for BLGradient API
**Dependencies**: 1.3 (BLGradient, BLPattern)
**Estimated Time**: 2-3 days

---

## PHASE 5: Application Updates (Parallel Execution)

### 5.1 Icon-O-Matic Transformable System
**Prompt**: Update Icon-O-Matic transformable interfaces for Blend2D
**Thread**: App-1
**Files to Modify**:
- `/src/apps/icon-o-matic/transformable/TransformBox.cpp`
- `/src/apps/icon-o-matic/transformable/TransformBoxStates.h`
- `/src/apps/icon-o-matic/transformable/PerspectiveBox.cpp`
**Specific Changes**:
- **TransformBox.cpp**: Replace AGG transformation handling with BLMatrix2D:
  ```cpp
  // OLD: agg::trans_affine matrix operations
  // NEW: BLMatrix2D matrix operations
  ```
- **TransformBoxStates.h**: Update transformation state handling for BLMatrix2D
- **PerspectiveBox.cpp**: Replace AGG perspective transforms with BLMatrix2D
- Update transformation UI to work with BLMatrix2D values
- Update transformation visualization to use BLPath and BLContext
**Dependencies**: 2.7 (Transformable), 4.3 (Icon transformers)
**Estimated Time**: 3-4 days

### 5.2 Icon-O-Matic Vector Editing
**Prompt**: Update Icon-O-Matic vector editing components for Blend2D
**Thread**: App-2
**Files to Modify**:
- `/src/apps/icon-o-matic/generic/support/support_ui.h`
- `/src/apps/icon-o-matic/import_export/svg/DocumentBuilder.cpp`
**Specific Changes**:
- **support_ui.h**: Update UI helper functions for BLPath and BLMatrix2D
- **DocumentBuilder.cpp**: Replace AGG SVG import with BLPath construction:
  ```cpp
  // OLD: AGG path building from SVG data
  // NEW: BLPath building from SVG data using BLPath API
  ```
- Update bezier curve handling to use BLPath cubic curves
- Update path import/export for BLPath compatibility
**Dependencies**: 4.2 (Shape system), 4.3 (Transformers)
**Estimated Time**: 2-3 days

### 5.3 MediaPlayer Blur Effects
**Prompt**: Update MediaPlayer StackBlurFilter for Blend2D
**Thread**: App-3
**File to Modify**: `/src/apps/mediaplayer/support/StackBlurFilter.cpp`
**Specific Changes**:
- Replace AGG blur algorithm with BLContext blur effects:
  ```cpp
  // OLD: AGG stack blur implementation using AGG rendering pipeline
  // NEW: BLContext image effects or custom blur using BLContext
  ```
- Update image buffer handling from AGG rendering_buffer to BLImage
- Replace AGG pixel format handling with BLFormat
- Update blur radius and quality settings for BLContext compatibility
**Dependencies**: 1.3 (BLImage, BLContext)
**Estimated Time**: 2-3 days

### 5.4 Application Header Updates
**Prompt**: Update application files that include AGG headers
**Thread**: App-4
**Files to Modify** (based on grep results):
- `/src/tests/servers/app/painter/Painter.cpp`
- `/src/tests/servers/app/painter/ShapeConverter.h`
- `/src/kits/interface/AffineTransform.cpp`
- `/src/apps/aboutsystem/AboutSystem.cpp`
- Various other application files with AGG includes
**Specific Changes**:
- Replace AGG header includes with Blend2D includes:
  ```cpp
  // OLD: #include <agg_*.h>
  // NEW: #include <blend2d.h>
  ```
- Update AGG type usage with Blend2D equivalents where directly used
- Most files likely only need header include updates
- Test compilation and update any direct AGG API usage
**Dependencies**: 1.3 (Blend2D headers)
**Estimated Time**: 1-2 days per file group

---

## PHASE 6: Build System and Cleanup (Parallel Execution)

### 6.1 Build System Cleanup - JAM
**Prompt**: Remove AGG references from JAM build system
**Thread**: Build-1
**Files to Modify**:
- `/src/kits/Jamfile` - Remove libagg references, add libblend2d
- `/src/servers/app/Jamfile` - Remove AGG library linking, add Blend2D
- `/src/tests/servers/app/painter/Jamfile` - Update library dependencies
- `/src/servers/app/drawing/Painter/Jamfile` - Remove AGG-specific files
**Specific Changes**:
- Remove `UseLibraryHeaders agg ;` statements
- Replace `StaticLibrary libagg.a` with `StaticLibrary libblend2d.a`
- Update `LinkSharedOSLibs` to include Blend2D instead of AGG
- Remove AGG source file lists from build targets
**Dependencies**: All previous phases completed
**Estimated Time**: 1-2 days

### 6.2 Build System Cleanup - Meson
**Prompt**: Remove AGG references from Meson build system
**Thread**: Build-2
**Files to Modify**:
- `/src/libs/meson.build` - Remove agg subdirectory, keep blend2d
- `/src/kits/meson.build` - Remove libagg dependency, add libblend2d
- `/meson.build` - Remove AGG from global configuration
**Specific Changes**:
- Remove `subdir('agg')` call
- Replace `libagg` dependency with `libblend2d` in dependency lists
- Update include directory paths to use Blend2D headers
- Remove AGG-specific compiler flags and preprocessor definitions
**Dependencies**: All previous phases completed
**Estimated Time**: 1 day

### 6.3 AGG Directory Removal
**Prompt**: Remove all AGG source and header directories
**Thread**: Cleanup-1
**Directories to Delete**:
- `/headers/libs/agg/` (115 files) - Complete AGG header tree
- `/src/libs/agg/` (22+ files) - AGG source implementation
**Verification Steps**:
- Ensure no remaining references to deleted files in any source
- Verify build system no longer references AGG paths
- Run full build to confirm no missing dependencies
**Dependencies**: 6.1, 6.2 (Build system cleanup)
**Estimated Time**: 1 day

### 6.4 AGG-specific File Cleanup
**Prompt**: Remove AGG-specific custom headers and implementations
**Thread**: Cleanup-2
**Files to Delete**:
- `/src/servers/app/drawing/Painter/agg_clipped_alpha_mask.h`
- `/src/servers/app/drawing/Painter/agg_rasterizer_scanline_aa_subpix.h`
- `/src/servers/app/drawing/Painter/agg_renderer_region.h`
- `/src/servers/app/drawing/Painter/agg_renderer_scanline_subpix.h`
- `/src/servers/app/drawing/Painter/agg_scanline_p_subpix.h`
- `/src/servers/app/drawing/Painter/agg_scanline_storage_subpix.h`
- `/src/servers/app/drawing/Painter/agg_scanline_u_subpix.h`
**Verification Steps**:
- Ensure no code references these deleted files
- Verify equivalent functionality exists in Blend2D implementation
**Dependencies**: Phases 2-5 complete (no more references to these files)
**Estimated Time**: Half day

### 6.5 Performance Testing and Optimization
**Prompt**: Benchmark and optimize Blend2D performance vs original AGG
**Thread**: Performance-1
**Testing Areas**:
- Basic shape rendering performance
- Text rendering performance
- Complex path rendering
- Gradient rendering
- Icon rendering performance
- Memory usage comparison
**Optimization Tasks**:
- Tune BLContext render quality settings
- Optimize BLPath construction and reuse
- Configure Blend2D JIT compiler settings
- Profile critical rendering paths
- Optimize memory allocation patterns
**Dependencies**: All migration phases complete
**Estimated Time**: 1-2 weeks

---

## THREAD DEPENDENCY CHART

```
Phase 1 (Infrastructure):
Infrastructure-1 ← None
Infrastructure-2 ← None  
Compatibility-1 ← Infrastructure-2
Build-1 ← Infrastructure-1, Infrastructure-2

Phase 2 (Core Painter):
Core-1 ← Compatibility-1
Core-2 ← Compatibility-1
Core-3 ← Core-1, Core-2
Core-4A ← Core-1, Core-2, Core-3
Core-4B ← Core-1, Core-2, Core-3
Core-4C ← Core-1, Core-2, Core-3
Core-5 ← Compatibility-1

Phase 3 (Text Rendering):
Text-1 ← Compatibility-1
Text-2 ← Text-1, Compatibility-1
Text-3 ← Text-1, Text-2
Text-4 ← Text-1, Text-2, Text-3
Text-5 ← Text-1, Text-2, Text-3

Phase 4 (Icon System):
Icon-1 ← Compatibility-1, Icon-2, Icon-3
Icon-2 ← Compatibility-1
Icon-3 ← Compatibility-1, Core-5
Icon-4 ← Compatibility-1

Phase 5 (Applications):
App-1 ← Core-5, Icon-3
App-2 ← Icon-2, Icon-3
App-3 ← Compatibility-1
App-4 ← Compatibility-1

Phase 6 (Cleanup):
Build-1 ← All previous phases
Build-2 ← All previous phases
Cleanup-1 ← Build-1, Build-2
Cleanup-2 ← Phases 2-5
Performance-1 ← All phases
```

## EXECUTION SUMMARY

**Total File Changes**: 178 files
- **Create**: 15 new files
- **Modify**: 45+ existing files  
- **Delete**: 115+ AGG files

**Parallel Execution Threads**: 25 concurrent threads maximum
**Total Estimated Time**: 6-12 months with proper coordination
**Critical Path**: Infrastructure → Core Painter → Text Rendering → Icon System → Applications → Cleanup

Each prompt is designed to be executed independently with clear dependencies, specific file targets, and exhaustive implementation details.