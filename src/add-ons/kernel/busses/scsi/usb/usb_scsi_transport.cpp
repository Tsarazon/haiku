/*
 * Copyright 2003-2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * USB Mass Storage transport layer: Bulk-Only (BBB) and CBI protocols.
 *
 * References:
 *   USB Mass Storage Class Bulk-Only Transport, Revision 1.0
 *   USB Mass Storage Class CBI Transport, Revision 1.1
 */

#include "usb_scsi.h"

#include <string.h>


// ---- Bulk-Only constants ----

#define CBW_SIGNATURE		0x43425355
#define CSW_SIGNATURE		0x53425355
#define CBW_LENGTH			31
#define CSW_LENGTH			13
#define CBW_FLAGS_IN		0x80
#define CBW_FLAGS_OUT		0x00
#define CSW_STATUS_GOOD		0x00
#define CSW_STATUS_FAILED	0x01
#define CSW_STATUS_PHASE	0x02

#define USB_REQ_MS_RESET		0xFF
#define USB_REQ_MS_GET_MAX_LUN	0xFE

// CBI constants
#define USB_REQ_CBI_ADSC		0x00
#define CBI_IDB_TYPE_CCI		0x00
#define CBI_IDB_VALUE_PASS		0x00
#define CBI_IDB_VALUE_FAIL		0x01
#define CBI_IDB_VALUE_PHASE		0x02
#define CBI_IDB_VALUE_MASK		0x03


struct usb_mass_CBW {
	uint32	signature;
	uint32	tag;
	uint32	data_transfer_len;
	uint8	flags;
	uint8	lun;
	uint8	cdb_len;
	uint8	CDB[16];
} _PACKED;

struct usb_mass_CSW {
	uint32	signature;
	uint32	tag;
	uint32	data_residue;
	uint8	status;
} _PACKED;

struct usb_mass_CBI_IDB {
	uint8	byte0;
	uint8	byte1;
} _PACKED;


// ---- USB helpers ----


void
usb_scsi_bulk_callback(void* cookie, status_t status, void* data,
	size_t actualLen)
{
	usb_mass_device* dev = (usb_mass_device*)cookie;
	dev->usb_status = status;
	dev->callback_data = data;
	dev->actual_len = (int)actualLen;
	if (status != B_CANCELED)
		release_sem(dev->trans_sem);
}


status_t
usb_scsi_queue_bulk(usb_mass_device* dev, void* buffer, size_t len,
	bool dirIn)
{
	usb_pipe pipe = dirIn ? dev->pipe_in : dev->pipe_out;
	status_t status = gUSB->queue_bulk(pipe, buffer, len,
		usb_scsi_bulk_callback, dev);
	if (status != B_OK) {
		ERROR("queue_bulk failed: %s\n", strerror(status));
		return status;
	}

	status = acquire_sem_etc(dev->trans_sem, 1, B_RELATIVE_TIMEOUT,
		dev->trans_timeout);
	if (status != B_OK) {
		ERROR("queue_bulk timeout: %s\n", strerror(status));
		gUSB->cancel_queued_transfers(pipe);
		return status;
	}

	return dev->usb_status;
}


// ---- Bulk-Only protocol ----


status_t
usb_scsi_get_max_lun(usb_mass_device* dev)
{
	dev->max_lun = 0;
	if (HAS_FIXES(dev->properties, FIX_NO_GETMAXLUN))
		return B_OK;

	size_t len = 0;
	status_t status = gUSB->send_request(dev->usb_dev,
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_REQ_MS_GET_MAX_LUN, 0, dev->interface,
		1, &dev->max_lun, &len);
	if (status != B_OK) {
		TRACE("GET_MAX_LUN not supported, single LUN assumed\n");
		dev->max_lun = 0;
	}
	return B_OK;
}


static status_t
bulk_only_reset(usb_mass_device* dev)
{
	status_t status = gUSB->send_request(dev->usb_dev,
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
		USB_REQ_MS_RESET, 0, dev->interface, 0, NULL, 0);
	if (status != B_OK)
		ERROR("bulk reset failed: %s\n", strerror(status));

	gUSB->clear_feature(dev->pipe_in, USB_FEATURE_ENDPOINT_HALT);
	gUSB->clear_feature(dev->pipe_out, USB_FEATURE_ENDPOINT_HALT);
	return status;
}


