/*
 * Copyright 2018-2020 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 */
#include "mmc_bus.h"

#include <Errors.h>

#include <stdint.h>


MMCBus::MMCBus(dk_node* node)
	:
	fNode(node),
	fController(NULL),
	fCookie(NULL),
	fStatus(B_OK),
	fWorkerThread(-1),
	fActiveDevice(0),
	fRegisteredCount(0)
{
	CALLED();

	// Walk up to the SDHCI slot node (parent) to retrieve the
	// mmc_bus_interface published there.
	fStatus = gDeviceKeeper->get_interface(node, MMC_BUS_INTERFACE_NAME,
		KOSM_INTERFACE_ANCESTORS,
		(const void**)&fController, &fCookie);

	if (fStatus != B_OK) {
		ERROR("Not able to establish the bus %s\n",
			strerror(fStatus));
		return;
	}

	memset(fRegisteredRCAs, 0, sizeof(fRegisteredRCAs));

	fScanSemaphore = create_sem(0, "MMC bus scan");
	fLockSemaphore = create_sem(1, "MMC bus lock");
	fWorkerThread = spawn_kernel_thread(_WorkerThread, "SD bus controller",
		B_NORMAL_PRIORITY, this);
	resume_thread(fWorkerThread);

	fController->set_scan_semaphore(fCookie, fScanSemaphore);
}


MMCBus::~MMCBus()
{
	CALLED();

	// Tell the worker thread we want to stop
	fStatus = B_SHUTTING_DOWN;

	// Delete the semaphores (this will unlock the worker thread if it was
	// waiting on them)
	delete_sem(fScanSemaphore);
	delete_sem(fLockSemaphore);

	// Wait for the worker thread to terminate
	status_t result;
	if (fWorkerThread != 0)
		wait_for_thread(fWorkerThread, &result);

	// Power off: reset all cards and stop the clock
	if (fController != NULL && fCookie != NULL) {
		fController->execute_command(fCookie, SD_GO_IDLE_STATE, 0, NULL);
		fController->set_clock(fCookie, 0);
	}
}


status_t
MMCBus::InitCheck()
{
	return fStatus;
}


void
MMCBus::Rescan()
{
	// Just wake up the thread for a scan
	release_sem(fScanSemaphore);
}


status_t
MMCBus::ExecuteCommand(uint16_t rca, uint8_t command, uint32_t argument,
	uint32_t* response)
{
	status_t status = _ActivateDevice(rca);
	if (status != B_OK)
		return status;
	return fController->execute_command(fCookie, command, argument, response);
}


status_t
MMCBus::DoIO(uint16_t rca, uint8_t command, IOOperation* operation,
	bool offsetAsSectors)
{
	status_t status = _ActivateDevice(rca);
	if (status != B_OK)
		return status;
	return fController->do_io(fCookie, command, operation, offsetAsSectors);
}


void
MMCBus::SetClock(int frequency)
{
	fController->set_clock(fCookie, frequency);
}


void
MMCBus::SetBusWidth(int width)
{
	fController->set_bus_width(fCookie, width);
}


status_t
MMCBus::_ActivateDevice(uint16_t rca)
{
	// Do nothing if the device is already activated
	if (fActiveDevice == rca)
		return B_OK;

	uint32_t response;
	status_t result;
	result = fController->execute_command(fCookie, SD_SELECT_DESELECT_CARD,
		((uint32)rca) << 16, &response);

	if (result == B_OK)
		fActiveDevice = rca;

	return result;
}


bool
MMCBus::_TrySelectCard(uint16_t rca)
{
	// Try to select the card. If it responds, it's still present.
	uint32_t response;
	status_t result = fController->execute_command(fCookie,
		SD_SELECT_DESELECT_CARD, ((uint32)rca) << 16, &response);
	if (result != B_OK)
		return false;

	// Check status bits — card should be in transfer state (4)
	uint8_t state = (response >> 9) & 0xF;
	if (state < 3 || state > 6) {
		TRACE("Card RCA %x in unexpected state %d\n", rca, state);
		return false;
	}

	return true;
}


status_t
MMCBus::_TrySwitchHighSpeed(uint16_t rca)
{
	// CMD6 (SWITCH_FUNC) — check if high speed mode is supported.
	// SD Physical Layer Spec v3.01+, section 4.3.10.
	//
	// Mode 0 (check): argument bit 31 = 0, function group 1 = 0xF (no change)
	// Mode 1 (switch): argument bit 31 = 1, function group 1 = 1 (high speed)
	//
	// The response is 512 bits (64 bytes) returned in data phase,
	// but we only need to check if the card supports the function.
	// Since our execute_command doesn't do data-phase transfers,
	// we use a simpler approach: just try to switch and check the
	// card status.

	// First read SCR to check spec version
	// SCR needs ACMD51, which requires a data transfer — skip for now
	// and just try the switch directly. Cards that don't support CMD6
	// will return an error, and we fall back to 25MHz.

	uint32_t response;
	status_t result;

	// Send CMD6 mode 1, switch to high speed (function group 1 = 1)
	// Argument: [31] mode=1 (switch), [3:0] function group 1 = 1
	result = fController->execute_command(fCookie, 6,
		0x80000001, &response);

	if (result != B_OK) {
		TRACE("CMD6 switch to high speed failed: %s\n", strerror(result));
		return result;
	}

	// Check card status for errors
	if ((response & 0xFFFF8000) != 0) {
		TRACE("CMD6 card reports error: %x\n", response);
		return B_ERROR;
	}

	TRACE("Switched RCA %x to high speed mode\n", rca);
	return B_OK;
}


