/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.h>
#include <KosmSurfaceAllocator.h>
#include <Message.h>

#include <stdio.h>
#include <string.h>


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


TEST(create_surface)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 100;
	desc.height = 100;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);
	ASSERT(surface != NULL);
	ASSERT_EQ(surface->Width(), 100u);
	ASSERT_EQ(surface->Height(), 100u);
	ASSERT_EQ(surface->Format(), PIXEL_FORMAT_BGRA8888);
	ASSERT(surface->ID() != 0);

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_lock_unlock)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 64;
	desc.height = 64;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	// BaseAddress should be NULL before lock
	ASSERT(surface->BaseAddress() == NULL);

	// Lock the surface
	uint32 seed1 = 0;
	status_t status = surface->Lock(0, &seed1);
	ASSERT_EQ(status, B_OK);

	// BaseAddress should be valid after lock
	ASSERT(surface->BaseAddress() != NULL);

	// Double lock should fail
	status = surface->Lock();
	ASSERT_EQ(status, B_SURFACE_ALREADY_LOCKED);

	// Write to the surface
	void* data = surface->BaseAddress();
	memset(data, 0xFF, surface->BytesPerRow() * surface->Height());

	// Unlock should increment seed (since we did write lock)
	uint32 seed2 = 0;
	status = surface->Unlock(0, &seed2);
	ASSERT_EQ(status, B_OK);
	ASSERT_EQ(seed2, seed1 + 1);

	// BaseAddress should be NULL after unlock
	ASSERT(surface->BaseAddress() == NULL);

	// Double unlock should fail
	status = surface->Unlock();
	ASSERT_EQ(status, B_SURFACE_NOT_LOCKED);

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_readonly_lock)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 32;
	desc.height = 32;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	uint32 seed1 = 0;
	status_t status = surface->Lock(SURFACE_LOCK_READ_ONLY, &seed1);
	ASSERT_EQ(status, B_OK);

	// Readonly unlock should NOT increment seed
	uint32 seed2 = 0;
	status = surface->Unlock(0, &seed2);
	ASSERT_EQ(status, B_OK);
	ASSERT_EQ(seed2, seed1);

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_use_count)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 32;
	desc.height = 32;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	ASSERT_EQ(surface->LocalUseCount(), 0);
	ASSERT(!surface->IsInUse());

	surface->IncrementUseCount();
	ASSERT_EQ(surface->LocalUseCount(), 1);
	ASSERT(surface->IsInUse());

	surface->IncrementUseCount();
	ASSERT_EQ(surface->LocalUseCount(), 2);

	surface->DecrementUseCount();
	ASSERT_EQ(surface->LocalUseCount(), 1);
	ASSERT(surface->IsInUse());

	surface->DecrementUseCount();
	ASSERT_EQ(surface->LocalUseCount(), 0);
	ASSERT(!surface->IsInUse());

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_attachments)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 32;
	desc.height = 32;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	// Set attachment
	BMessage value;
	value.AddInt32("test_value", 42);
	status_t status = surface->SetAttachment("test_key", value);
	ASSERT_EQ(status, B_OK);

	// Get attachment
	BMessage retrieved;
	status = surface->GetAttachment("test_key", &retrieved);
	ASSERT_EQ(status, B_OK);

	int32 testValue = 0;
	status = retrieved.FindInt32("test_value", &testValue);
	ASSERT_EQ(status, B_OK);
	ASSERT_EQ(testValue, 42);

	// Remove attachment
	status = surface->RemoveAttachment("test_key");
	ASSERT_EQ(status, B_OK);

	// Should not find removed attachment
	status = surface->GetAttachment("test_key", &retrieved);
	ASSERT(status != B_OK);

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_area)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 128;
	desc.height = 128;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	area_id areaId = surface->Area();
	ASSERT(areaId >= 0);

	// Verify area info
	area_info info;
	status_t status = get_area_info(areaId, &info);
	ASSERT_EQ(status, B_OK);
	ASSERT(info.size >= surface->AllocSize());

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(surface_bytes_per_row)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 100;  // Not aligned
	desc.height = 100;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	// BytesPerRow should be aligned to 64 bytes
	uint32 expectedBpr = (100 * 4 + 63) & ~63;  // 400 -> 448
	ASSERT_EQ(surface->BytesPerRow(), expectedBpr);

	KosmSurfaceAllocator::Default()->Free(surface);
}


int
main()
{
	printf("=== Surface Kit Tests ===\n\n");

	RUN_TEST(create_surface);
	RUN_TEST(surface_lock_unlock);
	RUN_TEST(surface_readonly_lock);
	RUN_TEST(surface_use_count);
	RUN_TEST(surface_attachments);
	RUN_TEST(surface_area);
	RUN_TEST(surface_bytes_per_row);

	printf("\n=== Results: %d passed, %d failed ===\n",
		sTestsPassed, sTestsFailed);

	return sTestsFailed > 0 ? 1 : 0;
}
