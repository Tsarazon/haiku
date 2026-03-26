/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "FirmwareLoader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <device_keeper.h>
#include <KernelExport.h>


static const char* const kFirmwarePaths[] = {
	KOSM_FIRMWARE_PATH_SYSTEM,
	KOSM_FIRMWARE_PATH_DATA,
	NULL
};


static status_t
_try_load(const char* fullPath, const void** _data, size_t* _size)
{
	int fd = open(fullPath, O_RDONLY | O_NOTRAVERSE | O_CLOEXEC);
	if (fd < 0)
		return errno;

	struct stat st;
	if (fstat(fd, &st) != 0) {
		status_t error = errno;
		close(fd);
		return error;
	}

	if (st.st_size == 0 || st.st_size > 64 * 1024 * 1024) {
		close(fd);
		return B_BAD_DATA;
	}

	size_t size = (size_t)st.st_size;
	void* buffer = malloc(size);
	if (buffer == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	size_t bytesRead = 0;
	while (bytesRead < size) {
		ssize_t r = read(fd, (uint8*)buffer + bytesRead,
			size - bytesRead);
		if (r <= 0) {
			free(buffer);
			close(fd);
			return r < 0 ? errno : B_IO_ERROR;
		}
		bytesRead += r;
	}

	close(fd);

	*_data = buffer;
	*_size = size;
	return B_OK;
}


status_t
dk_load_firmware(const char* name, const void** _data, size_t* _size)
{
	if (name == NULL || _data == NULL || _size == NULL)
		return B_BAD_VALUE;

	char path[B_PATH_NAME_LENGTH];

	for (const char* const* base = kFirmwarePaths; *base != NULL; base++) {
		int written = snprintf(path, sizeof(path), "%s/%s", *base, name);
		if (written < 0 || (size_t)written >= sizeof(path))
			continue;

		status_t status = _try_load(path, _data, _size);
		if (status == B_OK) {
			dprintf("DeviceKeeper: loaded firmware %s (%zu bytes)\n",
				path, *_size);
			return B_OK;
		}
	}

	dprintf("DeviceKeeper: firmware not found: %s\n", name);
	return B_ENTRY_NOT_FOUND;
}


void
dk_release_firmware(const void* data)
{
	free(const_cast<void*>(data));
}
