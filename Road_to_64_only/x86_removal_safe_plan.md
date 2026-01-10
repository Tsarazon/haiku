# Safe x86 32-bit Removal Plan

This plan describes a safe, step-by-step approach to removing 32-bit x86 support while **preserving legacy BIOS boot for x86_64**.

## Important Discovery

**`bios_ia32` and `pxe_ia32` boot platforms MUST be kept!**

These platforms contain `long.cpp` and `long_asm.S` which handle the transition from 32-bit real mode to 64-bit long mode. This is required for:
- Legacy BIOS boot of x86_64 systems
- PXE network boot of x86_64 systems
- Systems without UEFI

---

## Phase 0: Preparation

### Step 0.1: Create backup branch
```bash
git checkout -b backup-before-x86-removal
git push origin backup-before-x86-removal
git checkout master
git checkout -b remove-x86-32bit
```

### Step 0.2: Verify current build works
```bash
./configure --target-arch x86_64
jam -q @nightly-anyboot
```

---

## Phase 1: Build System Cleanup

**Risk: LOW**

### Step 1.1: Edit configure script
File: `configure`
- Remove `"x86"` from supported architectures (line 34)
- Remove all `x86_gcc2` references

### Step 1.2: Update Jamfiles with TARGET_ARCH conditions

---

## Phase 2: Fix Unconditional Headers (CRITICAL!)

**Risk: CRITICAL** - Must be done BEFORE deleting any directories!

### Step 2.1: Fix debugger.h
File: `headers/os/kernel/debugger.h`

Make includes conditional:
```c
#if defined(__i386__)
#include <arch/x86/arch_debugger.h>
#elif defined(__x86_64__)
#include <arch/x86_64/arch_debugger.h>
#elif ...
#endif
```

### Step 2.2: Fix commpage_defs.h
File: `headers/private/system/commpage_defs.h`

Make x86 include conditional.

---

## Phase 3: Remove 32-bit Kernel Code

**Risk: MEDIUM**

### Step 3.1: Remove kernel 32-bit directories
```bash
rm -rf src/system/kernel/arch/x86/32/
rm -rf src/system/kernel/arch/x86/paging/32bit/
rm -rf src/system/kernel/arch/x86/paging/pae/
```

### Step 3.2: Update kernel Jamfiles

---

## Phase 4: Remove Userland 32-bit Code

**Risk: MEDIUM**

### Step 4.1: Remove libroot arch directories
```bash
rm -rf src/system/libroot/os/arch/x86/
rm -rf src/system/libroot/posix/arch/x86/
rm -rf src/system/libroot/posix/string/arch/x86/
rm -rf src/system/libroot/posix/musl/math/x86/
rm -rf src/system/libroot/posix/musl/arch/x86/
rm -rf src/system/libroot/posix/glibc/include/arch/x86/
rm -rf src/system/libroot/posix/glibc/arch/x86/
```

### Step 4.2: Remove other userland directories
```bash
rm -rf src/system/glue/arch/x86/
rm -rf src/system/runtime_loader/arch/x86/
rm -rf src/system/ldscripts/x86/
rm -rf src/system/kernel/lib/arch/x86/
```

---

## Phase 5: Remove Public Headers

**Risk: HIGH** - May break third-party code

```bash
rm -rf headers/os/arch/x86/
rm -rf headers/posix/arch/x86/
```

---

## Phase 6: Remove Tools and Debugger Support

**Risk: LOW**

```bash
rm -rf src/kits/debugger/arch/x86/
rm -rf src/kits/debug/arch/x86/
rm -rf src/bin/debug/ltrace/arch/x86/
rm -rf src/tools/gensyscalls/arch/x86/
rm -rf src/data/package_infos/x86/
```

---

## Phase 7: Remove Private Headers

**Risk: MEDIUM**

```bash
rm -rf headers/private/system/arch/x86/
```

**DO NOT DELETE** `headers/private/kernel/arch/x86/` - contains shared x86/x86_64 headers!

---

## Phase 8: Clean up Conditional Compilation

**Risk: LOW-MEDIUM**

Remove `#ifdef __i386__` branches from non-external files.

**Skip external libraries:**
- `src/libs/iconv/`, `src/libs/libsolv/`, `src/libs/x86emu/`
- `src/libs/compat/freebsd_*`, `src/libs/compat/openbsd_*`
- `headers/libs/`

---

## Phase 9: FreeBSD Compat Cleanup

```bash
rm -rf src/libs/compat/freebsd_network/compat/machine/x86/
```

---

## Phase 10: Boot Platform (CAREFUL!)

**KEEP bios_ia32 and pxe_ia32 entirely!**

They are needed for legacy BIOS boot of x86_64.

---

## Testing After Each Phase

```bash
./configure --target-arch x86_64
jam -q @nightly-anyboot

# QEMU test
qemu-system-x86_64 -enable-kvm -m 2G -cdrom generated/haiku-nightly-anyboot.iso
```

---

## Summary

### REMOVED (~14,000 lines):
- 32-bit kernel code
- 32-bit userland libraries
- 32-bit public headers
- 32-bit debugger support
- Build system x86/x86_gcc2 targets

### KEPT:
- `bios_ia32` boot platform (legacy BIOS x86_64 boot)
- `pxe_ia32` boot platform (PXE x86_64 boot)
- Shared x86 kernel headers
- Shared ACPI/PCI/CPU x86 code
- External libraries