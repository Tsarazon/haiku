/*
 * Copyright 2004-2025, Haiku, Inc. All rights reserved.
 * Originally by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 *
 * SCSI CDB transformations and pre/post command feature handling.
 *
 * Many USB mass storage devices do not support the full SCSI command
 * set. This module translates 6-byte commands to 10-byte, adjusts
 * MODE SENSE/SELECT headers, and handles per-device quirks.
 */

#include "usb_scsi.h"

#include <string.h>


// ---- 6→10 byte CDB transforms ----


// Transform READ_6/WRITE_6 → READ_10/WRITE_10
static void
transform_rw_6_to_10(const uint8* cdb6, uint8* cdb10)
{
	memset(cdb10, 0, 10);
	cdb10[0] = (cdb6[0] == SCSI_OP_READ_6) ? SCSI_OP_READ_10
		: SCSI_OP_WRITE_10;
	// LBA: cdb6[1..3] bits 4:0 + cdb6[2] + cdb6[3]
	cdb10[2] = 0;
	cdb10[3] = cdb6[1] & 0x1F;
	cdb10[4] = cdb6[2];
	cdb10[5] = cdb6[3];
	// Transfer length: cdb6[4], 0 means 256 blocks
	uint16 blocks = cdb6[4];
	if (blocks == 0)
		blocks = 256;
	cdb10[7] = (blocks >> 8) & 0xFF;
	cdb10[8] = blocks & 0xFF;
}


// Transform MODE_SENSE_6 → MODE_SENSE_10
static void
transform_mode_sense_6_to_10(const uint8* cdb6, uint8* cdb10)
{
	memset(cdb10, 0, 10);
	cdb10[0] = SCSI_OP_MODE_SENSE_10;
	cdb10[1] = cdb6[1];			// DBD flag
	cdb10[2] = cdb6[2];			// page code + PC
	// allocation length: 6-byte uses 1 byte, 10-byte uses 2 bytes
	// add 4 extra for the larger 10-byte header
	uint16 allocLen = cdb6[4];
	if (allocLen > 0)
		allocLen += 4;  // 10-byte header is 4 bytes larger
	cdb10[7] = (allocLen >> 8) & 0xFF;
	cdb10[8] = allocLen & 0xFF;
}


// Transform MODE_SELECT_6 → MODE_SELECT_10
static void
transform_mode_select_6_to_10(const uint8* cdb6, uint8* cdb10)
{
	memset(cdb10, 0, 10);
	cdb10[0] = SCSI_OP_MODE_SELECT_10;
	cdb10[1] = cdb6[1];			// PF + SP flags
	uint16 paramLen = cdb6[4];
	if (paramLen > 0)
		paramLen += 4;
	cdb10[7] = (paramLen >> 8) & 0xFF;
	cdb10[8] = paramLen & 0xFF;
}


// Convert MODE_SENSE_10 response data to MODE_SENSE_6 format in-place.
// The 10-byte header is 8 bytes; the 6-byte header is 4 bytes.
// We shift the page data down by 4 bytes.
static void
convert_mode_sense_10_to_6(uint8* data, uint32 dataLen)
{
	if (dataLen < 8)
		return;

	// 10-byte header: [0..1] mode data length, [2] medium type,
	//   [3] device specific, [4..5] reserved, [6..7] block desc length
	// 6-byte header: [0] mode data length, [1] medium type,
	//   [2] device specific, [3] block desc length

	uint8 header6[4];
	header6[0] = data[1];		// data length LSB (truncated)
	header6[1] = data[2];		// medium type
	header6[2] = data[3];		// device specific
	header6[3] = data[7];		// block descriptor length LSB

	// Shift page data from offset 8 to offset 4
	if (dataLen > 8)
		memmove(data + 4, data + 8, dataLen - 8);

	memcpy(data, header6, 4);
}


