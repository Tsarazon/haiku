/*
 * Copyright 2009-2010, Stefano Ceccherini (stefano.ceccherini@gmail.com)
 * Copyright 2008, Dustin Howett, dustin.howett@gmail.com. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "hpet.h"
#include "hpet_interface.h"
#include "int.h"
#include "msi.h"

#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <ACPI.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define TRACE_HPET
#ifdef TRACE_HPET
	#define TRACE(x) dprintf x
#else
	#define TRACE(x) ;
#endif

#define TEST_HPET
#define HPET64 0


#define HPET_DRIVER_MODULE_NAME "drivers/timer/hpet/dk_driver_v1"
#define HPET_DEVICE_ID_GENERATOR "hpet/device_id"

// HPET ACPI hardware ID
#define HPET_ACPI_HID "PNP0103"


static struct hpet_regs *sHPETRegs = NULL;
static uint64 sHPETPeriod = 0;

static area_id sHPETArea = -1;
static acpi_module_info* sAcpi;
static dk_keeper_info* sDeviceKeeper;
static int32 sOpenCount;


struct hpet_timer_cookie {
	int number;
	int32 irq;
	sem_id sem;
	hpet_timer* timer;
};

typedef struct {
	dk_node*	node;
} hpet_driver_info;


////////////////////////////////////////////////////////////////////////////////

static status_t hpet_open(void*, const char*, int, void**);
static status_t hpet_close(void*);
static status_t hpet_free(void*);
static status_t hpet_control(void*, uint32, void*, size_t);
static status_t hpet_read(void*, off_t, void*, size_t*);
static status_t hpet_write(void*, off_t, const void*, size_t*);

////////////////////////////////////////////////////////////////////////////////


static inline bigtime_t
hpet_convert_timeout(const bigtime_t &relativeTimeout)
{
#if HPET64
	bigtime_t counter = sHPETRegs->u0.counter64;
#else
	bigtime_t counter = sHPETRegs->u0.counter32;
#endif
	bigtime_t converted = (1000000000ULL / sHPETPeriod) * relativeTimeout;

	dprintf("counter: %" B_PRId64 ", relativeTimeout: %" B_PRId64
		", converted: %" B_PRId64 "\n", counter, relativeTimeout, converted);

	return converted + counter;
}


#define MIN_TIMEOUT 1

static status_t
hpet_set_hardware_timer(bigtime_t relativeTimeout, hpet_timer *timer)
{
	// TODO:
	if (relativeTimeout < MIN_TIMEOUT)
		relativeTimeout = MIN_TIMEOUT;

	bigtime_t timerValue = hpet_convert_timeout(relativeTimeout);

#if HPET64
	timer->u0.comparator64 = timerValue;
#else
	timer->u0.comparator32 = timerValue;
#endif

	// enable timer interrupt
	timer->config |= HPET_CONF_TIMER_INT_ENABLE;

	return B_OK;
}


static status_t
hpet_clear_hardware_timer(hpet_timer *timer)
{
	timer->config &= ~HPET_CONF_TIMER_INT_ENABLE;
	return B_OK;
}


static status_t
hpet_set_enabled(bool enabled)
{
	if (enabled)
		sHPETRegs->config |= HPET_CONF_MASK_ENABLED;
	else
		sHPETRegs->config &= ~HPET_CONF_MASK_ENABLED;
	return B_OK;
}


static status_t
hpet_set_legacy(bool enabled)
{
	if (!HPET_IS_LEGACY_CAPABLE(sHPETRegs))
		return B_NOT_SUPPORTED;

	if (enabled)
		sHPETRegs->config |= HPET_CONF_MASK_LEGACY;
	else
		sHPETRegs->config &= ~HPET_CONF_MASK_LEGACY;
	return B_OK;
}


#ifdef TRACE_HPET
static void
hpet_dump_timer(hpet_timer *timer)
{
	dprintf("hpet_dump_timer: config: 0x%" B_PRIx64 ", comparator: %"
		B_PRIu64 ", fsb_route: 0x%" B_PRIx64 " 0x%" B_PRIx64 "\n",
		timer->config, timer->u0.comparator64,
		timer->fsb_route[0], timer->fsb_route[1]);
	dprintf("\tint route cap: 0x%08x, "
		"int type: %s, int route: %u\n",
		(unsigned)HPET_GET_CONF_TIMER_INT_ROUTE_CAP(timer),
		HPET_GET_CONF_TIMER_INT_IS_LEVEL(timer) ? "level" : "edge",
		(unsigned)HPET_GET_CONF_TIMER_INT_ROUTE(timer));
}
#endif


static int32
hpet_timer_interrupt(void *arg)
{
	hpet_timer_cookie* hpetCookie = (hpet_timer_cookie*)arg;
	hpet_timer* timer = &sHPETRegs->timer[hpetCookie->number];

	int32 intStatus = 1 << hpetCookie->number;

	if (!HPET_GET_CONF_TIMER_INT_IS_LEVEL(timer)
			|| (sHPETRegs->interrupt_status & intStatus)) {
		sHPETRegs->interrupt_status |= intStatus;
		hpet_clear_hardware_timer(timer);

		release_sem_etc(hpetCookie->sem, 1, B_DO_NOT_RESCHEDULE);
		return B_HANDLED_INTERRUPT;
	}

	return B_UNHANDLED_INTERRUPT;
}


static status_t
hpet_init_timer(hpet_timer_cookie* cookie)
{
	hpet_timer* timer = cookie->timer;

	uint32 interrupt = 0;
	uint64 route = HPET_GET_CONF_TIMER_INT_ROUTE_CAP(timer);
	for (interrupt = 0; interrupt < 32; interrupt++) {
		if (route & (1 << interrupt))
			break;
	}

	if (interrupt == 32) {
		dprintf("hpet_init_timer: no IRQ route found\n");
		return B_ERROR;
	}

	timer->config &= ~(HPET_CONF_TIMER_INT_ROUTE_MASK
		| HPET_CONF_TIMER_TYPE
		| HPET_CONF_TIMER_FSB_ENABLE);

	// level triggered
	timer->config |= HPET_CONF_TIMER_INT_IS_LEVEL;

#if HPET64
	// enable 64 bit mode
	timer->config &= ~HPET_CONF_TIMER_32MODE;
#else
	// enable 32 bit mode
	timer->config |= HPET_CONF_TIMER_32MODE;
#endif

	timer->config |= (interrupt << HPET_CONF_TIMER_INT_ROUTE_SHIFT)
		& HPET_CONF_TIMER_INT_ROUTE_MASK;

	cookie->irq = interrupt = HPET_GET_CONF_TIMER_INT_ROUTE(timer);
	status_t status = install_io_interrupt_handler(interrupt,
		&hpet_timer_interrupt, cookie, 0);
	if (status != B_OK) {
		dprintf("hpet_init_timer(): cannot install interrupt handler: %s\n",
			strerror(status));
		return status;
	}
#ifdef TRACE_HPET
	hpet_dump_timer(timer);
#endif
	return B_OK;
}


static status_t
hpet_test()
{
	uint64 initialValue = sHPETRegs->u0.counter32;
	spin(10);
	uint64 finalValue = sHPETRegs->u0.counter32;

	if (initialValue == finalValue) {
		dprintf("hpet_test: counter does not increment\n");
		return B_ERROR;
	}

	return B_OK;
}


static status_t
hpet_init()
{
	if (sHPETRegs == NULL)
		return B_NO_INIT;

	sHPETPeriod = HPET_GET_PERIOD(sHPETRegs);

	TRACE(("hpet_init: HPET is at %p.\n"
			"\tVendor ID: 0x%04x, rev: 0x%02x, period: %" B_PRIu64 "\n",
		sHPETRegs, (unsigned)HPET_GET_VENDOR_ID(sHPETRegs),
		(unsigned)HPET_GET_REVID(sHPETRegs), sHPETPeriod));

	status_t status = hpet_set_enabled(false);
	if (status != B_OK)
		return status;

	status = hpet_set_legacy(false);
	if (status != B_OK)
		return status;

	uint32 numTimers = HPET_GET_NUM_TIMERS(sHPETRegs) + 1;

	TRACE(("hpet_init: HPET supports %" B_PRIu32 " timers, is %s bits wide, "
			"and is %sin legacy mode.\n",
			numTimers, HPET_IS_64BIT(sHPETRegs) ? "64" : "32",
			sHPETRegs->config & HPET_CONF_MASK_LEGACY ? "" : "not "));

	TRACE(("hpet_init: configuration: 0x%" B_PRIx64
		", timer_interrupts: 0x%" B_PRIx64 "\n",
		sHPETRegs->config, sHPETRegs->interrupt_status));

	if (numTimers < 3) {
		dprintf("hpet_init: HPET does not have at least 3 timers. "
			"Skipping.\n");
		return B_ERROR;
	}


#ifdef TRACE_HPET
	for (uint32 c = 0; c < numTimers; c++)
		hpet_dump_timer(&sHPETRegs->timer[c]);
#endif

	sHPETRegs->interrupt_status = 0;

	status = hpet_set_enabled(true);
	if (status != B_OK)
		return status;

#ifdef TEST_HPET
	status = hpet_test();
	if (status != B_OK)
		return status;
#endif

	return status;
}


//	#pragma mark - device module API


status_t
hpet_open(void* _info, const char* name, int openMode, void** cookie)
{
	*cookie = NULL;

	if (sHPETRegs == NULL)
		return B_NO_INIT;

	if (atomic_add(&sOpenCount, 1) != 0) {
		atomic_add(&sOpenCount, -1);
		return B_BUSY;
	}

	int timerNumber = 2;
		// TODO

	char semName[B_OS_NAME_LENGTH];
	snprintf(semName, B_OS_NAME_LENGTH, "hpet_timer %d sem", timerNumber);
	sem_id sem = create_sem(0, semName);
	if (sem < 0) {
		atomic_add(&sOpenCount, -1);
		return sem;
	}

	hpet_timer_cookie* hpetCookie
		= (hpet_timer_cookie*)malloc(sizeof(hpet_timer_cookie));
	if (hpetCookie == NULL) {
		delete_sem(sem);
		atomic_add(&sOpenCount, -1);
		return B_NO_MEMORY;
	}

	hpetCookie->number = timerNumber;
	hpetCookie->timer = &sHPETRegs->timer[timerNumber];
	hpetCookie->sem = sem;
	set_sem_owner(hpetCookie->sem, B_SYSTEM_TEAM);

	hpet_set_enabled(false);

	status_t status = hpet_init_timer(hpetCookie);
	if (status != B_OK)
		dprintf("hpet_open: initializing timer failed: %s\n",
			strerror(status));

	hpet_set_enabled(true);

	*cookie = hpetCookie;

	if (status != B_OK) {
		delete_sem(sem);
		free(hpetCookie);
		atomic_add(&sOpenCount, -1);
	}
	return status;
}


status_t
hpet_close(void* cookie)
{
	if (sHPETRegs == NULL)
		return B_NO_INIT;

	atomic_add(&sOpenCount, -1);

	hpet_timer_cookie* hpetCookie = (hpet_timer_cookie*)cookie;

	dprintf("hpet_close (%d)\n", hpetCookie->number);
	hpet_clear_hardware_timer(&sHPETRegs->timer[hpetCookie->number]);
	remove_io_interrupt_handler(hpetCookie->irq, &hpet_timer_interrupt,
		hpetCookie);

	return B_OK;
}


status_t
hpet_free(void* cookie)
{
	if (sHPETRegs == NULL)
		return B_NO_INIT;

	hpet_timer_cookie* hpetCookie = (hpet_timer_cookie*)cookie;

	delete_sem(hpetCookie->sem);

	free(cookie);

	return B_OK;
}


status_t
hpet_control(void* cookie, uint32 op, void* arg, size_t length)
{
	hpet_timer_cookie* hpetCookie = (hpet_timer_cookie*)cookie;
	status_t status = B_BAD_VALUE;

	switch (op) {
		case HPET_WAIT_TIMER:
		{
			bigtime_t value = *(bigtime_t*)arg;
			dprintf("hpet: wait timer (%d) for %" B_PRId64 "...\n",
				hpetCookie->number, value);
			hpet_set_hardware_timer(value,
				&sHPETRegs->timer[hpetCookie->number]);
			status = acquire_sem_etc(hpetCookie->sem, 1, B_CAN_INTERRUPT,
				B_INFINITE_TIMEOUT);
			break;
		}
		default:
			break;
	}

	return status;
}


status_t
hpet_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	*(uint64*)buffer = sHPETRegs->u0.counter64;
	*numBytes = sizeof(uint64);
	return B_OK;
}


status_t
hpet_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_NOT_ALLOWED;
}


//	#pragma mark - dk_device_ops / dk_driver_info


static dk_device_ops sHpetDeviceOps = {
	hpet_open,
	hpet_close,
	hpet_free,
	hpet_read,
	hpet_write,
	NULL,	// io
	hpet_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


static const dk_match_rule sHpetMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" } },
	{}
};

static const dk_match_dict sHpetMatchDict = {
	sHpetMatchRules,
	0
};


static float
hpet_probe(dk_node *node)
{
	// match HPET ACPI device (PNP0103)
	char hid[32];
	if (sDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_HID, hid,
			sizeof(hid), NULL, false) != B_OK || strcmp(hid, HPET_ACPI_HID)) {
		return 0.0;
	}

	TRACE(("HPET device found via ACPI!\n"));
	return 0.6;
}


static status_t
hpet_attach(dk_node *node, void **cookie)
{
	TRACE(("hpet_attach()\n"));

	acpi_hpet *hpetTable;
	status_t status = sAcpi->get_table(ACPI_HPET_SIGNATURE, 0,
		(void**)&hpetTable);
	if (status != B_OK)
		return status;

	sHPETArea = map_physical_memory("HPET registries",
		hpetTable->hpet_address.address,
		B_PAGE_SIZE,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&sHPETRegs);

	if (sHPETArea < 0)
		return sHPETArea;

	status = hpet_init();
	if (status != B_OK) {
		// hpet_init may have enabled the counter before failing
		// (e.g. hpet_test after hpet_set_enabled(true)).
		hpet_set_enabled(false);
		delete_area(sHPETArea);
		sHPETArea = -1;
		sHPETRegs = NULL;
		sHPETPeriod = 0;
		return status;
	}

	hpet_driver_info *info = (hpet_driver_info *)malloc(
		sizeof(hpet_driver_info));
	if (info == NULL) {
		hpet_set_enabled(false);
		delete_area(sHPETArea);
		sHPETArea = -1;
		sHPETRegs = NULL;
		sHPETPeriod = 0;
		return B_NO_MEMORY;
	}

	info->node = node;

	// Publish device (must be last: must not leave devfs entry pointing
	// at a partially-initialised driver on error).
	status = sDeviceKeeper->publish_device(node, "misc/hpet",
		&sHpetDeviceOps);
	if (status != B_OK) {
		free(info);
		hpet_set_enabled(false);
		delete_area(sHPETArea);
		sHPETArea = -1;
		sHPETRegs = NULL;
		sHPETPeriod = 0;
		return status;
	}

	*cookie = info;
	return B_OK;
}


static void
hpet_detach(void *_cookie)
{
	TRACE(("hpet_detach()\n"));
	hpet_driver_info *info = (hpet_driver_info *)_cookie;

	// Unpublish the devfs entry first so that no new opens can find
	// sHpetDeviceOps after we tear down sHPETRegs below. Existing opens
	// must already be closed by the devfs layer before detach is called.
	sDeviceKeeper->unpublish_device(info->node, "misc/hpet");

	hpet_set_enabled(false);

	if (sHPETArea >= B_OK) {
		delete_area(sHPETArea);
		sHPETArea = -1;
	}
	sHPETRegs = NULL;
	sHPETPeriod = 0;

	free(info);
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ B_ACPI_MODULE_NAME, (module_info **)&sAcpi },
	{ NULL }
};

struct dk_driver_info sHpetDriver = {
	.info = {
		HPET_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match	= &sHpetMatchDict,
	.probe	= hpet_probe,
	.attach	= hpet_attach,
	.detach	= hpet_detach,
	.ops	= &sHpetDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sHpetDriver,
	NULL
};
