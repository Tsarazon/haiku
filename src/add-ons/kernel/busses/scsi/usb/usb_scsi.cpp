/*
 * Copyright 2003-2025, Haiku, Inc. All rights reserved.
 * Originally by Siarzhuk Zharski <imker@gmx.li>
 * Rewritten 2025 for DeviceKeeper / modern SCSI bus manager.
 * Distributed under the terms of the MIT License.
 *
 * USB Mass Storage SCSI SIM — DeviceKeeper module.
 *
 * Bridges USB Mass Storage devices into the SCSI subsystem via
 * scsi_sim_interface, so the SCSI bus manager can create
 * scsi_disk/scsi_cd device nodes on USB storage devices.
 */

#include "usb_scsi.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <KernelExport.h>


// ---- globals ----

usb_module_info*		gUSB;
scsi_for_sim_interface*	gSCSI;
dk_keeper_info*			gDeviceKeeper;


// ---- scsi_sim_interface ----


static void
sim_set_scsi_bus(scsi_sim_cookie cookie, scsi_bus bus)
{
	usb_mass_device* dev = (usb_mass_device*)cookie;
	dev->scsi_bus_cookie = bus;
	TRACE("set_scsi_bus(%p)\n", bus);
}


static void
sim_scsi_io(scsi_sim_cookie cookie, scsi_ccb* ccb)
{
	usb_mass_device* dev = (usb_mass_device*)cookie;

	if (ccb->target_id > dev->max_lun) {
		ccb->subsys_status = SCSI_DEV_NOT_THERE;
		gSCSI->finished(ccb, 1);
		return;
	}

	status_t status = acquire_sem(dev->lock_sem);
	if (status != B_OK) {
		ccb->subsys_status = SCSI_REQ_ABORTED;
		gSCSI->finished(ccb, 1);
		return;
	}

	// Save original timeout for possible restore in post_handle
	bigtime_t savedTimeout = dev->trans_timeout;

	do {
		// Pre-handle: transforms, quirks, fake responses
		if (!usb_scsi_pre_handle(dev, ccb))
			break;  // CCB already complete

		// Execute USB transfer
		status = usb_scsi_transfer(dev, ccb);
		if (status != B_OK) {
			ccb->subsys_status = SCSI_REQ_CMP_ERR;
			break;
		}

		// Post-handle: MODE_SENSE conversion, retry logic
	} while (usb_scsi_post_handle(dev, ccb));

	dev->trans_timeout = savedTimeout;
	release_sem(dev->lock_sem);
	gSCSI->finished(ccb, 1);
}


static uchar
sim_abort(scsi_sim_cookie cookie, scsi_ccb* ccb)
{
	return SCSI_REQ_CMP;
}


static uchar
sim_reset_device(scsi_sim_cookie cookie, uchar targetId, uchar targetLun)
{
	usb_mass_device* dev = (usb_mass_device*)cookie;
	usb_scsi_reset(dev);
	return SCSI_REQ_CMP;
}


static uchar
sim_term_io(scsi_sim_cookie cookie, scsi_ccb* ccb)
{
	return SCSI_REQ_CMP;
}


static uchar
sim_path_inquiry(scsi_sim_cookie cookie, scsi_path_inquiry* inquiry)
{
	memset(inquiry, 0, sizeof(*inquiry));
	inquiry->version_num = 1;
	inquiry->hba_inquiry = 0;
	inquiry->hba_misc = 0;
	inquiry->initiator_id = MAX_DEVICES_COUNT;
	inquiry->hba_queue_size = 1;
	strlcpy(inquiry->sim_vid, "Haiku", SCSI_SIM_ID);
	strlcpy(inquiry->hba_vid, "USB", SCSI_HBA_ID);
	strlcpy(inquiry->controller_family, "USB Mass Storage", SCSI_FAM_ID);
	strlcpy(inquiry->controller_type, "Bulk-Only/CBI", SCSI_TYPE_ID);
	return SCSI_REQ_CMP;
}


static uchar
sim_scan_bus(scsi_sim_cookie cookie)
{
	return SCSI_REQ_CMP;
}


static uchar
sim_reset_bus(scsi_sim_cookie cookie)
{
	usb_mass_device* dev = (usb_mass_device*)cookie;
	usb_scsi_reset(dev);
	return SCSI_REQ_CMP;
}


