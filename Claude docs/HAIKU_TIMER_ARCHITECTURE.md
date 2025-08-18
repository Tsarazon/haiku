# Haiku Timer Architecture Documentation

## Overview

Haiku's timer system provides a comprehensive framework for time-based operations including periodic timers, one-shot timers, system timing, and architecture-specific timer hardware management. The system is designed with a layered architecture that separates platform-independent timer logic from hardware-specific implementations.

## System Architecture

### Timer System Layers

```
┌─────────────────────────────────────────────────────────────┐
│                  User Space Applications                     │
├─────────────────────────────────────────────────────────────┤
│               Public Timer APIs                              │
│  • system_time()                                             │
│  • system_time_nsecs()                                       │
│  • add_timer() / cancel_timer()                              │
├─────────────────────────────────────────────────────────────┤
│              Core Timer Management                           │
│  • timer.cpp - Timer scheduling and interrupt handling      │
│  • Per-CPU timer queues                                      │
│  • Real-time clock integration                               │
├─────────────────────────────────────────────────────────────┤
│           Architecture Abstraction Layer                     │
│  • arch_timer.h - Common interface                           │
│  • arch_timer_set_hardware_timer()                           │
│  • arch_timer_clear_hardware_timer()                         │
├─────────────────────────────────────────────────────────────┤
│          Architecture-Specific Implementations               │
│  • ARM64: Generic Timer (CNTV_*)                             │
│  • x86: HPET, APIC Timer, PIT (priority-based selection)     │
│  • RISC-V: SBI timer interface                               │
├─────────────────────────────────────────────────────────────┤
│                Hardware Timer Controllers                    │
│  • ARM Generic Timer, x86 APIC/HPET/PIT, RISC-V Machine     │
└─────────────────────────────────────────────────────────────┘
```

## Core Timer Structures

### Timer Structure Definition

```c
// From headers/os/drivers/KernelExport.h
typedef int32 (*timer_hook)(timer *);

struct timer {
    struct timer *next;         // Linked list pointer for timer queue
    int64        schedule_time; // Absolute system time for execution (microseconds)
    void         *user_data;    // User-provided data for callback
    uint16       flags;         // Timer type and behavior flags
    uint16       cpu;           // CPU where timer is scheduled
    timer_hook   hook;          // Callback function to execute
    bigtime_t    period;        // Period for periodic timers (microseconds)
};
```

### Timer Flags and Types

```c
// Timer types
#define B_ONE_SHOT_ABSOLUTE_TIMER  1  // Fires once at absolute time
#define B_ONE_SHOT_RELATIVE_TIMER  2  // Fires once after relative delay
#define B_PERIODIC_TIMER           3  // Fires repeatedly with fixed period

// Timer flags (from headers/private/kernel/timer.h)
#define B_TIMER_REAL_TIME_BASE     0x2000  // Use real-time clock as base
#define B_TIMER_USE_TIMER_STRUCT_TIMES 0x4000  // Use timer.schedule_time/period

// Combined flags mask
#define B_TIMER_FLAGS (B_TIMER_USE_TIMER_STRUCT_TIMES | B_TIMER_REAL_TIME_BASE)
```

### Per-CPU Timer Data

```c
// From src/system/kernel/timer.cpp
struct per_cpu_timer_data {
    spinlock     lock;                        // Protects timer queue access
    timer*       events;                      // Head of timer queue (sorted by time)
    timer*       current_event;               // Currently executing timer
    int32        current_event_in_progress;   // Flag for cancellation handling
    bigtime_t    real_time_offset;           // Offset for real-time base timers
};

static per_cpu_timer_data sPerCPU[SMP_MAX_CPUS];
```

## Public Timer APIs

### System Time Functions

```c
// From headers/os/kernel/OS.h
extern bigtime_t  system_time(void);      // Microseconds since boot
extern nanotime_t system_time_nsecs(void); // Nanoseconds since boot

// Usage example
bigtime_t start = system_time();
// ... do work ...
bigtime_t elapsed = system_time() - start;
```

**Implementation Notes:**
- `system_time()` returns microseconds since system boot
- Architecture-specific implementations use different strategies:
  - **x86_64**: High-performance TSC (Time Stamp Counter) with RDTSC/RDTSCP
  - **ARM64**: Generic Timer physical counter (CNTPCT_EL0)
  - **RISC-V**: Machine-mode cycle counter

