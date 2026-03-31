/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_DEVICE_KEEPER_H
#define _KERNEL_DEVICE_KEEPER_H

#include <Drivers.h>
#include <TypeConstants.h>
#include <module.h>
#include <SupportDefs.h>


// Property value types: B_UINT8_TYPE .. B_RAW_TYPE from TypeConstants.h,
// plus KOSM_STRINGLIST_TYPE for FDT compatible-style string lists.
#define KOSM_STRINGLIST_TYPE		'ksls'

// Module name
#define KOSM_DEVICE_KEEPER_MODULE_NAME	"system/device_keeper/v1"

// Driver modules must export module_info with this suffix.
// Example: "drivers/dev/net/my_driver/dk_driver_v1"
#define DK_DRIVER_MODULE_SUFFIX			"dk_driver_v1"

// Standard node property names
#define KOSM_DEVICE_PRETTY_NAME		"device/pretty name"
#define KOSM_DEVICE_BUS				"device/bus"
#define KOSM_DEVICE_FLAGS			"device/flags"
#define KOSM_DEVICE_COMPATIBLE		"device/compatible"
#define KOSM_DEVICE_FIXED_CHILD	"device/fixed child"

#define KOSM_DEVICE_VENDOR_ID		"device/vendor"
#define KOSM_DEVICE_ID				"device/id"
#define KOSM_DEVICE_TYPE			"device/type"
#define KOSM_DEVICE_SUB_TYPE		"device/subtype"
#define KOSM_DEVICE_INTERFACE		"device/interface"
#define KOSM_DEVICE_UNIQUE_ID		"device/unique id"

// FDT-derived property names
#define KOSM_FDT_NODE_OFFSET		"fdt/node offset"
#define KOSM_DEVICE_REG_BASE		"device/reg base"
#define KOSM_DEVICE_REG_SIZE		"device/reg size"
#define KOSM_DEVICE_IRQ				"device/irq"
#define KOSM_DEVICE_IRQ_FLAGS		"device/irq flags"

// PCI-specific property names
#define KOSM_PCI_DEVICE_DOMAIN		"pci/domain"
#define KOSM_PCI_DEVICE_BUS			"pci/bus"
#define KOSM_PCI_DEVICE_DEVICE		"pci/device"
#define KOSM_PCI_DEVICE_FUNCTION	"pci/function"

// DMA property names
#define KOSM_DMA_LOW_ADDRESS		"dma/low_address"
#define KOSM_DMA_HIGH_ADDRESS		"dma/high_address"
#define KOSM_DMA_ALIGNMENT			"dma/alignment"
#define KOSM_DMA_BOUNDARY			"dma/boundary"
#define KOSM_DMA_MAX_TRANSFER_BLOCKS "dma/max_transfer_blocks"
#define KOSM_DMA_MAX_SEGMENT_BLOCKS	"dma/max_segment_blocks"
#define KOSM_DMA_MAX_SEGMENT_COUNT	"dma/max_segment_count"
#define KOSM_DMA_BLOCK_SIZE			"dma/block_size"

// ACPI-specific property names
#define KOSM_ACPI_DEVICE_PATH		"acpi/path"
#define KOSM_ACPI_DEVICE_HID		"acpi/hid"
#define KOSM_ACPI_DEVICE_CID		"acpi/cid"
#define KOSM_ACPI_DEVICE_UID		"acpi/uid"
#define KOSM_ACPI_DEVICE_ADR		"acpi/adr"
#define KOSM_ACPI_DEVICE_STATUS		"acpi/status"
#define KOSM_ACPI_DEVICE_TYPE		"acpi/type"
#define KOSM_ACPI_DEVICE_HANDLE		"acpi/handle"

// USB-specific property names
#define KOSM_USB_VENDOR_ID			"usb/vendor"
#define KOSM_USB_PRODUCT_ID			"usb/product"
#define KOSM_USB_BCD_DEVICE			"usb/bcd device"
#define KOSM_USB_DEVICE_CLASS		"usb/class"
#define KOSM_USB_DEVICE_SUBCLASS	"usb/subclass"
#define KOSM_USB_DEVICE_PROTOCOL	"usb/protocol"
#define KOSM_USB_INTERFACE_CLASS	"usb/if class"
#define KOSM_USB_INTERFACE_SUBCLASS	"usb/if subclass"
#define KOSM_USB_INTERFACE_PROTOCOL	"usb/if protocol"

