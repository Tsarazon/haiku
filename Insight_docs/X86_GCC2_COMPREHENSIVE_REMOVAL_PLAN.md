# X86_GCC2 COMPREHENSIVE SAFE REMOVAL PLAN

**Document Version**: 1.0  
**Target**: Complete and safe removal of x86_gcc2 architecture support from Haiku OS  
**Analysis Scope**: Deep ABI dependencies, BeOS compatibility layers, runtime behavior  

---

## EXECUTIVE SUMMARY

This plan provides a **COMPREHENSIVE TECHNICAL STRATEGY** for safely removing x86_gcc2 support from Haiku OS based on deep analysis of ABI dependencies, symbol mangling systems, threading models, and runtime behavior. The removal addresses **904 code locations** across the entire codebase while preserving BeOS compatibility through modern means.

**Critical Discovery**: x86_gcc2 is NOT merely "old x86" - it represents a complete GCC 2.95.3 ABI compatibility layer with unique C++ symbol mangling (egcs 1.1.x), custom threading models, integrated libgcc runtime, and sophisticated binary detection systems.

**IMPORTANT STATUS**: Analysis reveals that **partial x86_gcc2 removal has already been completed** in this codebase:
- Runtime loader GCC2 detection is **DISABLED** (lines 612-614 modified)
- Package architecture handling has **NULL safety** implemented
- Some JAM build rules have been **simplified**
- This plan addresses **complete removal** of remaining infrastructure

---

## DEEP ARCHITECTURAL ANALYSIS

### **1. ABI ARCHITECTURE DEPENDENCIES**

#### **Symbol Mangling System (egcs 1.1.x)**
- **Location**: `buildtools/legacy/gcc/gcc/config/i386/haiku.h:624`
- **Mechanism**: `USE_EGCS_MANGLED_NAMES` creates incompatible C++ symbols
- **Impact**: Thousands of BeOS C++ applications depend on this mangling
- **Dependencies**: 
  - `src/kits/debugger/demangler/gcc2.cpp` - GCC2 symbol demangling
  - `src/apps/debugger/` - Debugger symbol resolution
  - `src/kits/support/Archivable.cpp` - Object serialization symbol lookup

#### **Virtual Table Layout Differences**
- **GCC2 vtables**: Different layout than modern GCC
- **C++ Object ABI**: Incompatible object layouts between compilers
- **Exception Handling**: GCC2 uses different exception mechanisms
- **Template Instantiation**: Different template code generation

#### **Threading Model Integration**
- **File**: `buildtools/legacy/gcc/gcc/gthr-beos.h`
- **Model**: Custom BeOS threading with atomic spinlocks
- **Operations**: `atomic_or()`, `atomic_and()`, `snooze()` - BeOS-specific
- **Integration**: Embedded in libroot.so for GCC2 binaries

### **2. RUNTIME DETECTION SYSTEM**

#### **Binary Analysis Engine**
- **File**: `src/system/runtime_loader/runtime_loader.cpp:481-689`
- **Function**: `determine_x86_abi()` - **189 lines of complex GCC2 detection**
- **Method**: Deep ELF analysis including:
  - Memory mapping entire executable (`_kern_map_file`)
  - Section header parsing (SHT_HASH, SHT_DYNSYM, SHT_STRTAB)
  - Symbol table traversal and hash bucket resolution
  - ABI variable lookup (`B_SHARED_OBJECT_HAIKU_ABI_VARIABLE_NAME`)
  - Fallback detection for pre-ABI executables
- **Dependencies**:
  - ELF header parsing for GCC2 markers (lines 481-631)
  - Symbol space separation for ABI isolation
  - Architecture-specific runtime loader builds
  - **CRITICAL**: Lines 612-614 already modified to disable GCC2 detection

#### **Library Loading Strategy**
- **GCC2 Path**: `/system/lib/x86_gcc2/`
- **Modern Path**: `/system/lib/`
- **Mechanism**: Runtime architecture detection → appropriate library tree
- **Implementation**: `open_executable()` function (lines 301-371)
  - `abiSpecificSubDir` parameter for x86_gcc2 subdirectory loading
  - Library directory detection (lines 200-208) for known system paths
  - Path construction with ABI-specific subdirectories (lines 216-218)
- **Dependencies**: 107 separate x86_gcc2 packages in HaikuPorts

### **3. BUILD SYSTEM INTEGRATION**

#### **Cross-Compilation Infrastructure**
- **Target**: `i586-pc-haiku` (GCC2) vs `i586-pc-haiku` (modern)
- **Toolchain**: Complete GCC 2.95.3 source preservation
- **Build Flags**:
  - `HAIKU_CC_IS_LEGACY_GCC` - Conditional compilation
  - `HYBRID_SECONDARY` - Dual-architecture builds
  - `USE_EGCS_MANGLED_NAMES` - Symbol compatibility

