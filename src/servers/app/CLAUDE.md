# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands for app_server

### Building app_server
```bash
# Build app_server specifically
jam app_server

# Build with specific output directory (REQUIRED for cross-compilation)
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated jam app_server

# Full cross-compilation build path
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa app_server

# Build specific object files for debugging
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa BitmapPainter.o
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa AGGTextRenderer.o

# Build drawing subsystem libraries
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libasdrawing.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libpainter.a

# Build graphics libraries (AGG, Blend2D, AsmJIT)
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libagg.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libblend2d.a
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa libasmjit.a
```

### Development and Testing
```bash
# Build all drawing-related components
jam -qa libasdrawing.a libpainter.a app_server

# Cross-compilation build
HAIKU_OUTPUT_DIR=/home/ruslan/haiku/generated ./buildtools/jam/bin.linuxx86/jam -qa app_server
```

## Architecture Overview

### Core app_server Architecture
The app_server is Haiku's centralized graphics and windowing server, responsible for:
- **Window management**: Creating, positioning, and compositing all application windows
- **Graphics rendering**: Hardware-accelerated and software-based drawing operations  
- **Input handling**: Mouse, keyboard, and other input device coordination
- **Font rendering**: Text layout and anti-aliased font rendering
- **Desktop management**: Multiple workspaces, screen management, and decorators

### Key Architectural Components

#### Main Server Classes
- **AppServer**: Entry point, manages Desktop instances per user
- **Desktop**: Core window management, workspace handling, input coordination
- **MessageLooper**: Base class for BMessage-based communication with client applications

#### Window Management Hierarchy
- **ServerApp**: Represents a client application connection
- **ServerWindow**: Server-side representation of application BWindows
- **Window**: Internal window management with stacking, visibility, focus
- **View**: Server-side BView hierarchy with drawing state and clipping
- **Layer**: Legacy abstraction for view rendering (being phased out)

#### Graphics Rendering Pipeline
- **DrawingEngine**: Abstract interface for rendering operations
- **Painter**: AGG-based software renderer implementing drawing primitives
- **HWInterface**: Hardware abstraction for frame buffer access
- **BitmapBuffer/AccelerantBuffer**: Memory and hardware buffer management

### Drawing Subsystem Architecture

