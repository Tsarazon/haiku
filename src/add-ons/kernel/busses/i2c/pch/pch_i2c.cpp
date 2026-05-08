/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ACPI.h>
#include <ByteOrder.h>
#include <condition_variable.h>
#include <bus/PCI.h>


#include "pch_i2c.h"


dk_keeper_info* gDeviceKeeper;
acpi_module_info* gACPI;


static void
enable_device(pch_i2c_sim_info* bus, bool enable)
{
	uint32 status = enable ? 1 : 0;
	for (int tries = 100; tries >= 0; tries--) {
		write32(bus->registers + PCH_IC_ENABLE, status);
		if ((read32(bus->registers + PCH_IC_ENABLE_STATUS) & 1) == status)
			return;
		snooze(25);
	}

	ERROR("enable_device failed\n");
}


static int32
pch_i2c_interrupt_handler(pch_i2c_sim_info* bus)
{
	int32 handled = B_HANDLED_INTERRUPT;

	// Check if this interrupt is ours
	uint32 enable = read32(bus->registers + PCH_IC_ENABLE);
	if (enable == 0)
		return B_UNHANDLED_INTERRUPT;

	uint32 status = read32(bus->registers + PCH_IC_INTR_STAT);
	if ((status & PCH_IC_INTR_STAT_RX_UNDER) != 0)
		write32(bus->registers + PCH_IC_CLR_RX_UNDER, 0);
	if ((status & PCH_IC_INTR_STAT_RX_OVER) != 0)
		write32(bus->registers + PCH_IC_CLR_RX_OVER, 0);
	if ((status & PCH_IC_INTR_STAT_TX_OVER) != 0)
		write32(bus->registers + PCH_IC_CLR_TX_OVER, 0);
	if ((status & PCH_IC_INTR_STAT_RD_REQ) != 0)
		write32(bus->registers + PCH_IC_CLR_RD_REQ, 0);
	if ((status & PCH_IC_INTR_STAT_TX_ABRT) != 0)
		write32(bus->registers + PCH_IC_CLR_TX_ABRT, 0);
	if ((status & PCH_IC_INTR_STAT_RX_DONE) != 0)
		write32(bus->registers + PCH_IC_CLR_RX_DONE, 0);
	if ((status & PCH_IC_INTR_STAT_ACTIVITY) != 0)
		write32(bus->registers + PCH_IC_CLR_ACTIVITY, 0);
	if ((status & PCH_IC_INTR_STAT_STOP_DET) != 0)
		write32(bus->registers + PCH_IC_CLR_STOP_DET, 0);
	if ((status & PCH_IC_INTR_STAT_START_DET) != 0)
		write32(bus->registers + PCH_IC_CLR_START_DET, 0);
	if ((status & PCH_IC_INTR_STAT_GEN_CALL) != 0)
		write32(bus->registers + PCH_IC_CLR_GEN_CALL, 0);

	TRACE("pch_i2c_interrupt_handler %" B_PRIx32 "\n", status);

	if ((status & ~PCH_IC_INTR_STAT_ACTIVITY) == 0)
		return handled;

	if ((status & PCH_IC_INTR_STAT_RX_FULL) != 0)
		ConditionVariable::NotifyAll(&bus->readwait, B_OK);
	if ((status & PCH_IC_INTR_STAT_TX_EMPTY) != 0)
		ConditionVariable::NotifyAll(&bus->writewait, B_OK);
	if ((status & PCH_IC_INTR_STAT_STOP_DET) != 0) {
		bus->busy = 0;
		ConditionVariable::NotifyAll(&bus->busy, B_OK);
	}

	return handled;
}


//	#pragma mark - i2c_sim_interface implementation