// Convert MODE_SELECT_6 parameter data to MODE_SELECT_10 format.
// The 6-byte header is 4 bytes; the 10-byte header is 8 bytes.
// We need to expand the header by 4 bytes.
static void
convert_mode_select_6_to_10(uint8* data, uint32* dataLen, uint32 bufSize)
{
	if (*dataLen < 4 || *dataLen + 4 > bufSize)
		return;

	// Shift page data from offset 4 to offset 8
	if (*dataLen > 4)
		memmove(data + 8, data + 4, *dataLen - 4);

	uint8 header10[8];
	memset(header10, 0, 8);
	header10[1] = data[0];		// mode data length
	header10[2] = data[1];		// medium type
	header10[3] = data[2];		// device specific
	header10[7] = data[3];		// block descriptor length

	memcpy(data, header10, 8);
	*dataLen += 4;
}


// ---- SCSI command set transforms ----
// These adapt the CDB for specific command sets (UFI, ATAPI, RBC, QIC-157).
// The common case (SCSI transparent) passes the CDB through unmodified.


static void
transform_cdb(usb_mass_device* dev, scsi_ccb* ccb)
{
	uint8 opcode = ccb->cdb[0];

	// For UFI and ATAPI: always use 12-byte commands
	uint32 cmdset = CMDSET(dev->properties);
	if (cmdset == CMDSET_UFI || cmdset == CMDSET_ATAPI) {
		// Pad CDB to 12 bytes if shorter
		if (ccb->cdb_length < 12) {
			memset(ccb->cdb + ccb->cdb_length, 0,
				12 - ccb->cdb_length);
			ccb->cdb_length = 12;
		}
	}

	// Check if we need 6→10 conversion for READ/WRITE
	if (HAS_FIXES(dev->properties, FIX_FORCE_RW_TO_6)) {
		// Already using 6-byte — no conversion needed
		return;
	}

	// Convert 6-byte R/W to 10-byte for devices that need it
	if (opcode == SCSI_OP_READ_6 || opcode == SCSI_OP_WRITE_6) {
		uint8 cdb10[10];
		transform_rw_6_to_10(ccb->cdb, cdb10);
		memcpy(ccb->cdb, cdb10, 10);
		ccb->cdb_length = 10;
	}
}


// ---- pre/post command feature handling ----


bool
usb_scsi_pre_handle(usb_mass_device* dev, scsi_ccb* ccb)
{
	uint8 opcode = ccb->cdb[0];

	// Skip TEST_UNIT_READY for LUNs known to be not ready
	if (opcode == SCSI_OP_TEST_UNIT_READY
		&& HAS_FIXES(dev->properties, FIX_TRANS_TEST_UNIT)) {
		uint8 lun = ccb->target_lun;
		if (lun < 8 && (dev->not_ready_luns & (1 << lun))) {
			ccb->subsys_status = SCSI_REQ_CMP_ERR;
			ccb->device_status = 0x02;  // CHECK CONDITION
			return false;
		}
	}

	// Skip PREVENT_ALLOW_MEDIUM_REMOVAL if device doesn't support it
	if (opcode == SCSI_OP_PREVENT_ALLOW
		&& HAS_FIXES(dev->properties, FIX_NO_PREVENT_MEDIA)) {
		ccb->subsys_status = SCSI_REQ_CMP;
		return false;
	}

	// Replace INQUIRY with fake response if device doesn't support it
	if (opcode == SCSI_OP_INQUIRY
		&& HAS_FIXES(dev->properties, FIX_NO_INQUIRY)
		&& ccb->data != NULL && ccb->data_length >= 36) {
		memset(ccb->data, 0, ccb->data_length);
		ccb->data[1] = 0x80;  // removable
		ccb->data[2] = 0x02;  // SCSI-2
		ccb->data[3] = 0x02;  // response format
		ccb->data[4] = 31;    // additional length
		if (ccb->data_length >= 36) {
			memcpy(ccb->data + 8,  "USB     ", 8);
			memcpy(ccb->data + 16, "Mass Storage    ", 16);
			memcpy(ccb->data + 32, "0100", 4);
		}
		ccb->subsys_status = SCSI_REQ_CMP;
		return false;
	}

	// Skip TEST_UNIT_READY entirely if quirk says so
	if (opcode == SCSI_OP_TEST_UNIT_READY
		&& HAS_FIXES(dev->properties, FIX_NO_TEST_UNIT)) {
		ccb->subsys_status = SCSI_REQ_CMP;
		return false;
	}

	// MODE_SENSE_6 → MODE_SENSE_10 if device needs it
	if (opcode == SCSI_OP_MODE_SENSE_6
		&& HAS_FIXES(dev->properties, FIX_FORCE_MS_TO_10)) {
		uint8 cdb10[10];
		transform_mode_sense_6_to_10(ccb->cdb, cdb10);
		memcpy(ccb->cdb, cdb10, 10);
		ccb->cdb_length = 10;
	}

	// MODE_SELECT_6 → MODE_SELECT_10 if device needs it
	if (opcode == SCSI_OP_MODE_SELECT_6
		&& HAS_FIXES(dev->properties, FIX_FORCE_MS_TO_10)) {
		// Convert parameter data 6→10
		if (ccb->data != NULL && ccb->data_length > 0) {
			convert_mode_select_6_to_10(ccb->data, &ccb->data_length,
				ccb->data_length + 4);
		}
		uint8 cdb10[10];
		transform_mode_select_6_to_10(ccb->cdb, cdb10);
		memcpy(ccb->cdb, cdb10, 10);
		ccb->cdb_length = 10;
	}

	// Set infinite timeout for FORMAT_UNIT
	if (opcode == SCSI_OP_FORMAT_UNIT)
		dev->trans_timeout = B_INFINITE_TIMEOUT;

	// Apply command set transforms (UFI/ATAPI padding, etc.)
	transform_cdb(dev, ccb);

	return true;  // proceed to send to device
}


