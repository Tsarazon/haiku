/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
	Part of Open SCSI bus manager

	Bus node layer.

	Whenever a controller driver publishes a new controller, a new SCSI bus
	for public and internal use is registered in turn. After that, this
	bus is told to rescan for devices. For each device, there is a
	device registered for peripheral drivers. (see devices.c)
*/

#include "scsi_internal.h"

#include <stdio.h>
#include <string.h>
#include <malloc.h>


// bus service should hurry up a bit - good controllers don't take much time
// but are very happy to be busy; don't make it realtime though as we
// don't really need that but would risk to steel processing power of
// realtime-demanding threads
#define BUS_SERVICE_PRIORITY B_URGENT_DISPLAY_PRIORITY


/**	implementation of service thread:
 *	it handles DPC and pending requests
 */

static void
scsi_do_service(scsi_bus_info *bus)
{
	while (true) {
		SHOW_FLOW0( 3, "" );

		// handle DPCs first as they are more urgent
		if (scsi_check_exec_dpc(bus))
			continue;

		if (scsi_check_exec_service(bus))
			continue;

		break;
	}
}


/** main loop of service thread */

static int32
scsi_service_threadproc(void *arg)
{
	scsi_bus_info *bus = (scsi_bus_info *)arg;
	int32 processed_notifications = 0;

	SHOW_FLOW(3, "bus = %p", bus);

	while (true) {
		// we handle multiple requests in scsi_do_service at once;
		// to save time, we will acquire all notifications that are sent
		// up to now at once.
		// (Sadly, there is no "set semaphore to zero" function, so this
		//  is a poor-man emulation)
		acquire_sem_etc(bus->start_service, processed_notifications + 1, 0, 0);

		SHOW_FLOW0( 3, "1" );

		if (bus->shutting_down)
			break;

		// get number of notifications _before_ servicing to make sure no new
		// notifications are sent after do_service()
		get_sem_count(bus->start_service, &processed_notifications);

		scsi_do_service(bus);
	}

	return 0;
}


static scsi_bus_info *
scsi_create_bus(dk_node *node, uint8 path_id)
{
	scsi_bus_info *bus;
	int res;

	SHOW_FLOW0(3, "");

	bus = (scsi_bus_info *)malloc(sizeof(*bus));
	if (bus == NULL)
		return NULL;

	memset(bus, 0, sizeof(*bus));

	bus->path_id = path_id;

	if (pnp->get_property_uint32(node, SCSI_DEVICE_MAX_TARGET_COUNT,
			&bus->max_target_count, true) != B_OK)
		bus->max_target_count = MAX_TARGET_ID + 1;
	if (pnp->get_property_uint32(node, SCSI_DEVICE_MAX_LUN_COUNT,
			&bus->max_lun_count, true) != B_OK)
		bus->max_lun_count = MAX_LUN_ID + 1;

	// our scsi_ccb only has a uchar for target_id
	if (bus->max_target_count > 256)
		bus->max_target_count = 256;
	// our scsi_ccb only has a uchar for target_lun
	if (bus->max_lun_count > 256)
		bus->max_lun_count = 256;

	bus->node = node;
	bus->lock_count = bus->blocked[0] = bus->blocked[1] = 0;
	bus->sim_overflow = 0;
	bus->shutting_down = false;

	bus->waiting_devices = NULL;
	//bus->resubmitted_req = NULL;

	bus->dpc_list = NULL;

	if ((bus->scan_lun_lock = create_sem(1, "scsi_scan_lun_lock")) < 0) {
		res = bus->scan_lun_lock;
		goto err6;
	}

	bus->start_service = create_sem(0, "scsi_start_service");
	if (bus->start_service < 0) {
		res = bus->start_service;
		goto err4;
	}

	mutex_init(&bus->mutex, "scsi_bus_mutex");
	spinlock_irq_init(&bus->dpc_lock);

	bus->service_thread = spawn_kernel_thread(scsi_service_threadproc,
		"scsi_bus_service", BUS_SERVICE_PRIORITY, bus);

	if (bus->service_thread < 0) {
		res = bus->service_thread;
		goto err1;
	}

	resume_thread(bus->service_thread);

	return bus;

err1:
	mutex_destroy(&bus->mutex);
	delete_sem(bus->start_service);
err4:
	delete_sem(bus->scan_lun_lock);
err6:
	free(bus);
	return NULL;
}


