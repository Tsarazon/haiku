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
#define KOSM_LABEL					"device/label"
#define KOSM_DEVICE_BUS				"device/bus"
#define KOSM_DEVICE_FLAGS			"device/flags"
#define KOSM_DEVICE_COMPATIBLE		"device/compatible"
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




// Node watcher event flags (combinable bitmask for watch_node)
#define KOSM_EVENT_CHILD_REGISTERED		0x01
#define KOSM_EVENT_CHILD_UNREGISTERED	0x02
#define KOSM_EVENT_DEVICE_PUBLISHED		0x04
#define KOSM_EVENT_DEVICE_UNPUBLISHED	0x08


// get_interface flags (bitmask, mandatory parameter).
//
// KOSM_INTERFACE_ANCESTORS: walk up the parent chain (skips self).
//   Use when you want to find a parent bus manager's interface.
//
// KOSM_INTERFACE_SELF: check the node itself only.
//   Use to read back an interface published on the same node.
//
// The two flags may be combined to check self first, then ancestors.
//
// There is no implicit default — every call site must explicitly
// state its intent. This makes the search direction visible in code
// review and prevents subtle bugs from a driver accidentally finding
// (or not finding) its own published ops.
#define KOSM_INTERFACE_ANCESTORS		0x01
#define KOSM_INTERFACE_SELF				0x02


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


// Unified driver interface.
//
// Every DeviceKeeper driver exports one dk_driver_info as a module.
// Not all fields are used by every driver — zero-initialize with
// C99 designated initializers, fill only what applies.
//
// === Leaf driver (e.g. NIC, audio, block device) ===
//
//   static dk_driver_info sDriver = {
//       .info       = { MY_MODULE_NAME, 0, std_ops },
//       .match      = &sMatchDict,
//       .attach     = my_attach,
//       .detach     = my_detach,
//       .ops        = &sDeviceOps,
//   };
//
// === Bus manager (e.g. PCI, USB, VirtIO transport) ===
//
//   static dk_driver_info sDriver = {
//       .info       = { MY_MODULE_NAME, 0, std_ops },
//       .match      = &sMatchDict,
//       .attach     = my_attach,
//       .detach     = my_detach,
//       .node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
//   };
//
// Bus managers publish their typed interface in attach():
//
//   status_t my_attach(dk_node* node, void** _cookie) {
//       ...
//       keeper->publish_interface(node, PCI_DEVICE_INTERFACE_NAME,
//                                 &sPCIOps);
//       ...
//   }
//
// Child drivers retrieve it in their own attach():
//
//   const pci_device_ops* pci;
//   void* cookie;
//   keeper->get_interface(node, PCI_DEVICE_INTERFACE_NAME,
//                         (const void**)&pci, &cookie);
//
// Each bus defines PCI_DEVICE_INTERFACE_NAME (or equivalent) and a
// typed inline wrapper in its header. See DK_MATCH_* / DK_PROP_*
// macros below for match rule and property helpers.
//
typedef struct dk_driver_info {
	module_info				info;

	const dk_match_dict*	match;
		// Declarative matching rules. If non-NULL, the framework checks
		// these before calling probe(). Use DK_MATCH_* macros to build.
		// NULL for drivers that match only by module name (auto-attach).

	float					(*probe)(dk_node* node);
		// Optional lightweight check called under the matcher read lock.
		// Must not block on I/O or acquire heavy locks. Should only
		// inspect node properties and return a score: >= 0.0 to accept
		// (higher is better), < 0.0 to reject. May be NULL.

	status_t				(*attach)(dk_node* node, void** _cookie);
		// Called WITHOUT the DeviceKeeper lock. The driver may freely
		// call register_node, publish_device, publish_interface, etc.
		// Return KOSM_DEFER_PROBE if a dependency is not yet available.

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

	dk_device_ops*			ops;
		// Device I/O operations exposed to userland via devfs.
		// Set by leaf drivers that call publish_device().
		// NULL for bus managers that don't publish devices.

	uint32					node_flags;
		// Flags propagated to every node this driver attaches to.
		// The framework ORs these into the node's flags after attach().
		// Bus managers typically set KOSM_FIND_MULTIPLE_CHILDREN here
		// so that child probe happens automatically.
		// 0 for leaf drivers.
} dk_driver_info;


