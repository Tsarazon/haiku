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


/*
 * Memory alignment requirements per Intel PRM Vol 5/6 (Gen9-Gen12):
 * - GGTT translation table base: 4KB aligned
 * - GGTT pages: 4KB
 * - TileX/TileY/TileYf surfaces: 4KB aligned
 * - TileYs (64KB tiled resources): 64KB aligned
 * - Tiled Resources VA space: 64KB tiles
 */
#define INTEL_PAGE_SIZE					4096
#define INTEL_SURFACE_ALIGN_4K			4096
#define INTEL_SURFACE_ALIGN_64K			65536


static inline size_t
intel_round_to_alignment(size_t size, size_t alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}


void
intel_free_memory(addr_t base)
{
	if (base == 0)
		return;

	intel_free_graphics_memory freeMemory;
	freeMemory.magic = INTEL_PRIVATE_DATA_MAGIC;
	freeMemory.buffer_base = base;

	ioctl(gInfo->device, INTEL_FREE_GRAPHICS_MEMORY, &freeMemory,
		sizeof(freeMemory));
}


status_t
intel_allocate_memory(size_t size, uint32 flags, addr_t* base)
{
	if (size == 0 || base == NULL)
		return B_BAD_VALUE;

	intel_allocate_graphics_memory allocMemory;
	allocMemory.magic = INTEL_PRIVATE_DATA_MAGIC;
	allocMemory.size = intel_round_to_alignment(size, INTEL_PAGE_SIZE);
	allocMemory.alignment = INTEL_SURFACE_ALIGN_4K;
	allocMemory.flags = flags;

	if (ioctl(gInfo->device, INTEL_ALLOCATE_GRAPHICS_MEMORY, &allocMemory,
			sizeof(allocMemory)) < 0) {
		return errno;
	}

	*base = allocMemory.buffer_base;
	return B_OK;
}


status_t
intel_allocate_tiled_memory(size_t size, uint32 flags, addr_t* base,
	bool use64KBTiles)
{
	if (size == 0 || base == NULL)
		return B_BAD_VALUE;

	/*
	 * Per Intel PRM Vol 5:
	 * "Tiled surface base addresses must be tile aligned
	 * (64KB aligned for TileYS, 4KB aligned for all other tile modes)"
	 */
	size_t alignment = use64KBTiles
		? INTEL_SURFACE_ALIGN_64K
		: INTEL_SURFACE_ALIGN_4K;

	intel_allocate_graphics_memory allocMemory;
	allocMemory.magic = INTEL_PRIVATE_DATA_MAGIC;
	allocMemory.size = intel_round_to_alignment(size, alignment);
	allocMemory.alignment = alignment;
	allocMemory.flags = flags;

	if (ioctl(gInfo->device, INTEL_ALLOCATE_GRAPHICS_MEMORY, &allocMemory,
			sizeof(allocMemory)) < 0) {
		return errno;
	}

	*base = allocMemory.buffer_base;
	return B_OK;
}