// I2C-specific property names
#define KOSM_I2C_ADDRESS			"i2c/address"
#define KOSM_I2C_ADDRESS_10BIT		"i2c/address 10bit"
#define KOSM_I2C_SPEED				"i2c/speed"

// VirtIO-specific property names
#define KOSM_VIRTIO_DEVICE_ID		"virtio/device"
#define KOSM_VIRTIO_VENDOR_ID		"virtio/vendor"
#define KOSM_VIRTIO_TRANSPORT		"virtio/transport"

// Node flags
#define KOSM_FIND_CHILD_ON_DEMAND	0x01
#define KOSM_FIND_MULTIPLE_CHILDREN	0x02
#define KOSM_KEEP_DRIVER_LOADED		0x04

// Special return code from attach(): driver dependencies not yet
// available. DeviceKeeper queues the node and retries after each
// successful attach (Linux -EPROBE_DEFER equivalent).
#define KOSM_DEFER_PROBE			(B_ERRORS_END + 0x5000)

// Firmware search paths (tried in order by load_firmware)
#define KOSM_FIRMWARE_PATH_SYSTEM	"/system/firmware"
#define KOSM_FIRMWARE_PATH_DATA		"/system/data/firmware"

// I/O resource types (for acquire_io_resource / release_io_resource)
enum {
	KOSM_RESOURCE_IO_MEMORY		= 1,
	KOSM_RESOURCE_IO_PORT		= 2,
	KOSM_RESOURCE_DMA_CHANNEL	= 3,
};


// Bus protocol types (for bus_ops_type in dk_driver_info)
enum {
	KOSM_BUS_TYPE_NONE			= 0,
	KOSM_BUS_TYPE_PCI			= 1,
	KOSM_BUS_TYPE_USB			= 2,
	KOSM_BUS_TYPE_I2C			= 3,
	KOSM_BUS_TYPE_SPI			= 4,
	KOSM_BUS_TYPE_SDIO			= 5,
	KOSM_BUS_TYPE_PLATFORM		= 6,
	KOSM_BUS_TYPE_ACPI			= 7,
	KOSM_BUS_TYPE_VIRTIO		= 8,
	KOSM_BUS_TYPE_WMI			= 9,
	KOSM_BUS_TYPE_SCSI			= 10,
	KOSM_BUS_TYPE_ATA			= 11,
	KOSM_BUS_TYPE_MMC			= 12,
	KOSM_BUS_TYPE_HYPERV		= 13,
};


// Node watcher event flags (combinable bitmask for watch_node)
#define KOSM_EVENT_CHILD_REGISTERED		0x01
#define KOSM_EVENT_CHILD_UNREGISTERED	0x02
#define KOSM_EVENT_DEVICE_PUBLISHED		0x04
#define KOSM_EVENT_DEVICE_UNPUBLISHED	0x08


typedef struct dk_node dk_node;

// Node watcher callback.
// event: one of KOSM_EVENT_*
// node: for CHILD_REGISTERED/UNREGISTERED — the child;
//       for DEVICE_PUBLISHED/UNPUBLISHED — the watched node
// path: for DEVICE_PUBLISHED/UNPUBLISHED — the device path; NULL otherwise
typedef void (*dk_node_callback)(void* cookie, uint32 event,
				dk_node* node, const char* path);


// Property descriptor
typedef struct dk_property {
	const char*		name;
	type_code		type;
	union {
		uint8		ui8;
		uint16		ui16;
		uint32		ui32;
		uint64		ui64;
		const char*	string;
		struct {
			const void*	data;
			size_t		length;
		} raw;
		struct {
			const char* const*	items;
			uint32				count;
		} stringlist;
	} value;
} dk_property;


// Matching rule
typedef struct dk_match_rule {
	const char*		name;
	type_code		type;
	union {
		uint8		ui8;
		uint16		ui16;
		uint32		ui32;
		uint64		ui64;
		const char*	string;
	} value;
} dk_match_rule;


// Matching dictionary
typedef struct dk_match_dict {
	const dk_match_rule*	rules;
	int32					priority;
} dk_match_dict;


typedef struct IORequest io_request;
struct selectsync;


