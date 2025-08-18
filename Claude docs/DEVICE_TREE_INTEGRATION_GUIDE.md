# Device Tree Integration Guide for ARM64 Haiku OS

## Overview

This document provides comprehensive documentation for Device Tree Blob (DTB) format, parsing requirements, and integration strategies for ARM64 Haiku OS. The implementation supports both generic ARM64 platforms and platform-specific optimizations for BCM2712 (Raspberry Pi 5).

## Device Tree Blob (DTB) Binary Format

### DTB Header Structure

The Device Tree Blob begins with a fixed header containing essential metadata:

```c
struct fdt_header {
    uint32 magic;           // 0xd00dfeed (big-endian)
    uint32 totalsize;       // Total size of DTB in bytes
    uint32 off_dt_struct;   // Offset to structure block
    uint32 off_dt_strings;  // Offset to strings block
    uint32 off_mem_rsvmap;  // Offset to memory reservation map
    uint32 version;         // DTB format version
    uint32 last_comp_version; // Last compatible version
    uint32 boot_cpuid_phys; // Physical CPU ID of boot processor
    uint32 size_dt_strings; // Size of strings block
    uint32 size_dt_struct;  // Size of structure block
};
```

### DTB Layout

```
+-------------------+
| DTB Header        |  40 bytes
+-------------------+
| Memory Reservation|  Variable size
| Map               |
+-------------------+
| Structure Block   |  Variable size
+-------------------+
| Strings Block     |  Variable size
+-------------------+
```

### Structure Block Tokens

The structure block contains a sequence of tokens that define the device tree:

- `FDT_BEGIN_NODE (0x00000001)` - Start of node
- `FDT_END_NODE (0x00000002)` - End of node  
- `FDT_PROP (0x00000003)` - Property definition
- `FDT_NOP (0x00000004)` - No operation
- `FDT_END (0x00000009)` - End of structure block

## Device Tree Node Structure and Hierarchy

### Node Organization

Device tree nodes are organized in a hierarchical tree structure:

```
/ (root)
├── cpus/
│   ├── cpu@0
│   ├── cpu@1
│   ├── cpu@2
│   └── cpu@3
├── memory@0
├── soc/
│   ├── interrupt-controller@107c040000
│   ├── timer@107c003000
│   ├── gpio@107c107000
│   ├── uart@107c120000
│   └── i2c@107c180000
└── chosen/
```

### Node Naming Convention

- Node names follow format: `device-class[@unit-address]`
- Unit address must match first `reg` property value
- Examples: `cpu@0`, `timer@107c003000`, `uart@107c120000`

### Required Node Properties

#### Root Node Properties
```dts
compatible = "raspberrypi,5-model-b", "brcm,bcm2712";
model = "Raspberry Pi 5 Model B";
#address-cells = <2>;
#size-cells = <2>;
```

#### CPU Node Properties
```dts
cpu@0 {
    device_type = "cpu";
    compatible = "arm,cortex-a76";
    reg = <0x000>;
    enable-method = "psci";
    capacity-dmips-mhz = <1024>;
};
```

#### Memory Node Properties
```dts
memory@0 {
    device_type = "memory";
    reg = <0x00000000 0x00000000 0x00000001 0x00000000>;
};
```

## Property Encoding and Data Types

### Property Structure

Each property in the DTB contains:
```c
struct fdt_property {
    uint32 len;     // Length of property value
    uint32 nameoff; // Offset to property name in strings block
    char data[];    // Property value data
};
```

### Data Type Encoding

#### Strings
- UTF-8 encoded, null-terminated
- Example: `compatible = "brcm,bcm2712";`

#### Integers (32-bit, big-endian)
```c
// Single 32-bit value
reg = <0x107c003000>;

// Multiple 32-bit values  
interrupts = <0 96 4>, <0 97 4>;
```

#### 64-bit Addresses
```c
// High 32 bits, low 32 bits
reg = <0x00000001 0x07c003000 0x00000000 0x00001000>;
```

#### Boolean Properties
- Property exists = true, property absent = false
- Example: `interrupt-controller;`

#### Binary Data
- Raw byte sequences for driver-specific data
- Used for firmware blobs, calibration data

### Standard Property Types

#### Address Properties
- `#address-cells`: Number of cells for child addresses
- `#size-cells`: Number of cells for child sizes  
- `reg`: Address ranges `<address size>`

#### Interrupt Properties
- `#interrupt-cells`: Cells per interrupt specifier
- `interrupt-controller`: Marks interrupt controller
- `interrupts`: Interrupt connections `<type number flags>`

