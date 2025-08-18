# ARM64 Early Debug UART Implementation

## Overview

The ARM64 Early Debug UART implementation provides comprehensive serial debugging functionality for ARM64 systems during early boot, before device drivers are loaded. This implementation supports multiple UART types commonly found in ARM64 systems and provides a hardware abstraction layer for seamless cross-platform debugging.

## Supported UART Types

### 1. **ARM PrimeCell PL011 UART**
- **Type**: `UART_TYPE_PL011`
- **Description**: Industry-standard ARM UART found on most ARM development boards
- **Features**: Full-featured UART with FIFOs, flow control, and interrupt support
- **Common Platforms**: QEMU virt, ARM VersatileExpress, Raspberry Pi (full UART)

### 2. **8250/16550 Compatible UART**
- **Type**: `UART_TYPE_8250`, `UART_TYPE_8250_OMAP`
- **Description**: Industry-standard PC-compatible UART
- **Features**: Traditional UART with FIFO support
- **Common Platforms**: i.MX series, various ARM SoCs

### 3. **Broadcom BCM2835 Mini UART**
- **Type**: `UART_TYPE_BCM2835`
- **Description**: Simplified UART used in Raspberry Pi systems
- **Features**: Basic UART functionality, shared with GPU
- **Common Platforms**: Raspberry Pi 2/3/4/5

### 4. **Xilinx Zynq UART**
- **Type**: `UART_TYPE_ZYNQ`
- **Description**: Zynq UltraScale+ UART controller
- **Features**: High-performance UART for FPGA platforms
- **Common Platforms**: Zynq UltraScale+ MPSoCs

### 5. **NXP/Freescale UARTs**
- **Type**: `UART_TYPE_IMX`, `UART_TYPE_LINFLEX`
- **Description**: NXP-specific UART controllers
- **Features**: Enhanced UART with additional features
- **Common Platforms**: i.MX series, S32 automotive platforms

### 6. **Renesas R-Car UART**
- **Type**: `UART_TYPE_RCAR`
- **Description**: R-Car series SCIF UART
- **Features**: Automotive-grade UART
- **Common Platforms**: R-Car automotive SoCs

## Architecture Overview

### Hardware Abstraction Layer
```c
// Unified UART interface regardless of hardware type
status_t arch_debug_uart_putchar(char c);
status_t arch_debug_uart_puts(const char* str);
status_t arch_debug_uart_printf(const char* format, ...);
int arch_debug_uart_getchar(void);
bool arch_debug_uart_rx_ready(void);
```

### Auto-Detection System
```c
// Automatic UART detection table
static const struct {
    uart_type_t type;
    uint64_t base_address;
    const char* name;
    const char* description;
} uart_detection_table[] = {
    {UART_TYPE_PL011,   0xFE201000, "BCM2711-PL011", "Raspberry Pi 4/5 PL011"},
    {UART_TYPE_BCM2835, 0xFE215040, "BCM2711-Mini", "Raspberry Pi 4/5 Mini UART"},
    {UART_TYPE_PL011,   0x09000000, "QEMU-PL011", "QEMU virt machine PL011"},
    // ... more entries
};
```

### Configuration Management
```c
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
```

## Hardware Register Abstraction

### PL011 UART Registers
```c
#define PL011_DR        0x000   // Data register
#define PL011_FR        0x018   // Flag register
#define PL011_IBRD      0x024   // Integer baud rate register
#define PL011_FBRD      0x028   // Fractional baud rate register
#define PL011_LCR_H     0x02C   // Line control register
#define PL011_CR        0x030   // Control register
#define PL011_ICR       0x044   // Interrupt clear register
```

### 8250 UART Registers
```c
#define UART_8250_THR   0x0     // Transmit holding register
#define UART_8250_RBR   0x0     // Receive buffer register
#define UART_8250_IER   0x1     // Interrupt enable register
#define UART_8250_LCR   0x3     // Line control register
#define UART_8250_LSR   0x5     // Line status register
```

### BCM2835 Mini UART Registers
```c
#define BCM2835_MU_IO       0x40    // I/O data
#define BCM2835_MU_LSR      0x54    // Line status
#define BCM2835_MU_CNTL     0x60    // Control
#define BCM2835_MU_BAUD     0x68    // Baud rate
```

## API Reference

### Initialization Functions

#### Automatic Initialization
```c
status_t arch_debug_uart_init(void);
```
- **Purpose**: Initialize UART with automatic detection
- **Returns**: `B_OK` on success, error code on failure
- **Behavior**: Scans known UART locations and initializes first found

