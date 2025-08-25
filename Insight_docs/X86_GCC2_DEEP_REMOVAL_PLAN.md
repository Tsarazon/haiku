# x86_gcc2 Deep Technical Removal Plan - Haiku OS

## Executive Summary

This document provides a **PRECISE TECHNICAL PLAN** for safely removing x86_gcc2 support from Haiku OS based on deep ABI-level analysis. The plan addresses all technical dependencies including symbol mangling, vtable layouts, exception handling, runtime loading, and build system integration.

**Critical Finding**: x86_gcc2 is NOT equivalent to BeOS compatibility. It specifically provides GCC 2.95.3 C++ ABI compatibility through egcs 1.1.x symbol mangling, custom exception handling, and integrated libgcc runtime.

---

## Part 1: Technical Dependency Map

### 1.1 ABI-Level Dependencies

#### Symbol Mangling Infrastructure
```
egcs 1.1.x mangling: __7MyClassi (GCC2)
vs
Itanium ABI mangling: _ZN7MyClassC1Ei (Modern)
```

**Files Affected**:
- `/headers/os/BeBuild.h` - ABI version definitions
- `/src/system/runtime_loader/elf_haiku_version.cpp` - ABI detection
- `/src/system/libroot/stubbed/libroot_stubs.c` - Symbol stubs

#### VTable Layout Differences
- **GCC2**: Custom vtable layout with different offset calculations
- **Modern**: Itanium C++ ABI standard vtable structure
- **Impact**: Virtual function dispatch incompatible between ABIs

#### Exception Handling Mechanisms
- **GCC2**: setjmp/longjmp-based exceptions
- **Modern**: DWARF unwinding with `.eh_frame` sections
- **Files**: Runtime loader must handle both exception models

### 1.2 Runtime Loader Dependencies

#### Binary Detection Logic
```cpp
// runtime_loader.cpp:661-668
case EM_386:
case EM_486:
{
    bool isGcc2;
    if (determine_x86_abi(fd, elfHeader, isGcc2) && isGcc2)
        architecture = "x86_gcc2";
    else
        architecture = "x86";
}
```

**Function `determine_x86_abi()`**: 200+ lines analyzing:
- ELF comment sections for GCC version strings
- Symbol naming patterns
- ABI version symbols (`_gSharedObjectHaikuABI`)

#### Library Search Paths
```cpp
// elf_load_image.cpp:650
if ((image->abi & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_2)
    sSearchPathSubDir = "x86_gcc2";
```

**Path Resolution**:
- GCC2: `/system/lib/x86_gcc2/`
- Modern: `/system/lib/`

### 1.3 Build System Infrastructure

#### Stubbed Library Bootstrap
```
Stage 0: stubbed libroot.so (empty symbols)
    ↓
Stage 1: gcc-syslibs (libgcc, libsupc++, libstdc++)
    ↓
Stage 2: full libroot.so (with libgcc integrated for GCC2)
    ↓
Stage 3: secondary architecture libraries
```

**Critical Files**:
- `/src/system/libroot/stubbed/Jamfile`
- `/src/system/libroot/stubbed/libroot_stubs.c.readme`
- `/build/jam/SystemLibraryRules`

#### Hybrid Secondary Architecture
```makefile
# buildtools/gcc/gcc/Makefile.in
HYBRID_SECONDARY = @HYBRID_SECONDARY@
DRIVER_DEFINES += -DHYBRID_SECONDARY="$(HYBRID_SECONDARY)"
```

**Configure Support**:
```bash
# configure:342-344
set_variable HAIKU_CC_IS_LEGACY_GCC_$targetArch \
    `is_legacy_gcc $gccRawVersion`
```

### 1.4 Core System Dependencies

#### Memory Allocation Compatibility
```cpp
// malloc/hoard2/arch-specific.cpp:100
if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU)
    // Use legacy memory allocation
```

#### Thread Management
```cpp
// libroot/os/thread.c:86
if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU) {
    // Legacy thread creation semantics
}
```

#### Area Management
```cpp
// libroot/os/area.c:19
if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU)
    // Legacy area semantics
```

