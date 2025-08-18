/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * BCM2712 Device Tree Integration
 * 
 * This module provides device tree parsing and configuration for the
 * Broadcom BCM2712 (Raspberry Pi 5) system-on-chip. It detects hardware
 * capabilities, configures peripherals, and sets up the 54MHz System Timer
 * based on device tree information.
 *
 * Key Features:
 * - BCM2712 hardware detection and validation
 * - System Timer device tree parsing
 * - Clock frequency detection and configuration
 * - Peripheral base address discovery
 * - Interrupt routing configuration
 * - Raspberry Pi 5 board identification
 */

#include <kernel/kernel.h>
#include <kernel/debug.h>
#include <string.h>

#include <arch/arm64/arch_device_tree.h>
#include <arch/arm64/arch_bcm2712.h>

// Device Tree Compatibility Strings
#define BCM2712_DT_COMPAT_MAIN      "brcm,bcm2712"
#define BCM2712_DT_COMPAT_RPI5      "raspberrypi,5-model-b"
#define BCM2712_DT_COMPAT_CM5       "raspberrypi,5-compute-module"
#define BCM2712_DT_COMPAT_TIMER     "brcm,bcm2712-system-timer"
#define BCM2712_DT_COMPAT_CPRMAN    "brcm,bcm2712-cprman"

// Device Tree Property Names
#define DT_PROP_COMPATIBLE          "compatible"
#define DT_PROP_REG                 "reg"
#define DT_PROP_INTERRUPTS          "interrupts"
#define DT_PROP_CLOCK_FREQUENCY     "clock-frequency"
#define DT_PROP_CLOCK_NAMES         "clock-names"
#define DT_PROP_CLOCKS              "clocks"
#define DT_PROP_STATUS              "status"

// BCM2712 Hardware Detection State
struct bcm2712_dt_state {
    bool bcm2712_detected;
    bool raspberry_pi5;
    bool compute_module5;
    bool timer_found;
    bool cprman_found;
    
    // Hardware configuration
    phys_addr_t timer_base;
    size_t timer_size;
    uint32_t timer_frequency;
    uint32_t timer_interrupts[BCM2712_TIMER_CHANNELS];
    
    phys_addr_t cprman_base;
    size_t cprman_size;
    uint32_t crystal_frequency;
    
    // Board information
    uint32_t board_revision;
    uint32_t memory_size;
    char board_model[64];
    char board_serial[17]; // 16 hex digits + null
};

static struct bcm2712_dt_state bcm2712_dt = {
    .bcm2712_detected = false,
    .raspberry_pi5 = false,
    .compute_module5 = false,
    .timer_found = false,
    .cprman_found = false,
    .timer_base = 0,
    .timer_size = 0,
    .timer_frequency = BCM2712_TIMER_FREQ,
    .cprman_base = 0,
    .cprman_size = 0,
    .crystal_frequency = BCM2712_TIMER_FREQ,
    .board_revision = 0,
    .memory_size = 0,
    .board_model = "",
    .board_serial = ""
};

// ============================================================================
// Device Tree Parsing Functions
// ============================================================================

/*!
 * Check if device tree node has specified compatibility string
 */
static bool
bcm2712_dt_node_is_compatible(device_tree_node_t* node, const char* compat)
{
    if (node == NULL || compat == NULL) {
        return false;
    }
    
    for (int i = 0; i < node->compatible_count; i++) {
        if (strcmp(node->compatible[i], compat) == 0) {
            return true;
        }
    }
    
    return false;
}

/*!
 * Parse timer node from device tree
 */
