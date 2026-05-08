/*
 * Copyright 2004-2008, Haiku, Inc. All RightsReserved.
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

/*
	Controllers use this interface to interact with bus manager.
*/


#include "scsi_internal.h"
#include "queuing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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


// Plain kernel module retrieved by SCSI SIM drivers (ahci_sim,
// virtio_scsi_sim, usb_scsi) via get_module(SCSI_FOR_SIM_MODULE_NAME).
// Provides callbacks: requeue/resubmit/finished + dpc + bus/device
// blocking primitives used by the SIM side of the SCSI bus.
scsi_for_sim_interface scsi_for_sim_module = {
	{
		SCSI_FOR_SIM_MODULE_NAME,
		0,
		std_ops
	},

	scsi_requeue_request,
	scsi_resubmit_request,
	scsi_request_finished,

	scsi_alloc_dpc,
	scsi_free_dpc,
	scsi_schedule_dpc,

	scsi_block_bus,
	scsi_unblock_bus,
	scsi_block_device,
	scsi_unblock_device,

	scsi_cont_send_bus,
	scsi_cont_send_device
};
