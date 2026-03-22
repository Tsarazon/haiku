/*
 * Copyright 2005-2007 Marcus Overhagen
 * Distributed under the terms of the MIT License.
 *
 * PS/2 bus manager — device_manager integration
 */


#include "PS2.h"
#include "ps2_common.h"
#include "ps2_dev.h"

#include <string.h>

//#define TRACE_PS2_MODULE
#ifdef TRACE_PS2_MODULE
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...)
#endif


#define PS2_ROOT_MODULE_NAME		"bus_managers/ps2/root/driver_v1"
#define PS2_KEYBOARD_DEVICE_MODULE	"bus_managers/ps2/keyboard/device_v1"
#define PS2_POINTING_DEVICE_MODULE	"bus_managers/ps2/pointing/device_v1"


//	#pragma mark - driver module (root bus)


static float
ps2_supports_device(device_node* parent)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	// PS/2 is present on x86 systems — match root, ISA, or ACPI
	if (strcmp(bus, "root") == 0 || strcmp(bus, "isa") == 0
		|| strcmp(bus, "acpi") == 0)
		return 0.6;

	return 0.0;
}


static status_t
ps2_register_device(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PS/2 Bus"} },
		{ B_DEVICE_BUS, B_STRING_TYPE, {.string = "ps2"} },
		{ B_DEVICE_FLAGS, B_UINT32_TYPE,
			{.ui32 = B_FIND_MULTIPLE_CHILDREN | B_KEEP_DRIVER_LOADED} },
		{ NULL }
	};

	return gDeviceManager->register_node(parent, PS2_ROOT_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
ps2_init_driver(device_node* node, void** cookie)
{
	TRACE("ps2: init_driver\n");

	// Only initialize once
	if (gPS2DeviceNode != NULL) {
		*cookie = node;
		return B_OK;
	}

	gPS2DeviceNode = node;
	status_t status = ps2_init();
	if (status != B_OK) {
		gPS2DeviceNode = NULL;
		return status;
	}

	*cookie = node;
	return B_OK;
}


static void
ps2_uninit_driver(void* cookie)
{
	TRACE("ps2: uninit_driver\n");
	if (gPS2DeviceNode == (device_node*)cookie) {
		ps2_uninit();
		gPS2DeviceNode = NULL;
	}
}


static status_t
ps2_register_child_devices(void* cookie)
{
	// Children are published dynamically by ps2_dev_publish()
	// when devices are detected on the PS/2 bus.
	return B_OK;
}


//	#pragma mark - keyboard device module


static status_t ps2_keyboard_init_device(void* i, void** c)
{ *c = i; return B_OK; }
static void ps2_keyboard_uninit_device(void* c) {}

static status_t
ps2_keyboard_open(void* info, const char* path, int openMode, void** cookie)
{ return gKeyboardDeviceHooks.open(path, openMode, cookie); }

static status_t ps2_keyboard_close(void* cookie)
{ return gKeyboardDeviceHooks.close(cookie); }
static status_t ps2_keyboard_free(void* cookie)
{ return gKeyboardDeviceHooks.free(cookie); }
static status_t ps2_keyboard_read(void* cookie, off_t pos, void* buf,
	size_t* len)
{ return gKeyboardDeviceHooks.read(cookie, pos, buf, len); }
static status_t ps2_keyboard_write(void* cookie, off_t pos, const void* buf,
	size_t* len)
{ return gKeyboardDeviceHooks.write(cookie, pos, buf, len); }
static status_t ps2_keyboard_control(void* cookie, uint32 op, void* buf,
	size_t len)
{ return gKeyboardDeviceHooks.control(cookie, op, buf, len); }


//	#pragma mark - pointing device module


static status_t ps2_pointing_init_device(void* i, void** c)
{ *c = i; return B_OK; }
static void ps2_pointing_uninit_device(void* c) {}

static device_hooks* sActivePointingHooks = NULL;

static status_t
ps2_pointing_open(void* info, const char* path, int openMode, void** cookie)
{
	// Find which pointing device this is and use its specific hooks
	for (int i = 0; i < PS2_DEVICE_COUNT; i++) {
		if (ps2_device[i].name != NULL
			&& strcmp(path, ps2_device[i].name) == 0
			&& !(atomic_get(&ps2_device[i].flags) & PS2_FLAG_KEYB)) {
			device_hooks* hooks = NULL;
			status_t status = ps2_dev_detect_pointing(&ps2_device[i], &hooks);
			if (status == B_OK && hooks != NULL) {
				sActivePointingHooks = hooks;
				return hooks->open(path, openMode, cookie);
			}
		}
	}
	return B_ENTRY_NOT_FOUND;
}

static status_t ps2_pointing_close(void* cookie)
{ return sActivePointingHooks ? sActivePointingHooks->close(cookie) : B_ERROR; }
static status_t ps2_pointing_free(void* cookie)
{ return sActivePointingHooks ? sActivePointingHooks->free(cookie) : B_ERROR; }
static status_t ps2_pointing_read(void* cookie, off_t pos, void* buf,
	size_t* len)
{ return sActivePointingHooks ? sActivePointingHooks->read(cookie, pos, buf, len) : B_ERROR; }
static status_t ps2_pointing_write(void* cookie, off_t pos, const void* buf,
	size_t* len)
{ return sActivePointingHooks ? sActivePointingHooks->write(cookie, pos, buf, len) : B_ERROR; }
static status_t ps2_pointing_control(void* cookie, uint32 op, void* buf,
	size_t len)
{ return sActivePointingHooks ? sActivePointingHooks->control(cookie, op, buf, len) : B_ERROR; }


//	#pragma mark - module exports


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ NULL }
};


static struct device_module_info sPS2KeyboardDevice = {
	{ PS2_KEYBOARD_DEVICE_MODULE, 0, NULL },
	ps2_keyboard_init_device, ps2_keyboard_uninit_device, NULL,
	ps2_keyboard_open, ps2_keyboard_close, ps2_keyboard_free,
	ps2_keyboard_read, ps2_keyboard_write, NULL, ps2_keyboard_control,
	NULL, NULL
};

static struct device_module_info sPS2PointingDevice = {
	{ PS2_POINTING_DEVICE_MODULE, 0, NULL },
	ps2_pointing_init_device, ps2_pointing_uninit_device, NULL,
	ps2_pointing_open, ps2_pointing_close, ps2_pointing_free,
	ps2_pointing_read, ps2_pointing_write, NULL, ps2_pointing_control,
	NULL, NULL
};

static struct driver_module_info sPS2Driver = {
	{ PS2_ROOT_MODULE_NAME, 0, NULL },
	ps2_supports_device, ps2_register_device,
	ps2_init_driver, ps2_uninit_driver,
	ps2_register_child_devices, NULL, NULL
};


// Keep legacy ps2_module_info for ps2_hid compatibility
static int32 function1() { return 0; }
static int32 function2() { return 0; }

static status_t
legacy_std_ops(int32 op, ...)
{
	// No-op — initialization now happens through device_manager
	return B_OK;
}

static ps2_module_info ps2_module = {
	{
		{
			B_PS2_MODULE_NAME,
			B_KEEP_LOADED,
			legacy_std_ops
		},
		NULL
	},
	&function1,
	&function2,
};


module_info *modules[] = {
	(module_info*)&sPS2Driver,
	(module_info*)&sPS2KeyboardDevice,
	(module_info*)&sPS2PointingDevice,
	(module_info*)&ps2_module,
	NULL
};