static status_t
bcm2712_dt_parse_timer_node(device_tree_node_t* node)
{
    if (!bcm2712_dt_node_is_compatible(node, BCM2712_DT_COMPAT_TIMER)) {
        return B_NAME_NOT_FOUND;
    }
    
    dprintf("BCM2712: Found system timer in device tree\n");
    
    // Parse register information
    if (node->reg_count > 0) {
        bcm2712_dt.timer_base = node->reg[0].address;
        bcm2712_dt.timer_size = node->reg[0].size;
        
        dprintf("BCM2712: Timer base: 0x%lx, size: 0x%lx\n", 
                bcm2712_dt.timer_base, bcm2712_dt.timer_size);
    } else {
        dprintf("BCM2712: Timer node missing register information\n");
        return B_BAD_DATA;
    }
    
    // Parse interrupt information
    int irq_count = 0;
    for (int i = 0; i < node->interrupt_count && i < BCM2712_TIMER_CHANNELS; i++) {
        bcm2712_dt.timer_interrupts[i] = node->interrupts[i].number;
        irq_count++;
        
        dprintf("BCM2712: Timer channel %d IRQ: %u\n", i, 
                bcm2712_dt.timer_interrupts[i]);
    }
    
    if (irq_count == 0) {
        dprintf("BCM2712: Warning - no timer interrupts found\n");
    }
    
    // Parse clock frequency if available
    // Note: This would typically come from a clock-frequency property
    // For now, we use the default 54MHz
    bcm2712_dt.timer_frequency = BCM2712_TIMER_FREQ;
    
    bcm2712_dt.timer_found = true;
    
    return B_OK;
}

/*!
 * Parse CPRMAN (Clock and Power Management) node
 */
static status_t
bcm2712_dt_parse_cprman_node(device_tree_node_t* node)
{
    if (!bcm2712_dt_node_is_compatible(node, BCM2712_DT_COMPAT_CPRMAN)) {
        return B_NAME_NOT_FOUND;
    }
    
    dprintf("BCM2712: Found CPRMAN in device tree\n");
    
    // Parse register information
    if (node->reg_count > 0) {
        bcm2712_dt.cprman_base = node->reg[0].address;
        bcm2712_dt.cprman_size = node->reg[0].size;
        
        dprintf("BCM2712: CPRMAN base: 0x%lx, size: 0x%lx\n", 
                bcm2712_dt.cprman_base, bcm2712_dt.cprman_size);
    } else {
        dprintf("BCM2712: CPRMAN node missing register information\n");
        return B_BAD_DATA;
    }
    
    // The crystal frequency (typically 54MHz) would be parsed here
    // from clock properties if available
    bcm2712_dt.crystal_frequency = BCM2712_TIMER_FREQ;
    
    bcm2712_dt.cprman_found = true;
    
    return B_OK;
}

/*!
 * Parse main BCM2712 node and detect board type
 */
static status_t
bcm2712_dt_parse_main_node(device_tree_node_t* node)
{
    // Check for BCM2712 compatibility
    if (!bcm2712_dt_node_is_compatible(node, BCM2712_DT_COMPAT_MAIN)) {
        return B_NAME_NOT_FOUND;
    }
    
    dprintf("BCM2712: Detected BCM2712 SoC\n");
    bcm2712_dt.bcm2712_detected = true;
    
    // Check for specific board types
    if (bcm2712_dt_node_is_compatible(node, BCM2712_DT_COMPAT_RPI5)) {
        bcm2712_dt.raspberry_pi5 = true;
        strlcpy(bcm2712_dt.board_model, "Raspberry Pi 5", 
                sizeof(bcm2712_dt.board_model));
        dprintf("BCM2712: Detected Raspberry Pi 5\n");
    } else if (bcm2712_dt_node_is_compatible(node, BCM2712_DT_COMPAT_CM5)) {
        bcm2712_dt.compute_module5 = true;
        strlcpy(bcm2712_dt.board_model, "Compute Module 5", 
                sizeof(bcm2712_dt.board_model));
        dprintf("BCM2712: Detected Compute Module 5\n");
    } else {
        strlcpy(bcm2712_dt.board_model, "BCM2712 Board", 
                sizeof(bcm2712_dt.board_model));
        dprintf("BCM2712: Generic BCM2712 board\n");
    }
    
    return B_OK;
}