static status_t
bulk_only_transfer(usb_mass_device* dev, scsi_ccb* ccb)
{
	// Build CBW
	usb_mass_CBW cbw;
	memset(&cbw, 0, sizeof(cbw));
	cbw.signature = CBW_SIGNATURE;
	cbw.tag = atomic_add((int32*)&dev->tag, 1);
	cbw.data_transfer_len = ccb->data_length;
	cbw.flags = (ccb->flags & SCSI_DIR_IN) ? CBW_FLAGS_IN : CBW_FLAGS_OUT;
	cbw.lun = ccb->target_lun & 0x0F;
	cbw.cdb_len = ccb->cdb_length;
	memcpy(cbw.CDB, ccb->cdb, ccb->cdb_length);

	// Send CBW
	status_t status = usb_scsi_queue_bulk(dev, &cbw, CBW_LENGTH, false);
	if (status != B_OK) {
		ERROR("CBW send failed: %s\n", strerror(status));
		bulk_only_reset(dev);
		return B_IO_ERROR;
	}

	// Data phase
	if (ccb->data_length > 0 && ccb->data != NULL) {
		bool dirIn = (ccb->flags & SCSI_DIR_IN) != 0;
		status = usb_scsi_queue_bulk(dev, ccb->data, ccb->data_length,
			dirIn);
		if (status == B_DEV_STALLED) {
			gUSB->clear_feature(dirIn ? dev->pipe_in : dev->pipe_out,
				USB_FEATURE_ENDPOINT_HALT);
			// stall cleared, continue to read CSW
		} else if (status != B_OK) {
			ERROR("data phase failed: %s\n", strerror(status));
			bulk_only_reset(dev);
			return B_IO_ERROR;
		}
	}

	// Read CSW
	usb_mass_CSW csw;
	memset(&csw, 0, sizeof(csw));
	status = usb_scsi_queue_bulk(dev, &csw, CSW_LENGTH, true);
	if (status != B_OK) {
		// Retry once: clear halt, re-read
		gUSB->clear_feature(dev->pipe_in, USB_FEATURE_ENDPOINT_HALT);
		status = usb_scsi_queue_bulk(dev, &csw, CSW_LENGTH, true);
		if (status != B_OK) {
			ERROR("CSW read failed: %s\n", strerror(status));
			bulk_only_reset(dev);
			return B_IO_ERROR;
		}
	}

	// Validate CSW
	if (csw.signature != CSW_SIGNATURE || csw.tag != cbw.tag) {
		ERROR("CSW invalid: sig=0x%08x tag=%d\n",
			(unsigned)csw.signature, (int)csw.tag);
		bulk_only_reset(dev);
		return B_IO_ERROR;
	}

	if (csw.status == CSW_STATUS_PHASE) {
		ERROR("CSW phase error\n");
		bulk_only_reset(dev);
		return B_IO_ERROR;
	}

	ccb->data_resid = csw.data_residue;

	if (csw.status == CSW_STATUS_FAILED) {
		ccb->subsys_status = SCSI_REQ_CMP_ERR;
		ccb->device_status = 0x02;  // CHECK CONDITION
		return B_OK;  // wire OK, command failed
	}

	ccb->subsys_status = SCSI_REQ_CMP;
	return B_OK;
}


// ---- CBI protocol ----


static status_t
cbi_reset(usb_mass_device* dev)
{
	uint8 resetCmd[12];
	memset(resetCmd, 0xFF, sizeof(resetCmd));
	resetCmd[0] = 0x1D;
	resetCmd[1] = 0x04;

	size_t len = 0;
	status_t status = gUSB->send_request(dev->usb_dev,
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
		USB_REQ_CBI_ADSC, 0, dev->interface,
		sizeof(resetCmd), resetCmd, &len);
	if (status != B_OK)
		ERROR("CBI reset failed: %s\n", strerror(status));

	gUSB->clear_feature(dev->pipe_in, USB_FEATURE_ENDPOINT_HALT);
	gUSB->clear_feature(dev->pipe_out, USB_FEATURE_ENDPOINT_HALT);
	return status;
}