static status_t
exec_command(i2c_bus_cookie cookie, i2c_op op, i2c_addr slaveAddress,
	const void *cmdBuffer, size_t cmdLength, void* dataBuffer,
	size_t dataLength)
{
	CALLED();
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)cookie;

	if (atomic_test_and_set(&bus->busy, 1, 0) != 0)
		return B_BUSY;

	TRACE("exec_command: acquired busy flag\n");

	uint32 status = 0;
	for (int tries = 100; tries >= 0; tries--) {
		status = read32(bus->registers + PCH_IC_STATUS);
		if ((status & PCH_IC_STATUS_ACTIVITY) == 0)
			break;
		snooze(1000);
	}

	if ((status & PCH_IC_STATUS_ACTIVITY) != 0) {
		bus->busy = 0;
		return B_BUSY;
	}

	TRACE("exec_command: write slave address\n");

	enable_device(bus, false);
	write32(bus->registers + PCH_IC_CON,
		read32(bus->registers + PCH_IC_CON) & ~PCH_IC_CON_10BIT_ADDR_MASTER);
	write32(bus->registers + PCH_IC_TAR, slaveAddress);

	write32(bus->registers + PCH_IC_INTR_MASK, 0);
	read32(bus->registers + PCH_IC_CLR_INTR);

	enable_device(bus, true);

	read32(bus->registers + PCH_IC_CLR_INTR);
	write32(bus->registers + PCH_IC_INTR_MASK, PCH_IC_INTR_STAT_TX_EMPTY);

	if (cmdLength > 0) {
		TRACE("exec_command: write command buffer\n");
		uint16 txLimit = bus->tx_fifo_depth
			- read32(bus->registers + PCH_IC_TXFLR);
		if (cmdLength > txLimit) {
			ERROR("exec_command can't write, cmd too long %" B_PRIuSIZE
				" (max %d)\n", cmdLength, txLimit);
			bus->busy = 0;
			return B_BAD_VALUE;
		}

		uint8* buffer = (uint8*)cmdBuffer;
		for (size_t i = 0; i < cmdLength; i++) {
			uint32 cmd = buffer[i];
			if (i == cmdLength - 1 && dataLength == 0 && IS_STOP_OP(op))
				cmd |= PCH_IC_DATA_CMD_STOP;
			write32(bus->registers + PCH_IC_DATA_CMD, cmd);
		}
	}

	TRACE("exec_command: processing buffer %" B_PRIuSIZE " bytes\n",
		dataLength);
	uint16 txLimit = bus->tx_fifo_depth
		- read32(bus->registers + PCH_IC_TXFLR);
	uint8* buffer = (uint8*)dataBuffer;
	size_t readPos = 0;
	size_t i = 0;
	while (i < dataLength) {
		uint32 cmd = PCH_IC_DATA_CMD_READ;
		if (IS_WRITE_OP(op))
			cmd = buffer[i];

		if (i == 0 && cmdLength > 0 && IS_READ_OP(op))
			cmd |= PCH_IC_DATA_CMD_RESTART;

		if (i == (dataLength - 1) && IS_STOP_OP(op))
			cmd |= PCH_IC_DATA_CMD_STOP;

		write32(bus->registers + PCH_IC_DATA_CMD, cmd);

		if (IS_READ_OP(op) && IS_BLOCK_OP(op) && readPos == 0)
			txLimit = 1;
		txLimit--;
		i++;

		while (IS_READ_OP(op) && (txLimit == 0 || i == dataLength)) {
			write32(bus->registers + PCH_IC_INTR_MASK,
				PCH_IC_INTR_STAT_RX_FULL);

			struct ConditionVariable condition;
			condition.Publish(&bus->readwait, "pch_i2c");
			ConditionVariableEntry variableEntry;
			status_t sts = variableEntry.Wait(&bus->readwait,
				B_RELATIVE_TIMEOUT, 500000L);
			condition.Unpublish();
			if (sts != B_OK)
				ERROR("exec_command timed out waiting for read\n");
			uint32 rxBytes = read32(bus->registers + PCH_IC_RXFLR);
			if (rxBytes == 0) {
				ERROR("exec_command timed out reading %" B_PRIuSIZE
					" bytes\n", dataLength - readPos);
				bus->busy = 0;
				return B_ERROR;
			}
			for (; rxBytes > 0; rxBytes--) {
				uint32 read = read32(bus->registers + PCH_IC_DATA_CMD);
				if (readPos < dataLength)
					buffer[readPos++] = read;
			}

			if (IS_BLOCK_OP(op) && readPos > 0 && dataLength > buffer[0])
				dataLength = buffer[0] + 1;
			if (readPos >= dataLength)
				break;

			TRACE("exec_command %" B_PRIuSIZE " bytes to be read\n",
				dataLength - readPos);
			txLimit = bus->tx_fifo_depth
				- read32(bus->registers + PCH_IC_TXFLR);
		}
	}

	status_t err = B_OK;
	if (IS_STOP_OP(op) && IS_WRITE_OP(op)) {
		TRACE("exec_command: waiting busy condition\n");
		while (bus->busy == 1) {
			write32(bus->registers + PCH_IC_INTR_MASK,
				PCH_IC_INTR_STAT_STOP_DET);

			struct ConditionVariable condition;
			condition.Publish(&bus->busy, "pch_i2c");
			ConditionVariableEntry variableEntry;
			err = variableEntry.Wait(&bus->busy, B_RELATIVE_TIMEOUT,
				500000L);
			condition.Unpublish();
			if (err != B_OK)
				ERROR("exec_command timed out waiting for busy\n");
		}
	}
	TRACE("exec_command: processing done\n");

	bus->busy = 0;

	return err;
}


