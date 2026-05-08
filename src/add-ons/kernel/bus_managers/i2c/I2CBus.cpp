/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "I2CPrivate.h"


I2CBus::I2CBus(dk_node* node)
	:
	fNode(node),
	fPathID(0),
	fController(NULL),
	fControllerCookie(NULL)
{
	CALLED();

	// get controller ops from parent (controller node)
	status_t status = gDeviceKeeper->get_interface(node,
		I2C_SIM_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&fController, &fControllerCookie);
	if (status != B_OK) {
		ERROR("I2CBus: get_interface(i2c sim) failed: %s\n",
			strerror(status));
		return;
	}

	// allocate unique path ID
	int32 id = gDeviceKeeper->create_id(I2C_PATHID_GENERATOR);
	if (id < 0) {
		ERROR("I2CBus: out of path IDs\n");
		fController = NULL;
		return;
	}
	fPathID = (uint8)id;
}


I2CBus::~I2CBus()
{
	if (fPathID != 0 || fController != NULL)
		gDeviceKeeper->free_id(I2C_PATHID_GENERATOR, fPathID);
}


status_t
I2CBus::InitCheck()
{
	if (fController == NULL)
		return B_NO_INIT;
	return B_OK;
}


status_t
I2CBus::ExecCommand(i2c_op op, i2c_addr slaveAddress,
	const void* cmdBuffer, size_t cmdLength,
	void* dataBuffer, size_t dataLength)
{
	CALLED();
	return fController->exec_command(fControllerCookie, op,
		slaveAddress, cmdBuffer, cmdLength, dataBuffer, dataLength);
}


status_t
I2CBus::Scan()
{
	CALLED();
	if (fController->scan_bus == NULL)
		return B_OK;
	return fController->scan_bus(fControllerCookie, fNode);
}


status_t
I2CBus::AcquireBus()
{
	CALLED();
	if (fController->acquire_bus == NULL)
		return B_OK;
	return fController->acquire_bus(fControllerCookie);
}


void
I2CBus::ReleaseBus()
{
	CALLED();
	if (fController->release_bus != NULL)
		fController->release_bus(fControllerCookie);
}


// dk_driver_info callbacks

static status_t
i2c_bus_attach(dk_node* node, void** _cookie)
{
	CALLED();

	I2CBus* bus = new(std::nothrow) I2CBus(node);
	if (bus == NULL)
		return B_NO_MEMORY;

	status_t status = bus->InitCheck();
	if (status != B_OK) {
		ERROR("I2CBus: init failed: %s\n", strerror(status));
		delete bus;
		return status;
	}

	// publish raw devfs entry for i2c diag tool
	char name[B_PATH_NAME_LENGTH];
	snprintf(name, sizeof(name), "bus/i2c/%d/bus_raw", bus->PathID());

	status = gDeviceKeeper->publish_device(node, name, &gI2CBusRawOps);
	if (status != B_OK) {
		ERROR("I2CBus: publish_device(%s) failed: %s\n",
			name, strerror(status));
		delete bus;
		return status;
	}

	*_cookie = bus;

	// scan for devices: controller registers child nodes on our
	// node via dk_keeper_info::register_node() with module name
	// I2C_DEVICE_MODULE_NAME and KOSM_I2C_ADDRESS properties.
	bus->Scan();

	return B_OK;
}


static void
i2c_bus_detach(void* _cookie)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_cookie;
	delete bus;
}


static status_t
i2c_bus_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}


dk_driver_info gI2CBusDriver = {
	.info = {
		I2C_BUS_MODULE_NAME,
		0,
		i2c_bus_std_ops
	},
	// No .match: auto-attached by module name when I2C controller
	// registers a child node with I2C_BUS_MODULE_NAME.
	.attach	= i2c_bus_attach,
	.detach	= i2c_bus_detach,
	// No .ops: the raw bus device is published separately through
	// gI2CBusRawOps inside i2c_bus_attach.
};
