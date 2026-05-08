/*
 * Copyright 2009, Colin Günther, coling@gmx.de.
 * All Rights Reserved. Distributed under the terms of the MIT License.
 */


#include <sys/bus.h>


HAIKU_FBSD_WLAN_DRIVER_GLUE(marvell88w8335, malo, pci)
HAIKU_DRIVER_REQUIREMENTS(FBSD_WLAN);
HAIKU_FIRMWARE_VERSION(0);

NO_HAIKU_CHECK_DISABLE_INTERRUPTS();
NO_HAIKU_REENABLE_INTERRUPTS();
NO_HAIKU_FBSD_MII_DRIVER();
NO_HAIKU_FIRMWARE_NAME_MAP();
