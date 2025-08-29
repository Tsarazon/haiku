# AGG to Blend2D Migration Strategy for Haiku

## Executive Summary

This document provides a comprehensive strategy for replacing the Anti-Grain Geometry (AGG) library with Blend2D in the Haiku operating system. This migration affects core graphics rendering components including the app_server, icon library, and various applications.

**Scope**: Complete replacement of AGG with Blend2D across the Haiku graphics stack  
**Complexity**: High - involves core system components and 200+ source files  
**Timeline Estimate**: 6-12 months for full migration with testing  
**Risk Level**: High - affects critical system rendering functionality

## Current AGG Implementation Analysis

### Directory Structure Overview

**Headers (115 files)**:
- `/headers/libs/agg/` - Complete AGG library headers (115 header files)
- `/headers/libs/agg/util/` - Utility headers for color conversion
- `/headers/libs/agg/font_freetype/` - FreeType font integration
- `/headers/libs/agg/dbg_new/` - Debug memory allocation

**Source Implementation (22 files)**:
- `/src/libs/agg/src/` - Core AGG source files (22 cpp files)
- `/src/libs/agg/font_freetype/` - FreeType font rendering
- `/src/libs/agg/Jamfile` - Build configuration
- `/src/libs/agg/meson.build` - Meson build integration

### Core Integration Points

#### 1. App Server Graphics Engine
**Primary Location**: `/src/servers/app/drawing/Painter/`

**Key Components**:
- `Painter.h/cpp` - Main rendering class (358 lines, 421 lines)
- `PainterAggInterface.h` - AGG pipeline abstraction (66 lines)  
- `AGGTextRenderer.h/cpp` - Text rendering engine
- `defines.h` - AGG type definitions and pipeline configuration (80 lines)

**AGG Classes Used**:
- `agg::rasterizer_scanline_aa<>` - Anti-aliased rasterization
- `agg::renderer_scanline_aa_solid<>` - Solid color rendering
- `agg::scanline_u8/p8` - Scanline processing
- `agg::trans_affine` - Affine transformations
- `agg::path_storage` - Vector path storage
- `agg::conv_curve<>` - Bezier curve conversion

#### 2. Icon Rendering System
**Primary Location**: `/src/libs/icon/`

**Key Components**:
- `IconRenderer.h/cpp` - Vector icon rasterization (120 lines)
- Uses `agg::rasterizer_compound_aa<>` for complex shapes
- `agg::pixfmt_bgra32` for pixel format handling
- `agg::gamma_lut<>` for gamma correction

#### 3. Vector Graphics Components
**Locations**:
- `/src/libs/icon/shape/` - Path and shape handling
- `/src/libs/icon/transformer/` - Vector transformations  
- `/src/apps/icon-o-matic/` - Vector graphics editor

### Build System Dependencies

**JAM Build System**:
- `libagg.a` static library (22 source files)
- Integrated into `/src/kits/Jamfile`
- Referenced in `/src/servers/app/Jamfile`

**Meson Build System**:
- `/src/libs/agg/meson.build` - Full AGG library build
- Produces `libagg.a` with proper flags and optimization
- Integrated into main Haiku Meson build

## Blend2D Library Analysis

### Key Capabilities
- **High-performance 2D vector graphics engine**
- **JIT compiler** for optimized rendering pipelines
- **Multi-threaded rendering support**
- **CPU optimization** from SSE2 to AVX-512
- **8-bit anti-aliasing** for high-quality rendering
- **Zlib license** - compatible with Haiku licensing

### API Structure
- **BLContext** - Main rendering context (equivalent to Painter)
- **BLPath** - Vector path operations (equivalent to agg::path_storage)
- **BLImage** - Image/bitmap handling (equivalent to rendering buffers)
- **BLFont** - Font and text rendering (equivalent to AGGTextRenderer)
- **BLGradient** - Gradient definitions (equivalent to agg::span_gradient)
- **BLMatrix2D** - 2D transformations (equivalent to agg::trans_affine)

