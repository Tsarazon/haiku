/*
 * Copyright 2020, Jérôme Duval. All rights reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "I2CPrivate.h"

#include <kernel.h>
#include <StackOrHeapArray.h>


static status_t
i2c_bus_raw_open(void* _bus, const char* path, int openMode, void** _cookie)
{
	CALLED();
	*_cookie = _bus;
	return B_OK;
}


static status_t
i2c_bus_raw_close(void* cookie)
{
	return B_OK;
}


static status_t
i2c_bus_raw_free(void* cookie)
{
	return B_OK;
}


static status_t
i2c_bus_raw_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	*_length = 0;
	return B_UNSUPPORTED;
}


static status_t
i2c_bus_raw_write(void* cookie, off_t pos, const void* buffer,
	size_t* _length)
{
	*_length = 0;
	return B_UNSUPPORTED;
}


static status_t
i2c_bus_raw_control(void* _cookie, uint32 op, void* data, size_t length)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_cookie;

	switch (op) {
		case I2CEXEC:
		{
			i2c_ioctl_exec exec;
			if (user_memcpy(&exec, data, sizeof(exec)) != B_OK)
				return B_BAD_ADDRESS;

			if (exec.cmdBuffer == NULL)
				exec.cmdLength = 0;
			if (exec.buffer == NULL)
				exec.bufferLength = 0;

			if (exec.cmdLength > I2C_EXEC_MAX_BUFFER_LENGTH
				|| exec.bufferLength > I2C_EXEC_MAX_BUFFER_LENGTH)
				return B_BAD_VALUE;

			BStackOrHeapArray<uint8, 32> cmdBuffer(exec.cmdLength);
			BStackOrHeapArray<uint8, 32> buffer(exec.bufferLength);
			if (!cmdBuffer.IsValid() || !buffer.IsValid())
				return B_NO_MEMORY;

			void* userBuffer = exec.buffer;

			if (exec.cmdBuffer != NULL) {
				if (!IS_USER_ADDRESS(exec.cmdBuffer))
					return B_BAD_ADDRESS;
				if (user_memcpy(cmdBuffer, exec.cmdBuffer,
					exec.cmdLength) != B_OK)
					return B_BAD_ADDRESS;
				exec.cmdBuffer = cmdBuffer;
			}

			if (exec.buffer != NULL) {
				if (!IS_USER_ADDRESS(exec.buffer))
					return B_BAD_ADDRESS;
				if (IS_WRITE_OP(exec.op)) {
					if (user_memcpy(buffer, exec.buffer,
						exec.bufferLength) != B_OK)
						return B_BAD_ADDRESS;
				}
				exec.buffer = buffer;
			}

			status_t status = bus->AcquireBus();
			if (status != B_OK)
				return status;

			status = bus->ExecCommand(exec.op, exec.addr,
				exec.cmdBuffer, exec.cmdLength,
				exec.buffer, exec.bufferLength);

			bus->ReleaseBus();

			if (status != B_OK)
				return status;

			if (exec.buffer != NULL && IS_READ_OP(exec.op)) {
				if (user_memcpy(userBuffer, buffer,
					exec.bufferLength) != B_OK)
					return B_BAD_ADDRESS;
			}

			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


dk_device_ops gI2CBusRawOps = {
	i2c_bus_raw_open,
	i2c_bus_raw_close,
	i2c_bus_raw_free,
	i2c_bus_raw_read,
	i2c_bus_raw_write,
	NULL,			// io
	i2c_bus_raw_control,
	NULL,			// select
	NULL,			// deselect
	NULL,			// device_removed
};