// ============================================================================
// Hardware Detection and Validation
// ============================================================================

/*!
 * Detect BCM2712 hardware from device tree
 */
status_t
bcm2712_detect_hardware(struct bcm2712_chip_info* info)
{
    if (info == NULL) {
        return B_BAD_VALUE;
    }
    
    // Initialize chip info structure
    memset(info, 0, sizeof(*info));
    
    if (!bcm2712_dt.bcm2712_detected) {
        dprintf("BCM2712: Hardware not detected\n");
        return B_NAME_NOT_FOUND;
    }
    
    // Fill in detected information
    info->die_id = BCM2712_DIE_ID;
    info->revision = bcm2712_dt.board_revision;
    info->manufacturer = 0x42726462; // "Brdb" for Broadcom
    info->memory_size = bcm2712_dt.memory_size;
    info->cpu_max_freq = BCM2712_MAX_CPU_FREQ;
    info->is_raspberry_pi5 = bcm2712_dt.raspberry_pi5;
    info->is_compute_module = bcm2712_dt.compute_module5;
    info->board_revision = bcm2712_dt.board_revision;
    
    // Default frequencies
    info->cpu_max_freq = 2400000000;  // 2.4 GHz
    info->gpu_freq = 800000000;       // 800 MHz (estimated)
    info->core_freq = 400000000;      // 400 MHz (estimated)
    
    dprintf("BCM2712: Hardware detection complete\n");
    dprintf("BCM2712: Board: %s\n", bcm2712_dt.board_model);
    dprintf("BCM2712: Memory: %u MB\n", info->memory_size / (1024 * 1024));
    
    return B_OK;
}

/*!
 * Get timer capabilities from detected hardware
 */
status_t
bcm2712_get_timer_caps(struct bcm2712_timer_caps* caps)
{
    if (caps == NULL) {
        return B_BAD_VALUE;
    }
    
    if (!bcm2712_dt.timer_found) {
        return B_NAME_NOT_FOUND;
    }
    
    // Fill in timer capabilities
    caps->frequency = bcm2712_dt.timer_frequency;
    caps->channels = BCM2712_TIMER_CHANNELS;
    caps->max_value = BCM2712_TIMER_MAX_VALUE;
    caps->has_64bit_counter = true;
    caps->has_interrupt = true;
    caps->resolution_nsec = BCM2712_NSEC_PER_TICK;
    caps->max_period_usec = BCM2712_TIMER_MAX_USEC;
    
    return B_OK;
}

/*!
 * Check if this is a compatible BCM2712 board
 */
bool
bcm2712_is_compatible_board(void)
{
    return bcm2712_dt.bcm2712_detected;
}

/*!
 * Get board revision number
 */
uint32_t
bcm2712_get_board_revision(void)
{
    return bcm2712_dt.board_revision;
}

/*!
 * Get total memory size
 */
uint32_t
bcm2712_get_memory_size(void)
{
    return bcm2712_dt.memory_size;
}

/*!
 * Get timer base address from device tree
 */
phys_addr_t
bcm2712_get_timer_base(void)
{
    if (bcm2712_dt.timer_found) {
        return bcm2712_dt.timer_base;
    }
    
    // Fall back to hardcoded address
    return BCM2712_SYSTIMER_BASE;
}

/*!
 * Get timer size from device tree
 */
size_t
bcm2712_get_timer_size(void)
{
    if (bcm2712_dt.timer_found && bcm2712_dt.timer_size > 0) {
        return bcm2712_dt.timer_size;
    }
    
    // Default size
    return 0x1000; // 4KB
}

/*!
 * Get timer frequency from device tree
 */
uint32_t
bcm2712_get_timer_frequency(void)
{
    return bcm2712_dt.timer_frequency;
}

/*!
 * Get timer interrupt numbers
 */