static status_t
scsi_destroy_bus(scsi_bus_info *bus)
{
	// noone is using this bus now, time to clean it up
	bus->shutting_down = true;
	release_sem(bus->start_service);

	status_t retcode;
	wait_for_thread(bus->service_thread, &retcode);

	delete_sem(bus->start_service);
	mutex_destroy(&bus->mutex);
	delete_sem(bus->scan_lun_lock);

	return B_OK;
}


static status_t
scsi_bus_attach(dk_node *node, void **cookie)
{
	scsi_bus_info *bus;
	status_t res;

	SHOW_FLOW0( 3, "" );

	// Allocate SCSI path id for this bus.
	int32 pathID = pnp->create_id(SCSI_PATHID_GENERATOR);
	if (pathID < 0) {
		dprintf("scsi: out of path IDs\n");
		return pathID;
	}
	if (pathID > MAX_PATH_ID) {
		dprintf("scsi: path ID %" B_PRId32 " exceeds max %d\n",
			pathID, MAX_PATH_ID);
		pnp->free_id(SCSI_PATHID_GENERATOR, pathID);
		return B_ERROR;
	}

	bus = scsi_create_bus(node, (uint8)pathID);
	if (bus == NULL) {
		pnp->free_id(SCSI_PATHID_GENERATOR, pathID);
		return B_NO_MEMORY;
	}

	// extract controller/protocol restrictions from node (recursive
	// so they can be inherited from the SIM node set by the controller)
	if (pnp->get_property_uint32(node, KOSM_DMA_ALIGNMENT,
			&bus->dma_params.alignment, true) != B_OK)
		bus->dma_params.alignment = 0;
	if (pnp->get_property_uint32(node, KOSM_DMA_MAX_TRANSFER_BLOCKS,
			&bus->dma_params.max_blocks, true) != B_OK)
		bus->dma_params.max_blocks = UINT32_MAX;
	if (pnp->get_property_uint32(node, KOSM_DMA_BOUNDARY,
			&bus->dma_params.dma_boundary, true) != B_OK)
		bus->dma_params.dma_boundary = UINT32_MAX;
	if (pnp->get_property_uint32(node, KOSM_DMA_MAX_SEGMENT_BLOCKS,
			&bus->dma_params.max_sg_block_size, true) != B_OK)
		bus->dma_params.max_sg_block_size = UINT32_MAX;
	if (pnp->get_property_uint32(node, KOSM_DMA_MAX_SEGMENT_COUNT,
			&bus->dma_params.max_sg_blocks, true) != B_OK)
		bus->dma_params.max_sg_blocks = UINT32_MAX;
	if (pnp->get_property_uint64(node, KOSM_DMA_HIGH_ADDRESS,
			&bus->dma_params.high_address, true) != B_OK)
		bus->dma_params.high_address = UINT64_MAX;

	// do some sanity check:
	bus->dma_params.max_sg_block_size &= ~bus->dma_params.alignment;

	if (bus->dma_params.alignment > B_PAGE_SIZE) {
		SHOW_ERROR(0, "Alignment (0x%" B_PRIx32 ") must be less then "
			"B_PAGE_SIZE", bus->dma_params.alignment);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.max_sg_block_size < 1) {
		SHOW_ERROR(0, "Max s/g block size (0x%" B_PRIx32 ") is too small",
			bus->dma_params.max_sg_block_size);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.dma_boundary < B_PAGE_SIZE - 1) {
		SHOW_ERROR(0, "DMA boundary (0x%" B_PRIx32 ") must be at least "
			"B_PAGE_SIZE", bus->dma_params.dma_boundary);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.max_blocks < 1 || bus->dma_params.max_sg_blocks < 1) {
		SHOW_ERROR(0, "Max blocks (%" B_PRIu32 ") and max s/g blocks (%"
			B_PRIu32 ") must be at least 1", bus->dma_params.max_blocks,
			bus->dma_params.max_sg_blocks);
		res = B_ERROR;
		goto err;
	}

	// Walk up to the SIM node (our parent, since we were attached as
	// child via ProbeAndAttachAll) to retrieve scsi_sim_interface.
	res = pnp->get_interface(node, SCSI_SIM_INTERFACE_NAME,
		KOSM_INTERFACE_ANCESTORS,
		(const void **)&bus->interface, (void **)&bus->sim_cookie);
	if (res != B_OK) {
		SHOW_ERROR(0, "No SCSI SIM interface on parent: %s",
			strerror(res));
		goto err;
	}
	if (bus->interface == NULL || bus->sim_cookie == NULL) {
		res = B_ERROR;
		goto err;
	}

	bus->interface->set_scsi_bus(bus->sim_cookie, bus);

	// Publish the SCSI bus interface so peripheral drivers on child
	// device nodes can retrieve it via get_interface walk-up.
	res = pnp->publish_interface(node, SCSI_BUS_INTERFACE_NAME,
		&scsi_bus_ops);
	if (res != B_OK)
		goto err;

	// Publish path_id as a property so peripheral drivers can find it
	// via recursive property lookup (compose_device_name needs it).
	{
		dk_property pathProp = DK_PROP_UINT8(SCSI_BUS_PATH_ID_ITEM,
			bus->path_id);
		pnp->set_property(node, &pathProp);
	}

	// cache inquiry data
	scsi_inquiry_path(bus, &bus->inquiry_data);

	// get max. number of commands on bus
	bus->left_slots = bus->inquiry_data.hba_queue_size;
	SHOW_FLOW( 3, "Bus has %d slots", bus->left_slots );

	*cookie = bus;

	// Publish devfs entry bus/scsi/<path_id>/bus_raw for userspace
	// raw bus access (ioctl reset/path_inquiry).
	{
		char rawPath[64];
		snprintf(rawPath, sizeof(rawPath), "bus/scsi/%d/bus_raw",
			(int)bus->path_id);
		pnp->publish_device(node, rawPath, &gSCSIBusRawOps);
	}

	// Scan the SCSI bus; found devices register as child nodes
	// of this node and get auto-probed by peripheral drivers
	// (scsi_disk, scsi_cd) via the KOSM_FIND_MULTIPLE_CHILDREN flag.
	scsi_scan_bus(bus);

	return B_OK;

err:
	pnp->free_id(SCSI_PATHID_GENERATOR, pathID);
	scsi_destroy_bus(bus);
	return res;
}


