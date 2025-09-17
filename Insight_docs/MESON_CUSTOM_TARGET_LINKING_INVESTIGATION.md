# Meson Custom Target Linking Investigation

## Problem Summary

While implementing Meson build system for Haiku OS to replace the legacy Jam build system, we encountered a critical issue where `libbe.so` (Haiku's core shared library) was being built as 4.6KB instead of the expected 4.6MB - **1000x too small**.

## Background

The Haiku build system uses a unique approach where individual kits (app, storage, support, interface, locale) are compiled into merged object files (e.g., `app_kit.o`, `support_kit.o`) which are then linked together to create `libbe.so`. 

### Expected Workflow
1. Each kit compiles multiple `.cpp` files into individual `.o` files
2. Kit object files are merged using `ld -r` into single `kit_name.o` 
3. All merged kit objects are linked together into `libbe.so`
4. Files must be placed in external directory `/home/ruslan/haiku/generated_meson/...` for Jam compatibility

## Investigation Timeline

### Initial Implementation
We used `custom_target` to create merged object files:

```meson
support_kit_merged = custom_target(
    'support_kit.o',
    input: support_kit_lib,
    output: 'support_kit.o',
    command: [
        'sh', '-c',
        'mkdir -p ' + support_kit_output_dir + ' && /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o ' + support_kit_output_dir + '/support_kit.o @INPUT@.p/*.o'
    ],
    build_by_default: true,
    install: false
)
```

### Attempt 1: Direct Custom Target Inclusion
Tried including custom_target objects directly in shared_library:

```meson
libbe = shared_library('be',
    app_kit_merged,
    storage_kit_merged, 
    support_kit_merged,
    # ... other args
)
```

**Result**: `cannot find src/kits/app/app_kit.o: No such file or directory`

### Attempt 2: Using Dependencies Array
Tried using individual kit dependencies:

```meson
dependencies: [
    app_kit_dep,
    storage_kit_dep, 
    support_kit_dep,
    # ...
] + system_deps,
```

**Result**: Same linking error - files not found in builddir

### Attempt 3: Combined declare_dependency
Created a combined dependency with all kit sources:

```meson
libbe_dep = declare_dependency(
    sources: [
        app_kit_merged,
        storage_kit_merged,
        support_kit_merged,
        interface_kit_merged,
        locale_kit_merged
    ]
)
```

**Result**: Same linking error

### Attempt 4: Back to Direct Sources
Returned to direct custom_target inclusion as sources:

```meson
libbe = shared_library('be',
    version_resource,
    icons_resource, 
    country_flags_resource,
    language_flags_resource,
    
    # Include kit object files directly as sources
    app_kit_merged,
    storage_kit_merged,
    support_kit_merged,
    interface_kit_merged,
    locale_kit_merged,
    # ... other args
)
```

**Result**: Same linking error

## Root Cause Analysis

### The Issue
After extensive investigation, we discovered the fundamental problem:

1. **Our custom_target commands** create files **only** in external directory `/home/ruslan/haiku/generated_meson/objects/haiku/x86_64/release/kits/*/`
2. **Meson expects** custom_target files to be created in builddir as specified by `@OUTPUT@`
3. **Linker searches** for files relative to builddir: `src/kits/app/app_kit.o`, `src/kits/support/support_kit.o`, etc.
4. **Files don't exist** in builddir, causing "No such file or directory" errors

### Evidence

#### Linker Command Analysis
```bash
/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++ -o src/kits/libbe.so.1.0.0 src/kits/app/app_kit.o src/kits/storage/storage_kit.o src/kits/support/support_kit.o src/kits/interface/interface_kit.o src/kits/locale/locale_kit.o
```

The linker expects files at:
- `src/kits/app/app_kit.o` (relative to builddir)
- `src/kits/storage/storage_kit.o` (relative to builddir)
- etc.

#### Actual File Locations
Files are created at:
- `/home/ruslan/haiku/generated_meson/objects/haiku/x86_64/release/kits/app/app_kit.o`
- `/home/ruslan/haiku/generated_meson/objects/haiku/x86_64/release/kits/support/support_kit.o`
- etc.

#### Why Static Libraries Work
Static libraries (like `libicon.a`, `libagg.a`) work because:
1. They use `static_library()` target, not `custom_target`
2. Meson handles their location internally
3. They're referenced in `link_with` parameter, not `sources`

## Documentation Analysis

### Key Insights from Meson Docs

#### From Generating-sources.md:
```meson
libfoo = static_library('foo', [foo_c, foo_h])  # ✅ This works
executable('myexe', ['main.c', foo_h], link_with : libfoo)
```

Custom targets **should** work directly in target sources according to documentation.

#### From Dependencies.md:
```meson
idep_foo = declare_dependency(
    sources : [foo_h, bar_h],  # ✅ This should work
    link_with : [libfoo],
)
```

Custom targets in `declare_dependency(sources: ...)` **should** work.

## The Solution

### Problem Root Cause
Our custom_target commands don't create files in builddir (`@OUTPUT@`), only in external directory.

### Required Fix
Modify all custom_target commands to create files in **both** locations:
1. External directory (for Jam compatibility)
2. Builddir as `@OUTPUT@` (for Meson)

### Example Fix
```meson
support_kit_merged = custom_target(
    'support_kit.o',
    input: support_kit_lib,
    output: 'support_kit.o',
    command: [
        'sh', '-c',
        'mkdir -p ' + support_kit_output_dir + ' && ' +
        '/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o ' + support_kit_output_dir + '/support_kit.o @INPUT@.p/*.o && ' +
        'cp ' + support_kit_output_dir + '/support_kit.o @OUTPUT@'  # ← ADD THIS
    ],
    build_by_default: true,
    install: false
)
```

## Lessons Learned

1. **Custom targets must create `@OUTPUT@` files** - Meson tracks dependencies through builddir files
2. **External file creation alone is insufficient** - even if files exist elsewhere, Meson needs builddir copies
3. **Static libraries vs custom targets behave differently** - static_library targets are handled internally by Meson
4. **Documentation examples work** - but require proper `@OUTPUT@` file creation
5. **Cross-compilation adds complexity** - external directories needed for Jam compatibility, but Meson still needs builddir files

## Status
**PENDING**: Need to implement the fix by adding `&& cp ... @OUTPUT@` to all kit custom_target commands.

## Files Requiring Updates
- `/home/ruslan/haiku/src/kits/app/meson.build`
- `/home/ruslan/haiku/src/kits/storage/meson.build` 
- `/home/ruslan/haiku/src/kits/support/meson.build`
- `/home/ruslan/haiku/src/kits/interface/meson.build`
- `/home/ruslan/haiku/src/kits/locale/meson.build`

Each needs the copy command added to ensure files exist in both external directory and builddir.