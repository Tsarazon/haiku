# ARM64 Generic Timer Architecture Documentation

## Overview

The ARM64 Generic Timer provides a standardized timing infrastructure for ARM-based systems, offering a system-wide timestamp counter that operates independently of CPU clock frequency. This architecture enables precise timing measurements that remain invariant across processor scaling, power states, and throttling conditions.

## System Counter Architecture

### System Counter Overview

The Generic Timer is built around a **System Counter** that provides a unified view of time across all system components:

- **Always-on operation**: The system counter operates in the always-on power domain
- **System-wide uniformity**: All agents (CPUs, devices, peripherals) see a consistent time reference
- **Hardware independence**: Counter frequency is independent of CPU clock frequency
- **Reset behavior**: Counter begins counting from 0 at system reset

### System Counter Properties

```
Base Architecture: ARMv8-A and later
Counter Width: 64-bit
Frequency Range: Implementation defined (typically 1MHz - 100MHz)
Power Domain: Always-on (does not stop during system suspend)
Access Control: Configurable per exception level
```

## Timer Register Architecture

### Core Timer Registers

The ARM64 Generic Timer provides several sets of registers for different use cases:

#### Physical Timer Registers

**CNTPCT_EL0 - Physical Counter Register**
- **Purpose**: Provides direct access to the system counter value
- **Width**: 64-bit read-only
- **Access**: Available from EL0 (user space) if enabled
- **Usage**: `uint64_t count = READ_SPECIALREG(CNTPCT_EL0);`

**CNTP_CTL_EL0 - Physical Timer Control Register**
- **Purpose**: Controls physical timer operation and status
- **Width**: 32-bit read/write
- **Fields**:
  - Bit [0] **ENABLE**: Timer enable (0=disabled, 1=enabled)
  - Bit [1] **IMASK**: Interrupt mask (0=not masked, 1=masked)
  - Bit [2] **ISTATUS**: Interrupt status (0=condition not met, 1=condition met)
- **Usage**: `WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);`

**CNTP_CVAL_EL0 - Physical Timer Compare Value Register**
- **Purpose**: Holds the 64-bit compare value for physical timer
- **Width**: 64-bit read/write
- **Operation**: Timer condition met when (CNTPCT_EL0 >= CNTP_CVAL_EL0)
- **Usage**: `WRITE_SPECIALREG(CNTP_CVAL_EL0, target_time);`

**CNTP_TVAL_EL0 - Physical Timer Value Register**
- **Purpose**: Provides countdown timer interface
- **Width**: 32-bit read/write
- **Operation**: Countdown timer that sets CNTP_CVAL = CNTPCT + CNTP_TVAL
- **Usage**: `WRITE_SPECIALREG(CNTP_TVAL_EL0, timeout_ticks);`

#### Virtual Timer Registers

**CNTVCT_EL0 - Virtual Counter Register**
- **Purpose**: Virtual counter = Physical counter - Virtual offset
- **Width**: 64-bit read-only
- **Relationship**: CNTVCT_EL0 = CNTPCT_EL0 - CNTVOFF_EL2
- **Usage**: For virtualized environments and guest operating systems

**CNTV_CTL_EL0 - Virtual Timer Control Register**
- **Purpose**: Controls virtual timer operation (similar to CNTP_CTL_EL0)
- **Width**: 32-bit read/write
- **Fields**: Same as CNTP_CTL_EL0 (ENABLE, IMASK, ISTATUS)

**CNTV_CVAL_EL0 - Virtual Timer Compare Value Register**
- **Purpose**: Compare value for virtual timer
- **Width**: 64-bit read/write
- **Operation**: Timer condition met when (CNTVCT_EL0 >= CNTV_CVAL_EL0)

**CNTV_TVAL_EL0 - Virtual Timer Value Register**
- **Purpose**: Countdown interface for virtual timer
- **Width**: 32-bit read/write
- **Operation**: Sets CNTV_CVAL = CNTVCT + CNTV_TVAL

#### Frequency and Configuration Registers

**CNTFRQ_EL0 - Counter Frequency Register**
- **Purpose**: Reports system counter frequency in Hz
- **Width**: 32-bit read-only (EL0), read/write (EL1+)
- **Initialization**: Must be programmed by firmware/bootloader
- **Usage**: `uint32_t freq = READ_SPECIALREG(CNTFRQ_EL0);`