static void
sim_get_restrictions(scsi_sim_cookie cookie, uchar targetId,
	bool* isAtapi, bool* noAutosense, uint32* maxBlocks)
{
	*isAtapi = false;
	// USB mass storage has no autosense — the SCSI bus manager
	// will issue REQUEST_SENSE automatically on CHECK_CONDITION.
	*noAutosense = true;
	*maxBlocks = 65535;  // USB max 64K blocks per transfer
}


static status_t
sim_ioctl(scsi_sim_cookie cookie, uint8 targetId, uint32 op,
	void* buffer, size_t length)
{
	return B_DEV_INVALID_IOCTL;
}


static scsi_sim_interface sSimInterface = {
	sim_set_scsi_bus,
	sim_scsi_io,
	sim_abort,
	sim_reset_device,
	sim_term_io,
	sim_path_inquiry,
	sim_scan_bus,
	sim_reset_bus,
	sim_get_restrictions,
	sim_ioctl,
};


// ---- USB endpoint setup ----


static status_t
setup_endpoints(usb_mass_device* dev)
{
	const usb_configuration_info* config = gUSB->get_configuration(
		dev->usb_dev);
	if (config == NULL)
		return B_ERROR;

	for (size_t i = 0; i < config->interface_count; i++) {
		usb_interface_info* iface = config->interface[i].active;
		if (iface == NULL || iface->descr == NULL)
			continue;
		if (iface->descr->interface_class != USB_DEV_CLASS_MASS)
			continue;

		dev->interface = i;
		dev->pipe_in = 0;
		dev->pipe_out = 0;
		dev->pipe_intr = 0;

		// Determine protocol from interface descriptor
		uint8 proto = iface->descr->interface_protocol;
		uint8 subclass = iface->descr->interface_subclass;

		if (proto == USB_DEV_PROTOCOL_BULK)
			dev->properties |= PROTO_BULK_ONLY;
		else if (proto == USB_DEV_PROTOCOL_CBI)
			dev->properties |= PROTO_CBI;
		else if (proto == USB_DEV_PROTOCOL_CB)
			dev->properties |= PROTO_CB;

		switch (subclass) {
			case USB_DEV_SUBCLASS_SCSI:		dev->properties |= CMDSET_SCSI; break;
			case USB_DEV_SUBCLASS_UFI:		dev->properties |= CMDSET_UFI; break;
			case USB_DEV_SUBCLASS_SFF8020I:
			case USB_DEV_SUBCLASS_SFF8070I:	dev->properties |= CMDSET_ATAPI; break;
			case USB_DEV_SUBCLASS_RBC:		dev->properties |= CMDSET_RBC; break;
			case USB_DEV_SUBCLASS_QIC157:	dev->properties |= CMDSET_QIC157; break;
		}

		for (size_t e = 0; e < iface->endpoint_count; e++) {
			usb_endpoint_descriptor* ed = iface->endpoint[e].descr;
			uint8 attr = ed->attributes & USB_EP_ATTR_MASK;
			if (attr == USB_EP_ATTR_BULK) {
				if (ed->endpoint_address & USB_EP_ADDR_DIR_IN)
					dev->pipe_in = iface->endpoint[e].handle;
				else
					dev->pipe_out = iface->endpoint[e].handle;
			} else if (attr == USB_EP_ATTR_INTERRUPT) {
				dev->pipe_intr = iface->endpoint[e].handle;
			}
		}

		if (dev->pipe_in == 0 || dev->pipe_out == 0) {
			ERROR("missing bulk endpoints\n");
			return B_ERROR;
		}

		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


// ---- DeviceKeeper driver hooks ----


static float
usb_scsi_probe(dk_node* node)
{
	uint8 devClass;
	if (gDeviceKeeper->get_property_uint8(node,
			KOSM_USB_INTERFACE_CLASS, &devClass, false) != B_OK
		|| devClass != USB_DEV_CLASS_MASS) {
		return -1.0f;
	}

	TRACE("USB Mass Storage device found\n");
	return 0.6f;
}


static status_t
usb_scsi_attach(dk_node* node, void** _cookie)
{
	TRACE_ALWAYS("attach\n");

	usb_mass_device* dev = (usb_mass_device*)calloc(1, sizeof(*dev));
	if (dev == NULL)
		return B_NO_MEMORY;

	dev->node = node;
	dev->trans_timeout = 15000000LL;  // 15s default

	// Get USB device handle
	uint32 usbDevId;
	if (gDeviceKeeper->get_property_uint32(node, "usb/id",
			&usbDevId, true) != B_OK) {
		ERROR("no usb/id property\n");
		free(dev);
		return B_ERROR;
	}
	dev->usb_dev = (usb_device)(addr_t)usbDevId;

	// Set up endpoints + detect protocol/command set
	status_t status = setup_endpoints(dev);
	if (status != B_OK) {
		ERROR("endpoint setup failed: %s\n", strerror(status));
		free(dev);
		return status;
	}

	// Apply device quirks
	uint16 vendorId, productId;
	if (gDeviceKeeper->get_property_uint16(node, KOSM_USB_VENDOR_ID,
			&vendorId, true) == B_OK
		&& gDeviceKeeper->get_property_uint16(node, KOSM_USB_PRODUCT_ID,
			&productId, true) == B_OK) {
		dev->properties |= usb_scsi_lookup_quirks(vendorId, productId);
		TRACE("vendor=%04x product=%04x props=0x%08x\n",
			vendorId, productId, (unsigned)dev->properties);
	}

	// Semaphores
	dev->lock_sem = create_sem(1, MODULE_NAME " lock");
	if (dev->lock_sem < 0) {
		free(dev);
		return dev->lock_sem;
	}
	dev->trans_sem = create_sem(0, MODULE_NAME " transfer");
	if (dev->trans_sem < 0) {
		delete_sem(dev->lock_sem);
		free(dev);
		return dev->trans_sem;
	}

	// Get max LUN count
	usb_scsi_get_max_lun(dev);

	TRACE("attached: proto=%d cmdset=%d max_lun=%d\n",
		PROTO(dev->properties), CMDSET(dev->properties), dev->max_lun);

	// Publish SIM interface
	status = gDeviceKeeper->publish_interface(node,
		SCSI_SIM_INTERFACE_NAME, &sSimInterface);
	if (status != B_OK) {
		ERROR("failed to publish SIM interface: %s\n", strerror(status));
		delete_sem(dev->trans_sem);
		delete_sem(dev->lock_sem);
		free(dev);
		return status;
	}

	// Register child for SCSI bus manager auto-attach
	dk_property props[] = {
		DK_PROP_STRING(KOSM_DEVICE_BUS, "scsi-host"),
		DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
		DK_PROP_STRING(SCSI_DESCRIPTION_CONTROLLER_NAME,
			"USB Mass Storage"),
		DK_PROP_END
	};
	status = gDeviceKeeper->register_node(node,
		SCSI_BUS_MODULE_NAME, props, NULL, NULL);
	if (status != B_OK) {
		ERROR("failed to register SCSI bus child: %s\n", strerror(status));
		delete_sem(dev->trans_sem);
		delete_sem(dev->lock_sem);
		free(dev);
		return status;
	}

	*_cookie = dev;
	return B_OK;
}


static void
usb_scsi_detach(void* cookie)
{
	TRACE_ALWAYS("detach\n");

	usb_mass_device* dev = (usb_mass_device*)cookie;
	if (dev == NULL)
		return;

	if (dev->pipe_in != 0)
		gUSB->cancel_queued_transfers(dev->pipe_in);
	if (dev->pipe_out != 0)
		gUSB->cancel_queued_transfers(dev->pipe_out);
	if (dev->pipe_intr != 0)
		gUSB->cancel_queued_transfers(dev->pipe_intr);

	delete_sem(dev->trans_sem);
	delete_sem(dev->lock_sem);
	free(dev);
}


// ---- module glue ----


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			usb_scsi_load_quirks();
			return B_OK;
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


static const dk_match_rule sMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sMatchDict = {
	sMatchRules,
	0
};

static dk_driver_info sDriverInfo = {
	.info       = { "busses/scsi/usb/dk_driver_v1", 0, &std_ops },
	.match      = &sMatchDict,
	.probe      = usb_scsi_probe,
	.attach     = usb_scsi_attach,
	.detach     = usb_scsi_detach,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info**)&gUSB },
	{ SCSI_FOR_SIM_MODULE_NAME, (module_info**)&gSCSI },
	{}
};

module_info* modules[] = {
	(module_info*)&sDriverInfo,
	NULL
};