bool
usb_scsi_post_handle(usb_mass_device* dev, scsi_ccb* ccb)
{
	uint8 opcode = ccb->cdb[0];
	bool cmdOK = (ccb->subsys_status == SCSI_REQ_CMP);

	// MODE_SENSE_6 failed → enable FIX_FORCE_MS_TO_10 and retry
	if (opcode == SCSI_OP_MODE_SENSE_6 && !cmdOK
		&& !HAS_FIXES(dev->properties, FIX_FORCE_MS_TO_10)) {
		TRACE("MODE_SENSE_6 failed, retrying as 10-byte\n");
		dev->properties |= FIX_FORCE_MS_TO_10;
		ccb->subsys_status = SCSI_REQ_INPROG;
		ccb->device_status = 0;
		return true;  // retry
	}

	// Convert MODE_SENSE_10 response back to 6-byte format
	if ((opcode == SCSI_OP_MODE_SENSE_6 || opcode == SCSI_OP_MODE_SENSE_10)
		&& cmdOK
		&& HAS_FIXES(dev->properties, FIX_FORCE_MS_TO_10)
		&& ccb->data != NULL && ccb->data_length >= 8) {
		convert_mode_sense_10_to_6(ccb->data, ccb->data_length);
	}

	// Set write-protect flag if quirk says so
	if (opcode == SCSI_OP_MODE_SENSE_6 && cmdOK
		&& HAS_FIXES(dev->properties, FIX_FORCE_READ_ONLY)
		&& ccb->data != NULL && ccb->data_length >= 3) {
		ccb->data[2] |= 0x80;  // WP bit in device-specific parameter
	}

	// Track not-ready LUNs from TEST_UNIT_READY results
	if (opcode == SCSI_OP_TEST_UNIT_READY) {
		uint8 lun = ccb->target_lun;
		if (lun < 8) {
			if (!cmdOK)
				dev->not_ready_luns |= (1 << lun);
			else
				dev->not_ready_luns &= ~(1 << lun);
		}
	}

	// Restore default timeout after FORMAT_UNIT
	if (opcode == SCSI_OP_FORMAT_UNIT)
		dev->trans_timeout = 15000000LL;

	return false;  // no retry
}
