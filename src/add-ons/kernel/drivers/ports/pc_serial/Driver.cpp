/*
 * Copyright 2009-2010, François Revol, <revol@free.fr>.
 * Sponsored by TuneTracker Systems.
 * Based on the Haiku usb_serial driver which is:
 *
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <dpc.h>
#include <Drivers.h>
#include <driver_settings.h>
#include <image.h>
#include <kernel/safemode.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#include "Driver.h"
#include "SerialDevice.h"


#define PC_SERIAL_DRIVER_MODULE_NAME "drivers/ports/pc_serial/driver_v1"
#define PC_SERIAL_DEVICE_MODULE_NAME "drivers/ports/pc_serial/device_v1"

static const char *sDeviceBaseName = DEVFS_BASE;

SerialDevice *gSerialDevices[DEVICES_COUNT];
char *gDeviceNames[DEVICES_COUNT + 1];
isa_module_info *gISAModule = NULL;
pci_module_info *gPCIModule = NULL;
tty_module_info *gTTYModule = NULL;
dpc_module_info *gDPCModule = NULL;
device_manager_info *gDeviceManager = NULL;
void* gDPCHandle = NULL;
sem_id gDriverLock = -1;
bool gHandleISA = false;
uint32 gKernelDebugPort = 0x3f8;

static bool sInitialized = false;

// 24 MHz clock
static const uint32 sDefaultRates[] = {
		0,		//B0
		2304,	//B50
		1536,	//B75
		1047,	//B110
		857,	//B134
		768,	//B150
		512,	//B200
		384,	//B300
		192,	//B600
		96,		//B1200
		64,		//B1800
		48,		//B2400
		24,		//B4800
		12,		//B9600
		6,		//B19200
		3,		//B38400
		2,		//B57600
		1,		//B115200
		0,		//B230400
		4,		//B31250
		0, //921600 !?
};

// XXX: should really be generated from metadata (CSV ?)

static const struct serial_support_descriptor sSupportedDevices[] = {

#ifdef HANDLE_ISA_COM
	// ISA devices
	{ B_ISA_BUS, "Generic 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550 } },
#endif
	// PCI devices

	// vendor/device matches first

	// vendor: NetMos
#define VN "MosChip"

	// used in Manhattan cards
	// 1 function / port
	{ B_PCI_BUS, VN" 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8, 0, 0, 0 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550,
		0x9710, 0x9865, PCI_INVAL, PCI_INVAL } },

	// single function with all ports
	// only BAR 0 & 1 are UART
	{ B_PCI_BUS, VN" 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8, (uint8)~0x3, 2, 0x000f },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550,
		0x9710, 0x9835, PCI_INVAL, PCI_INVAL } },

#undef VN

	// generic fallback matches
	{ B_PCI_BUS, "Generic 16550 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16550,
		PCI_INVAL, PCI_INVAL, PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16650 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16650,
		PCI_INVAL, PCI_INVAL, PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16750 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16750,
		PCI_INVAL, PCI_INVAL, PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16850 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16850,
		PCI_INVAL, PCI_INVAL, PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, "Generic 16950 Serial Port", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_serial, PCI_serial_16950,
		PCI_INVAL, PCI_INVAL, PCI_INVAL, PCI_INVAL } },

	// non PCI_serial devices
	{ B_PCI_BUS, "Lucent Modem", sDefaultRates, NULL, { 8, 8, 8 },
	  { PCI_simple_communications, PCI_simple_communications_other, 0x00,
		0x11C1, 0x0480, PCI_INVAL, PCI_INVAL } },

	{ B_PCI_BUS, NULL, NULL, NULL, {0}, {0} }
};


// hardcoded ISA ports
static struct isa_ports {
	uint32 ioBase;
	uint32 irq;
} sHardcodedPorts[] = {
	{ 0x3f8, 4 },
	{ 0x2f8, 3 },
	{ 0x3e8, 4 },
	{ 0x2e8, 3 },
};


//#pragma mark -

status_t
pc_serial_insert_device(SerialDevice *device)
{
	status_t status = B_OK;

	acquire_sem(gDriverLock);
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] != NULL)
			continue;

		status = device->Init();
		if (status < B_OK) {
			delete device;
			break;
		}

		gSerialDevices[i] = device;

		release_sem(gDriverLock);
		TRACE_ALWAYS("%s added\n", device->Description());
		return B_OK;
	}

	release_sem(gDriverLock);
	return B_ERROR;
}


// until we support ISA device enumeration from PnP BIOS or ACPI,
// we have to probe the 4 default COM ports...
status_t
scan_isa_hardcoded()
{
#ifdef HANDLE_ISA_COM
	int i;
	bool serialDebug = get_safemode_boolean("serial_debug_output", true);

	for (i = 0; i < 4; i++) {
		// skip the port used for kernel debugging...
		if (serialDebug && sHardcodedPorts[i].ioBase == gKernelDebugPort) {
			TRACE_ALWAYS("Skipping port %d as it is used for kernel debug.\n",
				i);
			continue;
		}

		SerialDevice *device;
		device = new(std::nothrow) SerialDevice(&sSupportedDevices[0],
			sHardcodedPorts[i].ioBase, sHardcodedPorts[i].irq);
		if (device != NULL && device->Probe())
			pc_serial_insert_device(device);
		else
			delete device;
	}

#endif
	return B_OK;
}


status_t
scan_pci()
{
	pci_info info;
	int ix;
	TRACE_ALWAYS("scanning PCI bus (alt)...\n");

	// probe PCI devices
	for (ix = 0; (*gPCIModule->get_nth_pci_info)(ix, &info) == B_OK; ix++) {
		// sanity check
		if ((info.header_type & PCI_header_type_mask)
				!= PCI_header_type_generic)
			continue;

		const struct serial_support_descriptor *supported = NULL;
		for (int i = 0; sSupportedDevices[i].name; i++) {
			if (sSupportedDevices[i].bus != B_PCI_BUS)
				continue;
			if (info.class_base != sSupportedDevices[i].match.class_base)
				continue;
			if (info.class_sub != sSupportedDevices[i].match.class_sub)
				continue;
			if (info.class_api != sSupportedDevices[i].match.class_api)
				continue;
			if (sSupportedDevices[i].match.vendor_id != PCI_INVAL
				&& info.vendor_id != sSupportedDevices[i].match.vendor_id)
				continue;
			if (sSupportedDevices[i].match.device_id != PCI_INVAL
				&& info.device_id != sSupportedDevices[i].match.device_id)
				continue;
			supported = &sSupportedDevices[i];
			break;
		}
		if (supported == NULL)
			continue;

		TRACE_ALWAYS("found PCI device %2d [%x|%x|%x] %04x:%04x as %s\n",
			ix, info.class_base, info.class_sub, info.class_api,
			info.vendor_id, info.device_id, supported->name);

		TRACE_ALWAYS("irq line %d, pin %d\n",
			info.u.h0.interrupt_line, info.u.h0.interrupt_pin);
		int irq = info.u.h0.interrupt_line;

		SerialDevice *master = NULL;

		uint8 portCount = 0;
		uint32 maxPorts = DEVICES_COUNT;

		if (supported->constraints.maxports) {
			maxPorts = supported->constraints.maxports;
			TRACE_ALWAYS("card supports up to %d ports\n", maxPorts);
		}
		if (supported->constraints.subsystem_id_mask) {
			uint32 id = info.u.h0.subsystem_id;
			uint32 mask = supported->constraints.subsystem_id_mask;
			id &= mask;
			while (!(mask & 0x1)) {
				mask >>= 1;
				id >>= 1;
			}
			maxPorts = (uint8)id;
			TRACE_ALWAYS("subsystem id tells card has %d ports\n", maxPorts);
		}

		// find I/O ports
		for (int r = 0; r < 6; r++) {
			uint32 regbase = info.u.h0.base_registers[r];
			uint32 reglen = info.u.h0.base_register_sizes[r];

			TRACE("ranges[%d] at 0x%08lx len 0x%lx flags 0x%02x\n", r,
				regbase, reglen, info.u.h0.base_register_flags[r]);

			// empty
			if (reglen == 0)
				continue;

			// not I/O
			if ((info.u.h0.base_register_flags[r] & PCI_address_space) == 0)
				continue;

			// the range for sure doesn't contain any UART
			if (supported->constraints.ignoremask & (1 << r)) {
				TRACE_ALWAYS("ignored regs at 0x%08lx len 0x%lx\n",
					regbase, reglen);
				continue;
			}

			TRACE_ALWAYS("regs at 0x%08lx len 0x%lx\n",
				regbase, reglen);

			if (reglen < supported->constraints.minsize)
				continue;
			if (reglen > supported->constraints.maxsize)
				continue;

			SerialDevice *device;
			uint32 ioport = regbase;
next_split_alt:
			// no more to split
			if ((ioport - regbase) >= reglen)
				continue;

			if (portCount >= maxPorts)
				break;

			TRACE_ALWAYS("inserting device at io 0x%04lx as %s\n", ioport,
				supported->name);

			device = new(std::nothrow) SerialDevice(supported, ioport, irq,
				master);
			if (device == NULL) {
				TRACE_ALWAYS("can't allocate device\n");
				continue;
			}

			if (pc_serial_insert_device(device) < B_OK) {
				TRACE_ALWAYS("can't insert device\n");
				continue;
			}
			if (master == NULL)
				master = device;

			ioport += supported->constraints.split;
			portCount++;
			goto next_split_alt;
		}
	}

	return B_OK;
}


static void
check_kernel_debug_port()
{
	void *handle;
	long int value;

	handle = load_driver_settings("kernel");
	if (handle == NULL)
		return;

	const char *str = get_driver_parameter(handle, "serial_debug_port",
		NULL, NULL);
	if (str != NULL) {
		value = strtol(str, NULL, 0);
		if (value >= 4)
			gKernelDebugPort = (uint32)value;
		else if (value >= 0)
			gKernelDebugPort = sHardcodedPorts[value].ioBase;
	}

	unload_driver_settings(handle);
}


// Ensure global state is initialized once
static status_t
ensure_globals_initialized()
{
	if (sInitialized)
		return B_OK;

	status_t status;
	load_settings();
	create_log_file();

	TRACE_FUNCALLS("> ensure_globals_initialized()\n");

	status = get_module(B_DPC_MODULE_NAME, (module_info **)&gDPCModule);
	if (status < B_OK)
		return status;

	status = get_module(B_TTY_MODULE_NAME, (module_info **)&gTTYModule);
	if (status < B_OK) {
		put_module(B_DPC_MODULE_NAME);
		return status;
	}

	status = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCIModule);
	if (status < B_OK) {
		put_module(B_TTY_MODULE_NAME);
		put_module(B_DPC_MODULE_NAME);
		return status;
	}

	status = get_module(B_ISA_MODULE_NAME, (module_info **)&gISAModule);
	if (status < B_OK) {
		put_module(B_PCI_MODULE_NAME);
		put_module(B_TTY_MODULE_NAME);
		put_module(B_DPC_MODULE_NAME);
		return status;
	}

	status = gDPCModule->new_dpc_queue(&gDPCHandle, "pc_serial irq",
		B_REAL_TIME_PRIORITY);
	if (status != B_OK) {
		put_module(B_ISA_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		put_module(B_TTY_MODULE_NAME);
		put_module(B_DPC_MODULE_NAME);
		return status;
	}

	for (int32 i = 0; i < DEVICES_COUNT; i++)
		gSerialDevices[i] = NULL;

	gDeviceNames[0] = NULL;

	gDriverLock = create_sem(1, DRIVER_NAME "_devices_table_lock");
	if (gDriverLock < B_OK) {
		gDPCModule->delete_dpc_queue(gDPCHandle);
		gDPCHandle = NULL;
		put_module(B_ISA_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		put_module(B_TTY_MODULE_NAME);
		put_module(B_DPC_MODULE_NAME);
		return gDriverLock;
	}

	check_kernel_debug_port();
	scan_isa_hardcoded();
	scan_pci();

	sInitialized = true;
	TRACE_FUNCRET("< ensure_globals_initialized() returns\n");
	return B_OK;
}


//#pragma mark -


bool
pc_serial_service(struct tty *tty, uint32 op, void *buffer, size_t length)
{
	TRACE_FUNCALLS("> pc_serial_service(%p, 0x%08lx, %p, %lu)\n", tty,
		op, buffer, length);

	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i]
			&& gSerialDevices[i]->Service(tty, op, buffer, length)) {
			TRACE_FUNCRET("< pc_serial_service() returns: true\n");
			return true;
		}
	}

	TRACE_FUNCRET("< pc_serial_service() returns: false\n");
	return false;
}


static void
pc_serial_dpc(void *arg)
{
	SerialDevice *master = (SerialDevice *)arg;
	TRACE_FUNCALLS("> pc_serial_dpc(%p)\n", arg);
	master->InterruptHandler();
}


int32
pc_serial_interrupt(void *arg)
{
	SerialDevice *device = (SerialDevice *)arg;
	TRACE_FUNCALLS("> pc_serial_interrupt(%p)\n", arg);

	if (!device)
		return B_UNHANDLED_INTERRUPT;

	if (device->IsInterruptPending()) {
		status_t err;
		err = gDPCModule->queue_dpc(gDPCHandle, pc_serial_dpc, device);
		if (err != B_OK)
			dprintf(DRIVER_NAME ": error queing irq: %s\n", strerror(err));
		else {
			TRACE_FUNCRET("< pc_serial_interrupt() returns: resched\n");
			return B_INVOKE_SCHEDULER;
		}
	}

	TRACE_FUNCRET("< pc_serial_interrupt() returns: unhandled\n");
	return B_UNHANDLED_INTERRUPT;
}


//	#pragma mark - device module API


static status_t
pc_serial_open(void *_info, const char *name, int openMode, void **_cookie)
{
	TRACE_FUNCALLS("> pc_serial_open(%s, 0x%08x)\n", name, openMode);
	acquire_sem(gDriverLock);
	status_t status = ENODEV;

	*_cookie = NULL;
	int i = strtol(name + strlen(sDeviceBaseName), NULL, 10);
	if (i >= 0 && i < DEVICES_COUNT && gSerialDevices[i]) {
		status = gSerialDevices[i]->Open(openMode);
		*_cookie = gSerialDevices[i];
	}

	release_sem(gDriverLock);
	TRACE_FUNCRET("< pc_serial_open() returns: 0x%08x\n", status);
	return status;
}


static status_t
pc_serial_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE_FUNCALLS("> pc_serial_read()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Read((char *)buffer, numBytes);
}


static status_t
pc_serial_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	TRACE_FUNCALLS("> pc_serial_write()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Write((const char *)buffer, numBytes);
}


static status_t
pc_serial_control(void *cookie, uint32 op, void *arg, size_t length)
{
	TRACE_FUNCALLS("> pc_serial_control()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Control(op, arg, length);
}


static status_t
pc_serial_select(void *cookie, uint8 event, selectsync *sync)
{
	TRACE_FUNCALLS("> pc_serial_select()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Select(event, 0, sync);
}


static status_t
pc_serial_deselect(void *cookie, uint8 event, selectsync *sync)
{
	TRACE_FUNCALLS("> pc_serial_deselect()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->DeSelect(event, sync);
}


static status_t
pc_serial_close(void *cookie)
{
	TRACE_FUNCALLS("> pc_serial_close()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	return device->Close();
}


static status_t
pc_serial_free(void *cookie)
{
	TRACE_FUNCALLS("> pc_serial_free()\n");
	SerialDevice *device = (SerialDevice *)cookie;
	acquire_sem(gDriverLock);
	status_t status = device->Free();
	if (device->IsRemoved()) {
		for (int32 i = 0; i < DEVICES_COUNT; i++) {
			if (gSerialDevices[i] == device) {
				delete device;
				gSerialDevices[i] = NULL;
				break;
			}
		}
	}

	release_sem(gDriverLock);
	return status;
}


//	#pragma mark - driver module API


static float
pc_serial_supports_device(device_node *parent)
{
	TRACE_FUNCALLS("> pc_serial_supports_device()\n");
	const char *bus;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "pci"))
		return 0.0;

	// match PCI simple communications / serial class
	uint8 classBase = 0, classSub = 0;
	gDeviceManager->get_attr_uint8(parent, B_DEVICE_TYPE, &classBase, false);
	gDeviceManager->get_attr_uint8(parent, B_DEVICE_SUB_TYPE, &classSub,
		false);

	if (classBase == PCI_simple_communications
		&& classSub == PCI_serial) {
		TRACE_ALWAYS("PCI serial device found!\n");
		return 0.6;
	}

	// also match Lucent modem (class 0x07, subclass 0x80)
	if (classBase == PCI_simple_communications
		&& classSub == PCI_simple_communications_other) {
		uint16 vendorId = 0;
		gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID,
			&vendorId, false);
		if (vendorId == 0x11C1) {
			TRACE_ALWAYS("Lucent modem found!\n");
			return 0.6;
		}
	}

	return 0.0;
}


static status_t
pc_serial_register_device(device_node *node)
{
	TRACE_FUNCALLS("> pc_serial_register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "PC Serial Port"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node,
		PC_SERIAL_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


static status_t
pc_serial_init_driver(device_node *node, void **cookie)
{
	TRACE_FUNCALLS("> pc_serial_init_driver()\n");

	status_t status = ensure_globals_initialized();
	if (status != B_OK)
		return status;

	// Store the node as cookie
	*cookie = node;
	return B_OK;
}


static void
pc_serial_uninit_driver(void *_cookie)
{
	TRACE_FUNCALLS("> pc_serial_uninit_driver()\n");
	// Global cleanup happens when the last driver instance is unloaded
}


static status_t
pc_serial_register_child_devices(void *_cookie)
{
	TRACE_FUNCALLS("> pc_serial_register_child_devices()\n");
	device_node *node = (device_node *)_cookie;

	// Publish all detected serial ports
	acquire_sem(gDriverLock);
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] == NULL)
			continue;

		char name[64];
		snprintf(name, sizeof(name), "%s%d", sDeviceBaseName, (int)i);

		gDeviceManager->publish_device(node, name,
			PC_SERIAL_DEVICE_MODULE_NAME);
	}
	release_sem(gDriverLock);

	return B_OK;
}


//	#pragma mark - device init/uninit


static status_t
pc_serial_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
pc_serial_uninit_device(void *_cookie)
{
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{ NULL }
};

struct device_module_info sPcSerialDevice = {
	{
		PC_SERIAL_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	pc_serial_init_device,
	pc_serial_uninit_device,
	NULL,	// device_removed

	pc_serial_open,
	pc_serial_close,
	pc_serial_free,
	pc_serial_read,
	pc_serial_write,
	NULL,	// io
	pc_serial_control,

	pc_serial_select,
	pc_serial_deselect,
};

struct driver_module_info sPcSerialDriver = {
	{
		PC_SERIAL_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	pc_serial_supports_device,
	pc_serial_register_device,
	pc_serial_init_driver,
	pc_serial_uninit_driver,
	pc_serial_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info *modules[] = {
	(module_info *)&sPcSerialDriver,
	(module_info *)&sPcSerialDevice,
	NULL
};
