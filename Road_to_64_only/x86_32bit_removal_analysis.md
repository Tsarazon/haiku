# x86 32-bit Removal Analysis

This document contains a comprehensive analysis of all code that needs to be modified or removed to eliminate 32-bit x86 support from Haiku.

## Summary

| Category | Count |
|----------|-------|
| x86 directories | 31 |
| Files with `#ifdef __i386__` | 86 |
| Unconditional `#include <arch/x86/...>` | 89 |

---

## 1. x86 Directories (to be removed or cleaned)

### Category A: Can be deleted entirely (have x86_64 counterpart)

These directories contain 32-bit only code with separate x86_64 implementations:

| Directory | Notes |
|-----------|-------|
| `src/system/libroot/os/arch/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/arch/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/string/arch/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/musl/math/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/musl/arch/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/glibc/include/arch/x86/` | Has x86_64 counterpart |
| `src/system/libroot/posix/glibc/arch/x86/` | Has x86_64 counterpart |
| `src/system/glue/arch/x86/` | Has x86_64 counterpart |
| `src/system/runtime_loader/arch/x86/` | Has x86_64 counterpart |
| `src/system/ldscripts/x86/` | Has x86_64 counterpart |
| `src/system/kernel/lib/arch/x86/` | Has x86_64 counterpart |
| `src/tools/gensyscalls/arch/x86/` | Has x86_64 counterpart |
| `src/bin/debug/ltrace/arch/x86/` | Has x86_64 counterpart |
| `src/kits/debugger/arch/x86/` | Has x86_64 counterpart |
| `src/kits/debug/arch/x86/` | Has x86_64 counterpart |
| `src/libs/compat/freebsd_network/compat/machine/x86/` | Has x86_64 counterpart |
| `src/data/package_infos/x86/` | Has x86_64 counterpart |
| `headers/os/arch/x86/` | Has x86_64 counterpart |
| `headers/posix/arch/x86/` | Has x86_64 counterpart |
| `headers/private/system/arch/x86/` | Has x86_64 counterpart |

### Category B: Contains both 32-bit and 64-bit code (selective removal)

These directories contain code shared between architectures with 32/64 subdirectories:

| Directory | What to remove |
|-----------|----------------|
| `src/system/kernel/arch/x86/` | Remove `32/`, `paging/32bit/`, `paging/pae/` |
| `src/system/boot/arch/x86/` | Analyze for 32-bit specific code |
| `src/system/boot/platform/efi/arch/x86/` | Analyze for 32-bit specific code |
| `headers/private/kernel/arch/x86/` | Remove 32-bit specific headers |
| `headers/private/kernel/boot/arch/x86/` | Analyze |

### Category C: Shared x86 code (keep but clean up)

These directories contain code used by both 32-bit and 64-bit x86:

| Directory | Notes |
|-----------|-------|
| `src/add-ons/kernel/bus_managers/acpi/arch/x86/` | Shared ACPI code |
| `src/add-ons/kernel/bus_managers/isa/arch/x86/` | ISA bus (might be 32-bit only?) |
| `src/add-ons/kernel/busses/pci/x86/` | PCI driver for x86 |
| `src/add-ons/kernel/cpu/x86/` | CPU add-on |
| `src/add-ons/kernel/debugger/disasm/x86/` | Disassembler |
| `src/kits/debugger/source_language/x86/` | Debugger source language |

---

## 2. Files with Unconditional `#include <arch/x86/...>`

**CRITICAL**: These files include x86 headers unconditionally and will break the build if headers are removed.

### Headers (must be fixed BEFORE removing directories)

| File | Line | Include |
|------|------|---------|
| `headers/os/kernel/debugger.h` | 15 | `<arch/x86/arch_debugger.h>` |
| `headers/private/system/commpage_defs.h` | 24 | `<arch/x86/arch_commpage_defs.h>` |
| `headers/private/kernel/boot/platform/bios_ia32/platform_kernel_args.h` | 13 | `<arch/x86/apm.h>` |
| `headers/private/kernel/boot/platform/efi/platform_kernel_args.h` | 16 | `<arch/x86/apm.h>` |

### Boot Platform (bios_ia32 - 32-bit only, can be removed entirely)

