/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Early Debug UART Implementation
 * 
 * This module provides early UART output functionality for ARM64 systems,
 * enabling serial debugging output before device drivers are loaded.
 * Supports multiple UART types commonly found in ARM64 systems.
 *
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Basic Haiku types for kernel compilation
typedef int32_t status_t;
#define B_OK                     0
#define B_ERROR                 -1
#define B_NOT_SUPPORTED         -2147483647
#define B_NOT_INITIALIZED       -2147483646
#define B_BAD_VALUE             -2147483645
#define B_TIMEOUT               -2147483644

// Early debug support - actual implementation would be provided by kernel
#ifndef DEBUG_EARLY_UART_DISABLED
#define dprintf_early(fmt, ...) arch_debug_uart_printf(fmt, ##__VA_ARGS__)
#else
#define dprintf_early(fmt, ...) ((void)0)
#endif

/*
 * ARM64 UART Type Support
 */

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

// UART configuration structure
struct uart_config {
    uart_type_t type;
    uint64_t base_address;
    uint32_t clock_frequency;
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    bool parity_enable;
    bool parity_odd;
    bool initialized;
    const char* name;
};

/*
 * ARM PrimeCell PL011 UART Registers
 */
#define PL011_DR        0x000   // Data register
#define PL011_RSR       0x004   // Receive status register
#define PL011_FR        0x018   // Flag register
#define PL011_ILPR      0x020   // IrDA low-power counter register
#define PL011_IBRD      0x024   // Integer baud rate register
#define PL011_FBRD      0x028   // Fractional baud rate register
#define PL011_LCR_H     0x02C   // Line control register
#define PL011_CR        0x030   // Control register
#define PL011_IFLS      0x034   // Interrupt FIFO level select register
#define PL011_IMSC      0x038   // Interrupt mask set/clear register
#define PL011_RIS       0x03C   // Raw interrupt status register
#define PL011_MIS       0x040   // Masked interrupt status register
#define PL011_ICR       0x044   // Interrupt clear register
#define PL011_DMACR     0x048   // DMA control register

// PL011 Flag register bits
#define PL011_FR_TXFE   (1 << 7)    // Transmit FIFO empty
#define PL011_FR_RXFF   (1 << 6)    // Receive FIFO full
#define PL011_FR_TXFF   (1 << 5)    // Transmit FIFO full
#define PL011_FR_RXFE   (1 << 4)    // Receive FIFO empty
#define PL011_FR_BUSY   (1 << 3)    // UART busy

// PL011 Control register bits
#define PL011_CR_CTSEN  (1 << 15)   // CTS hardware flow control enable
#define PL011_CR_RTSEN  (1 << 14)   // RTS hardware flow control enable
#define PL011_CR_RTS    (1 << 11)   // Request to send
#define PL011_CR_RXE    (1 << 9)    // Receive enable
#define PL011_CR_TXE    (1 << 8)    // Transmit enable
#define PL011_CR_LBE    (1 << 7)    // Loopback enable
#define PL011_CR_UARTEN (1 << 0)    // UART enable

// PL011 Line control register bits
#define PL011_LCR_H_SPS     (1 << 7)   // Stick parity select
#define PL011_LCR_H_WLEN8   (3 << 5)   // Word length 8 bits
#define PL011_LCR_H_WLEN7   (2 << 5)   // Word length 7 bits
#define PL011_LCR_H_WLEN6   (1 << 5)   // Word length 6 bits
#define PL011_LCR_H_WLEN5   (0 << 5)   // Word length 5 bits
#define PL011_LCR_H_FEN     (1 << 4)   // Enable FIFOs
#define PL011_LCR_H_STP2    (1 << 3)   // Two stop bits select
#define PL011_LCR_H_EPS     (1 << 2)   // Even parity select
#define PL011_LCR_H_PEN     (1 << 1)   // Parity enable
#define PL011_LCR_H_BRK     (1 << 0)   // Send break

/*
 * 8250/16550 UART Registers
 */