#### Clock Properties
- `#clock-cells`: Cells per clock specifier
- `clocks`: Clock connections `<&provider index>`
- `clock-frequency`: Fixed frequency in Hz

## Parsing Requirements and Algorithms

### DTB Validation Algorithm

```c
status_t validate_dtb(void* dtb_addr) {
    struct fdt_header* header = (struct fdt_header*)dtb_addr;
    
    // 1. Verify magic number
    if (be32toh(header->magic) != FDT_MAGIC)
        return B_BAD_DATA;
        
    // 2. Check version compatibility
    if (be32toh(header->version) < FDT_MIN_VERSION)
        return B_NOT_SUPPORTED;
        
    // 3. Validate structure offsets
    uint32 totalsize = be32toh(header->totalsize);
    if (header->off_dt_struct >= totalsize ||
        header->off_dt_strings >= totalsize)
        return B_BAD_DATA;
        
    return B_OK;
}
```

### Node Traversal Algorithm

```c
typedef struct fdt_walker {
    void* dtb_base;
    char* struct_block;
    char* strings_block;
    uint32 current_offset;
    int depth;
} fdt_walker_t;

status_t walk_device_tree(fdt_walker_t* walker, 
                         node_callback_t callback) {
    uint32* token;
    
    while (walker->current_offset < struct_size) {
        token = (uint32*)(walker->struct_block + walker->current_offset);
        
        switch (be32toh(*token)) {
        case FDT_BEGIN_NODE:
            walker->depth++;
            // Process node start
            break;
            
        case FDT_END_NODE:
            walker->depth--;
            // Process node end
            break;
            
        case FDT_PROP:
            // Process property
            break;
            
        case FDT_END:
            return B_OK;
        }
        
        walker->current_offset += token_size;
    }
    
    return B_ERROR;
}
```

### Property Parsing Algorithm

```c
status_t parse_property(fdt_walker_t* walker, 
                       device_tree_node_t* node) {
    struct fdt_property* prop = 
        (struct fdt_property*)(walker->struct_block + 
                              walker->current_offset + 4);
    
    uint32 len = be32toh(prop->len);
    uint32 nameoff = be32toh(prop->nameoff);
    char* name = walker->strings_block + nameoff;
    
    if (strcmp(name, "compatible") == 0) {
        parse_compatible_property(prop->data, len, node);
    } else if (strcmp(name, "reg") == 0) {
        parse_reg_property(prop->data, len, node);
    } else if (strcmp(name, "interrupts") == 0) {
        parse_interrupts_property(prop->data, len, node);
    }
    
    return B_OK;
}
```

## Data Structures for Device Tree Representation

### Core Device Tree Structure

```c
typedef struct device_tree {
    void* dtb_base;                    // Original DTB location
    uint32 dtb_size;                   // DTB total size
    struct fdt_header* header;         // DTB header
    
    // Parsed structure
    device_tree_node_t* root_node;    // Root node
    device_tree_node_t** all_nodes;   // All nodes array
    uint32 node_count;                 // Total node count
    
    // String pool
    char* strings_block;               // Strings block
    uint32 strings_size;               // Strings block size
    
    // Memory reservations
    struct fdt_reserve_entry* reservations;
    uint32 reservation_count;
    
    // Parsing state
    bool parsed;                       // Parse completed
    status_t parse_status;             // Parse result
} device_tree_t;
```

### Enhanced Node Structure

