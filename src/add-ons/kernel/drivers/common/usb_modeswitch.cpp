/*
 * Copyright 2010, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, korli@users.berlios.de
 */

/*
	Devices and messages reference: usb-modeswitch-data-20100826
	Huawei devices updated to usb-modeswitch-data-20150115
*/

#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <lock.h>
#include <USB3.h>

#include <bus/USB.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRIVER_NAME			"usb_modeswitch"

#define TRACE_USB_MODESWITCH 1
#ifdef TRACE_USB_MODESWITCH
#define TRACE(x...)			dprintf(DRIVER_NAME ": " x)
#else
#define TRACE(x...)			/* nothing */
#endif
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)
#define ENTER()	TRACE("%s", __FUNCTION__)


#define USB_MODESWITCH_DRIVER_MODULE_NAME "drivers/common/usb_modeswitch/dk_driver_v1"


static usb_module_info *gUSBModule = NULL;
static dk_keeper_info *gDeviceKeeper = NULL;


enum msgType {
	MSG_HUAWEI_1 = 0,
	MSG_HUAWEI_2,
	MSG_HUAWEI_3,
	MSG_NOKIA_1,
	MSG_OLIVETTI_1,
	MSG_OLIVETTI_2,
	MSG_OPTION_1,
	MSG_ATHEROS_1,
	MSG_ZTE_1,
	MSG_ZTE_2,
	MSG_ZTE_3,
	MSG_NONE
};


unsigned char kDevicesMsg[][31] = {
	{ 	/* MSG_HUAWEI_1 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
		0x06, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_HUAWEI_2 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x0a, 0x11,
		0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{ 	/* MSG_HUAWEI_3 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
		0x06, 0x20, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_NOKIA_1 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_OLIVETTI_1 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_OLIVETTI_2 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0xc0, 0x00, 0x00, 0x00, 0x80, 0x01, 0x06, 0x06,
		0xf5, 0x04, 0x02, 0x52, 0x70, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_OPTION_1 */
		0x55, 0x53, 0x42, 0x43, 0x78, 0x56, 0x34, 0x12,
		0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0x10,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_ATHEROS_1 */
		0x55, 0x53, 0x42, 0x43, 0x29, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_ZTE_1 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1e,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_ZTE_2 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x79,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x1b,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
	{	/* MSG_ZTE_3 */
		0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x70,
		0x20, 0x00, 0x00, 0x00, 0x80, 0x00, 0x0c, 0x85,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	}
};


#define HUAWEI_VENDOR	0x12d1
#define NOKIA_VENDOR	0x0421
#define NOVATEL_VENDOR	0x1410
#define ZYDAS_VENDOR	0x0ace
#define ZTE_VENDOR		0x19d2
#define OLIVETTI_VENDOR	0x0b3c
#define OPTION_VENDOR	0x0af0
#define ATHEROS_VENDOR	0x0cf3


static const struct {
	usb_support_descriptor desc;
	msgType type, type2, type3;
} kDevices[] = {
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x101e}, MSG_HUAWEI_1},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1030}, MSG_HUAWEI_2},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1031}, MSG_HUAWEI_2},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1446}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1449}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14ad}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14b5}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14b7}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14ba}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14c1}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14c3}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14c4}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14c5}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14d1}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x14fe}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1505}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x151a}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1520}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1521}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1523}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1526}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1553}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1557}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x155b}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x156a}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1576}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x157d}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1583}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x15ca}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x15e7}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1c0b}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1c1b}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1c24}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1da1}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f01}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f02}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f03}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f11}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f15}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f16}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f17}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f18}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f19}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f1b}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f1c}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f1d}, MSG_HUAWEI_3},
	{{ 0, 0, 0, HUAWEI_VENDOR, 0x1f1e}, MSG_HUAWEI_3},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x060c}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x0610}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x061d}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x0622}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x0627}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x062c}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x0632}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOKIA_VENDOR, 0x0637}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5010}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5020}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5030}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5031}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5041}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x5059}, MSG_NOKIA_1},
	{{ 0, 0, 0, NOVATEL_VENDOR, 0x7001}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZYDAS_VENDOR, 0x2011}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZYDAS_VENDOR, 0x20ff}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0013}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0026}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0031}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0083}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0101}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0115}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0120}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0169}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0325}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1001}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1007}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1009}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1013}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1017}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1171}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1175}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1179}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1201}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1523}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0xffde}, MSG_NOKIA_1},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0003}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0053}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0103}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0154}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1224}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1517}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x1542}, MSG_ZTE_1, MSG_ZTE_2},
	{{ 0, 0, 0, ZTE_VENDOR, 0x0149}, MSG_ZTE_1, MSG_ZTE_2, MSG_ZTE_3},
	{{ 0, 0, 0, ZTE_VENDOR, 0x2000}, MSG_ZTE_1, MSG_ZTE_2, MSG_ZTE_3},
	{{ 0, 0, 0, OLIVETTI_VENDOR, 0xc700}, MSG_OLIVETTI_1},
	{{ 0, 0, 0, OLIVETTI_VENDOR, 0xf000}, MSG_OLIVETTI_2},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6711}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6731}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6751}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6771}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6791}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6811}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6911}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6951}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x6971}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7011}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7031}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7051}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7071}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7111}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7211}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7251}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7271}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7301}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7311}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7361}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7381}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7401}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7501}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7601}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7701}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7706}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7801}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x7901}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8006}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8200}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8201}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8300}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8302}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8304}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8400}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8600}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8800}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x8900}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0x9000}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xc031}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xc100}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xc031}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd013}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd031}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd033}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd035}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd055}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd057}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd058}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd155}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd157}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd255}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd257}, MSG_OPTION_1},
	{{ 0, 0, 0, OPTION_VENDOR, 0xd357}, MSG_OPTION_1},
	{{ 0, 0, 0, ATHEROS_VENDOR, 0x20ff}, MSG_ATHEROS_1},
};
static uint32 kDevicesCount = sizeof(kDevices) / sizeof(kDevices[0]);


