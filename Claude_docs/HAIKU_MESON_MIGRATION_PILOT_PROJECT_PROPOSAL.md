# Haiku Build System Migration: Pilot Project Proposal
## Three Candidate Components for Meson Migration

### Executive Summary

This document proposes three carefully selected components from the Haiku source code to serve as pilot projects for the Jam to Meson build system migration. Each candidate represents different aspects of Haiku's build complexity while maintaining manageable scope and dependencies, making them ideal for validating the migration approach and establishing patterns for the broader transition.

### Selection Criteria

The pilot candidates were chosen based on:

1. **Low Dependency Count**: Minimal external dependencies to reduce migration complexity
2. **Representative Complexity**: Each component demonstrates different build system features
3. **Cross-Compilation Relevance**: Components that benefit from simplified cross-compilation
4. **Validation Value**: Results that can inform the broader migration strategy
5. **Incremental Risk**: Manageable scope that allows for thorough testing and refinement

## Candidate 1: `data_to_source` Build Tool

### Component Overview

**Location**: `src/tools/data_to_source.cpp`
**Type**: Simple build utility
**Current Build Rule**: `BuildPlatformMain <build>data_to_source : data_to_source.cpp : $(HOST_LIBSUPC++)`

### Description

A standalone utility that converts binary data files into C source code arrays. This tool is used throughout the build system to embed binary resources directly into compiled code.

### Complexity Analysis

#### Build Complexity: **Low**
- **Single source file**: `data_to_source.cpp` (120 lines)
- **Minimal dependencies**: Only requires standard C++ library
- **No architecture-specific code**: Pure standard C/C++
- **No resource compilation**: No .rdef files or resources
- **No localization**: No catalog files

#### Current Jam Build Pattern
```jam
BuildPlatformMain <build>data_to_source : data_to_source.cpp 
    : $(HOST_LIBSUPC++) ;
```

#### Proposed Meson Equivalent
```meson
data_to_source = executable('data_to_source',
  'data_to_source.cpp',
  dependencies: [cpp_stdlib],
  native: true,
  install: false
)
```

### Dependency Analysis

#### External Dependencies: **Minimal**
- Standard C library functions (stdio.h, stdlib.h, string.h, unistd.h)
- File system operations (fcntl.h, sys/stat.h)
- Host C++ standard library (libsupc++)

#### Internal Dependencies: **None**
- No Haiku-specific headers
- No custom libraries
- No architecture-specific code

### Cross-Compilation Considerations

#### Current Challenges
- Must be built for host platform to run during target builds
- Currently uses `BuildPlatformMain` rule for host compilation
- Requires proper host toolchain detection

#### Meson Benefits
- Native cross-compilation support with `native: true` flag
- Automatic host/target toolchain separation
- Simplified host tool building patterns

### Compiler and Linker Flags Analysis

#### Jam Build Configuration
```jam
BuildPlatformMain <build>data_to_source : data_to_source.cpp : $(HOST_LIBSUPC++) ;
```

#### Extracted Flags from ArchitectureRules and BuildSetup

**Base Compiler Flags (all architectures):**
- `-pipe` (if HAIKU_USE_GCC_PIPE=1)
- `-fno-strict-aliasing` (non-legacy GCC)
- `-fno-delete-null-pointer-checks` (non-legacy GCC)
- `-nostdinc` (for Haiku builds)

**Architecture-Specific Flags:**
- **x86_64**: `-march=pentium` (legacy), `-march=native` (modern)
- **ARM64**: `-march=armv8.6-a+crypto+lse2` 
- **x86**: `-march=pentium`, `-fno-builtin-fork`, `-fno-builtin-vfork`
- **RISC-V64**: `-march=rv64gc`, `-mno-relax` (Clang)

**Warning Flags:**
- `-Wall`
- `-Wno-multichar` 
- `-Wpointer-arith`
- `-Wsign-compare`
- `-Wmissing-prototypes` (C only)
- `-Wno-ctor-dtor-privacy` (C++ only)
- `-Woverloaded-virtual` (C++ only)

**Debug Flags:**
- `-ggdb` (debug builds)
- `-DNDEBUG` (release builds)

