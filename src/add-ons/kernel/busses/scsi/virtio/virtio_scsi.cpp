/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioSCSIPrivate.h"

#include <new>
#include <stdlib.h>
#include <string.h>


#define VIRTIO_SCSI_ID_GENERATOR "virtio_scsi/id"
#define VIRTIO_SCSI_ID_ITEM "virtio_scsi/id"
#define VIRTIO_SCSI_BRIDGE_PRETTY_NAME "Virtio SCSI Bridge"
#define VIRTIO_SCSI_CONTROLLER_PRETTY_NAME "Virtio SCSI Controller"

#define VIRTIO_SCSI_DEVICE_MODULE_NAME "busses/scsi/virtio_scsi/dk_driver_v1"
#define VIRTIO_SCSI_SIM_MODULE_NAME "busses/scsi/virtio_scsi/sim/dk_driver_v1"


dk_keeper_info *gDeviceKeeper;
scsi_for_sim_interface *gSCSI;


//	#pragma mark - SIM module interface


static void
set_scsi_bus(scsi_sim_cookie cookie, scsi_bus bus)
{
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	sim->SetBus(bus);
}


static void
scsi_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	if (sim->ExecuteRequest(request) == B_BUSY)
		gSCSI->requeue(request, true);
}


static uchar
abort_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	return sim->AbortRequest(request);
}


static uchar
reset_device(scsi_sim_cookie cookie, uchar targetID, uchar targetLUN)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	return sim->ResetDevice(targetID, targetLUN);
}


static uchar
terminate_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	return sim->TerminateRequest(request);
}


static uchar
path_inquiry(scsi_sim_cookie cookie, scsi_path_inquiry *info)
{
	CALLED();

	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	if (sim->Bus() == NULL)
		return SCSI_NO_HBA;

	sim->PathInquiry(info);
	return SCSI_REQ_CMP;
}


//! this is called immediately before the SCSI bus manager scans the bus
static uchar
scan_bus(scsi_sim_cookie cookie)
{
	CALLED();

	return SCSI_REQ_CMP;
}


static uchar
reset_bus(scsi_sim_cookie cookie)
{
	CALLED();

	return SCSI_REQ_CMP;
}


/*!	Get restrictions of one device
	(used for non-SCSI transport protocols and bug fixes)
*/
static void
get_restrictions(scsi_sim_cookie cookie, uchar targetID, bool *isATAPI,
	bool *noAutoSense, uint32 *maxBlocks)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	sim->GetRestrictions(targetID, isATAPI, noAutoSense, maxBlocks);
}


static status_t
ioctl(scsi_sim_cookie cookie, uint8 targetID, uint32 op, void *buffer,
	size_t length)
{
	CALLED();
	VirtioSCSIController *sim = (VirtioSCSIController *)cookie;
	return sim->Control(targetID, op, buffer, length);
}


//	#pragma mark -


static scsi_sim_interface sVirtioSCSISimOps = {
	set_scsi_bus,
	scsi_io,
	abort_io,
	reset_device,
	terminate_io,
	path_inquiry,
	scan_bus,
	reset_bus,
	get_restrictions,
	ioctl
};


static status_t
sim_init_bus(dk_node *node, void **_cookie)
{
	CALLED();

	VirtioSCSIController *controller =  new(std::nothrow)
		VirtioSCSIController(node);
	if (controller == NULL)
		return B_NO_MEMORY;
	status_t status = controller->InitCheck();
	if (status < B_OK) {
		delete controller;
		return status;
	}

	// Publish the SCSI SIM interface on this node so gSCSIBusDriver
	// can retrieve it via get_interface walk-up.
	status = gDeviceKeeper->publish_interface(node, SCSI_SIM_INTERFACE_NAME,
		&sVirtioSCSISimOps);
	if (status != B_OK) {
		delete controller;
		return status;
	}

	*_cookie = controller;
	return B_OK;
}


static void
sim_uninit_bus(void *cookie)
{
	CALLED();
	VirtioSCSIController *controller = (VirtioSCSIController*)cookie;

	delete controller;
}


//	#pragma mark -


static const dk_match_rule kVirtioSCSIMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "virtio" } },
	{}
};

static const dk_match_dict kVirtioSCSIMatch = {
	kVirtioSCSIMatchRules,
	0
};


static float
virtio_scsi_supports_device(dk_node *parent)
{
	uint16 deviceType;

	// bus already filtered by match rules
	if (gDeviceKeeper->get_property_uint16(parent, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK || deviceType != VIRTIO_DEVICE_ID_SCSI)
		return 0.0;

	TRACE("Virtio SCSI device found!\n");

	return 0.6f;
}


static status_t
virtio_scsi_init_driver(dk_node *node, void **_cookie)
{
	CALLED();
	*_cookie = node;

	int32 id = gDeviceKeeper->create_id(VIRTIO_SCSI_ID_GENERATOR);
	if (id < 0)
		return id;

	dk_property attrs[] = {
		// gSCSIBusDriver matches bus="scsi-host" and attaches as child.
		DK_PROP_STRING(KOSM_DEVICE_BUS, "scsi-host"),
		DK_PROP_STRING(KOSM_LABEL, VIRTIO_SCSI_CONTROLLER_PRETTY_NAME),
		DK_PROP_STRING(SCSI_DESCRIPTION_CONTROLLER_NAME,
			VIRTIO_SCSI_DEVICE_MODULE_NAME),
		DK_PROP_UINT32(KOSM_DMA_MAX_TRANSFER_BLOCKS, 255),
		DK_PROP_UINT32(VIRTIO_SCSI_ID_ITEM, (uint32)id),
		DK_PROP_END
	};

	status_t status = gDeviceKeeper->register_node(node,
		VIRTIO_SCSI_SIM_MODULE_NAME, attrs, NULL, NULL);
	if (status < B_OK)
		gDeviceKeeper->free_id(VIRTIO_SCSI_ID_GENERATOR, id);

	return status;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


static dk_driver_info sVirtioSCSISimInterface = {
	.info		= { VIRTIO_SCSI_SIM_MODULE_NAME, 0, std_ops },
	.attach		= sim_init_bus,
	.detach		= sim_uninit_bus,
	.node_flags	= KOSM_FIND_MULTIPLE_CHILDREN,
};


static dk_driver_info sVirtioSCSIDevice = {
	{
		VIRTIO_SCSI_DEVICE_MODULE_NAME,
		0,
		std_ops
	},

	.match = &kVirtioSCSIMatch,
	.probe = virtio_scsi_supports_device,
	.attach = virtio_scsi_init_driver,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ SCSI_FOR_SIM_MODULE_NAME, (module_info **)&gSCSI },
	{}
};


module_info *modules[] = {
	(module_info *)&sVirtioSCSIDevice,
	(module_info *)&sVirtioSCSISimInterface,
	NULL
};