```c
typedef struct device_tree_node {
    // Basic node information
    char name[64];                     // Node name
    char full_path[256];               // Full path from root
    uint32 phandle;                    // Node handle
    
    // Compatibility
    char compatible[FDT_COMPATIBLE_MAX][64];
    int compatible_count;
    
    // Address space
    struct {
        phys_addr_t address;           // Physical address
        size_t size;                   // Address range size
        uint32 flags;                  // Address flags
    } reg[8];
    int reg_count;
    
    // Interrupt configuration
    struct {
        int type;                      // GIC interrupt type
        int number;                    // Interrupt number
        int flags;                     // Trigger/polarity flags
        int priority;                  // Interrupt priority
    } interrupts[4];
    int interrupt_count;
    bool is_interrupt_controller;      // Is interrupt controller
    uint32 interrupt_cells;            // #interrupt-cells value
    
    // Clock information
    struct {
        uint32 provider_phandle;       // Clock provider
        uint32 index;                  // Clock index
        uint32 frequency;              // Clock frequency
    } clocks[4];
    int clock_count;
    bool is_clock_provider;            // Is clock provider
    uint32 clock_cells;                // #clock-cells value
    
    // GPIO configuration
    struct {
        uint32 controller_phandle;     // GPIO controller
        uint32 pin;                    // GPIO pin number
        uint32 flags;                  // GPIO flags
    } gpios[8];
    int gpio_count;
    bool is_gpio_controller;           // Is GPIO controller
    uint32 gpio_cells;                 // #gpio-cells value
    
    // DMA channels
    struct {
        uint32 controller_phandle;     // DMA controller
        uint32 channel;                // DMA channel
        uint32 request;                // DMA request line
    } dma[4];
    int dma_count;
    
    // Power management
    uint32 power_domain_phandle;       // Power domain
    struct {
        uint32 supplier_phandle;       // Supply provider
        char supply_name[32];          // Supply name
    } power_supplies[4];
    int power_supply_count;
    
    // Node hierarchy
    struct device_tree_node* parent;   // Parent node
    struct device_tree_node** children; // Child nodes
    uint32 child_count;                // Number of children
    struct device_tree_node* next_sibling; // Next sibling
    
    // Device binding
    struct {
        bool bound;                    // Driver bound
        char driver_name[64];          // Bound driver name
        void* driver_data;             // Driver private data
        status_t bind_status;          // Bind operation status
    } binding;
    
    // Status and properties
    bool enabled;                      // Status = "okay"
    bool available;                    // Device available
    uint32 properties[32];             // Raw property storage
    int property_count;                // Number of properties
} device_tree_node_t;
```

### Platform-Specific Structures

#### BCM2712 Device Tree Extensions

```c
typedef struct bcm2712_dt_info {
    // Board identification
    char board_model[64];              // Board model string
    char board_revision[16];           // Board revision
    uint64 board_serial;               // Board serial number
    
    // Hardware capabilities
    bool has_gic_v2;                   // GIC-400 support
    bool has_54mhz_timer;              // 54MHz system timer
    bool has_cortex_a76;               // Cortex-A76 cores
    uint32 cpu_count;                  // Number of CPU cores
    uint32 max_cpu_freq_mhz;           // Maximum CPU frequency
    
    // Peripheral availability
    struct {
        uint32 uart_count;             // Available UARTs
        uint32 i2c_count;              // Available I2C controllers
        uint32 spi_count;              // Available SPI controllers
        uint32 gpio_count;             // Available GPIO pins
        bool has_hdmi;                 // HDMI support
        bool has_usb;                  // USB support
        bool has_ethernet;             // Ethernet support
        bool has_wifi;                 // WiFi capability
        bool has_bluetooth;            // Bluetooth capability
    } peripherals;
    
    // Memory layout
    phys_addr_t peripheral_base;       // Peripheral base address
    phys_addr_t gic_dist_base;         // GIC distributor base
    phys_addr_t gic_cpu_base;          // GIC CPU interface base
    phys_addr_t systimer_base;         // System timer base
    phys_addr_t gpio_base;             // GPIO controller base
    
    // Power and thermal
    struct {
        bool has_thermal_sensor;       // Thermal monitoring
        int32 max_temp_millicelsius;   // Maximum temperature
        bool has_cpufreq;              // CPU frequency scaling
        bool has_voltage_control;      // Voltage regulation
    } power_mgmt;
} bcm2712_dt_info_t;
```

## BCM2712-Specific Device Tree Requirements

### Required Compatible Strings

```dts
// Root compatible
compatible = "raspberrypi,5-model-b", "brcm,bcm2712";

// CPU compatible (Cortex-A76)
compatible = "arm,cortex-a76";

// Interrupt controller
compatible = "brcm,bcm2712-gic-400", "arm,gic-400";

// System timer (54MHz)
compatible = "brcm,bcm2712-system-timer";

// GPIO controller
compatible = "brcm,bcm2712-gpio";

// UART controllers
compatible = "brcm,bcm2712-uart", "arm,pl011";
```

### BCM2712 Address Layout

```dts
soc {
    #address-cells = <2>;
    #size-cells = <2>;
    compatible = "simple-bus";
    ranges = <0x0 0x107c000000 0x0 0x107c000000 0x0 0x01800000>;
    
    interrupt-controller@107c040000 {
        compatible = "brcm,bcm2712-gic-400", "arm,gic-400";
        reg = <0x0 0x107c040000 0x0 0x1000>,    // Distributor
              <0x0 0x107c042000 0x0 0x2000>;    // CPU interface
        interrupt-controller;
        #interrupt-cells = <3>;
    };
    
    timer@107c003000 {
        compatible = "brcm,bcm2712-system-timer";
        reg = <0x0 0x107c003000 0x0 0x1000>;
        interrupts = <0 96 4>, <0 97 4>, <0 98 4>, <0 99 4>;
        clock-frequency = <54000000>;
    };
    
    gpio@107c107000 {
        compatible = "brcm,bcm2712-gpio";
        reg = <0x0 0x107c107000 0x0 0x1000>;
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };
};
```