### Timer Management Functions

```c
// From headers/os/drivers/KernelExport.h
extern status_t add_timer(timer *t, timer_hook hook, bigtime_t period, int32 flags);
extern bool     cancel_timer(timer *t);

// Timer callback function type
typedef int32 (*timer_hook)(timer *);
```

**add_timer() Parameters:**
- `timer *t`: Timer structure (allocated by caller)
- `timer_hook hook`: Callback function to execute
- `bigtime_t period`: Time value (interpretation depends on flags)
- `int32 flags`: Timer type and behavior flags

**add_timer() Behavior:**
- **Absolute timers**: `period` is absolute system time
- **Relative timers**: `period` is microseconds from now
- **Periodic timers**: `period` is interval between executions
- Returns `B_OK` on success, error code on failure

**cancel_timer() Behavior:**
- Returns `true` if timer was canceled before execution
- Returns `false` if timer was already executing or completed
- Safe to call from timer callback (cancels future iterations)

### Timer Usage Examples

#### One-Shot Relative Timer
```c
static timer my_timer;

static int32 timeout_callback(timer *t)
{
    dprintf("Timer fired after 5 seconds!\n");
    return B_HANDLED_INTERRUPT;
}

// Schedule timer to fire in 5 seconds
status_t result = add_timer(&my_timer, timeout_callback, 5000000, 
                           B_ONE_SHOT_RELATIVE_TIMER);
```

#### Periodic Timer
```c
static timer periodic_timer;

static int32 periodic_callback(timer *t)
{
    dprintf("Periodic timer tick\n");
    return B_HANDLED_INTERRUPT;
}

// Schedule timer to fire every 100ms
status_t result = add_timer(&periodic_timer, periodic_callback, 100000, 
                           B_PERIODIC_TIMER);
```

#### Absolute Real-Time Timer
```c
static timer realtime_timer;

static int32 realtime_callback(timer *t)
{
    dprintf("Real-time timer fired\n");
    return B_HANDLED_INTERRUPT;
}

// Get current real time and schedule timer for 10 seconds from now
bigtime_t real_time = real_time_clock_usecs();
bigtime_t target_time = real_time + 10000000;  // 10 seconds

realtime_timer.schedule_time = target_time;
status_t result = add_timer(&realtime_timer, realtime_callback, 0, 
                           B_ONE_SHOT_ABSOLUTE_TIMER | B_TIMER_REAL_TIME_BASE |
                           B_TIMER_USE_TIMER_STRUCT_TIMES);
```

## Core Timer Implementation

### Timer Initialization

```c
// From src/system/kernel/timer.cpp
status_t timer_init(kernel_args* args)
{
    TRACE(("timer_init: entry\n"));
    
    // Initialize architecture-specific timer hardware
    if (arch_init_timer(args) != B_OK)
        panic("arch_init_timer() failed");
    
    // Register debugger command for timer inspection
    add_debugger_command_etc("timers", &dump_timers, "List all timers",
        "\nPrints a list of all scheduled timers.\n", 0);
    
    return B_OK;
}

void timer_init_post_rtc(void)
{
    // Initialize real-time offset for all CPUs after RTC is available
    bigtime_t realTimeOffset = rtc_boot_time();
    
    int32 cpuCount = smp_get_num_cpus();
    for (int32 i = 0; i < cpuCount; i++)
        sPerCPU[i].real_time_offset = realTimeOffset;
}
```

### Timer Queue Management

Timers are maintained in **per-CPU sorted linked lists** ordered by `schedule_time`:

```c
// From src/system/kernel/timer.cpp
static void add_event_to_list(timer* event, timer** list)
{
    timer* next;
    timer* previous = NULL;
    
    // Find insertion point (sorted by schedule_time)
    for (next = *list; next != NULL; previous = next, next = previous->next) {
        if ((bigtime_t)next->schedule_time >= (bigtime_t)event->schedule_time)
            break;
    }
    
    // Insert timer into sorted position
    event->next = next;
    if (previous != NULL)
        previous->next = event;
    else
        *list = event;  // New head of list
}
```

**Key Properties:**
- Each CPU maintains its own timer queue
- Timers are sorted by `schedule_time` (earliest first)
- Hardware timer is programmed for earliest event
- Lock-free insertion with per-CPU spinlocks

### Timer Interrupt Handling