static status_t
cbi_send_adsc(usb_mass_device* dev, void* cmd, size_t cmdLen)
{
	size_t len = 0;
	return gUSB->send_request(dev->usb_dev,
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
		USB_REQ_CBI_ADSC, 0, dev->interface, cmdLen, cmd, &len);
}


static status_t
cbi_read_interrupt_status(usb_mass_device* dev, usb_mass_CBI_IDB* idb)
{
	status_t status = gUSB->queue_interrupt(dev->pipe_intr, idb,
		sizeof(*idb), usb_scsi_bulk_callback, dev);
	if (status != B_OK)
		return status;

	status = acquire_sem_etc(dev->trans_sem, 1, B_RELATIVE_TIMEOUT,
		dev->trans_timeout);
	if (status != B_OK) {
		gUSB->cancel_queued_transfers(dev->pipe_intr);
		return status;
	}
	return dev->usb_status;
}


static status_t
cbi_transfer(usb_mass_device* dev, scsi_ccb* ccb)
{
	// Send command via ADSC
	status_t status = cbi_send_adsc(dev, ccb->cdb, ccb->cdb_length);
	if (status != B_OK) {
		if (status == B_DEV_STALLED) {
			ccb->subsys_status = SCSI_REQ_CMP_ERR;
			ccb->device_status = 0x02;  // CHECK CONDITION
			return B_OK;
		}
		cbi_reset(dev);
		return B_IO_ERROR;
	}

	// Data phase
	if (ccb->data_length > 0 && ccb->data != NULL) {
		bool dirIn = (ccb->flags & SCSI_DIR_IN) != 0;
		status = usb_scsi_queue_bulk(dev, ccb->data, ccb->data_length,
			dirIn);
		if (status != B_OK) {
			cbi_reset(dev);
			return B_IO_ERROR;
		}
	}

	// For CBI (with interrupt), read status
	if (PROTO(dev->properties) == PROTO_CBI && dev->pipe_intr != 0) {
		usb_mass_CBI_IDB idb;
		memset(&idb, 0, sizeof(idb));
		status = cbi_read_interrupt_status(dev, &idb);
		if (status != B_OK) {
			cbi_reset(dev);
			return B_IO_ERROR;
		}

		// Parse status
		if (CMDSET(dev->properties) == CMDSET_UFI) {
			if (idb.byte0 != 0 || idb.byte1 != 0) {
				ccb->subsys_status = SCSI_REQ_CMP_ERR;
				ccb->device_status = 0x02;  // CHECK CONDITION
				return B_OK;
			}
		} else {
			if (idb.byte0 == CBI_IDB_TYPE_CCI) {
				uint8 val = idb.byte1 & CBI_IDB_VALUE_MASK;
				if (val == CBI_IDB_VALUE_FAIL) {
					ccb->subsys_status = SCSI_REQ_CMP_ERR;
					ccb->device_status = 0x02;
					return B_OK;
				}
				if (val == CBI_IDB_VALUE_PHASE) {
					cbi_reset(dev);
					return B_IO_ERROR;
				}
			}
		}
	}

	// For CB (without interrupt), status is unknown — accept as OK
	ccb->subsys_status = SCSI_REQ_CMP;
	ccb->data_resid = 0;
	return B_OK;
}


// ---- public dispatch ----


status_t
usb_scsi_transfer(usb_mass_device* dev, scsi_ccb* ccb)
{
	switch (PROTO(dev->properties)) {
		case PROTO_BULK_ONLY:
			return bulk_only_transfer(dev, ccb);
		case PROTO_CBI:
		case PROTO_CB:
			return cbi_transfer(dev, ccb);
		default:
			ERROR("unknown protocol 0x%x\n", PROTO(dev->properties));
			return B_NOT_SUPPORTED;
	}
}


status_t
usb_scsi_reset(usb_mass_device* dev)
{
	switch (PROTO(dev->properties)) {
		case PROTO_BULK_ONLY:
			return bulk_only_reset(dev);
		case PROTO_CBI:
		case PROTO_CB:
			return cbi_reset(dev);
		default:
			return B_NOT_SUPPORTED;
	}
}
