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

#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>

#include <string.h>
#include <termios.h>


#define DEVICE_NAME "dprintf"
#define DPRINTF_DRIVER_MODULE_NAME "drivers/common/dprintf/driver_v1"
#define DPRINTF_DEVICE_MODULE_NAME "drivers/common/dprintf/device_v1"

static device_manager_info* sDeviceManager;
static bool sPublished = false;


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
dprintf_supports_device(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if ((strcmp(bus, "root") == 0 || strcmp(bus, "pci") == 0)
		&& !sPublished)
		return 0.01;
	return 0.0;
}


static status_t
dprintf_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Debug Output"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node, DPRINTF_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
dprintf_init_driver(device_node* node, void** cookie)
{
	*cookie = node;
	return B_OK;
}

static void dprintf_uninit_driver(void* c) { sPublished = false; }

static status_t
dprintf_register_child_devices(void* _cookie)
{
	device_node* node = (device_node*)_cookie;
	if (sPublished) return B_OK;
	sPublished = true;
	return sDeviceManager->publish_device(node, DEVICE_NAME,
		DPRINTF_DEVICE_MODULE_NAME);
}

static status_t dprintf_init_device(void* i, void** c)
{ *c = i; return B_OK; }
static void dprintf_uninit_device(void* c) {}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ NULL }
};

struct device_module_info sDprintfDevice = {
	{ DPRINTF_DEVICE_MODULE_NAME, 0, NULL },
	dprintf_init_device, dprintf_uninit_device, NULL,
	dprintf_open, dprintf_close, dprintf_free,
	dprintf_read, dprintf_write, NULL, dprintf_ioctl,
	NULL, NULL
};

struct driver_module_info sDprintfDriver = {
	{ DPRINTF_DRIVER_MODULE_NAME, 0, NULL },
	dprintf_supports_device, dprintf_register_device,
	dprintf_init_driver, dprintf_uninit_driver,
	dprintf_register_child_devices, NULL, NULL
};

module_info* modules[] = {
	(module_info*)&sDprintfDriver,
	(module_info*)&sDprintfDevice,
	NULL
};
