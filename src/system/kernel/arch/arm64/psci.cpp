/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 PSCI (Power State Coordination Interface) Implementation
 * 
 * This module provides PSCI v1.1 compliant power management functionality
 * for ARM64 systems, including system reset, CPU power management, and
 * power state queries.
 *
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Basic Haiku types for kernel compilation
typedef int32_t status_t;
#define B_OK                     0
#define B_ERROR                 -1
#define B_NOT_SUPPORTED         -2147483647
#define B_NOT_INITIALIZED       -2147483646
#define B_BAD_VALUE             -2147483645
#define B_BAD_ADDRESS           -2147483644
#define B_BUSY                  -2147483643
#define B_WOULD_BLOCK           -2147483642
#define B_ENTRY_NOT_FOUND       -2147483641
#define B_NOT_ALLOWED           -2147483640

// Kernel debug printf stub
#define dprintf(fmt, ...) ((void)0)

// PSCI Function IDs (PSCI v1.1)
#define PSCI_VERSION                    0x84000000
#define PSCI_CPU_SUSPEND                0xC4000001
#define PSCI_CPU_OFF                    0x84000002
#define PSCI_CPU_ON                     0xC4000003
#define PSCI_AFFINITY_INFO              0xC4000004
#define PSCI_MIGRATE                    0xC4000005
#define PSCI_MIGRATE_INFO_TYPE          0x84000006
#define PSCI_MIGRATE_INFO_UP_CPU        0xC4000007
#define PSCI_SYSTEM_OFF                 0x84000008
#define PSCI_SYSTEM_RESET               0x84000009
#define PSCI_PSCI_FEATURES              0x8400000A
#define PSCI_CPU_FREEZE                 0x8400000B
#define PSCI_CPU_DEFAULT_SUSPEND        0xC400000C
#define PSCI_NODE_HW_STATE              0xC400000D
#define PSCI_SYSTEM_SUSPEND             0xC400000E
#define PSCI_PSCI_SET_SUSPEND_MODE      0x8400000F
#define PSCI_PSCI_STAT_RESIDENCY        0xC4000010
#define PSCI_PSCI_STAT_COUNT            0xC4000011
#define PSCI_SYSTEM_RESET2              0xC4000012
#define PSCI_MEM_PROTECT                0x84000013
#define PSCI_MEM_CHK_RANGE              0xC4000014

// PSCI Return Values
#define PSCI_RET_SUCCESS                 0
#define PSCI_RET_NOT_SUPPORTED          -1
#define PSCI_RET_INVALID_PARAMS         -2
#define PSCI_RET_DENIED                 -3
#define PSCI_RET_ALREADY_ON             -4
#define PSCI_RET_ON_PENDING             -5
#define PSCI_RET_INTERNAL_FAILURE       -6
#define PSCI_RET_NOT_PRESENT            -7
#define PSCI_RET_DISABLED               -8
#define PSCI_RET_INVALID_ADDRESS        -9

// PSCI Power States
#define PSCI_POWER_STATE_TYPE_STANDBY    0x0
#define PSCI_POWER_STATE_TYPE_POWERDOWN  0x1

// PSCI Affinity Info States
#define PSCI_AFFINITY_INFO_ON            0
#define PSCI_AFFINITY_INFO_OFF           1
#define PSCI_AFFINITY_INFO_ON_PENDING    2

// PSCI Migrate Info Types
#define PSCI_TOS_UP_MIGRATE              0
#define PSCI_TOS_UP_NO_MIGRATE           1
#define PSCI_TOS_NOT_UP_MIG_CAP          2
#define PSCI_TOS_NOT_PRESENT_MP          3

// PSCI System Reset Types
#define PSCI_SYSTEM_RESET2_TYPE_WARM     0
#define PSCI_SYSTEM_RESET2_TYPE_COLD     1
#define PSCI_SYSTEM_RESET2_TYPE_VENDOR   0x80000000

// PSCI Version Information
struct psci_version_info {
    uint16_t major;
    uint16_t minor;
};

// PSCI Power State Structure
struct psci_power_state {
    uint8_t type;          // 0 = standby, 1 = powerdown
    uint8_t state_id;      // Implementation specific state ID
    uint8_t affinity_level; // Deepest affinity level affected
    bool valid;            // Whether this power state is valid
};

