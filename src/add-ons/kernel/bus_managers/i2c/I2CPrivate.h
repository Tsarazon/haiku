/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef I2C_PRIVATE_H
#define I2C_PRIVATE_H


#include <new>
#include <stdio.h>
#include <string.h>

#include <lock.h>
#include <util/AutoLock.h>
#include <i2c.h>


//#define I2C_TRACE
#ifdef I2C_TRACE
#	define TRACE(x...)		dprintf("\33[33mi2c:\33[0m " x)
#else
#	define TRACE(x...)
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mi2c:\33[0m " x)
#define ERROR(x...)			TRACE_ALWAYS(x)
#define CALLED()			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


// ID generator name for I2C bus path IDs (passed to create_id/free_id).
#define I2C_PATHID_GENERATOR	"i2c/path_id"


class I2CBus;
class I2CDevice;

extern dk_keeper_info* gDeviceKeeper;
extern dk_device_ops gI2CBusRawOps;


class I2CDevice {
public:
								I2CDevice(dk_node* node, I2CBus* bus,
									i2c_addr slaveAddress);
								~I2CDevice();

			status_t			InitCheck();

			status_t			ExecCommand(i2c_op op,
									const void* cmdBuffer, size_t cmdLength,
									void* dataBuffer, size_t dataLength);
			status_t			AcquireBus();
			void				ReleaseBus();

private:
			dk_node*			fNode;
			I2CBus*				fBus;
			i2c_addr			fSlaveAddress;
};


class I2CBus {
public:
								I2CBus(dk_node* node);
								~I2CBus();

			status_t			InitCheck();

			status_t			ExecCommand(i2c_op op, i2c_addr slaveAddress,
									const void* cmdBuffer, size_t cmdLength,
									void* dataBuffer, size_t dataLength);
			status_t			Scan();
			status_t			AcquireBus();
			void				ReleaseBus();

			uint8				PathID() const { return fPathID; }

private:
			dk_node*			fNode;
			uint8				fPathID;
			i2c_sim_interface*	fController;
			void*				fControllerCookie;
};


#endif // I2C_PRIVATE_H
