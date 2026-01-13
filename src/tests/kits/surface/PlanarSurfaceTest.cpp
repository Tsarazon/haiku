/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.h>
#include <KosmSurfaceAllocator.h>

#include <stdio.h>
#include <string.h>

#include "PlanarLayout.h"


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


TEST(planar_format_info)
{
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_RGBA8888), 1u);
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_BGRA8888), 1u);
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_RGB565), 1u);
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_NV12), 2u);
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_NV21), 2u);
	ASSERT_EQ(planar_get_plane_count(PIXEL_FORMAT_YV12), 3u);

	ASSERT_EQ(planar_get_bits_per_pixel(PIXEL_FORMAT_RGBA8888), 32u);
	ASSERT_EQ(planar_get_bits_per_pixel(PIXEL_FORMAT_RGB565), 16u);
	ASSERT_EQ(planar_get_bits_per_pixel(PIXEL_FORMAT_NV12), 12u);
	ASSERT_EQ(planar_get_bits_per_pixel(PIXEL_FORMAT_YV12), 12u);

	ASSERT(!planar_is_format_planar(PIXEL_FORMAT_RGBA8888));
	ASSERT(!planar_is_format_planar(PIXEL_FORMAT_BGRA8888));
	ASSERT(planar_is_format_planar(PIXEL_FORMAT_NV12));
	ASSERT(planar_is_format_planar(PIXEL_FORMAT_NV21));
	ASSERT(planar_is_format_planar(PIXEL_FORMAT_YV12));
}


TEST(nv12_surface)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 640;
	desc.height = 480;
	desc.format = PIXEL_FORMAT_NV12;

	KosmSurface* surface = NULL;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);
	ASSERT(surface != NULL);

	ASSERT_EQ(surface->PlaneCount(), 2u);

	// Y plane: full resolution
	ASSERT_EQ(surface->WidthOfPlane(0), 640u);
	ASSERT_EQ(surface->HeightOfPlane(0), 480u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(0), 1u);

	// UV plane: half resolution, interleaved
	ASSERT_EQ(surface->WidthOfPlane(1), 320u);
	ASSERT_EQ(surface->HeightOfPlane(1), 240u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(1), 2u);

	// Lock and verify plane addresses
	surface->Lock();

	void* yPlane = surface->BaseAddressOfPlane(0);
	void* uvPlane = surface->BaseAddressOfPlane(1);

	ASSERT(yPlane != NULL);
	ASSERT(uvPlane != NULL);
	ASSERT(uvPlane > yPlane);  // UV plane comes after Y plane

	// Y plane size should be stride * height
	uint32 yStride = surface->BytesPerRowOfPlane(0);
	size_t ySize = yStride * 480;

	// UV plane should start at Y plane end
	ASSERT_EQ((uint8*)uvPlane - (uint8*)yPlane, (ptrdiff_t)ySize);

	surface->Unlock();

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(yv12_surface)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 1920;
	desc.height = 1080;
	desc.format = PIXEL_FORMAT_YV12;

	KosmSurface* surface = NULL;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT_EQ(status, B_OK);
	ASSERT(surface != NULL);

	ASSERT_EQ(surface->PlaneCount(), 3u);

	// Y plane: full resolution
	ASSERT_EQ(surface->WidthOfPlane(0), 1920u);
	ASSERT_EQ(surface->HeightOfPlane(0), 1080u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(0), 1u);

	// V plane: half resolution
	ASSERT_EQ(surface->WidthOfPlane(1), 960u);
	ASSERT_EQ(surface->HeightOfPlane(1), 540u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(1), 1u);

	// U plane: half resolution
	ASSERT_EQ(surface->WidthOfPlane(2), 960u);
	ASSERT_EQ(surface->HeightOfPlane(2), 540u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(2), 1u);

	surface->Lock();

	void* yPlane = surface->BaseAddressOfPlane(0);
	void* vPlane = surface->BaseAddressOfPlane(1);
	void* uPlane = surface->BaseAddressOfPlane(2);

	ASSERT(yPlane != NULL);
	ASSERT(vPlane != NULL);
	ASSERT(uPlane != NULL);
	ASSERT(vPlane > yPlane);
	ASSERT(uPlane > vPlane);

	surface->Unlock();

	KosmSurfaceAllocator::Default()->Free(surface);
}