// DeviceKeeper module API
typedef struct dk_keeper_info {
	module_info info;

	// node management
	//
	// register_node: create a new node under `parent` with the given
	// module name, properties, and I/O resources.
	//
	// Duplicate detection: if a child already exists under `parent`
	// with the same moduleName AND every property listed in
	// `properties` has the same type and value on that existing
	// child, register_node returns B_NAME_IN_USE without creating a
	// new node (and without modifying resources). Bus managers rely
	// on this to make rescan_children idempotent — callers may loop
	// their enumeration at any time and only genuinely new children
	// will be registered.
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

	// global tree query (searches entire tree from root).
	// Disambiguates from find_child_node (which searches a subtree).
	status_t	(*find_node_global)(const dk_match_rule* rules,
					dk_node** _node);
		// Iterative: pass *_node = NULL to start, previous to continue.
		// Caller must put_node() the returned node when done.
		// Use for cross-device references (FDT phandle, PCI companion,
		// ACPI link) without knowing tree structure.

	// property accessors (recursive: walk parent chain if not found).
	// Copy semantics: string and raw accessors copy data into a
	// caller-provided buffer. Returns B_BUFFER_OVERFLOW if the
	// buffer is too small (string is truncated, raw is clipped).
	// Integer accessors return the value directly.
	//
	// get_property_string: _actualLength (if non-NULL) is set to
	// the source string length EXCLUDING the NUL terminator. On
	// B_BUFFER_OVERFLOW, callers can use this to allocate a buffer
	// of size (actualLength + 1) and retry.
	//
	// get_property_raw: _copiedSize is the number of bytes actually
	// written. On B_BUFFER_OVERFLOW, the caller can read entry size
	// from the property and re-allocate.
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
					size_t bufferSize, size_t* _actualLength,
					bool recursive);
	status_t	(*get_property_raw)(const dk_node* node,
					const char* name, void* buffer,
					size_t bufferSize, size_t* _copiedSize,
					bool recursive);

	// property iteration (single node, no parent walk).
	//
	// Pass *_property = NULL to get first. Returns B_ENTRY_NOT_FOUND
	// when no more properties. Caller must not free the property.
	//
	// _iterVersion is INPUT/OUTPUT: pass an uninitialized uint64*
	// on the first call (the function will snapshot the property
	// store's version into it). On subsequent calls, the function
	// compares the snapshot to the current version and returns
	// B_INTERRUPTED if the store has been mutated mid-iteration —
	// caller should restart from *_property = NULL.
	//
	// Usage:
	//   dk_property* prop = NULL;
	//   uint64 version = 0;
	//   while (gDeviceKeeper->get_next_property(node, &prop, &version) == B_OK) {
	//       // process prop
	//   }
	status_t	(*get_next_property)(dk_node* node,
					dk_property** _property, uint64* _iterVersion);

	// device publishing
	status_t	(*publish_device)(dk_node* node, const char* path,
					dk_device_ops* ops);
	status_t	(*unpublish_device)(dk_node* node, const char* path);

	// ID generation
	int32		(*create_id)(const char* generator);
	status_t	(*free_id)(const char* generator, uint32 id);

	// driver access: returns the dk_driver_info attached to a node
	// and its driver cookie. Distinct from get_interface (which
	// returns bus operation tables) — get_node_driver gives you the
	// raw driver record, useful only for introspection (rescan,
	// power management, debugging).
	status_t	(*get_node_driver)(dk_node* node,
					dk_driver_info** _driver, void** _cookie);

	// bus interface: bus managers publish a typed ops struct on their
	// node; child drivers retrieve it by name from any ancestor.
	//
	// publish_interface: called by bus managers in attach() to export
	// their typed ops struct. The name is a string constant defined
	// in the bus header (e.g. PCI_DEVICE_INTERFACE_NAME). A node may
	// publish multiple interfaces (dual-personality drivers — e.g.
	// SDHCI as both PCI consumer and MMC producer). Re-publishing
	// the same name replaces the existing ops in place. The ops
	// pointer must remain valid until detach().
	//
	// get_interface: called by child drivers in attach() to retrieve
	// a bus manager's ops. The flags argument is mandatory and
	// controls the search direction:
	//
	//   KOSM_INTERFACE_ANCESTORS — walk up the parent chain
	//     (skips self). Use this when you want to find a parent's
	//     bus interface (the common case).
	//
	//   KOSM_INTERFACE_SELF — check the node itself only.
	//     Use this when you want to read back your own published
	//     interface or when you know the bus manager is attached
	//     to the same node (e.g. SDHCI publishing MMC interface
	//     on its own node).
	//
	//   KOSM_INTERFACE_SELF | KOSM_INTERFACE_ANCESTORS — check self
	//     first, then walk up parents.
	//
	// Returns the ops pointer and the ancestor's driver cookie.
	// Returns B_NOT_SUPPORTED if no matching interface is found.
	//
	// Usage in bus manager attach():
	//   keeper->publish_interface(node, PCI_DEVICE_INTERFACE_NAME,
	//                             &sPCIOps);
	//
	// Usage in child driver attach():
	//   const pci_device_ops* pci;
	//   void* cookie;
	//   keeper->get_interface(node, PCI_DEVICE_INTERFACE_NAME,
	//                         KOSM_INTERFACE_ANCESTORS,
	//                         (const void**)&pci, &cookie);
	//
	status_t	(*publish_interface)(dk_node* node, const char* name,
					const void* ops);
	status_t	(*get_interface)(dk_node* node, const char* name,
					uint32 flags, const void** _ops, void** _cookie);

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