---

## Part 2: Removal Strategy

### 2.1 Phase-Based Approach

#### Phase 1: Non-Breaking Preparations
**Risk: LOW** | **Reversible: YES**

1. **Document Current State**
   - Archive all x86_gcc2 detection logic
   - Document ABI differences
   - Create compatibility matrix

2. **Add Deprecation Warnings**
   ```cpp
   // runtime_loader.cpp
   if (isGcc2) {
       debugger("WARNING: x86_gcc2 support is deprecated");
       // Continue loading for now
   }
   ```

3. **Update Documentation**
   - Mark x86_gcc2 as deprecated
   - Provide migration guides
   - Document removal timeline

#### Phase 2: Build System Simplification
**Risk: MEDIUM** | **Reversible: YES**

1. **Remove Legacy GCC Detection**
   ```bash
   # configure - REMOVE
   is_legacy_gcc() {
       # This function can be completely removed
   }
   
   # SIMPLIFY TO:
   set_variable HAIKU_CC_IS_LEGACY_GCC_$targetArch 0
   ```

2. **Remove Hybrid Secondary Support for x86_gcc2**
   ```makefile
   # Remove from buildtools/gcc/gcc/Makefile.in
   # Keep HYBRID_SECONDARY infrastructure for other uses
   ```

3. **Simplify JAM Rules**
   ```jam
   # Remove from ArchitectureRules
   if $(HAIKU_CC_IS_LEGACY_GCC_$(architecture)) = 1 {
       # Remove all legacy GCC specific rules
   }
   ```

#### Phase 3: Runtime Loader Modifications
**Risk: MEDIUM** | **Reversible: YES**

1. **Modify Architecture Detection (VERIFIED SAFE FOR x86_64)**
   ```cpp
   // runtime_loader.cpp:660-682 - FULL CONTEXT
   switch (machine) {
       case EM_386:      // 32-bit x86 ONLY
       case EM_486:      // 32-bit x86 variant ONLY
       {
           bool isGcc2;
           // Keep determine_x86_abi() for validation, ignore GCC2 result
           determine_x86_abi(fd, elfHeader, isGcc2);
           architecture = "x86";  // Always return modern x86
           break;
       }
       case EM_X86_64:   // 64-bit x86 - COMPLETELY SEPARATE
           architecture = "x86_64";  // UNCHANGED - No GCC2 check here
           break;
       case EM_ARM:
           architecture = "arm";
           break;
       // ... other architectures
   }
   ```
   **Important**: x86_64 detection (EM_X86_64) is completely independent and will NOT be affected by changes to the EM_386/EM_486 case.

2. **Modify `determine_x86_abi()` Function (DO NOT REMOVE)**
   **Important**: Keep this 153-line function for:
   - Binary validation and ELF structure verification
   - ABI symbol detection for future versioning
   - Debugging and diagnostics
   
   **Changes needed (only ~10 lines)**:
   ```cpp
   // Line 614-615: Disable GCC2 detection
   // OLD: _isGcc2 = (abi & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_2;
   _isGcc2 = false;  // Never detect as GCC2
   
   // Line 628: Remove GCC2 fallback
   // OLD: _isGcc2 = true;
   _isGcc2 = false;  // No GCC2 fallback for old binaries
   ```

3. **Remove GCC2 Library Paths**
   ```cpp
   // elf_load_image.cpp - REMOVE
   if ((image->abi & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_2)
       sSearchPathSubDir = "x86_gcc2";  // DELETE THIS
   ```

#### Phase 4: Library Infrastructure Cleanup
**Risk: HIGH** | **Reversible: NO**

1. **Remove Stubbed Library GCC2 Support**
   ```jam
   # libroot/stubbed/Jamfile - SIMPLIFY
   stubsSource = [ FGristFiles libroot_stubs.c ] ;
   # Remove x86_gcc2 specific stubs
   ```

2. **Remove GCC2 Symbol Versions**
   - Clean libroot symbol exports
   - Remove legacy symbol mappings
   - Update version scripts

3. **Remove Legacy ABI Checks**
   ```cpp
   // Remove all instances of:
   if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU)
   ```