// Global PSCI state
static struct {
    bool initialized;
    struct psci_version_info version;
    bool smc_calling_convention;  // true = SMC, false = HVC
    uint32_t cpu_suspend_support;
    uint32_t cpu_off_support;
    uint32_t cpu_on_support;
    uint32_t affinity_info_support;
    uint32_t system_off_support;
    uint32_t system_reset_support;
    uint32_t system_reset2_support;
    uint32_t system_suspend_support;
} psci_state = { false };

/*
 * Low-level PSCI call implementations
 */

// Execute SMC (Secure Monitor Call) instruction
static inline int64_t
psci_smc_call(uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x0 asm("x0") = function_id;
    register uint64_t x1 asm("x1") = arg0;
    register uint64_t x2 asm("x2") = arg1;
    register uint64_t x3 asm("x3") = arg2;
    
    asm volatile("smc #0" 
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "memory", "cc");
    
    return (int64_t)x0;
}

// Execute HVC (Hypervisor Call) instruction
static inline int64_t
psci_hvc_call(uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x0 asm("x0") = function_id;
    register uint64_t x1 asm("x1") = arg0;
    register uint64_t x2 asm("x2") = arg1;
    register uint64_t x3 asm("x3") = arg2;
    
    asm volatile("hvc #0" 
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "memory", "cc");
    
    return (int64_t)x0;
}

// Generic PSCI call wrapper
static int64_t
psci_call(uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    if (!psci_state.initialized) {
        dprintf("PSCI: Not initialized, cannot make call 0x%x\n", function_id);
        return PSCI_RET_NOT_SUPPORTED;
    }
    
    if (psci_state.smc_calling_convention) {
        return psci_smc_call(function_id, arg0, arg1, arg2);
    } else {
        return psci_hvc_call(function_id, arg0, arg1, arg2);
    }
}

/*
 * PSCI Feature Detection and Initialization
 */