## AGG to Blend2D API Mapping

### Core Rendering Pipeline

| AGG Component | Blend2D Equivalent | Migration Notes |
|---------------|-------------------|-----------------|
| `agg::rasterizer_scanline_aa<>` | `BLContext::fillGeometry()` | Direct context call instead of pipeline |
| `agg::renderer_scanline_aa_solid<>` | `BLContext` with solid style | Integrated into context |
| `agg::scanline_u8/p8` | Internal to BLContext | Hidden implementation detail |
| `agg::rendering_buffer` | `BLImage` | Target image for rendering |
| `agg::pixfmt_bgra32` | `BLFormat::BL_FORMAT_PRGB32` | Pixel format enum |

### Path and Vector Operations

| AGG Component | Blend2D Equivalent | Migration Notes |
|---------------|-------------------|-----------------|
| `agg::path_storage` | `BLPath` | Similar API, direct replacement |
| `agg::conv_curve<>` | `BLPath::cubicTo()` | Built-in curve support |
| `agg::conv_stroke<>` | `BLContext::strokePath()` | Integrated stroking |
| `agg::conv_dash<>` | `BLStrokeOptions::setDashArray()` | Dash pattern options |

### Transformation System

| AGG Component | Blend2D Equivalent | Migration Notes |
|---------------|-------------------|-----------------|
| `agg::trans_affine` | `BLMatrix2D` | Near-identical API |
| `agg::trans_affine_translation()` | `BLMatrix2D::translate()` | Method-based approach |
| `agg::trans_affine_rotation()` | `BLMatrix2D::rotate()` | Method-based approach |
| `agg::trans_affine_scaling()` | `BLMatrix2D::scale()` | Method-based approach |

### Text Rendering

| AGG Component | Blend2D Equivalent | Migration Notes |
|---------------|-------------------|-----------------|
| `AGGTextRenderer` | `BLFont` + `BLContext` | Simplified text rendering |
| `agg::glyph_rendering` | `BLContext::fillGlyphRun()` | Built-in glyph rendering |
| FreeType integration | Built-in font support | May reduce complexity |

## Migration Strategy

### Phase 1: Infrastructure Setup (Weeks 1-4)
**Objective**: Establish Blend2D build integration

**Tasks**:
1. **Add Blend2D as dependency**
   - Create `/src/libs/blend2d/` directory
   - Add Blend2D source/headers to Haiku tree
   - Configure JAM build for libblend2d.a
   - Configure Meson build for Blend2D integration

2. **Create compatibility layer**
   - Create `/src/servers/app/drawing/Blend2D/` directory
   - Implement `Blend2DInterface.h` as AGG replacement
   - Create type aliases for gradual migration
   - Implement basic BLContext wrapper

**Files to Create**:
- `/src/libs/blend2d/meson.build`
- `/src/libs/blend2d/Jamfile`
- `/src/servers/app/drawing/Blend2D/Blend2DInterface.h`
- `/src/servers/app/drawing/Blend2D/Blend2DPainter.h`

### Phase 2: Core Painter Migration (Weeks 5-12)
**Objective**: Replace AGG in core Painter class

**Tasks**:
1. **Replace PainterAggInterface.h**
   - Implement `PainterBlend2DInterface.h`
   - Replace AGG pipeline types with Blend2D equivalents
   - Migrate rendering buffer management

2. **Update Painter.cpp core methods**
   - Replace AGG rasterizer calls with BLContext operations
   - Migrate path rendering functions
   - Update transformation handling
   - Replace gradient rendering pipeline

3. **Update defines.h**
   - Replace AGG type definitions
   - Create Blend2D pipeline aliases
   - Update renderer type definitions

**Files to Replace**:
- `/src/servers/app/drawing/Painter/PainterAggInterface.h` → `PainterBlend2DInterface.h`
- `/src/servers/app/drawing/Painter/defines.h` → Updated with Blend2D types
- `/src/servers/app/drawing/Painter/Painter.cpp` → Major rewrite (421 lines)