### BCM2712 CPU Topology

```dts
cpus {
    #address-cells = <1>;
    #size-cells = <0>;
    
    cpu@0 {
        device_type = "cpu";
        compatible = "arm,cortex-a76";
        reg = <0x000>;
        enable-method = "psci";
        capacity-dmips-mhz = <1024>;
        dynamic-power-coefficient = <515>;
        
        cpu-idle-states = <&cpu_sleep>;
        operating-points-v2 = <&cpu_opp_table>;
    };
    
    // Additional CPU cores...
    
    idle-states {
        cpu_sleep: cpu-sleep {
            compatible = "arm,idle-state";
            entry-latency-us = <300>;
            exit-latency-us = <600>;
            min-residency-us = <1200>;
        };
    };
};
```

## Integration Architecture

### Parser Integration Flow

```c
status_t arch_device_tree_init(kernel_args* args) {
    // 1. Locate DTB in memory
    void* dtb_addr = find_dtb_in_kernel_args(args);
    if (!dtb_addr)
        return B_ERROR;
    
    // 2. Validate DTB structure
    status_t status = validate_dtb(dtb_addr);
    if (status != B_OK)
        return status;
    
    // 3. Parse device tree structure
    status = parse_device_tree(dtb_addr);
    if (status != B_OK)
        return status;
    
    // 4. Platform-specific initialization
    if (is_bcm2712_platform()) {
        status = init_bcm2712_device_tree();
        if (status != B_OK)
            return status;
    }
    
    // 5. Discover and bind devices
    status = discover_devices();
    if (status != B_OK)
        return status;
    
    return B_OK;
}
```

### Driver Binding Strategy

```c
typedef struct device_driver_binding {
    const char** compatible_strings;   // Compatible strings to match
    status_t (*probe)(device_tree_node_t* node); // Probe function
    status_t (*init)(device_tree_node_t* node);  // Initialization
    void (*cleanup)(device_tree_node_t* node);   // Cleanup function
    const char* driver_name;           // Driver name
    uint32 priority;                   // Binding priority
} device_driver_binding_t;

static device_driver_binding_t bcm2712_bindings[] = {
    {
        .compatible_strings = {"brcm,bcm2712-gic-400", "arm,gic-400", NULL},
        .probe = bcm2712_gic_probe,
        .init = bcm2712_gic_init,
        .driver_name = "bcm2712_gic",
        .priority = 100
    },
    {
        .compatible_strings = {"brcm,bcm2712-system-timer", NULL},
        .probe = bcm2712_timer_probe,
        .init = bcm2712_timer_init,
        .driver_name = "bcm2712_timer",
        .priority = 90
    },
    // Additional bindings...
};
```

### Memory Management Integration

```c
status_t setup_device_tree_memory_regions(void) {
    device_tree_node_t* memory_node = 
        arch_device_tree_find_node("/memory");
    
    for (int i = 0; i < memory_node->reg_count; i++) {
        phys_addr_t start = memory_node->reg[i].address;
        size_t size = memory_node->reg[i].size;
        
        // Add memory region to kernel
        vm_add_physical_memory_region(start, size);
    }
    
    // Process reserved memory regions
    process_reserved_memory_regions();
    
    return B_OK;
}
```

## Performance Optimizations

### Fast Path Device Lookup

```c
// Hash table for O(1) device lookup by compatible string
typedef struct device_hash_entry {
    const char* compatible;
    device_tree_node_t* node;
    struct device_hash_entry* next;
} device_hash_entry_t;

#define DEVICE_HASH_SIZE 128
static device_hash_entry_t* device_hash[DEVICE_HASH_SIZE];

device_tree_node_t* fast_find_compatible(const char* compatible) {
    uint32 hash = simple_hash(compatible) % DEVICE_HASH_SIZE;
    device_hash_entry_t* entry = device_hash[hash];
    
    while (entry) {
        if (strcmp(entry->compatible, compatible) == 0)
            return entry->node;
        entry = entry->next;
    }
    
    return NULL;
}
```

### Cached Property Access

