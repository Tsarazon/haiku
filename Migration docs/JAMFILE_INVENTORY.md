# Complete Jamfile Inventory for Haiku Build System

## Overview

This document provides a comprehensive inventory of all Jamfiles in the Haiku repository. The Haiku build system contains **1,322 Jamfiles** distributed across the entire source tree, managing the build process for all components from the kernel to applications.

## Directory Structure and Jamfile Distribution

### Root Level
- `/Jamfile` - Main orchestration file
- `/generated/Jamfile` - Generated content management

### Build System Core (`/build/`)
- No direct Jamfiles (uses jam rules and configuration files)

### Build Tools (`/buildtools/`)
- `/buildtools/jam/Jamfile` - Jam build tool itself

### Third Party Code (`/3rdparty/`)
- `/3rdparty/Jamfile` - Third-party integration
- `/3rdparty/mmu_man/Jamfile` - MMU management utilities

## Source Tree Jamfiles (`/src/`)

### Core System

#### Kernel (`/src/system/kernel/`)
Main kernel build coordination with subsystem organization:

```
/src/system/kernel/Jamfile                    # Main kernel build
/src/system/kernel/arch/Jamfile              # Architecture abstraction layer
/src/system/kernel/arch/x86/Jamfile          # x86 32-bit architecture
/src/system/kernel/arch/x86_64/Jamfile       # x86 64-bit architecture  
/src/system/kernel/arch/arm/Jamfile          # ARM 32-bit architecture
/src/system/kernel/arch/arm64/Jamfile        # ARM 64-bit (aarch64) architecture
/src/system/kernel/arch/riscv64/Jamfile      # RISC-V 64-bit architecture

# Kernel Subsystems
/src/system/kernel/cache/Jamfile             # File system cache
/src/system/kernel/debug/Jamfile             # Kernel debugging infrastructure
/src/system/kernel/device_manager/Jamfile   # Device management layer
/src/system/kernel/disk_device_manager/Jamfile # Disk device management
/src/system/kernel/fs/Jamfile                # File system core
/src/system/kernel/messaging/Jamfile         # Inter-process messaging
/src/system/kernel/posix/Jamfile            # POSIX compatibility layer
/src/system/kernel/slab/Jamfile             # Memory slab allocator
/src/system/kernel/util/Jamfile             # Kernel utilities
/src/system/kernel/vm/Jamfile               # Virtual memory management

# Kernel Libraries
/src/system/kernel/lib/Jamfile               # Kernel library coordination
/src/system/kernel/lib/arch/x86/Jamfile     # x86-specific kernel libs
/src/system/kernel/lib/arch/x86_64/Jamfile  # x86_64-specific kernel libs
/src/system/kernel/lib/arch/arm/Jamfile     # ARM-specific kernel libs
/src/system/kernel/lib/arch/arm64/Jamfile   # ARM64-specific kernel libs
/src/system/kernel/lib/arch/riscv64/Jamfile # RISC-V specific kernel libs
/src/system/kernel/lib/zlib/Jamfile         # Zlib compression
/src/system/kernel/lib/zstd/Jamfile         # Zstandard compression

# Platform Support
/src/system/kernel/platform/efi/Jamfile     # EFI platform support
/src/system/kernel/platform/bios_ia32/Jamfile # BIOS IA32 platform
/src/system/kernel/platform/pxe_ia32/Jamfile  # PXE network boot
/src/system/kernel/platform/u-boot/Jamfile    # U-Boot bootloader support
/src/system/kernel/platform/openfirmware/Jamfile # OpenFirmware support
```

#### Boot System (`/src/system/boot/`)
Boot loader and early system initialization:

```
/src/system/boot/Jamfile                     # Boot system coordination
/src/system/boot/arch/aarch64/Jamfile       # ARM64 boot support
/src/system/boot/arch/x86_64/Jamfile        # x86_64 boot support
/src/system/boot/loader/Jamfile             # Boot loader core

# Boot Platform Support  
/src/system/boot/platform/efi/Jamfile       # EFI boot platform
/src/system/boot/platform/efi/arch/aarch64/Jamfile # EFI ARM64 support

# Boot File Systems
/src/system/boot/loader/file_systems/amiga_ffs/Jamfile # Amiga FFS support
```

#### System Libraries (`/src/system/libroot/`)
Core system library infrastructure:

```
/src/system/libroot/os/Jamfile               # OS abstraction layer
/src/system/libroot/os/arch/aarch64/Jamfile # ARM64 OS layer
/src/system/libroot/posix/Jamfile           # POSIX compatibility
/src/system/libroot/posix/arch/aarch64/Jamfile # ARM64 POSIX layer
/src/system/libroot/posix/glibc/arch/aarch64/Jamfile # GNU libc compatibility
/src/system/libroot/posix/musl/math/aarch64/Jamfile # Math library
/src/system/libroot/posix/string/arch/aarch64/Jamfile # String functions

# Runtime Loader
/src/system/runtime_loader/Jamfile           # Dynamic linker
/src/system/runtime_loader/arch/aarch64/Jamfile # ARM64 runtime loader

# System Glue Code
/src/system/glue/arch/aarch64/Jamfile       # ARM64 startup code
```

### Applications (`/src/apps/`)
User applications - 85+ application Jamfiles:

#### Core Applications
```
/src/apps/Jamfile                           # Application coordination
/src/apps/aboutsystem/Jamfile              # System information app
/src/apps/activitymonitor/Jamfile          # Task manager equivalent
/src/apps/deskcalc/Jamfile                 # Calculator application
/src/apps/deskbar/Jamfile                  # System taskbar/dock
/src/apps/terminal/Jamfile                 # Terminal emulator
/src/apps/tracker/Jamfile                  # File manager
/src/apps/installer/Jamfile                # System installer
```

#### Media Applications
```
/src/apps/mediaplayer/Jamfile              # Media player
/src/apps/mediaconverter/Jamfile           # Media format converter
/src/apps/soundrecorder/Jamfile            # Audio recording
/src/apps/midiplayer/Jamfile               # MIDI player
/src/apps/musiccollection/Jamfile          # Music organizer
/src/apps/overlayimage/Jamfile             # Image overlay utility
/src/apps/showimage/Jamfile                # Image viewer
```

#### Development and Utilities
```
/src/apps/debugger/Jamfile                 # GUI debugger
/src/apps/debuganalyzer/Jamfile            # Debug analysis tool
/src/apps/stylededit/Jamfile               # Text editor
/src/apps/icon-o-matic/Jamfile             # Icon editor
/src/apps/diskprobe/Jamfile                # Disk editor
/src/apps/drivesetup/Jamfile               # Disk partitioning
/src/apps/packageinstaller/Jamfile         # Package installer
/src/apps/expander/Jamfile                 # Archive extractor
```

#### Complex Applications with Submodules
```
# Cortex Media Kit
/src/apps/cortex/Jamfile                   # Main Cortex application
/src/apps/cortex/AddOnHost/Jamfile         # Add-on hosting
/src/apps/cortex/DiagramView/Jamfile       # Node diagram view
/src/apps/cortex/MediaRoutingView/Jamfile  # Media routing interface
/src/apps/cortex/NodeManager/Jamfile       # Media node management
/src/apps/cortex/Persistence/Jamfile       # State persistence
/src/apps/cortex/addons/*/Jamfile          # Multiple add-on modules

# Debug Analyzer  
/src/apps/debuganalyzer/model/Jamfile      # Data model
/src/apps/debuganalyzer/gui/*/Jamfile      # Multiple GUI components
```

### Servers (`/src/servers/`)
System servers and daemons:

```
/src/servers/Jamfile                        # Server coordination
/src/servers/app/Jamfile                   # Application server (GUI)
/src/servers/registrar/Jamfile             # System registration service
/src/servers/input/Jamfile                 # Input device management
/src/servers/media/Jamfile                 # Media services
/src/servers/net/Jamfile                   # Network services
/src/servers/print/Jamfile                 # Print spooler
/src/servers/mail/Jamfile                  # Mail services
/src/servers/bluetooth/Jamfile             # Bluetooth stack
/src/servers/package/Jamfile               # Package management
/src/servers/keystore/Jamfile              # Security key management
/src/servers/launch/Jamfile                # Service launcher
/src/servers/mount/Jamfile                 # File system mounting
/src/servers/power/Jamfile                 # Power management
/src/servers/notification/Jamfile          # System notifications
/src/servers/index/Jamfile                 # File indexing service
/src/servers/debug/Jamfile                 # Debug services
/src/servers/syslog_daemon/Jamfile         # System logging
```

#### Complex Server Submodules
```
# Application Server
/src/servers/app/drawing/Jamfile           # Graphics drawing backend
/src/servers/app/drawing/Painter/Jamfile  # Drawing painter
/src/servers/app/drawing/interface/local/Jamfile  # Local interface
/src/servers/app/drawing/interface/remote/Jamfile # Remote interface
/src/servers/app/stackandtile/Jamfile      # Window tiling system
```