### Phase 3: Text Rendering Migration (Weeks 13-16)
**Objective**: Replace AGGTextRenderer with Blend2D text system

**Tasks**:
1. **Create Blend2DTextRenderer**
   - Replace AGGTextRenderer.h/cpp
   - Implement BLFont-based text rendering
   - Migrate glyph caching system
   - Update subpixel rendering

2. **Update Font Integration**
   - Modify ServerFont.cpp integration
   - Update FontEngine.cpp for Blend2D
   - Test font rendering quality

**Files to Replace**:
- `/src/servers/app/drawing/Painter/AGGTextRenderer.h` → `Blend2DTextRenderer.h`
- `/src/servers/app/drawing/Painter/AGGTextRenderer.cpp` → `Blend2DTextRenderer.cpp`
- `/src/servers/app/font/FontEngine.cpp` → Updated for Blend2D
- `/src/servers/app/font/FontCacheEntry.cpp` → Updated for Blend2D

### Phase 4: Icon System Migration (Weeks 17-20)
**Objective**: Migrate vector icon rendering system

**Tasks**:
1. **Update IconRenderer**
   - Replace AGG compound rasterizer with BLContext
   - Migrate gamma correction system
   - Update pixel format handling

2. **Update Icon Library Components**
   - Migrate shape rendering in `/src/libs/icon/shape/`
   - Update transformer system in `/src/libs/icon/transformer/`
   - Replace AGG path handling

**Files to Replace**:
- `/src/libs/icon/IconRenderer.h/cpp` → Updated for Blend2D (120 lines)
- `/src/libs/icon/shape/*.cpp` → Path handling updates
- `/src/libs/icon/transformer/*.h` → Transformation updates

### Phase 5: Application Updates (Weeks 21-24)
**Objective**: Update applications using AGG directly

**Tasks**:
1. **Update Icon-O-Matic**
   - Replace AGG vector editing components
   - Update transformable interfaces
   - Test vector editing functionality

2. **Update other applications**
   - MediaPlayer blur effects
   - Various drawing applications

**Files to Update**:
- `/src/apps/icon-o-matic/transformable/*` → Multiple files
- `/src/apps/mediaplayer/support/StackBlurFilter.cpp`
- Various application files with AGG includes

### Phase 6: Cleanup and Optimization (Weeks 25-26)
**Objective**: Remove AGG dependencies and optimize

**Tasks**:
1. **Remove AGG Library**
   - Delete `/headers/libs/agg/` directory (115 files)
   - Delete `/src/libs/agg/` directory (22 files)
   - Update build systems to remove AGG references

2. **Performance Optimization**
   - Profile Blend2D vs AGG performance
   - Optimize critical rendering paths
   - Tune JIT compiler settings

**Files to Delete** (178 total):
- All files in `/headers/libs/agg/` (115 files)
- All files in `/src/libs/agg/` (22 files)
- AGG-specific custom headers (7 files)

## Detailed File-by-File Analysis

### Critical Files Requiring Major Rewrites

#### 1. `/src/servers/app/drawing/Painter/Painter.cpp` (421 lines)
**Complexity**: Very High  
**Changes Required**:
- Replace AGG rasterizer setup with BLContext initialization
- Migrate all `_StrokePath()` template methods to BLContext stroke calls
- Replace gradient rendering pipeline with BLGradient + BLContext
- Update transformation handling from agg::trans_affine to BLMatrix2D
- Replace scanline rendering with direct BLContext operations

**Key Method Migrations**:
```cpp
// AGG Current:
template<class VertexSource>
BRect _StrokePath(VertexSource& path) const;

// Blend2D Target:
BRect StrokePath(const BLPath& path) const;
```

#### 2. `/src/servers/app/drawing/Painter/PainterAggInterface.h` (66 lines)
**Complexity**: High  
**Complete Replacement Required**:
- Replace all AGG pipeline objects with BLContext
- Replace rendering buffer with BLImage
- Simplify interface due to Blend2D's integrated approach