**Link Libraries:**
- `$(HOST_LIBSUPC++)` = `supc++` (C++ support library)

### Justification for Selection

#### 1. **Ideal Starting Point**
- Minimal complexity allows focus on basic Meson patterns
- No complex dependencies to resolve
- Clear success criteria (tool works identically)

#### 2. **Build Tool Category Validation**
- Represents the "build tools" category used throughout Haiku
- Pattern can be applied to 20+ similar tools in `/src/tools/`
- Establishes foundation for more complex tools

#### 3. **Cross-Compilation Foundation**
- Validates host vs target compilation patterns
- Tests toolchain selection mechanisms
- Proves Meson's native build capabilities

#### 4. **Low Risk, High Learning Value**
- Failure has minimal impact on development
- Success provides confidence for more complex migrations
- Quick feedback cycle for initial Meson setup

### Expected Migration Effort

- **Time Estimate**: 2-4 hours
- **Risk Level**: Very Low
- **Complexity**: Trivial to Low

## Candidate 2: `addattr` Application

### Component Overview

**Location**: `src/bin/addattr/`
**Type**: Command-line application with BeAPI usage
**Current Build Rule**: `Application addattr : main.cpp addAttr.cpp : be`

### Description

A command-line utility for adding file attributes to Haiku files. This tool demonstrates typical Haiku application patterns including BeAPI usage, multiple source files, and system integration.

### Complexity Analysis

#### Build Complexity: **Medium**
- **Multiple source files**: `main.cpp` (command-line parsing) and `addAttr.cpp` (core functionality)
- **BeAPI dependency**: Links against libbe.so for file system operations
- **Standard Haiku application pattern**: Uses Application rule
- **No resource compilation**: No .rdef files
- **No localization**: No catalog support

#### Current Jam Build Pattern
```jam
SubDir HAIKU_TOP src bin addattr ;

Application addattr :
    main.cpp
    addAttr.cpp
    : be
    ;
```

#### Proposed Meson Equivalent
```meson
addattr_sources = files([
  'main.cpp',
  'addAttr.cpp'
])

addattr = executable('addattr',
  addattr_sources,
  dependencies: [libbe_dep],
  install: true,
  install_dir: 'bin'
)
```

### Dependency Analysis

#### External Dependencies: **Low-Medium**
- **libbe.so**: Haiku's main application framework library
- Standard C++ library (file operations, string handling)

#### Internal Dependencies: **Medium**
- BeAPI headers (Node.h, Entry.h, Volume.h, fs_attr.h)
- Haiku-specific attribute system
- Standard POSIX file operations

### Cross-Compilation Considerations

#### Current Challenges
- Must be built for target architecture (Haiku)
- Requires proper Haiku headers and libbe.so for target
- BeAPI dependency requires target system libraries

#### Meson Benefits
- Standard executable building with dependency management
- Automatic library discovery and linking
- Cross-compilation with proper dependency resolution

### Justification for Selection

#### 1. **Representative Application Pattern**
- Demonstrates typical Haiku command-line application build
- Multi-file source organization
- BeAPI integration patterns
- Standard executable installation

#### 2. **Library Dependency Management**
- Tests Meson's dependency system with libbe.so
- Validates cross-compilation with system libraries
- Proves library detection and linking mechanisms

### Compiler and Linker Flags Analysis

#### Jam Build Configuration
```jam
Application addattr : main.cpp addAttr.cpp : be ;
```

#### Extracted Flags from Application Rule and ArchitectureRules

**Application-Specific Flags:**
- `-Xlinker -soname=_APP_` (application identifier)
- `-nostdlib` (manual library control)
- `-Xlinker --no-undefined` (strict linking)

**Base Compiler Flags (all architectures):**
- `-pipe` (build optimization)
- `-fno-strict-aliasing` (compatibility)
- `-fno-delete-null-pointer-checks` (safety)
- `-nostdinc` (controlled headers)

