/*
 * Copyright 2024-2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * BCM2712 (Raspberry Pi 5) Hardware Definitions
 * 
 * This header provides hardware definitions for the Broadcom BCM2712 SoC
 * found in the Raspberry Pi 5. It includes memory-mapped register addresses,
 * interrupt numbers, and hardware configuration constants.
 *
 * BCM2712 Specifications:
 * - CPU: Quad-core Cortex-A76 @ 2.4GHz
 * - L1 Cache: 64KB I + 64KB D per core
 * - L2 Cache: 512KB per core
 * - L3 Cache: 2MB shared
 * - System Timer: 54MHz
 * - Interrupt Controller: GIC-400 (GICv2)
 * - UART: ARM PL011
 * - Memory: LPDDR4X up to 8GB
 */

#ifndef _KERNEL_ARCH_ARM64_BCM2712_H
#define _KERNEL_ARCH_ARM64_BCM2712_H

#include <SupportDefs.h>

// ============================================================================
// BCM2712 Memory Map
// ============================================================================

// Peripheral Base Addresses (Physical)
#define BCM2712_PERI_BASE           0x0000107C000000ULL  // Peripheral base
#define BCM2712_GIC_BASE            0x0000107FFF0000ULL  // GIC-400 base

// System Timer
#define BCM2712_SYSTIMER_BASE       0x0000107C003000ULL
#define BCM2712_SYSTIMER_SIZE       0x1000

// Clock and Power Management (CPRMAN)
#define BCM2712_CPRMAN_BASE         0x0000107D202000ULL
#define BCM2712_CPRMAN_SIZE         0x2000

// UART Addresses
#define BCM2712_UART0_BASE          0x0000107D001000ULL  // PL011 UART0
#define BCM2712_UART2_BASE          0x0000107D001400ULL  // PL011 UART2
#define BCM2712_UART5_BASE          0x0000107D001A00ULL  // PL011 UART5
#define BCM2712_UART_SIZE           0x200

// GIC-400 Interrupt Controller (GICv2)
#define BCM2712_GICD_BASE           0x0000107FFF9000ULL  // Distributor
#define BCM2712_GICC_BASE           0x0000107FFFA000ULL  // CPU Interface
#define BCM2712_GICH_BASE           0x0000107FFFC000ULL  // Hypervisor Interface
#define BCM2712_GICV_BASE           0x0000107FFFE000ULL  // Virtual Interface
#define BCM2712_GIC_SIZE            0x2000

// PCIe Controller
#define BCM2712_PCIE_BASE           0x0000108000000000ULL
#define BCM2712_PCIE_SIZE           0x400000

// ============================================================================
// BCM2712 Interrupt Numbers
// ============================================================================

// System Timer Interrupts (GIC SPI)
#define BCM2712_IRQ_SYSTIMER_0      96   // System Timer Channel 0
#define BCM2712_IRQ_SYSTIMER_1      97   // System Timer Channel 1
#define BCM2712_IRQ_SYSTIMER_2      98   // System Timer Channel 2
#define BCM2712_IRQ_SYSTIMER_3      99   // System Timer Channel 3

// UART Interrupts (GIC SPI)
#define BCM2712_IRQ_UART0           121  // UART0 (PL011)
#define BCM2712_IRQ_UART2           122  // UART2 (PL011)
#define BCM2712_IRQ_UART5           123  // UART5 (PL011)

// ARM Generic Timer Interrupts (GIC PPI)
#define BCM2712_IRQ_TIMER_PHYS      30   // Physical Timer (PPI 14)
#define BCM2712_IRQ_TIMER_VIRT      27   // Virtual Timer (PPI 11)
#define BCM2712_IRQ_TIMER_HYP       26   // Hypervisor Timer (PPI 10)
#define BCM2712_IRQ_TIMER_SEC       29   // Secure Physical Timer (PPI 13)

// ============================================================================
// BCM2712 System Timer Configuration
// ============================================================================

#define BCM2712_TIMER_FREQ          54000000    // 54 MHz base frequency
#define BCM2712_TIMER_CHANNELS      4           // 4 timer channels
#define BCM2712_TICKS_PER_USEC      54          // Ticks per microsecond
#define BCM2712_NSEC_PER_TICK       18          // Nanoseconds per tick (1000/54)

// ============================================================================
// BCM2712 CPU Configuration
// ============================================================================

#define BCM2712_CPU_COUNT           4           // Quad-core
#define BCM2712_CPU_MAX_FREQ        2400000000  // 2.4 GHz
#define BCM2712_CPU_TYPE            "Cortex-A76"

// Cache Sizes
#define BCM2712_L1_ICACHE_SIZE      (64 * 1024)     // 64KB per core
#define BCM2712_L1_DCACHE_SIZE      (64 * 1024)     // 64KB per core
#define BCM2712_L2_CACHE_SIZE       (512 * 1024)    // 512KB per core
#define BCM2712_L3_CACHE_SIZE       (2 * 1024 * 1024)  // 2MB shared

#define BCM2712_CACHE_LINE_SIZE     64              // 64 bytes

// ============================================================================
// BCM2712 Device Tree Compatibility Strings
// ============================================================================

#define BCM2712_DT_COMPAT           "brcm,bcm2712"
#define BCM2712_DT_COMPAT_RPI5      "raspberrypi,5-model-b"
#define BCM2712_DT_COMPAT_CM5       "raspberrypi,5-compute-module"

// ============================================================================
// BCM2712 System Timer Registers
// ============================================================================

#define BCM2712_ST_CS               0x00    // Control/Status
#define BCM2712_ST_CLO              0x04    // Counter Lower 32 bits
#define BCM2712_ST_CHI              0x08    // Counter Higher 32 bits
#define BCM2712_ST_C0               0x0C    // Compare 0
#define BCM2712_ST_C1               0x10    // Compare 1
#define BCM2712_ST_C2               0x14    // Compare 2
#define BCM2712_ST_C3               0x18    // Compare 3

// Control/Status Register Bits
#define BCM2712_ST_CS_M0            (1 << 0)   // Timer 0 Match
#define BCM2712_ST_CS_M1            (1 << 1)   // Timer 1 Match
#define BCM2712_ST_CS_M2            (1 << 2)   // Timer 2 Match
#define BCM2712_ST_CS_M3            (1 << 3)   // Timer 3 Match

// ============================================================================
// BCM2712 UART Configuration (PL011)
// ============================================================================

#define BCM2712_UART_CLOCK          48000000    // 48 MHz UART clock
#define BCM2712_UART_BAUD           115200      // Default baud rate

#endif /* _KERNEL_ARCH_ARM64_BCM2712_H */