#define UART_8250_THR   0x0     // Transmit holding register
#define UART_8250_RBR   0x0     // Receive buffer register
#define UART_8250_DLL   0x0     // Divisor latch low (when DLAB=1)
#define UART_8250_IER   0x1     // Interrupt enable register
#define UART_8250_DLH   0x1     // Divisor latch high (when DLAB=1)
#define UART_8250_IIR   0x2     // Interrupt identification register
#define UART_8250_FCR   0x2     // FIFO control register
#define UART_8250_LCR   0x3     // Line control register
#define UART_8250_MCR   0x4     // Modem control register
#define UART_8250_LSR   0x5     // Line status register
#define UART_8250_MSR   0x6     // Modem status register
#define UART_8250_SCR   0x7     // Scratch register

// 8250 Line status register bits
#define UART_8250_LSR_TEMT  (1 << 6)   // Transmitter empty
#define UART_8250_LSR_THRE  (1 << 5)   // Transmit holding register empty
#define UART_8250_LSR_BI    (1 << 4)   // Break interrupt
#define UART_8250_LSR_FE    (1 << 3)   // Framing error
#define UART_8250_LSR_PE    (1 << 2)   // Parity error
#define UART_8250_LSR_OE    (1 << 1)   // Overrun error
#define UART_8250_LSR_DR    (1 << 0)   // Data ready

// 8250 Line control register bits
#define UART_8250_LCR_DLAB  (1 << 7)   // Divisor latch access bit
#define UART_8250_LCR_SBC   (1 << 6)   // Set break control
#define UART_8250_LCR_SPAR  (1 << 5)   // Stick parity
#define UART_8250_LCR_EPAR  (1 << 4)   // Even parity select
#define UART_8250_LCR_PARITY (1 << 3)  // Parity enable
#define UART_8250_LCR_STOP  (1 << 2)   // Number of stop bits
#define UART_8250_LCR_WLEN8 (0x3 << 0) // Word length 8 bits
#define UART_8250_LCR_WLEN7 (0x2 << 0) // Word length 7 bits
#define UART_8250_LCR_WLEN6 (0x1 << 0) // Word length 6 bits
#define UART_8250_LCR_WLEN5 (0x0 << 0) // Word length 5 bits

/*
 * BCM2835 Mini UART Registers (Raspberry Pi)
 */
#define BCM2835_MU_IO       0x40    // I/O data
#define BCM2835_MU_IER      0x44    // Interrupt enable
#define BCM2835_MU_IIR      0x48    // Interrupt identify
#define BCM2835_MU_LCR      0x4C    // Line control
#define BCM2835_MU_MCR      0x50    // Modem control
#define BCM2835_MU_LSR      0x54    // Line status
#define BCM2835_MU_MSR      0x58    // Modem status
#define BCM2835_MU_SCRATCH  0x5C    // Scratch
#define BCM2835_MU_CNTL     0x60    // Extra control
#define BCM2835_MU_STAT     0x64    // Extra status
#define BCM2835_MU_BAUD     0x68    // Baud rate

#define BCM2835_MU_LSR_TX_IDLE  (1 << 6)
#define BCM2835_MU_LSR_TX_EMPTY (1 << 5)
#define BCM2835_MU_LSR_RX_READY (1 << 0)

/*
 * Global UART state
 */
static struct uart_config debug_uart = {
    .type = UART_TYPE_UNKNOWN,
    .base_address = 0,
    .clock_frequency = 24000000,    // Default 24MHz
    .baud_rate = 115200,            // Default 115200 baud
    .data_bits = 8,
    .stop_bits = 1,
    .parity_enable = false,
    .parity_odd = false,
    .initialized = false,
    .name = "Unknown"
};

// Hardware register access functions
static inline void
write_reg32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t*)address = value;
}

static inline uint32_t
read_reg32(uint64_t address)
{
    return *(volatile uint32_t*)address;
}

