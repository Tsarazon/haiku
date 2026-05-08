/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioRNGPrivate.h"

#include <new>
#include <stdlib.h>
#include <string.h>


#define VIRTIO_RNG_CONTROLLER_PRETTY_NAME "Virtio RNG Device"

#define VIRTIO_RNG_DRIVER_MODULE_NAME "busses/random/virtio_rng/dk_driver_v1"
#define VIRTIO_RNG_DEVICE_MODULE_NAME "busses/random/virtio_rng/device_v1"


dk_keeper_info *gDeviceKeeper;
random_for_controller_interface *gRandom;
dpc_module_info *gDPC;


//	#pragma mark -	Driver module interface


static const dk_match_rule kVirtioRNGMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "virtio"),
	DK_MATCH_END
};

static const dk_match_dict kVirtioRNGMatch = {
	kVirtioRNGMatchRules,
	0
};


static float
virtio_rng_supports_device(dk_node *parent)
{
	uint16 deviceType;

	// bus already filtered by match rules
	if (gDeviceKeeper->get_property_uint16(parent, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK || deviceType != VIRTIO_DEVICE_ID_ENTROPY)
		return 0.0;

	TRACE("Virtio RNG device found!\n");

	return 0.6f;
}



static status_t
virtio_rng_init_driver(dk_node *node, void **_cookie)
{
	CALLED();
	VirtioRNGDevice *device =  new(std::nothrow) VirtioRNGDevice(node);
	if (device == NULL)
		return B_NO_MEMORY;
	status_t status = device->InitCheck();
	if (status < B_OK) {
		delete device;
		return status;
	}
	*_cookie = device;

	return B_OK;
}


static void
virtio_rng_uninit_driver(void *cookie)
{
	VirtioRNGDevice *device = (VirtioRNGDevice*)cookie;
	delete device;
}


static dk_driver_info sVirtioRNGDriver = {
	.info = {
		VIRTIO_RNG_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &kVirtioRNGMatch,
	.probe   = virtio_rng_supports_device,
	.attach  = virtio_rng_init_driver,
	.detach  = virtio_rng_uninit_driver,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ RANDOM_FOR_CONTROLLER_MODULE_NAME, (module_info **)&gRandom },
	{ B_DPC_MODULE_NAME, (module_info **)&gDPC },
	{}
};


module_info *modules[] = {
	(module_info *)&sVirtioRNGDriver,
	NULL
};
