/*
 * Copyright 2002-04, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


//!	Devfs entry for raw bus access.


#include "scsi_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <device/scsi_bus_raw_driver.h>


// The deviceCookie passed by publish_device is the bus manager's driver
// cookie, which is scsi_bus_info*. We use it directly — no separate
// allocation needed.


static status_t
scsi_bus_raw_open(void *deviceCookie, const char *path, int openMode,
	void **handle_cookie)
{
	*handle_cookie = deviceCookie;
	return B_OK;
}


static status_t
scsi_bus_raw_close(void *cookie)
{
	return B_OK;
}


static status_t
scsi_bus_raw_free(void *cookie)
{
	return B_OK;
}


static status_t
scsi_bus_raw_control(void *cookie, uint32 op, void *data, size_t length)
{
	scsi_bus_info *bus = (scsi_bus_info *)cookie;

	switch (op) {
		case B_SCSI_BUS_RAW_RESET:
			return bus->interface->reset_bus(bus->sim_cookie);

		case B_SCSI_BUS_RAW_PATH_INQUIRY:
			return bus->interface->path_inquiry(bus->sim_cookie,
				(scsi_path_inquiry*)data);
	}

	return B_ERROR;
}


static status_t
scsi_bus_raw_read(void *cookie, off_t position, void *data,
	size_t *numBytes)
{
	*numBytes = 0;
	return B_ERROR;
}


static status_t
scsi_bus_raw_write(void *cookie, off_t position,
	const void *data, size_t *numBytes)
{
	*numBytes = 0;
	return B_ERROR;
}


// Published by scsi_bus_attach() via publish_device("bus/scsi/N/bus_raw",
// &gSCSIBusRawOps). The deviceCookie is the scsi_bus_info* (driver
// cookie of the scsi bus node).
dk_device_ops gSCSIBusRawOps = {
	scsi_bus_raw_open,
	scsi_bus_raw_close,
	scsi_bus_raw_free,
	scsi_bus_raw_read,
	scsi_bus_raw_write,
	NULL,	// io
	scsi_bus_raw_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};
