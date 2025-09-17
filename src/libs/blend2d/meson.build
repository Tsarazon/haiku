# Blend2D library for Haiku - Meson build configuration

blend2d_inc = include_directories(
  'src',
  '../../headers/libs/blend2d'
)

# Compiler flags
blend2d_cflags = [
  '-O2',
  '-fno-strict-aliasing', 
  '-DHAIKU_TARGET_PLATFORM_HAIKU',
  '-Wno-unused-parameter',
  '-Wno-sign-compare'
]

# Enable JIT for supported architectures
if host_machine.cpu_family() == 'x86_64' or host_machine.cpu_family() == 'x86'
  blend2d_cflags += '-DBLEND2D_BUILD_JIT'
endif

# Core Blend2D source files
blend2d_sources = [
  # Core API
  'src/blend2d/api-build.cpp',
  'src/blend2d/api-impl.cpp',
  
  # Geometry
  'src/blend2d/geometry/geometry.cpp',
  'src/blend2d/geometry/path.cpp', 
  'src/blend2d/geometry/pathstroke.cpp',
  
  # Images
  'src/blend2d/image/image.cpp',
  'src/blend2d/image/imagecodec.cpp',
  
  # Pipeline/JIT
  'src/blend2d/pipeline/pipegen.cpp',
  'src/blend2d/pipeline/pipegencore.cpp', 
  'src/blend2d/pipeline/pipegenruntime.cpp',
  
  # Runtime
  'src/blend2d/runtime/runtime.cpp',
  
  # Support
  'src/blend2d/support/algorithm.cpp',
  'src/blend2d/support/memops.cpp',
  
  # Text
  'src/blend2d/text/textlayout.cpp',
  
  # Threading  
  'src/blend2d/threading/thread.cpp',
  'src/blend2d/threading/threadpool.cpp'
]

# Build static library
libblend2d = static_library(
  'blend2d',
  blend2d_sources,
  include_directories: blend2d_inc,
  c_args: blend2d_cflags,
  cpp_args: blend2d_cflags,
  install: false
)