// I/O resource descriptor (for register_node resource binding)
typedef struct dk_io_resource {
	uint32			type;
		// KOSM_RESOURCE_IO_MEMORY, KOSM_RESOURCE_IO_PORT,
		// or KOSM_RESOURCE_DMA_CHANNEL
	generic_addr_t	base;
	generic_addr_t	length;
} dk_io_resource;


// Device operations (exposed to userland via devfs)
typedef struct dk_device_ops {
	status_t	(*open)(void* deviceCookie, const char* path,
					int openMode, void** _cookie);
	status_t	(*close)(void* cookie);
	status_t	(*free)(void* cookie);
	status_t	(*read)(void* cookie, off_t pos, void* buffer,
					size_t* _length);
	status_t	(*write)(void* cookie, off_t pos, const void* buffer,
					size_t* _length);
	status_t	(*io)(void* cookie, io_request* request);
	status_t	(*control)(void* cookie, uint32 op, void* buffer,
					size_t length);
	status_t	(*select)(void* cookie, uint8 event, selectsync* sync);
	status_t	(*deselect)(void* cookie, uint8 event, selectsync* sync);

	void		(*device_removed)(void* deviceCookie);
		// Called when hardware is physically removed while device
		// is still open. Must unblock any pending I/O.
		// May be NULL if hotplug is not applicable.
} dk_device_ops;


// Unified driver interface
typedef struct dk_driver_info {
	module_info				info;

	const dk_match_dict*	match;

	float					(*probe)(dk_node* node);
		// Optional lightweight check called under the matcher read lock.
		// Must not block on I/O or acquire heavy locks. Should only
		// inspect node properties and return a score: >= 0.0 to accept
		// (higher is better), < 0.0 to reject. May be NULL.

	status_t				(*attach)(dk_node* node, void** _cookie);
		// Called WITHOUT the DeviceKeeper lock. The driver may freely
		// call register_node, publish_device, etc. Return
		// KOSM_DEFER_PROBE if a dependency is not yet available.

	void					(*detach)(void* cookie);
		// Called WITHOUT the DeviceKeeper lock. The driver may freely
		// call unregister_node, unpublish_device, etc.

	status_t				(*rescan_children)(void* cookie);
		// Re-enumerate child devices (USB hub detect, SCSI rescan,
		// MMC card detect). Called via dk_keeper_info::rescan_node.
		// May be NULL if the driver has no dynamic children.

	status_t				(*suspend)(void* cookie, int32 state);
	status_t				(*resume)(void* cookie);

	// Per-device runtime power management.
	// Called when pm_runtime_put drops the last reference (suspend)
	// or pm_runtime_get wakes from idle (resume). May be NULL if
	// the device does not support runtime PM.
	status_t				(*runtime_suspend)(void* cookie);
	status_t				(*runtime_resume)(void* cookie);

	// bus-specific protocol ops for child drivers.
	// Bus managers set this to their typed ops struct (e.g.
	// pci_device_ops, usb_device_ops). Child drivers retrieve
	// it via dk_keeper_info::get_bus_ops(). NULL if not a bus.
	const void*				bus_ops;
	uint32					bus_ops_type;
		// KOSM_BUS_TYPE_PCI, _USB, _I2C, etc.

	dk_device_ops*			ops;
} dk_driver_info;


