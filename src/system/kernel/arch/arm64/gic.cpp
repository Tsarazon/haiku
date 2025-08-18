/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Generic Interrupt Controller (GIC) Driver
 * 
 * This module provides comprehensive support for ARM Generic Interrupt Controllers,
 * including GICv2, GICv3, and GICv4 variants. It handles interrupt routing,
 * priority management, and inter-processor interrupts (IPIs) for SMP systems.
 *
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <kernel/kernel.h>
#include <kernel/debug.h>
#include <kernel/vm/vm.h>
#include <kernel/arch/arm64/arch_kernel.h>
#include <kernel/arch/arm64/arch_int.h>
#include <kernel/arch/arm64/arch_mmio.h>
#include <kernel/arch/arm64/arch_smp.h>
#include <kernel/lock.h>
#include <kernel/smp.h>
#include <kernel/int.h>

// GIC Architecture Version Detection
#define GIC_VERSION_UNKNOWN     0
#define GIC_VERSION_V2          2
#define GIC_VERSION_V3          3
#define GIC_VERSION_V4          4

// GIC Distributor Registers (GICv2/v3 compatible)
#define GICD_CTLR               0x0000  // Distributor Control Register
#define GICD_TYPER              0x0004  // Interrupt Controller Type Register
#define GICD_IIDR               0x0008  // Distributor Implementer Identification Register
#define GICD_IGROUPR            0x0080  // Interrupt Group Registers
#define GICD_ISENABLER          0x0100  // Interrupt Set-Enable Registers
#define GICD_ICENABLER          0x0180  // Interrupt Clear-Enable Registers
#define GICD_ISPENDR            0x0200  // Interrupt Set-Pending Registers
#define GICD_ICPENDR            0x0280  // Interrupt Clear-Pending Registers
#define GICD_ISACTIVER          0x0300  // Interrupt Set-Active Registers
#define GICD_ICACTIVER          0x0380  // Interrupt Clear-Active Registers
#define GICD_IPRIORITYR         0x0400  // Interrupt Priority Registers
#define GICD_ITARGETSR          0x0800  // Interrupt Processor Targets Registers (GICv2)
#define GICD_ICFGR              0x0C00  // Interrupt Configuration Registers
#define GICD_NSACR              0x0E00  // Non-secure Access Control Registers
#define GICD_SGIR               0x0F00  // Software Generated Interrupt Register (GICv2)
#define GICD_CPENDSGIR          0x0F10  // SGI Clear-Pending Registers (GICv2)
#define GICD_SPENDSGIR          0x0F20  // SGI Set-Pending Registers (GICv2)

// GICv3 specific distributor registers
#define GICD_IROUTER            0x6000  // Interrupt Routing Registers (GICv3)

// GICv3 Redistributor Registers
#define GICR_CTLR               0x0000  // Redistributor Control Register
#define GICR_IIDR               0x0004  // Redistributor Implementer Identification Register
#define GICR_TYPER              0x0008  // Redistributor Type Register
#define GICR_STATUSR            0x0010  // Redistributor Status Register
#define GICR_WAKER              0x0014  // Redistributor Wake Register
#define GICR_SETLPIR            0x0040  // Set LPI Pending Register
#define GICR_CLRLPIR            0x0048  // Clear LPI Pending Register
#define GICR_PROPBASER          0x0070  // Redistributor Properties Base Address Register
#define GICR_PENDBASER          0x0078  // Redistributor LPI Pending Table Base Address Register

// GICv3 Redistributor SGI/PPI Registers (at offset 0x10000)
#define GICR_SGI_OFFSET         0x10000
#define GICR_IGROUPR0           (GICR_SGI_OFFSET + 0x0080)  // SGI/PPI Group Register
#define GICR_ISENABLER0         (GICR_SGI_OFFSET + 0x0100)  // SGI/PPI Set-Enable Register
#define GICR_ICENABLER0         (GICR_SGI_OFFSET + 0x0180)  // SGI/PPI Clear-Enable Register
#define GICR_ISPENDR0           (GICR_SGI_OFFSET + 0x0200)  // SGI/PPI Set-Pending Register
#define GICR_ICPENDR0           (GICR_SGI_OFFSET + 0x0280)  // SGI/PPI Clear-Pending Register
#define GICR_ISACTIVER0         (GICR_SGI_OFFSET + 0x0300)  // SGI/PPI Set-Active Register
#define GICR_ICACTIVER0         (GICR_SGI_OFFSET + 0x0380)  // SGI/PPI Clear-Active Register
#define GICR_IPRIORITYR         (GICR_SGI_OFFSET + 0x0400)  // SGI/PPI Priority Registers
#define GICR_ICFGR0             (GICR_SGI_OFFSET + 0x0C00)  // SGI Configuration Register
#define GICR_ICFGR1             (GICR_SGI_OFFSET + 0x0C04)  // PPI Configuration Register
#define GICR_IGRPMODR0          (GICR_SGI_OFFSET + 0x0D00)  // SGI/PPI Group Modifier Register
#define GICR_NSACR              (GICR_SGI_OFFSET + 0x0E00)  // SGI/PPI Non-secure Access Control Register

// Redistributor Control Register bits
#define GICR_CTLR_ENABLE_LPIS   (1 << 0)   // Enable LPIs
#define GICR_CTLR_CES           (1 << 1)   // Clear Enable Supported
#define GICR_CTLR_IR            (1 << 2)   // Interrupt Routing
#define GICR_CTLR_CIL           (1 << 3)   // Common LPI Configuration

// Redistributor Type Register bits
#define GICR_TYPER_PLPIS        (1 << 0)   // Physical LPIs supported
#define GICR_TYPER_VLPIS        (1 << 1)   // Virtual LPIs supported
#define GICR_TYPER_DIRTY        (1 << 2)   // Dirty state supported
#define GICR_TYPER_DIRECTLPI    (1 << 3)   // Direct LPI supported
#define GICR_TYPER_LAST         (1 << 4)   // Last redistributor
#define GICR_TYPER_DPGS         (1 << 5)   // Dirty Page Support
#define GICR_TYPER_MPAM         (1 << 6)   // MPAM support
#define GICR_TYPER_RVPEID       (1 << 7)   // Reported VPEID

// Redistributor Wake Register bits
#define GICR_WAKER_PROCESSORSL  (1 << 1)   // Processor Sleep
#define GICR_WAKER_CHILDRENASK  (1 << 2)   // Children Asleep

// GIC CPU Interface Registers (GICv2)
#define GICC_CTLR               0x0000  // CPU Interface Control Register
#define GICC_PMR                0x0004  // Interrupt Priority Mask Register
#define GICC_BPR                0x0008  // Binary Point Register
#define GICC_IAR                0x000C  // Interrupt Acknowledge Register
#define GICC_EOIR               0x0010  // End of Interrupt Register
#define GICC_RPR                0x0014  // Running Priority Register
#define GICC_HPPIR              0x0018  // Highest Priority Pending Interrupt Register
#define GICC_ABPR               0x001C  // Aliased Binary Point Register
#define GICC_AIAR               0x0020  // Aliased Interrupt Acknowledge Register
#define GICC_AEOIR              0x0024  // Aliased End of Interrupt Register
#define GICC_AHPPIR             0x0028  // Aliased Highest Priority Pending Interrupt Register
#define GICC_APR0               0x00D0  // Active Priority Register
#define GICC_NSAPR0             0x00E0  // Non-secure Active Priority Register
#define GICC_IIDR               0x00FC  // CPU Interface Identification Register
#define GICC_DIR                0x1000  // Deactivate Interrupt Register