#### Phase 5: Package System Updates
**Risk: CRITICAL** | **Reversible: NO**

1. **PRESERVE Enum Values** (Critical for ABI stability)
   ```cpp
   enum BPackageArchitecture {
       B_PACKAGE_ARCHITECTURE_ANY      = 0,
       B_PACKAGE_ARCHITECTURE_X86      = 1,
       B_PACKAGE_ARCHITECTURE_X86_GCC2 = 2,  // KEEP (deprecated)
       B_PACKAGE_ARCHITECTURE_SOURCE   = 3,
       // ...
   };
   ```

2. **Safe String Deprecation (CORRECTED)**
   ```cpp
   // PackageInfo.cpp - AVOID NULL (causes crashes)
   static const char* const kArchitectureNames[] = {
       "any",                  // 0
       "x86",                  // 1
       "x86_gcc2_deprecated",  // 2 - Keep non-NULL to prevent crashes
       "source",               // 3
       // ...
   };
   
   // Add validation in accessor functions:
   const char* GetArchitectureName(BPackageArchitecture arch) {
       if (arch == B_PACKAGE_ARCHITECTURE_X86_GCC2)
           return NULL;  // Or throw error
       return kArchitectureNames[arch];
   }
   ```

3. **Update Package Validation**
   ```cpp
   if (architecture == B_PACKAGE_ARCHITECTURE_X86_GCC2) {
       return B_NOT_SUPPORTED;  // Reject x86_gcc2 packages
   }
   ```

---

## Important Corrections After Review

### Correction 1: `determine_x86_abi()` Function
**Critical Finding**: This function should NOT be removed entirely. It serves important purposes beyond GCC2 detection:
1. **Binary Validation**: Validates ELF structure, section headers, and symbol tables
2. **ABI Symbol Detection**: Looks for `_gSharedObjectHaikuABI` for version information
3. **Future Compatibility**: Infrastructure for future ABI versioning
4. **Debugging Support**: Provides valuable diagnostic information

**Revised Approach**: Keep the 153-line function, modify only ~10 lines to disable GCC2 detection.

### Correction 2: BeOS Compatibility Dependency
**Critical Finding**: BeOS R5 compatibility is tied to GCC2 detection:
```cpp
// HaikuConfig.h:115-116
#if defined(__HAIKU_ARCH_X86) && __GNUC__ == 2
#   define __HAIKU_BEOS_COMPATIBLE      1
#endif
```
Many system components use `__HAIKU_BEOS_COMPATIBLE` for legitimate BeOS R5 application compatibility, not just GCC2 binaries. Removing GCC2 detection breaks BeOS app compatibility.

**Impact**: Need to separate BeOS API compatibility from GCC2 binary support.

### Correction 3: x86_64 Detection is Independent
**Verification**: The x86_64 architecture detection is completely separate from x86/x86_gcc2:
```cpp
// runtime_loader.cpp - Architecture detection uses different ELF machine types:
case EM_386/EM_486:  // 32-bit x86 - where determine_x86_abi() is called
case EM_X86_64:      // 64-bit x86 - separate case, no GCC2 check
```
The `determine_x86_abi()` function is ONLY called for 32-bit x86 binaries (EM_386/EM_486). The EM_X86_64 case goes directly to `architecture = "x86_64"` without any GCC2 detection. Changes to 32-bit x86 handling cannot affect x86_64 detection.

### Correction 4: Package String Array Safety
**Critical Finding**: The kArchitectureNames array is directly indexed:
```cpp
// Multiple files use direct indexing:
kArchitectureNames[fArchitecture]  // Could crash if NULL
kArchitectureNames[architecture]   // Direct array access
```
Using NULL placeholders would cause crashes when code directly indexes the array.

**Revised Approach**: Keep string but mark deprecated, or validate before access.

## Part 3: Technical Implementation Details

### 3.1 BeOS Compatibility Separation (NEW)

#### Problem
BeOS R5 compatibility is currently tied to GCC2:
```cpp
#if defined(__HAIKU_ARCH_X86) && __GNUC__ == 2
#   define __HAIKU_BEOS_COMPATIBLE      1
#endif
```