#### 3. `/src/servers/app/drawing/Painter/defines.h` (80 lines)
**Complexity**: Medium  
**Changes Required**:
- Replace all AGG type aliases with Blend2D equivalents
- Update renderer type definitions
- Remove complex AGG pipeline typedef chains

#### 4. `/src/libs/icon/IconRenderer.cpp` (estimated 200+ lines)
**Complexity**: High  
**Changes Required**:
- Replace `agg::rasterizer_compound_aa<>` with BLContext
- Migrate gamma correction from AGG to Blend2D
- Update render loop for simplified Blend2D API

### Medium Complexity Files

#### 1. Transformation System Files
- `/src/servers/app/drawing/Painter/Transformable.h/cpp`
- Already inherits from agg::trans_affine
- Need to change inheritance to composition with BLMatrix2D
- Update all transformation method calls

#### 2. Font-related Files  
- `/src/servers/app/font/FontEngine.cpp`
- `/src/servers/app/font/FontCacheEntry.cpp`
- Update FreeType integration points
- May benefit from Blend2D's built-in font support

### Low Complexity Files

#### 1. Application Files
- Most application files only include AGG headers
- Primary changes are header include updates
- `/src/apps/mediaplayer/support/StackBlurFilter.cpp` - may need blur algorithm update

## Risk Assessment and Mitigation

### High Risk Areas

#### 1. Performance Regression
**Risk**: Blend2D may have different performance characteristics
**Mitigation**: 
- Comprehensive benchmarking during Phase 6
- Profile critical rendering paths
- Optimize Blend2D JIT settings for Haiku workloads

#### 2. Rendering Quality Differences  
**Risk**: Subtle differences in anti-aliasing or text rendering
**Mitigation**:
- Visual comparison testing
- Regression test suite for rendering
- Community beta testing

#### 3. Font Rendering Changes
**Risk**: Text may render differently, affecting UI layout
**Mitigation**:
- Careful font metric comparison
- Extensive text rendering testing
- Maintain compatibility with ServerFont API

### Medium Risk Areas

#### 1. Build System Complexity
**Risk**: Blend2D integration may complicate builds
**Mitigation**:
- Parallel build system maintenance during migration
- Comprehensive build testing on all platforms

#### 2. Memory Usage Changes
**Risk**: Different memory allocation patterns
**Mitigation**:
- Memory usage profiling
- Optimize Blend2D memory settings

## Testing Strategy

### Unit Testing
- Individual component replacement testing
- API compatibility verification
- Performance benchmarks for each component

### Integration Testing  
- Complete graphics pipeline testing
- Application compatibility testing
- Real-world usage scenarios

### Regression Testing
- Visual comparison with reference screenshots
- Performance benchmarks vs current AGG implementation
- Memory usage analysis

## Success Metrics

### Performance Targets
- **Rendering Speed**: No more than 10% performance degradation
- **Memory Usage**: No more than 15% memory increase
- **Binary Size**: Evaluate impact of Blend2D vs AGG library size

### Quality Targets
- **Visual Fidelity**: Pixel-perfect rendering for core graphics operations
- **Font Rendering**: Maintain or improve text rendering quality
- **Stability**: No regressions in graphics-related crashes

## Conclusion

The migration from AGG to Blend2D represents a significant undertaking affecting 178 files and core system functionality. However, Blend2D's modern architecture, performance optimizations, and simplified API should provide long-term benefits including:

- **Improved Performance**: JIT optimization and modern CPU feature utilization
- **Simplified Codebase**: Reduced complexity from AGG's pipeline architecture  
- **Better Maintainability**: Modern C++ API and active development
- **Future-Proofing**: Active project with ongoing optimization and features

The migration should be executed carefully with extensive testing at each phase to ensure system stability and performance.

**Estimated Total Effort**: 6-12 months with dedicated graphics developer(s)  
**Risk Level**: High (due to core system impact)  
**Recommended Approach**: Phased migration with extensive testing and rollback capability