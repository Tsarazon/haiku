/*
 * Copyright 2003-2025, Haiku, Inc. All rights reserved.
 * Originally by Siarzhuk Zharski <imker@gmx.li>
 * Rewritten 2025 for DeviceKeeper / modern SCSI bus manager.
 * Distributed under the terms of the MIT License.
 *
 * USB Mass Storage SCSI SIM.
 */
#ifndef _USB_SCSI_H_
#define _USB_SCSI_H_

#include <OS.h>
#include <USB3.h>
#include <bus/SCSI.h>
#include <device_keeper.h>

#define MODULE_NAME "usb_scsi"

#define MAX_DEVICES_COUNT	8
#define MAX_LUNS_COUNT		8


// ---- transport protocol (not bitmasks) ----

#define PROTO_BULK_ONLY		0x00000001
#define PROTO_CB			0x00000002
#define PROTO_CBI			0x00000003
#define PROTO_MASK			0x0000000f
#define PROTO(v)			((v) & PROTO_MASK)

// ---- command set (not bitmasks) ----

#define CMDSET_SCSI			0x00000010
#define CMDSET_UFI			0x00000020
#define CMDSET_ATAPI		0x00000030
#define CMDSET_RBC			0x00000040
#define CMDSET_QIC157		0x00000050
#define CMDSET_MASK			0x000000f0
#define CMDSET(v)			((v) & CMDSET_MASK)

// ---- device quirk fix flags (bitmask) ----

#define FIX_NO_GETMAXLUN		0x00000100
#define FIX_FORCE_RW_TO_6		0x00000200
#define FIX_NO_TEST_UNIT		0x00000400
#define FIX_NO_INQUIRY			0x00000800
#define FIX_TRANS_TEST_UNIT		0x00001000
#define FIX_NO_PREVENT_MEDIA	0x00002000
#define FIX_FORCE_MS_TO_10		0x00004000
#define FIX_FORCE_READ_ONLY		0x00008000
#define HAS_FIXES(v, f)			(((v) & (f)) == (f))

// ---- SCSI opcodes used in transforms ----

#define SCSI_OP_TEST_UNIT_READY		0x00
#define SCSI_OP_REQUEST_SENSE		0x03
#define SCSI_OP_FORMAT_UNIT			0x04
#define SCSI_OP_READ_6				0x08
#define SCSI_OP_WRITE_6				0x0A
#define SCSI_OP_INQUIRY				0x12
#define SCSI_OP_MODE_SELECT_6		0x15
#define SCSI_OP_MODE_SENSE_6		0x1A
#define SCSI_OP_START_STOP_UNIT		0x1B
#define SCSI_OP_PREVENT_ALLOW		0x1E
#define SCSI_OP_READ_CAPACITY		0x25
#define SCSI_OP_READ_10				0x28
#define SCSI_OP_WRITE_10			0x2A
#define SCSI_OP_VERIFY				0x2F
#define SCSI_OP_SYNCHRONIZE_CACHE	0x35
#define SCSI_OP_MODE_SELECT_10		0x55
#define SCSI_OP_MODE_SENSE_10		0x5A

// ---- USB endpoint / class constants ----

#define USB_EP_ATTR_BULK		0x02
#define USB_EP_ATTR_INTERRUPT	0x03
#define USB_EP_ATTR_MASK		0x03
#define USB_EP_ADDR_DIR_IN		0x80

#define USB_DEV_CLASS_MASS			0x08
#define USB_DEV_SUBCLASS_RBC		0x01
#define USB_DEV_SUBCLASS_SFF8020I	0x02
#define USB_DEV_SUBCLASS_QIC157		0x03
#define USB_DEV_SUBCLASS_UFI		0x04
#define USB_DEV_SUBCLASS_SFF8070I	0x05
#define USB_DEV_SUBCLASS_SCSI		0x06

#define USB_DEV_PROTOCOL_CBI		0x00
#define USB_DEV_PROTOCOL_CB			0x01
#define USB_DEV_PROTOCOL_BULK		0x50


// ---- per-device state ----

struct usb_mass_device {
	dk_node*		node;
	usb_device		usb_dev;
	uint16			interface;
	uint8			max_lun;
	uint32			properties;		// PROTO_* | CMDSET_* | FIX_*

	usb_pipe		pipe_in;
	usb_pipe		pipe_out;
	usb_pipe		pipe_intr;

	sem_id			lock_sem;
	sem_id			trans_sem;
	uint32			tag;
	status_t		usb_status;
	bigtime_t		trans_timeout;

	void*			callback_data;
	int				actual_len;

	// SCSI bus manager connection
	scsi_bus		scsi_bus_cookie;

	// Transform state
	uint8			transformed_cdb[SCSI_MAX_CDB_SIZE];
	uint8			not_ready_luns;		// bitmask
};


// ---- logging ----

//#define TRACE_USB_SCSI
#ifdef TRACE_USB_SCSI
#	define TRACE(x...)		dprintf(MODULE_NAME ": " x)
#else
#	define TRACE(x...)		do {} while (0)
#endif
#define TRACE_ALWAYS(x...)	dprintf(MODULE_NAME ": " x)
#define ERROR(x...)			dprintf(MODULE_NAME ": " x)


// ---- globals ----

extern usb_module_info*			gUSB;
extern scsi_for_sim_interface*	gSCSI;
extern dk_keeper_info*			gDeviceKeeper;


// ---- usb_scsi_transport.cpp ----

void	usb_scsi_bulk_callback(void* cookie, status_t status,
			void* data, size_t actualLen);
status_t usb_scsi_queue_bulk(usb_mass_device* dev, void* buffer,
			size_t len, bool dirIn);
status_t usb_scsi_transfer(usb_mass_device* dev, scsi_ccb* ccb);
status_t usb_scsi_reset(usb_mass_device* dev);
status_t usb_scsi_get_max_lun(usb_mass_device* dev);


// ---- usb_scsi_transform.cpp ----

// Returns true if the CCB should be sent to the device.
// Returns false if the CCB is already complete (subsys_status set).
bool	usb_scsi_pre_handle(usb_mass_device* dev, scsi_ccb* ccb);

// Returns true if the command should be retried (e.g. MODE_SENSE_6
// failed, now set FIX_FORCE_MS_TO_10 and retry as 10-byte).
bool	usb_scsi_post_handle(usb_mass_device* dev, scsi_ccb* ccb);


// ---- usb_scsi_quirks.cpp ----

void	usb_scsi_load_quirks();
uint32	usb_scsi_lookup_quirks(uint16 vendorId, uint16 productId);


#endif /* _USB_SCSI_H_ */