void
MMCBus::_UnpublishGoneCards()
{
	for (int32 i = fRegisteredCount - 1; i >= 0; i--) {
		uint16_t rca = fRegisteredRCAs[i];

		if (!_TrySelectCard(rca)) {
			TRACE("Card RCA %x no longer responds, removing\n", rca);

			// Remove from tracking array
			fRegisteredRCAs[i] = fRegisteredRCAs[fRegisteredCount - 1];
			fRegisteredCount--;

			// Notify DeviceKeeper — trigger device_removed on child
			// nodes that match this RCA. The child was registered
			// with kMmcRcaAttribute, so we find it by attribute match.
			dk_match_rule rules[] = {
				{ kMmcRcaAttribute, B_UINT16_TYPE, { .ui16 = rca } },
				{}
			};

			dk_node* child = NULL;
			if (gDeviceKeeper->find_child_node(
					reinterpret_cast<dk_node*>(fNode), rules, &child) == B_OK) {
				gDeviceKeeper->unregister_node(child);
				gDeviceKeeper->put_node(child);
			}

			// Reset active device tracking if it was this card
			if (fActiveDevice == rca)
				fActiveDevice = 0;
		}
	}
}


void MMCBus::_AcquireScanSemaphore()
{
	release_sem(fLockSemaphore);
	acquire_sem(fScanSemaphore);
	acquire_sem(fLockSemaphore);
}


