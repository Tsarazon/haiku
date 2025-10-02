# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

### Build Commands
- Build app_server: `HAIKU_OUTPUT_DIR=generated ./buildtools/jam/bin.linuxx86/jam -qa app_server`
- Build specific painter components: `HAIKU_OUTPUT_DIR=generated ./buildtools/jam/bin.linuxx86/jam -qa libpainter.a`
- Build drawing engine: `HAIKU_OUTPUT_DIR=generated ./buildtools/jam/bin.linuxx86/jam -qa DrawingEngine.o`
- Full Haiku build: `jam -q @nightly-anyboot` (from generated.* directory)

### Key Environment Variables
- `HAIKU_OUTPUT_DIR`: Set to `generated` for builds from root, or omit when building from generated.* directories
- `USE_BLEND2D_BACKEND=1`: Enable Blend2D backend instead of AGG (set in Jamfile)

## Architecture Overview

### Graphics Backend Migration (AGG → Blend2D)
This directory contains a **dual graphics backend system** where Haiku's app_server is being migrated from Anti-Grain Geometry (AGG) to Blend2D for improved performance and modern graphics capabilities.

**Current State:**
- Legacy AGG code: `DrawingMode*.h`, `PixelFormat.h`, `AggCompOpAdapter.h`
- New Blend2D code: `Blend2DDrawingMode*.h`, `Blend2DPixelFormat.h`, `Blend2DCompOpAdapter.h`


### Drawing Modes Architecture
The drawing modes implement Haiku's `drawing_mode` enum values (B_OP_COPY, B_OP_OVER, B_OP_ADD, etc.) through a template-based system:

**Pattern:**
- Base classes: `DrawingMode.h` / `Blend2DDrawingMode.h` (macros and utilities)
- Mode implementations: `DrawingMode[Mode].h` / `Blend2DDrawingMode[Mode].h`
- Subpixel variants: `*SUBPIX.h` files for LCD text rendering
- Solid color optimizations: `*Solid.h` files

**Key Drawing Modes:**
- Copy, Over, Add, Subtract, Min, Max (direct GPU mapping)
- Blend, Select, Invert, Erase (custom pixel manipulation required)
- AlphaCC, AlphaCO, AlphaPC, AlphaPO (alpha compositing variants)

### Pixel Format System
- `PixelFormat.h`: AGG rendering_buffer-based pixel access
- `Blend2DPixelFormat.h`: BLContext-based rendering with AGG-compatible API
- Both provide identical interfaces for drawing operations
- Function pointer dispatch for different drawing modes

## Working with Drawing Modes

### Adding New Drawing Mode Support
1. Implement both AGG and Blend2D variants
2. Add SUBPIX variant for subpixel text rendering
3. Consider Solid variant for performance optimization
4. Update corresponding CompOpAdapter for blend operations

### Debugging Graphics Issues
- Check both AGG and Blend2D implementations for consistency
- Verify macro definitions in base DrawingMode headers
- Test with both regular and SUBPIX variants
- Use Blend2D's native composition operators where possible

### Performance Optimization
- Prefer native Blend2D operations over pixel-level manipulation
- Use Solid variants for solid colors
- Leverage `canUseNativeBlend2D()` helper function
- 
## File Organization

### Core Headers
- `DrawingMode.h` / `Blend2DDrawingMode.h`: Base macros and utilities
- `PixelFormat.h` / `Blend2DPixelFormat.h`: Pixel format abstraction
- `*CompOpAdapter.h`: Composition operation adapters

### Implementation Files
- `Blend2DPixelFormat.cpp`: Blend2D pixel format implementation (only .cpp file)
- All other drawing modes are header-only template implementations

### Migration Status
This is an active migration from AGG to Blend2D. Both backends must be maintained until migration is complete.