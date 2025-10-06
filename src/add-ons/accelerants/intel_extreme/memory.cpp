/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant.h"
#include "intel_extreme.h"

#include <errno.h>
#include <unistd.h>


#undef TRACE
//#define TRACE_MEMORY
#ifdef TRACE_MEMORY
#	define TRACE(x...) _sPrintf("intel_extreme accelerant:" x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme accelerant: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


void
intel_free_memory(addr_t base)
{
	// Validity check
	if (base == 0) {
		TRACE("%s: Ignoring request to free NULL address\n", __func__);
		return;
	}

	if (gInfo == NULL || gInfo->device < 0) {
		ERROR("%s: Invalid accelerant state (gInfo=%p, device=%d)\n",
			__func__, gInfo, gInfo ? gInfo->device : -1);
		return;
	}

	// Prepare data for ioctl
	intel_free_graphics_memory freeMemory;
	freeMemory.magic = INTEL_PRIVATE_DATA_MAGIC;
	freeMemory.buffer_base = base;

	// Call driver
	int result = ioctl(gInfo->device, INTEL_FREE_GRAPHICS_MEMORY,
		&freeMemory, sizeof(freeMemory));

	if (result < 0) {
		ERROR("%s: Failed to free memory at 0x%" B_PRIxADDR ": %s\n",
			__func__, base, strerror(errno));
	} else {
		TRACE("%s: Freed memory at 0x%" B_PRIxADDR "\n", __func__, base);
	}
}


status_t
intel_allocate_memory(size_t size, uint32 flags, addr_t &base)
{
	CALLED();

	// Initialize output parameter
	base = 0;

	// Parameter checks
	if (size == 0) {
		ERROR("%s: Invalid size 0\n", __func__);
		return B_BAD_VALUE;
	}

	if (gInfo == NULL || gInfo->device < 0) {
		ERROR("%s: Invalid accelerant state (gInfo=%p, device=%d)\n",
			__func__, gInfo, gInfo ? gInfo->device : -1);
		return B_NO_INIT;
	}

	// Validate flags
	const uint32 validFlags = B_APERTURE_NON_RESERVED | B_APERTURE_NEED_PHYSICAL;
	if (flags & ~validFlags) {
		ERROR("%s: Unknown flags 0x%x (masked)\n", __func__,
			flags & ~validFlags);
		flags &= validFlags;
	}

	// Prepare request
	intel_allocate_graphics_memory allocMemory;
	allocMemory.magic = INTEL_PRIVATE_DATA_MAGIC;
	allocMemory.size = size;
	allocMemory.alignment = 0;  // Could add as function parameter
	allocMemory.flags = flags;

	TRACE("%s: Requesting %zu bytes with flags 0x%x\n",
		__func__, size, flags);

	// Call driver
	int result = ioctl(gInfo->device, INTEL_ALLOCATE_GRAPHICS_MEMORY,
		&allocMemory, sizeof(allocMemory));

	if (result < 0) {
		status_t status = errno;
		ERROR("%s: Allocation failed: %s (size=%zu, flags=0x%x)\n",
			__func__, strerror(status), size, flags);
		return status;
	}

	// Verify result
	if (allocMemory.buffer_base == 0) {
		ERROR("%s: Driver returned NULL address despite success\n", __func__);
		return B_ERROR;
	}

	base = allocMemory.buffer_base;

	TRACE("%s: Allocated %zu bytes at 0x%" B_PRIxADDR "\n",
		__func__, size, base);

	return B_OK;
}

