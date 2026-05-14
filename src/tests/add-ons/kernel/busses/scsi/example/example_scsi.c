/*
 * Copyright 2007, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 */
#include <module.h>
#include <device_keeper.h>
#include <bus/SCSI.h>


#define MODULE_NAME "example_scsi"


static scsi_for_sim_interface *sSimInterface;
static dk_keeper_info *sDeviceKeeper;


// module functions
static status_t
example_std_ops(int32 op, ...)
{
	dprintf(MODULE_NAME": std ops\n");

	switch (op) {
		case B_MODULE_INIT: {
			dprintf(MODULE_NAME": B_MODULE_INIT\n");
			return B_OK;
		}

		case B_MODULE_UNINIT: {
			dprintf(MODULE_NAME": B_MODULE_UNINIT\n");
			return B_OK;
		}
	}

	return B_ERROR;
}


// driver functions
static float
example_supports_device(dk_node *parent)
{
	dprintf(MODULE_NAME": supports device\n");
	return 0.0f;
}


static status_t
example_init_driver(dk_node *node, void **cookie)
{
	dprintf(MODULE_NAME": init driver\n");
	return B_OK;
}


static void
example_uninit_driver(void *cookie)
{
	dprintf(MODULE_NAME": uninit driver\n");
}


// sim functions
static void
example_scsi_io(scsi_sim_cookie cookie, scsi_ccb *ccb)
{
	dprintf(MODULE_NAME": scsi io\n");
}


static uchar
example_abort(scsi_sim_cookie cookie, scsi_ccb *ccbToAbort)
{
	dprintf(MODULE_NAME": abort\n");
	return 0;
}


static uchar
example_reset_device(scsi_sim_cookie cookie, uchar targetID, uchar targetLUN)
{
	dprintf(MODULE_NAME": reset device\n");
	return 0;
}


static uchar
example_term_io(scsi_sim_cookie cookie, scsi_ccb *ccbToTerminate)
{
	dprintf(MODULE_NAME": terminate io\n");
	return 0;
}


static uchar
example_path_inquiry(scsi_sim_cookie cookie, scsi_path_inquiry *inquiryData)
{
	dprintf(MODULE_NAME": path inquiry\n");
	return 0;
}


static uchar
example_scan_bus(scsi_sim_cookie cookie)
{
	dprintf(MODULE_NAME": scan bus\n");
	return 0;
}


static uchar
example_reset_bus(scsi_sim_cookie cookie)
{
	dprintf(MODULE_NAME": reset bus\n");
	return 0;
}


static void
example_get_restrictions(scsi_sim_cookie cookie, uchar targetID, bool *isATAPI,
	bool *noAutosense, uint32 *maxBlocks)
{
	dprintf(MODULE_NAME": get restrictions\n");
}


static status_t
example_ioctl(scsi_sim_cookie cookie, uint8 targetID, uint32 op, void *buffer,
	size_t bufferLength)
{
	dprintf(MODULE_NAME": io control\n");
	return B_ERROR;
}


// module management
module_dependency module_dependencies[] = {
	{ SCSI_FOR_SIM_MODULE_NAME, (module_info **)&sSimInterface },
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{}
};


static scsi_sim_interface example_sim_ops = {
	example_scsi_io,
	example_abort,
	example_reset_device,
	example_term_io,
	example_path_inquiry,
	example_scan_bus,
	example_reset_bus,
	example_get_restrictions,
	example_ioctl
};


static dk_driver_info example_sim = {
	{
		"busses/scsi/example/dk_driver_v1",
		0,
		example_std_ops
	},

	.probe = example_supports_device,
	.attach = example_init_driver,
	.detach = example_uninit_driver,

	.bus_ops = &example_sim_ops,
	.bus_ops_type = KOSM_BUS_TYPE_SCSI,
};


module_info *modules[] = {
	(module_info *)&example_sim,
	NULL
};
