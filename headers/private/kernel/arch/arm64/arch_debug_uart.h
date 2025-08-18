/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Early Debug UART Header
 */

#ifndef _KERNEL_ARCH_ARM64_ARCH_DEBUG_UART_H
#define _KERNEL_ARCH_ARM64_ARCH_DEBUG_UART_H

#include <stdint.h>
#include <stdbool.h>

// Basic Haiku status type
#ifndef _STATUS_T_DECLARED
#define _STATUS_T_DECLARED
typedef int32_t status_t;
#endif

// Supported UART types
typedef enum {
    UART_TYPE_UNKNOWN = 0,
    UART_TYPE_PL011,        // ARM PrimeCell PL011 UART
    UART_TYPE_8250,         // Standard 8250/16550 UART
    UART_TYPE_8250_OMAP,    // TI OMAP 8250 variant
    UART_TYPE_LINFLEX,      // NXP LinFlexD UART
    UART_TYPE_SIFIVE,       // SiFive UART
    UART_TYPE_BCM2835,      // Broadcom BCM2835/2711 Mini UART
    UART_TYPE_ZYNQ,         // Xilinx Zynq UART
    UART_TYPE_IMX,          // NXP i.MX UART
    UART_TYPE_RCAR,         // Renesas R-Car UART
    UART_TYPE_MAX
} uart_type_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Early Debug UART Initialization
 */

// Initialize early debug UART with auto-detection
status_t arch_debug_uart_init(void);

// Initialize with specific UART configuration
status_t arch_debug_uart_init_config(uart_type_t type, uint64_t base_address, 
                                     uint32_t clock_freq, uint32_t baud_rate);

// Check if debug UART is available and initialized
bool arch_debug_uart_available(void);

/*
 * Character I/O Functions
 */

// Send a single character
status_t arch_debug_uart_putchar(char c);

// Send a string
status_t arch_debug_uart_puts(const char* str);

// Receive a character (non-blocking)
int arch_debug_uart_getchar(void);

// Check if receive data is available
bool arch_debug_uart_rx_ready(void);

/*
 * Formatted Output
 */

// Printf-style formatted output for early debugging
status_t arch_debug_uart_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

/*
 * Configuration and Diagnostics
 */

// Get current UART configuration
status_t arch_debug_uart_get_config(uart_type_t* type, uint64_t* base_address, 
                                    uint32_t* clock_freq, uint32_t* baud_rate);

// Dump debug UART information
void arch_debug_uart_dump_info(void);

/*
 * Common UART Base Addresses (for reference)
 */

// Raspberry Pi 4/5 (BCM2711/BCM2712)
#define ARM64_UART_BCM2711_PL011        0xFE201000
#define ARM64_UART_BCM2711_MINI         0xFE215040

// QEMU virt machine
#define ARM64_UART_QEMU_PL011           0x09000000

// ARM Development Boards
#define ARM64_UART_VEXPRESS_PL011       0x1C090000
#define ARM64_UART_VERSATILE_PL011      0x10009000
#define ARM64_UART_INTEGRATOR_PL011     0x101F1000

// i.MX8 Series (NXP)
#define ARM64_UART_IMX8_UART1           0x30860000
#define ARM64_UART_IMX8_UART2           0x30890000

// Zynq UltraScale+ (Xilinx)
#define ARM64_UART_ZYNQUS_UART0         0xFF000000
#define ARM64_UART_ZYNQUS_UART1         0xFF010000

// R-Car Series (Renesas)
#define ARM64_UART_RCAR_SCIF0           0xE6E68000
#define ARM64_UART_RCAR_SCIF1           0xE6E60000

/*
 * Early Debug Macros
 */

// Early debug printf (can be disabled at compile time)
#ifndef DEBUG_EARLY_UART_DISABLED
#define dprintf_early(fmt, ...) arch_debug_uart_printf(fmt, ##__VA_ARGS__)
#else
#define dprintf_early(fmt, ...) ((void)0)
#endif

// Early debug puts
#define dputs_early(str) arch_debug_uart_puts(str)

// Early debug character output
#define dputc_early(c) arch_debug_uart_putchar(c)

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_ARM64_ARCH_DEBUG_UART_H */