// GIC Control Register bits
#define GICD_CTLR_ENABLE_GRP0   (1 << 0)
#define GICD_CTLR_ENABLE_GRP1   (1 << 1)
#define GICD_CTLR_ARE           (1 << 4)  // Affinity Routing Enable (GICv3)
#define GICD_CTLR_ARE_NS        (1 << 5)  // Non-secure Affinity Routing Enable

#define GICC_CTLR_ENABLE        (1 << 0)
#define GICC_CTLR_EOIMODE       (1 << 9)  // End of Interrupt mode

// Interrupt Types
#define GIC_SGI_MAX             15   // Software Generated Interrupts (0-15)
#define GIC_PPI_BASE            16   // Private Peripheral Interrupts (16-31)
#define GIC_PPI_MAX             31
#define GIC_SPI_BASE            32   // Shared Peripheral Interrupts (32+)

// GIC Priority levels
#define GIC_PRIORITY_HIGHEST    0x00
#define GIC_PRIORITY_HIGH       0x40
#define GIC_PRIORITY_NORMAL     0x80
#define GIC_PRIORITY_LOW        0xC0
#define GIC_PRIORITY_LOWEST     0xFF

// Maximum number of interrupts supported
#define GIC_MAX_INTERRUPTS      1024

// IPI (Inter-Processor Interrupt) Definitions
#define IPI_SGI_BASE            0    // Base SGI for IPIs
#define IPI_SGI_COUNT           8    // Number of SGIs reserved for IPIs

// IPI Types (using SGI 0-7)
#define IPI_RESCHEDULE          0    // Request reschedule
#define IPI_CALL_FUNCTION       1    // Execute function on target CPU
#define IPI_CALL_FUNCTION_SYNC  2    // Execute function with synchronization
#define IPI_TLB_FLUSH           3    // TLB flush request
#define IPI_CACHE_FLUSH         4    // Cache flush request
#define IPI_TIMER_SYNC          5    // Timer synchronization
#define IPI_SHUTDOWN            6    // CPU shutdown request
#define IPI_DEBUG_BREAK         7    // Debug break request

// IPI Flags
#define IPI_FLAG_WAIT_COMPLETE  (1 << 0)  // Wait for completion
#define IPI_FLAG_BROADCAST      (1 << 1)  // Broadcast to all CPUs
#define IPI_FLAG_EXCLUDE_SELF   (1 << 2)  // Exclude sending CPU

// Maximum number of CPUs for IPI support
#define IPI_MAX_CPUS            64

// Cross-CPU function call parameters
struct ipi_call_data {
    void (*function)(void* data);
    void* data;
    volatile int32* call_count;
    volatile int32* finished_count;
    volatile bool* wait_flag;
};

// GIC driver state structure
struct gic_driver_state {
    uint32 version;                    // GIC version (2, 3, or 4)
    bool initialized;                  // Initialization status
    
    // Memory mapped register bases
    addr_t distributor_base;           // GICD base address
    addr_t cpu_interface_base;         // GICC base address (GICv2 only)
    addr_t redistributor_base;         // GICR base address (GICv3+ only)
    
    // Interrupt configuration
    uint32 max_interrupts;             // Maximum supported interrupts
    uint32 max_cpus;                   // Maximum supported CPUs
    uint32 priority_mask;              // Priority mask value
    
    // IPI support
    uint32 ipi_base;                   // Base SGI for IPIs
    spinlock ipi_locks[IPI_MAX_CPUS];  // Per-CPU IPI locks
    volatile uint32 ipi_pending[IPI_MAX_CPUS]; // Pending IPI bitmask per CPU
    
    // Cross-CPU function call support
    struct ipi_call_data call_data;   // Function call data
    volatile int32 call_active;       // Call in progress flag
    volatile int32 call_count;        // Number of CPUs to call
    volatile int32 finished_count;    // Number of CPUs finished
    
    // Security state
    bool secure_mode;                  // Operating in secure mode
    
    // Lock for thread-safe access
    spinlock gic_lock;
};

// Global GIC driver state
static struct gic_driver_state gic_state = {
    .version = GIC_VERSION_UNKNOWN,
    .initialized = false,
    .distributor_base = 0,
    .cpu_interface_base = 0,
    .redistributor_base = 0,
    .max_interrupts = 0,
    .max_cpus = 0,
    .priority_mask = GIC_PRIORITY_LOWEST,
    .ipi_base = 0,
    .secure_mode = false,
    .gic_lock = B_SPINLOCK_INITIALIZER
};

/*
 * Low-level GIC register access functions
 */

static inline uint32
gic_read_distributor(uint32 offset)
{
    if (gic_state.distributor_base == 0) {
        panic("GIC: Distributor not mapped");
        return 0;
    }
    return arch_device_read_32(gic_state.distributor_base + offset);
}

static inline void
gic_write_distributor(uint32 offset, uint32 value)
{
    if (gic_state.distributor_base == 0) {
        panic("GIC: Distributor not mapped");
        return;
    }
    arch_device_write_32(gic_state.distributor_base + offset, value);
}

static inline uint32
gic_read_cpu_interface(uint32 offset)
{
    if (gic_state.version >= GIC_VERSION_V3) {
        // GICv3+ uses system registers for CPU interface
        panic("GIC: CPU interface access not supported in GICv3+");
        return 0;
    }
    
    if (gic_state.cpu_interface_base == 0) {
        panic("GIC: CPU interface not mapped");
        return 0;
    }
    return arch_device_read_32(gic_state.cpu_interface_base + offset);
}

static inline void
gic_write_cpu_interface(uint32 offset, uint32 value)
{
    if (gic_state.version >= GIC_VERSION_V3) {
        // GICv3+ uses system registers for CPU interface
        panic("GIC: CPU interface access not supported in GICv3+");
        return;
    }
    
    if (gic_state.cpu_interface_base == 0) {
        panic("GIC: CPU interface not mapped");
        return;
    }
    arch_device_write_32(gic_state.cpu_interface_base + offset, value);
}

static inline uint64
gic_read_redistributor(uint32 cpu, uint32 offset)
{
    if (gic_state.version < GIC_VERSION_V3) {
        panic("GIC: Redistributor not available in GICv2");
        return 0;
    }
    
    if (gic_state.redistributor_base == 0) {
        panic("GIC: Redistributor not mapped");
        return 0;
    }
    
    // Each redistributor has 128KB address space
    addr_t cpu_redistributor = gic_state.redistributor_base + (cpu * 0x20000);
    return arch_device_read_64(cpu_redistributor + offset);
}

static inline void
gic_write_redistributor(uint32 cpu, uint32 offset, uint64 value)
{
    if (gic_state.version < GIC_VERSION_V3) {
        panic("GIC: Redistributor not available in GICv2");
        return;
    }
    
    if (gic_state.redistributor_base == 0) {
        panic("GIC: Redistributor not mapped");
        return;
    }
    
    // Each redistributor has 128KB address space
    addr_t cpu_redistributor = gic_state.redistributor_base + (cpu * 0x20000);
    arch_device_write_64(cpu_redistributor + offset, value);
}

