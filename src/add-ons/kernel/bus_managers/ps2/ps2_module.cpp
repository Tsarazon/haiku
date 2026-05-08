/*
 * Copyright 2005-2007 Marcus Overhagen
 * Distributed under the terms of the MIT License.
 *
 * PS/2 bus manager — DeviceKeeper integration
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


#define PS2_ROOT_MODULE_NAME		"bus_managers/ps2/root/dk_driver_v1"


//	#pragma mark - driver module (root bus)


// PS/2 is a legacy x86 controller discovered via fixed I/O ports,
// not via bus enumeration. It attaches once at the root node; probe
// over ISA/ACPI subtrees caused 60+ spurious attach calls on every
// rescan. The match dict below pins it to the root node, so probe
// is invoked exactly once per rescan cycle.
static const dk_match_rule sPS2MatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sPS2MatchDict = {
	sPS2MatchRules,
	0
};


static float
ps2_supports_device(dk_node* parent)
{
	// Match dict already guarantees bus="root"; the probe just
	// gates re-attach when we're already initialized.
	if (gPS2DeviceNode != NULL)
		return 0.0;
	return 0.6;
}


static status_t
ps2_init_driver(dk_node* node, void** cookie)
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
	if (gPS2DeviceNode == (dk_node*)cookie) {
		ps2_uninit();
		gPS2DeviceNode = NULL;
	}
}


//	#pragma mark - keyboard device ops


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


//	#pragma mark - pointing device ops


static device_hooks* sActivePointingHooks = NULL;

static status_t
ps2_pointing_open(void* info, const char* path, int openMode, void** cookie)
{
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
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ NULL }
};


dk_device_ops sPS2KeyboardDeviceOps = {
	ps2_keyboard_open, ps2_keyboard_close, ps2_keyboard_free,
	ps2_keyboard_read, ps2_keyboard_write, NULL, ps2_keyboard_control,
	NULL, NULL
};

dk_device_ops sPS2PointingDeviceOps = {
	ps2_pointing_open, ps2_pointing_close, ps2_pointing_free,
	ps2_pointing_read, ps2_pointing_write, NULL, ps2_pointing_control,
	NULL, NULL
};

static dk_driver_info sPS2Driver = {
	.info	= { PS2_ROOT_MODULE_NAME, 0, NULL },
	.match	= &sPS2MatchDict,
	.probe	= ps2_supports_device,
	.attach	= ps2_init_driver,
	.detach	= ps2_uninit_driver,
};


// Keep legacy ps2_module_info for ps2_hid compatibility
static int32 function1() { return 0; }
static int32 function2() { return 0; }

static status_t
legacy_std_ops(int32 op, ...)
{
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
	(module_info*)&ps2_module,
	NULL
};
