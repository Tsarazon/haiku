# ARM64 PSCI (Power State Coordination Interface) Implementation

## Overview

The ARM64 PSCI implementation provides comprehensive power management functionality for ARM64 systems, implementing the PSCI v1.1 specification with fallback support for PSCI v0.2. PSCI is the standard ARM interface for system power management, CPU control, and power state coordination.

## PSCI Specification Support

### PSCI v1.1 Features
- **System Power Management**: System off, reset, suspend
- **CPU Power Management**: CPU on/off, suspend/resume
- **Advanced Features**: SYSTEM_RESET2, SYSTEM_SUSPEND, feature detection
- **Power State Queries**: Affinity info, power state construction
- **Enhanced Error Handling**: Comprehensive return code support

### PSCI v0.2 Compatibility
- **Basic Functions**: Core CPU and system power management
- **Legacy Support**: Automatic fallback for older systems
- **Feature Detection**: Graceful degradation when advanced features unavailable

## Architecture Overview

### Calling Conventions
The implementation supports both ARM64 PSCI calling conventions:

1. **SMC (Secure Monitor Call)**: Primary calling convention for most systems
2. **HVC (Hypervisor Call)**: Fallback calling convention when SMC unavailable

```c
// Automatic calling convention detection
psci_state.smc_calling_convention = true;  // Try SMC first
status = psci_get_version();
if (status != B_OK) {
    psci_state.smc_calling_convention = false;  // Fall back to HVC
    status = psci_get_version();
}
```

### System State Management
```c
static struct {
    bool initialized;
    struct psci_version_info version;
    bool smc_calling_convention;
    uint32_t cpu_suspend_support;
    uint32_t cpu_off_support;
    uint32_t cpu_on_support;
    uint32_t affinity_info_support;
    uint32_t system_off_support;
    uint32_t system_reset_support;
    uint32_t system_reset2_support;
    uint32_t system_suspend_support;
} psci_state;
```

## Core Functions

### 1. System Power Management

#### System Shutdown
```c
status_t arch_psci_system_off(void);
```
- **Purpose**: Complete system power off
- **Behavior**: Does not return if successful
- **Use Case**: System shutdown sequence

#### System Reset
```c
status_t arch_psci_system_reset(void);
status_t arch_psci_system_reset2(uint32_t reset_type, uint64_t cookie);
```
- **Basic Reset**: Warm system reset
- **Advanced Reset**: Cold reset, vendor-specific reset types
- **Reset Types**: `PSCI_SYSTEM_RESET2_TYPE_WARM`, `PSCI_SYSTEM_RESET2_TYPE_COLD`

#### System Suspend
```c
status_t arch_psci_system_suspend(uint64_t entry_point, uint64_t context_id);
```
- **Purpose**: System-wide suspend to RAM
- **Entry Point**: Resume execution address
- **Context ID**: System-specific context information

### 2. CPU Power Management

#### CPU Power On
```c
status_t arch_psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id);
```
- **Target CPU**: CPU affinity identifier
- **Entry Point**: CPU startup address
- **Context**: CPU-specific context information
- **Return Codes**: Success, already on, invalid parameters, etc.

#### CPU Power Off
```c
status_t arch_psci_cpu_off(void);
```
- **Purpose**: Power off calling CPU
- **Behavior**: Does not return if successful
- **Use Case**: CPU hotplug, SMP shutdown

#### CPU Suspend
```c
status_t arch_psci_cpu_suspend(uint32_t power_state, uint64_t entry_point, uint64_t context_id);
```
- **Power State**: Constructed power state value
- **Entry Point**: Resume execution address
- **Context**: CPU-specific resume context

### 3. Power State Queries

#### Affinity Information
```c
status_t arch_psci_affinity_info(uint64_t target_affinity, uint32_t lowest_affinity_level, uint32_t* state);
```
- **Target Affinity**: CPU/cluster/socket identifier
- **Affinity Level**: Scope of query (0=CPU, 1=cluster, 2=socket, 3=system)
- **States**: `PSCI_AFFINITY_INFO_ON`, `PSCI_AFFINITY_INFO_OFF`, `PSCI_AFFINITY_INFO_ON_PENDING`

### 4. Power State Construction

#### Power State Helpers
```c
uint32_t arch_psci_make_power_state(uint8_t type, uint8_t state_id, uint8_t affinity_level);
void arch_psci_parse_power_state(uint32_t power_state, struct psci_power_state* parsed);
```

**Power State Structure**:
```c
struct psci_power_state {
    uint8_t type;           // 0 = standby, 1 = powerdown
    uint8_t state_id;       // Implementation specific state ID
    uint8_t affinity_level; // Deepest affinity level affected
    bool valid;             // Whether this power state is valid
};
```

**Power State Bit Layout**:
```
Bit 30:    Type (0=Standby, 1=Powerdown)
Bits 25-24: Affinity Level (0-3)
Bits 15-0:  State ID (implementation specific)
```

## PSCI Function IDs

