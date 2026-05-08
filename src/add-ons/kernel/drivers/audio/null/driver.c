/*
 * Copyright 2007 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Bek, host.haiku@gmx.de
 */
#include "driver.h"

#include <device_keeper.h>
#include <string.h>


#define NULL_AUDIO_DRIVER_MODULE_NAME "drivers/audio/null_audio/dk_driver_v1"

static dk_keeper_info *sDeviceKeeper;
static bool sPublished = false;

device_t device;


static status_t
null_audio_open(void* driverCookie, const char* name, int flags, void** cookie)
{
	dprintf("null_audio: %s\n", __func__);
	*cookie = &device;
	return B_OK;
}


static status_t
null_audio_read(void* cookie, off_t a, void* b, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}


static status_t
null_audio_write(void* cookie, off_t a, const void* b, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}


static status_t
null_audio_control(void* cookie, uint32 op, void* arg, size_t len)
{
	if (cookie)
		return multi_audio_control(cookie, op, arg, len);
	return B_BAD_VALUE;
}


static status_t
null_audio_close(void* cookie)
{
	device_t* dev = (device_t*)cookie;
	if (dev && dev->running)
		null_stop_hardware(dev);
	return B_OK;
}


static status_t
null_audio_free(void* cookie)
{
	return B_OK;
}


static dk_device_ops sDeviceOps = {
	null_audio_open,
	null_audio_close,
	null_audio_free,
	null_audio_read,
	null_audio_write,
	NULL,	/* io */
	null_audio_control,
	NULL,	/* select */
	NULL,	/* deselect */
	NULL,	/* device_removed */
};


/*	#pragma mark - dk_driver_info */


static const dk_match_rule sNullMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sNullMatchDict = {
	sNullMatchRules,
	0
};


static float
null_audio_probe(dk_node *node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
null_audio_attach(dk_node *node, void **cookie)
{
	device.running = false;

	if (!sPublished) {
		sPublished = true;
		sDeviceKeeper->publish_device(node,
			MULTI_AUDIO_DEV_PATH "/null/0", &sDeviceOps);
	}

	*cookie = node;
	return B_OK;
}


static void
null_audio_detach(void *_cookie)
{
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sNullAudioDriver = {
	.info	= { NULL_AUDIO_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sNullMatchDict,
	.probe	= null_audio_probe,
	.attach	= null_audio_attach,
	.detach	= null_audio_detach,
	.ops	= &sDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sNullAudioDriver,
	NULL
};