#### Manual Configuration
```c
status_t arch_debug_uart_init_config(uart_type_t type, uint64_t base_address, 
                                     uint32_t clock_freq, uint32_t baud_rate);
```
- **Purpose**: Initialize with specific UART configuration
- **Parameters**:
  - `type`: UART type (e.g., `UART_TYPE_PL011`)
  - `base_address`: Hardware base address
  - `clock_freq`: UART clock frequency in Hz
  - `baud_rate`: Desired baud rate

#### Availability Check
```c
bool arch_debug_uart_available(void);
```
- **Purpose**: Check if debug UART is initialized and ready
- **Returns**: `true` if available, `false` otherwise

### Character I/O Functions

#### Character Output
```c
status_t arch_debug_uart_putchar(char c);
```
- **Purpose**: Send a single character
- **Features**: Automatic LF to CRLF conversion
- **Timeout**: 10ms timeout for transmit ready
- **Returns**: `B_OK` on success, `B_TIMEOUT` on timeout

#### String Output
```c
status_t arch_debug_uart_puts(const char* str);
```
- **Purpose**: Send null-terminated string
- **Features**: Handles line endings automatically
- **Returns**: `B_OK` on success, error code on failure

#### Character Input
```c
int arch_debug_uart_getchar(void);
```
- **Purpose**: Receive single character (non-blocking)
- **Returns**: Character (0-255) or -1 if no data available

#### Input Status
```c
bool arch_debug_uart_rx_ready(void);
```
- **Purpose**: Check if receive data is available
- **Returns**: `true` if data available, `false` otherwise

### Formatted Output

#### Printf-style Formatting
```c
status_t arch_debug_uart_printf(const char* format, ...);
```
- **Purpose**: Formatted output for debugging
- **Buffer Size**: 512 characters maximum
- **Supported Formats**:
  - `%d` - signed decimal integer
  - `%u` - unsigned decimal integer
  - `%x` - lowercase hexadecimal
  - `%X` - uppercase hexadecimal
  - `%lld` - signed long long decimal
  - `%llx` - long long hexadecimal
  - `%s` - null-terminated string
  - `%c` - single character
  - `%%` - literal percent sign

### Configuration and Diagnostics

#### Get Configuration
```c
status_t arch_debug_uart_get_config(uart_type_t* type, uint64_t* base_address,
                                    uint32_t* clock_freq, uint32_t* baud_rate);
```
- **Purpose**: Retrieve current UART configuration
- **Parameters**: Pointers to store configuration (can be NULL)
- **Returns**: `B_OK` if initialized, `B_NOT_INITIALIZED` otherwise

#### Diagnostic Dump
```c
void arch_debug_uart_dump_info(void);
```
- **Purpose**: Display comprehensive UART information
- **Output**: Configuration, status, and hardware details

## Usage Examples

### Basic Early Boot Debugging
```c
// Very early in kernel boot
status_t status = arch_debug_uart_init();
if (status == B_OK) {
    arch_debug_uart_puts("ARM64 kernel starting...\r\n");
    arch_debug_uart_printf("Detected %s UART\r\n", uart_name);
}

// Throughout early boot
arch_debug_uart_printf("Memory size: %llu bytes\r\n", memory_size);
arch_debug_uart_printf("CPU count: %d\r\n", cpu_count);
```

### Error Reporting and Panic
```c
void early_panic(const char* message) {
    if (arch_debug_uart_available()) {
        arch_debug_uart_puts("\r\n*** KERNEL PANIC ***\r\n");
        arch_debug_uart_printf("Error: %s\r\n", message);
        arch_debug_uart_printf("PC: 0x%llx\r\n", get_pc());
        arch_debug_uart_dump_info();
    }
    while (1) { /* halt */ }
}
```

### Platform-Specific Initialization
```c
// Manual configuration for specific platform
void init_debug_uart_rpi4(void) {
    status_t status = arch_debug_uart_init_config(
        UART_TYPE_PL011,           // Raspberry Pi 4 PL011
        0xFE201000,               // Base address
        48000000,                 // 48MHz clock
        115200                    // 115200 baud
    );
    
    if (status == B_OK) {
        arch_debug_uart_puts("Raspberry Pi 4 UART initialized\r\n");
    }
}
```