/*
 * GIC Version Detection and Feature Discovery
 */

static uint32
gic_detect_version(void)
{
    // Read the Peripheral ID registers to determine GIC version
    uint32 pidr2 = gic_read_distributor(0xFFE8);
    uint32 arch_rev = (pidr2 >> 4) & 0xF;
    
    switch (arch_rev) {
        case 1:
        case 2:
            return GIC_VERSION_V2;
        case 3:
            return GIC_VERSION_V3;
        case 4:
            return GIC_VERSION_V4;
        default:
            dprintf("GIC: Unknown architecture revision: %u\n", arch_rev);
            return GIC_VERSION_UNKNOWN;
    }
}

static void
gic_detect_features(void)
{
    uint32 typer = gic_read_distributor(GICD_TYPER);
    
    // Extract maximum number of interrupts
    gic_state.max_interrupts = ((typer & 0x1F) + 1) * 32;
    if (gic_state.max_interrupts > GIC_MAX_INTERRUPTS) {
        gic_state.max_interrupts = GIC_MAX_INTERRUPTS;
    }
    
    // Extract maximum number of CPUs (GICv2 only)
    if (gic_state.version == GIC_VERSION_V2) {
        gic_state.max_cpus = ((typer >> 5) & 0x7) + 1;
        if (gic_state.max_cpus > 8) {
            gic_state.max_cpus = 8;  // GICv2 supports max 8 CPUs
        }
    } else {
        // GICv3+ can support many more CPUs
        gic_state.max_cpus = 255;  // Conservative estimate
    }
    
    // Detect security extensions
    gic_state.secure_mode = (typer & (1 << 10)) == 0;
    
    dprintf("GIC: Detected GICv%u with %u interrupts, %u CPUs, %s mode\n",
            gic_state.version, gic_state.max_interrupts, gic_state.max_cpus,
            gic_state.secure_mode ? "secure" : "non-secure");
}

/*
 * GIC Initialization Functions
 */

static status_t
gic_init_distributor(void)
{
    uint32 ctlr;
    
    dprintf("GIC: Initializing distributor\n");
    
    // Disable distributor
    gic_write_distributor(GICD_CTLR, 0);
    
    // Wait for the disable to take effect
    while (gic_read_distributor(GICD_CTLR) & (GICD_CTLR_ENABLE_GRP0 | GICD_CTLR_ENABLE_GRP1)) {
        cpu_pause();
    }
    
    // Configure all interrupts as Group 1 (non-secure)
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 32) {
        gic_write_distributor(GICD_IGROUPR + (i / 8), 0xFFFFFFFF);
    }
    
    // Set all interrupts to lowest priority
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 4) {
        uint32 priority = (GIC_PRIORITY_LOWEST << 24) | (GIC_PRIORITY_LOWEST << 16) |
                         (GIC_PRIORITY_LOWEST << 8) | GIC_PRIORITY_LOWEST;
        gic_write_distributor(GICD_IPRIORITYR + i, priority);
    }
    
    // Disable all interrupts
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 32) {
        gic_write_distributor(GICD_ICENABLER + (i / 8), 0xFFFFFFFF);
    }
    
    // Clear all pending interrupts
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 32) {
        gic_write_distributor(GICD_ICPENDR + (i / 8), 0xFFFFFFFF);
    }
    
    // Clear all active interrupts
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 32) {
        gic_write_distributor(GICD_ICACTIVER + (i / 8), 0xFFFFFFFF);
    }
    
    // Configure interrupt targets (GICv2 only)
    if (gic_state.version == GIC_VERSION_V2) {
        // Set all SPIs to target CPU 0 initially
        for (uint32 i = GIC_SPI_BASE; i < gic_state.max_interrupts; i += 4) {
            gic_write_distributor(GICD_ITARGETSR + i, 0x01010101);
        }
    }
    
    // Configure all interrupts as level-triggered
    for (uint32 i = 0; i < gic_state.max_interrupts; i += 16) {
        gic_write_distributor(GICD_ICFGR + (i / 4), 0x00000000);
    }
    
    // Enable distributor
    ctlr = GICD_CTLR_ENABLE_GRP1;
    if (gic_state.version >= GIC_VERSION_V3) {
        ctlr |= GICD_CTLR_ARE | GICD_CTLR_ARE_NS;
    }
    if (!gic_state.secure_mode) {
        ctlr |= GICD_CTLR_ENABLE_GRP0;
    }
    
    gic_write_distributor(GICD_CTLR, ctlr);
    
    dprintf("GIC: Distributor initialized successfully\n");
    return B_OK;
}

static status_t
gic_init_cpu_interface(uint32 cpu)
{
    if (gic_state.version == GIC_VERSION_V2) {
        dprintf("GIC: Initializing CPU interface for CPU %u\n", cpu);
        
        // Set priority mask to allow all interrupts
        gic_write_cpu_interface(GICC_PMR, gic_state.priority_mask);
        
        // Set binary point to 0 (no grouping)
        gic_write_cpu_interface(GICC_BPR, 0);
        
        // Enable CPU interface
        gic_write_cpu_interface(GICC_CTLR, GICC_CTLR_ENABLE);
        
        dprintf("GIC: CPU interface initialized for CPU %u\n", cpu);
    } else {
        // GICv3+ uses system registers
        dprintf("GIC: Initializing GICv%u system registers for CPU %u\n", gic_state.version, cpu);
        
        // Enable system register access
        uint64_t sre;
        __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(sre));
        sre |= (1 << 0); // SRE bit - enable system register access
        __asm__ volatile("msr ICC_SRE_EL1, %0" :: "r"(sre));
        __asm__ volatile("isb" ::: "memory");
        
        // Set priority mask to allow all interrupts
        __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"((uint64_t)gic_state.priority_mask));
        
        // Set binary point register to 0 (no grouping)
        __asm__ volatile("msr ICC_BPR1_EL1, %0" :: "r"(0ULL));
        
        // Configure interrupt group enable
        uint64_t igrpen1 = 1; // Enable Group 1 interrupts
        __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(igrpen1));
        
        // Configure control register
        uint64_t ctlr = 0;
        // EOImode = 0 (EOI deactivates interrupt)
        // CBPR = 0 (separate binary point registers)
        __asm__ volatile("msr ICC_CTLR_EL1, %0" :: "r"(ctlr));
        
        // Ensure all changes are visible
        __asm__ volatile("isb" ::: "memory");
        
        dprintf("GIC: GICv%u system registers initialized for CPU %u\n", gic_state.version, cpu);
    }
    
    return B_OK;
}

/*
 * Interrupt Management Functions
 */

status_t
gic_enable_interrupt(uint32 irq)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICD_ISENABLER + (irq / 32) * 4;
    uint32 bit = 1U << (irq % 32);
    
    gic_write_distributor(reg, bit);
    
    dprintf("GIC: Enabled interrupt %u\n", irq);
    return B_OK;
}

status_t
gic_disable_interrupt(uint32 irq)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICD_ICENABLER + (irq / 32) * 4;
    uint32 bit = 1U << (irq % 32);
    
    gic_write_distributor(reg, bit);
    
    dprintf("GIC: Disabled interrupt %u\n", irq);
    return B_OK;
}