static void
scsi_bus_detach(void *cookie)
{
	scsi_bus_info *bus = (scsi_bus_info *)cookie;
	pnp->free_id(SCSI_PATHID_GENERATOR, bus->path_id);
	scsi_destroy_bus(bus);
}


static status_t
scsi_bus_rescan(void *cookie)
{
	scsi_bus_info *bus = (scsi_bus_info *)cookie;
	return scsi_scan_bus(bus);
}


uchar
scsi_inquiry_path(scsi_bus bus, scsi_path_inquiry *inquiry_data)
{
	SHOW_FLOW(4, "path_id=%d", bus->path_id);
	return bus->interface->path_inquiry(bus->sim_cookie, inquiry_data);
}


static uchar
scsi_reset_bus(scsi_bus_info *bus)
{
	return bus->interface->reset_bus(bus->sim_cookie);
}


static status_t
scsi_bus_module_init(void)
{
	SHOW_FLOW0(4, "");

	status_t status = init_ccb_alloc();
	if (status != B_OK)
		return status;
	return init_temp_sg();
}


static status_t
scsi_bus_module_uninit(void)
{
	SHOW_INFO0(4, "");

	uninit_ccb_alloc();
	uninit_temp_sg();
	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return scsi_bus_module_init();
		case B_MODULE_UNINIT:
			return scsi_bus_module_uninit();

		default:
			return B_ERROR;
	}
}


// Plain scsi_bus_interface published via publish_interface on each
// scsi bus node. Peripheral drivers retrieve it via get_interface.
scsi_bus_interface scsi_bus_ops = {
	scsi_inquiry_path,
	scsi_reset_bus,
};


// Match rule: attach on any node whose KOSM_DEVICE_BUS = "scsi-host".
// SCSI SIM drivers (ahci_sim, virtio_scsi_sim, usb_scsi) set this
// property on their node and publish scsi_sim_interface; then the
// framework creates a child scsi bus node here and attaches us to it.
static const dk_match_rule sSCSIBusMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "scsi-host"),
	DK_MATCH_END
};

static const dk_match_dict sSCSIBusMatchDict = {
	sSCSIBusMatchRules,
	0
};


struct dk_driver_info gSCSIBusDriver = {
	.info		= { SCSI_BUS_MODULE_NAME, 0, std_ops },
	.match		= &sSCSIBusMatchDict,
	.attach		= scsi_bus_attach,
	.detach		= scsi_bus_detach,
	.rescan_children = scsi_bus_rescan,
	.node_flags	= KOSM_FIND_MULTIPLE_CHILDREN,
};