```c
typedef struct property_cache_entry {
    device_tree_node_t* node;
    const char* property_name;
    void* cached_value;
    size_t value_size;
    bool valid;
} property_cache_entry_t;

status_t get_cached_property(device_tree_node_t* node,
                           const char* property_name,
                           void* value, size_t* size) {
    property_cache_entry_t* entry = 
        find_cache_entry(node, property_name);
    
    if (entry && entry->valid) {
        memcpy(value, entry->cached_value, entry->value_size);
        *size = entry->value_size;
        return B_OK;
    }
    
    // Cache miss - parse and cache
    status_t status = parse_property(node, property_name, 
                                   value, size);
    if (status == B_OK) {
        cache_property(node, property_name, value, *size);
    }
    
    return status;
}
```

## libfdt Library Integration

### Overview

Haiku kernel includes a comprehensive libfdt (Flattened Device Tree) library integration for robust device tree parsing and manipulation. The libfdt library provides standardized, well-tested functions for DTB operations.

### libfdt Library Structure

#### Core API Functions

```c
// DTB Validation and Sanity Checking
int fdt_check_header(const void *fdt);
int fdt_totalsize(const void *fdt);
int fdt_version(const void *fdt);

// Node Traversal and Discovery
int fdt_next_node(const void *fdt, int offset, int *depth);
int fdt_path_offset(const void *fdt, const char *path);
int fdt_node_offset_by_compatible(const void *fdt, 
                                  int startoffset, 
                                  const char *compatible);
int fdt_parent_offset(const void *fdt, int nodeoffset);
int fdt_first_subnode(const void *fdt, int offset);
int fdt_next_subnode(const void *fdt, int offset);

// Property Access and Manipulation
const void *fdt_getprop(const void *fdt, int nodeoffset,
                       const char *name, int *lenp);
const char *fdt_get_name(const void *fdt, int nodeoffset, int *lenp);
int fdt_first_property_offset(const void *fdt, int nodeoffset);
int fdt_next_property_offset(const void *fdt, int offset);
const void *fdt_getprop_by_offset(const void *fdt, int offset,
                                 const char **namep, int *lenp);

// Node and Property Creation/Modification
int fdt_setprop(void *fdt, int nodeoffset, const char *name,
               const void *val, int len);
int fdt_add_subnode(void *fdt, int parentoffset, const char *name);
int fdt_del_node(void *fdt, int nodeoffset);
int fdt_delprop(void *fdt, int nodeoffset, const char *name);

// Specialized Helper Functions
int fdt_node_offset_by_phandle(const void *fdt, uint32_t phandle);
uint32_t fdt_get_phandle(const void *fdt, int nodeoffset);
int fdt_address_cells(const void *fdt, int nodeoffset);
int fdt_size_cells(const void *fdt, int nodeoffset);
```

#### Error Codes and Status

```c
// Standard Error Codes
#define FDT_ERR_NOTFOUND     1   // Node/property not found
#define FDT_ERR_EXISTS       2   // Already exists
#define FDT_ERR_NOSPACE      3   // Insufficient buffer space
#define FDT_ERR_BADOFFSET    4   // Invalid offset
#define FDT_ERR_BADPATH      5   // Invalid path format
#define FDT_ERR_BADPHANDLE   6   // Invalid phandle
#define FDT_ERR_BADSTATE     7   // Incomplete device tree
#define FDT_ERR_TRUNCATED    8   // Truncated data
#define FDT_ERR_BADMAGIC     9   // Invalid magic number
#define FDT_ERR_BADVERSION   10  // Unsupported version
#define FDT_ERR_BADSTRUCTURE 11  // Corrupt structure
#define FDT_ERR_BADLAYOUT    12  // Bad memory layout
#define FDT_ERR_INTERNAL     13  // Internal library error
#define FDT_ERR_BADNCELLS    14  // Bad #address-cells/#size-cells
#define FDT_ERR_BADVALUE     15  // Unexpected property value
#define FDT_ERR_BADOVERLAY   16  // Bad overlay
#define FDT_ERR_NOPHANDLES   17  // No phandles available

// Error handling function
const char *fdt_strerror(int errval);
```

#### Endianness Conversion

```c
// Big-endian conversion utilities
static inline uint32_t fdt32_to_cpu(fdt32_t x);
static inline fdt32_t cpu_to_fdt32(uint32_t x);
static inline uint64_t fdt64_to_cpu(fdt64_t x);
static inline fdt64_t cpu_to_fdt64(uint64_t x);

// Custom macros for byte extraction
#define EXTRACT_BYTE(x, n) ((unsigned long long)((uint8_t *)&x)[n])
#define CPU_TO_FDT32(x) ((EXTRACT_BYTE(x, 0) << 24) | \
                        (EXTRACT_BYTE(x, 1) << 16) | \
                        (EXTRACT_BYTE(x, 2) << 8) | \
                        EXTRACT_BYTE(x, 3))
```