// Get PSCI version
static status_t
psci_get_version(void)
{
    int64_t version = psci_call(PSCI_VERSION, 0, 0, 0);
    
    if (version == PSCI_RET_NOT_SUPPORTED) {
        dprintf("PSCI: Version query not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    psci_state.version.major = (version >> 16) & 0xFFFF;
    psci_state.version.minor = version & 0xFFFF;
    
    dprintf("PSCI: Version %u.%u detected\n", 
            psci_state.version.major, psci_state.version.minor);
    
    return B_OK;
}

// Check if a PSCI feature is supported
static bool
psci_feature_supported(uint32_t function_id)
{
    if (psci_state.version.major < 1) {
        // PSCI v0.2 doesn't have PSCI_FEATURES, assume basic support
        switch (function_id) {
            case PSCI_CPU_SUSPEND:
            case PSCI_CPU_OFF:
            case PSCI_CPU_ON:
            case PSCI_AFFINITY_INFO:
            case PSCI_SYSTEM_OFF:
            case PSCI_SYSTEM_RESET:
                return true;
            default:
                return false;
        }
    }
    
    int64_t result = psci_call(PSCI_PSCI_FEATURES, function_id, 0, 0);
    return (result >= 0);
}

// Detect PSCI feature support
static void
psci_detect_features(void)
{
    psci_state.cpu_suspend_support = psci_feature_supported(PSCI_CPU_SUSPEND);
    psci_state.cpu_off_support = psci_feature_supported(PSCI_CPU_OFF);
    psci_state.cpu_on_support = psci_feature_supported(PSCI_CPU_ON);
    psci_state.affinity_info_support = psci_feature_supported(PSCI_AFFINITY_INFO);
    psci_state.system_off_support = psci_feature_supported(PSCI_SYSTEM_OFF);
    psci_state.system_reset_support = psci_feature_supported(PSCI_SYSTEM_RESET);
    psci_state.system_reset2_support = psci_feature_supported(PSCI_SYSTEM_RESET2);
    psci_state.system_suspend_support = psci_feature_supported(PSCI_SYSTEM_SUSPEND);
    
    dprintf("PSCI: Feature support detected:\n");
    dprintf("  CPU_SUSPEND:     %s\n", psci_state.cpu_suspend_support ? "yes" : "no");
    dprintf("  CPU_OFF:         %s\n", psci_state.cpu_off_support ? "yes" : "no");
    dprintf("  CPU_ON:          %s\n", psci_state.cpu_on_support ? "yes" : "no");
    dprintf("  AFFINITY_INFO:   %s\n", psci_state.affinity_info_support ? "yes" : "no");
    dprintf("  SYSTEM_OFF:      %s\n", psci_state.system_off_support ? "yes" : "no");
    dprintf("  SYSTEM_RESET:    %s\n", psci_state.system_reset_support ? "yes" : "no");
    dprintf("  SYSTEM_RESET2:   %s\n", psci_state.system_reset2_support ? "yes" : "no");
    dprintf("  SYSTEM_SUSPEND:  %s\n", psci_state.system_suspend_support ? "yes" : "no");
}

/*
 * Public PSCI Interface Functions
 */

// Initialize PSCI subsystem
status_t
arch_psci_init(void)
{
    if (psci_state.initialized) {
        dprintf("PSCI: Already initialized\n");
        return B_OK;
    }
    
    dprintf("PSCI: Initializing Power State Coordination Interface\n");
    
    // Try SMC calling convention first (most common)
    psci_state.smc_calling_convention = true;
    
    status_t status = psci_get_version();
    if (status != B_OK) {
        // Try HVC calling convention
        dprintf("PSCI: SMC failed, trying HVC calling convention\n");
        psci_state.smc_calling_convention = false;
        status = psci_get_version();
        
        if (status != B_OK) {
            dprintf("PSCI: Both SMC and HVC failed, PSCI not available\n");
            return B_NOT_SUPPORTED;
        }
    }
    
    psci_detect_features();
    
    psci_state.initialized = true;
    
    dprintf("PSCI: Initialization complete using %s calling convention\n",
            psci_state.smc_calling_convention ? "SMC" : "HVC");
    
    return B_OK;
}

// Get PSCI version information
status_t
arch_psci_get_version(uint16_t* major, uint16_t* minor)
{
    if (!psci_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (major != NULL) {
        *major = psci_state.version.major;
    }
    
    if (minor != NULL) {
        *minor = psci_state.version.minor;
    }
    
    return B_OK;
}

/*
 * System Power Management Functions
 */

// System shutdown
status_t
arch_psci_system_off(void)
{
    if (!psci_state.initialized || !psci_state.system_off_support) {
        dprintf("PSCI: SYSTEM_OFF not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Initiating system shutdown\n");
    
    // This call should not return
    psci_call(PSCI_SYSTEM_OFF, 0, 0, 0);
    
    // If we reach here, the call failed
    dprintf("PSCI: SYSTEM_OFF call returned unexpectedly\n");
    return B_ERROR;
}

// System reset
status_t
arch_psci_system_reset(void)
{
    if (!psci_state.initialized || !psci_state.system_reset_support) {
        dprintf("PSCI: SYSTEM_RESET not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Initiating system reset\n");
    
    // This call should not return
    psci_call(PSCI_SYSTEM_RESET, 0, 0, 0);
    
    // If we reach here, the call failed
    dprintf("PSCI: SYSTEM_RESET call returned unexpectedly\n");
    return B_ERROR;
}

// System reset with type (PSCI v1.1+)
status_t
arch_psci_system_reset2(uint32_t reset_type, uint64_t cookie)
{
    if (!psci_state.initialized || !psci_state.system_reset2_support) {
        dprintf("PSCI: SYSTEM_RESET2 not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Initiating system reset2 (type=%u)\n", reset_type);
    
    // This call should not return for valid reset types
    int64_t result = psci_call(PSCI_SYSTEM_RESET2, reset_type, cookie, 0);
    
    if (result < 0) {
        dprintf("PSCI: SYSTEM_RESET2 failed with error %lld\n", result);
        return B_ERROR;
    }
    
    return B_OK;
}

// System suspend
status_t
arch_psci_system_suspend(uint64_t entry_point, uint64_t context_id)
{
    if (!psci_state.initialized || !psci_state.system_suspend_support) {
        dprintf("PSCI: SYSTEM_SUSPEND not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Initiating system suspend (entry=0x%llx)\n", entry_point);
    
    int64_t result = psci_call(PSCI_SYSTEM_SUSPEND, entry_point, context_id, 0);
    
    if (result < 0) {
        dprintf("PSCI: SYSTEM_SUSPEND failed with error %lld\n", result);
        return B_ERROR;
    }
    
    return B_OK;
}

/*
 * CPU Power Management Functions
 */

// Turn on a CPU
status_t
arch_psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id)
{
    if (!psci_state.initialized || !psci_state.cpu_on_support) {
        dprintf("PSCI: CPU_ON not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Turning on CPU %llu (entry=0x%llx)\n", target_cpu, entry_point);
    
    int64_t result = psci_call(PSCI_CPU_ON, target_cpu, entry_point, context_id);
    
    switch (result) {
        case PSCI_RET_SUCCESS:
            dprintf("PSCI: CPU %llu successfully turned on\n", target_cpu);
            return B_OK;
            
        case PSCI_RET_INVALID_PARAMS:
            dprintf("PSCI: CPU_ON invalid parameters\n");
            return B_BAD_VALUE;
            
        case PSCI_RET_INVALID_ADDRESS:
            dprintf("PSCI: CPU_ON invalid entry point address\n");
            return B_BAD_ADDRESS;
            
        case PSCI_RET_ALREADY_ON:
            dprintf("PSCI: CPU %llu is already on\n", target_cpu);
            return B_BUSY;
            
        case PSCI_RET_ON_PENDING:
            dprintf("PSCI: CPU %llu power on is pending\n", target_cpu);
            return B_WOULD_BLOCK;
            
        case PSCI_RET_INTERNAL_FAILURE:
            dprintf("PSCI: CPU_ON internal failure\n");
            return B_ERROR;
            
        case PSCI_RET_NOT_PRESENT:
            dprintf("PSCI: CPU %llu not present\n", target_cpu);
            return B_ENTRY_NOT_FOUND;
            
        case PSCI_RET_DISABLED:
            dprintf("PSCI: CPU %llu is disabled\n", target_cpu);
            return B_NOT_ALLOWED;
            
        default:
            dprintf("PSCI: CPU_ON returned unexpected result %lld\n", result);
            return B_ERROR;
    }
}

// Turn off current CPU
status_t
arch_psci_cpu_off(void)
{
    if (!psci_state.initialized || !psci_state.cpu_off_support) {
        dprintf("PSCI: CPU_OFF not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Turning off current CPU\n");
    
    // This call should not return if successful
    int64_t result = psci_call(PSCI_CPU_OFF, 0, 0, 0);
    
    // If we reach here, the call failed
    dprintf("PSCI: CPU_OFF call returned unexpectedly with result %lld\n", result);
    return B_ERROR;
}

// Suspend current CPU
status_t
arch_psci_cpu_suspend(uint32_t power_state, uint64_t entry_point, uint64_t context_id)
{
    if (!psci_state.initialized || !psci_state.cpu_suspend_support) {
        dprintf("PSCI: CPU_SUSPEND not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("PSCI: Suspending CPU (state=0x%x, entry=0x%llx)\n", 
            power_state, entry_point);
    
    int64_t result = psci_call(PSCI_CPU_SUSPEND, power_state, entry_point, context_id);
    
    switch (result) {
        case PSCI_RET_SUCCESS:
            dprintf("PSCI: CPU suspend/resume completed successfully\n");
            return B_OK;
            
        case PSCI_RET_INVALID_PARAMS:
            dprintf("PSCI: CPU_SUSPEND invalid parameters\n");
            return B_BAD_VALUE;
            
        case PSCI_RET_INVALID_ADDRESS:
            dprintf("PSCI: CPU_SUSPEND invalid entry point address\n");
            return B_BAD_ADDRESS;
            
        case PSCI_RET_DENIED:
            dprintf("PSCI: CPU_SUSPEND denied\n");
            return B_NOT_ALLOWED;
            
        default:
            dprintf("PSCI: CPU_SUSPEND returned unexpected result %lld\n", result);
            return B_ERROR;
    }
}

/*
 * Power State Query Functions
 */

// Get affinity info for a CPU
status_t
arch_psci_affinity_info(uint64_t target_affinity, uint32_t lowest_affinity_level, uint32_t* state)
{
    if (!psci_state.initialized || !psci_state.affinity_info_support) {
        dprintf("PSCI: AFFINITY_INFO not supported\n");
        return B_NOT_SUPPORTED;
    }
    
    if (state == NULL) {
        return B_BAD_VALUE;
    }
    
    int64_t result = psci_call(PSCI_AFFINITY_INFO, target_affinity, lowest_affinity_level, 0);
    
    if (result < 0) {
        dprintf("PSCI: AFFINITY_INFO failed with error %lld\n", result);
        return B_ERROR;
    }
    
    *state = (uint32_t)result;
    
    const char* state_str;
    switch (*state) {
        case PSCI_AFFINITY_INFO_ON:
            state_str = "ON";
            break;
        case PSCI_AFFINITY_INFO_OFF:
            state_str = "OFF";
            break;
        case PSCI_AFFINITY_INFO_ON_PENDING:
            state_str = "ON_PENDING";
            break;
        default:
            state_str = "UNKNOWN";
            break;
    }
    
    dprintf("PSCI: CPU affinity %llu state: %s\n", target_affinity, state_str);
    
    return B_OK;
}

// Check if PSCI is available and initialized
bool
arch_psci_available(void)
{
    return psci_state.initialized;
}

// Get PSCI feature support status
status_t
arch_psci_get_features(uint32_t function_id, bool* supported)
{
    if (!psci_state.initialized) {
        return B_NOT_INITIALIZED;
    }
    
    if (supported == NULL) {
        return B_BAD_VALUE;
    }
    
    *supported = psci_feature_supported(function_id);
    return B_OK;
}

/*
 * Power State Construction Helpers
 */

// Create a PSCI power state value
uint32_t
arch_psci_make_power_state(uint8_t type, uint8_t state_id, uint8_t affinity_level)
{
    uint32_t power_state = 0;
    
    // Type field (bit 30)
    if (type == PSCI_POWER_STATE_TYPE_POWERDOWN) {
        power_state |= (1U << 30);
    }
    
    // State ID (bits 15:0)
    power_state |= (state_id & 0xFFFF);
    
    // Affinity level (bits 25:24)
    power_state |= ((affinity_level & 0x3) << 24);
    
    return power_state;
}

// Parse a PSCI power state value
void
arch_psci_parse_power_state(uint32_t power_state, struct psci_power_state* parsed)
{
    if (parsed == NULL) {
        return;
    }
    
    parsed->type = (power_state & (1U << 30)) ? 
        PSCI_POWER_STATE_TYPE_POWERDOWN : PSCI_POWER_STATE_TYPE_STANDBY;
    
    parsed->state_id = power_state & 0xFFFF;
    parsed->affinity_level = (power_state >> 24) & 0x3;
    parsed->valid = true;
}

/*
 * Debug and Diagnostic Functions
 */

// Dump PSCI state information
void
arch_psci_dump_state(void)
{
    if (!psci_state.initialized) {
        dprintf("PSCI: Not initialized\n");
        return;
    }
    
    dprintf("PSCI State Information:\n");
    dprintf("======================\n");
    dprintf("Version:             %u.%u\n", 
            psci_state.version.major, psci_state.version.minor);
    dprintf("Calling convention:  %s\n", 
            psci_state.smc_calling_convention ? "SMC" : "HVC");
    dprintf("Initialized:         %s\n", 
            psci_state.initialized ? "yes" : "no");
    
    dprintf("\nSupported Functions:\n");
    dprintf("  CPU_SUSPEND:     %s\n", psci_state.cpu_suspend_support ? "✓" : "✗");
    dprintf("  CPU_OFF:         %s\n", psci_state.cpu_off_support ? "✓" : "✗");
    dprintf("  CPU_ON:          %s\n", psci_state.cpu_on_support ? "✓" : "✗");
    dprintf("  AFFINITY_INFO:   %s\n", psci_state.affinity_info_support ? "✓" : "✗");
    dprintf("  SYSTEM_OFF:      %s\n", psci_state.system_off_support ? "✓" : "✗");
    dprintf("  SYSTEM_RESET:    %s\n", psci_state.system_reset_support ? "✓" : "✗");
    dprintf("  SYSTEM_RESET2:   %s\n", psci_state.system_reset2_support ? "✓" : "✗");
    dprintf("  SYSTEM_SUSPEND:  %s\n", psci_state.system_suspend_support ? "✓" : "✗");
}