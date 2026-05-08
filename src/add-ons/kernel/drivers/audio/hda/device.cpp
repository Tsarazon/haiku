/*
 * Copyright 2007-2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar Adema, ithamar AT unet DOT nl
 */


#include "driver.h"

#include <device_keeper.h>


static status_t
hda_open(void* deviceCookie, const char* name, int openMode, void** _cookie)
{
	// deviceCookie is the hda_controller* set as node cookie in attach().
	// DeviceKeeper's Publisher forwards fNode->DriverCookie() here, so
	// each published devfs path resolves directly to its owning card.
	hda_controller* controller = (hda_controller*)deviceCookie;
	if (controller == NULL)
		return ENODEV;

	if (atomic_get(&controller->opened) != 0)
		return B_BUSY;

	status_t status = hda_hw_init(controller);
	if (status != B_OK)
		return status;

	atomic_add(&controller->opened, 1);

	*_cookie = controller;

	// optional user-settable buffer frames and count
	get_settings_from_file();

	return B_OK;
}


static status_t
hda_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_IO_ERROR;
}


static status_t
hda_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_IO_ERROR;
}


static status_t
hda_control(void* cookie, uint32 op, void* arg, size_t length)
{
	hda_controller* controller = (hda_controller*)cookie;
	if (controller->active_codec != NULL)
		return multi_audio_control(controller->active_codec, op, arg, length);

	return B_BAD_VALUE;
}


static status_t
hda_close(void* cookie)
{
	hda_controller* controller = (hda_controller*)cookie;
	hda_hw_stop(controller);
	atomic_add(&controller->opened, -1);

	return B_OK;
}


static status_t
hda_free(void* cookie)
{
	hda_controller* controller = (hda_controller*)cookie;
	hda_hw_uninit(controller);

	return B_OK;
}


dk_device_ops gHdaDeviceOps = {
	hda_open,
	hda_close,
	hda_free,
	hda_read,
	hda_write,
	NULL,	// io
	hda_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};