typedef struct {
	dk_node*		node;
	usb_device		device;
	usb_pipe		bulk_in;
	usb_pipe		bulk_out;
	sem_id			notify;
	status_t		status;
	size_t			actual_length;
	msgType			type[3];
} usb_modeswitch_info;


//
//#pragma mark - Helper Functions
//


static enum msgType
my_get_msg_type(const usb_device_descriptor *desc, int index)
{
	for (uint32 i = 0; i < kDevicesCount; i++) {
		if (kDevices[i].desc.dev_class != 0x0
			&& kDevices[i].desc.dev_class != desc->device_class)
			continue;
		if (kDevices[i].desc.dev_subclass != 0x0
			&& kDevices[i].desc.dev_subclass != desc->device_subclass)
			continue;
		if (kDevices[i].desc.dev_protocol != 0x0
			&& kDevices[i].desc.dev_protocol != desc->device_protocol)
			continue;
		if (kDevices[i].desc.vendor != 0x0
			&& kDevices[i].desc.vendor != desc->vendor_id)
			continue;
		if (kDevices[i].desc.product != 0x0
			&& kDevices[i].desc.product != desc->product_id)
			continue;
		switch (index) {
			case 0:
				return kDevices[i].type;
			case 1:
				return kDevices[i].type2;
			case 2:
				return kDevices[i].type3;
		}
	}

	return MSG_NONE;
}


//
//#pragma mark - Bulk-only Functions
//


static void
my_callback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	usb_modeswitch_info *info = (usb_modeswitch_info *)cookie;
	info->status = status;
	info->actual_length = actualLength;
	release_sem(info->notify);
}


static status_t
my_transfer_data(usb_modeswitch_info *info, bool directionIn, void *data,
	size_t dataLength)
{
	status_t result = gUSBModule->queue_bulk(directionIn ? info->bulk_in
		: info->bulk_out, data, dataLength, my_callback, info);
	if (result != B_OK) {
		TRACE_ALWAYS("failed to queue data transfer\n");
		return result;
	}

	do {
		bigtime_t timeout = 500000;
		result = acquire_sem_etc(info->notify, 1, B_RELATIVE_TIMEOUT,
			timeout);
		if (result == B_TIMED_OUT) {
			gUSBModule->cancel_queued_transfers(directionIn ? info->bulk_in
				: info->bulk_out);
			acquire_sem_etc(info->notify, 1, B_RELATIVE_TIMEOUT, 0);
		}
	} while (result == B_INTERRUPTED);

	if (result != B_OK) {
		TRACE_ALWAYS("acquire_sem failed while waiting for data transfer\n");
		return result;
	}

	return B_OK;
}


static status_t
my_modeswitch(usb_modeswitch_info *info)
{
	if (info->type[0] == MSG_NONE)
		return B_OK;

	for (int i = 0; i < 3; i++) {
		if (info->type[i] == MSG_NONE)
			break;

		status_t err = my_transfer_data(info, false,
			kDevicesMsg[info->type[i]],
			sizeof(kDevicesMsg[info->type[i]]));
		if (err != B_OK) {
			TRACE_ALWAYS("send message %d failed\n", i + 1);
			return err;
		}

		TRACE("device switched: %" B_PRIu32 "\n", info->device);

		char data[36];
		err = my_transfer_data(info, true, data, sizeof(data));
		if (err != B_OK) {
			TRACE_ALWAYS("receive response %d failed 0x%" B_PRIx32 "\n",
				i + 1, info->status);
			return err;
		}
		TRACE("device switched (response length %ld)\n", info->actual_length);
	}

	TRACE("device switched: %" B_PRIu32 "\n", info->device);
	return B_OK;
}