**Architecture-Specific Flags:**
- **x86_64**: `-march=pentium` (or tuned), warning flags
- **ARM64**: `-march=armv8.6-a+crypto+lse2`, ARMv8 optimizations
- **x86**: `-march=pentium`, `-fno-builtin-fork`, `-fno-builtin-vfork`
- **RISC-V64**: `-march=rv64gc`, RISC-V extensions

**Warning Flags:**
- `-Wall` (all warnings)
- `-Wno-multichar` (Haiku compatibility)
- `-Wpointer-arith` (pointer safety)
- `-Wsign-compare` (sign safety)
- `-Wno-ctor-dtor-privacy` (C++ compatibility)
- `-Woverloaded-virtual` (C++ safety)

**BeAPI Integration:**
- Links against `libbe.so` (Haiku application framework)
- Automatic glue code insertion (HAIKU_EXECUTABLE_BEGIN_GLUE_CODE)
- Target-specific library dependencies

#### 3. **Scalable Pattern**
- Success pattern applies to 100+ similar applications in `/src/bin/`
- Establishes standard for Haiku application building
- Foundation for more complex applications

#### 4. **Moderate Complexity**
- More complex than simple tools but still manageable
- No resource or localization complexity
- Clear success/failure criteria

### Expected Migration Effort

- **Time Estimate**: 4-8 hours (including dependency setup)
- **Risk Level**: Low to Medium
- **Complexity**: Medium

## Candidate 3: `syslog_daemon` Server

### Component Overview

**Location**: `src/servers/syslog_daemon/`
**Type**: System server with resources and localization
**Current Build Rules**: `Application`, `AddResources`, `DoCatalogs`

### Description

The system logging daemon that handles log message collection and distribution. This component represents the full complexity of Haiku builds including resource compilation, localization catalogs, and system integration.

### Complexity Analysis

#### Build Complexity: **High**
- **Multiple source files**: 3 C++ sources (SyslogDaemon.cpp, syslog_output.cpp, listener_output.cpp)
- **Resource compilation**: SyslogDaemon.rdef → .rsrc compilation
- **Localization support**: DoCatalogs for i18n catalog generation
- **Multiple library dependencies**: libbe, libsupc++, localestub
- **Private header usage**: System-internal headers

#### Current Jam Build Pattern
```jam
SubDir HAIKU_TOP src servers syslog_daemon ;

UsePrivateHeaders app syslog_daemon ;
UsePrivateSystemHeaders ;

AddResources syslog_daemon : SyslogDaemon.rdef ;

Application syslog_daemon :
    SyslogDaemon.cpp
    syslog_output.cpp
    listener_output.cpp
    :
    be [ TargetLibsupc++ ] localestub
;

DoCatalogs syslog_daemon :
    x-vnd.Haiku-SystemLogger
    :
    SyslogDaemon.cpp
;
```

#### Proposed Meson Equivalent
```meson
# Resource compilation
syslog_daemon_rsrc = custom_target('syslog_daemon.rsrc',
  input: 'SyslogDaemon.rdef',
  output: 'SyslogDaemon.rsrc',
  command: [rc_compiler, '@INPUT@', '@OUTPUT@'],
  depends: rc_compiler
)

# Localization catalog
syslog_daemon_catalog = custom_target('syslog_daemon.catalog',
  input: 'SyslogDaemon.cpp',
  output: 'x-vnd.Haiku-SystemLogger.catalog',
  command: [catalog_generator, '@INPUT@', '@OUTPUT@'],
  depends: catalog_tools
)

# Main executable
syslog_daemon_sources = files([
  'SyslogDaemon.cpp',
  'syslog_output.cpp', 
  'listener_output.cpp'
])

syslog_daemon = executable('syslog_daemon',
  syslog_daemon_sources,
  dependencies: [
    libbe_dep,
    libsupc_dep,
    localestub_dep
  ],
  link_with: [syslog_daemon_rsrc],
  install: true,
  install_dir: 'system/servers'
)
```

### Dependency Analysis

#### External Dependencies: **High**
- **libbe.so**: Primary Haiku application framework
- **libsupc++**: C++ runtime support
- **localestub**: Localization stub library
- **Resource compiler (rc)**: For .rdef → .rsrc conversion
- **Catalog tools**: For localization catalog generation

