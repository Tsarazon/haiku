# X86_GCC2 Removal - Root Cause Analysis

## Summary
**ROOT CAUSE IDENTIFIED**: The removal of x86_gcc2 architecture from Haiku broke the hybrid secondary architecture system, causing package dependency resolution failures during Haiku build.

## Evidence

### 1. Architecture Removal from Configure
**File**: `/home/ruslan/haiku/configure:651-657`
```bash
supportedTargetArchs="
	arm
	arm64
	riscv64
	x86
	x86_64
	"
```
**Issue**: x86_gcc2 completely removed from supported architectures list.

### 2. Repository Comments Confirm Removal
**File**: `/home/ruslan/haiku/build/jam/repositories/HaikuPortsCross/x86_64:19,63`
```
# secondary_x86_gcc2 removed
# secondary_x86_gcc2 bootstrap packages removed
```

**File**: `/home/ruslan/haiku/build/jam/repositories/HaikuPortsCross/x86:13,43`
```
# secondary_x86_gcc2 removed  
# secondary_x86_gcc2 bootstrap packages removed
```

### 3. Build System Still Expects Secondary Architecture
**File**: `/home/ruslan/haiku/generated.x86_64/build/jamfile_cache:62,66`
```jam
if $(HAIKU_PACKAGING_ARCHS[1]) != x86_gcc2 {
    # ... code for non-x86_gcc2 primary
    if $(HAIKU_PACKAGING_ARCHS[2]) {
        # ... code expecting secondary architecture
```

**Current Config**: `/home/ruslan/haiku/generated.x86_64/build/BuildConfig:14`
```jam
HAIKU_PACKAGING_ARCHS		?=  x86_64 ;
```
**Issue**: Single architecture (x86_64 only) but build system expects secondary packages.

### 4. Package Dependencies Reference Missing Secondary Architecture
Many packages fail with "not installable" because they expect secondary architecture variants that x86_gcc2 would have provided:

- mandoc-1.14.3-2 
- openssh-9.8p1-2
- openssl3-3.0.14-2
- pe-2.4.5-10
- vision-0.10.6-2
- fontconfig-2.13.96-2
- jasper-2.0.33-1
- And 44 more packages...

### 5. Secondary Architecture Infrastructure Still Present
**File**: `/home/ruslan/haiku/src/data/package_infos/x86_64/haiku_secondary:1-36`
```
name 			haiku_%HAIKU_SECONDARY_PACKAGING_ARCH%
summary			"The Haiku base system %HAIKU_SECONDARY_PACKAGING_ARCH% secondary architecture support"
requires:
	lib:libfreetype_%HAIKU_SECONDARY_PACKAGING_ARCH%
	lib:libgcc_s_%HAIKU_SECONDARY_PACKAGING_ARCH%
	lib:libstdc++_%HAIKU_SECONDARY_PACKAGING_ARCH%
	# ... many more secondary arch libraries
```

## The Problem

1. **x86_gcc2 was the secondary architecture** that provided additional package variants
2. **Build system expects secondary packages** for hybrid architecture support
3. **x86_gcc2 removal broke this expectation** - packages that depended on secondary architecture support now fail
4. **Repository infrastructure assumes hybrid setup** but configuration is single-architecture

## Impact

- **51 package dependency resolution failures** during Haiku anyboot ISO build
- **96% build completion stalled** at package download phase
- **LibsolvSolver correctly identifies architecture** but packages expect secondary variants

## Solution Options

### Option A: Add x86 as Secondary Architecture
Configure build for hybrid x86_64 + x86 secondary architecture:
```bash
../configure --cross-tools-source ../../buildtools --build-cross-tools x86_64,x86 -j10
```

### Option B: Update Package Definitions  
Remove secondary architecture dependencies from all affected package definitions.

### Option C: Repository Infrastructure Fix
Restore missing secondary architecture packages in HaikuPorts repository.

## Recommendation

**Immediate**: Try Option A - reconfigure for hybrid x86_64 + x86 to restore secondary architecture support that replaces the removed x86_gcc2 functionality.

The user was correct: "maybe smth with deleted x86_gcc2 repo/HaikuPorts" - the root issue is indeed the removal of x86_gcc2 secondary architecture breaking the hybrid architecture system that the build configuration and package definitions still expect.