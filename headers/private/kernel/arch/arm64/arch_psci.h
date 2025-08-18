/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 PSCI (Power State Coordination Interface) Header
 */

#ifndef _KERNEL_ARCH_ARM64_ARCH_PSCI_H
#define _KERNEL_ARCH_ARM64_ARCH_PSCI_H

#include <SupportDefs.h>

// PSCI Power State Types
#define PSCI_POWER_STATE_TYPE_STANDBY    0x0
#define PSCI_POWER_STATE_TYPE_POWERDOWN  0x1

// PSCI Affinity Info States
#define PSCI_AFFINITY_INFO_ON            0
#define PSCI_AFFINITY_INFO_OFF           1
#define PSCI_AFFINITY_INFO_ON_PENDING    2

// PSCI System Reset Types (for SYSTEM_RESET2)
#define PSCI_SYSTEM_RESET2_TYPE_WARM     0
#define PSCI_SYSTEM_RESET2_TYPE_COLD     1
#define PSCI_SYSTEM_RESET2_TYPE_VENDOR   0x80000000

// PSCI Power State Structure
struct psci_power_state {
    uint8_t type;           // 0 = standby, 1 = powerdown
    uint8_t state_id;       // Implementation specific state ID
    uint8_t affinity_level; // Deepest affinity level affected
    bool valid;             // Whether this power state is valid
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PSCI Initialization and Management
 */

// Initialize PSCI subsystem
status_t arch_psci_init(void);

// Get PSCI version information
status_t arch_psci_get_version(uint16_t* major, uint16_t* minor);

// Check if PSCI is available and initialized
bool arch_psci_available(void);

// Get PSCI feature support status
status_t arch_psci_get_features(uint32_t function_id, bool* supported);

/*
 * System Power Management Functions
 */

// System shutdown (power off)
status_t arch_psci_system_off(void);

// System reset (warm reboot)
status_t arch_psci_system_reset(void);

// System reset with type (PSCI v1.1+)
status_t arch_psci_system_reset2(uint32_t reset_type, uint64_t cookie);

// System suspend to RAM
status_t arch_psci_system_suspend(uint64_t entry_point, uint64_t context_id);

/*
 * CPU Power Management Functions
 */

// Turn on a CPU
status_t arch_psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id);

// Turn off current CPU
status_t arch_psci_cpu_off(void);

// Suspend current CPU
status_t arch_psci_cpu_suspend(uint32_t power_state, uint64_t entry_point, uint64_t context_id);

/*
 * Power State Query Functions
 */

// Get affinity info for a CPU
status_t arch_psci_affinity_info(uint64_t target_affinity, uint32_t lowest_affinity_level, uint32_t* state);

/*
 * Power State Construction Helpers
 */

// Create a PSCI power state value
uint32_t arch_psci_make_power_state(uint8_t type, uint8_t state_id, uint8_t affinity_level);

// Parse a PSCI power state value
void arch_psci_parse_power_state(uint32_t power_state, struct psci_power_state* parsed);

/*
 * Debug and Diagnostic Functions
 */

// Dump PSCI state information
void arch_psci_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_ARM64_ARCH_PSCI_H */