static acpi_status
pch_i2c_scan_parse_callback(ACPI_RESOURCE *res, void *context)
{
	struct pch_i2c_crs* crs = (struct pch_i2c_crs*)context;

	if (res->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
	    res->Data.CommonSerialBus.Type == ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		crs->i2c_addr = B_LENDIAN_TO_HOST_INT16(
			res->Data.I2cSerialBus.SlaveAddress);
		return AE_CTRL_TERMINATE;
	} else if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupts[0];
		crs->irq_triggering = res->Data.Irq.Triggering;
		crs->irq_polarity = res->Data.Irq.Polarity;
		crs->irq_shareable = res->Data.Irq.Shareable;
	} else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		crs->irq = res->Data.ExtendedIrq.Interrupts[0];
		crs->irq_triggering = res->Data.ExtendedIrq.Triggering;
		crs->irq_polarity = res->Data.ExtendedIrq.Polarity;
		crs->irq_shareable = res->Data.ExtendedIrq.Shareable;
	}

	return B_OK;
}


static status_t
acpi_GetInteger(acpi_handle acpiCookie,
	const char* path, int64* number)
{
	acpi_data buf;
	acpi_object_type object;
	buf.pointer = &object;
	buf.length = sizeof(acpi_object_type);

	status_t status = gACPI->evaluate_method(acpiCookie, path, NULL, &buf);
	if (status == B_OK) {
		if (object.object_type == ACPI_TYPE_INTEGER)
			*number = object.integer.integer;
		else
			status = B_BAD_VALUE;
	}
	return status;
}