#### Solution
Separate BeOS API compatibility from GCC2 binary support:
```cpp
// Option 1: Always enable for x86 (if BeOS compatibility desired)
#if defined(__HAIKU_ARCH_X86)
#   define __HAIKU_BEOS_COMPATIBLE      1
#endif

// Option 2: Make it a build option
#ifdef ENABLE_BEOS_COMPATIBILITY
#   define __HAIKU_BEOS_COMPATIBLE      1
#endif

// Option 3: Remove entirely if BeOS compatibility not needed
// (Would affect area.c, system_info.cpp, etc.)
```

**Files affected by __HAIKU_BEOS_COMPATIBLE**:
- `/src/system/libroot/os/area.c` - Memory protection flags
- `/src/system/libroot/os/system_info.cpp` - System info compatibility
- `/src/kits/app/` - Multiple files for BeOS app compatibility
- `/src/kits/storage/` - Storage kit compatibility
- Network compatibility layers

### 3.2 ABI Version Handling (Renumbered)

#### Current State
```cpp
#define B_HAIKU_ABI_GCC_2           0x00020000
#define B_HAIKU_ABI_GCC_2_ANCIENT   0x00020000
#define B_HAIKU_ABI_GCC_2_BEOS      0x00020001
#define B_HAIKU_ABI_GCC_2_HAIKU     0x00020002
```

#### After Removal
```cpp
// Keep definitions for binary compatibility
// But never set these values at runtime
#define B_HAIKU_ABI_GCC_2           0x00020000  // Deprecated
#define B_HAIKU_ABI_GCC_2_ANCIENT   0x00020000  // Deprecated
#define B_HAIKU_ABI_GCC_2_BEOS      0x00020001  // Deprecated
#define B_HAIKU_ABI_GCC_2_HAIKU     0x00020002  // Deprecated
```

### 3.3 Architecture Detection Simplification (CORRECTED)

#### Current Complex Detection
```cpp
// 153 lines of determine_x86_abi() analysis including:
- ELF section header validation
- Symbol table parsing
- ABI symbol detection (_gSharedObjectHaikuABI)
- GCC2 vs modern determination
```

#### Modified Detection (NOT REMOVED)
```cpp
// runtime_loader.cpp - KEEP determine_x86_abi() but modify:
// Only ~10 lines changed out of 153:

// Line 614-615: Disable GCC2 detection
_isGcc2 = false;  // Always false, never detect as GCC2

// Line 628: Remove fallback
_isGcc2 = false;  // No GCC2 fallback

// Lines 663-668: Simplify usage
case EM_386:
case EM_486:
{
    bool isGcc2;
    determine_x86_abi(fd, elfHeader, isGcc2);  // Keep for validation
    architecture = "x86";  // Always modern
    break;
}
```

**Why Keep determine_x86_abi():**
- Validates ELF structure integrity
- Detects ABI symbols for future versioning
- Provides debugging information
- Only needs minimal changes (~10 lines)

### 3.4 Library Path Resolution

#### Current Dual-Path System
```
GCC2 binaries → /system/lib/x86_gcc2/
Modern binaries → /system/lib/
```

#### Unified Path System
```
All x86 binaries → /system/lib/
```

**Implementation**:
```cpp
// elf_load_image.cpp
static const char* get_library_path(image_t* image)
{
    // Remove all GCC2-specific path logic
    return "/system/lib";
}
```

### 3.5 Sibling Architecture Handling (VERIFIED SAFE)

#### Current Sibling System
```cpp
#ifdef __HAIKU_ARCH_X86
    static const char* const kSiblingArchitectures[] = {"x86_gcc2", "x86"};
#endif
```

#### Simplified System
```cpp
// No siblings for x86
static const char* const kSiblingArchitectures[] = {};
```

---

## Part 4: What Will NOT Be Broken

### 4.1 Architecture Detection Safety

**x86_64 Detection**: COMPLETELY UNAFFECTED
- Uses separate ELF machine type (EM_X86_64)
- Never calls determine_x86_abi()
- Independent code path in switch statement