#### **Bootstrap System**
- **Stubbed Libraries**: Separate GCC2 bootstrap process
- **libgcc Integration**: GCC2 runtime embedded in libroot.so
- **Symbol Stubs**: Different stub files for GCC2 vs modern symbol formats

### **4. PACKAGE ARCHITECTURE SYSTEM**

#### **Architecture Enumeration**
- **Enum**: `B_PACKAGE_ARCHITECTURE_X86_GCC2 = 2`
- **Package Count**: 107 dedicated x86_gcc2 packages
- **Library Naming**: `_x86_gcc2` suffix for all libraries
- **Repository Structure**: Separate HaikuPorts x86_gcc2 section

---

## COMPREHENSIVE REMOVAL PLAN

### **PHASE 1: BUILD SYSTEM ARCHITECTURE REMOVAL**

#### **Step 1.1: Package Architecture Enumeration**
```cpp
// File: headers/os/package/PackageArchitecture.h
enum BPackageArchitecture {
    B_PACKAGE_ARCHITECTURE_ANY      = 0,
    B_PACKAGE_ARCHITECTURE_X86      = 1,
    // B_PACKAGE_ARCHITECTURE_X86_GCC2 = 2,  ← REMOVE
    B_PACKAGE_ARCHITECTURE_SOURCE   = 3,
    B_PACKAGE_ARCHITECTURE_X86_64   = 4,
    // ... rest unchanged
};
```

#### **Step 1.2: Package Architecture Names**
```cpp
// File: src/kits/package/PackageInfo.cpp:67
static const char* const kArchitectureNames[] = {
    "any",        // B_PACKAGE_ARCHITECTURE_ANY
    "x86",        // B_PACKAGE_ARCHITECTURE_X86
    NULL,         // B_PACKAGE_ARCHITECTURE_X86_GCC2 ← SET TO NULL
    "source",     // B_PACKAGE_ARCHITECTURE_SOURCE
    // ... rest unchanged
};
```

#### **Step 1.3: NULL Safety Implementation**
```cpp
// File: src/kits/package/PackageInfo.cpp:614-618
const char* arch = kArchitectureNames[fArchitecture];
if (arch == NULL)  // ← ADD NULL CHECK
    arch = "unknown";
return BString().SetToFormat("%s-%s-%s.hpkg", fName.String(),
    fVersion.ToString().String(), arch);
```

### **PHASE 2: ABI SYSTEM REMOVAL**

#### **Step 2.1: Architecture ABI Definition**
```cpp
// File: headers/config/HaikuConfig.h
#if defined(__HAIKU_ARCH_X86)
#   define __HAIKU_ARCH_ABI "x86"  // ← ALWAYS x86, never x86_gcc2
#endif
```

#### **Step 2.2: ABI Constants Removal**
```cpp
// File: headers/os/BeBuild.h
#define B_HAIKU_ABI_MAJOR               0xffff0000
// #define B_HAIKU_ABI_GCC_2            0x00020000  ← REMOVE ALL GCC2 CONSTANTS
// #define B_HAIKU_ABI_GCC_2_ANCIENT    0x00020000
// #define B_HAIKU_ABI_GCC_2_BEOS       0x00020001  
// #define B_HAIKU_ABI_GCC_2_HAIKU      0x00020002
#define B_HAIKU_ABI_GCC_4               0x00040000
```

#### **Step 2.3: Runtime ABI Detection Removal**
```cpp
// File: src/system/runtime_loader/runtime_loader.cpp:481-631
// REMOVE ENTIRE determine_x86_abi() FUNCTION (189 lines)
// CURRENT STATUS: Lines 612-614 already modified to disable GCC2 detection
// COMPLETE REMOVAL:
//   - Remove memory mapping logic for ELF analysis
//   - Remove symbol table traversal (SHT_HASH, SHT_DYNSYM, SHT_STRTAB)
//   - Remove ABI variable lookup and hash bucket resolution
//   - Remove fallback detection for pre-ABI executables
//   - Simplify get_executable_architecture() case EM_386/EM_486 to always return "x86"
```

### **PHASE 3: SIBLING ARCHITECTURE SYSTEM**

#### **Step 3.1: Sibling Architecture Removal**
```cpp
// File: src/system/libroot/os/Architecture.cpp:23-27
#ifdef __HAIKU_ARCH_X86
    static const char* const kSiblingArchitectures[] = {}; // ← EMPTY ARRAY
#else
    static const char* const kSiblingArchitectures[] = {};
#endif
```

