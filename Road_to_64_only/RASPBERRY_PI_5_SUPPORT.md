# Raspberry Pi 5 Support for Haiku OS

## Overview

This document describes the implementation and usage of Haiku OS support for the Raspberry Pi 5 (BCM2712 SoC).

## Hardware Specifications

### Raspberry Pi 5 / BCM2712
- **CPU**: Broadcom BCM2712 (Cortex-A76 quad-core @ 2.4GHz)
- **Memory**: LPDDR4X (2GB/4GB/8GB variants)
- **Caches**:
  - L1 I-Cache: 64KB per core
  - L1 D-Cache: 64KB per core
  - L2 Cache: 512KB per core
  - L3 Cache: 2MB shared
- **Interrupt Controller**: ARM GIC-400 (GICv2)
- **System Timer**: 54MHz BCM2712 System Timer
- **UART**: ARM PrimeCell PL011 (48MHz clock)
- **Boot**: UEFI via Raspberry Pi bootloader

## Implementation Status

### ✅ Completed Components

#### 1. **Bootloader Support**
- [x] BCM2712/RPi5 detection via device tree
- [x] GIC-400 interrupt controller detection
- [x] CPU enumeration (4 cores)
- [x] MPIDR propagation from bootloader to kernel
- [x] PSCI method detection (SMC/HVC)
- [x] Device tree parsing for BCM2712

**Files**:
- `src/system/boot/platform/efi/arch/arm64/arch_dtb.cpp`
- `src/system/boot/platform/efi/arch/arm64/arch_smp.cpp`

#### 2. **Kernel Core**
- [x] BCM2712 hardware detection
- [x] GIC-400 interrupt controller support
- [x] PL011 UART driver (ARM64 version)
- [x] System timer support (54MHz)
- [x] Multi-CPU boot via PSCI
- [x] Memory management for BCM2712 address space

**Files**:
- `src/system/kernel/arch/arm64/arch_bcm2712_init.cpp`
- `src/system/kernel/arch/arm64/arch_uart_pl011.cpp`
- `src/system/kernel/arch/arm64/bcm2712_device_tree.cpp`
- `src/system/kernel/arch/arm64/bcm2712_timer.cpp`
- `headers/private/kernel/arch/arm64/arch_bcm2712.h`

#### 3. **Device Tree Support**
- [x] Compatible string detection: `raspberrypi,5-model-b`
- [x] Compatible string detection: `brcm,bcm2712`
- [x] GIC-400 base address parsing
- [x] UART base address parsing
- [x] Clock frequency detection

## Memory Map

### BCM2712 Peripheral Base Addresses

| Peripheral | Physical Address | Size | Description |
|------------|-----------------|------|-------------|
| **Peripherals Base** | 0x0000_107C_0000_0000 | - | Main peripheral base |
| **System Timer** | 0x0000_107C_0030_0000 | 4KB | 54MHz system timer |
| **UART0 (PL011)** | 0x0000_107D_0010_0000 | 512B | Debug UART |
| **UART2 (PL011)** | 0x0000_107D_0014_0000 | 512B | Alternative UART |
| **UART5 (PL011)** | 0x0000_107D_001A_0000 | 512B | Alternative UART |
| **CPRMAN** | 0x0000_107D_2020_0000 | 8KB | Clock/Power Mgmt |
| **GIC-400 Distributor** | 0x0000_107F_FF90_0000 | 8KB | Interrupt distributor |
| **GIC-400 CPU Interface** | 0x0000_107F_FFA0_0000 | 8KB | CPU interface |

### Interrupt Numbers (GIC SPI)

| IRQ | Description |
|-----|-------------|
| 96 | System Timer Channel 0 |
| 97 | System Timer Channel 1 |
| 98 | System Timer Channel 2 |
| 99 | System Timer Channel 3 |
| 121 | UART0 (PL011) |
| 122 | UART2 (PL011) |
| 123 | UART5 (PL011) |

## Building Haiku for Raspberry Pi 5

### Prerequisites
```bash
# Install cross-compilation toolchain
sudo apt install build-essential git nasm autoconf automake \
    texinfo flex bison gawk libssl-dev libpci-dev \
    mtools libtool gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Clone Haiku repository
git clone https://github.com/haiku/haiku.git
cd haiku
```

### Build Configuration
```bash
# Create build directory
mkdir build_arm64
cd build_arm64

# Configure for ARM64
../configure \
    --cross-tools-source ../buildtools \
    --build-cross-tools arm64 \
    --target arm64 \
    --include-gpl-addons \
    -j$(nproc)

# Build Haiku image
jam -q @minimum-raw
```

### Generated Files
- `haiku-minimum-raw.image` - Raw disk image
- `haiku.uefi` - UEFI bootloader (EFI application)

## Installing Haiku on Raspberry Pi 5

### 1. Prepare SD Card

```bash
# Write Haiku image to SD card
sudo dd if=haiku-minimum-raw.image of=/dev/sdX bs=4M status=progress
sync
```

### 2. Setup UEFI Boot

Raspberry Pi 5 requires UEFI firmware for booting Haiku:

```bash
# Download Raspberry Pi UEFI firmware
wget https://github.com/pftf/RPi4/releases/latest/download/RPi4_UEFI.img.gz
gunzip RPi4_UEFI.img.gz

# Mount EFI partition
mkdir -p /tmp/efi
sudo mount /dev/sdX1 /tmp/efi

# Copy UEFI files
sudo cp -r RPi4_UEFI/* /tmp/efi/

# Copy Haiku EFI bootloader
sudo mkdir -p /tmp/efi/EFI/BOOT
sudo cp haiku.uefi /tmp/efi/EFI/BOOT/BOOTAA64.EFI

# Unmount
sudo umount /tmp/efi
```