status_t
gic_set_interrupt_priority(uint32 irq, uint8 priority)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICD_IPRIORITYR + irq;
    gic_write_distributor(reg, priority);
    
    return B_OK;
}

status_t
gic_set_interrupt_target(uint32 irq, uint32 cpu_mask)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts || irq < GIC_SPI_BASE) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    if (gic_state.version == GIC_VERSION_V2) {
        uint32 reg = GICD_ITARGETSR + irq;
        gic_write_distributor(reg, cpu_mask & 0xFF);
    } else {
        // GICv3+ uses affinity routing
        uint64 reg = GICD_IROUTER + (irq * 8);
        // Convert CPU mask to affinity value (simplified - assumes 1:1 mapping)
        uint64 affinity = 0;
        for (uint32 cpu = 0; cpu < 32; cpu++) {
            if (cpu_mask & (1U << cpu)) {
                affinity = cpu; // Route to first CPU in mask
                break;
            }
        }
        gic_write_distributor(reg, affinity & 0xFFFFFFFF);
        gic_write_distributor(reg + 4, affinity >> 32);
    }
    
    return B_OK;
}

status_t
gic_set_interrupt_trigger(uint32 irq, bool edge_triggered)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts || irq < GIC_PPI_BASE) {
        return B_BAD_VALUE; // SGIs are always edge-triggered
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    // Configuration register: each interrupt uses 2 bits
    uint32 reg = GICD_ICFGR + (irq / 16) * 4;
    uint32 shift = (irq % 16) * 2 + 1; // Bit 1 of the 2-bit field controls trigger type
    uint32 value = gic_read_distributor(reg);
    
    if (edge_triggered) {
        value |= (1U << shift);   // Set bit for edge-triggered
    } else {
        value &= ~(1U << shift);  // Clear bit for level-triggered
    }
    
    gic_write_distributor(reg, value);
    
    dprintf("GIC: Set interrupt %u to %s-triggered\n", irq, 
            edge_triggered ? "edge" : "level");
    
    return B_OK;
}

status_t
gic_get_interrupt_trigger(uint32 irq, bool *edge_triggered)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts || edge_triggered == NULL) {
        return B_BAD_VALUE;
    }
    
    if (irq <= GIC_SGI_MAX) {
        *edge_triggered = true; // SGIs are always edge-triggered
        return B_OK;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICD_ICFGR + (irq / 16) * 4;
    uint32 shift = (irq % 16) * 2 + 1;
    uint32 value = gic_read_distributor(reg);
    
    *edge_triggered = (value & (1U << shift)) != 0;
    
    return B_OK;
}

status_t
gic_set_interrupt_security(uint32 irq, bool secure)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts) {
        return B_BAD_VALUE;
    }
    
    // Only allow security configuration in secure mode
    if (!gic_state.secure_mode) {
        dprintf("GIC: Security configuration not allowed in non-secure mode\n");
        return B_NOT_ALLOWED;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    // Group register: 0 = Group 0 (secure), 1 = Group 1 (non-secure)
    uint32 reg = GICD_IGROUPR + (irq / 32) * 4;
    uint32 bit = 1U << (irq % 32);
    uint32 value = gic_read_distributor(reg);
    
    if (secure) {
        value &= ~bit;  // Clear bit for Group 0 (secure)
    } else {
        value |= bit;   // Set bit for Group 1 (non-secure)
    }
    
    gic_write_distributor(reg, value);
    
    dprintf("GIC: Set interrupt %u to %s group\n", irq, secure ? "secure" : "non-secure");
    
    return B_OK;
}

status_t
gic_get_interrupt_security(uint32 irq, bool *secure)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (irq >= gic_state.max_interrupts || secure == NULL) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICD_IGROUPR + (irq / 32) * 4;
    uint32 bit = 1U << (irq % 32);
    uint32 value = gic_read_distributor(reg);
    
    *secure = (value & bit) == 0; // Bit clear = Group 0 = secure
    
    return B_OK;
}

status_t
gic_set_priority_mask(uint8 mask)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    gic_state.priority_mask = mask;
    
    // Update CPU interface priority mask
    if (gic_state.version == GIC_VERSION_V2) {
        gic_write_cpu_interface(GICC_PMR, mask);
    } else {
        // GICv3+ uses system registers
        __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"((uint64_t)mask));
        __asm__ volatile("isb" ::: "memory");
    }
    
    dprintf("GIC: Set priority mask to 0x%02X\n", mask);
    
    return B_OK;
}

uint8
gic_get_priority_mask(void)
{
    return gic_state.priority_mask;
}

int32
gic_acknowledge_interrupt(void)
{
    if (!gic_state.initialized) {
        return -1;
    }
    
    int32 irq;
    
    if (gic_state.version == GIC_VERSION_V2) {
        uint32 iar = gic_read_cpu_interface(GICC_IAR);
        irq = iar & 0x3FF;  // Interrupt ID is in bits 0-9
        
        if (irq >= 1020) {
            // Spurious interrupt
            return -1;
        }
    } else {
        // GICv3+ uses system registers
        uint64_t iar;
        __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
        irq = iar & 0xFFFFFF;  // Interrupt ID is in bits 0-23
        
        if (irq >= 1020) {
            // Spurious interrupt or special interrupt
            return -1;
        }
    }
    
    return irq;
}

status_t
gic_end_interrupt(uint32 irq)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (gic_state.version == GIC_VERSION_V2) {
        gic_write_cpu_interface(GICC_EOIR, irq);
    } else {
        // GICv3+ uses system registers
        __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"((uint64_t)irq));
        __asm__ volatile("isb" ::: "memory");
    }
    
    return B_OK;
}

/*
 * Advanced Inter-Processor Interrupt (IPI) Support
 */

// IPI handler function prototypes
typedef void (*ipi_handler_func)(uint32 cpu, void* data);

// IPI handler registry
static ipi_handler_func ipi_handlers[IPI_SGI_COUNT] = { NULL };
static void* ipi_handler_data[IPI_SGI_COUNT] = { NULL };

// Initialize IPI subsystem
static status_t
gic_init_ipi_subsystem(void)
{
    // Initialize per-CPU IPI locks
    for (int i = 0; i < IPI_MAX_CPUS; i++) {
        B_INITIALIZE_SPINLOCK(&gic_state.ipi_locks[i]);
        gic_state.ipi_pending[i] = 0;
    }
    
    // Initialize call data
    gic_state.call_data.function = NULL;
    gic_state.call_data.data = NULL;
    gic_state.call_data.call_count = &gic_state.call_count;
    gic_state.call_data.finished_count = &gic_state.finished_count;
    gic_state.call_data.wait_flag = NULL;
    
    gic_state.call_active = 0;
    gic_state.call_count = 0;
    gic_state.finished_count = 0;
    
    dprintf("GIC: IPI subsystem initialized\n");
    return B_OK;
}

