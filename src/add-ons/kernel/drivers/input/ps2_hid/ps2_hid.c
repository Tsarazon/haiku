/*
 * Copyright 2005-2009 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * PS/2 hid device driver
 *
 * Authors (in chronological order):
 *		Marcus Overhagen (marcus@overhagen.de)
 */


#include <device_keeper.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <string.h>

#include "PS2.h"

#define TRACE(x) dprintf x

#define PS2_HID_DRIVER_MODULE_NAME "drivers/input/ps2_hid/dk_driver_v1"

static dk_keeper_info *sDeviceKeeper;
static bool sPublished = false;

ps2_module_info *gPs2 = NULL;


static const dk_match_rule sPs2HidMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sPs2HidMatchDict = {
	sPs2HidMatchRules,
	0
};


static float
ps2_hid_probe(dk_node *node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
ps2_hid_attach(dk_node *node, void **cookie)
{
	TRACE(("ps2_hid: attach\n"));
	status_t status = get_module(B_PS2_MODULE_NAME, (module_info **)&gPs2);
	if (status != B_OK)
		return status;
	sPublished = true;
	*cookie = node;
	return B_OK;
}


static void
ps2_hid_detach(void *_cookie)
{
	TRACE(("ps2_hid: detach\n"));
	put_module(B_PS2_MODULE_NAME);
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sPs2HidDriver = {
	.info	= { PS2_HID_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sPs2HidMatchDict,
	.probe	= ps2_hid_probe,
	.attach	= ps2_hid_attach,
	.detach	= ps2_hid_detach,
	/* no .ops — ps2_hid does not publish a devfs device */
};

module_info *modules[] = {
	(module_info *)&sPs2HidDriver,
	NULL
};