status_t
bcm2712_get_timer_interrupts(uint32_t* interrupts, size_t count)
{
    if (interrupts == NULL || count == 0) {
        return B_BAD_VALUE;
    }
    
    if (!bcm2712_dt.timer_found) {
        // Use default interrupt numbers
        for (size_t i = 0; i < count && i < BCM2712_TIMER_CHANNELS; i++) {
            interrupts[i] = BCM2712_IRQ_TIMER0 + i;
        }
        return B_OK;
    }
    
    // Use device tree information
    for (size_t i = 0; i < count && i < BCM2712_TIMER_CHANNELS; i++) {
        interrupts[i] = bcm2712_dt.timer_interrupts[i];
    }
    
    return B_OK;
}

// ============================================================================
// Device Tree Integration and Initialization
// ============================================================================

/*!
 * Parse BCM2712 device tree and configure hardware
 */
status_t
bcm2712_dt_init(kernel_args* args)
{
    status_t result;
    device_tree_node_t nodes[16];
    int found_nodes;
    
    dprintf("BCM2712: Initializing device tree support\n");
    
    // Initialize device tree parsing if not already done
    result = arch_device_tree_init(args);
    if (result != B_OK) {
        dprintf("BCM2712: Device tree initialization failed: %s\n", 
                strerror(result));
        return result;
    }
    
    // Look for BCM2712 main node
    found_nodes = arch_device_tree_find_compatible(BCM2712_DT_COMPAT_MAIN, 
                                                   nodes, sizeof(nodes)/sizeof(nodes[0]));
    if (found_nodes > 0) {
        result = bcm2712_dt_parse_main_node(&nodes[0]);
        if (result != B_OK) {
            dprintf("BCM2712: Failed to parse main node: %s\n", strerror(result));
        }
    }
    
    // Look for Raspberry Pi 5 specific node
    if (!bcm2712_dt.bcm2712_detected) {
        found_nodes = arch_device_tree_find_compatible(BCM2712_DT_COMPAT_RPI5, 
                                                       nodes, sizeof(nodes)/sizeof(nodes[0]));
        if (found_nodes > 0) {
            bcm2712_dt.bcm2712_detected = true;
            bcm2712_dt.raspberry_pi5 = true;
            strlcpy(bcm2712_dt.board_model, "Raspberry Pi 5", 
                    sizeof(bcm2712_dt.board_model));
            dprintf("BCM2712: Detected Raspberry Pi 5 from device tree\n");
        }
    }
    
    // Look for system timer
    found_nodes = arch_device_tree_find_compatible(BCM2712_DT_COMPAT_TIMER, 
                                                   nodes, sizeof(nodes)/sizeof(nodes[0]));
    if (found_nodes > 0) {
        result = bcm2712_dt_parse_timer_node(&nodes[0]);
        if (result != B_OK) {
            dprintf("BCM2712: Failed to parse timer node: %s\n", strerror(result));
        }
    }
    
    // Look for CPRMAN (Clock and Power Management)
    found_nodes = arch_device_tree_find_compatible(BCM2712_DT_COMPAT_CPRMAN, 
                                                   nodes, sizeof(nodes)/sizeof(nodes[0]));
    if (found_nodes > 0) {
        result = bcm2712_dt_parse_cprman_node(&nodes[0]);
        if (result != B_OK) {
            dprintf("BCM2712: Failed to parse CPRMAN node: %s\n", strerror(result));
        }
    }
    
    // If no device tree nodes found, check if we're running on known hardware
    if (!bcm2712_dt.bcm2712_detected) {
        // Try to detect by reading hardware registers or other methods
        // For now, we'll just report that no BCM2712 was found
        dprintf("BCM2712: No BCM2712 device tree nodes found\n");
        return B_NAME_NOT_FOUND;
    }
    
    dprintf("BCM2712: Device tree initialization complete\n");
    dprintf("BCM2712: Board: %s\n", bcm2712_dt.board_model);
    dprintf("BCM2712: Timer: %s (base: 0x%lx, freq: %u Hz)\n", 
            bcm2712_dt.timer_found ? "found" : "using defaults",
            bcm2712_dt.timer_base, bcm2712_dt.timer_frequency);
    dprintf("BCM2712: CPRMAN: %s (base: 0x%lx)\n", 
            bcm2712_dt.cprman_found ? "found" : "using defaults",
            bcm2712_dt.cprman_base);
    
    return B_OK;
}