// Register an IPI handler for a specific SGI
status_t
gic_register_ipi_handler(uint32 ipi_type, ipi_handler_func handler, void* data)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (ipi_type >= IPI_SGI_COUNT) {
        return B_BAD_VALUE;
    }
    
    if (handler == NULL) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    if (ipi_handlers[ipi_type] != NULL) {
        dprintf("GIC: IPI handler %u already registered\n", ipi_type);
        return B_BUSY;
    }
    
    ipi_handlers[ipi_type] = handler;
    ipi_handler_data[ipi_type] = data;
    
    dprintf("GIC: Registered IPI handler for type %u\n", ipi_type);
    return B_OK;
}

// Unregister an IPI handler
status_t
gic_unregister_ipi_handler(uint32 ipi_type)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (ipi_type >= IPI_SGI_COUNT) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    ipi_handlers[ipi_type] = NULL;
    ipi_handler_data[ipi_type] = NULL;
    
    dprintf("GIC: Unregistered IPI handler for type %u\n", ipi_type);
    return B_OK;
}

// Low-level IPI sending function
static status_t
gic_send_ipi_raw(uint32 target_cpu, uint32 ipi_id)
{
    if (gic_state.version == GIC_VERSION_V2) {
        // Use GICD_SGIR register for GICv2
        uint32 sgir = (1U << (16 + target_cpu)) | ipi_id;
        gic_write_distributor(GICD_SGIR, sgir);
        
        // Memory barrier to ensure the IPI is sent
        __asm__ volatile("dsb sy" ::: "memory");
        __asm__ volatile("isb" ::: "memory");
        
    } else if (gic_state.version >= GIC_VERSION_V3) {
        // GICv3+ uses system registers
        // Construct SGI1R value: Aff3.Aff2.Aff1.<RS>.TargetList.INTID
        uint64 sgir_val = ((uint64)ipi_id) | 
                         ((uint64)(1ULL << target_cpu) << 16);
        
        // Write to ICC_SGI1R_EL1 system register
        __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"(sgir_val));
        
        // Memory barrier to ensure the IPI is sent
        __asm__ volatile("dsb sy" ::: "memory");
        __asm__ volatile("isb" ::: "memory");
        
    } else {
        return B_NOT_SUPPORTED;
    }
    
    return B_OK;
}

// Send an IPI to a specific CPU
status_t
gic_send_ipi(uint32 target_cpu, uint32 ipi_type)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (ipi_type >= IPI_SGI_COUNT) {
        return B_BAD_VALUE;
    }
    
    if (target_cpu >= gic_state.max_cpus) {
        return B_BAD_VALUE;
    }
    
    // Mark IPI as pending for target CPU
    InterruptsSpinLocker cpu_locker(gic_state.ipi_locks[target_cpu]);
    gic_state.ipi_pending[target_cpu] |= (1U << ipi_type);
    cpu_locker.Unlock();
    
    // Send the actual SGI
    status_t result = gic_send_ipi_raw(target_cpu, gic_state.ipi_base + ipi_type);
    
    if (result != B_OK) {
        // Clear pending flag on failure
        InterruptsSpinLocker retry_locker(gic_state.ipi_locks[target_cpu]);
        gic_state.ipi_pending[target_cpu] &= ~(1U << ipi_type);
    }
    
    return result;
}

// Broadcast an IPI to all CPUs (excluding sender)
status_t
gic_broadcast_ipi(uint32 ipi_type)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (ipi_type >= IPI_SGI_COUNT) {
        return B_BAD_VALUE;
    }
    
    uint32 current_cpu = smp_get_current_cpu();
    status_t overall_result = B_OK;
    
    if (gic_state.version == GIC_VERSION_V2) {
        // Mark IPI as pending for all CPUs except current
        for (uint32 cpu = 0; cpu < gic_state.max_cpus; cpu++) {
            if (cpu != current_cpu) {
                InterruptsSpinLocker cpu_locker(gic_state.ipi_locks[cpu]);
                gic_state.ipi_pending[cpu] |= (1U << ipi_type);
            }
        }
        
        // Use broadcast SGI for GICv2
        uint32 sgir = (1U << 24) | (gic_state.ipi_base + ipi_type);
        gic_write_distributor(GICD_SGIR, sgir);
        
        // Memory barrier
        __asm__ volatile("dsb sy" ::: "memory");
        __asm__ volatile("isb" ::: "memory");
        
    } else {
        // For GICv3+, send individual IPIs
        for (uint32 cpu = 0; cpu < gic_state.max_cpus; cpu++) {
            if (cpu != current_cpu) {
                status_t result = gic_send_ipi(cpu, ipi_type);
                if (result != B_OK) {
                    overall_result = result;
                }
            }
        }
    }
    
    return overall_result;
}

// Send IPI to a specific set of CPUs
status_t
gic_send_ipi_mask(uint32 cpu_mask, uint32 ipi_type)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (ipi_type >= IPI_SGI_COUNT) {
        return B_BAD_VALUE;
    }
    
    status_t overall_result = B_OK;
    
    for (uint32 cpu = 0; cpu < gic_state.max_cpus; cpu++) {
        if (cpu_mask & (1U << cpu)) {
            status_t result = gic_send_ipi(cpu, ipi_type);
            if (result != B_OK) {
                overall_result = result;
            }
        }
    }
    
    return overall_result;
}

// Handle incoming IPI
void
gic_handle_ipi(uint32 cpu, uint32 ipi_id)
{
    if (!gic_state.initialized) {
        return;
    }
    
    // Convert SGI ID back to IPI type
    if (ipi_id < gic_state.ipi_base || 
        ipi_id >= (gic_state.ipi_base + IPI_SGI_COUNT)) {
        dprintf("GIC: Invalid IPI SGI %u on CPU %u\n", ipi_id, cpu);
        return;
    }
    
    uint32 ipi_type = ipi_id - gic_state.ipi_base;
    
    // Clear pending flag
    InterruptsSpinLocker cpu_locker(gic_state.ipi_locks[cpu]);
    gic_state.ipi_pending[cpu] &= ~(1U << ipi_type);
    cpu_locker.Unlock();
    
    // Call registered handler
    if (ipi_handlers[ipi_type] != NULL) {
        ipi_handlers[ipi_type](cpu, ipi_handler_data[ipi_type]);
    } else {
        dprintf("GIC: No handler for IPI type %u on CPU %u\n", ipi_type, cpu);
    }
}

/*
 * Cross-CPU Function Call Support
 */

// Default IPI handlers for common operations
static void
ipi_reschedule_handler(uint32 cpu, void* data)
{
    // Trigger reschedule on this CPU
    // This would integrate with Haiku's scheduler
    dprintf("GIC: Reschedule IPI on CPU %u\n", cpu);
}

static void
ipi_function_call_handler(uint32 cpu, void* data)
{
    // Handle cross-CPU function call
    if (gic_state.call_data.function != NULL) {
        gic_state.call_data.function(gic_state.call_data.data);
        
        // Increment finished count
        atomic_add(&gic_state.finished_count, 1);
    }
}

static void
ipi_function_call_sync_handler(uint32 cpu, void* data)
{
    // Handle synchronous cross-CPU function call
    ipi_function_call_handler(cpu, data);
    
    // Signal completion if wait flag is set
    if (gic_state.call_data.wait_flag != NULL) {
        *gic_state.call_data.wait_flag = true;
    }
}

