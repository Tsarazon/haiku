# Blend2D Library - Meson Build Configuration for Haiku
# High-performance 2D vector graphics engine with JIT compilation

# Blend2D core source files
blend2d_sources = [
    'api-build.cpp',
    'api-globals.cpp',
    'api-impl.cpp',
    'api-nocxx.cpp',
    'array.cpp',
    'bitarray.cpp',
    'bitset.cpp',
    'compression/deflate.cpp', 
    'compression/zlib.cpp',
    'context.cpp',
    'font.cpp',
    'fontdata.cpp',
    'fontface.cpp',
    'fontfeaturesettings.cpp',
    'fontmanager.cpp',
    'fonttagdata.cpp',
    'fonttagdataids.cpp',
    'fontvariationsettings.cpp',
    'format.cpp',
    'geometry.cpp',
    'gradient.cpp',
    'image.cpp',
    'imagecodec.cpp',
    'imagedecoder.cpp',
    'imageencoder.cpp',
    'matrix.cpp',
    'object.cpp',
    'path.cpp',
    'pattern.cpp',
    'pixelconverter.cpp',
    'random.cpp',
    'region.cpp',
    'runtime.cpp',
    'string.cpp',
    'style.cpp',
    'support/algorithm.cpp',
    'support/math.cpp',
    'support/memops.cpp',
    'tables/tables.cpp',
    'var.cpp',
]

# Image codec sources
blend2d_codec_sources = [
    'codec/bmpcodec.cpp',
    'codec/jpegcodec.cpp', 
    'codec/pngcodec.cpp',
    'codec/qoicodec.cpp',
]

# OpenType font engine sources
blend2d_opentype_sources = [
    'opentype/otcff.cpp',
    'opentype/otcmap.cpp',
    'opentype/otcore.cpp',
    'opentype/otdefs.cpp',
    'opentype/otface.cpp',
    'opentype/otglyf.cpp',
    'opentype/othb.cpp',
    'opentype/otkern.cpp',
    'opentype/otlayout.cpp',
    'opentype/otmetrics.cpp',
    'opentype/otname.cpp',
]

# Pipeline sources (rendering engine)
blend2d_pipeline_sources = [
    'pipeline/pipedefs.cpp',
    'pipeline/pipegencore.cpp',
    'pipeline/pipegenruntime.cpp',
    'pipeline/pipegen.cpp',
]

# JIT pipeline sources (performance critical)
blend2d_jit_sources = [
    'pipeline/jit/compoppart.cpp',
    'pipeline/jit/fetchpart.cpp',
    'pipeline/jit/fetchpixelptrpart.cpp',
    'pipeline/jit/fetchsolidpart.cpp',
    'pipeline/jit/fillpart.cpp',
    'pipeline/jit/jitbase.cpp',
    'pipeline/jit/pipecompiler.cpp',
    'pipeline/jit/pipefunction.cpp',
    'pipeline/jit/pipegencore.cpp',
    'pipeline/jit/pipejitruntime.cpp',
    'pipeline/jit/pipepart.cpp',
    'pipeline/jit/pipeprimitives.cpp',
]

# Threading sources  
blend2d_threading_sources = [
    'threading/atomic.cpp',
    'threading/conditionvariable.cpp',
    'threading/mutex.cpp',
    'threading/thread.cpp',
    'threading/threadpool.cpp',
    'threading/uniqueidgenerator.cpp',
]

# AsmJit JIT compiler sources (embedded)
asmjit_sources = [
    'asmjit/core/archtraits.cpp',
    'asmjit/core/assembler.cpp',
    'asmjit/core/builder.cpp',
    'asmjit/core/codebuffer.cpp',
    'asmjit/core/codeholder.cpp',
    'asmjit/core/compiler.cpp',
    'asmjit/core/constpool.cpp',
    'asmjit/core/cpuinfo.cpp',
    'asmjit/core/emitter.cpp',
    'asmjit/core/emithelper.cpp',
    'asmjit/core/environment.cpp',
    'asmjit/core/errorhandler.cpp',
    'asmjit/core/formatter.cpp',
    'asmjit/core/func.cpp',
    'asmjit/core/funcargscontext.cpp',
    'asmjit/core/globals.cpp',
    'asmjit/core/inst.cpp',
    'asmjit/core/jitallocator.cpp',
    'asmjit/core/jitruntime.cpp',
    'asmjit/core/logger.cpp',
    'asmjit/core/operand.cpp',
    'asmjit/core/osutils.cpp',
    'asmjit/core/ralocal.cpp',
    'asmjit/core/rapass.cpp',
    'asmjit/core/rastack.cpp',
    'asmjit/core/string.cpp',
    'asmjit/core/support.cpp',
    'asmjit/core/target.cpp',
    'asmjit/core/type.cpp',
    'asmjit/core/virtmem.cpp',
    'asmjit/core/zone.cpp',
    'asmjit/core/zonehash.cpp',
    'asmjit/core/zonelist.cpp',
    'asmjit/core/zonestack.cpp',
    'asmjit/core/zonetree.cpp',
    'asmjit/core/zonevector.cpp',
    'asmjit/x86/x86assembler.cpp',
    'asmjit/x86/x86builder.cpp',
    'asmjit/x86/x86compiler.cpp',
    'asmjit/x86/x86emithelper.cpp',
    'asmjit/x86/x86formatter.cpp',
    'asmjit/x86/x86func.cpp',
    'asmjit/x86/x86instapi.cpp',
    'asmjit/x86/x86instdb.cpp',
    'asmjit/x86/x86rapass.cpp',
]

# Combine all sources
all_blend2d_sources = (
    blend2d_sources + 
    blend2d_codec_sources + 
    blend2d_opentype_sources +
    blend2d_pipeline_sources +
    blend2d_jit_sources +
    blend2d_threading_sources +
    asmjit_sources
)

message('Building Blend2D Library with ' + all_blend2d_sources.length().to_string() + ' source files')

# Blend2D library object  
libblend2d = static_library('blend2d',
    all_blend2d_sources,
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('.'),           # Blend2D sources
        include_directories('asmjit'),      # AsmJit headers
    ],
    cpp_args: [
        '-O2',                              # Release optimization
        '-fno-strict-aliasing',             # Standard Haiku flag
        '-fvisibility=hidden',              # Hide internal symbols
        '-DBL_STATIC',                      # Static library build
        '-DBL_NO_STDCXX',                   # Minimal C++ stdlib usage
        '-DASMJIT_STATIC',                  # Static AsmJit build
        '-DASMJIT_NO_STDCXX',               # Minimal C++ stdlib for AsmJit
        '-w',                               # Disable warnings 
        '-DHAIKU_TARGET_PLATFORM_HAIKU',    # Haiku platform
    ],
    c_args: [
        '-O2',
        '-fno-strict-aliasing', 
        '-fvisibility=hidden',
        '-w'
    ],
    pic: true,
    install: false
)

message('Blend2D Library configured successfully - high-performance 2D graphics with JIT compilation')