### Haiku Build System Integration

#### Current libfdt Integration

Haiku already includes a complete libfdt integration with the following structure:

**Source Files Location**: `/home/ruslan/haiku/src/libs/libfdt/`
```
fdt.c               - Core DTB validation and basic operations
fdt_ro.c            - Read-only operations (property access, traversal)
fdt_rw.c            - Read-write operations (modification functions)
fdt_sw.c            - Sequential write operations (DTB creation)
fdt_wip.c           - Work-in-progress operations (overlay support)
fdt_strerror.c      - Error message support
fdt_addresses.c     - Address parsing helpers
fdt_check.c         - DTB integrity checking
fdt_empty_tree.c    - Empty tree creation
fdt_overlay.c       - Device tree overlay support
```

**Header Files Location**: `/home/ruslan/haiku/headers/libs/libfdt/`
```
libfdt.h           - Main API header file
libfdt_env.h       - Environment-specific definitions
libfdt_internal.h  - Internal library definitions
fdt.h              - DTB format structure definitions
```

#### Haiku Jamfile Build Configuration

**Boot Loader Integration** (`/home/ruslan/haiku/src/system/boot/libs/Jamfile`):
```jamfile
# libfdt integration for boot loader
local libFDTSources =
    fdt.c fdt_ro.c fdt_rw.c fdt_strerror.c fdt_sw.c fdt_wip.c
    fdt_addresses.c fdt_check.c fdt_empty_tree.c fdt_overlay.c ;

local libFDTSourceDirectory = [ FDirName $(HAIKU_TOP) src libs libfdt ] ;

UseLibraryHeaders [ FDirName libfdt ] ;

BootStaticLibrary [ MultiBootGristFiles boot_fdt ] :
    $(libFDTSources) ;

LOCATE on [ FGristFiles $(libFDTSources) ] = $(libFDTSourceDirectory) ;
```

**Kernel Module Integration** (`/home/ruslan/haiku/src/add-ons/kernel/bus_managers/fdt/Jamfile`):
```jamfile
# FDT Bus Manager with embedded libfdt
local libFDTSources =
    fdt.c fdt_ro.c fdt_rw.c fdt_strerror.c fdt_sw.c fdt_wip.c ;

UsePrivateKernelHeaders ;
UseLibraryHeaders [ FDirName libfdt ] ;

KernelAddon fdt :
    fdt_module.cpp
    $(libFDTSources) ;

KernelStaticLibrary kernel_fdt :
    $(libFDTSources) ;

SEARCH on [ FGristFiles $(libFDTSources) ]
    = [ FDirName $(HAIKU_TOP) src libs libfdt ] ;
```

#### Compilation Flags and Environment

**Warning Suppression**:
```jamfile
SubDirCcFlags -Wno-error=missing-prototypes 
              -Wno-error=sign-compare 
              -Wno-error=missing-braces ;
```

**Environment Configuration** (`libfdt_env.h`):
```c
// Standard library dependencies
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// Haiku-specific type definitions
typedef uint16_t FDT_BITWISE fdt16_t;
typedef uint32_t FDT_BITWISE fdt32_t;
typedef uint64_t FDT_BITWISE fdt64_t;

// Platform compatibility for macOS
#ifdef __APPLE__
#define strnlen fdt_strnlen
static inline size_t fdt_strnlen(const char *string, size_t max_count);
#endif
```

### Kernel Integration Strategy

#### FDT Bus Manager Architecture

The Haiku FDT bus manager (`fdt_module.cpp`) provides a complete device tree integration:

**Core Structures**:
```c
struct fdt_bus {
    device_node* node;
    HashMap<HashKey32<int32>, device_node*> phandles;  // phandle lookup
};

struct fdt_device {
    device_node* node;
    device_node* bus;
};
```

**Device Registration**:
```c
static status_t fdt_register_node(fdt_bus* bus, int node, 
                                 device_node* parentDev,
                                 device_node*& curDev) {
    // Extract device properties using libfdt
    const char *name = fdt_get_name(gFDT, node, NULL);
    const void* prop = fdt_getprop(gFDT, node, "compatible", &propLen);
    
    // Register with Haiku device manager
    return gDeviceManager->register_node(parentDev,
        "bus_managers/fdt/driver_v1", &attrs[0], NULL, &curDev);
}
```