```c
// From src/system/kernel/timer.cpp
int32 timer_interrupt()
{
    per_cpu_timer_data& cpuData = sPerCPU[smp_get_current_cpu()];
    int32 rc = B_HANDLED_INTERRUPT;
    
    acquire_spinlock(&cpuData.lock);
    
    timer* event = cpuData.events;
    while (event != NULL && ((bigtime_t)event->schedule_time < system_time())) {
        // Remove timer from queue
        cpuData.events = event->next;
        cpuData.current_event = event;
        atomic_set(&cpuData.current_event_in_progress, 1);
        
        release_spinlock(&cpuData.lock);
        
        // Execute timer callback
        if (event->hook)
            rc = event->hook(event);
        
        atomic_set(&cpuData.current_event_in_progress, 0);
        acquire_spinlock(&cpuData.lock);
        
        // Handle periodic timer rescheduling
        if ((event->flags & ~B_TIMER_FLAGS) == B_PERIODIC_TIMER
                && cpuData.current_event != NULL) {
            event->schedule_time += event->period;
            
            // Skip missed ticks if system is behind
            bigtime_t now = system_time();
            if (now >= event->schedule_time + event->period) {
                event->schedule_time = now 
                    - (now - event->schedule_time) % event->period;
            }
            
            add_event_to_list(event, &cpuData.events);
        }
        
        cpuData.current_event = NULL;
        event = cpuData.events;
    }
    
    // Program hardware timer for next event
    if (cpuData.events != NULL)
        set_hardware_timer(cpuData.events->schedule_time);
    
    release_spinlock(&cpuData.lock);
    return rc;
}
```

**Key Features:**
- **Batch processing**: Handles multiple expired timers in one interrupt
- **Periodic timer support**: Automatically reschedules periodic timers
- **Tick skipping**: Handles system lag by skipping missed periodic ticks
- **Callback execution**: Runs timer callbacks with interrupts enabled
- **Hardware timer management**: Programs next interrupt time

## Architecture-Specific Implementations

### Common Architecture Interface

```c
// From headers/private/kernel/arch/timer.h
extern void arch_timer_set_hardware_timer(bigtime_t timeout);
extern void arch_timer_clear_hardware_timer(void);
extern int arch_init_timer(struct kernel_args *args);
```

### ARM64 Implementation

**Timer Hardware**: ARM Generic Timer with Virtual Timer interface

```c
// From src/system/kernel/arch/arm64/arch_timer.cpp
static uint64 sTimerTicksUS;        // Ticks per microsecond
static bigtime_t sTimerMaxInterval; // Maximum timer interval
static uint64 sBootTime;            // Boot time reference

void arch_timer_set_hardware_timer(bigtime_t timeout)
{
    if (timeout > sTimerMaxInterval)
        timeout = sTimerMaxInterval;
    
    // Program Virtual Timer countdown value
    WRITE_SPECIALREG(CNTV_TVAL_EL0, timeout * sTimerTicksUS);
    // Enable Virtual Timer
    WRITE_SPECIALREG(CNTV_CTL_EL0, TIMER_ENABLE);
}

void arch_timer_clear_hardware_timer()
{
    // Disable Virtual Timer
    WRITE_SPECIALREG(CNTV_CTL_EL0, TIMER_DISABLED);
}

bigtime_t system_time(void)
{
    // Read Physical Counter and convert to microseconds
    return (READ_SPECIALREG(CNTPCT_EL0) - sBootTime) / sTimerTicksUS;
}
```

**ARM64 Timer Features:**
- **Hardware**: ARM Generic Timer Virtual Timer (CNTV_*)
- **Frequency**: Read from CNTFRQ_EL0 register
- **Interrupt**: Virtual Timer PPI (typically IRQ 27)
- **Resolution**: Determined by Generic Timer frequency (typically 1-100 MHz)
- **System Time**: Based on Physical Counter (CNTPCT_EL0)

### x86 Implementation

**Timer Hardware**: Priority-based selection (HPET > APIC > PIT)