### Register Access Control

Timer register access is controlled by several mechanisms:

**CNTKCTL_EL1 - Counter-Timer Kernel Control Register**
- Controls EL0 access to timer and counter registers
- **EL0PTEN**: EL0 access to physical timer registers
- **EL0VTEN**: EL0 access to virtual timer registers
- **EL0PCTEN**: EL0 access to physical counter (CNTPCT_EL0)
- **EL0VCTEN**: EL0 access to virtual counter (CNTVCT_EL0)

**CNTHCTL_EL2 - Counter-Timer Hypervisor Control Register**
- Controls EL1 access to physical timer registers
- **EL1PCEN**: EL1 access to physical timer
- **EL1PCTEN**: EL1 access to physical counter

## Timer Programming Model

### Basic Timer Operation

The ARM64 Generic Timer supports two primary programming models:

#### 1. Compare Value Model (CVAL)

```c
// Set absolute target time
uint64_t current_time = READ_SPECIALREG(CNTPCT_EL0);
uint64_t target_time = current_time + (timeout_us * timer_frequency / 1000000);

// Program compare value
WRITE_SPECIALREG(CNTP_CVAL_EL0, target_time);

// Enable timer
WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);
```

#### 2. Timer Value Model (TVAL)

```c
// Set relative timeout (countdown)
uint32_t timeout_ticks = timeout_us * timer_frequency / 1000000;

// Program timer value (automatically sets CVAL)
WRITE_SPECIALREG(CNTP_TVAL_EL0, timeout_ticks);

// Enable timer
WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);
```

### Timer States and Control

**Timer Enable/Disable**
```c
#define TIMER_DISABLED  (0)
#define TIMER_ENABLE    (1)
#define TIMER_IMASK     (2)
#define TIMER_ISTATUS   (4)

// Enable timer
WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);

// Disable timer
WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_DISABLED);

// Enable with interrupt masked
WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE | TIMER_IMASK);
```

**Timer Status Checking**
```c
uint32_t status = READ_SPECIALREG(CNTP_CTL_EL0);
bool timer_fired = (status & TIMER_ISTATUS) != 0;
bool timer_enabled = (status & TIMER_ENABLE) != 0;
bool interrupt_masked = (status & TIMER_IMASK) != 0;
```

### Current Haiku Implementation

The existing Haiku ARM64 timer implementation (`arch_timer.cpp`) demonstrates the basic programming model:

```c
// Initialize timer system
int arch_init_timer(kernel_args *args)
{
    // Get timer frequency from hardware
    sTimerTicksUS = READ_SPECIALREG(CNTFRQ_EL0) / 1000000;
    sTimerMaxInterval = INT32_MAX / sTimerTicksUS;
    
    // Disable timer initially
    WRITE_SPECIALREG(CNTV_CTL_EL0, TIMER_DISABLED);
    
    // Install interrupt handler (IRQ 27 = Virtual Timer)
    install_io_interrupt_handler(TIMER_IRQ, &arch_timer_interrupt, NULL, 0);
    
    // Record boot time for system_time() calculation
    sBootTime = READ_SPECIALREG(CNTPCT_EL0);
    
    return B_OK;
}

// Set hardware timer for timeout
void arch_timer_set_hardware_timer(bigtime_t timeout)
{
    if (timeout > sTimerMaxInterval)
        timeout = sTimerMaxInterval;
    
    // Use TVAL countdown model
    WRITE_SPECIALREG(CNTV_TVAL_EL0, timeout * sTimerTicksUS);
    WRITE_SPECIALREG(CNTV_CTL_EL0, TIMER_ENABLE);
}

// Handle timer interrupt
int32 arch_timer_interrupt(void *data)
{
    // Disable timer to clear interrupt
    WRITE_SPECIALREG(CNTV_CTL_EL0, TIMER_DISABLED);
    return timer_interrupt();
}
```

## Interrupt Generation

### Timer Interrupt Characteristics

The ARM64 Generic Timer generates **level-sensitive interrupts**:

- **Persistent signaling**: Interrupt remains asserted until condition is cleared
- **Condition clearing**: Interrupt clears when timer is disabled or new target set
- **Interrupt types**: Can be configured as IRQ or FIQ depending on GIC setup

### Interrupt Types per Timer

Each timer type has dedicated interrupt lines:

**Physical Timer Interrupts:**
- **Secure Physical Timer**: Typically PPI 29
- **Non-Secure Physical Timer**: Typically PPI 30

**Virtual Timer Interrupts:**
- **Virtual Timer**: Typically PPI 27 (used by Haiku)
- **Hypervisor Timer**: Typically PPI 26

### Device Tree Interrupt Configuration

```devicetree
timer {
    compatible = "arm,armv8-timer";
    interrupts = <GIC_PPI 13 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,  // Virtual
                 <GIC_PPI 14 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,  // Physical
                 <GIC_PPI 11 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,  // Hypervisor
                 <GIC_PPI 10 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>;  // Secure Physical
    interrupt-names = "virt", "phys", "hyp-phys", "hyp-virt";
    arm,cpu-registers-not-fw-configured;
};
```

### GIC Integration

Timer interrupts are delivered through the ARM Generic Interrupt Controller (GIC):

**Private Peripheral Interrupts (PPIs):**
- Each CPU core has its own timer interrupt
- Timer interrupts are automatically routed to the originating core
- No explicit CPU targeting required

**Interrupt Handling Flow:**
1. Timer condition met (counter >= compare value)
2. Timer sets ISTATUS bit and asserts interrupt (if IMASK=0)
3. GIC delivers PPI to local CPU core
4. CPU takes exception and vectors to interrupt handler
5. Handler disables timer or sets new target to clear interrupt

## Frequency Detection and Calibration

### Frequency Detection Methods

#### 1. Hardware Register Method (Primary)

```c
uint32_t timer_frequency = READ_SPECIALREG(CNTFRQ_EL0);
```

**Advantages:**
- Direct hardware reporting
- No calibration required
- Immediate availability

**Limitations:**
- May be uninitialized on some systems
- Requires proper firmware/bootloader setup
- Some implementations report incorrect values

#### 2. Device Tree Method (Fallback)

```devicetree
timer {
    compatible = "arm,armv8-timer";
    clock-frequency = <24000000>;  // 24 MHz
};
```

**Usage in software:**
```c
// Parse device tree for timer-frequency property
fdt_node timer_node = fdt_find_compatible_node("arm,armv8-timer");
uint32_t dt_frequency = fdt_get_property_u32(timer_node, "clock-frequency");
```

#### 3. Calibration Method (Last Resort)

```c
uint64_t calibrate_timer_frequency(void)
{
    // Use known reference timer (e.g., RTC, platform timer)
    uint64_t start_counter = READ_SPECIALREG(CNTPCT_EL0);
    uint64_t start_reference = read_reference_timer_ms();
    
    // Wait for known interval (e.g., 100ms)
    delay_ms(100);
    
    uint64_t end_counter = READ_SPECIALREG(CNTPCT_EL0);
    uint64_t end_reference = read_reference_timer_ms();
    
    uint64_t counter_ticks = end_counter - start_counter;
    uint64_t reference_ms = end_reference - start_reference;
    
    return (counter_ticks * 1000) / reference_ms;  // Hz
}
```

### Frequency Validation

```c
bool validate_timer_frequency(uint32_t frequency)
{
    // Typical ranges for ARM64 systems
    const uint32_t MIN_FREQ = 1000000;     // 1 MHz
    const uint32_t MAX_FREQ = 100000000;   // 100 MHz
    
    if (frequency < MIN_FREQ || frequency > MAX_FREQ) {
        return false;
    }
    
    // Additional sanity checks
    if (frequency == 0 || frequency == 0xFFFFFFFF) {
        return false;
    }
    
    return true;
}
```

### Frequency Detection Hierarchy

