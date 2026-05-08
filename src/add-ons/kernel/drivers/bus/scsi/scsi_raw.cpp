/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

/*
	Part of Open SCSI raw driver.

	We don't support particular file handles, instead we use
	file handle = device handle.
*/


#include "scsi_raw.h"
#include <string.h>
#include <malloc.h>
#include <stdio.h>


dk_keeper_info *pnp;


//	#pragma mark - dk_device_ops


static status_t
raw_open(void *deviceCookie, const char *path, int openMode, void **_cookie)
{
	*_cookie = deviceCookie;
	return B_OK;
}


static status_t
raw_close(void *cookie)
{
	return B_OK;
}


static status_t
raw_free(void *cookie)
{
	return B_OK;
}


static status_t
raw_read(void *cookie, off_t position, void *data, size_t *numBytes)
{
	return B_ERROR;
}


static status_t
raw_write(void *cookie, off_t position, const void *data, size_t *numBytes)
{
	return B_ERROR;
}


/* TODO: sync with scsi_periph module, this has been updated there to use
	user_memcpy */
static status_t
raw_command(raw_device_info *device, raw_device_command *cmd)
{
	scsi_ccb *request;

	SHOW_FLOW0(3, "");

	request = device->scsi->alloc_ccb(device->scsi_dev);
	if (request == NULL)
		return B_NO_MEMORY;

	request->flags = 0;

	if (cmd->flags & B_RAW_DEVICE_DATA_IN)
		request->flags |= SCSI_DIR_IN;
	else if (cmd->data_length)
		request->flags |= SCSI_DIR_OUT;
	else
		request->flags |= SCSI_DIR_NONE;

	request->data = (uchar *)cmd->data;
	request->sg_list = NULL;
	request->data_length = cmd->data_length;
	request->sort = -1;
	request->timeout = cmd->timeout;

	memcpy(request->cdb, cmd->command, SCSI_MAX_CDB_SIZE);
	request->cdb_length = cmd->command_length;

	device->scsi->sync_io(request);

	// TBD: should we call standard error handler here, or may the
	// actions done there (like starting the unit) confuse the application?

	cmd->cam_status = request->subsys_status;
	cmd->scsi_status = request->device_status;

	if ((request->subsys_status & SCSI_AUTOSNS_VALID) != 0 && cmd->sense_data) {
		memcpy(cmd->sense_data, request->sense,
			min_c((int32)cmd->sense_data_length,
				(int32)(SCSI_MAX_SENSE_SIZE - request->sense_resid)));
	}

	if ((cmd->flags & B_RAW_DEVICE_REPORT_RESIDUAL) != 0) {
		// this is a bit strange, see Be's sample code where I pinched this from;
		// normally, residual means "number of unused bytes left"
		// but here, we have to return "number of used bytes", which is the opposite
		cmd->data_length = cmd->data_length - request->data_resid;
		cmd->sense_data_length = SCSI_MAX_SENSE_SIZE - request->sense_resid;
	}

	device->scsi->free_ccb(request);
	return B_OK;
}


static status_t
raw_ioctl(void *cookie, uint32 op, void *buffer, size_t length)
{
	raw_device_info *device = (raw_device_info *)cookie;
	status_t res;

	switch (op) {
		case B_RAW_DEVICE_COMMAND:
			res = raw_command(device, (raw_device_command *)buffer);
			break;

		default:
			res = B_DEV_INVALID_IOCTL;
	}

	SHOW_FLOW(4, "%x: %s", (unsigned)op, strerror(res));

	return res;
}


static dk_device_ops sRawDeviceOps = {
	raw_open,
	raw_close,
	raw_free,
	raw_read,
	raw_write,
	NULL,	// io
	raw_ioctl,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - dk_driver_info


static status_t
raw_attach(dk_node *node, void **_cookie)
{
	SHOW_FLOW0(3, "");

	raw_device_info *device = (raw_device_info *)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	device->node = node;

	// Get SCSI device interface from parent
	status_t status = pnp->get_interface(node,
		SCSI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&device->scsi, (void **)&device->scsi_dev);
	if (status != B_OK) {
		free(device);
		return status;
	}

	// Compose devfs path from SCSI addressing properties
	uint8 path_id, target_id, target_lun;
	if (pnp->get_property_uint8(node, SCSI_BUS_PATH_ID_ITEM,
			&path_id, true) != B_OK
		|| pnp->get_property_uint8(node, SCSI_DEVICE_TARGET_ID_ITEM,
			&target_id, true) != B_OK
		|| pnp->get_property_uint8(node, SCSI_DEVICE_TARGET_LUN_ITEM,
			&target_lun, true) != B_OK) {
		free(device);
		return B_ERROR;
	}

	sprintf(device->publishedPath, "bus/scsi/%d/%d/%d/raw",
		path_id, target_id, target_lun);

	SHOW_FLOW(3, "name=%s", device->publishedPath);

	status = pnp->publish_device(node, device->publishedPath, &sRawDeviceOps);
	if (status != B_OK) {
		free(device);
		return status;
	}

	*_cookie = device;
	return B_OK;
}


static void
raw_detach(void *_cookie)
{
	raw_device_info *device = (raw_device_info *)_cookie;

	if (device->publishedPath[0] != '\0')
		pnp->unpublish_device(device->node, device->publishedPath);

	free(device);
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


static const dk_match_rule sScsiRawMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "scsi"),
	DK_MATCH_END
};

static const dk_match_dict sScsiRawMatchDict = {
	sScsiRawMatchRules,
	-1	// low priority — don't compete with real peripheral drivers
};


static dk_driver_info sScsiRawDriver = {
	.info   = { SCSI_RAW_MODULE_NAME, 0, std_ops },
	.match  = &sScsiRawMatchDict,
	.attach = raw_attach,
	.detach = raw_detach,
	.ops    = &sRawDeviceOps,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&pnp },
	{}
};

module_info *modules[] = {
	(module_info *)&sScsiRawDriver,
	NULL
};