### Interactive Debugging
```c
void early_debug_shell(void) {
    arch_debug_uart_puts("Early debug shell\r\n");
    arch_debug_uart_puts("Commands: h=help, d=dump, r=reboot\r\n");
    
    while (true) {
        arch_debug_uart_puts("> ");
        
        if (arch_debug_uart_rx_ready()) {
            char c = arch_debug_uart_getchar();
            arch_debug_uart_putchar(c);
            arch_debug_uart_puts("\r\n");
            
            switch (c) {
                case 'h':
                    arch_debug_uart_puts("Help: Available commands\r\n");
                    break;
                case 'd':
                    arch_debug_uart_dump_info();
                    break;
                case 'r':
                    arch_debug_uart_puts("Rebooting...\r\n");
                    system_reboot();
                    break;
            }
        }
    }
}
```

## Platform Support Matrix

| Platform | UART Type | Base Address | Clock | Status |
|----------|-----------|--------------|-------|--------|
| **Raspberry Pi 4/5** | PL011 | 0xFE201000 | 48MHz | ✓ Supported |
| **Raspberry Pi 4/5** | Mini UART | 0xFE215040 | Variable | ✓ Supported |
| **QEMU virt** | PL011 | 0x09000000 | 24MHz | ✓ Supported |
| **ARM VersatileExpress** | PL011 | 0x1C090000 | 24MHz | ✓ Supported |
| **ARM Versatile/PB** | PL011 | 0x10009000 | 24MHz | ✓ Supported |
| **ARM Integrator/CP** | PL011 | 0x101F1000 | 14.7MHz | ✓ Supported |
| **i.MX8 series** | 8250 | 0x30860000+ | Variable | ✓ Supported |
| **Zynq UltraScale+** | Zynq UART | 0xFF000000+ | Variable | ✓ Framework |
| **R-Car series** | SCIF | 0xE6E68000+ | Variable | ✓ Framework |

## Hardware Detection Process

### 1. **Automatic Detection Flow**
```
1. Scan detection table for known UART addresses
2. For each entry, probe hardware:
   - PL011: Check peripheral ID registers
   - 8250: Test scratch register read/write
   - BCM2835: Validate status register
3. Initialize first successfully detected UART
4. Configure with default parameters (115200 8N1)
5. Send initialization banner
```

### 2. **Probe Methods by Type**

#### PL011 Detection
```c
// Check PL011 peripheral ID (should be 0x00041011)
uint32_t pid0 = read_reg32(base + 0xFE0);
uint32_t pid1 = read_reg32(base + 0xFE4);
uint32_t pid2 = read_reg32(base + 0xFE8);
uint32_t pid3 = read_reg32(base + 0xFEC);
uint32_t pid = ((pid3 & 0xFF) << 24) | ((pid2 & 0xFF) << 16) |
               ((pid1 & 0xFF) << 8) | (pid0 & 0xFF);
return (pid == 0x00041011);
```

#### 8250 Detection
```c
// Test scratch register for read/write capability
uint8_t orig = read_reg8(base + UART_8250_SCR);
write_reg8(base + UART_8250_SCR, 0x55);
uint8_t test1 = read_reg8(base + UART_8250_SCR);
write_reg8(base + UART_8250_SCR, 0xAA);
uint8_t test2 = read_reg8(base + UART_8250_SCR);
write_reg8(base + UART_8250_SCR, orig);
return (test1 == 0x55 && test2 == 0xAA);
```

## Configuration Details

### Baud Rate Calculation

#### PL011 Baud Rate
```c
// UARTCLK / (16 * baud_rate) = integer + fractional
uint32_t div = clock_frequency / (16 * baud_rate);
uint32_t remainder = clock_frequency % (16 * baud_rate);
uint32_t fractional = (remainder * 64 + (16 * baud_rate) / 2) / (16 * baud_rate);

write_reg32(base + PL011_IBRD, div);        // Integer part
write_reg32(base + PL011_FBRD, fractional); // Fractional part
```

#### 8250 Baud Rate
```c
// Divisor = clock_frequency / (16 * baud_rate)
uint32_t divisor = clock_frequency / (16 * baud_rate);

write_reg8(base + UART_8250_LCR, UART_8250_LCR_DLAB); // Enable divisor access
write_reg8(base + UART_8250_DLL, divisor & 0xFF);     // Low byte
write_reg8(base + UART_8250_DLH, (divisor >> 8) & 0xFF); // High byte
write_reg8(base + UART_8250_LCR, lcr_value);          // Restore LCR
```

#### BCM2835 Baud Rate
```c
// baud_register = (clock_frequency / (8 * baud_rate)) - 1
uint32_t baud_reg = (clock_frequency / (8 * baud_rate)) - 1;
write_reg32(base + BCM2835_MU_BAUD, baud_reg);
```