// Logging macros.
// DK_INFO: always on (init, attach, register, errors).
// DK_TRACE: verbose, on only with DEVICE_KEEPER_VERBOSE.
// DK_ERROR: always on, prefixed with "ERROR:".
#define DK_INFO(fmt, ...)  dprintf("DK: " fmt, ##__VA_ARGS__)
#define DK_ERROR(fmt, ...) dprintf("DK: ERROR: " fmt, ##__VA_ARGS__)

#ifdef DEVICE_KEEPER_VERBOSE
#define DK_TRACE(fmt, ...) dprintf("DK: " fmt, ##__VA_ARGS__)
#else
#define DK_TRACE(fmt, ...) do {} while (0)
#endif


// Convenience macros for dk_match_rule arrays.
// Usage:
//   static const dk_match_rule sMatch[] = {
//       DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
//       DK_MATCH_UINT16(KOSM_DEVICE_VENDOR_ID, 0x8086),
//       DK_MATCH_UINT16(KOSM_DEVICE_ID, 0x100e),
//       DK_MATCH_END
//   };

#define DK_MATCH_UINT8(_name, _val)		\
	{ (_name), B_UINT8_TYPE, { .ui8 = (_val) } }
#define DK_MATCH_UINT16(_name, _val)	\
	{ (_name), B_UINT16_TYPE, { .ui16 = (_val) } }
#define DK_MATCH_UINT32(_name, _val)	\
	{ (_name), B_UINT32_TYPE, { .ui32 = (_val) } }
#define DK_MATCH_UINT64(_name, _val)	\
	{ (_name), B_UINT64_TYPE, { .ui64 = (_val) } }
#define DK_MATCH_STRING(_name, _val)	\
	{ (_name), B_STRING_TYPE, { .string = (_val) } }
#define DK_MATCH_END					\
	{ NULL, 0, { .ui64 = 0 } }


// Convenience macros for dk_property arrays.
// Usage:
//   dk_property props[] = {
//       DK_PROP_STRING(KOSM_DEVICE_BUS, "pci"),
//       DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
//       DK_PROP_END
//   };

#define DK_PROP_UINT8(_name, _val)		\
	{ (_name), B_UINT8_TYPE, { .ui8 = (_val) } }
#define DK_PROP_UINT16(_name, _val)		\
	{ (_name), B_UINT16_TYPE, { .ui16 = (_val) } }
#define DK_PROP_UINT32(_name, _val)		\
	{ (_name), B_UINT32_TYPE, { .ui32 = (_val) } }
#define DK_PROP_UINT64(_name, _val)		\
	{ (_name), B_UINT64_TYPE, { .ui64 = (_val) } }
#define DK_PROP_STRING(_name, _val)		\
	{ (_name), B_STRING_TYPE, { .string = (_val) } }
#define DK_PROP_END						\
	{ NULL, 0, { .ui64 = 0 } }


// Convenience macros for dk_io_resource arrays.
// The framework iterates resources until it sees an entry with
// type == 0; DK_RESOURCE_END produces such a sentinel. Always
// terminate dk_io_resource arrays with DK_RESOURCE_END.
//
// Usage:
//   dk_io_resource resources[] = {
//       { KOSM_RESOURCE_IO_MEMORY, 0xfed00000, 0x1000 },
//       { KOSM_RESOURCE_IO_PORT,   0x60,       0x4    },
//       DK_RESOURCE_END
//   };

#define DK_RESOURCE_END					\
	{ 0, 0, 0 }


#endif // _KERNEL_DEVICE_KEEPER_H