TEST(planar_calculate_plane)
{
	const size_t alignment = 64;
	plane_info plane;

	// Test BGRA8888 (single plane)
	planar_calculate_plane(PIXEL_FORMAT_BGRA8888, 0, 800, 600, alignment, &plane);
	ASSERT_EQ(plane.width, 800u);
	ASSERT_EQ(plane.height, 600u);
	ASSERT_EQ(plane.bytesPerElement, 4u);
	ASSERT_EQ(plane.bytesPerRow, (800u * 4 + 63) & ~63u);  // Aligned to 64
	ASSERT_EQ(plane.offset, 0u);

	// Test NV12 Y plane
	planar_calculate_plane(PIXEL_FORMAT_NV12, 0, 1280, 720, alignment, &plane);
	ASSERT_EQ(plane.width, 1280u);
	ASSERT_EQ(plane.height, 720u);
	ASSERT_EQ(plane.bytesPerElement, 1u);
	ASSERT_EQ(plane.bytesPerRow, 1280u);  // 1280 already aligned to 64
	ASSERT_EQ(plane.offset, 0u);

	// Test NV12 UV plane
	planar_calculate_plane(PIXEL_FORMAT_NV12, 1, 1280, 720, alignment, &plane);
	ASSERT_EQ(plane.width, 640u);
	ASSERT_EQ(plane.height, 360u);
	ASSERT_EQ(plane.bytesPerElement, 2u);
	// UV plane offset = Y plane size = 1280 * 720
	ASSERT_EQ(plane.offset, 1280u * 720);
}


TEST(planar_total_size)
{
	const size_t alignment = 64;

	// BGRA8888: single plane
	size_t size = planar_calculate_total_size(PIXEL_FORMAT_BGRA8888,
		100, 100, alignment);
	// stride = (100*4 + 63) & ~63 = 448
	// size = 448 * 100 = 44800
	ASSERT_EQ(size, 44800u);

	// NV12: Y + UV planes
	size = planar_calculate_total_size(PIXEL_FORMAT_NV12, 640, 480, alignment);
	// Y: 640 * 480 = 307200
	// UV: 640 * 240 = 153600
	// Total = 460800
	ASSERT_EQ(size, 460800u);
}


TEST(invalid_plane_index)
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 640;
	desc.height = 480;
	desc.format = PIXEL_FORMAT_BGRA8888;

	KosmSurface* surface = NULL;
	KosmSurfaceAllocator::Default()->Allocate(desc, &surface);
	ASSERT(surface != NULL);

	// Single plane format - plane 1 should return 0
	ASSERT_EQ(surface->WidthOfPlane(1), 0u);
	ASSERT_EQ(surface->HeightOfPlane(1), 0u);
	ASSERT_EQ(surface->BytesPerElementOfPlane(1), 0u);
	ASSERT_EQ(surface->BytesPerRowOfPlane(1), 0u);

	surface->Lock();
	ASSERT(surface->BaseAddressOfPlane(1) == NULL);
	surface->Unlock();

	KosmSurfaceAllocator::Default()->Free(surface);
}


int
main()
{
	printf("=== Planar Surface Tests ===\n\n");

	RUN_TEST(planar_format_info);
	RUN_TEST(nv12_surface);
	RUN_TEST(yv12_surface);
	RUN_TEST(planar_calculate_plane);
	RUN_TEST(planar_total_size);
	RUN_TEST(invalid_plane_index);

	printf("\n=== Results: %d passed, %d failed ===\n",
		sTestsPassed, sTestsFailed);

	return sTestsFailed > 0 ? 1 : 0;
}
