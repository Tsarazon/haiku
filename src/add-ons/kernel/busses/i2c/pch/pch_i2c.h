/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PCH_I2C_H
#define _PCH_I2C_H


#include "pch_i2c_hardware.h"

extern "C" {
#	include "acpi.h"
}

#include <i2c.h>
#include <lock.h>


//#define TRACE_PCH_I2C
#ifdef TRACE_PCH_I2C
#	define TRACE(x...) dprintf("\33[33mpch_i2c_pci:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mpch_i2c_pci:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33mpch_i2c_pci:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define PCH_I2C_ACPI_DEVICE_MODULE_NAME "busses/i2c/pch_i2c/acpi/dk_driver_v1"
#define PCH_I2C_PCI_DEVICE_MODULE_NAME "busses/i2c/pch_i2c/pci/dk_driver_v1"
#define PCH_I2C_SIM_MODULE_NAME "busses/i2c/pch_i2c/controller/dk_driver_v1"


#define write32(address, data) \
	(*((volatile uint32*)(address)) = (data))
#define read32(address) \
	(*((volatile uint32*)(address)))


extern dk_keeper_info* gDeviceKeeper;
extern acpi_module_info* gACPI;
extern dk_driver_info gPchI2cAcpiDevice;
extern dk_driver_info gPchI2cPciDevice;


acpi_status pch_i2c_scan_bus_callback(acpi_handle object, uint32 nestingLevel,
	void *context, void** returnValue);


enum pch_version {
	PCH_NONE,
	PCH_EMAG,
	PCH_HASWELL,
	PCH_ATOM,
	PCH_SKYLAKE,
	PCH_APL,
	PCH_CANNONLAKE,
	PCH_TIGERLAKE,
	PCH_GEMINILAKE
};


struct pch_i2c_crs {
	uint16	i2c_addr;
	uint32	irq;
	uint8	irq_triggering;
	uint8	irq_polarity;
	uint8	irq_shareable;

	uint32	addr_bas;
	uint32	addr_len;
};


typedef enum {
	PCH_I2C_IRQ_LEGACY,
	PCH_I2C_IRQ_MSI,
	PCH_I2C_IRQ_MSI_X_SHARED
} pch_i2c_irq_type;


typedef struct pch_i2c_sim_info {
	phys_addr_t base_addr;
	uint64 map_size;
	uint32 irq;

	dk_node* node;
	dk_node* driver_node;

	// Set temporarily by scan_bus() before calling the per-backend
	// scan callback, so pch_i2c_scan_bus_callback can register
	// child device nodes on the bus manager's node.
	dk_node* busNode;

	pch_version version;

	area_id registersArea;
	addr_t registers;
	uint32 capabilities;

	uint16 ss_hcnt;
	uint16 ss_lcnt;
	uint16 fs_hcnt;
	uint16 fs_lcnt;
	uint16 hs_hcnt;
	uint16 hs_lcnt;
	uint32 sda_hold_time;

	uint8 tx_fifo_depth;
	uint8 rx_fifo_depth;

	uint32 masterConfig;

	// transfer
	int32	busy;
	bool	readwait;
	bool	writewait;
	i2c_op	op;
	void*	buffer;
	size_t	length;
	uint32	flags;
	int32	error;

	mutex	lock;
	status_t (*scan_bus)(struct pch_i2c_sim_info* bus, dk_node* busNode);
} pch_i2c_sim_info;


#endif // _PCH_I2C_H