**ARM/ARM64/RISCV Detection**: COMPLETELY UNAFFECTED
- Separate cases in switch statement
- No interaction with x86 code

**Modern x86 (32-bit)**: CONTINUES TO WORK
- Still detected via EM_386/EM_486
- Still validated by determine_x86_abi()
- Just won't detect as "x86_gcc2" anymore

### 4.2 Binary Compatibility Preserved

**Modern Binaries**: ALL CONTINUE TO WORK
- x86_64 binaries: Unaffected
- Modern x86 binaries: Unaffected
- ARM/RISCV binaries: Unaffected
- Only GCC2-compiled binaries rejected

**Package System**: REMAINS FUNCTIONAL
- Enum values preserved for ABI stability
- Modern packages continue to work
- Only x86_gcc2 packages rejected

## Part 5: Risk Assessment

### 5.1 Critical Risks

#### Binary Incompatibility
**Risk**: GCC2-compiled binaries will no longer load
**Impact**: High - Breaks existing software
**Mitigation**:
- Provide recompilation guides
- Maintain list of affected software
- Consider compatibility shim (separate project)

#### Package Repository Breakage
**Risk**: x86_gcc2 packages become invalid
**Impact**: Medium - Repository inconsistency
**Mitigation**:
- Clean repository before deployment
- Provide migration scripts
- Implement graceful rejection

#### ABI Stability
**Risk**: Enum value changes break binary compatibility
**Impact**: Critical - System instability
**Mitigation**:
- **NEVER** remove enum values
- Only NULL string mappings
- Preserve all constants

### 5.2 Medium Risks

#### Build System Disruption
**Risk**: Third-party builds may fail
**Impact**: Medium - Developer friction
**Mitigation**:
- Gradual deprecation period
- Clear migration documentation
- Compatibility defines

#### Runtime Performance
**Risk**: Simplified detection may affect performance
**Impact**: Low - Minimal overhead reduction
**Mitigation**:
- Benchmark before/after
- Profile critical paths

### 5.3 Low Risks

#### Documentation Inconsistency
**Risk**: Outdated references to x86_gcc2
**Impact**: Low - Confusion only
**Mitigation**:
- Comprehensive documentation update
- Search and replace automation

---

## Part 5: Testing Strategy

### 5.1 Pre-Removal Testing

1. **Inventory GCC2 Binaries**
   ```bash
   find /boot -type f -exec file {} \; | grep "GCC 2"
   ```

2. **Test Package Detection**
   ```bash
   pkgman search --arch x86_gcc2
   ```

3. **Benchmark Current System**
   - Boot time
   - Library loading performance
   - Memory usage

### 5.2 Post-Removal Testing

#### Build Verification
```bash
# Clean build test
jam -q clean
jam -q @x86
```

#### Runtime Verification
```bash
# Test binary loading
/system/runtime_loader/runtime_loader test_binary
```

#### Package System
```bash
# Verify package operations
pkgman update
pkgman install test-package
```

### 5.3 Regression Testing

1. **Core System Functions**
   - Boot process
   - Driver loading
   - Network stack
   - Graphics subsystem

2. **Application Compatibility**
   - Test modern x86 applications
   - Verify GCC4+ binaries work
   - Check x86_64 unaffected

3. **Performance Testing**
   - Library loading speed
   - Symbol resolution time
   - Memory usage patterns

---

## Part 6: Implementation Timeline

### Week 1-2: Preparation
- [ ] Complete code inventory
- [ ] Document all x86_gcc2 references
- [ ] Create test suite
- [ ] Set up rollback procedures

### Week 3-4: Build System
- [ ] Remove legacy GCC detection
- [ ] Update configure script
- [ ] Simplify JAM rules
- [ ] Test cross-compilation

### Week 5-6: Runtime Changes
- [ ] Simplify architecture detection
- [ ] Remove GCC2 binary analysis
- [ ] Update library paths
- [ ] Test runtime loading

### Week 7-8: Library Updates
- [ ] Remove stubbed library support
- [ ] Clean symbol versions
- [ ] Update ABI checks
- [ ] Test library loading

