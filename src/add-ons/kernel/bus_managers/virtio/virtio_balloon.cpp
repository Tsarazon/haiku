/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioBalloonPrivate.h"

#include <new>
#include <stdlib.h>
#include <string.h>


#define VIRTIO_BALLOON_DRIVER_MODULE_NAME \
	"drivers/misc/virtio_balloon/dk_driver_v1"


//	#pragma mark - dk_driver_info


static const dk_match_rule sVirtioBalloonMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "virtio"),
	DK_MATCH_UINT16(VIRTIO_DEVICE_TYPE_ITEM, VIRTIO_DEVICE_ID_BALLOON),
	DK_MATCH_END
};

static const dk_match_dict sVirtioBalloonMatchDict = {
	sVirtioBalloonMatchRules,
	0
};


static status_t
virtio_balloon_attach(dk_node* node, void** _cookie)
{
	CALLED();

	VirtioBalloonDevice* device
		= new(std::nothrow) VirtioBalloonDevice(node);
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
virtio_balloon_detach(void* cookie)
{
	CALLED();
	VirtioBalloonDevice* device = (VirtioBalloonDevice*)cookie;
	delete device;
}


dk_driver_info sVirtioBalloonDriver = {
	.info	= { VIRTIO_BALLOON_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sVirtioBalloonMatchDict,
	.attach	= virtio_balloon_attach,
	.detach	= virtio_balloon_detach,
};