//
//#pragma mark - Driver Module API
//


static float
usb_modeswitch_supports_device(dk_node *parent)
{
	TRACE("supports_device()\n");
	char bus[64];

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "usb") != 0)
		return -1.0f;

	uint16 vendorId, productId;
	if (gDeviceKeeper->get_property_uint16(parent, KOSM_USB_VENDOR_ID,
			&vendorId, false) != B_OK)
		return -1.0f;
	if (gDeviceKeeper->get_property_uint16(parent, KOSM_USB_PRODUCT_ID,
			&productId, false) != B_OK)
		return -1.0f;

	// check against our device table
	for (uint32 i = 0; i < kDevicesCount; i++) {
		if (kDevices[i].desc.vendor != 0
			&& kDevices[i].desc.vendor != vendorId)
			continue;
		if (kDevices[i].desc.product != 0
			&& kDevices[i].desc.product != productId)
			continue;
		TRACE("USB modeswitch device found! vendor: 0x%04x, product: 0x%04x\n",
			vendorId, productId);
		return 0.6f;
	}

	return -1.0f;
}


static status_t
usb_modeswitch_attach(dk_node *node, void **cookie)
{
	TRACE("init_driver()\n");

	usb_modeswitch_info *info = (usb_modeswitch_info *)malloc(
		sizeof(usb_modeswitch_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	// get USB device ID
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&info->device, true) != B_OK) {
		free(info);
		return B_ERROR;
	}

	// get device descriptor for message type lookup
	const usb_device_descriptor *descriptor
		= gUSBModule->get_device_descriptor(info->device);
	if (descriptor == NULL) {
		free(info);
		return B_ERROR;
	}

	for (int i = 0; i < 3; i++)
		info->type[i] = my_get_msg_type(descriptor, i);

	if (info->type[0] == MSG_NONE) {
		TRACE("no matching message type\n");
		free(info);
		return B_ERROR;
	}

	// find bulk endpoints
	const usb_configuration_info *configuration
		= gUSBModule->get_configuration(info->device);
	if (configuration == NULL) {
		free(info);
		return B_ERROR;
	}

	bool hasIn = false, hasOut = false;
	for (size_t i = 0; i < configuration->interface_count; i++) {
		usb_interface_info *interface = configuration->interface[i].active;
		if (interface == NULL)
			continue;

		for (size_t j = 0; j < interface->endpoint_count; j++) {
			usb_endpoint_info *endpoint = &interface->endpoint[j];
			if (endpoint == NULL
				|| endpoint->descr->attributes != USB_ENDPOINT_ATTR_BULK)
				continue;

			if (!hasIn && (endpoint->descr->endpoint_address
				& USB_ENDPOINT_ADDR_DIR_IN)) {
				info->bulk_in = endpoint->handle;
				hasIn = true;
			} else if (!hasOut && (endpoint->descr->endpoint_address
				& USB_ENDPOINT_ADDR_DIR_IN) == 0) {
				info->bulk_out = endpoint->handle;
				hasOut = true;
			}

			if (hasIn && hasOut)
				break;
		}

		if (hasIn && hasOut)
			break;
	}

	if (!(hasIn && hasOut)) {
		TRACE_ALWAYS("no valid bulk endpoints found\n");
		free(info);
		return B_ERROR;
	}

	// create notification semaphore
	info->notify = create_sem(0, DRIVER_NAME " callback notify");
	if (info->notify < B_OK) {
		status_t status = info->notify;
		free(info);
		return status;
	}

	// perform the mode switch
	status_t result = my_modeswitch(info);
	if (result != B_OK)
		TRACE_ALWAYS("modeswitch failed: %s\n", strerror(result));

	*cookie = info;
	return B_OK;
}


static void
usb_modeswitch_detach(void *_cookie)
{
	TRACE("detach()\n");
	usb_modeswitch_info *info = (usb_modeswitch_info *)_cookie;

	if (info->notify >= 0)
		delete_sem(info->notify);
	free(info);
}


//
//#pragma mark -
//


static const dk_match_rule sUsbModeSwitchMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbModeSwitchMatchDict = {
	sUsbModeSwitchMatchRules,
	0
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ NULL }
};

static dk_driver_info sUsbModeSwitchDriver = {
	.info = {
		USB_MODESWITCH_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbModeSwitchMatchDict,
	.probe   = usb_modeswitch_supports_device,
	.attach  = usb_modeswitch_attach,
	.detach  = usb_modeswitch_detach,
};

module_info *modules[] = {
	(module_info *)&sUsbModeSwitchDriver,
	NULL
};