| File | Line | Include |
|------|------|---------|
| `src/system/boot/platform/bios_ia32/long_asm.S` | 10 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/bios_ia32/smp.cpp` | 21-25 | Multiple x86 headers |
| `src/system/boot/platform/bios_ia32/mmu.cpp` | 16 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/bios_ia32/start.cpp` | 11-14 | Multiple x86 headers |
| `src/system/boot/platform/bios_ia32/cpu.cpp` | 10,16 | Multiple x86 headers |
| `src/system/boot/platform/bios_ia32/mmu.h` | 9 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/bios_ia32/interrupts.cpp` | 20 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/bios_ia32/long.cpp` | 15 | `<arch/x86/descriptors.h>` |

### Boot Platform EFI (shared, needs conditional includes)

| File | Line | Include |
|------|------|---------|
| `src/system/boot/platform/efi/arch/x86/arch_mmu.cpp` | 12 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/efi/arch/x86/entry.S` | 9 | `<arch/x86/descriptors.h>` |
| `src/system/boot/platform/efi/arch/x86/arch_smp.cpp` | 21-23 | Multiple x86 headers |
| `src/system/boot/platform/efi/arch/x86/arch_timer.cpp` | 15-16 | Multiple x86 headers |
| `src/system/boot/platform/efi/arch/x86/smp_trampoline.S` | 19 | `<arch/x86/descriptors.h>` |

### Kernel (internal includes within x86 directory - OK)

| File | Line | Include |
|------|------|---------|
| `src/system/kernel/arch/x86/arch_smp.cpp` | 24-27 | Internal x86 headers |
| `src/system/kernel/arch/x86/msi.cpp` | 6-8 | Internal x86 headers |
| `src/system/kernel/arch/x86/ioapic.cpp` | 6-22 | Internal x86 headers |
| `src/system/kernel/arch/x86/arch_timer.cpp` | 24-25 | Internal x86 headers |
| `src/system/kernel/arch/x86/arch_int.cpp` | 26-36 | Internal x86 headers |
| `src/system/kernel/arch/x86/arch_cpu.cpp` | 35 | Internal x86 headers |
| `src/system/kernel/arch/x86/apic.cpp` | 12-13 | Internal x86 headers |
| `src/system/kernel/arch/x86/arch_vm.cpp` | 33 | Internal x86 headers |
| `src/system/kernel/arch/x86/64/*` | Various | Internal x86 headers |
| `src/system/kernel/arch/x86/32/*` | Various | Internal x86 headers (will be removed) |
| `src/system/kernel/arch/x86/timers/*` | Various | Internal x86 headers |

### Other files with x86 includes

| File | Line | Include |
|------|------|---------|
| `src/system/libroot/posix/arch/x86/fenv.c` | 32 | `<arch/x86/npx.h>` |
| `src/system/boot/arch/x86/arch_hpet.cpp` | 18-20 | Multiple x86 headers |
| `src/system/boot/arch/x86/arch_cpu.cpp` | 13,20 | Multiple x86 headers |
| `src/apps/powerstatus/APMDriverInterface.cpp` | 12 | `<arch/x86/apm_defs.h>` |
| `src/add-ons/kernel/bus_managers/pci/pci.cpp` | 15 | `<arch/x86/msi.h>` |
| `src/add-ons/kernel/cpu/x86/generic_x86.cpp` | 18 | `<arch/x86/arch_cpu.h>` |
| `src/add-ons/kernel/generic/bios/bios.cpp` | 13 | `<arch/x86/arch_cpu.h>` |

---

## 3. Files with `#ifdef __i386__` Conditional Compilation

These files contain conditional code for 32-bit x86 that needs to be cleaned up:

### Headers

| File | Notes |
|------|-------|
| `headers/build/HaikuBuildCompatibility.h` | Build system |
| `headers/config/HaikuConfig.h` | Config |
| `headers/os/kernel/debugger.h` | CPU state typedef |
| `headers/os/kernel/OS.h` | System defs |
| `headers/os/support/SupportDefs.h` | Basic types |
| `headers/posix/arch/x86/signal.h` | Signal handling |
| `headers/posix/arch/x86_64/fpu.h` | FPU state |
| `headers/posix/fenv.h` | Floating point env |
| `headers/posix/time.h` | Time defs |
| `headers/private/audio/soundcard.h` | Audio |
| `headers/private/fs_shell/fssh_api_wrapper.h` | FS shell |
| `headers/private/fs_shell/fssh_time.h` | FS shell time |
| `headers/private/kernel/arch/user_memory.h` | User memory |
| `headers/private/net/r5_compatibility.h` | R5 compat |
| `headers/private/shared/cpu_type.h` | CPU type detection |
| `headers/private/system/syscalls.h` | Syscalls |

### External libraries (probably should not modify)

| File | Notes |
|------|-------|
| `headers/libs/x86emu/x86emu/prim_x86_gcc.h` | x86 emulator |
| `headers/libs/zydis/Zycore/Defines.h` | Zydis disassembler |
| `src/libs/iconv/ChangeLog` | External lib |
| `src/libs/libsolv/solv/md5.c` | External lib |
| `src/libs/x86emu/prim_ops.c` | x86 emulator |
| `src/libs/compat/freebsd_network/compat/machine/*` | FreeBSD compat |
| `src/libs/compat/freebsd_iflib/*` | FreeBSD compat |
| `src/libs/compat/openbsd_wlan/crypto/sha2.c` | OpenBSD compat |

### Kernel and system

| File | Notes |
|------|-------|
| `src/system/kernel/arch/x86/arch_system_info.cpp` | System info |
| `src/system/kernel/arch/generic/acpi_irq_routing_table.cpp` | ACPI |
| `src/system/kernel/arch/generic/debug_uart.cpp` | Debug UART |
| `src/system/kernel/lib/strtod.c` | String to double |
| `src/system/kernel/vm/vm.cpp` | Virtual memory |
| `src/system/libroot/os/atomic.c` | Atomic ops |
| `src/system/libroot/os/driver_settings.cpp` | Driver settings |
| `src/system/libroot/posix/glibc/include/features.h` | Glibc features |
| `src/system/libroot/posix/glibc/stdlib/longlong.h` | Long long math |
| `src/system/libroot/posix/malloc/hoard2/block.h` | Memory allocator |
| `src/system/libroot/posix/malloc/hoard2/wrapper.cpp` | Memory allocator |
| `src/system/libroot/posix/malloc/openbsd/malloc.c` | Memory allocator |
| `src/system/libnetwork/netresolv/net/getnetent.c` | Network |
| `src/system/libnetwork/netresolv/net/getnetnamadr.c` | Network |
| `src/system/boot/platform/efi/serial.cpp` | Serial console |

### Applications and add-ons

| File | Notes |
|------|-------|
| `src/apps/aboutsystem/AboutSystem.cpp` | About dialog |
| `src/apps/icon-o-matic/generic/property/specific_properties/Int64Property.cpp` | Icon-O-Matic |
| `src/apps/icon-o-matic/generic/support/Debug.cpp` | Debug support |
| `src/apps/installer/WorkerThread.cpp` | Installer |
| `src/apps/webpositive/BrowserApp.cpp` | Web browser |
| `src/apps/powerstatus/APMDriverInterface.cpp` | Power status (APM is 32-bit only!) |
| `src/servers/app/drawing/Painter/Painter.cpp` | App server |
| `src/servers/app/drawing/Painter/bitmap_painter/DrawBitmapBilinear.h` | App server |
| `src/servers/package/DebugSupport.cpp` | Package server |
| `src/bin/rc/rdef.h` | Resource compiler |
| `src/bin/sysinfo.cpp` | System info tool |
| `src/bin/unzip/beos.c` | Unzip tool |

### Add-ons

| File | Notes |
|------|-------|
| `src/add-ons/kernel/bus_managers/acpi/acpica/include/platform/achaiku.h` | ACPI |
| `src/add-ons/kernel/bus_managers/pci/pci.cpp` | PCI |
| `src/add-ons/kernel/bus_managers/pci/pci_fixup.cpp` | PCI fixups |
| `src/add-ons/kernel/bus_managers/pci/pci.h` | PCI header |
| `src/add-ons/kernel/bus_managers/pci/pci_info.cpp` | PCI info |
| `src/add-ons/kernel/bus_managers/pci/pci_io.cpp` | PCI I/O |
| `src/add-ons/kernel/drivers/misc/poke.cpp` | Poke driver |
| `src/add-ons/kernel/drivers/network/ether/dec21xxx/dev/de/if_de.c` | Network |
| `src/add-ons/kernel/drivers/network/ether/etherpci/etherpci.c` | Network |
| `src/add-ons/kernel/drivers/network/ether/ipro1000/dev/e1000/e1000_osdep.h` | Network |
| `src/add-ons/kernel/drivers/network/ether/pcnet/dev/le/am79900.c` | Network |
| `src/add-ons/kernel/drivers/network/ether/sis19x/dev/sge/if_sge.c` | Network |
| `src/add-ons/kernel/file_systems/bindfs/DebugSupport.cpp` | FS |
| `src/add-ons/kernel/file_systems/netfs/shared/DebugSupport.cpp` | FS |
| `src/add-ons/kernel/file_systems/ntfs/libntfs/compress.c` | NTFS |
| `src/add-ons/kernel/file_systems/udf/UdfDebug.cpp` | UDF |
| `src/add-ons/kernel/file_systems/userlandfs/shared/Debug.cpp` | UserFS |
| `src/add-ons/kernel/partitioning_systems/common/PartitionMapWriter.cpp` | Partitions |
| `src/add-ons/kernel/partitioning_systems/session/Debug.cpp` | Partitions |
| `src/add-ons/media/media-add-ons/mixer/ByteSwap.cpp` | Audio mixer |
| `src/add-ons/media/media-add-ons/video_mixer/CpuCapabilities.cpp` | Video |
| `src/add-ons/media/media-add-ons/video_mixer/CpuCapabilities.h` | Video |
| `src/add-ons/media/plugins/ffmpeg/CpuCapabilities.cpp` | FFmpeg |
| `src/add-ons/media/plugins/ffmpeg/CpuCapabilities.h` | FFmpeg |
| `src/add-ons/translators/wonderbrush/support/support.h` | Translator |

### Debugger

| File | Notes |
|------|-------|
| `src/kits/debugger/arch/x86/ArchitectureX86.cpp` | Will be removed with directory |

### Tests

| File | Notes |
|------|-------|
| `src/tests/kits/game/chart/ChartRender_d.h` | Game kit test |
| `src/tests/servers/debug/crashing_app.cpp` | Debug test |
| `src/tests/system/benchmarks/atomic_bench.cpp` | Benchmark |

---

## 4. Build System Files

### configure

Remove `x86` and `x86_gcc2` from supported architectures:
- Line 34: `"x86", "x86_64",`
- Line 668: `x86_gcc2`
- Lines 712, 771: `x86_gcc2` target machine definitions

### Jamfiles with `if $(TARGET_ARCH) = x86`

| File | Notes |
|------|-------|
| `src/servers/app/drawing/Painter/Jamfile` | Line 17 |
| `src/tests/system/libroot/posix/Jamfile` | Line 31 |
| `build/jam/OptionalPackages` | Line 44 |

---

## 5. Recommended Removal Order

1. **Phase 1: Build system cleanup**
   - Edit `configure` to remove x86/x86_gcc2
   - Update Jamfiles with TARGET_ARCH conditions

2. **Phase 2: Fix unconditional includes**
   - Edit `headers/os/kernel/debugger.h`
   - Edit `headers/private/system/commpage_defs.h`
   - Edit other headers with unconditional x86 includes

3. **Phase 3: Remove 32-bit only directories**
   - Delete `src/system/boot/platform/bios_ia32/` entirely
   - Delete `src/system/kernel/arch/x86/32/`
   - Delete `src/system/kernel/arch/x86/paging/32bit/`
   - Delete `src/system/kernel/arch/x86/paging/pae/`

4. **Phase 4: Remove duplicate arch directories**
   - Delete directories in Category A (those with x86_64 counterparts)

5. **Phase 5: Clean up `#ifdef __i386__` conditionals**
   - Remove dead 32-bit branches from all listed files

6. **Phase 6: Remove 32-bit specific features**
   - APM support (32-bit BIOS calls)
   - BIOS calls in general

---

## 6. Estimated Lines of Code

| Category | Lines |
|----------|-------|
| Category A directories | ~5,900 |
| Kernel 32-bit code | ~7,700 |
| Additional directories | ~5,500 |
| Compat files | ~800 |
| Conditional branches | ~500-1,000 |
| **Total** | **~20,000-21,000** |