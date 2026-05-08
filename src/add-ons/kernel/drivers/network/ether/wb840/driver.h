/* Copyright (c) 2003-2004
 * Stefano Ceccherini <burton666@libero.it>. All rights reserved.
 * This file is released under the MIT license
 */
#ifndef __DRIVER_H
#define __DRIVER_H

#include <device_keeper.h>
#include <Drivers.h>
#include <bus/PCI.h>
#include "ether_driver.h"

#define DEVICE_NAME "net/wb840"

extern pci_device_ops *gPci;
extern pci_device *gPciDev;
extern dk_keeper_info *gDeviceKeeper;
extern char* gDevNameList[];
extern pci_info *gDevList[];


#endif // __WB840_H
