/* Copyright (c) 2003-2004 
 * Stefano Ceccherini <burton666@libero.it>. All rights reserved.
 * This file is released under the MIT license
 */
 
#ifndef __WB_DRIVER_H
#define __WB_DRIVER_H

// PCI Communications
#include <bus/PCI.h>

#define IO_PORT_PCI_ACCESS true
//#define MEMORY_MAPPED_PCI_ACCESS true

#if IO_PORT_PCI_ACCESS
#	define write8(address,value)		gPci->write_io_8(gPciDev,(address),(value))
#	define write16(address,value)		gPci->write_io_16(gPciDev,(address),(value))
#	define write32(address,value)		gPci->write_io_32(gPciDev,(address),(value))
#	define read8(address)				gPci->read_io_8(gPciDev,(address))
#	define read16(address)				gPci->read_io_16(gPciDev,(address))
#	define read32(address)				gPci->read_io_32(gPciDev,(address))
#else	/* MEMORY_MAPPED_PCI_ACCESS */
#	define read8(address)   			(*((volatile uint8*)(address)))
#	define read16(address)  			(*((volatile uint16*)(address)))
#	define read32(address) 			(*((volatile uint32*)(address)))
#	define write8(address,data)  		(*((volatile uint8 *)(address))  = data)
#	define write16(address,data) 		(*((volatile uint16 *)(address)) = (data))
#	define write32(address,data) 		(*((volatile uint32 *)(address)) = (data))
#endif

extern pci_device_ops* gPci;
extern pci_device* gPciDev;

/* Device hooks (used by dk_device_ops in driver.c) */
status_t wb840_open(void* info, const char* name, int flags, void** cookie);
status_t wb840_close(void* cookie);
status_t wb840_free(void* cookie);
status_t wb840_read(void* cookie, off_t position, void* buf, size_t* num_bytes);
status_t wb840_write(void* cookie, off_t position, const void* buffer, size_t* num_bytes);
status_t wb840_control(void* cookie, uint32 op, void* arg, size_t len);

#endif