```c
// From src/system/kernel/arch/x86/arch_timer.cpp
static timer_info *sTimers[] = {
    &gHPETTimer,    // Highest priority
    &gAPICTimer,    // Medium priority  
    &gPITTimer,     // Lowest priority (fallback)
    NULL
};

static timer_info *sTimer = NULL;

void arch_timer_set_hardware_timer(bigtime_t timeout)
{
    sTimer->set_hardware_timer(timeout);
}

int arch_init_timer(kernel_args *args)
{
    // Sort timers by priority
    sort_timers(sTimers, (sizeof(sTimers) / sizeof(sTimers[0])) - 1);
    
    // Try each timer in priority order
    for (int i = 0; (timer = sTimers[i]) != NULL; i++) {
        if (timer->init(args) == B_OK)
            break;
    }
    
    sTimer = timer;
    dprintf("arch_init_timer: using %s timer.\n", sTimer->name);
    return 0;
}
```

**x86 Timer Priority:**
1. **HPET (High Precision Event Timer)**: Modern, high-resolution timer
2. **APIC Timer**: Per-CPU timer in Advanced Programmable Interrupt Controller
3. **PIT (Programmable Interval Timer)**: Legacy 8254 timer (fallback)

**x86 System Time**: Uses high-performance TSC (Time Stamp Counter)

```c
// From src/system/libroot/os/arch/x86_64/system_time.cpp
static int64_t __system_time_rdtscp()
{
    uint32_t aux;
    __uint128_t time = static_cast<__uint128_t>(__rdtscp(&aux)) * cv_factor;
    return time >> 64;
}

extern "C" int64_t system_time()
{
    return sSystemTime();  // Function pointer to RDTSC/RDTSCP variant
}
```

### Timer Information Structure

```c
// From headers/private/kernel/timer.h
struct timer_info {
    const char *name;                                    // Timer name
    int (*get_priority)(void);                          // Priority value
    status_t (*set_hardware_timer)(bigtime_t timeout);  // Set timer
    status_t (*clear_hardware_timer)(void);            // Clear timer
    status_t (*init)(struct kernel_args *args);        // Initialize
};
```

## Real-Time Clock Integration

### Real-Time Base Timers

Haiku supports timers based on **real-time clock** rather than **system uptime**:

```c
// From src/system/kernel/timer.cpp
void timer_real_time_clock_changed()
{
    // Update all real-time base timers when RTC changes
    call_all_cpus(&per_cpu_real_time_clock_changed, NULL);
}

static void per_cpu_real_time_clock_changed(void*, int cpu)
{
    per_cpu_timer_data& cpuData = sPerCPU[cpu];
    bigtime_t timeDiff = cpuData.real_time_offset - rtc_boot_time();
    
    // Find all real-time base timers and update their schedule times
    timer* affectedTimers = NULL;
    timer** it = &cpuData.events;
    
    while (timer* event = *it) {
        if ((event->flags & ~B_TIMER_FLAGS) == B_ONE_SHOT_ABSOLUTE_TIMER
            && (event->flags & B_TIMER_REAL_TIME_BASE) != 0) {
            // Remove from queue and add to affected list
            *it = event->next;
            event->next = affectedTimers;
            affectedTimers = event;
        } else {
            it = &event->next;
        }
    }
    
    // Update and requeue affected timers
    while (affectedTimers != NULL) {
        timer* event = affectedTimers;
        affectedTimers = event->next;
        
        event->schedule_time += timeDiff;  // Adjust for RTC change
        add_event_to_list(event, &cpuData.events);
    }
}
```

**Real-Time Features:**
- **Automatic adjustment**: Timers adjust when system clock changes
- **Overflow protection**: Handles time adjustment overflows
- **Per-CPU handling**: Updates timers on all CPUs independently

## Performance Characteristics

### Timer Resolution and Accuracy

| Architecture | Timer Source | Typical Frequency | Resolution | Accuracy |
|--------------|--------------|-------------------|------------|----------|
| ARM64 | Generic Timer | 24 MHz - 100 MHz | 10-42 ns | Hardware dependent |
| x86_64 | TSC | ~3 GHz | <1 ns | High (if invariant) |
| x86 HPET | HPET | 14.318 MHz | ~70 ns | High |
| x86 APIC | APIC Timer | CPU frequency dependent | Variable | Medium |

### Timer Interrupt Latency

**Typical Latencies:**
- **Hardware timer setup**: 5-10 CPU cycles
- **Interrupt delivery**: 100-1000 CPU cycles (depends on GIC/APIC)
- **Timer processing**: 50-200 CPU cycles per timer
- **Callback execution**: Variable (user-defined)

### Optimization Strategies