#### Internal Dependencies: **High**
- Private Haiku headers (app, syslog_daemon subsystems)
- Private system headers
- Haiku-specific logging APIs
- System server integration patterns

### Cross-Compilation Considerations

#### Current Challenges
- Complex build dependencies (resource compiler, catalog tools)
- Multiple build phases (preprocessing, compilation, resource compilation, catalog generation)
- Target-specific system integration
- Private header access across architectures

#### Meson Benefits
- Custom target support for complex build steps
- Proper dependency tracking across build phases  
- Cross-compilation aware custom tools
- Simplified multi-step build processes

### Justification for Selection

#### 1. **Complete Build System Validation**
- Tests all major Haiku build features
- Resource compilation patterns
- Localization system integration
- Multi-dependency management

#### 2. **Real-World Complexity**
- Represents actual system components
- Full feature set validation
- Complex dependency resolution
- Multi-phase build processes

### Compiler and Linker Flags Analysis

#### Jam Build Configuration
```jam
UsePrivateHeaders app syslog_daemon ;
UsePrivateSystemHeaders ;
AddResources syslog_daemon : SyslogDaemon.rdef ;
Application syslog_daemon :
    SyslogDaemon.cpp syslog_output.cpp listener_output.cpp :
    be [ TargetLibsupc++ ] localestub ;
DoCatalogs syslog_daemon : x-vnd.Haiku-SystemLogger : SyslogDaemon.cpp ;
```

#### Extracted Flags from Complex Build Rules

**Private Header Access:**
- `-I$(HAIKU_PRIVATE_HEADERS)/app` (private application headers)
- `-I$(HAIKU_PRIVATE_HEADERS)/syslog_daemon` (component-specific headers)
- `-I$(HAIKU_PRIVATE_SYSTEM_HEADERS)` (system-internal headers)

**System Server Flags:**
- `-Xlinker -soname=_APP_` (application identifier)
- `-nostdlib` (manual library control)
- `-Xlinker --no-undefined` (strict linking)
- System service installation paths

**Base Compiler Flags (all architectures):**
- `-pipe` (build optimization)
- `-fno-strict-aliasing` (compatibility)
- `-fno-delete-null-pointer-checks` (safety)
- `-nostdinc` (controlled headers)

**Architecture-Specific Flags:**
- **x86_64**: `-march=pentium` (baseline), optimized for compatibility
- **ARM64**: `-march=armv8.6-a+crypto+lse2`, full ARMv8 features
- **x86**: `-march=pentium`, `-fno-builtin-fork`, `-fno-builtin-vfork`
- **RISC-V64**: `-march=rv64gc`, RISC-V base + extensions

**Warning Flags (System Server):**
- `-Wall` (all warnings enabled)
- `-Wno-multichar` (Haiku string compatibility)
- `-Wpointer-arith` (pointer arithmetic safety)
- `-Wsign-compare` (sign comparison safety)
- `-Wno-ctor-dtor-privacy` (C++ private member access)
- `-Woverloaded-virtual` (C++ virtual function safety)

**System Library Dependencies:**
- `libbe.so` (Haiku application framework)
- `libsupc++` (C++ support library, target-specific)
- `localestub` (localization stub library)
- Automatic glue code for system services

**Resource and Localization Integration:**
- Resource compilation: `SyslogDaemon.rdef → SyslogDaemon.rsrc`
- Catalog generation: source → catalog keys → localization catalogs
- Multi-phase dependency tracking

#### 3. **System Integration Patterns**
- Server application deployment patterns
- System directory installation
- Service integration validation
- Cross-architecture system building

#### 4. **Migration Strategy Validation**
- Tests most complex migration scenarios
- Validates custom build step handling
- Proves localization migration approach
- Establishes patterns for remaining servers

### Expected Migration Effort

- **Time Estimate**: 12-20 hours (including custom tool setup)
- **Risk Level**: Medium to High
- **Complexity**: High

## Migration Strategy and Timeline

### Phase 1: Foundation Tool (`data_to_source`)
**Duration**: 1 week
**Goals**: 
- Establish basic Meson project structure
- Validate host tool building patterns
- Create initial build documentation
- Test cross-compilation toolchain setup

