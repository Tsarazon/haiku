/*
 *  Copyright 2024, Diego Roux, diegoroux04 at proton dot me
 *  Distributed under the terms of the MIT License.
 */

#include <device_keeper.h>
#include <virtio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define VIRTIO_SOUND_DRIVER_MODULE_NAME 	"drivers/audio/hmulti/virtio_sound/dk_driver_v1"
#define VIRTIO_SOUND_DEVICE_ID_GEN 			"virtio_sound/device_id"


struct VirtIOSoundDriverInfo {
	dk_node* 				node;
	::virtio_device 		virtio_dev;
	virtio_device_interface*	iface;
	uint32					features;
};

struct VirtIOSoundHandle {
    VirtIOSoundDriverInfo*		info;
};

static dk_keeper_info*		sDeviceKeeper;


const char*
get_feature_name(uint32 feature)
{
	// TODO: Implement this.
	return NULL;
}


static float
SupportsDevice(dk_node* parent)
{
	uint16 deviceType;
	char bus[64];

	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK
		|| sDeviceKeeper->get_property_uint16(parent, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK) {
		return 0.0f;
	}

	if (strcmp(bus, "virtio") != 0)
		return 0.0f;

	if (deviceType != VIRTIO_DEVICE_ID_SOUND)
		return 0.0f;

	return 1.0f;
}


static status_t
InitDriver(dk_node* node, void** cookie)
{
	VirtIOSoundDriverInfo* info = (VirtIOSoundDriverInfo*)malloc(sizeof(VirtIOSoundDriverInfo));

	if (info == NULL)
		return B_NO_MEMORY;

	info->node = node;
	*cookie = info;

	return B_OK;
}


static void
UninitDriver(void* cookie)
{
	free(cookie);
}


static dk_driver_info sVirtioSoundDriver = {
	{
		VIRTIO_SOUND_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	.probe = SupportsDevice,
	.attach = InitDriver,
	.detach = UninitDriver,
};


module_info* modules[] = {
	(module_info*)&sVirtioSoundDriver,
	NULL
};


module_dependency module_dependencies[] = {
	{
		KOSM_DEVICE_KEEPER_MODULE_NAME,
		(module_info**)&sDeviceKeeper
	},
	{}
};