#### Painter and AGG Integration
Located in `drawing/Painter/`, this is the core graphics rendering system:
- **Painter.cpp**: Main rendering class using Anti-Grain Geometry (AGG) library
- **drawing_modes/**: Comprehensive BeOS-compatible blend mode implementations
- **bitmap_painter/**: Optimized bitmap drawing with scaling and filtering
- **AGGTextRenderer**: Font rendering with subpixel anti-aliasing

#### Hardware Abstraction
- **drawing/interface/local/**: Direct frame buffer access via accelerant drivers
- **drawing/interface/remote/**: Network-based rendering for thin clients
- **drawing/BitmapDrawingEngine**: Software rendering into memory bitmaps

#### Current Graphics Migration - Blend2D Integration
The graphics subsystem is actively being migrated from AGG to Blend2D:
- **Status**: Blend2D backend implementation in progress (277+ references in codebase)
- **New Classes**: 
  - `PainterBlend2D`: Main Blend2D-based rendering implementation
  - `BitmapPainterBlend2D`: Bitmap operations using Blend2D
  - `TextRenderer`: New text rendering system replacing AGGTextRenderer
  - `BlendServerFont`: Blend2D font handling
- **Migration Path**: Parallel implementation allowing gradual transition
- **Key Files**:
  - `drawing/Painter/PainterBlend2D.cpp`: Core Blend2D renderer
  - `drawing/Painter/bitmap_painter/BitmapPainterBlend2D.cpp`: Bitmap operations
  - `drawing/Painter/TextRenderer.cpp`: Text rendering with Blend2D
  - `drawing/Painter/drawing_modes/Blend2DDrawingMode.h`: Drawing mode compatibility
- **Libraries**: Requires libblend2d.a and libasmjit.a (JIT compilation support)

### Font System Architecture
Located in `font/` directory:
- **FontManager**: Global font discovery and caching
- **FontEngine**: FreeType integration for font parsing
- **FontCache**: Glyph bitmap caching for performance
- **ServerFont**: Server-side font objects with metrics and rendering

### Input and Event System
- **EventDispatcher**: Routes input events to appropriate windows/views
- **InputManager**: Coordinates with input_server for device input
- **CursorManager**: System and application cursor management

### Window Decorators
Located in `decorator/` directory:
- **DecorManager**: Loads and manages window decoration add-ons
- **DefaultDecorator**: Built-in window title bars, borders, controls
- **Decorator API**: Extensible system for custom window appearance

### Multi-User and Screen Management
- **ScreenManager**: Multiple monitor configuration and hot-plug support
- **VirtualScreen**: Unified coordinate system across multiple displays
- **Workspace**: Virtual desktop implementation with per-workspace window lists

## Development Patterns

### Message-Based Architecture
app_server uses BMessage-based IPC for client communication:
- Client applications connect via `AS_GET_DESKTOP` messages
- Drawing commands sent as structured BMessage protocols
- Asynchronous event delivery for input and window updates

### Locking Strategy
Complex multi-level locking for thread safety:
- **Desktop window lock**: Controls window list modifications
- **Screen lock**: Hardware access synchronization  
- **Direct screen access**: Special locking for games/media apps
- **Painter lock**: Rendering pipeline synchronization

### Memory Management
- **ClientMemoryAllocator**: Shared memory for large data transfer
- **ServerBitmap**: Reference-counted bitmap storage
- **BitmapManager**: Global bitmap allocation and cleanup

### AGG Integration Patterns
When working with graphics code:
- Use `Painter` class for all drawing operations
- Drawing modes in `drawing_modes/` implement BeOS compatibility
- AGG-specific headers prefixed with `agg_` contain customizations
- Coordinate system transformations handled in `Transformable` class

### Performance Considerations
- **Region-based clipping**: All drawing operations use BRegion for efficiency
- **Dirty rectangle tracking**: Minimize redraws using precise damage tracking
- **Font caching**: Extensive glyph and font metric caching
- **Bitmap optimization**: Multiple code paths for different bitmap formats and scaling

## Testing and Debugging

### Build Validation
```bash
# Test app_server build with dependencies
jam -qa app_server

# Verify drawing subsystem
jam -qa libasdrawing.a libpainter.a

# Check AGG integration
jam -qa libagg.a
```

### Key Files for Graphics Development
- `drawing/Painter/Painter.cpp`: Core AGG rendering implementation
- `drawing/Painter/PainterBlend2D.cpp`: Blend2D rendering backend (NEW)
- `drawing/DrawingEngine.cpp`: Hardware abstraction layer
- `Desktop.cpp`: Window management and event coordination
- `ServerWindow.cpp`: Client window protocol handling
- `drawing/Painter/drawing_modes/`: BeOS drawing mode compatibility
- `drawing/Painter/TextRenderer.cpp`: Blend2D text rendering (NEW)
- `drawing/Painter/bitmap_painter/BitmapPainterBlend2D.cpp`: Blend2D bitmap ops (NEW)

## Important Notes for Development

### Build System Requirements
- **ALWAYS use HAIKU_OUTPUT_DIR**: Cross-compilation requires explicit output directory
- **JAM executable path**: Use `./buildtools/jam/bin.linuxx86/jam` for Linux builds
- **Build flags**: `-qa` ensures all dependencies are rebuilt correctly

### Active Migration Work
The codebase is undergoing migration from AGG to Blend2D graphics library. When working on graphics code:
1. Check if both AGG and Blend2D implementations exist
2. Prefer updating Blend2D implementations when making changes
3. Maintain compatibility between both backends during transition
4. Test changes with both rendering paths if possible

### Header File Organization
- AGG headers: Located in system headers under `headers/libs/agg/`
- Blend2D headers: Located in `headers/libs/blend2d/`
- AsmJIT headers: Located in `headers/libs/asmjit/` (Blend2D dependency)
- Custom AGG extensions: Prefixed with `agg_` in `drawing/Painter/`

This architecture enables Haiku's app_server to provide hardware-accelerated graphics with full BeOS API compatibility while supporting modern features like subpixel font rendering and multi-monitor setups.