/*
 * Copyright 2005-2007, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Author(s):
 *   Ingo Weinhold <bonefish@users.sourceforge.net>
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <debug.h>

#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>

#include <string.h>
#include <termios.h>


#define DEVICE_NAME "dprintf"
#define DPRINTF_DRIVER_MODULE_NAME "drivers/common/dprintf/dk_driver_v1"

static dk_keeper_info* sDeviceKeeper;


static status_t
dprintf_open(void* _info, const char* name, int openMode, void** cookie)
{
	*cookie = NULL;
	return B_OK;
}


static status_t
dprintf_close(void* cookie)
{
	return B_OK;
}


static status_t
dprintf_free(void* cookie)
{
	return B_OK;
}


static status_t
dprintf_ioctl(void* cookie, uint32 op, void* buffer, size_t length)
{
	if (op == TCGETA)
		return B_OK;

	return EPERM;
}


static status_t
dprintf_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	*_length = 0;
	return B_OK;
}


static status_t
dprintf_write(void* cookie, off_t pos, const void* buffer, size_t* _length)
{
	const char* str = (const char*)buffer;

	int bytesLeft = *_length;
	while (bytesLeft > 0) {
		ssize_t size = user_strlcpy(NULL, str, 0);
		if (size < 0)
			return 0;
		int chunkSize = min_c(bytesLeft, (int)size);

		if (chunkSize == 0) {
			str++;
			bytesLeft--;
			continue;
		}

		if (chunkSize == bytesLeft) {
			while (bytesLeft > 0) {
				chunkSize = bytesLeft;

				char localBuffer[512];
				if (bytesLeft > (int)sizeof(localBuffer) - 1)
					chunkSize = (int)sizeof(localBuffer) - 1;
				if (user_memcpy(localBuffer, str, chunkSize) < B_OK)
					return B_BAD_ADDRESS;
				localBuffer[chunkSize] = '\0';

				debug_puts(localBuffer, chunkSize);

				str += chunkSize;
				bytesLeft -= chunkSize;
			}
		} else {
			debug_puts(str, chunkSize);

			str += chunkSize + 1;
			bytesLeft -= chunkSize + 1;
		}
	}

	return B_OK;
}


//	#pragma mark - driver module API


static float
dprintf_supports_device(dk_node* parent)
{
	char bus[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;
	if (strcmp(bus, "generic") == 0)
		return 0.01f;
	return -1.0f;
}


static dk_device_ops sDprintfDeviceOps = {
	dprintf_open, dprintf_close, dprintf_free,
	dprintf_read, dprintf_write, NULL, dprintf_ioctl,
	NULL, NULL,
	NULL	// device_removed
};


static status_t
dprintf_init_driver(dk_node* node, void** cookie)
{
	sDeviceKeeper->publish_device(node, DEVICE_NAME, &sDprintfDeviceOps);
	*cookie = node;
	return B_OK;
}

static void dprintf_uninit_driver(void* c) { }


static const dk_match_rule sDprintfMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sDprintfMatchDict = {
	sDprintfMatchRules,
	0
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

static dk_driver_info sDprintfDriver = {
	.info	= { DPRINTF_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sDprintfMatchDict,
	.probe	= dprintf_supports_device,
	.attach	= dprintf_init_driver,
	.detach	= dprintf_uninit_driver,
	.ops	= &sDprintfDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sDprintfDriver,
	NULL
};