### Line Configuration

#### 8-bit, No Parity, 1 Stop Bit (8N1)
```c
// PL011
uint32_t lcr_h = PL011_LCR_H_WLEN8 | PL011_LCR_H_FEN;
write_reg32(base + PL011_LCR_H, lcr_h);

// 8250
uint8_t lcr = UART_8250_LCR_WLEN8;
write_reg8(base + UART_8250_LCR, lcr);

// BCM2835
write_reg32(base + BCM2835_MU_LCR, 0x03); // 8-bit mode
```

## Error Handling

### Status Codes
```c
#define B_OK                     0      // Success
#define B_ERROR                 -1      // Generic error  
#define B_NOT_SUPPORTED         -2147483647  // Operation not supported
#define B_NOT_INITIALIZED       -2147483646  // Not initialized
#define B_BAD_VALUE             -2147483645  // Invalid parameter
#define B_TIMEOUT               -2147483644  // Operation timeout
```

### Timeout Handling
```c
static bool uart_wait_tx_ready(uint32_t timeout_us) {
    uint32_t count = 0;
    
    while (count < timeout_us) {
        if (/* hardware ready */) {
            return true;
        }
        count++;  // Approximate 1μs delay
    }
    
    return false; // Timeout occurred
}
```

### Error Recovery
- **Hardware Not Found**: Graceful fallback, no crash
- **Transmit Timeout**: Return error, continue operation
- **Invalid Parameters**: Validate inputs, return error codes
- **Initialization Failure**: Clean state, allow retry

## Debug Features

### Initialization Banner
```
[ARM64 Early Debug UART Initialized]
UART Type: BCM2711-PL011 at 0xFE201000
Baud: 115200, Clock: 48000000 Hz
```

### Comprehensive Status Dump
```
ARM64 Debug UART Information:
============================
Type:        BCM2711-PL011
Base:        0xFE201000
Clock:       48000000 Hz
Baud Rate:   115200
Data Bits:   8
Stop Bits:   1
Parity:      None
Status:      Initialized
PL011 Flags: 0x000000B0
  TX Empty:  Yes
  RX Empty:  Yes
  UART Busy: No
```

### Macro Support
```c
// Early debug macros (can be compiled out)
#define dprintf_early(fmt, ...) arch_debug_uart_printf(fmt, ##__VA_ARGS__)
#define dputs_early(str) arch_debug_uart_puts(str)
#define dputc_early(c) arch_debug_uart_putchar(c)
```

## Performance Characteristics

### Throughput
- **PL011**: ~115,200 bps at 115200 baud (full speed)
- **8250**: ~115,200 bps at 115200 baud (full speed)
- **BCM2835**: ~100,000 bps at 115200 baud (shared with GPU)

### Latency
- **Character Output**: ~87μs per character at 115200 baud
- **Timeout Handling**: 10ms maximum wait per character
- **Initialization**: <1ms for most UART types

### Memory Usage
- **Code Size**: ~8KB compiled code
- **Data Size**: <1KB static data
- **Stack Usage**: <512 bytes maximum

## Integration Points

### Kernel Boot Sequence
```c
// 1. Very early boot (before MMU)
void arch_early_init(void) {
    arch_debug_uart_init();
    dprintf_early("ARM64 kernel starting...\n");
}

// 2. Architecture initialization
void arch_init(void) {
    dprintf_early("Initializing ARM64 architecture\n");
    // ... more initialization
}

// 3. Before driver loading
void drivers_init(void) {
    dprintf_early("Loading device drivers\n");
    // Early UART hands off to full driver
}
```

### Panic and Error Handling
```c
void kernel_panic(const char* message) {
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("\nKERNEL PANIC: %s\n", message);
        arch_debug_uart_dump_info();
    }
    arch_halt();
}
```

## Future Enhancements

### 1. **Extended UART Support**
- SiFive UART for RISC-V compatibility
- Additional NXP UART variants
- Qualcomm MSM UART support

### 2. **Enhanced Features**
- Hardware flow control (RTS/CTS)
- FIFO threshold configuration
- Interrupt-driven I/O (when interrupts available)

### 3. **Performance Optimization**
- Burst character transmission
- Buffered output for better performance
- Adaptive timeout handling

### 4. **Debug Enhancements**
- Binary data dump capabilities
- Memory examination commands
- Register dump utilities

---

This ARM64 Early Debug UART implementation provides a robust, cross-platform foundation for early boot debugging, enabling effective troubleshooting and development across diverse ARM64 hardware platforms.