static inline void
write_reg8(uint64_t address, uint8_t value)
{
    *(volatile uint8_t*)address = value;
}

static inline uint8_t
read_reg8(uint64_t address)
{
    return *(volatile uint8_t*)address;
}

/*
 * UART Type Detection
 */

// Common ARM64 UART base addresses for detection
static const struct {
    uart_type_t type;
    uint64_t base_address;
    const char* name;
    const char* description;
} uart_detection_table[] = {
    // Raspberry Pi 4/5 (BCM2711/BCM2712)
    {UART_TYPE_PL011,   0xFE201000, "BCM2711-PL011", "Raspberry Pi 4/5 PL011 UART"},
    {UART_TYPE_BCM2835, 0xFE215040, "BCM2711-MiniUART", "Raspberry Pi 4/5 Mini UART"},
    
    // QEMU virt machine
    {UART_TYPE_PL011,   0x09000000, "QEMU-PL011", "QEMU virt machine PL011 UART"},
    
    // Common ARM development boards
    {UART_TYPE_PL011,   0x1C090000, "VExpress-PL011", "ARM Versatile Express PL011"},
    {UART_TYPE_PL011,   0x10009000, "VersatilePB-PL011", "ARM Versatile/PB PL011"},
    {UART_TYPE_PL011,   0x101F1000, "Integrator-PL011", "ARM Integrator/CP PL011"},
    
    // i.MX series (NXP)
    {UART_TYPE_8250,    0x30860000, "IMX8-UART1", "i.MX8 UART1"},
    {UART_TYPE_8250,    0x30890000, "IMX8-UART2", "i.MX8 UART2"},
    
    // Zynq UltraScale+ (Xilinx)
    {UART_TYPE_ZYNQ,    0xFF000000, "ZynqUS-UART0", "Zynq UltraScale+ UART0"},
    {UART_TYPE_ZYNQ,    0xFF010000, "ZynqUS-UART1", "Zynq UltraScale+ UART1"},
    
    // R-Car series (Renesas)
    {UART_TYPE_RCAR,    0xE6E68000, "RCar-SCIF0", "R-Car SCIF0"},
    {UART_TYPE_RCAR,    0xE6E60000, "RCar-SCIF1", "R-Car SCIF1"},
    
    // End marker
    {UART_TYPE_UNKNOWN, 0, NULL, NULL}
};

// Check if a UART is present at given address
static bool
uart_probe_address(uint64_t base_address, uart_type_t expected_type)
{
    // Basic probe - try to read/write a register safely
    switch (expected_type) {
        case UART_TYPE_PL011: {
            // Read PL011 peripheral ID registers to verify presence
            uint32_t pid0 = read_reg32(base_address + 0xFE0);
            uint32_t pid1 = read_reg32(base_address + 0xFE4);
            uint32_t pid2 = read_reg32(base_address + 0xFE8);
            uint32_t pid3 = read_reg32(base_address + 0xFEC);
            
            // PL011 should have PID 0x00041011
            uint32_t pid = ((pid3 & 0xFF) << 24) | ((pid2 & 0xFF) << 16) |
                          ((pid1 & 0xFF) << 8) | (pid0 & 0xFF);
                          
            return (pid == 0x00041011);
        }
        
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP: {
            // For 8250, try to read/write scratch register
            uint8_t orig = read_reg8(base_address + UART_8250_SCR);
            write_reg8(base_address + UART_8250_SCR, 0x55);
            uint8_t test1 = read_reg8(base_address + UART_8250_SCR);
            write_reg8(base_address + UART_8250_SCR, 0xAA);
            uint8_t test2 = read_reg8(base_address + UART_8250_SCR);
            write_reg8(base_address + UART_8250_SCR, orig);
            
            return (test1 == 0x55 && test2 == 0xAA);
        }
        
        case UART_TYPE_BCM2835: {
            // BCM2835 Mini UART - check if we can read status register
            uint32_t stat = read_reg32(base_address + BCM2835_MU_LSR);
            // Should be able to read some meaningful status bits
            return (stat != 0xFFFFFFFF && stat != 0x00000000);
        }
        
        default:
            // For other types, just check if we can read without bus fault
            uint32_t test = read_reg32(base_address);
            return (test != 0xFFFFFFFF);
    }
}