### Week 9-10: Package System
- [ ] Update architecture handling
- [ ] Implement rejection logic
- [ ] Clean repositories
- [ ] Test package operations

### Week 11-12: Integration
- [ ] Full system testing
- [ ] Performance validation
- [ ] Documentation updates
- [ ] Release preparation

---

## Part 7: Rollback Plan

### 7.1 Rollback Triggers
- System fails to boot
- > 10% performance regression
- Critical application breakage
- Package system corruption

### 7.2 Rollback Procedure

1. **Immediate Rollback** (< 24 hours)
   ```bash
   git revert --no-commit HEAD~N..HEAD
   git commit -m "Rollback x86_gcc2 removal"
   ```

2. **Partial Rollback** (Selective reversion)
   - Restore runtime loader detection
   - Keep build system simplifications
   - Preserve documentation updates

3. **Data Recovery**
   - Restore package repositories
   - Rebuild affected packages
   - Regenerate library symbols

---

## Part 8: Success Metrics

### 8.1 Technical Metrics
- [ ] All x86 modern binaries load correctly
- [ ] No performance regression > 5%
- [ ] Package system operates normally
- [ ] Build time reduced by > 10%
- [ ] Code complexity reduced by ~1850 lines (keeping 153 for validation)

### 8.2 Quality Metrics
- [ ] Zero system crashes post-removal
- [ ] All regression tests pass
- [ ] Documentation 100% updated
- [ ] Migration guide published
- [ ] User feedback addressed

---

## Part 9: Long-Term Considerations

### 9.1 Future Architecture Support
With x86_gcc2 removed, Haiku can:
- Simplify new architecture ports
- Reduce maintenance burden
- Improve code clarity
- Accelerate development

### 9.2 Compatibility Layer Options
Consider separate project for:
- GCC2 binary translator
- Virtualization solution
- Static recompilation tools
- Binary compatibility shim

### 9.3 Community Impact
- Some legacy software becomes unusable
- Reduced BeOS binary compatibility
- Simplified developer experience
- Modern toolchain focus

---

## Conclusion

The removal of x86_gcc2 support from Haiku represents a significant architectural modernization. This plan provides a **technically precise**, **risk-aware**, and **phased approach** to safely remove this legacy infrastructure while:

1. **Preserving ABI stability** through enum retention
2. **Maintaining system integrity** through careful testing
3. **Minimizing disruption** through gradual implementation
4. **Enabling rollback** through version control

The key insight is that x86_gcc2 is specifically about **GCC 2.95.3 C++ ABI compatibility**, not general BeOS compatibility. Modern x86 builds can maintain compatibility with BeOS APIs without requiring GCC2 ABI support.

**Recommendation**: Proceed with removal following this plan, with emphasis on:
- Extensive testing at each phase
- Clear communication with community
- Preservation of enum values for stability
- Documentation of all changes

**Expected Outcome (FULLY CORRECTED)**:
- **~1850 lines of code removed** (preserving critical functions)
- **~10 lines modified** in determine_x86_abi() function
- **153 lines preserved** for binary validation
- **Build system simplified** (~660 JAM lines removed)
- **BeOS compatibility separated** from GCC2 support
- **Package strings kept non-NULL** to prevent crashes
- **Runtime loader partially streamlined**
- **Lower risk** than originally estimated

**Critical Caveats (After Full Review)**:
1. BeOS R5 app compatibility requires separate handling from GCC2 removal
2. Package string arrays cannot use NULL (causes crashes on direct indexing)
3. determine_x86_abi() must be kept for binary validation (only modify ~10 lines)
4. Some "GCC2-specific" code actually serves BeOS compatibility
5. x86_64 detection is completely independent and will NOT be affected
6. Modern x86, ARM, and RISCV architectures remain unaffected
7. Only GCC 2.95.3-compiled binaries will be rejected

---

*Technical Analysis Date: 2025-01-20*  
*Haiku Version: Master branch*  
*Analysis Depth: ABI-level with full dependency mapping*  
*Risk Assessment: HIGH but manageable with proper implementation*