// Execute a function on specific CPUs
status_t
gic_call_function_on_cpus(uint32 cpu_mask, void (*function)(void*), void* data, bool wait)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (function == NULL) {
        return B_BAD_VALUE;
    }
    
    // Check if another call is in progress
    if (atomic_test_and_set(&gic_state.call_active, 1, 0) != 0) {
        return B_BUSY;
    }
    
    // Set up call data
    gic_state.call_data.function = function;
    gic_state.call_data.data = data;
    
    // Count target CPUs
    uint32 target_count = 0;
    for (uint32 cpu = 0; cpu < gic_state.max_cpus; cpu++) {
        if (cpu_mask & (1U << cpu)) {
            target_count++;
        }
    }
    
    gic_state.call_count = target_count;
    gic_state.finished_count = 0;
    
    // Memory barrier to ensure call data is visible
    __asm__ volatile("dsb sy" ::: "memory");
    
    // Send IPIs to target CPUs
    uint32 ipi_type = wait ? IPI_CALL_FUNCTION_SYNC : IPI_CALL_FUNCTION;
    status_t result = gic_send_ipi_mask(cpu_mask, ipi_type);
    
    if (result != B_OK) {
        // Clear call active flag on error
        gic_state.call_active = 0;
        return result;
    }
    
    // Wait for completion if requested
    if (wait) {
        bigtime_t timeout = system_time() + 1000000; // 1 second timeout
        
        while (gic_state.finished_count < gic_state.call_count) {
            if (system_time() > timeout) {
                dprintf("GIC: Function call timeout (completed %d/%d)\n",
                       gic_state.finished_count, gic_state.call_count);
                break;
            }
            cpu_pause();
        }
    }
    
    // Clear call active flag
    gic_state.call_active = 0;
    
    return B_OK;
}

// Execute a function on all CPUs except current
status_t
gic_call_function_on_all_cpus(void (*function)(void*), void* data, bool wait)
{
    uint32 current_cpu = smp_get_current_cpu();
    uint32 cpu_mask = 0;
    
    // Build mask excluding current CPU
    for (uint32 cpu = 0; cpu < gic_state.max_cpus; cpu++) {
        if (cpu != current_cpu) {
            cpu_mask |= (1U << cpu);
        }
    }
    
    return gic_call_function_on_cpus(cpu_mask, function, data, wait);
}

// Execute a function on a single CPU
status_t
gic_call_function_on_cpu(uint32 target_cpu, void (*function)(void*), void* data, bool wait)
{
    if (target_cpu >= gic_state.max_cpus) {
        return B_BAD_VALUE;
    }
    
    return gic_call_function_on_cpus(1U << target_cpu, function, data, wait);
}

/*
 * Synchronization and Utility Functions
 */

// Request reschedule on specific CPUs
status_t
gic_request_reschedule(uint32 cpu_mask)
{
    return gic_send_ipi_mask(cpu_mask, IPI_RESCHEDULE);
}

// Request reschedule on all CPUs
status_t
gic_request_reschedule_all(void)
{
    return gic_broadcast_ipi(IPI_RESCHEDULE);
}

// TLB flush support
static void
ipi_tlb_flush_handler(uint32 cpu, void* data)
{
    // Perform TLB flush on this CPU
    // This would integrate with ARM64 TLB management
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    dprintf("GIC: TLB flush IPI on CPU %u\n", cpu);
}

// Request TLB flush on specific CPUs
status_t
gic_request_tlb_flush(uint32 cpu_mask, bool wait)
{
    status_t result = gic_send_ipi_mask(cpu_mask, IPI_TLB_FLUSH);
    
    if (wait && result == B_OK) {
        // Wait a bit for TLB flush to complete
        spin(10); // 10 microseconds
    }
    
    return result;
}

