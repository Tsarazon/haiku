/*
 * Copyright 2003, Thomas Kurschel. All rights reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SCSI_RAW_H
#define _SCSI_RAW_H

/*
	Part of Open SCSI Raw Driver
*/


#include <device_keeper.h>
#include <bus/SCSI.h>
#include <device/scsi.h>

#define debug_level_flow 0
#define debug_level_error 3
#define debug_level_info 0

#define DEBUG_MSG_PREFIX "SCSI_RAW -- "

#include "wrapper.h"


typedef struct raw_device_info {
	dk_node *node;
	scsi_device scsi_dev;
	scsi_device_interface *scsi;
	char publishedPath[64];
} raw_device_info;


#define SCSI_RAW_MODULE_NAME "drivers/bus/scsi/scsi_raw/dk_driver_v1"

#endif	/* _SCSI_RAW_H */