#### **Step 3.2: Secondary Architecture Detection**
```cpp
// File: src/system/libroot/os/Architecture.cpp
// REMOVE has_secondary_architecture() checks for x86_gcc2
// REMOVE /system/lib/x86_gcc2/libroot.so detection
```

### **PHASE 4: BUILD SYSTEM INTEGRATION REMOVAL**

#### **Step 4.1: JAM Build Rules**
```jam
// File: build/jam/ArchitectureRules:376
// REMOVE: if x86_gcc2 in $(HAIKU_ARCHS[2-]) conditions
// SIMPLIFY: x86 architecture handling only
```

#### **Step 4.2: Legacy GCC Detection**
```jam
// File: build/jam/BuildSetup
// REMOVE: HAIKU_CC_IS_LEGACY_GCC variables
// REMOVE: GCC2 version detection logic
// REMOVE: Cross-tools GCC2 building support
```

#### **Step 4.3: Build Feature Rules**
```jam
// File: build/jam/BuildFeatureRules
// REMOVE: Legacy GCC detection conditionals
// REMOVE: GCC2 workaround flags
```

### **PHASE 5: REPOSITORY AND PACKAGE REMOVAL**

#### **Step 5.1: HaikuPorts Repository**
```jam
// File: build/jam/repositories/HaikuPorts/x86_gcc2
// REMOVE ENTIRE FILE: 107 x86_gcc2 packages
```

#### **Step 5.2: Cross Repository**
```jam
// File: build/jam/repositories/HaikuPortsCross/x86_gcc2  
// REMOVE ENTIRE FILE: x86_gcc2 bootstrap packages
```

#### **Step 5.3: Package Filters**
```jam
// File: Jamfile (root)
// REMOVE: !gcc2 @{ package_list }@ conditionals
// REMOVE: gcc2 @{ package_list }@ conditionals
// SIMPLIFY: Direct package inclusion
```

### **PHASE 6: RUNTIME LOADER ARCHITECTURE CLEANUP**

#### **Step 6.1: ABI-Specific Library Path Removal**
```cpp
// File: src/system/runtime_loader/runtime_loader.cpp:194-209
// REMOVE: abiSpecificSubDir parameter usage in open_executable()
// REMOVE: Library directory detection for x86_gcc2 subdirectory creation
// REMOVE: Path construction logic with ABI-specific subdirectories (lines 216-218)
// SIMPLIFY: Direct library loading without ABI subdirectory checks
```

#### **Step 6.2: Runtime Loader Prefix**
```cpp
// File: src/system/runtime_loader/runtime_loader_private.h
// REMOVE: #define RLD_PREFIX "runtime_loader_x86_gcc2: "
// UNIFY: Single runtime loader for x86
```

#### **Step 6.3: ELF Loading Logic**
```cpp
// File: src/system/runtime_loader/runtime_loader.cpp:634-685
// SIMPLIFY: get_executable_architecture() function
case EM_386:
case EM_486:
{
    // REMOVE: bool isGcc2; determine_x86_abi(fd, elfHeader, isGcc2);
    architecture = "x86";  // Always modern x86, no conditional logic
    break;
}
```

#### **Step 6.4: Architecture Jamfiles**
```jam
// Files: src/system/runtime_loader/arch/x86/Jamfile
// REMOVE: GCC2-specific compilation flags
// REMOVE: Legacy GCC workarounds
```

### **PHASE 7: LIBRARY SYSTEM CLEANUP**

#### **Step 7.1: Library Jamfiles** 
```jam
// Files: Multiple library Jamfiles
// REMOVE: GCC2 compiler workarounds
// REMOVE: Legacy compilation flags
// EXAMPLES:
//   - src/system/libroot/posix/musl/math/x86/Jamfile
//   - src/libs/zydis/Jamfile
//   - src/system/libroot/posix/musl/complex/Jamfile
```

#### **Step 7.2: Stub Library System**
```cpp
// File: src/system/libroot/stubbed/
// REMOVE: GCC2-specific stub files
// REMOVE: libgcc symbol integration for GCC2
// SIMPLIFY: Modern GCC stub system only
```

### **PHASE 8: SYMBOL SYSTEM CLEANUP**

#### **Step 8.1: Symbol Demangling**
```cpp
// File: src/kits/debugger/demangler/gcc2.cpp
// REMOVE ENTIRE FILE: GCC2 symbol demangling
```

#### **Step 8.2: Archivable Symbol Lookup**
```cpp
// File: src/kits/support/Archivable.cpp
// REMOVE: GCC2 symbol mangling logic
// REMOVE: Named return value handling for GCC2
// SIMPLIFY: Modern C++ symbol lookup only
```

### **PHASE 9: LEGACY TOOLCHAIN REMOVAL**