// Cache flush support
static void
ipi_cache_flush_handler(uint32 cpu, void* data)
{
    // Perform cache flush on this CPU
    // This would integrate with ARM64 cache management
    __asm__ volatile("dc cisw, xzr" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    dprintf("GIC: Cache flush IPI on CPU %u\n", cpu);
}

// Request cache flush on specific CPUs
status_t
gic_request_cache_flush(uint32 cpu_mask, bool wait)
{
    status_t result = gic_send_ipi_mask(cpu_mask, IPI_CACHE_FLUSH);
    
    if (wait && result == B_OK) {
        // Wait for cache flush to complete
        spin(50); // 50 microseconds
    }
    
    return result;
}

// Check if any IPIs are pending for the current CPU
bool
gic_has_pending_ipi(uint32 cpu)
{
    if (cpu >= IPI_MAX_CPUS) {
        return false;
    }
    
    InterruptsSpinLocker locker(gic_state.ipi_locks[cpu]);
    return gic_state.ipi_pending[cpu] != 0;
}

// Get pending IPI mask for a CPU
uint32
gic_get_pending_ipi_mask(uint32 cpu)
{
    if (cpu >= IPI_MAX_CPUS) {
        return 0;
    }
    
    InterruptsSpinLocker locker(gic_state.ipi_locks[cpu]);
    return gic_state.ipi_pending[cpu];
}

// Clear all pending IPIs for a CPU (emergency use)
void
gic_clear_pending_ipis(uint32 cpu)
{
    if (cpu >= IPI_MAX_CPUS) {
        return;
    }
    
    InterruptsSpinLocker locker(gic_state.ipi_locks[cpu]);
    gic_state.ipi_pending[cpu] = 0;
    
    dprintf("GIC: Cleared all pending IPIs for CPU %u\n", cpu);
}

/*
 * GIC Initialization and Management
 */

status_t
gic_init(addr_t distributor_base, addr_t cpu_interface_base, addr_t redistributor_base)
{
    status_t result;
    
    dprintf("GIC: Initializing Generic Interrupt Controller\n");
    
    if (gic_state.initialized) {
        dprintf("GIC: Already initialized\n");
        return B_OK;
    }
    
    // Map distributor registers
    result = arch_mmio_map_range("gic_distributor", distributor_base,
                                B_PAGE_SIZE * 4, 0, &gic_state.distributor_base);
    if (result != B_OK) {
        dprintf("GIC: Failed to map distributor registers: %s\n", strerror(result));
        return result;
    }
    
    // Detect GIC version and features
    gic_state.version = gic_detect_version();
    if (gic_state.version == GIC_VERSION_UNKNOWN) {
        dprintf("GIC: Unknown or unsupported GIC version\n");
        arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
        return B_NOT_SUPPORTED;
    }
    
    gic_detect_features();
    
    // Map version-specific interfaces
    if (gic_state.version == GIC_VERSION_V2) {
        if (cpu_interface_base == 0) {
            dprintf("GIC: CPU interface base required for GICv2\n");
            arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
            return B_BAD_VALUE;
        }
        
        result = arch_mmio_map_range("gic_cpu_interface", cpu_interface_base,
                                    B_PAGE_SIZE, 0, &gic_state.cpu_interface_base);
        if (result != B_OK) {
            dprintf("GIC: Failed to map CPU interface: %s\n", strerror(result));
            arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
            return result;
        }
    } else {
        // GICv3+ uses redistributor
        if (redistributor_base != 0) {
            result = arch_mmio_map_range("gic_redistributor", redistributor_base,
                                        B_PAGE_SIZE * 128, 0, &gic_state.redistributor_base);
            if (result != B_OK) {
                dprintf("GIC: Failed to map redistributor: %s\n", strerror(result));
                arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
                return result;
            }
        }
    }
    
    // Initialize distributor
    result = gic_init_distributor();
    if (result != B_OK) {
        dprintf("GIC: Failed to initialize distributor: %s\n", strerror(result));
        // Cleanup mappings
        arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
        if (gic_state.cpu_interface_base != 0) {
            arch_mmio_unmap_range(gic_state.cpu_interface_base, B_PAGE_SIZE);
        }
        if (gic_state.redistributor_base != 0) {
            arch_mmio_unmap_range(gic_state.redistributor_base, B_PAGE_SIZE * 128);
        }
        return result;
    }
    
    // Initialize CPU interface for boot CPU
    result = gic_init_cpu_interface(0);
    if (result != B_OK) {
        dprintf("GIC: Failed to initialize CPU interface: %s\n", strerror(result));
        return result;
    }
    
    // Initialize redistributor for boot CPU (GICv3+ only)
    if (gic_state.version >= GIC_VERSION_V3) {
        result = gic_init_redistributor(0);
        if (result != B_OK) {
            dprintf("GIC: Failed to initialize redistributor: %s\n", strerror(result));
            return result;
        }
    }
    
    // Initialize IPI subsystem
    result = gic_init_ipi_subsystem();
    if (result != B_OK) {
        dprintf("GIC: Failed to initialize IPI subsystem: %s\n", strerror(result));
        return result;
    }
    
    // Configure IPI base
    gic_state.ipi_base = IPI_SGI_BASE;
    
    // Register default IPI handlers
    gic_register_ipi_handler(IPI_RESCHEDULE, ipi_reschedule_handler, NULL);
    gic_register_ipi_handler(IPI_CALL_FUNCTION, ipi_function_call_handler, NULL);
    gic_register_ipi_handler(IPI_CALL_FUNCTION_SYNC, ipi_function_call_sync_handler, NULL);
    gic_register_ipi_handler(IPI_TLB_FLUSH, ipi_tlb_flush_handler, NULL);
    gic_register_ipi_handler(IPI_CACHE_FLUSH, ipi_cache_flush_handler, NULL);
    
    // Enable SGI interrupts for IPI support
    for (uint32 sgi = gic_state.ipi_base; sgi < (gic_state.ipi_base + IPI_SGI_COUNT); sgi++) {
        gic_enable_interrupt(sgi);
    }
    
    gic_state.initialized = true;
    
    dprintf("GIC: Successfully initialized GICv%u with %u interrupts\n",
            gic_state.version, gic_state.max_interrupts);
    
    return B_OK;
}

status_t
gic_init_secondary_cpu(uint32 cpu)
{
    status_t result;
    
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    dprintf("GIC: Initializing CPU %u interface\n", cpu);
    
    // Initialize CPU interface
    result = gic_init_cpu_interface(cpu);
    if (result != B_OK) {
        dprintf("GIC: Failed to initialize CPU %u interface: %s\n", cpu, strerror(result));
        return result;
    }
    
    // Initialize redistributor for this CPU (GICv3+ only)
    if (gic_state.version >= GIC_VERSION_V3) {
        result = gic_init_redistributor(cpu);
        if (result != B_OK) {
            dprintf("GIC: Failed to initialize CPU %u redistributor: %s\n", cpu, strerror(result));
            return result;
        }
    }
    
    dprintf("GIC: CPU %u interface initialized successfully\n", cpu);
    
    return B_OK;
}

void
gic_cleanup(void)
{
    if (!gic_state.initialized) {
        return;
    }
    
    dprintf("GIC: Cleaning up driver\n");
    
    // Disable distributor
    gic_write_distributor(GICD_CTLR, 0);
    
    // Unmap memory regions
    if (gic_state.distributor_base != 0) {
        arch_mmio_unmap_range(gic_state.distributor_base, B_PAGE_SIZE * 4);
        gic_state.distributor_base = 0;
    }
    
    if (gic_state.cpu_interface_base != 0) {
        arch_mmio_unmap_range(gic_state.cpu_interface_base, B_PAGE_SIZE);
        gic_state.cpu_interface_base = 0;
    }
    
    if (gic_state.redistributor_base != 0) {
        arch_mmio_unmap_range(gic_state.redistributor_base, B_PAGE_SIZE * 128);
        gic_state.redistributor_base = 0;
    }
    
    gic_state.initialized = false;
    gic_state.version = GIC_VERSION_UNKNOWN;
    
    dprintf("GIC: Driver cleanup complete\n");
}

/*
 * Debug and Information Functions
 */

void
gic_dump_state(void)
{
    if (!gic_state.initialized) {
        dprintf("GIC: Driver not initialized\n");
        return;
    }
    
    dprintf("GIC Driver State:\n");
    dprintf("================\n");
    dprintf("Version:           GICv%u\n", gic_state.version);
    dprintf("Max interrupts:    %u\n", gic_state.max_interrupts);
    dprintf("Max CPUs:          %u\n", gic_state.max_cpus);
    dprintf("Priority mask:     0x%02X\n", gic_state.priority_mask);
    dprintf("IPI base:          %u (using SGIs %u-%u)\n", 
            gic_state.ipi_base, gic_state.ipi_base, gic_state.ipi_base + IPI_SGI_COUNT - 1);
    
    // Display IPI statistics
    dprintf("\nIPI Statistics:\n");
    for (uint32 cpu = 0; cpu < min_c(gic_state.max_cpus, 8); cpu++) {
        uint32 pending = gic_get_pending_ipi_mask(cpu);
        dprintf("  CPU %u pending IPIs: 0x%02X\n", cpu, pending);
    }
    
    dprintf("Active function call: %s\n", gic_state.call_active ? "yes" : "no");
    if (gic_state.call_active) {
        dprintf("  Call count: %d, Finished: %d\n", 
                gic_state.call_count, gic_state.finished_count);
    }
    dprintf("Security mode:     %s\n", gic_state.secure_mode ? "Secure" : "Non-secure");
    dprintf("Distributor base:  0x%lx\n", gic_state.distributor_base);
    if (gic_state.version == GIC_VERSION_V2) {
        dprintf("CPU interface base: 0x%lx\n", gic_state.cpu_interface_base);
    } else {
        dprintf("Redistributor base: 0x%lx\n", gic_state.redistributor_base);
    }
    
    // Display distributor control register
    uint32 ctlr = gic_read_distributor(GICD_CTLR);
    dprintf("Distributor CTLR:  0x%08X\n", ctlr);
    
    if (gic_state.version == GIC_VERSION_V2) {
        // Display CPU interface control register
        uint32 cpu_ctlr = gic_read_cpu_interface(GICC_CTLR);
        dprintf("CPU interface CTLR: 0x%08X\n", cpu_ctlr);
    }
}

uint32
gic_get_version(void)
{
    return gic_state.version;
}

uint32
gic_get_max_interrupts(void)
{
    return gic_state.max_interrupts;
}

bool
gic_is_initialized(void)
{
    return gic_state.initialized;
}

/*
 * Enhanced GICv3 Redistributor Management
 */

static status_t
gic_init_redistributor(uint32 cpu)
{
    if (gic_state.version < GIC_VERSION_V3) {
        return B_OK; // No redistributor in GICv2
    }
    
    if (gic_state.redistributor_base == 0) {
        dprintf("GIC: No redistributor base configured\n");
        return B_BAD_VALUE;
    }
    
    dprintf("GIC: Initializing redistributor for CPU %u\n", cpu);
    
    // Read redistributor type register to verify it exists
    uint64_t typer = gic_read_redistributor(cpu, GICR_TYPER);
    uint32_t affinity = (typer >> 32) & 0xFFFFFFFF;
    
    dprintf("GIC: CPU %u redistributor affinity: 0x%08X\n", cpu, affinity);
    
    // Wake up the redistributor
    uint32_t waker = gic_read_redistributor(cpu, GICR_WAKER);
    waker &= ~GICR_WAKER_PROCESSORSL; // Clear processor sleep bit
    gic_write_redistributor(cpu, GICR_WAKER, waker);
    
    // Wait for children to wake up
    bigtime_t timeout = system_time() + 100000; // 100ms timeout
    do {
        waker = gic_read_redistributor(cpu, GICR_WAKER);
        if (!(waker & GICR_WAKER_CHILDRENASK)) {
            break; // Children are awake
        }
        spin(1);
    } while (system_time() < timeout);
    
    if (waker & GICR_WAKER_CHILDRENASK) {
        dprintf("GIC: Warning - redistributor children did not wake up for CPU %u\n", cpu);
    }
    
    // Configure SGI and PPI interrupts for this CPU
    
    // Set all SGIs and PPIs to Group 1 (non-secure)
    gic_write_redistributor(cpu, GICR_IGROUPR0, 0xFFFFFFFF);
    
    // Set all SGIs and PPIs to lowest priority
    for (uint32 i = 0; i < 32; i += 4) {
        uint32 priority = (GIC_PRIORITY_LOWEST << 24) | (GIC_PRIORITY_LOWEST << 16) |
                         (GIC_PRIORITY_LOWEST << 8) | GIC_PRIORITY_LOWEST;
        gic_write_redistributor(cpu, GICR_IPRIORITYR + i, priority);
    }
    
    // Disable all SGIs and PPIs initially
    gic_write_redistributor(cpu, GICR_ICENABLER0, 0xFFFFFFFF);
    
    // Clear all pending SGIs and PPIs
    gic_write_redistributor(cpu, GICR_ICPENDR0, 0xFFFFFFFF);
    
    // Clear all active SGIs and PPIs
    gic_write_redistributor(cpu, GICR_ICACTIVER0, 0xFFFFFFFF);
    
    // Configure SGI and PPI trigger types
    // SGIs (0-15) are always edge-triggered (register reads as 0xAAAA)
    // PPIs (16-31) can be level or edge-triggered
    gic_write_redistributor(cpu, GICR_ICFGR0, 0xAAAAAAAA); // SGIs edge-triggered
    gic_write_redistributor(cpu, GICR_ICFGR1, 0x00000000); // PPIs level-triggered
    
    dprintf("GIC: CPU %u redistributor initialized successfully\n", cpu);
    
    return B_OK;
}

status_t
gic_enable_sgi_ppi(uint32 cpu, uint32 irq)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (gic_state.version < GIC_VERSION_V3) {
        // For GICv2, SGI/PPI control is through distributor
        return gic_enable_interrupt(irq);
    }
    
    if (irq > GIC_PPI_MAX) {
        return B_BAD_VALUE; // Only SGIs and PPIs
    }
    
    if (cpu >= gic_state.max_cpus) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 bit = 1U << irq;
    gic_write_redistributor(cpu, GICR_ISENABLER0, bit);
    
    dprintf("GIC: Enabled SGI/PPI %u on CPU %u\n", irq, cpu);
    
    return B_OK;
}