### Core Functions (PSCI v0.2+)
| Function | ID | Description |
|----------|-------|-------------|
| PSCI_VERSION | 0x84000000 | Get PSCI version |
| PSCI_CPU_SUSPEND | 0xC4000001 | Suspend calling CPU |
| PSCI_CPU_OFF | 0x84000002 | Power off calling CPU |
| PSCI_CPU_ON | 0xC4000003 | Power on target CPU |
| PSCI_AFFINITY_INFO | 0xC4000004 | Query CPU/cluster state |
| PSCI_SYSTEM_OFF | 0x84000008 | System power off |
| PSCI_SYSTEM_RESET | 0x84000009 | System reset |

### Extended Functions (PSCI v1.0+)
| Function | ID | Description |
|----------|-------|-------------|
| PSCI_PSCI_FEATURES | 0x8400000A | Query feature support |
| PSCI_SYSTEM_SUSPEND | 0xC400000E | System suspend to RAM |
| PSCI_SYSTEM_RESET2 | 0xC4000012 | Enhanced system reset |

## Error Handling

### PSCI Return Codes
| Code | Value | Description |
|------|-------|-------------|
| PSCI_RET_SUCCESS | 0 | Operation successful |
| PSCI_RET_NOT_SUPPORTED | -1 | Function not supported |
| PSCI_RET_INVALID_PARAMS | -2 | Invalid parameters |
| PSCI_RET_DENIED | -3 | Operation denied |
| PSCI_RET_ALREADY_ON | -4 | CPU already powered on |
| PSCI_RET_ON_PENDING | -5 | CPU power-on pending |
| PSCI_RET_INTERNAL_FAILURE | -6 | Implementation failure |
| PSCI_RET_NOT_PRESENT | -7 | CPU/cluster not present |
| PSCI_RET_DISABLED | -8 | CPU/cluster disabled |
| PSCI_RET_INVALID_ADDRESS | -9 | Invalid entry point |

### Error Code Mapping
```c
switch (result) {
    case PSCI_RET_SUCCESS:
        return B_OK;
    case PSCI_RET_INVALID_PARAMS:
        return B_BAD_VALUE;
    case PSCI_RET_INVALID_ADDRESS:
        return B_BAD_ADDRESS;
    case PSCI_RET_ALREADY_ON:
        return B_BUSY;
    case PSCI_RET_ON_PENDING:
        return B_WOULD_BLOCK;
    case PSCI_RET_NOT_PRESENT:
        return B_ENTRY_NOT_FOUND;
    case PSCI_RET_DISABLED:
        return B_NOT_ALLOWED;
    default:
        return B_ERROR;
}
```

## Usage Examples

### Basic Initialization
```c
// Initialize PSCI subsystem
status_t status = arch_psci_init();
if (status != B_OK) {
    panic("PSCI initialization failed");
}

// Check PSCI version
uint16_t major, minor;
arch_psci_get_version(&major, &minor);
dprintf("PSCI v%u.%u detected\n", major, minor);
```

### System Power Management
```c
// System shutdown
if (arch_psci_available()) {
    arch_psci_system_off();  // Does not return
}

// System reset
arch_psci_system_reset();  // Does not return

// Enhanced reset (PSCI v1.1)
arch_psci_system_reset2(PSCI_SYSTEM_RESET2_TYPE_COLD, 0);
```

### CPU Power Management
```c
// Power on secondary CPU
uint64_t target_cpu = 0x1;  // CPU 1
uint64_t entry_point = (uint64_t)secondary_cpu_start;
uint64_t context = 0x12345678;

status_t status = arch_psci_cpu_on(target_cpu, entry_point, context);
switch (status) {
    case B_OK:
        dprintf("CPU %llu powered on successfully\n", target_cpu);
        break;
    case B_BUSY:
        dprintf("CPU %llu is already on\n", target_cpu);
        break;
    case B_WOULD_BLOCK:
        dprintf("CPU %llu power-on is pending\n", target_cpu);
        break;
    default:
        dprintf("Failed to power on CPU %llu\n", target_cpu);
        break;
}

// Power off current CPU (does not return)
arch_psci_cpu_off();
```

### CPU Suspend/Resume
```c
// Construct power state for CPU suspend
uint32_t power_state = arch_psci_make_power_state(
    PSCI_POWER_STATE_TYPE_STANDBY,  // Standby (low power)
    0x5,                            // State ID 5
    0                               // CPU level (affinity 0)
);

// Suspend CPU
uint64_t resume_entry = (uint64_t)cpu_resume_handler;
uint64_t context = current_cpu_context();

status = arch_psci_cpu_suspend(power_state, resume_entry, context);
if (status == B_OK) {
    // CPU was suspended and has now resumed
    dprintf("CPU suspend/resume completed\n");
}
```

### Power State Queries
```c
// Check if a CPU is powered on
uint64_t target_cpu = 0x101;  // Cluster 1, CPU 1
uint32_t affinity_level = 0;  // CPU level
uint32_t state;

status = arch_psci_affinity_info(target_cpu, affinity_level, &state);
if (status == B_OK) {
    switch (state) {
        case PSCI_AFFINITY_INFO_ON:
            dprintf("CPU is powered on\n");
            break;
        case PSCI_AFFINITY_INFO_OFF:
            dprintf("CPU is powered off\n");
            break;
        case PSCI_AFFINITY_INFO_ON_PENDING:
            dprintf("CPU power-on is pending\n");
            break;
    }
}
```