**Device Tree Traversal**:
```c
static void fdt_traverse(fdt_bus* bus, int &node, int &depth, 
                        device_node* parentDev) {
    device_node* curDev;
    fdt_register_node(bus, node, parentDev, curDev);
    
    node = fdt_next_node(gFDT, node, &depth);
    while (node >= 0 && depth == curDepth + 1) {
        fdt_traverse(bus, node, depth, curDev);
    }
}
```

#### Property Access Integration

**Register Property Parsing**:
```c
static bool fdt_device_get_reg(fdt_device* dev, uint32 ord, 
                              uint64* regs, uint64* len) {
    uint32 fdtNode;
    gDeviceManager->get_attr_uint32(dev->node, "fdt/node", &fdtNode, false);
    
    const void* prop = fdt_getprop(gFDT, fdtNode, "reg", &propLen);
    if (prop == NULL) return false;
    
    uint32 addressCells = fdt_get_address_cells(gFDT, fdtNode);
    uint32 sizeCells = fdt_get_size_cells(gFDT, fdtNode);
    
    // Parse based on cell sizes
    switch (addressCells) {
        case 1: *regs = fdt32_to_cpu(*(const uint32*)addressPtr); break;
        case 2: *regs = fdt64_to_cpu(*(const uint64*)addressPtr); break;
    }
    
    return true;
}
```

**Interrupt Property Parsing**:
```c
static bool fdt_device_get_interrupt(fdt_device* dev, uint32 index,
                                    device_node** interruptController, 
                                    uint64* interrupt) {
    const uint32 *prop = (uint32*)fdt_getprop(gFDT, fdtNode, 
                                              "interrupts-extended", &propLen);
    
    if (prop == NULL) {
        // Use standard "interrupts" property
        uint32 interruptParent = fdt_get_interrupt_parent(dev, fdtNode);
        uint32 interruptCells = fdt_get_interrupt_cells(interruptParent);
        
        if (interruptCells == 3) {
            // GIC interrupt format: <type number flags>
            uint32 interruptType = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_TYPE]);
            uint32 interruptNumber = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_ID]);
            
            if (interruptType == GIC_INTERRUPT_TYPE_SPI)
                interruptNumber += GIC_INTERRUPT_BASE_SPI;
        }
    }
    
    return true;
}
```

#### Memory Management Integration

**FDT Memory Copying**:
```c
static status_t fdt_bus_init(device_node* node, void** cookie) {
    // Copy FDT from kernel_args to kernel heap
    size_t size = fdt_totalsize(gFDT);
    void* newFDT = malloc(size);
    if (newFDT == NULL) return B_NO_MEMORY;
    
    memcpy(newFDT, gFDT, size);
    gFDT = newFDT;
    
    return B_OK;
}
```

**Device Tree Validation**:
```c
status_t validate_dtb(void* dtb_addr) {
    // Use libfdt validation
    if (fdt_check_header(dtb_addr) != 0)
        return B_BAD_DATA;
    
    // Additional Haiku-specific validation
    uint32 totalsize = fdt_totalsize(dtb_addr);
    if (totalsize > MAX_DTB_SIZE)
        return B_NO_MEMORY;
    
    return B_OK;
}
```

### Enhanced Integration for ARM64/BCM2712

#### ARM64-Specific libfdt Usage

**BCM2712 Device Tree Integration**:
```c
status_t bcm2712_device_tree_init(void) {
    // Validate BCM2712 root compatibility
    int root = fdt_path_offset(gFDT, "/");
    if (root < 0) return B_ERROR;
    
    const char* compatible = fdt_getprop(gFDT, root, "compatible", NULL);
    if (!compatible || !strstr(compatible, "brcm,bcm2712"))
        return B_DEVICE_NOT_FOUND;
    
    // Initialize BCM2712-specific components
    bcm2712_init_timer_from_dt();
    bcm2712_init_gic_from_dt();
    bcm2712_init_gpio_from_dt();
    
    return B_OK;
}
```

**Timer Integration with libfdt**:
```c
status_t bcm2712_init_timer_from_dt(void) {
    int timer_node = fdt_node_offset_by_compatible(gFDT, -1, 
                                                  "brcm,bcm2712-system-timer");
    if (timer_node < 0) return B_DEVICE_NOT_FOUND;
    
    // Get timer base address
    const fdt32_t* reg = fdt_getprop(gFDT, timer_node, "reg", NULL);
    if (reg) {
        uint64 timer_base = fdt64_to_cpu(*(uint64*)reg);
        bcm2712_timer_set_base(timer_base);
    }
    
    // Get timer frequency  
    const fdt32_t* freq = fdt_getprop(gFDT, timer_node, "clock-frequency", NULL);
    if (freq) {
        uint32 frequency = fdt32_to_cpu(*freq);
        bcm2712_timer_set_frequency(frequency);
    }
    
    return B_OK;
}
```