status_t
gic_disable_sgi_ppi(uint32 cpu, uint32 irq)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (gic_state.version < GIC_VERSION_V3) {
        // For GICv2, SGI/PPI control is through distributor
        return gic_disable_interrupt(irq);
    }
    
    if (irq > GIC_PPI_MAX) {
        return B_BAD_VALUE; // Only SGIs and PPIs
    }
    
    if (cpu >= gic_state.max_cpus) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 bit = 1U << irq;
    gic_write_redistributor(cpu, GICR_ICENABLER0, bit);
    
    dprintf("GIC: Disabled SGI/PPI %u on CPU %u\n", irq, cpu);
    
    return B_OK;
}

status_t
gic_set_sgi_ppi_priority(uint32 cpu, uint32 irq, uint8 priority)
{
    if (!gic_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (gic_state.version < GIC_VERSION_V3) {
        // For GICv2, SGI/PPI priority is through distributor
        return gic_set_interrupt_priority(irq, priority);
    }
    
    if (irq > GIC_PPI_MAX) {
        return B_BAD_VALUE; // Only SGIs and PPIs
    }
    
    if (cpu >= gic_state.max_cpus) {
        return B_BAD_VALUE;
    }
    
    InterruptsSpinLocker locker(gic_state.gic_lock);
    
    uint32 reg = GICR_IPRIORITYR + irq;
    gic_write_redistributor(cpu, reg, priority);
    
    return B_OK;
}

void
gic_dump_redistributor_state(uint32 cpu)
{
    if (!gic_state.initialized || gic_state.version < GIC_VERSION_V3) {
        dprintf("GIC: No redistributor available\n");
        return;
    }
    
    if (cpu >= gic_state.max_cpus) {
        dprintf("GIC: Invalid CPU %u\n", cpu);
        return;
    }
    
    dprintf("GIC Redistributor State for CPU %u:\n", cpu);
    dprintf("===================================\n");
    
    // Read redistributor registers
    uint32_t ctlr = gic_read_redistributor(cpu, GICR_CTLR);
    uint64_t typer = gic_read_redistributor(cpu, GICR_TYPER);
    uint32_t waker = gic_read_redistributor(cpu, GICR_WAKER);
    uint32_t statusr = gic_read_redistributor(cpu, GICR_STATUSR);
    
    dprintf("Control:    0x%08X\n", ctlr);
    dprintf("Type:       0x%016llX\n", typer);
    dprintf("  Affinity: 0x%08X\n", (uint32_t)(typer >> 32));
    dprintf("  Last:     %s\n", (typer & GICR_TYPER_LAST) ? "yes" : "no");
    dprintf("  LPIs:     %s\n", (typer & GICR_TYPER_PLPIS) ? "supported" : "not supported");
    dprintf("Waker:      0x%08X\n", waker);
    dprintf("  Sleep:    %s\n", (waker & GICR_WAKER_PROCESSORSL) ? "yes" : "no");
    dprintf("  Children: %s\n", (waker & GICR_WAKER_CHILDRENASK) ? "asleep" : "awake");
    dprintf("Status:     0x%08X\n", statusr);
    
    // SGI/PPI status
    uint32_t enabled = gic_read_redistributor(cpu, GICR_ISENABLER0);
    uint32_t pending = gic_read_redistributor(cpu, GICR_ISPENDR0);
    uint32_t active = gic_read_redistributor(cpu, GICR_ISACTIVER0);
    
    dprintf("SGI/PPI Enabled:  0x%08X\n", enabled);
    dprintf("SGI/PPI Pending:  0x%08X\n", pending);
    dprintf("SGI/PPI Active:   0x%08X\n", active);
}