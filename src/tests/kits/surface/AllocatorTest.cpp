/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.hpp>
#include <KosmSurfaceAllocator.hpp>

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


TEST(allocator_singleton)
{
	KosmSurfaceAllocator* alloc1 = KosmSurfaceAllocator::Default();
	KosmSurfaceAllocator* alloc2 = KosmSurfaceAllocator::Default();
	ASSERT(alloc1 != NULL);
	ASSERT(alloc1 == alloc2);
}


TEST(allocator_property_maximum)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	size_t maxWidth = allocator->GetPropertyMaximum("width");
	size_t maxHeight = allocator->GetPropertyMaximum("height");

	ASSERT(maxWidth > 0);
	ASSERT(maxHeight > 0);
	ASSERT_EQ(maxWidth, 16384u);
	ASSERT_EQ(maxHeight, 16384u);

	// Unknown property should return 0
	size_t unknown = allocator->GetPropertyMaximum("unknown");
	ASSERT_EQ(unknown, 0u);
}


TEST(allocator_property_alignment)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	size_t alignment = allocator->GetPropertyAlignment("bytesPerRow");
	ASSERT(alignment > 0);
	ASSERT_EQ(alignment, 64u);

	// Unknown property should return 1
	size_t unknown = allocator->GetPropertyAlignment("unknown");
	ASSERT_EQ(unknown, 1u);
}


TEST(allocator_format_support)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_RGBA8888));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_BGRA8888));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_RGB565));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_RGBX8888));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_NV12));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_NV21));
	ASSERT(allocator->IsFormatSupported(PIXEL_FORMAT_YV12));

	// Unknown format should not be supported
	ASSERT(!allocator->IsFormatSupported((pixel_format)999));
}


TEST(allocator_invalid_params)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	// NULL output pointer
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 100;
	desc.height = 100;

	status_t status = allocator->Allocate(desc, NULL);
	ASSERT_EQ(status, B_BAD_VALUE);

	// Zero dimensions
	desc.width = 0;
	desc.height = 100;
	KosmSurface* surface = NULL;
	status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_BAD_VALUE);
	ASSERT(surface == NULL);

	desc.width = 100;
	desc.height = 0;
	status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_BAD_VALUE);
	ASSERT(surface == NULL);

	// Unsupported format
	desc.width = 100;
	desc.height = 100;
	desc.format = (pixel_format)999;
	status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_BAD_VALUE);
	ASSERT(surface == NULL);
}


TEST(allocator_exceeds_maximum)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 20000;  // Exceeds 16384
	desc.height = 100;

	KosmSurface* surface = NULL;
	status_t status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_BAD_VALUE);
	ASSERT(surface == NULL);

	desc.width = 100;
	desc.height = 20000;  // Exceeds 16384
	status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_BAD_VALUE);
	ASSERT(surface == NULL);
}


TEST(allocator_lookup)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 64;
	desc.height = 64;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	status_t status = allocator->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);
	ASSERT(surface != NULL);

	surface_id id = surface->ID();

	// Lookup should find the surface
	KosmSurface* found = NULL;
	status = allocator->Lookup(id, &found);
	ASSERT_EQ(status, B_OK);
	ASSERT_EQ(found, surface);

	// Free the surface
	allocator->Free(surface);

	// Lookup should not find it anymore
	status = allocator->Lookup(id, &found);
	ASSERT_EQ(status, B_NAME_NOT_FOUND);
}


TEST(allocator_multiple_surfaces)
{
	KosmSurfaceAllocator* allocator = KosmSurfaceAllocator::Default();

	const int count = 10;
	KosmSurface* surfaces[count];
	surface_id ids[count];

	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 32;
	desc.height = 32;
	desc.format = PIXEL_FORMAT_BGRA8888;

	// Allocate multiple surfaces
	for (int i = 0; i < count; i++) {
		status_t status = allocator->Allocate(desc, &surfaces[i]);
		ASSERT_EQ(status, B_OK);
		ids[i] = surfaces[i]->ID();
	}

	// Verify all IDs are unique
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			ASSERT(ids[i] != ids[j]);
		}
	}

	// Free in reverse order
	for (int i = count - 1; i >= 0; i--) {
		allocator->Free(surfaces[i]);
	}
}


TEST(allocator_free_null)
{
	// Should not crash
	KosmSurfaceAllocator::Default()->Free(NULL);
}


int
main()
{
	printf("=== Allocator Tests ===\n\n");

	RUN_TEST(allocator_singleton);
	RUN_TEST(allocator_property_maximum);
	RUN_TEST(allocator_property_alignment);
	RUN_TEST(allocator_format_support);
	RUN_TEST(allocator_invalid_params);
	RUN_TEST(allocator_exceeds_maximum);
	RUN_TEST(allocator_lookup);
	RUN_TEST(allocator_multiple_surfaces);
	RUN_TEST(allocator_free_null);

	printf("\n=== Results: %d passed, %d failed ===\n",
		sTestsPassed, sTestsFailed);

	return sTestsFailed > 0 ? 1 : 0;
}