**Success Criteria**:
- Tool builds and produces identical output to Jam version
- Works on all supported host platforms
- Can be integrated into larger build process

### Phase 2: Application with Dependencies (`addattr`)
**Duration**: 2 weeks  
**Goals**:
- Validate Haiku library dependency handling
- Establish standard application build patterns
- Test target executable cross-compilation
- Create reusable application build templates

**Success Criteria**:
- Application builds and functions identically to Jam version
- Library dependencies resolve correctly for target architecture
- Installation integration works properly
- Cross-compilation produces working ARM64 binaries

### Phase 3: Complex Component (`syslog_daemon`)
**Duration**: 3 weeks
**Goals**:
- Implement resource compilation pipeline
- Create localization catalog build system
- Validate complex dependency resolution
- Test complete system integration

**Success Criteria**:
- All build phases work correctly (source→object→resource→catalog→executable)
- Generated catalogs are identical to Jam versions
- Server integrates properly with system
- Multi-architecture packages build correctly

### Total Timeline: 6 weeks

## Risk Assessment and Mitigation

### Low Risk (data_to_source)
**Risks**: Minimal - basic toolchain issues
**Mitigation**: Comprehensive toolchain testing, fallback to existing Jam build

### Medium Risk (addattr)  
**Risks**: Library dependency resolution, cross-compilation issues
**Mitigation**: Incremental dependency addition, parallel Jam build validation

### High Risk (syslog_daemon)
**Risks**: Custom build steps, resource compilation, localization complexity
**Mitigation**: Custom tool development, extensive testing, phased feature addition

## Success Metrics

### Quantitative Metrics
1. **Build Time Comparison**: Meson builds ≤ 110% of Jam build times
2. **Binary Compatibility**: 100% identical functionality
3. **Cross-Compilation Setup**: ARM64 builds working within setup time targets
4. **Resource Validation**: Identical .rsrc and .catalog files

### Qualitative Metrics  
1. **Developer Experience**: Improved build system understandability
2. **Documentation Quality**: Complete migration documentation for each pattern
3. **Pattern Reusability**: Clear templates for similar components
4. **Community Acceptance**: Positive feedback from core developers

## Learning Objectives

### Technical Learning
1. **Meson Best Practices**: Establish Haiku-specific Meson patterns
2. **Cross-Compilation**: Validate simplified ARM64 building
3. **Custom Build Steps**: Resource and catalog compilation pipelines
4. **Dependency Management**: Complex library and tool dependencies

### Process Learning
1. **Migration Methodology**: Proven approach for component migration
2. **Testing Strategies**: Validation methods for build system changes
3. **Documentation Patterns**: Effective knowledge transfer techniques
4. **Community Integration**: Developer adoption and feedback processes

## Expected Outcomes

### Immediate Benefits
- **Proof of Concept**: Meson viability for Haiku demonstrated
- **Migration Patterns**: Reusable templates for remaining components
- **Developer Confidence**: Early success builds community support
- **Technical Foundation**: Core infrastructure for broader migration

### Strategic Value
- **Risk Reduction**: Early problem identification and resolution
- **Timeline Validation**: Realistic estimates for full migration
- **Resource Planning**: Accurate effort and skill requirements
- **Technology Validation**: Meson capability confirmation for Haiku's needs

## Conclusion

These three pilot candidates provide a comprehensive validation path for the Haiku build system migration from Jam to Meson. Starting with the simple `data_to_source` tool establishes basic patterns and confidence, progressing through the `addattr` application validates dependency management and cross-compilation, and culminating with `syslog_daemon` proves the migration approach can handle Haiku's full build complexity.

The progression from low to high complexity allows for iterative learning and pattern refinement while maintaining manageable risk at each stage. Success with these pilots will provide the foundation, confidence, and proven methodology needed for the broader migration to achieve its primary goal of simplified cross-compilation for ARM64 and future architectures.

Each component represents critical aspects of Haiku's build system - development tools, user applications, and system services - ensuring that the migration patterns developed will scale effectively to the remaining thousands of components in the Haiku source tree.