### Kits and Libraries (`/src/kits/`)
Shared libraries and frameworks - 50+ kit Jamfiles:

```
/src/kits/Jamfile                          # Kit coordination
/src/kits/app/Jamfile                      # Application kit
/src/kits/interface/Jamfile                # User interface kit  
/src/kits/storage/Jamfile                  # File storage kit
/src/kits/support/Jamfile                  # Support utilities kit
/src/kits/media/Jamfile                    # Media framework
/src/kits/network/Jamfile                  # Network communication kit
/src/kits/game/Jamfile                     # Game development kit
/src/kits/device/Jamfile                   # Device access kit
/src/kits/midi/Jamfile                     # MIDI support kit
/src/kits/print/Jamfile                    # Printing support kit
/src/kits/translation/Jamfile              # Data translation kit
/src/kits/locale/Jamfile                   # Internationalization kit
/src/kits/package/Jamfile                  # Package management kit
/src/kits/bluetooth/Jamfile                # Bluetooth support kit
/src/kits/mail/Jamfile                     # Mail handling kit
/src/kits/tracker/Jamfile                  # File manager kit
/src/kits/screensaver/Jamfile              # Screen saver framework
/src/kits/textencoding/Jamfile             # Text encoding support
/src/kits/shared/Jamfile                   # Shared utilities
/src/kits/debugger/Jamfile                 # Debugger framework
```

#### Kit Submodules
```
# Network Kit
/src/kits/network/libnetapi/Jamfile        # Network API
/src/kits/network/libnetservices/Jamfile   # Network services
/src/kits/network/libnetservices2/Jamfile  # Enhanced network services

# Package Kit
/src/kits/package/solver/Jamfile           # Dependency solver
/src/kits/package/solver/libsolv/Jamfile   # libsolv integration

# Storage Kit
/src/kits/storage/mime/Jamfile             # MIME type handling

# Debugger Kit
/src/kits/debugger/arch/x86/disasm/Jamfile     # x86 disassembler
/src/kits/debugger/arch/x86_64/disasm/Jamfile  # x86_64 disassembler
/src/kits/debugger/dwarf/Jamfile           # DWARF debug info
/src/kits/debugger/demangler/Jamfile       # Symbol demangling
```

### Add-ons (`/src/add-ons/`)
Loadable modules and drivers - 500+ add-on Jamfiles:

#### Kernel Add-ons
```
/src/add-ons/Jamfile                       # Add-on coordination
/src/add-ons/kernel/Jamfile                # Kernel add-on coordination

# Bus Managers
/src/add-ons/kernel/bus_managers/acpi/Jamfile     # ACPI support
/src/add-ons/kernel/bus_managers/pci/Jamfile      # PCI bus
/src/add-ons/kernel/bus_managers/usb/Jamfile      # USB bus
/src/add-ons/kernel/bus_managers/ata/Jamfile      # ATA/IDE bus
/src/add-ons/kernel/bus_managers/scsi/Jamfile     # SCSI bus
/src/add-ons/kernel/bus_managers/isa/Jamfile      # ISA bus
/src/add-ons/kernel/bus_managers/ps2/Jamfile      # PS/2 devices
/src/add-ons/kernel/bus_managers/mmc/Jamfile      # MMC/SD cards
/src/add-ons/kernel/bus_managers/i2c/Jamfile      # I2C bus
/src/add-ons/kernel/bus_managers/firewire/Jamfile # Firewire/IEEE1394
/src/add-ons/kernel/bus_managers/virtio/Jamfile   # VirtIO virtualization
/src/add-ons/kernel/bus_managers/agp_gart/Jamfile # AGP graphics
/src/add-ons/kernel/bus_managers/fdt/Jamfile      # Device tree
/src/add-ons/kernel/bus_managers/random/Jamfile   # Random number generation

# Bus Drivers (Hardware-Specific)
/src/add-ons/kernel/busses/pci/*/Jamfile          # PCI device drivers
/src/add-ons/kernel/busses/ata/*/Jamfile          # ATA controller drivers
/src/add-ons/kernel/busses/usb/*/Jamfile          # USB controller drivers
/src/add-ons/kernel/busses/scsi/*/Jamfile         # SCSI controller drivers
```