// Auto-detect UART type and address
static status_t
uart_auto_detect(void)
{
    for (int i = 0; uart_detection_table[i].type != UART_TYPE_UNKNOWN; i++) {
        const auto& entry = uart_detection_table[i];
        
        if (uart_probe_address(entry.base_address, entry.type)) {
            debug_uart.type = entry.type;
            debug_uart.base_address = entry.base_address;
            debug_uart.name = entry.name;
            
            return B_OK;
        }
    }
    
    return B_NOT_SUPPORTED;
}

/*
 * UART Hardware Abstraction Layer
 */

// Wait for transmit ready
static bool
uart_wait_tx_ready(uint32_t timeout_us)
{
    uint32_t count = 0;
    
    switch (debug_uart.type) {
        case UART_TYPE_PL011: {
            while (count < timeout_us) {
                uint32_t fr = read_reg32(debug_uart.base_address + PL011_FR);
                if (!(fr & PL011_FR_TXFF)) {  // TX FIFO not full
                    return true;
                }
                count++;
            }
            break;
        }
        
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP: {
            while (count < timeout_us) {
                uint8_t lsr = read_reg8(debug_uart.base_address + UART_8250_LSR);
                if (lsr & UART_8250_LSR_THRE) {  // TX holding register empty
                    return true;
                }
                count++;
            }
            break;
        }
        
        case UART_TYPE_BCM2835: {
            while (count < timeout_us) {
                uint32_t lsr = read_reg32(debug_uart.base_address + BCM2835_MU_LSR);
                if (lsr & BCM2835_MU_LSR_TX_EMPTY) {  // TX empty
                    return true;
                }
                count++;
            }
            break;
        }
        
        default:
            return false;
    }
    
    return false;
}