acpi_status
pch_i2c_scan_bus_callback(acpi_handle object, uint32 nestingLevel,
	void *context, void** returnValue)
{
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)context;
	TRACE("pch_i2c_scan_bus_callback %p\n", object);

	// skip absent devices
	int64 sta;
	status_t status = acpi_GetInteger(object, "_STA", &sta);
	if (status == B_OK && (sta & ACPI_STA_DEVICE_PRESENT) == 0)
		return B_OK;

	// Attach devices for I2C resources
	struct pch_i2c_crs crs;
	memset(&crs, 0, sizeof(crs));
	status = gACPI->walk_resources(object, (ACPI_STRING)"_CRS",
		pch_i2c_scan_parse_callback, &crs);
	if (status != B_OK) {
		ERROR("Error while getting I2C devices\n");
		return status;
	}

	TRACE("pch_i2c_scan_bus_callback deviceAddress %x\n", crs.i2c_addr);

	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;
	status = gACPI->ns_handle_to_pathname(object, &buffer);
	if (status != B_OK) {
		ERROR("pch_i2c_scan_bus_callback ns_handle_to_pathname failed\n");
		return status;
	}

	char* hid = NULL;
	char* cidList[8] = {};
	status = gACPI->get_device_info((const char*)buffer.pointer, &hid,
		cidList, 8, NULL, NULL);
	if (status != B_OK) {
		ERROR("pch_i2c_scan_bus_callback get_device_info failed\n");
		free(buffer.pointer);
		return status;
	}

	// Count CID entries for stringlist
	uint32 cidCount = 0;
	for (int i = 0; cidList[i] != NULL && i < 8; i++)
		cidCount++;

	// Build properties and register device node on the bus manager's
	// node directly (replaces old gI2c->register_device path).
	dk_property attrs[8] = {
		{ KOSM_LABEL, B_STRING_TYPE,
			{ .string = "I2C Device" } },
		{ KOSM_I2C_ADDRESS, B_UINT16_TYPE,
			{ .ui16 = crs.i2c_addr } },
		{ KOSM_DEVICE_BUS, B_STRING_TYPE,
			{ .string = "i2c" } },
		{ KOSM_DEVICE_FLAGS, B_UINT32_TYPE,
			{ .ui32 = KOSM_FIND_MULTIPLE_CHILDREN } },
		{}
	};
	uint32 attrCount = 4;

	if (hid != NULL) {
		attrs[attrCount].name = KOSM_ACPI_DEVICE_HID;
		attrs[attrCount].type = B_STRING_TYPE;
		attrs[attrCount].value.string = hid;
		attrCount++;
	}

	if (cidCount > 0) {
		attrs[attrCount].name = KOSM_DEVICE_COMPATIBLE;
		attrs[attrCount].type = KOSM_STRINGLIST_TYPE;
		attrs[attrCount].value.stringlist.items
			= (const char* const*)cidList;
		attrs[attrCount].value.stringlist.count = cidCount;
		attrCount++;
	}

	// NULL-terminate
	attrs[attrCount].name = NULL;

	status = gDeviceKeeper->register_node(bus->busNode,
		I2C_DEVICE_MODULE_NAME, attrs, NULL, NULL);

	free(hid);
	for (int i = 0; cidList[i] != NULL; i++)
		free(cidList[i]);
	free(buffer.pointer);

	TRACE("pch_i2c_scan_bus_callback registered device: %s\n",
		strerror(status));

	return status;
}


static status_t
scan_bus(i2c_bus_cookie cookie, dk_node* busNode)
{
	CALLED();
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)cookie;
	bus->busNode = busNode;
	if (bus->scan_bus != NULL)
		return bus->scan_bus(bus, busNode);
	return B_OK;
}


static status_t
acquire_bus(i2c_bus_cookie cookie)
{
	CALLED();
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)cookie;
	return mutex_lock(&bus->lock);
}


static void
release_bus(i2c_bus_cookie cookie)
{
	CALLED();
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)cookie;
	mutex_unlock(&bus->lock);
}


//	#pragma mark - dk_driver_info for controller


