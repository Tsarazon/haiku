/*
 * Copyright 2007-2009, Marcus Overhagen. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ahci_controller.h"

#include <KernelExport.h>
#include <string.h>
#include <new>

#define TRACE(a...) dprintf("ahci: " a)
//#define FLOW(a...)	dprintf("ahci: " a)
#define FLOW(a...)


//	#pragma mark - SIM module interface


static void
ahci_set_scsi_bus(scsi_sim_cookie cookie, scsi_bus bus)
{
}


//! execute request
static void
ahci_scsi_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	dprintf("ahci: scsi_io cookie=%p target=%u lun=%u opcode=0x%02x\n",
		cookie, request->target_id, request->target_lun,
		request->cdb[0]);
	static_cast<AHCIController *>(cookie)->ExecuteRequest(request);
}


//! abort request
static uchar
ahci_abort_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	TRACE("ahci_abort_io, cookie %p\n", cookie);
	return static_cast<AHCIController *>(cookie)->AbortRequest(request);
}


static uchar
ahci_reset_device(scsi_sim_cookie cookie, uchar targetID, uchar targetLUN)
{
	TRACE("ahci_reset_device, cookie %p\n", cookie);
	return static_cast<AHCIController *>(cookie)->ResetDevice(targetID, targetLUN);
}


//! terminate request
static uchar
ahci_terminate_io(scsi_sim_cookie cookie, scsi_ccb *request)
{
	TRACE("ahci_terminate_io, cookie %p\n", cookie);
	return static_cast<AHCIController *>(cookie)->TerminateRequest(request);
}


//! get information about bus
static uchar
ahci_path_inquiry(scsi_sim_cookie cookie, scsi_path_inquiry *info)
{
	TRACE("ahci_path_inquiry, cookie %p\n", cookie);

	memset(info, 0, sizeof(*info));
	info->version_num = 1;
	// supports tagged requests and soft reset
	info->hba_inquiry = 0; // SCSI_PI_TAG_ABLE | SCSI_PI_SOFT_RST;
	// controller is 32, devices are 0 to 31
	info->initiator_id = 32;
	// adapter command queue size
	info->hba_queue_size = 1;

	return SCSI_REQ_CMP;
}


//! this is called immediately before the SCSI bus manager scans the bus
static uchar
ahci_scan_bus(scsi_sim_cookie cookie)
{
	TRACE("ahci_scan_bus, cookie %p\n", cookie);

	return SCSI_REQ_CMP;
}


static uchar
ahci_reset_bus(scsi_sim_cookie cookie)
{
	TRACE("ahci_reset_bus, cookie %p\n", cookie);

	return SCSI_REQ_CMP;
}


/*!	Get restrictions of one device
	(used for non-SCSI transport protocols and bug fixes)
*/
static void
ahci_get_restrictions(scsi_sim_cookie cookie, uchar targetID, bool *isATAPI,
	bool *noAutoSense, uint32 *maxBlocks)
{
	TRACE("ahci_get_restrictions, cookie %p\n", cookie);

	static_cast<AHCIController *>(cookie)->GetRestrictions(targetID, isATAPI, noAutoSense, maxBlocks);
}


static status_t
ahci_ioctl(scsi_sim_cookie cookie, uint8 targetID, uint32 op, void *buffer,
	size_t length)
{
	TRACE("ahci_ioctl, cookie %p\n", cookie);
	return B_DEV_INVALID_IOCTL;
}


//	#pragma mark -


extern scsi_sim_interface gAHCISimOps;	// defined below


static status_t
ahci_sim_init_bus(dk_node *node, void **_cookie)
{
	TRACE("ahci_sim_init_bus\n");

	// get PCI device ops via walk-up
	pci_device_ops *pci;
	pci_device *pciDevice;
	status_t status = gDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&pci, (void **)&pciDevice);
	if (status != B_OK)
		return status;

	TRACE("ahci_sim_init_bus: pciDevice %p\n", pciDevice);

	AHCIController *controller =  new(std::nothrow) AHCIController(node, pci,
		pciDevice);
	if (!controller)
		return B_NO_MEMORY;
	status = controller->Init();
	if (status < B_OK) {
		delete controller;
		return status;
	}

	// Publish the SCSI SIM interface on this node. The SCSI bus manager
	// walks up from its scsi_bus_node (created as our child by the
	// framework) to find this interface via get_interface.
	status = gDeviceKeeper->publish_interface(node, SCSI_SIM_INTERFACE_NAME,
		&gAHCISimOps);
	if (status != B_OK) {
		controller->Uninit();
		delete controller;
		return status;
	}

	*_cookie = controller;
	TRACE("cookie = %p\n", *_cookie);
	return B_OK;
}


static void
ahci_sim_uninit_bus(void *cookie)
{
	TRACE("ahci_sim_uninit_bus, cookie %p\n", cookie);
	AHCIController *controller = static_cast<AHCIController *>(cookie);

	controller->Uninit();
	delete controller;
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


scsi_sim_interface gAHCISimOps = {
	ahci_set_scsi_bus,
	ahci_scsi_io,
	ahci_abort_io,
	ahci_reset_device,
	ahci_terminate_io,
	ahci_path_inquiry,
	ahci_scan_bus,
	ahci_reset_bus,
	ahci_get_restrictions,
	ahci_ioctl
};

// Auto-attached by module name when ahci.c register_sim() calls
// register_node(AHCI_SIM_MODULE_NAME). node_flags=MULTI_CHILDREN
// triggers ProbeAndAttachAll on our node, letting gSCSIBusDriver
// (match_dict bus=scsi-host) attach as a child.
dk_driver_info gAHCISimInterface = {
	.info		= { AHCI_SIM_MODULE_NAME, 0, std_ops },
	.attach		= ahci_sim_init_bus,
	.detach		= ahci_sim_uninit_bus,
	.node_flags	= KOSM_FIND_MULTIPLE_CHILDREN,
};