// DeviceKeeper module API
typedef struct dk_keeper_info {
	module_info info;

	// node management
	status_t	(*register_node)(dk_node* parent, const char* moduleName,
					const dk_property* properties,
					const dk_io_resource* resources,
					dk_node** _node);
	status_t	(*unregister_node)(dk_node* node);
	status_t	(*rescan_node)(dk_node* node);

	// surprise removal: hardware physically disappeared while device
	// may still be open. Notifies device_removed on all published
	// devices in the subtree (unblocking pending I/O), then detaches
	// drivers, releases I/O resources, and removes the node from the
	// tree. Bus drivers call this when hotplug-out is detected.
	void		(*device_removed)(dk_node* node);

	// tree navigation
	dk_node*	(*get_root_node)(void);
	dk_node*	(*get_parent_node)(dk_node* node);
	status_t	(*get_next_child_node)(dk_node* parent,
					const dk_match_rule* attrs,
					dk_node** _node);
	status_t	(*find_child_node)(dk_node* parent,
					const dk_match_rule* attrs,
					dk_node** _node);
		// Deep recursive search through all descendants.
		// Iterative: pass last found node in *_node, NULL to start.
	void		(*put_node)(dk_node* node);

	// global tree query (searches entire tree from root)
	status_t	(*find_node)(const dk_match_rule* rules,
					dk_node** _node);
		// Iterative: pass *_node = NULL to start, previous to continue.
		// Caller must put_node() the returned node when done.
		// Use for cross-device references (FDT phandle, PCI companion,
		// ACPI link) without knowing tree structure.

	// property accessors (recursive: walk parent chain if not found)
	// Copy semantics: string and raw accessors copy data into a
	// caller-provided buffer. Returns B_BUFFER_OVERFLOW if the
	// buffer is too small (string is truncated, raw is clipped).
	// Integer accessors return the value directly.
	status_t	(*get_property_uint8)(const dk_node* node,
					const char* name, uint8* _value,
					bool recursive);
	status_t	(*get_property_uint16)(const dk_node* node,
					const char* name, uint16* _value,
					bool recursive);
	status_t	(*get_property_uint32)(const dk_node* node,
					const char* name, uint32* _value,
					bool recursive);
	status_t	(*get_property_uint64)(const dk_node* node,
					const char* name, uint64* _value,
					bool recursive);
	status_t	(*get_property_string)(const dk_node* node,
					const char* name, char* buffer,
					size_t bufferSize, bool recursive);
	status_t	(*get_property_raw)(const dk_node* node,
					const char* name, void* buffer,
					size_t bufferSize, size_t* _copiedSize,
					bool recursive);

	// property iteration (single node, no parent walk)
	status_t	(*get_next_property)(dk_node* node,
					dk_property** _property);
		// Pass *_property = NULL to get first. Returns B_ENTRY_NOT_FOUND
		// when no more properties. Caller must not free the property.
		// Not synchronized with set_property(): concurrent set_property
		// may add entries that are missed by an ongoing iteration.
		// If a consistent view is required, caller must serialize
		// externally (e.g. only iterate during attach, before the node
		// is published).

	// device publishing
	status_t	(*publish_device)(dk_node* node, const char* path,
					dk_device_ops* ops);
	status_t	(*unpublish_device)(dk_node* node, const char* path);

	// ID generation
	int32		(*create_id)(const char* generator);
	status_t	(*free_id)(const char* generator, uint32 id);

	// driver access
	status_t	(*get_driver)(dk_node* node,
					dk_driver_info** _driver, void** _cookie);

	// bus protocol: retrieve parent's bus-specific ops.
	// Child driver calls this on its own node to get the parent
	// bus manager's typed interface. Returns B_BAD_VALUE if parent
	// does not provide ops of the requested type.
	status_t	(*get_bus_ops)(dk_node* node, uint32 busType,
					const void** _ops, void** _cookie);

	// I/O resource management (prefer passing resources to register_node)
	status_t	(*acquire_io_resource)(uint32 type, generic_addr_t base,
					generic_addr_t length);
	void		(*release_io_resource)(uint32 type, generic_addr_t base);

	// node watcher (kernel-side notification for hotplug, partition scan, etc.)
	status_t	(*watch_node)(dk_node* node, uint32 events,
					dk_node_callback callback, void* cookie);
	status_t	(*unwatch_node)(dk_node* node,
					dk_node_callback callback, void* cookie);

	// runtime power management
	// get: wake device if suspended, increment usage refcount.
	// put: decrement refcount, auto-suspend after idle timeout if 0.
	// Stubs (no-op) until PM subsystem is implemented.
	status_t	(*pm_runtime_get)(dk_node* node);
	void		(*pm_runtime_put)(dk_node* node);
	status_t	(*pm_set_idle_timeout)(dk_node* node, bigtime_t timeout);

	// mutable property update (node lock protects concurrent access)
	// Updates an existing property or adds a new one. Integer types
	// are updated atomically; string values are replaced via
	// strdup + pointer swap.
	status_t	(*set_property)(dk_node* node, const dk_property* prop);

	// firmware loading
	// Searches KOSM_FIRMWARE_PATH_SYSTEM, then KOSM_FIRMWARE_PATH_DATA.
	// Returns a kernel-allocated buffer that must be freed with
	// release_firmware().
	status_t	(*load_firmware)(dk_node* node, const char* name,
					const void** _data, size_t* _size);
	void		(*release_firmware)(const void* data);
} dk_keeper_info;


#endif // _KERNEL_DEVICE_KEEPER_H