static status_t
init_bus(dk_node* node, void** bus_cookie)
{
	CALLED();
	status_t status = B_OK;

	// The parent node is a pch_i2c_{pci,acpi} root driver that stashed a
	// fully-initialised pch_i2c_sim_info* as its driver cookie.
	dk_driver_info* driver;
	pch_i2c_sim_info* bus;
	dk_node* parent = gDeviceKeeper->get_parent_node(node);
	if (parent == NULL)
		return B_NO_INIT;
	status = gDeviceKeeper->get_node_driver(parent, &driver,
		(void**)&bus);
	gDeviceKeeper->put_node(parent);
	if (status != B_OK)
		return status;

	TRACE_ALWAYS("init_bus() addr 0x%" B_PRIxPHYSADDR " size 0x%" B_PRIx64
		" irq 0x%" B_PRIx32 "\n", bus->base_addr, bus->map_size, bus->irq);

	bus->registersArea = map_physical_memory("PCHI2C memory mapped registers",
		bus->base_addr, bus->map_size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void **)&bus->registers);

	uint32 version = read32(bus->registers + PCH_IC_COMP_VERSION);
	TRACE_ALWAYS("version 0x%" B_PRIx32 "\n", version);

	if (bus->version >= PCH_SKYLAKE) {
		bus->capabilities = read32(bus->registers + PCH_SUP_CAPABLITIES);
		TRACE_ALWAYS("init_bus() 0x%" B_PRIx32 " (0x%" B_PRIx32 ")\n",
			(bus->capabilities >> PCH_SUP_CAPABLITIES_TYPE_SHIFT)
				& PCH_SUP_CAPABLITIES_TYPE_MASK,
			bus->capabilities);
		if (((bus->capabilities >> PCH_SUP_CAPABLITIES_TYPE_SHIFT)
				& PCH_SUP_CAPABLITIES_TYPE_MASK) != 0) {
			status = B_ERROR;
			ERROR("init_bus() device type not supported\n");
			goto err;
		}

		write32(bus->registers + PCH_SUP_RESETS, 0);
		write32(bus->registers + PCH_SUP_RESETS,
			PCH_SUP_RESETS_FUNC | PCH_SUP_RESETS_IDMA);
	}

	if (bus->ss_hcnt == 0)
		bus->ss_hcnt = read32(bus->registers + PCH_IC_SS_SCL_HCNT);
	if (bus->ss_lcnt == 0)
		bus->ss_lcnt = read32(bus->registers + PCH_IC_SS_SCL_LCNT);
	if (bus->fs_hcnt == 0)
		bus->fs_hcnt = read32(bus->registers + PCH_IC_FS_SCL_HCNT);
	if (bus->fs_lcnt == 0)
		bus->fs_lcnt = read32(bus->registers + PCH_IC_FS_SCL_LCNT);
	if (bus->sda_hold_time == 0)
		bus->sda_hold_time = read32(bus->registers + PCH_IC_SDA_HOLD);
	TRACE_ALWAYS("init_bus() 0x%04" B_PRIx16 " 0x%04" B_PRIx16
		" 0x%04" B_PRIx16 " 0x%04" B_PRIx16 " 0x%08" B_PRIx32 "\n",
		bus->ss_hcnt, bus->ss_lcnt, bus->fs_hcnt, bus->fs_lcnt,
		bus->sda_hold_time);

	enable_device(bus, false);

	write32(bus->registers + PCH_IC_SS_SCL_HCNT, bus->ss_hcnt);
	write32(bus->registers + PCH_IC_SS_SCL_LCNT, bus->ss_lcnt);
	write32(bus->registers + PCH_IC_FS_SCL_HCNT, bus->fs_hcnt);
	write32(bus->registers + PCH_IC_FS_SCL_LCNT, bus->fs_lcnt);
	if (bus->hs_hcnt > 0)
		write32(bus->registers + PCH_IC_HS_SCL_HCNT, bus->hs_hcnt);
	if (bus->hs_lcnt > 0)
		write32(bus->registers + PCH_IC_HS_SCL_LCNT, bus->hs_lcnt);
	{
		uint32 reg = read32(bus->registers + PCH_IC_COMP_VERSION);
		if (reg >= PCH_IC_COMP_VERSION_MIN)
			write32(bus->registers + PCH_IC_SDA_HOLD, bus->sda_hold_time);
	}

	{
		bus->tx_fifo_depth = 32;
		bus->rx_fifo_depth = 32;
		uint32 reg = read32(bus->registers + PCH_IC_COMP_PARAM1);
		uint32 rx_fifo_depth = PCH_IC_COMP_PARAM1_RX(reg);
		uint32 tx_fifo_depth = PCH_IC_COMP_PARAM1_TX(reg);
		if (rx_fifo_depth > 1 && rx_fifo_depth < bus->rx_fifo_depth)
			bus->rx_fifo_depth = rx_fifo_depth;
		if (tx_fifo_depth > 1 && tx_fifo_depth < bus->tx_fifo_depth)
			bus->tx_fifo_depth = tx_fifo_depth;
		write32(bus->registers + PCH_IC_RX_TL, 0);
		write32(bus->registers + PCH_IC_TX_TL, bus->tx_fifo_depth / 2);
	}

	bus->masterConfig = PCH_IC_CON_MASTER | PCH_IC_CON_SLAVE_DISABLE |
	    PCH_IC_CON_RESTART_EN | PCH_IC_CON_SPEED_FAST;
	write32(bus->registers + PCH_IC_CON, bus->masterConfig);

	write32(bus->registers + PCH_IC_INTR_MASK, 0);
	read32(bus->registers + PCH_IC_CLR_INTR);

	status = install_io_interrupt_handler(bus->irq,
		(interrupt_handler)pch_i2c_interrupt_handler, bus, 0);
	if (status != B_OK) {
		ERROR("install interrupt handler failed\n");
		goto err;
	}

	mutex_init(&bus->lock, "pch_i2c");

	// Publish the I2C SIM interface on the controller node. The I2C bus
	// manager is already attached here (auto-attach by module name from
	// the root pch_i2c_{pci,acpi} driver's register_node), and retrieves
	// this interface via get_interface(I2C_SIM_INTERFACE_NAME, ANCESTORS).
	extern i2c_sim_interface gPchI2cSimInterface;
	status = gDeviceKeeper->publish_interface(node,
		I2C_SIM_INTERFACE_NAME, &gPchI2cSimInterface);
	if (status != B_OK) {
		ERROR("publish_interface(i2c sim) failed: %s\n", strerror(status));
		mutex_destroy(&bus->lock);
		remove_io_interrupt_handler(bus->irq,
			(interrupt_handler)pch_i2c_interrupt_handler, bus);
		goto err;
	}

	*bus_cookie = bus;
	return B_OK;