### Feature Detection
```c
// Check if advanced features are supported
bool system_suspend_supported;
arch_psci_get_features(PSCI_SYSTEM_SUSPEND, &system_suspend_supported);

if (system_suspend_supported) {
    // Use system suspend functionality
    uint64_t resume_point = (uint64_t)system_resume_handler;
    arch_psci_system_suspend(resume_point, 0);
}
```

## Integration Points

### Kernel Boot Sequence
1. **Early Init**: Call `arch_psci_init()` during platform initialization
2. **Feature Detection**: Query supported PSCI functions
3. **SMP Bringup**: Use `arch_psci_cpu_on()` for secondary CPU startup
4. **Power Management**: Integrate with kernel power management framework

### SMP Support
```c
// Secondary CPU startup sequence
for (int cpu = 1; cpu < num_cpus; cpu++) {
    uint64_t cpu_affinity = cpu_to_affinity(cpu);
    uint64_t entry_point = (uint64_t)secondary_start;
    
    status = arch_psci_cpu_on(cpu_affinity, entry_point, cpu);
    if (status != B_OK) {
        dprintf("Failed to start CPU %d\n", cpu);
    }
}
```

### Power Management Integration
```c
// System shutdown hook
void platform_system_shutdown(void) {
    if (arch_psci_available()) {
        arch_psci_system_off();
    } else {
        // Fallback shutdown method
        platform_legacy_shutdown();
    }
}

// System reset hook
void platform_system_reset(void) {
    if (arch_psci_available()) {
        arch_psci_system_reset();
    } else {
        // Fallback reset method
        platform_legacy_reset();
    }
}
```

## Debug and Diagnostics

### PSCI State Dump
```c
// Comprehensive PSCI state information
arch_psci_dump_state();
```

**Example Output**:
```
PSCI State Information:
======================
Version:             1.1
Calling convention:  SMC
Initialized:         yes

Supported Functions:
  CPU_SUSPEND:     ✓
  CPU_OFF:         ✓
  CPU_ON:          ✓
  AFFINITY_INFO:   ✓
  SYSTEM_OFF:      ✓
  SYSTEM_RESET:    ✓
  SYSTEM_RESET2:   ✓
  SYSTEM_SUSPEND:  ✓
```

## Testing

### Test Suite Coverage
1. **Function ID Validation**: Verify all PSCI function IDs are correct
2. **Return Code Handling**: Test error code mapping and handling
3. **Power State Construction**: Test power state creation and parsing
4. **Version Parsing**: Test PSCI version detection and parsing
5. **Affinity Handling**: Test CPU affinity and level handling
6. **Feature Detection**: Test capability detection matrix
7. **Calling Convention**: Test SMC/HVC detection logic

### Test Results
```
ARM64 PSCI (Power State Coordination Interface) Test Suite
=========================================================
✓ PSCI function ID tests passed
✓ PSCI return value tests passed
✓ PSCI power state construction tests passed
✓ PSCI version parsing tests passed
✓ PSCI affinity state tests passed
✓ PSCI error handling tests passed
✓ CPU affinity handling tests passed
✓ Power state parsing tests passed
✓ Calling convention detection tests passed
✓ Feature detection matrix tests passed
✓ Comprehensive PSCI functionality tests passed

All PSCI tests PASSED! ✓
```

## Benefits and Features

### 1. **Standards Compliance**
- Full PSCI v1.1 specification compliance
- Backward compatibility with PSCI v0.2
- Proper ARM64 calling convention support

### 2. **Robust Error Handling**
- Comprehensive error code mapping
- Graceful degradation for unsupported features
- Detailed diagnostic information

### 3. **Power Management Integration**
- System-wide power control (off, reset, suspend)
- CPU-level power management (on, off, suspend)
- Power state queries and affinity info

### 4. **SMP Support**
- CPU startup and shutdown functionality
- Multi-level affinity support (CPU, cluster, socket)
- CPU hotplug capability

### 5. **Flexibility**
- Automatic calling convention detection (SMC/HVC)
- Feature detection and capability reporting
- Power state construction helpers

### 6. **Debug Support**
- Comprehensive state dump functionality
- Detailed error reporting and logging
- Test suite for validation

## Future Enhancements

### 1. **Extended PSCI Features**
- PSCI statistics support (residency, count)
- Memory protection functions
- Node hardware state queries

### 2. **Performance Optimization**
- CPU suspend/resume optimization
- Power state caching
- Batch CPU operations

### 3. **Enhanced Integration**
- Device tree PSCI configuration
- ACPI PSCI integration
- Thermal management integration

---

This PSCI implementation provides a complete, standards-compliant foundation for ARM64 power management in Haiku, enabling efficient system and CPU power control with comprehensive error handling and diagnostic capabilities.