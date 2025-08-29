# AGG to Blend2D Migration Strategy for Haiku OS

## Executive Summary

This document provides a comprehensive analysis and migration strategy for replacing the Anti-Grain Geometry (AGG) library with Blend2D in the Haiku operating system. The migration represents a significant modernization effort that could bring performance improvements, active maintenance, and modern 2D graphics capabilities to Haiku's graphics stack.

## Table of Contents
1. [Current State Analysis](#current-state-analysis)
2. [Technology Comparison](#technology-comparison)
3. [Migration Challenges](#migration-challenges)
4. [Migration Strategy](#migration-strategy)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Risk Assessment](#risk-assessment)
7. [Performance Analysis](#performance-analysis)
8. [Recommendations](#recommendations)

## Current State Analysis

### AGG Integration in Haiku

AGG is deeply integrated into Haiku's graphics subsystem, particularly in:

#### Core Components Using AGG:
1. **app_server/drawing/Painter/** - The main 2D rendering engine
   - Over 100 files directly include AGG headers
   - Critical rendering pipelines depend on AGG's rasterizer and scanline renderers
   - Custom subpixel rendering extensions built on AGG

2. **Font Rendering System**
   - FontEngine.cpp/h heavily uses AGG for glyph rendering
   - FontCacheEntry utilizes AGG path adapters and scanline storage
   - AGGTextRenderer implements text rendering with AGG primitives

3. **Icon System (libs/icon/)**
   - IconRenderer uses AGG for vector icon rendering
   - Shape, VectorPath, and transformation classes depend on AGG

4. **Drawing Modes**
   - 40+ custom drawing mode implementations extending AGG's pixel formats
   - Specialized blending modes for Haiku's unique requirements

### AGG Usage Statistics in Haiku:
- **Direct AGG includes**: ~100+ files
- **AGG type usage**: 
  - `agg::trans_affine`: Transformation matrices
  - `agg::path_storage`: Vector path management
  - `agg::rendering_buffer`: Pixel buffer management
  - `agg::rasterizer_scanline_aa`: Anti-aliased rasterization
  - `agg::gradient_*`: Gradient functions
  - `agg::conv_curve`: Bezier curve conversion
  - `agg::scanline_*`: Various scanline types for rendering

## Technology Comparison

### AGG vs Blend2D Feature Matrix

| Feature | AGG | Blend2D | Migration Complexity |
|---------|-----|---------|---------------------|
| **Architecture** | Template-based C++ | C/C++ dual API with JIT | Medium |
| **Maintenance** | Inactive since 2006 | Actively maintained | N/A |
| **License** | Modified BSD (v2.4) | Zlib | Low |
| **Dependencies** | None | AsmJit for JIT | Low |
| **Performance** | Good (software) | Excellent (JIT optimized) | N/A |
| **Threading** | Single-threaded | Multi-threaded native | Medium |
| **SIMD Support** | Manual | Automatic via JIT | Low |
| **Build System** | Custom | CMake | Low |

### Core API Mapping

| AGG Component | Blend2D Equivalent | Notes |
|---------------|-------------------|-------|
| `agg::rendering_buffer` | `BLImage` | Different memory management model |
| `agg::trans_affine` | `BLMatrix2D` | Similar transformation capabilities |
| `agg::path_storage` | `BLPath` | More feature-rich in Blend2D |
| `agg::rasterizer_scanline_aa` | Internal to `BLContext` | Abstracted away in Blend2D |
| `agg::pixfmt_*` | `BLFormat` | Simpler format handling |
| `agg::gradient_*` | `BLGradient` | Built-in gradient types |
| `agg::conv_curve` | Built into `BLPath` | Automatic curve handling |
| `agg::scanline_*` | Internal to rendering | Not directly exposed |
| FreeType integration | Built-in font engine | Different API structure |

### Performance Advantages of Blend2D

1. **JIT Compilation**: Generates optimized pipelines at runtime
   - SSE2 to AVX-512 on x86/x64
   - ASIMD on ARM64
   - 100-200MB/s code generation speed

2. **Multi-threading**: Native support with thread count configuration
   ```cpp
   BLContextCreateInfo createInfo {};
   createInfo.threadCount = 4;
   ```

3. **Memory Efficiency**: Pipeline caching prevents redundant generation
   - Single pipeline: 0.2-5KB
   - Total cache: typically 20-50KB

4. **Rendering Speed**: Blend2D claims significant performance advantages over Cairo and Qt

## Migration Challenges

### 1. Architectural Differences

#### **Template vs Runtime Architecture**
- **AGG**: Heavy template usage for compile-time optimization
- **Blend2D**: Runtime JIT compilation for optimization
- **Challenge**: Refactoring template-based code to runtime API calls

#### **Scanline Rendering Model**
- **AGG**: Explicit scanline-based rendering exposed to user
- **Blend2D**: Scanline rendering abstracted within BLContext
- **Challenge**: Rewriting rendering loops that directly manipulate scanlines

### 2. Haiku-Specific Extensions

#### **Subpixel Rendering**
Haiku has custom AGG extensions for subpixel rendering:
- `agg_scanline_u_subpix.h`
- `agg_scanline_storage_subpix.h`
- `agg_rasterizer_scanline_aa_subpix.h`

**Solution**: Implement custom Blend2D pipeline extensions or use Blend2D's built-in subpixel capabilities

#### **Custom Drawing Modes**
40+ custom drawing modes in `/servers/app/drawing/Painter/drawing_modes/`:
- DrawingModeAlphaCC, DrawingModeBlend, DrawingModeCopy, etc.

**Solution**: Leverage Blend2D's composition operators and custom blend functions

### 3. Font Rendering Integration

#### **Current AGG Implementation**
- Direct FreeType integration via AGG's font_freetype
- Custom glyph caching with AGG scanline storage
- Complex path adapter system for glyph outlines

#### **Blend2D Approach**
- Built-in font engine (no FreeType dependency)
- Different caching mechanism
- Simplified API: `BLFont`, `BLFontFace`, `BLGlyphBuffer`

**Challenge**: Complete rewrite of font rendering subsystem

### 4. Memory Management

#### **AGG Model**
- Manual buffer management with `agg::rendering_buffer`
- Direct memory access for pixel manipulation
- No reference counting

#### **Blend2D Model**
- Reference-counted `BLImage` objects
- Abstracted pixel access
- Automatic memory management

**Challenge**: Adapting Haiku's direct memory manipulation patterns

## Migration Strategy

### Phase 1: Preparation (2-3 months)

1. **Create Abstraction Layer**
   ```cpp
   class GraphicsBackend {
   public:
       virtual void drawPath(const Path& path) = 0;
       virtual void setTransform(const Transform& t) = 0;
       // ... other methods
   };
   
   class AGGBackend : public GraphicsBackend { /* current implementation */ };
   class Blend2DBackend : public GraphicsBackend { /* new implementation */ };
   ```

2. **Build System Integration**
   - Add Blend2D as submodule or vendored dependency
   - Update build configurations for dual backend support
   - Create feature flags for backend selection

3. **Testing Infrastructure**
   - Develop visual regression tests
   - Create performance benchmarks
   - Set up A/B testing framework

### Phase 2: Core Implementation (4-6 months)

1. **Implement Blend2D Backend**
   - Start with basic shape rendering
   - Add gradient support
   - Implement text rendering
   - Add image composition

2. **Port Critical Components**
   ```cpp
   // Example: Painter class adaptation
   class Painter {
   private:
       #ifdef USE_BLEND2D
       BLContext blContext;
       BLPath currentPath;
       #else
       agg::path_storage fPath;
       agg::rendering_buffer fBuffer;
       #endif
   };
   ```

3. **Custom Extensions**
   - Implement subpixel rendering using Blend2D's pipeline API
   - Create custom blend modes if needed
   - Adapt Haiku-specific optimizations

### Phase 3: Integration (3-4 months)

1. **Gradual Component Migration**
   - Start with IconRenderer (self-contained)
   - Move to Painter subsystem
   - Finally migrate font rendering

2. **Parallel Testing**
   - Run both backends simultaneously
   - Compare output pixel-by-pixel
   - Measure performance differences

3. **Optimization**
   - Profile and optimize hot paths
   - Tune JIT compiler settings
   - Implement Haiku-specific optimizations

### Phase 4: Transition (2-3 months)

1. **Beta Testing**
   - Enable Blend2D backend for testing builds
   - Gather user feedback
   - Fix compatibility issues

2. **Performance Validation**
   - Comprehensive benchmarking
   - Memory usage analysis
   - Power consumption testing (for mobile)

3. **Documentation**
   - Update developer documentation
   - Create migration guides for third-party apps
   - Document new capabilities

### Phase 5: Completion (1-2 months)

1. **Final Migration**
   - Switch default backend to Blend2D
   - Deprecate AGG backend
   - Clean up transitional code

2. **Legacy Support**
   - Maintain AGG compatibility layer for critical apps
   - Provide migration tools
   - Support gradual third-party migration

## Implementation Roadmap

### Year 1: Foundation
- **Q1**: Research, planning, abstraction layer design
- **Q2**: Abstraction layer implementation, testing infrastructure
- **Q3**: Basic Blend2D backend, shape rendering
- **Q4**: Gradient support, image composition

### Year 2: Full Implementation
- **Q1**: Font rendering system
- **Q2**: Custom drawing modes, subpixel rendering
- **Q3**: Integration testing, performance optimization
- **Q4**: Beta release, community testing

### Year 3: Transition
- **Q1**: Bug fixes, performance tuning
- **Q2**: Documentation, migration tools
- **Q3**: Final transition
- **Q4**: AGG deprecation, cleanup

## Risk Assessment

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Performance regression | Medium | High | Extensive benchmarking, JIT tuning |
| Visual differences | High | Medium | Pixel-perfect tests, gradual rollout |
| Memory usage increase | Low | Medium | Profiling, optimization |
| Third-party breakage | Medium | High | Compatibility layer, long transition |
| Missing features | Low | High | Custom implementations, upstream contribution |

### Non-Technical Risks

1. **Community Resistance**
   - Risk: Users attached to current rendering
   - Mitigation: Demonstrate clear benefits, maintain compatibility

2. **Resource Constraints**
   - Risk: Limited developer resources
   - Mitigation: Phased approach, community involvement

3. **Upstream Changes**
   - Risk: Blend2D API changes
   - Mitigation: Version pinning, active communication

## Performance Analysis

### Expected Improvements

1. **Rendering Speed**: 2-4x improvement in complex scenes
2. **Multi-threading**: Linear scaling with core count
3. **Memory Usage**: Similar or slightly better
4. **JIT Overhead**: < 1ms pipeline generation

### Benchmark Targets

| Operation | AGG (baseline) | Blend2D Target | Actual |
|-----------|---------------|----------------|---------|
| Path rendering | 100% | 150-200% | TBD |
| Text rendering | 100% | 120-150% | TBD |
| Gradient fills | 100% | 200-300% | TBD |
| Image composition | 100% | 150-200% | TBD |

## Recommendations

### Immediate Actions

1. **Proof of Concept**
   - Create minimal Blend2D integration
   - Benchmark critical operations
   - Validate feature parity

2. **Community Engagement**
   - Present findings to Haiku community
   - Gather feedback on approach
   - Recruit additional developers

3. **Technical Preparation**
   - Set up CI/CD for dual backends
   - Create comprehensive test suite
   - Document current AGG usage

### Long-term Strategy

1. **Maintain Flexibility**
   - Design for potential future backend changes
   - Keep abstraction layer clean
   - Document architectural decisions

2. **Contribute Upstream**
   - Submit Haiku-specific improvements to Blend2D
   - Participate in Blend2D development
   - Ensure long-term support

3. **Gradual Migration**
   - Don't rush the transition
   - Prioritize stability over speed
   - Maintain backward compatibility

## Conclusion

The migration from AGG to Blend2D represents a significant modernization opportunity for Haiku's graphics stack. While the migration presents substantial challenges due to AGG's deep integration, the benefits of active maintenance, superior performance, and modern features make it a worthwhile investment.

The proposed phased approach minimizes risk while allowing for thorough testing and community feedback. With proper planning and execution, this migration can position Haiku's graphics subsystem for the next decade of development.

### Key Success Factors

1. **Strong abstraction layer** to ease transition
2. **Comprehensive testing** to ensure quality
3. **Community buy-in** for smooth adoption
4. **Adequate resources** for implementation
5. **Patience and persistence** through challenges

### Final Recommendation

**Proceed with the migration** using the phased approach outlined in this document. Begin with a proof of concept to validate assumptions and refine the strategy based on real-world findings. The long-term benefits significantly outweigh the short-term costs and risks.

---

*Document Version: 1.0*  
*Date: 2025-08-29*  
*Status: Initial Analysis Complete*