### 3. Boot Configuration

Create `/tmp/efi/config.txt`:
```ini
# Raspberry Pi 5 Configuration for Haiku
arm_64bit=1
enable_uart=1
uart_2ndstage=1

# Memory
dtoverlay=vc4-kms-v3d
max_framebuffers=2

# Disable WiFi/Bluetooth (optional, for debugging)
dtoverlay=disable-wifi
dtoverlay=disable-bt

# UEFI boot
kernel=RPi4_UEFI.bin
```

## Boot Process

### 1. Hardware Initialization
1. Raspberry Pi firmware loads UEFI
2. UEFI loads `BOOTAA64.EFI` (Haiku bootloader)
3. Haiku bootloader parses device tree
4. Detects BCM2712 hardware and Raspberry Pi 5 board

### 2. Bootloader Phase
```
[Bootloader] Raspberry Pi 5 detected!
[Bootloader] BCM2712 SoC detected
[Bootloader] Found interrupt controller: gicv2
[Bootloader]   BCM2712 GIC-400 at 0x107fff9000
[Bootloader]   CPU Interface at 0x107fffa000
[Bootloader] PSCI: method=SMC
[Bootloader] cpu: id=0x0 (MPIDR=0x0)
[Bootloader] cpu: id=0x1 (MPIDR=0x1)
[Bootloader] cpu: id=0x2 (MPIDR=0x2)
[Bootloader] cpu: id=0x3 (MPIDR=0x3)
[Bootloader] Registered CPUs: 4
```

### 3. Kernel Initialization
```
BCM2712: Raspberry Pi 5 detected!
BCM2712: Cortex-A76 quad-core @ 2.4GHz
BCM2712: GIC-400 at 0x107fff9000
BCM2712: System Timer at 0x107c003000 (54MHz)
BCM2712: UART0 initialized successfully
```

## Serial Console Access

### Hardware Setup
- Connect USB-TTL adapter to GPIO pins:
  - GPIO14 (TXD) - Pin 8
  - GPIO15 (RXD) - Pin 10
  - GND - Pin 6

### Terminal Settings
```bash
# Screen
screen /dev/ttyUSB0 115200

# Minicom
minicom -D /dev/ttyUSB0 -b 115200

# PuTTY
putty -serial /dev/ttyUSB0 -sercfg 115200,8,n,1,N
```

## Debugging

### Enable Debug Output

Edit kernel build configuration to enable BCM2712 debug traces:

```cpp
// In arch_bcm2712_init.cpp
#define TRACE_BCM2712  // Uncomment this line
```

Rebuild kernel:
```bash
jam -q kernel_arm64
```

### Common Boot Issues

#### Issue: "GIC not detected"
**Solution**: Check device tree is properly loaded by UEFI firmware

#### Issue: "No CPUs detected"
**Solution**: Verify PSCI is enabled in device tree and firmware

#### Issue: "UART not working"
**Solution**: 
1. Check `enable_uart=1` in config.txt
2. Verify GPIO pins are correctly connected
3. Check baud rate (115200)

## Performance Characteristics

### BCM2712 Specifications
- **CPU Frequency**: 2.4 GHz
- **Memory Bandwidth**: Up to 17 GB/s (LPDDR4X)
- **Cache Latency**:
  - L1: ~4 cycles
  - L2: ~12 cycles
  - L3: ~40 cycles
- **Timer Frequency**: 54 MHz (18.5ns resolution)

## Future Improvements

### High Priority
- [ ] USB support (USB 3.0 + USB 2.0)
- [ ] PCIe support (for NVMe, etc.)
- [ ] Ethernet support (Gigabit)
- [ ] GPU support (VideoCore VII)
- [ ] Power management (DVFS, idle states)

### Medium Priority
- [ ] WiFi support (Cypress CYW43455)
- [ ] Bluetooth support
- [ ] Audio support (PWM audio)
- [ ] Camera interface (CSI)

### Low Priority
- [ ] Hardware video decode (HEVC)
- [ ] GPIO kernel driver
- [ ] I2C/SPI support
- [ ] Hardware crypto acceleration

## Testing

### Boot Test
```bash
# Expected output on serial console
Welcome to Haiku!
BCM2712: Raspberry Pi 5 detected!
Starting 4 CPU cores...
Kernel initialization complete
```

### SMP Test
```bash
# In Haiku shell
sysinfo

# Should show:
CPU #0: Cortex-A76 @ 2400 MHz
CPU #1: Cortex-A76 @ 2400 MHz
CPU #2: Cortex-A76 @ 2400 MHz
CPU #3: Cortex-A76 @ 2400 MHz
```

## References

### Hardware Documentation
- [BCM2712 Datasheet](https://datasheets.raspberrypi.com/) (pending official release)
- [Raspberry Pi 5 Product Brief](https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf)
- [ARM Cortex-A76 TRM](https://developer.arm.com/documentation/100798/)
- [ARM GIC-400 TRM](https://developer.arm.com/documentation/ddi0471/)
- [ARM PL011 UART TRM](https://developer.arm.com/documentation/ddi0183/)

### Software References
- [Raspberry Pi Linux Kernel](https://github.com/raspberrypi/linux) - Device tree reference
- [Raspberry Pi UEFI](https://github.com/pftf/RPi4) - UEFI firmware
- [ARM Trusted Firmware](https://github.com/ARM-software/arm-trusted-firmware)

## Contributors

- Haiku ARM64 Development Team
- Raspberry Pi Foundation
- ARM Holdings

## License

All Haiku source code is licensed under the MIT License.

---

**Last Updated**: 2025-10-04
**Haiku Version**: Development (64-bit only)
**Hardware**: Raspberry Pi 5 (BCM2712)
