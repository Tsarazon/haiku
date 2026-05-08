/*
 * Copyright 2018 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 */
#ifndef MMC_BUS_H
#define MMC_BUS_H


#include <new>
#include <stdio.h>
#include <string.h>

#include <lock.h>
#include <util/AutoLock.h>
#include "mmc.h"


#define MMCBUS_TRACE
#ifdef MMCBUS_TRACE
#	define TRACE(x...)		dprintf("\33[33mmmc_bus:\33[0m " x)
#else
#	define TRACE(x...)
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mmmc_bus:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33mmmc_bus:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

extern dk_keeper_info *gDeviceKeeper;

static const int kMaxCards = 16;


class MMCBus {
public:

								MMCBus(dk_node *node);
								~MMCBus();
				status_t		InitCheck();
				void			Rescan();

				status_t		ExecuteCommand(uint16_t rca, uint8_t command,
									uint32_t argument, uint32_t* response);
				status_t		DoIO(uint16_t rca, uint8_t command,
									IOOperation* operation,
									bool offsetAsSectors);

				void			SetClock(int frequency);
				void			SetBusWidth(int width);

				void			AcquireBus() { acquire_sem(fLockSemaphore); }
				void			ReleaseBus() { release_sem(fLockSemaphore); }
private:
				status_t		_ActivateDevice(uint16_t rca);
				void			_AcquireScanSemaphore();
				bool			_TrySelectCard(uint16_t rca);
				status_t		_TrySwitchHighSpeed(uint16_t rca);
				void			_UnpublishGoneCards();
		static	status_t		_WorkerThread(void*);

private:

		dk_node* 				fNode;
		mmc_bus_interface* 		fController;
		void* 					fCookie;
		status_t				fStatus;
		thread_id				fWorkerThread;
		sem_id					fScanSemaphore;
		sem_id					fLockSemaphore;
		uint16					fActiveDevice;

		// Tracking registered cards for hotplug removal detection
		uint16					fRegisteredRCAs[kMaxCards];
		int32					fRegisteredCount;
};


#endif /*MMC_BUS_H*/