```c
uint32_t detect_timer_frequency(void)
{
    uint32_t frequency = 0;
    
    // 1. Try hardware register
    frequency = READ_SPECIALREG(CNTFRQ_EL0);
    if (validate_timer_frequency(frequency)) {
        dprintf("Timer frequency from CNTFRQ_EL0: %u Hz\n", frequency);
        return frequency;
    }
    
    // 2. Try device tree
    frequency = get_device_tree_timer_frequency();
    if (validate_timer_frequency(frequency)) {
        dprintf("Timer frequency from device tree: %u Hz\n", frequency);
        return frequency;
    }
    
    // 3. Try calibration
    frequency = calibrate_timer_frequency();
    if (validate_timer_frequency(frequency)) {
        dprintf("Timer frequency from calibration: %u Hz\n", frequency);
        return frequency;
    }
    
    // 4. Use conservative default
    frequency = 24000000;  // 24 MHz common default
    dprintf("Using default timer frequency: %u Hz\n", frequency);
    return frequency;
}
```

## Advanced Features

### Virtualization Support

The Generic Timer provides native virtualization support:

**Virtual Offset (CNTVOFF_EL2):**
- Hypervisor can adjust guest's view of time
- Virtual counter = Physical counter - Virtual offset
- Enables time migration and guest isolation

**Access Control:**
- Hypervisor controls guest access to physical timers
- Virtual timers provide safe guest timing interface
- Trapping mechanism for unauthorized access

### Power Management Integration

**System Suspend Considerations:**
```c
// Save timer state before suspend
struct timer_state {
    uint64_t cval;
    uint32_t ctl;
    bool was_enabled;
};

void save_timer_state(struct timer_state *state)
{
    state->ctl = READ_SPECIALREG(CNTP_CTL_EL0);
    state->was_enabled = (state->ctl & TIMER_ENABLE) != 0;
    
    if (state->was_enabled) {
        state->cval = READ_SPECIALREG(CNTP_CVAL_EL0);
        WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_DISABLED);
    }
}

void restore_timer_state(struct timer_state *state)
{
    if (state->was_enabled) {
        WRITE_SPECIALREG(CNTP_CVAL_EL0, state->cval);
        WRITE_SPECIALREG(CNTP_CTL_EL0, state->ctl);
    }
}
```

### Event Stream Generation

The Generic Timer can generate periodic events for power management:

**CNTHCTL_EL2 Event Stream Control:**
- **EVNTEN**: Enable event stream
- **EVNTI**: Select counter bit for event generation
- **EVNTDIR**: Control transition direction (0→1 or 1→0)

```c
// Configure 10ms event stream (assuming 1MHz counter)
// Bit 10 toggles every 1024 cycles = ~1ms
uint32_t cnthctl = READ_SPECIALREG(CNTHCTL_EL2);
cnthctl |= CNTHCTL_EVNTEN | (10 << 4);  // Enable, bit 10
WRITE_SPECIALREG(CNTHCTL_EL2, cnthctl);
```

## Performance Characteristics

### Timing Accuracy

**Resolution:**
- Limited by counter frequency (typically 1-100 MHz)
- 1 MHz = 1 microsecond resolution
- 24 MHz = ~42 nanosecond resolution

**Latency:**
- Register reads: ~1-2 CPU cycles
- Timer setup: ~5-10 CPU cycles
- Interrupt delivery: ~100-1000 CPU cycles (GIC dependent)

### Best Practices

**Efficient Time Reading:**
```c
// Avoid division in hot paths
static uint64_t timer_freq_mhz;
static uint32_t timer_freq_khz;

void init_timer_constants(void)
{
    uint32_t freq = READ_SPECIALREG(CNTFRQ_EL0);
    timer_freq_mhz = freq / 1000000;
    timer_freq_khz = freq / 1000;
}

// Fast microsecond conversion
uint64_t get_time_us(void)
{
    return READ_SPECIALREG(CNTPCT_EL0) / timer_freq_mhz;
}

// Fast millisecond conversion
uint64_t get_time_ms(void)
{
    return READ_SPECIALREG(CNTPCT_EL0) / timer_freq_khz;
}
```

**Timer Programming Optimization:**
```c
// Prefer TVAL for short relative timeouts
void set_short_timeout(uint32_t us)
{
    uint32_t ticks = us * timer_freq_mhz;
    WRITE_SPECIALREG(CNTP_TVAL_EL0, ticks);
    WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);
}

// Use CVAL for absolute or long timeouts
void set_absolute_timeout(uint64_t target_time)
{
    WRITE_SPECIALREG(CNTP_CVAL_EL0, target_time);
    WRITE_SPECIALREG(CNTP_CTL_EL0, TIMER_ENABLE);
}
```