err:
	if (bus->registersArea >= 0) {
		delete_area(bus->registersArea);
		bus->registersArea = -1;
	}
	return status;
}


static void
uninit_bus(void* bus_cookie)
{
	pch_i2c_sim_info* bus = (pch_i2c_sim_info*)bus_cookie;

	remove_io_interrupt_handler(bus->irq,
		(interrupt_handler)pch_i2c_interrupt_handler, bus);
	mutex_destroy(&bus->lock);
	if (bus->registersArea >= 0) {
		delete_area(bus->registersArea);
		bus->registersArea = -1;
	}
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_ACPI_MODULE_NAME, (module_info**)&gACPI },
	{}
};


// Published on the controller dk_node in init_bus; the i2c_bus_cookie
// passed to every callback is the pch_i2c_sim_info* set by init_bus.
i2c_sim_interface gPchI2cSimInterface = {
	.set_i2c_bus				= NULL,
	.exec_command				= exec_command,
	.scan_bus					= scan_bus,
	.acquire_bus				= acquire_bus,
	.release_bus				= release_bus,
	.install_interrupt_handler	= NULL,
	.uninstall_interrupt_handler = NULL,
};

static dk_driver_info sPchI2cControllerDriver = {
	.info = {
		PCH_I2C_SIM_MODULE_NAME,
		0,
		NULL
	},
	// No .match: auto-attached by module name when pch_i2c_{pci,acpi}
	// root driver registers a child node with PCH_I2C_SIM_MODULE_NAME.
	.attach	= init_bus,
	.detach	= uninit_bus,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


module_info* modules[] = {
	(module_info*)&gPchI2cAcpiDevice,
	(module_info*)&gPchI2cPciDevice,
	(module_info*)&sPchI2cControllerDriver,
	NULL
};