// Send a single character
static status_t
uart_putchar(char c)
{
    if (!debug_uart.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    // Wait for transmit ready
    if (!uart_wait_tx_ready(10000)) {  // 10ms timeout
        return B_TIMEOUT;
    }
    
    // Send character
    switch (debug_uart.type) {
        case UART_TYPE_PL011:
            write_reg32(debug_uart.base_address + PL011_DR, (uint32_t)c);
            break;
            
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP:
            write_reg8(debug_uart.base_address + UART_8250_THR, (uint8_t)c);
            break;
            
        case UART_TYPE_BCM2835:
            write_reg32(debug_uart.base_address + BCM2835_MU_IO, (uint32_t)c);
            break;
            
        default:
            return B_NOT_SUPPORTED;
    }
    
    return B_OK;
}

// Check if receive data is available
static bool
uart_rx_ready(void)
{
    if (!debug_uart.initialized) {
        return false;
    }
    
    switch (debug_uart.type) {
        case UART_TYPE_PL011: {
            uint32_t fr = read_reg32(debug_uart.base_address + PL011_FR);
            return !(fr & PL011_FR_RXFE);  // RX FIFO not empty
        }
        
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP: {
            uint8_t lsr = read_reg8(debug_uart.base_address + UART_8250_LSR);
            return (lsr & UART_8250_LSR_DR);  // Data ready
        }
        
        case UART_TYPE_BCM2835: {
            uint32_t lsr = read_reg32(debug_uart.base_address + BCM2835_MU_LSR);
            return (lsr & BCM2835_MU_LSR_RX_READY);  // RX ready
        }
        
        default:
            return false;
    }
}

// Receive a single character (non-blocking)
static int
uart_getchar(void)
{
    if (!debug_uart.initialized || !uart_rx_ready()) {
        return -1;
    }
    
    switch (debug_uart.type) {
        case UART_TYPE_PL011:
            return (int)(read_reg32(debug_uart.base_address + PL011_DR) & 0xFF);
            
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP:
            return (int)read_reg8(debug_uart.base_address + UART_8250_RBR);
            
        case UART_TYPE_BCM2835:
            return (int)(read_reg32(debug_uart.base_address + BCM2835_MU_IO) & 0xFF);
            
        default:
            return -1;
    }
}

/*
 * UART Initialization Functions
 */

// Initialize PL011 UART
static status_t
uart_init_pl011(void)
{
    uint64_t base = debug_uart.base_address;
    
    // Disable UART first
    write_reg32(base + PL011_CR, 0);
    
    // Calculate baud rate divisors
    // UARTCLK / (16 * baud_rate) = (integer_part) + (fractional_part)
    uint32_t div = debug_uart.clock_frequency / (16 * debug_uart.baud_rate);
    uint32_t remainder = debug_uart.clock_frequency % (16 * debug_uart.baud_rate);
    uint32_t fractional = (remainder * 64 + (16 * debug_uart.baud_rate) / 2) / (16 * debug_uart.baud_rate);
    
    // Set baud rate
    write_reg32(base + PL011_IBRD, div);
    write_reg32(base + PL011_FBRD, fractional);
    
    // Set line control - 8N1 (8 data bits, no parity, 1 stop bit)
    uint32_t lcr_h = PL011_LCR_H_WLEN8;  // 8-bit word length
    if (debug_uart.parity_enable) {
        lcr_h |= PL011_LCR_H_PEN;
        if (!debug_uart.parity_odd) {
            lcr_h |= PL011_LCR_H_EPS;  // Even parity
        }
    }
    if (debug_uart.stop_bits == 2) {
        lcr_h |= PL011_LCR_H_STP2;
    }
    lcr_h |= PL011_LCR_H_FEN;  // Enable FIFOs
    write_reg32(base + PL011_LCR_H, lcr_h);
    
    // Clear all interrupts
    write_reg32(base + PL011_ICR, 0x7FF);
    
    // Enable UART, TX, and RX
    write_reg32(base + PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
    
    return B_OK;
}

// Initialize 8250 UART
static status_t
uart_init_8250(void)
{
    uint64_t base = debug_uart.base_address;
    
    // Calculate divisor for baud rate
    uint32_t divisor = debug_uart.clock_frequency / (16 * debug_uart.baud_rate);
    
    // Set DLAB to access divisor registers
    write_reg8(base + UART_8250_LCR, UART_8250_LCR_DLAB);
    
    // Set divisor
    write_reg8(base + UART_8250_DLL, divisor & 0xFF);
    write_reg8(base + UART_8250_DLH, (divisor >> 8) & 0xFF);
    
    // Set line control - 8N1
    uint8_t lcr = UART_8250_LCR_WLEN8;  // 8-bit word length
    if (debug_uart.parity_enable) {
        lcr |= UART_8250_LCR_PARITY;
        if (!debug_uart.parity_odd) {
            lcr |= UART_8250_LCR_EPAR;  // Even parity
        }
    }
    if (debug_uart.stop_bits == 2) {
        lcr |= UART_8250_LCR_STOP;
    }
    write_reg8(base + UART_8250_LCR, lcr);
    
    // Enable and reset FIFOs
    write_reg8(base + UART_8250_FCR, 0x07);
    
    // Disable all interrupts
    write_reg8(base + UART_8250_IER, 0x00);
    
    return B_OK;
}

// Initialize BCM2835 Mini UART
static status_t
uart_init_bcm2835(void)
{
    uint64_t base = debug_uart.base_address;
    
    // Enable UART
    write_reg32(base + BCM2835_MU_CNTL, 0x03);  // Enable TX and RX
    
    // Set baud rate (BCM2835 uses a different formula)
    uint32_t baud_reg = (debug_uart.clock_frequency / (8 * debug_uart.baud_rate)) - 1;
    write_reg32(base + BCM2835_MU_BAUD, baud_reg);
    
    // Set 8-bit mode
    write_reg32(base + BCM2835_MU_LCR, 0x03);
    
    // Disable interrupts
    write_reg32(base + BCM2835_MU_IER, 0x00);
    
    return B_OK;
}

/*
 * Forward Declarations
 */
status_t arch_debug_uart_puts(const char* str);
status_t arch_debug_uart_printf(const char* format, ...);

/*
 * Public API Functions
 */

// Initialize early debug UART
status_t
arch_debug_uart_init(void)
{
    if (debug_uart.initialized) {
        return B_OK;
    }
    
    // Try auto-detection first
    if (uart_auto_detect() != B_OK) {
        return B_NOT_SUPPORTED;
    }
    
    status_t result = B_NOT_SUPPORTED;
    
    // Initialize based on detected type
    switch (debug_uart.type) {
        case UART_TYPE_PL011:
            result = uart_init_pl011();
            break;
            
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP:
            result = uart_init_8250();
            break;
            
        case UART_TYPE_BCM2835:
            result = uart_init_bcm2835();
            break;
            
        default:
            result = B_NOT_SUPPORTED;
            break;
    }
    
    if (result == B_OK) {
        debug_uart.initialized = true;
        
        // Send initialization message
        arch_debug_uart_puts("\r\n[ARM64 Early Debug UART Initialized]\r\n");
        arch_debug_uart_printf("UART Type: %s at 0x%llx\r\n", 
                              debug_uart.name, debug_uart.base_address);
        arch_debug_uart_printf("Baud: %u, Clock: %u Hz\r\n", 
                              debug_uart.baud_rate, debug_uart.clock_frequency);
    }
    
    return result;
}

// Initialize with specific UART configuration
status_t
arch_debug_uart_init_config(uart_type_t type, uint64_t base_address, 
                           uint32_t clock_freq, uint32_t baud_rate)
{
    if (debug_uart.initialized) {
        return B_OK;
    }
    
    debug_uart.type = type;
    debug_uart.base_address = base_address;
    debug_uart.clock_frequency = clock_freq;
    debug_uart.baud_rate = baud_rate;
    
    status_t result = B_NOT_SUPPORTED;
    
    switch (type) {
        case UART_TYPE_PL011:
            debug_uart.name = "PL011";
            result = uart_init_pl011();
            break;
            
        case UART_TYPE_8250:
            debug_uart.name = "8250";
            result = uart_init_8250();
            break;
            
        case UART_TYPE_8250_OMAP:
            debug_uart.name = "8250-OMAP";
            result = uart_init_8250();
            break;
            
        case UART_TYPE_BCM2835:
            debug_uart.name = "BCM2835-MiniUART";
            result = uart_init_bcm2835();
            break;
            
        default:
            return B_BAD_VALUE;
    }
    
    if (result == B_OK) {
        debug_uart.initialized = true;
    }
    
    return result;
}

// Check if debug UART is available and initialized
bool
arch_debug_uart_available(void)
{
    return debug_uart.initialized;
}

// Send a single character
status_t
arch_debug_uart_putchar(char c)
{
    if (c == '\n') {
        // Convert LF to CRLF for proper terminal display
        uart_putchar('\r');
    }
    
    return uart_putchar(c);
}

// Send a string
status_t
arch_debug_uart_puts(const char* str)
{
    if (!str) {
        return B_BAD_VALUE;
    }
    
    while (*str) {
        status_t result = arch_debug_uart_putchar(*str);
        if (result != B_OK) {
            return result;
        }
        str++;
    }
    
    return B_OK;
}

// Receive a character (non-blocking)
int
arch_debug_uart_getchar(void)
{
    return uart_getchar();
}

// Check if receive data is available
bool
arch_debug_uart_rx_ready(void)
{
    return uart_rx_ready();
}

// Simple printf-style formatting for early debugging
static char printf_buffer[512];

status_t
arch_debug_uart_printf(const char* format, ...)
{
    if (!format) {
        return B_BAD_VALUE;
    }
    
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    // Simple printf implementation for kernel use
    int pos = 0;
    const char* p = format;
    
    while (*p && pos < (int)(sizeof(printf_buffer) - 1)) {
        if (*p != '%') {
            printf_buffer[pos++] = *p++;
            continue;
        }
        
        p++;  // Skip '%'
        
        switch (*p) {
            case 'd': {
                int val = __builtin_va_arg(args, int);
                char num_buf[16];
                int num_pos = 0;
                bool negative = val < 0;
                
                if (negative) val = -val;
                
                do {
                    num_buf[num_pos++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);
                
                if (negative && pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = '-';
                }
                
                while (num_pos > 0 && pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = num_buf[--num_pos];
                }
                break;
            }
            
            case 'u': {
                unsigned int val = __builtin_va_arg(args, unsigned int);
                char num_buf[16];
                int num_pos = 0;
                
                do {
                    num_buf[num_pos++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);
                
                while (num_pos > 0 && pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = num_buf[--num_pos];
                }
                break;
            }
            
            case 'x':
            case 'X': {
                unsigned int val = __builtin_va_arg(args, unsigned int);
                char num_buf[16];
                int num_pos = 0;
                const char* hex_chars = (*p == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                
                do {
                    num_buf[num_pos++] = hex_chars[val & 0xF];
                    val >>= 4;
                } while (val > 0);
                
                while (num_pos > 0 && pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = num_buf[--num_pos];
                }
                break;
            }
            
            case 'l': {
                p++;  // Skip 'l'
                if (*p == 'l') {
                    p++;  // Skip second 'l' for 'lld' or 'llx'
                    if (*p == 'd') {
                        long long val = __builtin_va_arg(args, long long);
                        char num_buf[32];
                        int num_pos = 0;
                        bool negative = val < 0;
                        
                        if (negative) val = -val;
                        
                        do {
                            num_buf[num_pos++] = '0' + (val % 10);
                            val /= 10;
                        } while (val > 0);
                        
                        if (negative && pos < (int)(sizeof(printf_buffer) - 1)) {
                            printf_buffer[pos++] = '-';
                        }
                        
                        while (num_pos > 0 && pos < (int)(sizeof(printf_buffer) - 1)) {
                            printf_buffer[pos++] = num_buf[--num_pos];
                        }
                    } else if (*p == 'x' || *p == 'X') {
                        unsigned long long val = __builtin_va_arg(args, unsigned long long);
                        char num_buf[32];
                        int num_pos = 0;
                        const char* hex_chars = (*p == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                        
                        do {
                            num_buf[num_pos++] = hex_chars[val & 0xF];
                            val >>= 4;
                        } while (val > 0);
                        
                        while (num_pos > 0 && pos < (int)(sizeof(printf_buffer) - 1)) {
                            printf_buffer[pos++] = num_buf[--num_pos];
                        }
                    }
                }
                break;
            }
            
            case 's': {
                const char* str = __builtin_va_arg(args, const char*);
                if (str) {
                    while (*str && pos < (int)(sizeof(printf_buffer) - 1)) {
                        printf_buffer[pos++] = *str++;
                    }
                } else {
                    const char* null_str = "(null)";
                    while (*null_str && pos < (int)(sizeof(printf_buffer) - 1)) {
                        printf_buffer[pos++] = *null_str++;
                    }
                }
                break;
            }
            
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                if (pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = c;
                }
                break;
            }
            
            case '%': {
                if (pos < (int)(sizeof(printf_buffer) - 1)) {
                    printf_buffer[pos++] = '%';
                }
                break;
            }
            
            default:
                // Unknown format specifier, just copy it
                if (pos < (int)(sizeof(printf_buffer) - 2)) {
                    printf_buffer[pos++] = '%';
                    printf_buffer[pos++] = *p;
                }
                break;
        }
        
        if (*p) p++;
    }
    
    printf_buffer[pos] = '\0';
    
    __builtin_va_end(args);
    
    return arch_debug_uart_puts(printf_buffer);
}

// Get current UART configuration
status_t
arch_debug_uart_get_config(uart_type_t* type, uint64_t* base_address, 
                          uint32_t* clock_freq, uint32_t* baud_rate)
{
    if (!debug_uart.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (type) *type = debug_uart.type;
    if (base_address) *base_address = debug_uart.base_address;
    if (clock_freq) *clock_freq = debug_uart.clock_frequency;
    if (baud_rate) *baud_rate = debug_uart.baud_rate;
    
    return B_OK;
}

// Dump debug UART information
void
arch_debug_uart_dump_info(void)
{
    if (!debug_uart.initialized) {
        arch_debug_uart_puts("Debug UART: Not initialized\r\n");
        return;
    }
    
    arch_debug_uart_puts("ARM64 Debug UART Information:\r\n");
    arch_debug_uart_puts("============================\r\n");
    arch_debug_uart_printf("Type:        %s\r\n", debug_uart.name);
    arch_debug_uart_printf("Base:        0x%llX\r\n", debug_uart.base_address);
    arch_debug_uart_printf("Clock:       %u Hz\r\n", debug_uart.clock_frequency);
    arch_debug_uart_printf("Baud Rate:   %u\r\n", debug_uart.baud_rate);
    arch_debug_uart_printf("Data Bits:   %u\r\n", debug_uart.data_bits);
    arch_debug_uart_printf("Stop Bits:   %u\r\n", debug_uart.stop_bits);
    arch_debug_uart_printf("Parity:      %s\r\n", 
                          debug_uart.parity_enable ? 
                          (debug_uart.parity_odd ? "Odd" : "Even") : "None");
    arch_debug_uart_printf("Status:      %s\r\n", 
                          debug_uart.initialized ? "Initialized" : "Not Initialized");
    
    // Show hardware status
    switch (debug_uart.type) {
        case UART_TYPE_PL011: {
            uint32_t fr = read_reg32(debug_uart.base_address + PL011_FR);
            arch_debug_uart_printf("PL011 Flags: 0x%08X\r\n", fr);
            arch_debug_uart_printf("  TX Empty:  %s\r\n", (fr & PL011_FR_TXFE) ? "Yes" : "No");
            arch_debug_uart_printf("  RX Empty:  %s\r\n", (fr & PL011_FR_RXFE) ? "Yes" : "No");
            arch_debug_uart_printf("  UART Busy: %s\r\n", (fr & PL011_FR_BUSY) ? "Yes" : "No");
            break;
        }
        
        case UART_TYPE_8250:
        case UART_TYPE_8250_OMAP: {
            uint8_t lsr = read_reg8(debug_uart.base_address + UART_8250_LSR);
            arch_debug_uart_printf("8250 LSR:    0x%02X\r\n", lsr);
            arch_debug_uart_printf("  TX Empty:  %s\r\n", (lsr & UART_8250_LSR_TEMT) ? "Yes" : "No");
            arch_debug_uart_printf("  TX Ready:  %s\r\n", (lsr & UART_8250_LSR_THRE) ? "Yes" : "No");
            arch_debug_uart_printf("  RX Ready:  %s\r\n", (lsr & UART_8250_LSR_DR) ? "Yes" : "No");
            break;
        }
        
        case UART_TYPE_BCM2835: {
            uint32_t lsr = read_reg32(debug_uart.base_address + BCM2835_MU_LSR);
            arch_debug_uart_printf("BCM2835 LSR: 0x%08X\r\n", lsr);
            arch_debug_uart_printf("  TX Empty:  %s\r\n", (lsr & BCM2835_MU_LSR_TX_EMPTY) ? "Yes" : "No");
            arch_debug_uart_printf("  RX Ready:  %s\r\n", (lsr & BCM2835_MU_LSR_RX_READY) ? "Yes" : "No");
            break;
        }
        
        default:
            arch_debug_uart_puts("Hardware status: Unknown UART type\r\n");
            break;
    }
}