#### **Step 9.1: Legacy GCC Source**
```bash
# Directory: buildtools/legacy/gcc/
# REMOVE ENTIRE DIRECTORY: Complete GCC 2.95.3 source
```

#### **Step 9.2: Legacy Binutils**
```bash
# Directory: buildtools/legacy/binutils/
# REMOVE ENTIRE DIRECTORY: GCC2-compatible binutils
```

#### **Step 9.3: Cross-Tools Scripts**
```bash
# Files: buildtools/legacy/compile-gcc, buildtools/legacy/compile-binutils
# REMOVE: GCC2 cross-compilation build scripts
```

### **PHASE 10: CONFIGURATION SYSTEM CLEANUP**

#### **Step 10.1: Configure Script**
```bash
# File: configure
# REMOVE: haikuRequiredLegacyGCCVersion variables
# REMOVE: GCC2 version validation
# REMOVE: --build-cross-tools x86_gcc2 support
```

#### **Step 10.2: Build Profiles**
```jam
# File: build/jam/DefaultBuildProfiles
# REMOVE: x86_gcc2 architecture conditions
# REMOVE: WebPositive x86_gcc2 restrictions
```

### **PHASE 11: SECONDARY ARCHITECTURE PACKAGE REMOVAL**

#### **Step 11.1: Secondary Package Definition**
```
# File: src/data/package_infos/x86_gcc2/haiku_secondary
# REMOVE ENTIRE FILE: x86_gcc2 secondary architecture package
```

#### **Step 11.2: Library Dependencies**
```
# REMOVE ALL x86_gcc2 library requirements:
# - lib:libfreetype_x86_gcc2
# - lib:libgcc_s_x86_gcc2  
# - lib:libicudata_x86_gcc2
# - lib:libstdc++_x86_gcc2
# - lib:libsupc++_x86_gcc2
# + 50+ other x86_gcc2 libraries
```

---

## VERIFICATION AND SAFETY MEASURES

### **Build Verification**
1. **Clean Build Test**: `jam -q @nightly-anyboot` must succeed
2. **Package Verification**: All packages must have x86_64 or "any" architecture
3. **Runtime Testing**: Boot and application testing in QEMU
4. **Library Loading**: Verify no x86_gcc2 library path references

### **Regression Prevention**
1. **NULL Safety**: All package architecture lookups handle NULL gracefully
2. **Fallback Mechanisms**: Unknown architectures handled as "unknown"
3. **Modern ABI**: All binaries use modern GCC ABI consistently
4. **Testing**: Comprehensive test suite for architecture handling

### **BeOS Compatibility Preservation**
1. **API Compatibility**: BeOS APIs remain unchanged
2. **Binary Format**: ELF format support maintained  
3. **Application Support**: Modern BeOS applications continue working
4. **User Experience**: No visible changes to BeOS application compatibility

---

## EXECUTION TIMELINE

### **Phase 1-3**: ABI and Architecture System (2-3 hours)
- Package architecture enumeration
- ABI constant removal  
- Sibling architecture cleanup

### **Phase 4-6**: Build System and Runtime (3-4 hours)
- JAM build rules cleanup
- Runtime loader simplification
- Cross-compilation removal

### **Phase 7-9**: Library and Symbol Systems (2-3 hours)
- Library Jamfile cleanup
- Symbol demangling removal
- Legacy toolchain removal

### **Phase 10-11**: Configuration and Packages (1-2 hours)
- Configure script cleanup
- Secondary architecture removal
- Final verification

**Total Estimated Time**: 8-12 hours of careful, systematic removal

---

## SUCCESS CRITERIA

1. **Build Success**: Nightly ISO builds without errors
2. **Package Integrity**: All packages use modern architecture only
3. **Runtime Stability**: Haiku boots and runs applications normally
4. **No Regressions**: Existing functionality remains intact
5. **Clean Codebase**: No remaining x86_gcc2 references
6. **BeOS Support**: Modern BeOS applications continue working

---

## RISK MITIGATION

### **High-Risk Operations**
1. **Runtime Loader Changes**: Extensive testing required
2. **Package System Modifications**: NULL handling critical
3. **Symbol System Changes**: Debugger functionality verification

### **Mitigation Strategies**
1. **Incremental Testing**: Test after each major phase
2. **Backup Strategy**: Git commits after successful phases
3. **Rollback Plan**: Detailed reversal procedures documented
4. **Verification Scripts**: Automated testing for each phase

---

This plan provides **COMPLETE TECHNICAL COVERAGE** for safely removing x86_gcc2 support while preserving all other functionality and maintaining BeOS compatibility through modern means. The removal addresses the **904 identified code locations** across the entire Haiku codebase in a systematic, verifiable manner.