**Interrupt Controller Integration**:
```c
status_t bcm2712_init_gic_from_dt(void) {
    int gic_node = fdt_node_offset_by_compatible(gFDT, -1, 
                                                "brcm,bcm2712-gic-400");
    if (gic_node < 0) return B_DEVICE_NOT_FOUND;
    
    // Parse GIC register ranges
    const fdt32_t* reg = fdt_getprop(gFDT, gic_node, "reg", NULL);
    if (reg) {
        uint64 gic_dist_base = fdt64_to_cpu(*(uint64*)reg);
        uint64 gic_cpu_base = fdt64_to_cpu(*((uint64*)reg + 1));
        
        bcm2712_gic_set_bases(gic_dist_base, gic_cpu_base);
    }
    
    return B_OK;
}
```

### Performance Optimizations

#### Cached Device Tree Access

```c
// Cache frequently accessed nodes
static struct {
    int timer_node;
    int gic_node;  
    int gpio_node;
    bool cached;
} bcm2712_dt_cache = {0};

static int bcm2712_get_cached_timer_node(void) {
    if (!bcm2712_dt_cache.cached) {
        bcm2712_dt_cache.timer_node = fdt_node_offset_by_compatible(gFDT, -1,
            "brcm,bcm2712-system-timer");
        bcm2712_dt_cache.gic_node = fdt_node_offset_by_compatible(gFDT, -1,
            "brcm,bcm2712-gic-400");
        bcm2712_dt_cache.gpio_node = fdt_node_offset_by_compatible(gFDT, -1,
            "brcm,bcm2712-gpio");
        bcm2712_dt_cache.cached = true;
    }
    return bcm2712_dt_cache.timer_node;
}
```

#### Fast Property Access

```c
// Optimized property reading with error checking
static inline uint32 bcm2712_fdt_read_u32_prop(int node, const char* prop_name,
                                               uint32 default_value) {
    int len;
    const fdt32_t* prop = fdt_getprop(gFDT, node, prop_name, &len);
    if (prop && len == sizeof(uint32))
        return fdt32_to_cpu(*prop);
    return default_value;
}

static inline uint64 bcm2712_fdt_read_u64_prop(int node, const char* prop_name,
                                               uint64 default_value) {
    int len;
    const fdt64_t* prop = fdt_getprop(gFDT, node, prop_name, &len);
    if (prop && len == sizeof(uint64))
        return fdt64_to_cpu(*prop);
    return default_value;
}
```

### Integration Testing and Validation

#### libfdt Function Testing

```c
status_t test_libfdt_integration(void) {
    // Test basic DTB validation
    if (fdt_check_header(gFDT) != 0) {
        dprintf("FDT header validation failed\n");
        return B_ERROR;
    }
    
    // Test root node access
    int root = fdt_path_offset(gFDT, "/");
    if (root < 0) {
        dprintf("Root node not found\n");
        return B_ERROR;
    }
    
    // Test compatible string parsing
    const char* compatible = fdt_getprop(gFDT, root, "compatible", NULL);
    if (!compatible) {
        dprintf("Root compatible property not found\n");
        return B_ERROR;
    }
    
    // Test node traversal
    int node = fdt_next_node(gFDT, -1, NULL);
    int node_count = 0;
    while (node >= 0) {
        node_count++;
        node = fdt_next_node(gFDT, node, NULL);
    }
    
    dprintf("libfdt integration test passed: %d nodes found\n", node_count);
    return B_OK;
}
```

## Conclusion

This comprehensive device tree integration provides robust support for ARM64 platforms with specific optimizations for BCM2712/Raspberry Pi 5. The implementation includes:

- Complete DTB binary format parsing using libfdt library
- Hierarchical device node representation with Haiku device manager integration
- Efficient property encoding/decoding with endianness handling
- Platform-specific device discovery for BCM2712/Raspberry Pi 5
- Driver binding framework with interrupt and register mapping
- Performance optimizations for real-time requirements
- Complete libfdt library integration with kernel and boot loader support
- Comprehensive error handling and validation

The system leverages the industry-standard libfdt library while providing Haiku-specific extensions for optimal ARM64 platform support. The integration is designed to be extensible for additional ARM64 platforms while maintaining compatibility with existing Haiku kernel architecture.