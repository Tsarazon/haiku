/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Tests for inter-process surface sharing via area cloning.
 */

#include <KosmSurface.hpp>
#include <KosmSurfaceAllocator.hpp>
#include <Message.h>
#include <OS.h>

#include <stdio.h>
#include <string.h>

#include "SurfaceRegistry.hpp"


static int sTestsPassed = 0;
static int sTestsFailed = 0;


#define TEST(name) \
	static void test_##name()

#define RUN_TEST(name) \
	do { \
		printf("Running %s...", #name); \
		test_##name(); \
		printf(" passed\n"); \
		sTestsPassed++; \
	} catch (...) { \
		printf(" FAILED\n"); \
		sTestsFailed++; \
	}

#define ASSERT(cond) \
	do { \
		if (!(cond)) { \
			printf("\n  ASSERTION FAILED: %s (line %d)\n", #cond, __LINE__); \
			throw 1; \
		} \
	} while (0)

#define ASSERT_EQ(a, b) \
	do { \
		if ((a) != (b)) { \
			printf("\n  ASSERTION FAILED: %s != %s (line %d)\n", #a, #b, __LINE__); \
			throw 1; \
		} \
	} while (0)


TEST(area_cloneable)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 128;
	desc.height = 128;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);

	// Write a pattern to the surface
	surface->Lock();
	uint32* data = (uint32*)surface->BaseAddress();
	for (uint32 i = 0; i < 128 * 128; i++) {
		data[i] = 0xDEADBEEF;
	}
	surface->Unlock();

	// Clone the area (simulating IPC)
	area_id sourceArea = surface->Area();
	void* clonedAddress = NULL;
	area_id clonedArea = clone_area("cloned_surface", &clonedAddress,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);

	ASSERT(clonedArea >= 0);
	ASSERT(clonedAddress != NULL);

	// Verify the cloned area contains our data
	uint32* clonedData = (uint32*)clonedAddress;
	for (uint32 i = 0; i < 128 * 128; i++) {
		ASSERT_EQ(clonedData[i], 0xDEADBEEFu);
	}

	// Modify through clone
	clonedData[0] = 0xCAFEBABE;

	// Verify original sees the change
	surface->Lock();
	data = (uint32*)surface->BaseAddress();
	ASSERT_EQ(data[0], 0xCAFEBABEu);
	surface->Unlock();

	delete_area(clonedArea);
	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(registry_lookup_area)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 64;
	desc.height = 64;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);

	surface_id id = surface->ID();
	area_id expectedArea = surface->Area();

	// Registry should have the area
	area_id foundArea = -1;
	status = SurfaceRegistry::Default()->LookupArea(id, &foundArea);
	ASSERT_EQ(status, B_OK);
	ASSERT_EQ(foundArea, expectedArea);

	KosmSurfaceAllocator::Default()->Free(surface);

	// After free, lookup should fail
	status = SurfaceRegistry::Default()->LookupArea(id, &foundArea);
	ASSERT(status != B_OK);
}


TEST(global_use_count)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 32;
	desc.height = 32;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	surface_id id = surface->ID();

	// Initial global use count should be 1 (from registration)
	int32 count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 1);

	// Increment from "another process"
	SurfaceRegistry::Default()->IncrementGlobalUseCount(id);
	count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 2);

	// Decrement
	SurfaceRegistry::Default()->DecrementGlobalUseCount(id);
	count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 1);

	// Decrement to 0
	SurfaceRegistry::Default()->DecrementGlobalUseCount(id);
	count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 0);

	// Surface is no longer in use
	ASSERT(!SurfaceRegistry::Default()->IsInUse(id));

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(serialize_surface_info)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 800;
	desc.height = 600;
	desc.format = PIXEL_FORMAT_BGRA8888;
	desc.usage = SURFACE_USAGE_CPU_WRITE | SURFACE_USAGE_COMPOSITOR;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	// Serialize surface info to BMessage (simulating IPC)
	BMessage msg;
	msg.AddInt32("surface_id", surface->ID());
	msg.AddInt32("area_id", surface->Area());
	msg.AddInt32("width", surface->Width());
	msg.AddInt32("height", surface->Height());
	msg.AddInt32("format", surface->Format());
	msg.AddInt32("bytes_per_row", surface->BytesPerRow());
	msg.AddInt32("bytes_per_element", surface->BytesPerElement());
	msg.AddInt64("alloc_size", surface->AllocSize());

	// Deserialize (simulating receiver)
	int32 id, areaId, width, height, format, bpr, bpe;
	int64 allocSize;

	msg.FindInt32("surface_id", &id);
	msg.FindInt32("area_id", &areaId);
	msg.FindInt32("width", &width);
	msg.FindInt32("height", &height);
	msg.FindInt32("format", &format);
	msg.FindInt32("bytes_per_row", &bpr);
	msg.FindInt32("bytes_per_element", &bpe);
	msg.FindInt64("alloc_size", &allocSize);

	ASSERT_EQ((uint32)width, 800u);
	ASSERT_EQ((uint32)height, 600u);
	ASSERT_EQ((pixel_format)format, PIXEL_FORMAT_BGRA8888);
	ASSERT_EQ((uint32)bpe, 4u);
	ASSERT(bpr >= (int32)(800 * 4));  // At least width * bytesPerElement
	ASSERT(allocSize > 0);

	// Clone the area
	void* clonedAddress = NULL;
	area_id clonedArea = clone_area("imported_surface", &clonedAddress,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, areaId);
	ASSERT(clonedArea >= 0);

	// Increment global use count (as receiver would)
	SurfaceRegistry::Default()->IncrementGlobalUseCount(id);
	ASSERT(SurfaceRegistry::Default()->IsInUse(id));

	// Clean up receiver side
	SurfaceRegistry::Default()->DecrementGlobalUseCount(id);
	delete_area(clonedArea);

	// Clean up sender side
	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(concurrent_use_counts)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 16;
	desc.height = 16;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	surface_id id = surface->ID();

	// Simulate multiple processes incrementing
	for (int i = 0; i < 100; i++) {
		SurfaceRegistry::Default()->IncrementGlobalUseCount(id);
	}

	int32 count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 101);  // 1 initial + 100 increments

	// Simulate multiple processes decrementing
	for (int i = 0; i < 100; i++) {
		SurfaceRegistry::Default()->DecrementGlobalUseCount(id);
	}

	count = SurfaceRegistry::Default()->GlobalUseCount(id);
	ASSERT_EQ(count, 1);  // Back to initial

	KosmSurfaceAllocator::Default()->Free(surface);
}


int
main()
{
	printf("=== IPC Tests ===\n\n");

	RUN_TEST(area_cloneable);
	RUN_TEST(registry_lookup_area);
	RUN_TEST(global_use_count);
	RUN_TEST(serialize_surface_info);
	RUN_TEST(concurrent_use_counts);

	printf("\n=== Results: %d passed, %d failed ===\n",
		sTestsPassed, sTestsFailed);

	return sTestsFailed > 0 ? 1 : 0;
}