**Efficient Timer Programming:**
```c
// Prefer relative timers for short delays
add_timer(&timer, callback, 1000, B_ONE_SHOT_RELATIVE_TIMER);

// Use periodic timers instead of rescheduling one-shots
add_timer(&timer, callback, 16667, B_PERIODIC_TIMER);  // 60 Hz

// Batch timer operations when possible
// (hardware timer is updated only when earliest timer changes)
```

**System Time Optimization:**
```c
// Cache conversion factors to avoid division
static uint64_t timer_freq_mhz;

void init_timing_constants()
{
    timer_freq_mhz = arch_timer_frequency() / 1000000;
}

// Fast microsecond conversion
uint64_t get_time_fast()
{
    return arch_read_counter() / timer_freq_mhz;
}
```

## Debugging and Diagnostics

### Timer State Inspection

Haiku provides built-in debugging support for timer inspection:

```bash
# In kernel debugger (KDL)
> timers
CPU 0:
  [  1234567] 0xdeadbeef: one shot,           flags: 0x1, user data: 0x0, callback: 0x12345678   kernel:timeout_func
  [  2345678] 0xcafebabe: periodic     100000, flags: 0x3, user data: 0x1000, callback: 0x87654321   driver:periodic_check

CPU 1:
  no timers scheduled

current time: 1234567890
```

### Common Timer Issues

**Problem**: Timer not firing
```c
// Check timer setup
if (add_timer(&timer, callback, period, flags) != B_OK) {
    dprintf("Timer add failed\n");
}

// Verify callback signature
int32 my_callback(timer *t)  // Must return int32
{
    // Timer logic here
    return B_HANDLED_INTERRUPT;  // or B_INVOKE_SCHEDULER
}
```

**Problem**: Timer firing too early/late
```c
// Check system time consistency
bigtime_t start = system_time();
bigtime_t schedule_time = start + 1000000;  // 1 second

// Use absolute timers for precise timing
timer.schedule_time = schedule_time;
add_timer(&timer, callback, 0, B_ONE_SHOT_ABSOLUTE_TIMER | 
          B_TIMER_USE_TIMER_STRUCT_TIMES);
```

**Problem**: Timer cancellation race conditions
```c
// Safe cancellation pattern
bool was_pending = cancel_timer(&timer);
if (was_pending) {
    // Timer was canceled before execution
} else {
    // Timer may have executed or is executing
    // Wait for completion if necessary
}
```

## Multi-Core Considerations

### Per-CPU Timer Queues

- **Independent queues**: Each CPU maintains its own timer queue
- **CPU affinity**: Timers execute on the CPU where they were scheduled
- **Load balancing**: No automatic timer migration between CPUs
- **Synchronization**: Per-CPU spinlocks protect timer queue access

### SMP Timer Coordination

```c
// Timer operations are CPU-local by default
int current_cpu = smp_get_current_cpu();

// To ensure timer runs on specific CPU, schedule from that CPU
// or use IPI-based mechanisms for cross-CPU timer management
```

**Best Practices:**
- Schedule timers from the CPU where they should execute
- Use per-CPU data structures when possible
- Avoid sharing timer structures between CPUs
- Consider NUMA topology for high-frequency timers

## Future Enhancements

### Potential Improvements

1. **High-Resolution Timer Support**
   - Sub-microsecond timer resolution
   - Nanosecond-precision interfaces
   - Hardware timestamp integration

2. **Power Management Integration**
   - CPU idle state coordination
   - Timer coalescing for power efficiency
   - Dynamic tick adjustment

3. **Virtualization Support**
   - Guest timer virtualization
   - Time synchronization with host
   - Virtual timer migration

4. **Real-Time Enhancements**
   - Priority-based timer execution
   - Deadline scheduling integration
   - Interrupt latency guarantees

## References

- **Core Implementation**: `src/system/kernel/timer.cpp`
- **Architecture Interface**: `headers/private/kernel/arch/timer.h`
- **Public APIs**: `headers/os/drivers/KernelExport.h`, `headers/os/kernel/OS.h`
- **ARM64 Timer**: `src/system/kernel/arch/arm64/arch_timer.cpp`
- **x86 Timer**: `src/system/kernel/arch/x86/arch_timer.cpp`
- **Timer Debugging**: Built-in `timers` debugger command

---

*This document provides comprehensive coverage of Haiku's timer architecture for kernel and driver development.*