/*!
 * Get board information string
 */
const char*
bcm2712_get_board_model(void)
{
    if (bcm2712_dt.board_model[0] != '\0') {
        return bcm2712_dt.board_model;
    }
    
    return "Unknown BCM2712 Board";
}

/*!
 * Check if BCM2712 timer was found in device tree
 */
bool
bcm2712_dt_timer_found(void)
{
    return bcm2712_dt.timer_found;
}

/*!
 * Check if BCM2712 CPRMAN was found in device tree
 */
bool
bcm2712_dt_cprman_found(void)
{
    return bcm2712_dt.cprman_found;
}

/*!
 * Dump device tree detection results
 */
void
bcm2712_dt_dump_state(void)
{
    dprintf("BCM2712 Device Tree State:\n");
    dprintf("=========================\n");
    dprintf("BCM2712 Detected:     %s\n", bcm2712_dt.bcm2712_detected ? "yes" : "no");
    dprintf("Raspberry Pi 5:       %s\n", bcm2712_dt.raspberry_pi5 ? "yes" : "no");
    dprintf("Compute Module 5:     %s\n", bcm2712_dt.compute_module5 ? "yes" : "no");
    dprintf("Board Model:          %s\n", bcm2712_dt.board_model);
    dprintf("Board Revision:       0x%08x\n", bcm2712_dt.board_revision);
    dprintf("Memory Size:          %u MB\n", bcm2712_dt.memory_size / (1024 * 1024));
    
    dprintf("\nTimer Configuration:\n");
    dprintf("Timer Found:          %s\n", bcm2712_dt.timer_found ? "yes" : "no");
    dprintf("Timer Base:           0x%lx\n", bcm2712_dt.timer_base);
    dprintf("Timer Size:           0x%lx\n", bcm2712_dt.timer_size);
    dprintf("Timer Frequency:      %u Hz\n", bcm2712_dt.timer_frequency);
    
    for (int i = 0; i < BCM2712_TIMER_CHANNELS; i++) {
        dprintf("Timer %d IRQ:          %u\n", i, bcm2712_dt.timer_interrupts[i]);
    }
    
    dprintf("\nClock Management:\n");
    dprintf("CPRMAN Found:         %s\n", bcm2712_dt.cprman_found ? "yes" : "no");
    dprintf("CPRMAN Base:          0x%lx\n", bcm2712_dt.cprman_base);
    dprintf("CPRMAN Size:          0x%lx\n", bcm2712_dt.cprman_size);
    dprintf("Crystal Frequency:    %u Hz\n", bcm2712_dt.crystal_frequency);
}

// Export functions for use by other modules
extern "C" {
    status_t bcm2712_dt_init(kernel_args* args);
    status_t bcm2712_detect_hardware(struct bcm2712_chip_info* info);
    status_t bcm2712_get_timer_caps(struct bcm2712_timer_caps* caps);
    bool bcm2712_is_compatible_board(void);
    uint32_t bcm2712_get_board_revision(void);
    uint32_t bcm2712_get_memory_size(void);
    phys_addr_t bcm2712_get_timer_base(void);
    size_t bcm2712_get_timer_size(void);
    uint32_t bcm2712_get_timer_frequency(void);
    status_t bcm2712_get_timer_interrupts(uint32_t* interrupts, size_t count);
    const char* bcm2712_get_board_model(void);
    bool bcm2712_dt_timer_found(void);
    bool bcm2712_dt_cprman_found(void);
    void bcm2712_dt_dump_state(void);
}