#### Graphics Accelerants
```
/src/add-ons/accelerants/*/Jamfile         # Graphics card drivers
/src/add-ons/accelerants/intel_extreme/Jamfile    # Intel graphics
/src/add-ons/accelerants/nvidia/Jamfile           # NVIDIA graphics  
/src/add-ons/accelerants/ati/Jamfile              # ATI graphics
/src/add-ons/accelerants/radeon/Jamfile           # AMD Radeon
/src/add-ons/accelerants/radeon_hd/Jamfile        # AMD Radeon HD
/src/add-ons/accelerants/intel_810/Jamfile        # Intel 810 graphics
/src/add-ons/accelerants/matrox/Jamfile           # Matrox graphics
/src/add-ons/accelerants/via/Jamfile              # VIA graphics
/src/add-ons/accelerants/s3/Jamfile               # S3 graphics
/src/add-ons/accelerants/vesa/Jamfile             # VESA graphics
/src/add-ons/accelerants/framebuffer/Jamfile      # Generic framebuffer
/src/add-ons/accelerants/virtio/Jamfile           # VirtIO graphics
```

#### Input Device Add-ons
```
/src/add-ons/input_server/Jamfile          # Input server coordination
/src/add-ons/input_server/devices/*/Jamfile       # Input device drivers
/src/add-ons/input_server/devices/keyboard/Jamfile # Keyboard driver
/src/add-ons/input_server/devices/mouse/Jamfile    # Mouse driver
/src/add-ons/input_server/devices/tablet/Jamfile   # Graphics tablet
/src/add-ons/input_server/devices/wacom/Jamfile    # Wacom tablet
/src/add-ons/input_server/devices/virtio/Jamfile   # VirtIO input
/src/add-ons/input_server/filters/*/Jamfile        # Input filters
/src/add-ons/input_server/methods/*/Jamfile        # Input methods
```

#### Decorators and UI
```
/src/add-ons/decorators/*/Jamfile          # Window decorators
/src/add-ons/decorators/BeDecorator/Jamfile       # BeOS-style decorator
/src/add-ons/decorators/MacDecorator/Jamfile      # Mac-style decorator  
/src/add-ons/decorators/WinDecorator/Jamfile      # Windows-style decorator
/src/add-ons/decorators/FlatDecorator/Jamfile     # Flat modern decorator

/src/add-ons/control_look/*/Jamfile        # Control appearance
/src/add-ons/control_look/BeControlLook/Jamfile   # BeOS control look
/src/add-ons/control_look/FlatControlLook/Jamfile # Flat control look
```

### Preferences (`/src/preferences/`)
System preference applications:

```
/src/preferences/Jamfile                   # Preferences coordination
/src/preferences/appearance/Jamfile        # UI appearance settings
/src/preferences/backgrounds/Jamfile       # Desktop wallpaper
/src/preferences/bluetooth/Jamfile         # Bluetooth configuration
/src/preferences/datatranslations/Jamfile  # Data format handling
/src/preferences/deskbar/Jamfile          # Taskbar preferences
/src/preferences/filetypes/Jamfile        # File type associations
/src/preferences/input/Jamfile            # Input device settings
/src/preferences/joysticks/Jamfile        # Game controller settings
/src/preferences/keymap/Jamfile           # Keyboard layout
/src/preferences/locale/Jamfile           # Language and region
/src/preferences/mail/Jamfile             # Email configuration
/src/preferences/media/Jamfile            # Media device settings
/src/preferences/network/Jamfile          # Network configuration
/src/preferences/notifications/Jamfile     # Notification settings
/src/preferences/printers/Jamfile         # Printer setup
/src/preferences/repositories/Jamfile      # Package repositories
/src/preferences/screen/Jamfile           # Display settings
/src/preferences/screensaver/Jamfile      # Screen saver settings
/src/preferences/shortcuts/Jamfile        # Keyboard shortcuts
/src/preferences/sounds/Jamfile           # System sounds
/src/preferences/time/Jamfile             # Date and time
/src/preferences/tracker/Jamfile          # File manager settings
/src/preferences/virtualmemory/Jamfile    # Virtual memory settings
```

### Bin Utilities (`/src/bin/`)
Command-line utilities and tools - 200+ utility Jamfiles:

```
/src/bin/Jamfile                          # Binary utilities coordination

# Core POSIX Tools
/src/bin/addattr/Jamfile                  # Extended attribute utility
/src/bin/catattr/Jamfile                  # Read extended attributes
/src/bin/listattr/Jamfile                 # List extended attributes
/src/bin/rmattr/Jamfile                   # Remove extended attributes
/src/bin/copyattr/Jamfile                 # Copy extended attributes

# System Management
/src/bin/pkgman/Jamfile                   # Package manager CLI
/src/bin/mount/Jamfile                    # File system mounting
/src/bin/unmount/Jamfile                  # File system unmounting
/src/bin/df/Jamfile                       # Disk space utility
/src/bin/ps/Jamfile                       # Process listing
/src/bin/kill/Jamfile                     # Process termination
/src/bin/top/Jamfile                      # Process monitor

# File Operations
/src/bin/ls/Jamfile                       # List directory contents
/src/bin/cp/Jamfile                       # Copy files
/src/bin/mv/Jamfile                       # Move/rename files
/src/bin/rm/Jamfile                       # Remove files
/src/bin/mkdir/Jamfile                    # Create directories
/src/bin/rmdir/Jamfile                    # Remove directories

# Archive and Compression
/src/bin/zip/Jamfile                      # ZIP creation
/src/bin/unzip/Jamfile                    # ZIP extraction
/src/bin/gzip/Jamfile                     # GZIP compression
/src/bin/tar/Jamfile                      # TAR archiver

# Debug Utilities
/src/bin/debug/Jamfile                    # Debug tool coordination
/src/bin/debug/ltrace/Jamfile             # Library call tracer
/src/bin/debug/strace/Jamfile             # System call tracer
/src/bin/debug/profile/Jamfile            # Performance profiler
```

### Tests (`/src/tests/`)
Test suites and validation - 300+ test Jamfiles:

```
/src/tests/Jamfile                        # Test coordination
/src/tests/system/kernel/*/Jamfile        # Kernel tests
/src/tests/kits/*/Jamfile                 # Kit functionality tests  
/src/tests/servers/*/Jamfile              # Server tests
/src/tests/add-ons/*/Jamfile              # Add-on tests
/src/tests/apps/*/Jamfile                 # Application tests
```

## Special Build Configurations

### Architecture-Specific Jamfiles
The build system includes architecture-specific Jamfiles for:

- **x86**: Legacy 32-bit Intel architecture
- **x86_64**: Modern 64-bit Intel/AMD architecture  
- **ARM**: 32-bit ARM architecture
- **ARM64/AArch64**: 64-bit ARM architecture
- **RISC-V 64**: 64-bit RISC-V architecture

### Platform-Specific Jamfiles
Platform-specific builds for:

- **EFI**: UEFI firmware platforms
- **BIOS**: Legacy BIOS platforms
- **U-Boot**: ARM bootloader platforms
- **OpenFirmware**: PowerPC firmware platforms

### Build Types
Different Jamfile inclusions based on build type:

- **Bootstrap**: Minimal build for initial compilation
- **Minimum**: Reduced feature set for embedded systems
- **Regular**: Full desktop system build

## Build Pattern Analysis

### Typical Jamfile Structure
Most Jamfiles follow these patterns:

1. **SubDir Declaration**: `SubDir HAIKU_TOP path ;`
2. **Header Dependencies**: `UsePrivateHeaders`, `UseLibraryHeaders`
3. **Build Target**: `Application`, `SharedLibrary`, `KernelAddon`, etc.
4. **Source Files**: List of .cpp/.c source files
5. **Library Dependencies**: Required libraries and frameworks
6. **Resource Files**: .rdef resource definitions
7. **Localization**: `DoCatalogs` for internationalization
8. **Subsystem Inclusion**: `SubInclude` for subdirectories

### Build Target Types Distribution
- **Applications**: ~85 GUI and CLI applications
- **Shared Libraries**: ~50 system kits and libraries
- **Kernel Add-ons**: ~500 drivers and kernel modules
- **Static Libraries**: ~100 utility and support libraries
- **Servers**: ~25 system services and daemons
- **Preferences**: ~25 system configuration apps
- **Tests**: ~300 validation and test programs

## Migration Considerations

### Build System Strengths
1. **Consistent Patterns**: All Jamfiles follow similar conventions
2. **Modular Design**: Clean separation of concerns
3. **Cross-Platform**: Support for multiple architectures
4. **Feature-Based Building**: Conditional compilation support
5. **Package Integration**: Seamless package system integration

### Complexity Areas
1. **Scale**: 1,322 Jamfiles require systematic migration
2. **Architecture Dependencies**: Multi-arch support complexity
3. **Build Features**: Conditional compilation logic
4. **Kernel Integration**: Special kernel build requirements
5. **Resource Management**: .rdef resource file handling

This inventory demonstrates the comprehensive nature of the Haiku build system and provides the foundation for understanding migration requirements to alternative build systems.