status_t
MMCBus::_WorkerThread(void* cookie)
{
	MMCBus* bus = (MMCBus*)cookie;
	uint32_t response;

	acquire_sem(bus->fLockSemaphore);

	// We assume the bus defaults to 400kHz clock and has already powered on
	// cards.

	// Reset all cards on the bus
	// This does not work if the bus has not been powered on yet (the command
	// will timeout), in that case we wait until asked to scan again when a
	// card has been inserted and powered on.
	status_t result;
	do {
		bus->_AcquireScanSemaphore();

		// Check if we need to exit early (possible if the parent device did
		// not manage initialize itself correctly)
		if (bus->fStatus == B_SHUTTING_DOWN) {
			release_sem(bus->fLockSemaphore);
			return B_OK;
		}

		TRACE("Reset the bus...\n");
		result = bus->ExecuteCommand(0, SD_GO_IDLE_STATE, 0, NULL);
		TRACE("CMD0 result: %s\n", strerror(result));
	} while (result != B_OK);

	// Need to wait at least 8 clock cycles after CMD0 before sending the next
	// command. With the default 400kHz clock that would be 20 microseconds,
	// but we need to wait at least 20ms here, otherwise the next command times
	// out
	snooze(30000);

	while (bus->fStatus != B_SHUTTING_DOWN) {
		TRACE("Scanning the bus\n");

		// Check for removed cards before scanning for new ones
		if (bus->fRegisteredCount > 0)
			bus->_UnpublishGoneCards();

		// Use the low speed clock and 1bit bus width for scanning
		bus->SetClock(400);
		bus->SetBusWidth(1);

		// Probe the voltage range
		enum {
			// Table 4-40 in physical layer specification v8.00
			// All other values are currently reserved
			HOST_27_36V = 1, //Host supplied voltage 2.7-3.6V
		};

		// An arbitrary value, we just need to check that the response
		// containts the same.
		static const uint8 kVoltageCheckPattern = 0xAA;

		// FIXME MMC cards will not reply to this! They expect CMD1 instead
		// SD v1 cards will also not reply, but we can proceed to ACMD41
		// If ACMD41 also does not work, it may be an SDIO card, too
		uint32_t probe = (HOST_27_36V << 8) | kVoltageCheckPattern;
		uint32_t hcs = 1 << 30;
		if (bus->ExecuteCommand(0, SD_SEND_IF_COND, probe, &response) != B_OK) {
			TRACE("Card does not implement CMD8, may be a V1 SD card\n");
			// Do not check for SDHC support in this case
			hcs = 0;
		} else if (response != probe) {
			ERROR("Card does not support voltage range (expected %x, "
				"reply %x)\n", probe, response);
			// TODO we should power off the bus in this case.
		}

		// Probe OCR, waiting for card to become ready
		// We keep repeating ACMD41 until the card replies that it is
		// initialized.
		uint32_t ocr;
		do {
			uint32_t cardStatus;
			while (bus->ExecuteCommand(0, SD_APP_CMD, 0, &cardStatus)
					== B_BUSY) {
				ERROR("Card locked after CMD8...\n");
				snooze(1000000);
			}
			if ((cardStatus & 0xFFFF8000) != 0)
				ERROR("SD card reports error %x\n", cardStatus);
			if ((cardStatus & (1 << 5)) == 0)
				ERROR("Card did not enter ACMD mode\n");

			bus->ExecuteCommand(0, SD_SEND_OP_COND, hcs | 0xFF8000, &ocr);

			if ((ocr & (1 << 31)) == 0) {
				TRACE("Card is busy\n");
				snooze(100000);
			}
		} while (((ocr & (1 << 31)) == 0));

		// FIXME this should be asked to each card, when there are multiple
		// ones. So ACMD41 should be moved inside the probing loop below?
		uint8_t cardType = CARD_TYPE_SD;

		if ((ocr & hcs) != 0)
			cardType = CARD_TYPE_SDHC;
		if ((ocr & (1 << 29)) != 0)
			cardType = CARD_TYPE_UHS2;
		if ((ocr & (1 << 24)) != 0)
			TRACE("Card supports 1.8v");
		TRACE("Voltage range: %x\n", ocr & 0xFFFFFF);

		// TODO send CMD11 to switch to low voltage mode if card supports it?

		// We use CMD2 (ALL_SEND_CID) and CMD3 (SEND_RELATIVE_ADDR) to assign
		// an RCA to all cards. Initially all cards have an RCA of 0 and will
		// all receive CMD2. But only one of them will reply (they do collision
		// detection while sending the CID in reply). We assign a new RCA to
		// that first card, and repeat the process with the remaining ones
		// until no one answers to CMD2. Then we know all cards have an RCA
		// (and a matching published device on our side).
		uint32_t cid[4];

		// This being an if statement as opposed to a while statement restricts
		// it to one device per bus.
		if (bus->ExecuteCommand(0, SD_ALL_SEND_CID, 0, cid) == B_OK) {
			bus->ExecuteCommand(0, SD_SEND_RELATIVE_ADDR, 0, &response);

			TRACE("RCA: %x Status: %x\n", response >> 16, response & 0xFFFF);

			if ((response & 0xFF00) != 0x500) {
				TRACE("Card did not enter data state\n");
				break;
			}

			uint32_t vendor = cid[3] & 0xFFFFFF;
			char name[6] = {(char)(cid[2] >> 24), (char)(cid[2] >> 16),
				(char)(cid[2] >> 8), (char)cid[2], (char)(cid[1] >> 24), 0};
			uint32_t serial = (cid[1] << 16) | (cid[0] >> 16);
			uint16_t revision = (cid[1] >> 20) & 0xF;
			revision *= 100;
			revision += (cid[1] >> 16) & 0xF;
			uint8_t month = cid[0] & 0xF;
			uint16_t year = 2000 + ((cid[0] >> 4) & 0xFF);
			uint16_t rca = response >> 16;

			dk_property attrs[] = {
				{ KOSM_DEVICE_BUS, B_STRING_TYPE, {.string = "mmc" }},
				{ KOSM_LABEL, B_STRING_TYPE, {.string = "mmc device" }},
				{ KOSM_DEVICE_VENDOR_ID, B_UINT32_TYPE, {.ui32 = vendor}},
				{ KOSM_DEVICE_ID, B_STRING_TYPE, {.string = name}},
				{ KOSM_DEVICE_UNIQUE_ID, B_UINT32_TYPE, {.ui32 = serial}},
				{ "mmc/revision", B_UINT16_TYPE, {.ui16 = revision}},
				{ "mmc/month", B_UINT8_TYPE, {.ui8 = month}},
				{ "mmc/year", B_UINT16_TYPE, {.ui16 = year}},
				{ kMmcRcaAttribute, B_UINT16_TYPE, {.ui16 = rca}},
				{ kMmcTypeAttribute, B_UINT8_TYPE, {.ui8 = cardType}},
				{}
			};

			// publish child device for the card
			gDeviceKeeper->register_node(bus->fNode, MMC_BUS_MODULE_NAME,
				attrs, NULL, NULL);

			// Track the RCA for hotplug removal detection
			if (bus->fRegisteredCount < kMaxCards)
				bus->fRegisteredRCAs[bus->fRegisteredCount++] = rca;

			// Try to switch to high speed mode (50MHz instead of 25MHz).
			// CMD6 is optional — cards that don't support it will simply
			// stay at default speed.
			if (bus->_TrySwitchHighSpeed(rca) == B_OK)
				bus->SetClock(50000);
			else
				bus->SetClock(25000);
		} else {
			// No new cards found — keep current clock
			bus->SetClock(25000);
		}

		// Wait for the next scan request
		// The thread will spend most of its time waiting here
		bus->_AcquireScanSemaphore();
	}

	release_sem(bus->fLockSemaphore);

	TRACE("poller thread terminating");
	return B_OK;
}