## Debugging and Diagnostics

### Timer State Inspection

```c
void dump_timer_state(void)
{
    uint64_t pct = READ_SPECIALREG(CNTPCT_EL0);
    uint64_t vct = READ_SPECIALREG(CNTVCT_EL0);
    uint32_t freq = READ_SPECIALREG(CNTFRQ_EL0);
    
    uint32_t pctl = READ_SPECIALREG(CNTP_CTL_EL0);
    uint64_t pcval = READ_SPECIALREG(CNTP_CVAL_EL0);
    uint32_t ptval = READ_SPECIALREG(CNTP_TVAL_EL0);
    
    uint32_t vctl = READ_SPECIALREG(CNTV_CTL_EL0);
    uint64_t vcval = READ_SPECIALREG(CNTV_CVAL_EL0);
    uint32_t vtval = READ_SPECIALREG(CNTV_TVAL_EL0);
    
    dprintf("Timer State Dump:\n");
    dprintf("  Frequency: %u Hz\n", freq);
    dprintf("  Physical Counter: 0x%016llx\n", pct);
    dprintf("  Virtual Counter:  0x%016llx\n", vct);
    dprintf("  Virtual Offset:   0x%016llx\n", pct - vct);
    
    dprintf("  Physical Timer:\n");
    dprintf("    Control: 0x%08x (EN=%d MASK=%d STATUS=%d)\n", 
            pctl, !!(pctl & 1), !!(pctl & 2), !!(pctl & 4));
    dprintf("    Compare: 0x%016llx\n", pcval);
    dprintf("    Value:   0x%08x (%d)\n", ptval, (int32_t)ptval);
    
    dprintf("  Virtual Timer:\n");
    dprintf("    Control: 0x%08x (EN=%d MASK=%d STATUS=%d)\n", 
            vctl, !!(vctl & 1), !!(vctl & 2), !!(vctl & 4));
    dprintf("    Compare: 0x%016llx\n", vcval);
    dprintf("    Value:   0x%08x (%d)\n", vtval, (int32_t)vtval);
}
```

### Common Issues and Solutions

**Problem: Timer not firing**
```c
// Check timer enable status
uint32_t ctl = READ_SPECIALREG(CNTP_CTL_EL0);
if (!(ctl & TIMER_ENABLE)) {
    dprintf("Timer not enabled\n");
}

// Check interrupt mask
if (ctl & TIMER_IMASK) {
    dprintf("Timer interrupt masked\n");
}

// Check if condition already met
if (ctl & TIMER_ISTATUS) {
    dprintf("Timer condition already met\n");
}

// Check compare value
uint64_t counter = READ_SPECIALREG(CNTPCT_EL0);
uint64_t compare = READ_SPECIALREG(CNTP_CVAL_EL0);
if (counter >= compare) {
    dprintf("Timer condition should be met: counter=0x%llx compare=0x%llx\n", 
            counter, compare);
}
```

**Problem: Incorrect timing**
```c
// Verify frequency setting
uint32_t freq = READ_SPECIALREG(CNTFRQ_EL0);
if (freq == 0 || freq == 0xFFFFFFFF) {
    dprintf("Invalid timer frequency: 0x%08x\n", freq);
}

// Check for frequency mismatch
uint64_t start = READ_SPECIALREG(CNTPCT_EL0);
platform_delay_ms(1000);  // 1 second delay
uint64_t end = READ_SPECIALREG(CNTPCT_EL0);
uint64_t measured_freq = end - start;

if (abs((int64_t)measured_freq - (int64_t)freq) > freq / 100) {  // >1% error
    dprintf("Timer frequency mismatch: reported=%u measured=%llu\n", 
            freq, measured_freq);
}
```

## References

- ARM Architecture Reference Manual ARMv8-A
- ARM Generic Timer Architecture Specification
- ARM GIC Architecture Specification
- Haiku ARM64 Timer Implementation (`src/system/kernel/arch/arm64/arch_timer.cpp`)
- Device Tree Bindings for ARM Architectural Timer

---

*This document provides comprehensive coverage of the ARM64 Generic Timer architecture for Haiku OS development and system-level programming.*