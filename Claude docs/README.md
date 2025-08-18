# Haiku libbe Meson Build

This directory contains the Meson build system files for Haiku's `libbe.so` library, converted from the original Jam-based build system.

## Quick Start

### Prerequisites

1. **Meson Build System**: Install Meson (>= 0.60.0)
```bash
pip3 install meson ninja
```

2. **Dependencies**:
   - zlib development headers
   - ICU development headers
   - libzstd development headers (optional)
   - GCC or Clang with C++14 support

3. **Haiku Module**: Ensure the Haiku custom Meson module is available
```bash
export MESON_MODULE_PATH=/path/to/haiku/build/meson/modules:$MESON_MODULE_PATH
```

### Building

```bash
# Setup build directory
meson setup builddir

# Compile
meson compile -C builddir

# Install (optional)
meson install -C builddir
```

### Architecture-Specific Builds

The build system automatically detects the target architecture using `host_machine.cpu_family()`:

```bash
# For ARM64
meson setup builddir --cross-file aarch64-haiku.txt

# For x86_64 (default on x86_64 hosts)
meson setup builddir

# For 32-bit x86
meson setup builddir --cross-file i586-haiku.txt
```

### Build Options

Configure build options using `-D` flags:

```bash
# Development builds
meson setup builddir \
    -Drun_without_registrar=true \
    -Drun_without_app_server=true \
    -Denable_debug=true

# Production builds with tests
meson setup builddir \
    -Dbuild_tests=true \
    -Darchitecture_specific_optimizations=true
```

## Architecture Support

The build system supports conditional compilation based on target architecture:

| Architecture | Support Level | Debug Support | Notes |
|--------------|---------------|---------------|-------|
| x86_64       | Full          | Yes           | Primary desktop target |
| aarch64      | Full          | Yes           | ARM 64-bit (Raspberry Pi, etc.) |
| x86          | Full          | Yes           | Legacy 32-bit x86 |
| riscv64      | Full          | Yes           | RISC-V 64-bit |
| arm          | Partial       | Yes           | ARM 32-bit |

### Architecture-Specific Features

1. **Debug Support**: Each architecture has its own debug implementation in `../debug/arch/{arch}/`
2. **Resource Defines**: Architecture-specific preprocessor defines in resource files
3. **Optimization**: Architecture-specific compiler optimizations can be enabled

## Generated Outputs

### Libraries
- `libbe.so` - Main Haiku Be API library
- `libbe_test.so` - Test version (if `build_tests=true`)
- `libshared.a` - Internal shared utilities
- `libcolumnlistview.a` - Column list view widget

### Resources
- `libbe.rsrc` - Compiled resource file with architecture-specific content

### Localization
- `x-vnd.Haiku-libbe.catkeys` - Extracted translatable strings
- `x-vnd.Haiku-libbe.catalog` - Compiled localization catalog

## Cross-Compilation

For cross-compilation, create a cross-file (e.g., `aarch64-haiku.txt`):

```ini
[binaries]
c = 'aarch64-unknown-haiku-gcc'
cpp = 'aarch64-unknown-haiku-g++'
ar = 'aarch64-unknown-haiku-ar'
strip = 'aarch64-unknown-haiku-strip'

[host_machine]
system = 'haiku'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[properties]
module_path = '/path/to/haiku/build/meson/modules'
```

Then build with:
```bash
meson setup builddir --cross-file aarch64-haiku.txt
meson compile -C builddir
```

## Integration with Haiku Build System

To integrate with the larger Haiku build system:

1. **Add to main meson.build**:
```meson
subdir('src/kits/meson_build')
libbe_dep = dependency('be', fallback: ['libbe', 'libbe_dep'])
```

2. **Use in applications**:
```meson
executable('myapp', 'main.cpp',
    dependencies: [libbe_dep]
)
```

## Troubleshooting

### Common Issues

1. **Module not found**: Ensure `MESON_MODULE_PATH` includes the Haiku module directory
2. **Missing dependencies**: Install development packages for zlib, ICU, and optionally zstd
3. **Architecture mismatch**: Verify cross-compilation files match target architecture
4. **Resource compilation fails**: Check that the `rc` tool is available in PATH

### Debug Builds

Enable verbose output for debugging:
```bash
meson setup builddir --buildtype=debug
meson compile -C builddir -v
```

### Validation

Compare with original Jam build:
```bash
# List exported symbols
nm -D builddir/libbe.so | grep " T " > meson_symbols.txt
nm -D /path/to/jam/built/libbe.so | grep " T " > jam_symbols.txt
diff jam_symbols.txt meson_symbols.txt
```

## Contributing

When adding new source files or modifying the build:

1. Update the appropriate source file list in `meson.build`
2. Add architecture-specific files using conditional inclusion
3. Update resource files with new architecture defines if needed
4. Test on all supported architectures
5. Update this documentation

## Performance

Meson build performance improvements over Jam:

- **Parallel Compilation**: Better utilization of multiple cores
- **Incremental Builds**: Only recompile changed files
- **Dependency Tracking**: Automatic header dependency tracking
- **Cross-Platform**: Native support for different host platforms

Typical build times:
- **Full build**: ~2-3 minutes (8-core system)
- **Incremental**: ~10-30 seconds (single file change)
- **Clean rebuild**: ~3-4 minutes (8-core system)