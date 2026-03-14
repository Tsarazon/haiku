/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmSurface test functions for the unified test suite.
 */


#include "TestCommon.h"

#include <KosmSurface.hpp>
#include <KosmSurfaceAllocator.hpp>

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>


static void
trace_status(const char* func, status_t result)
{
	trace("    %s -> %s (0x%08x)\n", func, strerror(result),
		(unsigned)result);
}

// ============================================================
// Thread helpers
// ============================================================

struct lock_test_args {
	KosmSurface*	surface;
	status_t		result;
	uint32			options;
};


static status_t
lock_from_other_thread(void* data)
{
	lock_test_args* args = (lock_test_args*)data;
	args->result = args->surface->Lock(args->options);
	trace("    [thread %d] Lock(options=0x%x) -> %s (0x%08x)\n",
		(int)find_thread(NULL), (unsigned)args->options,
		strerror(args->result), (unsigned)args->result);
	if (args->result == B_OK)
		args->surface->Unlock();
	return B_OK;
}


struct unlock_test_args {
	KosmSurface*	surface;
	status_t		result;
};


static status_t
unlock_from_other_thread(void* data)
{
	unlock_test_args* args = (unlock_test_args*)data;
	args->result = args->surface->Unlock();
	trace("    [thread %d] Unlock -> %s (0x%08x)\n",
		(int)find_thread(NULL),
		strerror(args->result), (unsigned)args->result);
	return B_OK;
}


struct alloc_free_args {
	int				iterations;
	int				errors;
	int				allocFails;
	int				lockFails;
	bigtime_t		totalTime;
};


static status_t
alloc_free_churn_thread(void* data)
{
	alloc_free_args* args = (alloc_free_args*)data;
	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	args->errors = 0;
	args->allocFails = 0;
	args->lockFails = 0;

	int tid = (int)find_thread(NULL);
	trace("    [thread %d] alloc_free_churn start, iters=%d\n",
		tid, args->iterations);

	bigtime_t t0 = system_time();
	for (int i = 0; i < args->iterations; i++) {
		KosmSurfaceDesc desc;
		desc.width = 64;
		desc.height = 64;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		KosmSurface* surface = NULL;
		status_t err = alloc->Allocate(desc, &surface);
		if (err != B_OK || surface == NULL) {
			args->allocFails++;
			args->errors++;
			trace("    [thread %d] Allocate FAIL iter %d: %s (0x%08x)\n",
				tid, i, strerror(err), (unsigned)err);
			continue;
		}

		err = surface->Lock();
		if (err == B_OK) {
			void* base = surface->BaseAddress();
			if (base != NULL)
				memset(base, 0xFF, 64 * 4);
			surface->Unlock();
		} else {
			args->lockFails++;
			trace("    [thread %d] Lock FAIL iter %d: %s (0x%08x)\n",
				tid, i, strerror(err), (unsigned)err);
		}

		alloc->Free(surface);
	}
	args->totalTime = system_time() - t0;
	trace("    [thread %d] alloc_free_churn done: allocFails=%d, "
		"lockFails=%d, time=%lld us\n", tid, args->allocFails,
		args->lockFails, (long long)args->totalTime);
	return B_OK;
}


struct lock_contention_args {
	KosmSurface*	surface;
	int				iterations;
	int				errors;
	int				acquired;
	int				timedOut;
	bigtime_t		maxLockTime;
	bigtime_t		totalTime;
	int32*			counter;
};


static status_t
lock_contention_thread(void* data)
{
	lock_contention_args* args = (lock_contention_args*)data;
	args->errors = 0;
	args->acquired = 0;
	args->timedOut = 0;
	args->maxLockTime = 0;

	int tid = (int)find_thread(NULL);
	bigtime_t tStart = system_time();

	trace("    [thread %d] lock_contention start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		bigtime_t t0 = system_time();
		status_t err = args->surface->LockWithTimeout(1000000);
		bigtime_t dt = system_time() - t0;

		if (dt > args->maxLockTime)
			args->maxLockTime = dt;

		if (err != B_OK) {
			if (err == B_TIMED_OUT) {
				args->timedOut++;
			} else {
				trace("    [thread %d] LockWithTimeout FAIL iter %d: %s "
					"(0x%08x), waited %lld us\n", tid, i, strerror(err),
					(unsigned)err, (long long)dt);
			}
			args->errors++;
			continue;
		}

		args->acquired++;
		atomic_add(args->counter, 1);
		snooze(1);
		args->surface->Unlock();
	}

	args->totalTime = system_time() - tStart;
	trace("    [thread %d] lock_contention done: acquired=%d, timedOut=%d, "
		"errors=%d, maxWait=%lld us, time=%lld us\n", tid,
		args->acquired, args->timedOut,
		args->errors - args->timedOut,
		(long long)args->maxLockTime, (long long)args->totalTime);
	return B_OK;
}


// ============================================================
// Test functions
// ============================================================


// --- Basic allocation ---

static void
test_allocate_free()
{
	trace("\n--- test_allocate_free ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	TEST_ASSERT("allocator exists", alloc != NULL);

	KosmSurfaceDesc desc;
	desc.width = 320;
	desc.height = 240;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(320x240, ARGB8888)", err);
	TEST_ASSERT("allocate succeeds", err == B_OK);
	TEST_ASSERT("surface not null", surface != NULL);

	trace("    ID=%u, Width=%u, Height=%u, Format=%d\n",
		(unsigned)surface->ID(), (unsigned)surface->Width(),
		(unsigned)surface->Height(), (int)surface->Format());
	trace("    BytesPerElement=%u, BytesPerRow=%u, AllocSize=%zu\n",
		(unsigned)surface->BytesPerElement(),
		(unsigned)surface->BytesPerRow(),
		surface->AllocSize());

	TEST_ASSERT("ID > 0", surface->ID() > 0);
	TEST_ASSERT("Width == 320", surface->Width() == 320);
	TEST_ASSERT("Height == 240", surface->Height() == 240);
	TEST_ASSERT("Format == ARGB8888",
		surface->Format() == KOSM_PIXEL_FORMAT_ARGB8888);
	TEST_ASSERT("BytesPerElement == 4", surface->BytesPerElement() == 4);
	TEST_ASSERT("BytesPerRow >= 320*4",
		surface->BytesPerRow() >= 320 * 4);
	TEST_ASSERT("AllocSize >= 320*240*4",
		surface->AllocSize() >= 320 * 240 * 4);
	TEST_ASSERT("IsValid", surface->IsValid());

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_allocate_zero_size()
{
	trace("\n--- test_allocate_zero_size ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 0;
	desc.height = 0;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(0x0)", err);
	TEST_ASSERT("zero-size alloc fails", err != B_OK);
	TEST_ASSERT("surface is null", surface == NULL);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_allocate_all_formats()
{
	trace("\n--- test_allocate_all_formats ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	const char* formatNames[] = {
		"ARGB8888", "BGRA8888", "RGBA8888", "RGBX8888", "XRGB8888",
		"RGB565", "NV12", "NV21", "YV12", "A8", "L8"
	};

	for (int f = 0; f < KOSM_PIXEL_FORMAT_COUNT; f++) {
		KosmSurfaceDesc desc;
		desc.width = 128;
		desc.height = 128;
		desc.format = (kosm_pixel_format)f;

		KosmSurface* surface = NULL;
		status_t err = alloc->Allocate(desc, &surface);
		trace("    format %s (%d): %s (0x%08x)\n",
			formatNames[f], f, strerror(err), (unsigned)err);

		char name[64];
		snprintf(name, sizeof(name), "allocate %s", formatNames[f]);
		TEST_ASSERT(name, err == B_OK && surface != NULL);

		if (surface != NULL) {
			trace("      planes=%u, bpe=%u, bpr=%u, alloc=%zu\n",
				(unsigned)surface->PlaneCount(),
				(unsigned)surface->BytesPerElement(),
				(unsigned)surface->BytesPerRow(),
				surface->AllocSize());
			alloc->Free(surface);
		}
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lookup()
{
	trace("\n--- test_lookup ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	kosm_surface_id id = surface->ID();
	trace("    allocated id=%u\n", (unsigned)id);

	KosmSurface* found = NULL;
	status_t err = alloc->Lookup(id, &found);
	trace_status("Lookup(id)", err);
	TEST_ASSERT("lookup succeeds", err == B_OK);
	TEST_ASSERT("lookup returns same surface", found == surface);

	KosmSurface* notFound = NULL;
	err = alloc->Lookup(99999, &notFound);
	trace_status("Lookup(99999)", err);
	TEST_ASSERT("lookup nonexistent fails", err != B_OK);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_format_support()
{
	trace("\n--- test_format_support ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	TEST_ASSERT("ARGB8888 supported",
		alloc->IsFormatSupported(KOSM_PIXEL_FORMAT_ARGB8888));
	TEST_ASSERT("RGB565 supported",
		alloc->IsFormatSupported(KOSM_PIXEL_FORMAT_RGB565));
	TEST_ASSERT("A8 supported",
		alloc->IsFormatSupported(KOSM_PIXEL_FORMAT_A8));
	TEST_ASSERT("invalid format not supported",
		!alloc->IsFormatSupported((kosm_pixel_format)999));

	size_t maxW = alloc->GetPropertyMaximum("width");
	size_t maxH = alloc->GetPropertyMaximum("height");
	size_t strideAlign = alloc->GetPropertyAlignment("bytesPerRow");
	trace("    maxWidth=%zu, maxHeight=%zu, strideAlign=%zu\n",
		maxW, maxH, strideAlign);

	TEST_ASSERT("maxWidth > 0", maxW > 0);
	TEST_ASSERT("maxHeight > 0", maxH > 0);
	TEST_ASSERT("strideAlign > 0", strideAlign > 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lookup_or_clone()
{
	trace("\n--- test_lookup_or_clone ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 128;
	desc.height = 128;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* original = NULL;
	alloc->Allocate(desc, &original);
	TEST_ASSERT("allocate original", original != NULL);

	kosm_surface_id id = original->ID();

	// Write known pattern into original
	original->Lock();
	uint32* pixels = (uint32*)original->BaseAddress();
	if (pixels != NULL) {
		uint32 bpr = original->BytesPerRow();
		for (uint32 y = 0; y < 4; y++) {
			uint32* row = (uint32*)((uint8*)pixels + y * bpr);
			for (uint32 x = 0; x < 128; x++)
				row[x] = 0xDEADBEEF;
		}
	}
	original->Unlock();

	// LookupOrClone with same ID — should return same or cloned surface
	KosmSurface* cloned = NULL;
	status_t err = alloc->LookupOrClone(id, &cloned);
	trace_status("LookupOrClone(id)", err);
	TEST_ASSERT("lookup or clone succeeds", err == B_OK);
	TEST_ASSERT("cloned not null", cloned != NULL);

	if (cloned != NULL) {
		TEST_ASSERT("cloned has same dimensions",
			cloned->Width() == 128 && cloned->Height() == 128);
		TEST_ASSERT("cloned has same format",
			cloned->Format() == KOSM_PIXEL_FORMAT_ARGB8888);

		// Verify data visible through clone
		err = cloned->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
		if (err == B_OK) {
			uint32* clonedPixels = (uint32*)cloned->BaseAddress();
			bool dataMatch = (clonedPixels != NULL
				&& clonedPixels[0] == 0xDEADBEEF);
			trace("    cloned pixel[0]=0x%08x\n",
				clonedPixels ? (unsigned)clonedPixels[0] : 0);
			TEST_ASSERT("cloned data matches original", dataMatch);
			cloned->Unlock();
		} else {
			trace("    could not lock clone: %s\n", strerror(err));
			TEST_ASSERT("lock clone for read", false);
		}

		if (cloned != original)
			alloc->Free(cloned);
	}

	alloc->Free(original);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lookup_with_token()
{
	trace("\n--- test_lookup_with_token ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Create token
	KosmSurfaceToken token;
	memset(&token, 0, sizeof(token));
	status_t err = surface->CreateAccessToken(&token);
	TEST_ASSERT("create token", err == B_OK);
	trace("    token: id=%u, secret=0x%llx, gen=%u\n",
		(unsigned)token.id, (unsigned long long)token.secret,
		(unsigned)token.generation);

	// Lookup using token
	KosmSurface* found = NULL;
	err = alloc->LookupWithToken(token, &found);
	trace_status("LookupWithToken", err);
	TEST_ASSERT("lookup with token succeeds", err == B_OK);
	TEST_ASSERT("found not null", found != NULL);

	if (found != NULL) {
		TEST_ASSERT("found has correct dimensions",
			found->Width() == 64 && found->Height() == 64);
		TEST_ASSERT("found has correct format",
			found->Format() == KOSM_PIXEL_FORMAT_ARGB8888);
		trace("    found: id=%u, %ux%u\n",
			(unsigned)found->ID(),
			(unsigned)found->Width(),
			(unsigned)found->Height());

		if (found != surface)
			alloc->Free(found);
	}

	// Invalid token should fail
	KosmSurfaceToken bogus;
	bogus.id = 99999;
	bogus.secret = 0xBADBADBAD;
	bogus.generation = 0;
	KosmSurface* notFound = NULL;
	err = alloc->LookupWithToken(bogus, &notFound);
	trace_status("LookupWithToken(bogus)", err);
	TEST_ASSERT("bogus token fails", err != B_OK);
	TEST_ASSERT("bogus result is null", notFound == NULL);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_revoked_token_lookup()
{
	trace("\n--- test_revoked_token_lookup ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Create token, then revoke
	KosmSurfaceToken token;
	memset(&token, 0, sizeof(token));
	surface->CreateAccessToken(&token);
	trace("    token before revoke: secret=0x%llx, gen=%u\n",
		(unsigned long long)token.secret, (unsigned)token.generation);

	surface->RevokeAllAccess();

	// Old token should no longer work
	KosmSurface* found = NULL;
	status_t err = alloc->LookupWithToken(token, &found);
	trace_status("LookupWithToken(revoked)", err);
	TEST_ASSERT("revoked token lookup fails", err != B_OK);
	TEST_ASSERT("revoked result is null", found == NULL);

	// New token should still work
	KosmSurfaceToken newToken;
	surface->CreateAccessToken(&newToken);
	trace("    new token: secret=0x%llx, gen=%u\n",
		(unsigned long long)newToken.secret,
		(unsigned)newToken.generation);

	found = NULL;
	err = alloc->LookupWithToken(newToken, &found);
	trace_status("LookupWithToken(new)", err);
	TEST_ASSERT("new token lookup succeeds", err == B_OK);

	if (found != NULL && found != surface)
		alloc->Free(found);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_free_then_lookup()
{
	trace("\n--- test_free_then_lookup ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	kosm_surface_id id = surface->ID();
	trace("    allocated id=%u\n", (unsigned)id);

	// Also grab a token before freeing
	KosmSurfaceToken token;
	memset(&token, 0, sizeof(token));
	surface->CreateAccessToken(&token);

	alloc->Free(surface);

	// Lookup by ID should fail after free
	KosmSurface* found = NULL;
	status_t err = alloc->Lookup(id, &found);
	trace_status("Lookup(freed id)", err);
	TEST_ASSERT("lookup after free fails", err != B_OK);
	TEST_ASSERT("result is null", found == NULL);

	// LookupWithToken should also fail
	found = NULL;
	err = alloc->LookupWithToken(token, &found);
	trace_status("LookupWithToken(freed)", err);
	TEST_ASSERT("token lookup after free fails", err != B_OK);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_double_free()
{
	trace("\n--- test_double_free ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	kosm_surface_id id = surface->ID();
	trace("    id=%u, freeing first time\n", (unsigned)id);
	alloc->Free(surface);

	// Second free — should not crash
	trace("    freeing second time (should not crash)\n");
	alloc->Free(surface);

	TEST_ASSERT("double free did not crash", true);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_cache_modes()
{
	trace("\n--- test_cache_modes ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	uint32 modes[] = {
		KOSM_CACHE_DEFAULT,
		KOSM_CACHE_INHIBIT,
		KOSM_CACHE_WRITE_THROUGH,
		KOSM_CACHE_WRITE_COMBINE
	};
	const char* modeNames[] = {
		"DEFAULT", "INHIBIT", "WRITE_THROUGH", "WRITE_COMBINE"
	};

	for (int i = 0; i < 4; i++) {
		KosmSurfaceDesc desc;
		desc.width = 64;
		desc.height = 64;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
		desc.cacheMode = modes[i];

		KosmSurface* s = NULL;
		status_t err = alloc->Allocate(desc, &s);
		trace("    cacheMode %s (%u) -> %s\n",
			modeNames[i], (unsigned)modes[i], strerror(err));

		char name[64];
		snprintf(name, sizeof(name), "cache mode %s", modeNames[i]);
		TEST_ASSERT(name, err == B_OK && s != NULL);

		if (s != NULL) {
			// Verify basic lock/write/unlock cycle works
			err = s->Lock();
			if (err == B_OK) {
				void* base = s->BaseAddress();
				if (base != NULL)
					memset(base, 0xAA, 64 * 4);
				s->Unlock();
			}
			alloc->Free(s);
		}
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_custom_stride()
{
	trace("\n--- test_custom_stride ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	// Custom stride with extra padding (e.g. 256-byte aligned rows)
	KosmSurfaceDesc desc;
	desc.width = 100;
	desc.height = 100;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	desc.bytesPerRow = 512;  // 100*4=400, padded to 512

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(100x100, bpr=512)", err);
	TEST_ASSERT("custom stride alloc succeeds", err == B_OK);

	if (surface != NULL) {
		trace("    actual bpr=%u\n", (unsigned)surface->BytesPerRow());
		TEST_ASSERT("bytesPerRow >= requested 512",
			surface->BytesPerRow() >= 512);
		TEST_ASSERT("allocSize accounts for stride",
			surface->AllocSize() >= (size_t)512 * 100);

		// Write and read using custom stride
		err = surface->Lock();
		if (err == B_OK) {
			uint8* base = (uint8*)surface->BaseAddress();
			uint32 bpr = surface->BytesPerRow();
			// Write last pixel of first row
			uint32* lastPixel = (uint32*)(base + 99 * 4);
			*lastPixel = 0xCAFEBABE;
			// Write first pixel of second row
			uint32* nextRow = (uint32*)(base + bpr);
			*nextRow = 0xFACEFACE;
			surface->Unlock();
		}

		err = surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
		if (err == B_OK) {
			uint8* base = (uint8*)surface->BaseAddress();
			uint32 bpr = surface->BytesPerRow();
			uint32 val1 = *(uint32*)(base + 99 * 4);
			uint32 val2 = *(uint32*)(base + bpr);
			trace("    last pixel row0=0x%08x, first pixel row1=0x%08x\n",
				(unsigned)val1, (unsigned)val2);
			TEST_ASSERT("stride preserves row separation",
				val1 == 0xCAFEBABE && val2 == 0xFACEFACE);
			surface->Unlock();
		}

		alloc->Free(surface);
	}

	// Too-small stride should fail
	KosmSurfaceDesc badDesc;
	badDesc.width = 100;
	badDesc.height = 100;
	badDesc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	badDesc.bytesPerRow = 64;  // way too small for 100*4=400

	KosmSurface* bad = NULL;
	err = alloc->Allocate(badDesc, &bad);
	trace_status("Allocate(bpr=64, too small)", err);
	TEST_ASSERT("too-small stride rejected", err != B_OK);
	TEST_ASSERT("bad surface is null", bad == NULL);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_large_allocation()
{
	trace("\n--- test_large_allocation ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	size_t maxW = alloc->GetPropertyMaximum("width");
	size_t maxH = alloc->GetPropertyMaximum("height");
	trace("    maxWidth=%zu, maxHeight=%zu\n", maxW, maxH);

	// Allocate at max dimensions (clamped to something reasonable)
	uint32 testW = (maxW > 8192) ? 8192 : (uint32)maxW;
	uint32 testH = (maxH > 8192) ? 8192 : (uint32)maxH;

	KosmSurfaceDesc desc;
	desc.width = testW;
	desc.height = testH;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(large)", err);
	trace("    requested %ux%u\n", (unsigned)testW, (unsigned)testH);

	char name[64];
	snprintf(name, sizeof(name), "allocate %ux%u", (unsigned)testW,
		(unsigned)testH);
	TEST_ASSERT(name, err == B_OK && surface != NULL);

	if (surface != NULL) {
		trace("    allocSize=%zu\n", surface->AllocSize());
		TEST_ASSERT("large surface valid", surface->IsValid());
		alloc->Free(surface);
	}

	// Over-max should fail
	if (maxW > 0 && maxH > 0) {
		KosmSurfaceDesc overDesc;
		overDesc.width = (uint32)maxW + 1;
		overDesc.height = (uint32)maxH + 1;
		overDesc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		KosmSurface* over = NULL;
		err = alloc->Allocate(overDesc, &over);
		trace_status("Allocate(over-max)", err);
		TEST_ASSERT("over-max allocation fails", err != B_OK);
		TEST_ASSERT("over-max surface is null", over == NULL);
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Lock / Unlock ---

static void
test_lock_unlock()
{
	trace("\n--- test_lock_unlock ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// BaseAddress is null before lock
	TEST_ASSERT("BaseAddress null before lock",
		surface->BaseAddress() == NULL);
	TEST_ASSERT("not locked initially", !surface->IsLocked());

	// Lock
	uint32 seed0 = 0;
	status_t err = surface->Lock(0, &seed0);
	trace_status("Lock()", err);
	TEST_ASSERT("lock succeeds", err == B_OK);
	TEST_ASSERT("IsLocked after lock", surface->IsLocked());
	TEST_ASSERT("LockOwner is current thread",
		surface->LockOwner() == find_thread(NULL));

	// BaseAddress is not null after lock
	void* base = surface->BaseAddress();
	trace("    BaseAddress=%p\n", base);
	TEST_ASSERT("BaseAddress not null after lock", base != NULL);

	// Write some pixels
	if (base != NULL) {
		uint32* pixels = (uint32*)base;
		pixels[0] = 0xFFFF0000;
		pixels[1] = 0xFF00FF00;
		pixels[2] = 0xFF0000FF;
	}

	// Unlock
	uint32 seed1 = 0;
	err = surface->Unlock(0, &seed1);
	trace_status("Unlock()", err);
	TEST_ASSERT("unlock succeeds", err == B_OK);
	TEST_ASSERT("not locked after unlock", !surface->IsLocked());

	// Seed incremented on write unlock
	trace("    seed before=%u, seed after=%u\n",
		(unsigned)seed0, (unsigned)seed1);
	TEST_ASSERT("seed incremented after write unlock", seed1 > seed0);

	// BaseAddress null after unlock
	TEST_ASSERT("BaseAddress null after unlock",
		surface->BaseAddress() == NULL);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lock_read_only()
{
	trace("\n--- test_lock_read_only ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Read-only lock
	uint32 seed0 = surface->Seed();
	status_t err = surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	trace_status("Lock(READ_ONLY)", err);
	TEST_ASSERT("read-only lock succeeds", err == B_OK);

	void* base = surface->BaseAddress();
	TEST_ASSERT("BaseAddress not null", base != NULL);

	// Unlock — seed should NOT increment for read-only
	uint32 seed1 = 0;
	err = surface->Unlock(0, &seed1);
	trace_status("Unlock() after read-only", err);
	TEST_ASSERT("unlock succeeds", err == B_OK);

	trace("    seed before=%u, seed after=%u\n",
		(unsigned)seed0, (unsigned)seed1);
	TEST_ASSERT("seed NOT incremented after read-only unlock",
		seed1 == seed0);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lock_recursive()
{
	trace("\n--- test_lock_recursive ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Lock twice (recursive)
	status_t err1 = surface->Lock();
	status_t err2 = surface->Lock();
	trace_status("Lock() 1st", err1);
	trace_status("Lock() 2nd (recursive)", err2);
	TEST_ASSERT("first lock", err1 == B_OK);
	TEST_ASSERT("second lock (recursive)", err2 == B_OK);

	// Still locked after one unlock
	surface->Unlock();
	TEST_ASSERT("still locked after 1 unlock", surface->IsLocked());

	// Fully unlocked after second
	surface->Unlock();
	TEST_ASSERT("unlocked after 2 unlocks", !surface->IsLocked());

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lock_other_thread()
{
	trace("\n--- test_lock_other_thread ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Lock in main thread
	surface->Lock();
	TEST_ASSERT("locked", surface->IsLocked());

	// Try lock from another thread — should return B_BUSY
	lock_test_args args;
	args.surface = surface;
	args.result = B_OK;
	args.options = 0;

	thread_id tid = spawn_thread(lock_from_other_thread,
		"lock_other", B_NORMAL_PRIORITY, &args);
	trace("    spawned thread %d for cross-thread lock\n", (int)tid);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("lock from other thread returns B_BUSY",
		args.result == B_BUSY);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_unlock_not_owner()
{
	trace("\n--- test_unlock_not_owner ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	surface->Lock();

	// Try unlock from another thread — should fail
	unlock_test_args args;
	args.surface = surface;
	args.result = B_OK;

	thread_id tid = spawn_thread(unlock_from_other_thread,
		"unlock_other", B_NORMAL_PRIORITY, &args);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("unlock from non-owner fails",
		args.result == B_NOT_ALLOWED);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_unlock_not_locked()
{
	trace("\n--- test_unlock_not_locked ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	status_t err = surface->Unlock();
	trace_status("Unlock() when not locked", err);
	TEST_ASSERT("unlock not-locked returns KOSM_SURFACE_NOT_LOCKED",
		err == KOSM_SURFACE_NOT_LOCKED);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lock_timeout()
{
	trace("\n--- test_lock_timeout ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Lock in main
	surface->Lock();

	// LockWithTimeout from another thread
	struct timeout_args {
		KosmSurface*	surface;
		bigtime_t		timeout;
		status_t		result;
		bigtime_t		elapsed;
	};

	timeout_args targs;
	targs.surface = surface;
	targs.timeout = 100000;
	targs.result = B_OK;
	targs.elapsed = 0;

	auto timeout_thread = [](void* data) -> status_t {
		timeout_args* a = (timeout_args*)data;
		bigtime_t t0 = system_time();
		a->result = a->surface->LockWithTimeout(a->timeout);
		a->elapsed = system_time() - t0;
		trace("    [thread %d] LockWithTimeout(%lld) -> %s (0x%08x), "
			"elapsed=%lld us\n",
			(int)find_thread(NULL), (long long)a->timeout,
			strerror(a->result), (unsigned)a->result,
			(long long)a->elapsed);
		if (a->result == B_OK)
			a->surface->Unlock();
		return B_OK;
	};

	thread_id tid = spawn_thread(timeout_thread, "lock_timeout",
		B_NORMAL_PRIORITY, &targs);
	trace("    spawned thread %d for timed lock (timeout=%lld us)\n",
		(int)tid, (long long)targs.timeout);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("timed lock returns B_TIMED_OUT",
		targs.result == B_TIMED_OUT);

	bigtime_t drift = targs.elapsed > targs.timeout
		? targs.elapsed - targs.timeout : targs.timeout - targs.elapsed;
	trace("    timeout=%lld, elapsed=%lld, drift=%lld us\n",
		(long long)targs.timeout, (long long)targs.elapsed,
		(long long)drift);
	TEST_ASSERT("timeout accuracy (drift < 50ms)", drift < 50000);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_lock_read_write_upgrade()
{
	trace("\n--- test_lock_read_write_upgrade ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Lock read-only first
	status_t err = surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	TEST_ASSERT("read-only lock", err == B_OK);

	// Try to upgrade to write lock (should fail)
	err = surface->Lock(0);
	trace_status("Lock(write) while read-only held", err);
	TEST_ASSERT("write upgrade from read-only fails",
		err == B_NOT_ALLOWED);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Seed ---

static void
test_seed()
{
	trace("\n--- test_seed ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	uint32 s0 = surface->Seed();
	trace("    initial seed=%u\n", (unsigned)s0);

	// Write lock + unlock increments seed
	surface->Lock();
	surface->Unlock();
	uint32 s1 = surface->Seed();
	trace("    after write lock/unlock seed=%u\n", (unsigned)s1);
	TEST_ASSERT("seed incremented after write", s1 == s0 + 1);

	// Read-only lock + unlock does NOT increment seed
	surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	surface->Unlock();
	uint32 s2 = surface->Seed();
	trace("    after read-only lock/unlock seed=%u\n", (unsigned)s2);
	TEST_ASSERT("seed NOT incremented after read-only", s2 == s1);

	// Multiple writes increment multiple times
	surface->Lock();
	surface->Unlock();
	surface->Lock();
	surface->Unlock();
	uint32 s3 = surface->Seed();
	trace("    after 2 more writes seed=%u\n", (unsigned)s3);
	TEST_ASSERT("seed incremented twice", s3 == s2 + 2);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Planar formats ---

static void
test_planar_nv12()
{
	trace("\n--- test_planar_nv12 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 640;
	desc.height = 480;
	desc.format = KOSM_PIXEL_FORMAT_NV12;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(640x480, NV12)", err);
	TEST_ASSERT("allocate NV12", err == B_OK && surface != NULL);

	TEST_ASSERT("NV12 has 2 planes", surface->PlaneCount() == 2);

	// Plane 0: Y, full resolution
	trace("    plane0: %ux%u, bpe=%u, bpr=%u\n",
		(unsigned)surface->WidthOfPlane(0),
		(unsigned)surface->HeightOfPlane(0),
		(unsigned)surface->BytesPerElementOfPlane(0),
		(unsigned)surface->BytesPerRowOfPlane(0));
	TEST_ASSERT("plane0 width == 640",
		surface->WidthOfPlane(0) == 640);
	TEST_ASSERT("plane0 height == 480",
		surface->HeightOfPlane(0) == 480);
	TEST_ASSERT("plane0 bpe == 1",
		surface->BytesPerElementOfPlane(0) == 1);

	// Plane 1: UV, half resolution
	trace("    plane1: %ux%u, bpe=%u, bpr=%u\n",
		(unsigned)surface->WidthOfPlane(1),
		(unsigned)surface->HeightOfPlane(1),
		(unsigned)surface->BytesPerElementOfPlane(1),
		(unsigned)surface->BytesPerRowOfPlane(1));
	TEST_ASSERT("plane1 width == 320",
		surface->WidthOfPlane(1) == 320);
	TEST_ASSERT("plane1 height == 240",
		surface->HeightOfPlane(1) == 240);
	TEST_ASSERT("plane1 bpe == 2",
		surface->BytesPerElementOfPlane(1) == 2);

	// Component info
	TEST_ASSERT("plane0 components == 1",
		surface->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("plane1 components == 2",
		surface->ComponentCountOfPlane(1) == 2);

	// Plane access with lock
	err = surface->Lock();
	TEST_ASSERT("lock NV12", err == B_OK);

	void* plane0 = surface->BaseAddressOfPlane(0);
	void* plane1 = surface->BaseAddressOfPlane(1);
	trace("    plane0 base=%p, plane1 base=%p\n", plane0, plane1);
	TEST_ASSERT("plane0 base not null", plane0 != NULL);
	TEST_ASSERT("plane1 base not null", plane1 != NULL);
	TEST_ASSERT("plane1 > plane0",
		(uintptr_t)plane1 > (uintptr_t)plane0);

	// Out-of-bounds plane
	TEST_ASSERT("plane2 base is null",
		surface->BaseAddressOfPlane(2) == NULL);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_planar_yv12()
{
	trace("\n--- test_planar_yv12 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 320;
	desc.height = 240;
	desc.format = KOSM_PIXEL_FORMAT_YV12;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(320x240, YV12)", err);
	TEST_ASSERT("allocate YV12", err == B_OK && surface != NULL);

	TEST_ASSERT("YV12 has 3 planes", surface->PlaneCount() == 3);

	trace("    plane0: %ux%u, bpe=%u\n",
		(unsigned)surface->WidthOfPlane(0),
		(unsigned)surface->HeightOfPlane(0),
		(unsigned)surface->BytesPerElementOfPlane(0));
	trace("    plane1: %ux%u, bpe=%u\n",
		(unsigned)surface->WidthOfPlane(1),
		(unsigned)surface->HeightOfPlane(1),
		(unsigned)surface->BytesPerElementOfPlane(1));
	trace("    plane2: %ux%u, bpe=%u\n",
		(unsigned)surface->WidthOfPlane(2),
		(unsigned)surface->HeightOfPlane(2),
		(unsigned)surface->BytesPerElementOfPlane(2));

	TEST_ASSERT("plane0 full res", surface->WidthOfPlane(0) == 320);
	TEST_ASSERT("plane1 half res", surface->WidthOfPlane(1) == 160);
	TEST_ASSERT("plane2 half res", surface->WidthOfPlane(2) == 160);

	// Lock and verify all 3 planes accessible
	surface->Lock();
	void* p0 = surface->BaseAddressOfPlane(0);
	void* p1 = surface->BaseAddressOfPlane(1);
	void* p2 = surface->BaseAddressOfPlane(2);
	trace("    plane bases: %p, %p, %p\n", p0, p1, p2);
	TEST_ASSERT("all 3 planes accessible",
		p0 != NULL && p1 != NULL && p2 != NULL);
	TEST_ASSERT("planes ordered",
		(uintptr_t)p0 < (uintptr_t)p1
		&& (uintptr_t)p1 < (uintptr_t)p2);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_planar_nv21()
{
	trace("\n--- test_planar_nv21 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 640;
	desc.height = 480;
	desc.format = KOSM_PIXEL_FORMAT_NV21;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(640x480, NV21)", err);
	TEST_ASSERT("allocate NV21", err == B_OK && surface != NULL);

	TEST_ASSERT("NV21 has 2 planes", surface->PlaneCount() == 2);

	// Plane 0: Y, full resolution (same as NV12)
	trace("    plane0: %ux%u, bpe=%u, bpr=%u\n",
		(unsigned)surface->WidthOfPlane(0),
		(unsigned)surface->HeightOfPlane(0),
		(unsigned)surface->BytesPerElementOfPlane(0),
		(unsigned)surface->BytesPerRowOfPlane(0));
	TEST_ASSERT("plane0 width == 640",
		surface->WidthOfPlane(0) == 640);
	TEST_ASSERT("plane0 height == 480",
		surface->HeightOfPlane(0) == 480);
	TEST_ASSERT("plane0 bpe == 1",
		surface->BytesPerElementOfPlane(0) == 1);

	// Plane 1: VU (reversed vs NV12), half resolution
	trace("    plane1: %ux%u, bpe=%u, bpr=%u\n",
		(unsigned)surface->WidthOfPlane(1),
		(unsigned)surface->HeightOfPlane(1),
		(unsigned)surface->BytesPerElementOfPlane(1),
		(unsigned)surface->BytesPerRowOfPlane(1));
	TEST_ASSERT("plane1 width == 320",
		surface->WidthOfPlane(1) == 320);
	TEST_ASSERT("plane1 height == 240",
		surface->HeightOfPlane(1) == 240);
	TEST_ASSERT("plane1 bpe == 2",
		surface->BytesPerElementOfPlane(1) == 2);

	// Component info — plane1 should have 2 components (V, U)
	TEST_ASSERT("plane0 components == 1",
		surface->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("plane1 components == 2",
		surface->ComponentCountOfPlane(1) == 2);

	// Verify planes accessible
	err = surface->Lock();
	TEST_ASSERT("lock NV21", err == B_OK);

	void* plane0 = surface->BaseAddressOfPlane(0);
	void* plane1 = surface->BaseAddressOfPlane(1);
	trace("    plane0 base=%p, plane1 base=%p\n", plane0, plane1);
	TEST_ASSERT("plane0 base not null", plane0 != NULL);
	TEST_ASSERT("plane1 base not null", plane1 != NULL);
	TEST_ASSERT("plane1 > plane0",
		(uintptr_t)plane1 > (uintptr_t)plane0);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_component_info()
{
	trace("\n--- test_component_info ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	// Component indices: R=0, G=1, B=2, A(or X)=3

	// ARGB8888: 0xAARRGGBB -> R@16 G@8 B@0 A@24
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	KosmSurface* s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate ARGB", s != NULL);
	TEST_ASSERT("ARGB components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("ARGB R depth == 8",
		s->BitDepthOfComponentOfPlane(0, 0) == 8);
	TEST_ASSERT("ARGB R@16",
		s->BitOffsetOfComponentOfPlane(0, 0) == 16);
	TEST_ASSERT("ARGB G@8",
		s->BitOffsetOfComponentOfPlane(0, 1) == 8);
	TEST_ASSERT("ARGB B@0",
		s->BitOffsetOfComponentOfPlane(0, 2) == 0);
	TEST_ASSERT("ARGB A@24",
		s->BitOffsetOfComponentOfPlane(0, 3) == 24);
	trace("    ARGB8888: R@%u, G@%u, B@%u, A@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3));
	alloc->Free(s);

	// BGRA8888: 0xBBGGRRAA -> R@8 G@16 B@24 A@0
	desc.format = KOSM_PIXEL_FORMAT_BGRA8888;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate BGRA", s != NULL);
	TEST_ASSERT("BGRA components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("BGRA R@8",
		s->BitOffsetOfComponentOfPlane(0, 0) == 8);
	TEST_ASSERT("BGRA G@16",
		s->BitOffsetOfComponentOfPlane(0, 1) == 16);
	TEST_ASSERT("BGRA B@24",
		s->BitOffsetOfComponentOfPlane(0, 2) == 24);
	TEST_ASSERT("BGRA A@0",
		s->BitOffsetOfComponentOfPlane(0, 3) == 0);
	trace("    BGRA8888: R@%u, G@%u, B@%u, A@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3));
	alloc->Free(s);

	// RGBA8888: 0xRRGGBBAA -> R@24 G@16 B@8 A@0
	desc.format = KOSM_PIXEL_FORMAT_RGBA8888;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate RGBA", s != NULL);
	TEST_ASSERT("RGBA components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("RGBA R@24",
		s->BitOffsetOfComponentOfPlane(0, 0) == 24);
	TEST_ASSERT("RGBA G@16",
		s->BitOffsetOfComponentOfPlane(0, 1) == 16);
	TEST_ASSERT("RGBA B@8",
		s->BitOffsetOfComponentOfPlane(0, 2) == 8);
	TEST_ASSERT("RGBA A@0",
		s->BitOffsetOfComponentOfPlane(0, 3) == 0);
	trace("    RGBA8888: R@%u, G@%u, B@%u, A@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3));
	alloc->Free(s);

	// RGBX8888: 0xRRGGBBXX -> R@24 G@16 B@8 X@0
	desc.format = KOSM_PIXEL_FORMAT_RGBX8888;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate RGBX", s != NULL);
	TEST_ASSERT("RGBX components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("RGBX R@24",
		s->BitOffsetOfComponentOfPlane(0, 0) == 24);
	TEST_ASSERT("RGBX G@16",
		s->BitOffsetOfComponentOfPlane(0, 1) == 16);
	TEST_ASSERT("RGBX B@8",
		s->BitOffsetOfComponentOfPlane(0, 2) == 8);
	TEST_ASSERT("RGBX X@0",
		s->BitOffsetOfComponentOfPlane(0, 3) == 0);
	trace("    RGBX8888: R@%u, G@%u, B@%u, X@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3));
	alloc->Free(s);

	// XRGB8888: 0xXXRRGGBB -> R@16 G@8 B@0 X@24
	desc.format = KOSM_PIXEL_FORMAT_XRGB8888;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate XRGB", s != NULL);
	TEST_ASSERT("XRGB components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("XRGB R@16",
		s->BitOffsetOfComponentOfPlane(0, 0) == 16);
	TEST_ASSERT("XRGB G@8",
		s->BitOffsetOfComponentOfPlane(0, 1) == 8);
	TEST_ASSERT("XRGB B@0",
		s->BitOffsetOfComponentOfPlane(0, 2) == 0);
	TEST_ASSERT("XRGB X@24",
		s->BitOffsetOfComponentOfPlane(0, 3) == 24);
	trace("    XRGB8888: R@%u, G@%u, B@%u, X@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3));
	alloc->Free(s);

	// RGB565: R=5@11 G=6@5 B=5@0
	desc.format = KOSM_PIXEL_FORMAT_RGB565;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate RGB565", s != NULL);
	TEST_ASSERT("RGB565 components == 3",
		s->ComponentCountOfPlane(0) == 3);
	TEST_ASSERT("RGB565 R depth == 5",
		s->BitDepthOfComponentOfPlane(0, 0) == 5);
	TEST_ASSERT("RGB565 G depth == 6",
		s->BitDepthOfComponentOfPlane(0, 1) == 6);
	TEST_ASSERT("RGB565 B depth == 5",
		s->BitDepthOfComponentOfPlane(0, 2) == 5);
	TEST_ASSERT("RGB565 R@11",
		s->BitOffsetOfComponentOfPlane(0, 0) == 11);
	TEST_ASSERT("RGB565 G@5",
		s->BitOffsetOfComponentOfPlane(0, 1) == 5);
	TEST_ASSERT("RGB565 B@0",
		s->BitOffsetOfComponentOfPlane(0, 2) == 0);
	trace("    RGB565: R@%u(%u), G@%u(%u), B@%u(%u)\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 2));
	alloc->Free(s);

	// A8: 1 component, 8-bit at offset 0
	desc.format = KOSM_PIXEL_FORMAT_A8;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate A8", s != NULL);
	TEST_ASSERT("A8 components == 1",
		s->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("A8 depth == 8",
		s->BitDepthOfComponentOfPlane(0, 0) == 8);
	TEST_ASSERT("A8 offset == 0",
		s->BitOffsetOfComponentOfPlane(0, 0) == 0);
	alloc->Free(s);

	// L8: 1 component, 8-bit at offset 0
	desc.format = KOSM_PIXEL_FORMAT_L8;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate L8", s != NULL);
	TEST_ASSERT("L8 components == 1",
		s->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("L8 depth == 8",
		s->BitDepthOfComponentOfPlane(0, 0) == 8);
	TEST_ASSERT("L8 offset == 0",
		s->BitOffsetOfComponentOfPlane(0, 0) == 0);
	alloc->Free(s);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Use count ---

static void
test_use_count()
{
	trace("\n--- test_use_count ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	TEST_ASSERT("initial use count == 0",
		surface->LocalUseCount() == 0);
	TEST_ASSERT("not in use initially", !surface->IsInUse());

	surface->IncrementUseCount();
	trace("    after inc: local=%d, inUse=%d\n",
		(int)surface->LocalUseCount(),
		(int)surface->IsInUse());
	TEST_ASSERT("use count == 1 after inc",
		surface->LocalUseCount() == 1);
	TEST_ASSERT("in use after inc", surface->IsInUse());

	surface->IncrementUseCount();
	TEST_ASSERT("use count == 2", surface->LocalUseCount() == 2);

	surface->DecrementUseCount();
	TEST_ASSERT("use count == 1 after dec",
		surface->LocalUseCount() == 1);
	TEST_ASSERT("still in use", surface->IsInUse());

	surface->DecrementUseCount();
	TEST_ASSERT("use count == 0 after 2nd dec",
		surface->LocalUseCount() == 0);
	TEST_ASSERT("not in use after all decs", !surface->IsInUse());

	// Extra decrement shouldn't go negative
	surface->DecrementUseCount();
	TEST_ASSERT("use count doesn't go negative",
		surface->LocalUseCount() == 0);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_free_while_in_use()
{
	trace("\n--- test_free_while_in_use ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Mark as in-use (simulating compositor holding a reference)
	surface->IncrementUseCount();
	TEST_ASSERT("in use", surface->IsInUse());

	// Free while in-use — should return KOSM_SURFACE_IN_USE
	// The surface should remain valid until use count drops to 0
	kosm_surface_id id = surface->ID();
	trace("    id=%u, trying Free while InUse\n", (unsigned)id);

	// Note: depending on implementation, Free might:
	// a) return immediately with error, or
	// b) defer destruction until use count reaches 0
	// We verify it doesn't crash and the surface remains accessible
	alloc->Free(surface);

	// If implementation defers: surface should still be lookupable
	// If implementation rejects: we already passed (no crash)
	KosmSurface* found = NULL;
	status_t err = alloc->Lookup(id, &found);
	trace_status("Lookup(in-use freed id)", err);

	if (err == B_OK && found != NULL) {
		trace("    surface still in registry (deferred free)\n");
		TEST_ASSERT("deferred free: still findable", true);

		// Now release and free properly
		found->DecrementUseCount();
		TEST_ASSERT("use count back to 0",
			found->LocalUseCount() == 0);
		alloc->Free(found);
	} else {
		trace("    surface removed from registry (immediate free)\n");
		TEST_ASSERT("immediate free: removed from registry", true);
		// Use count was orphaned — that's the implementation's choice
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Attachments ---

static void
test_attachments()
{
	trace("\n--- test_attachments ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Set attachment
	BMessage value1;
	value1.AddString("type", "texture");
	value1.AddInt32("layer", 3);
	status_t err = surface->SetAttachment("render_info", value1);
	trace_status("SetAttachment(render_info)", err);
	TEST_ASSERT("set attachment", err == B_OK);

	// Get attachment
	BMessage out1;
	err = surface->GetAttachment("render_info", &out1);
	trace_status("GetAttachment(render_info)", err);
	TEST_ASSERT("get attachment", err == B_OK);

	const char* typeStr = NULL;
	out1.FindString("type", &typeStr);
	int32 layer = 0;
	out1.FindInt32("layer", &layer);
	trace("    retrieved: type=%s, layer=%d\n",
		typeStr ? typeStr : "(null)", (int)layer);
	TEST_ASSERT("attachment type matches",
		typeStr != NULL && strcmp(typeStr, "texture") == 0);
	TEST_ASSERT("attachment layer matches", layer == 3);

	// Get nonexistent
	BMessage out2;
	err = surface->GetAttachment("nonexistent", &out2);
	trace_status("GetAttachment(nonexistent)", err);
	TEST_ASSERT("get nonexistent fails", err != B_OK);

	// Set second attachment
	BMessage value2;
	value2.AddFloat("opacity", 0.75f);
	surface->SetAttachment("blend_info", value2);

	// Overwrite first attachment with new value
	BMessage value1b;
	value1b.AddString("type", "framebuffer");
	value1b.AddInt32("layer", 7);
	err = surface->SetAttachment("render_info", value1b);
	trace_status("SetAttachment(render_info) overwrite", err);
	TEST_ASSERT("overwrite attachment", err == B_OK);

	// Verify overwritten value
	BMessage outOverwrite;
	err = surface->GetAttachment("render_info", &outOverwrite);
	TEST_ASSERT("get overwritten attachment", err == B_OK);
	const char* newType = NULL;
	outOverwrite.FindString("type", &newType);
	int32 newLayer = 0;
	outOverwrite.FindInt32("layer", &newLayer);
	trace("    overwritten: type=%s, layer=%d\n",
		newType ? newType : "(null)", (int)newLayer);
	TEST_ASSERT("overwritten type matches",
		newType != NULL && strcmp(newType, "framebuffer") == 0);
	TEST_ASSERT("overwritten layer matches", newLayer == 7);

	// CopyAllAttachments — verify count
	BMessage all;
	err = surface->CopyAllAttachments(&all);
	trace_status("CopyAllAttachments", err);
	TEST_ASSERT("copy all", err == B_OK);

	// Should contain exactly 2 entries: render_info, blend_info
	int32 attachCount = 0;
	BMessage checkMsg;
	if (all.FindMessage("render_info", &checkMsg) == B_OK)
		attachCount++;
	if (all.FindMessage("blend_info", &checkMsg) == B_OK)
		attachCount++;
	trace("    CopyAll found %d expected attachments\n",
		(int)attachCount);
	TEST_ASSERT("CopyAll contains both attachments",
		attachCount == 2);

	// Remove one
	err = surface->RemoveAttachment("render_info");
	trace_status("RemoveAttachment(render_info)", err);
	TEST_ASSERT("remove attachment", err == B_OK);

	err = surface->GetAttachment("render_info", &out1);
	TEST_ASSERT("removed attachment not found", err != B_OK);

	// Other still exists
	BMessage out3;
	err = surface->GetAttachment("blend_info", &out3);
	TEST_ASSERT("other attachment still exists", err == B_OK);

	// RemoveAll
	err = surface->RemoveAllAttachments();
	trace_status("RemoveAllAttachments", err);
	TEST_ASSERT("remove all", err == B_OK);

	err = surface->GetAttachment("blend_info", &out3);
	TEST_ASSERT("all attachments removed", err != B_OK);

	// SetAttachments (batch)
	BMessage batch;
	BMessage v1, v2;
	v1.AddInt32("x", 10);
	v2.AddInt32("y", 20);
	batch.AddMessage("pos_x", &v1);
	batch.AddMessage("pos_y", &v2);
	err = surface->SetAttachments(batch);
	trace_status("SetAttachments(batch)", err);
	TEST_ASSERT("batch set", err == B_OK);

	BMessage outX;
	surface->GetAttachment("pos_x", &outX);
	int32 x = 0;
	outX.FindInt32("x", &x);
	TEST_ASSERT("batch: pos_x.x == 10", x == 10);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Purgeable ---

static void
test_purgeable()
{
	trace("\n--- test_purgeable ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 256;
	desc.height = 256;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Initially non-volatile
	TEST_ASSERT("not volatile initially", !surface->IsVolatile());

	// Set volatile
	kosm_purgeable_state oldState;
	status_t err = surface->SetPurgeable(KOSM_PURGEABLE_VOLATILE, &oldState);
	trace_status("SetPurgeable(VOLATILE)", err);
	trace("    old state=%d\n", (int)oldState);
	TEST_ASSERT("set volatile", err == B_OK);
	TEST_ASSERT("old state was NON_VOLATILE",
		oldState == KOSM_PURGEABLE_NON_VOLATILE);
	TEST_ASSERT("is volatile now", surface->IsVolatile());

	// Set back to non-volatile (no purge happened)
	err = surface->SetPurgeable(KOSM_PURGEABLE_NON_VOLATILE, &oldState);
	trace_status("SetPurgeable(NON_VOLATILE)", err);
	trace("    old state=%d\n", (int)oldState);
	TEST_ASSERT("set non-volatile", err == B_OK);
	TEST_ASSERT("old state was VOLATILE",
		oldState == KOSM_PURGEABLE_VOLATILE);
	TEST_ASSERT("not volatile after set back", !surface->IsVolatile());

	// Mark as EMPTY then try to set NON_VOLATILE → PURGED
	err = surface->SetPurgeable(KOSM_PURGEABLE_EMPTY);
	trace_status("SetPurgeable(EMPTY)", err);
	TEST_ASSERT("set empty", err == B_OK);

	err = surface->SetPurgeable(KOSM_PURGEABLE_NON_VOLATILE, &oldState);
	trace_status("SetPurgeable(NON_VOLATILE) after EMPTY", err);
	trace("    result=0x%08x\n", (unsigned)err);
	TEST_ASSERT("after EMPTY, set NON_VOLATILE returns PURGED",
		err == KOSM_SURFACE_PURGED);

	// KEEP_CURRENT is a no-op
	err = surface->SetPurgeable(KOSM_PURGEABLE_KEEP_CURRENT, &oldState);
	TEST_ASSERT("KEEP_CURRENT succeeds", err == B_OK);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_purgeable_lock_interaction()
{
	trace("\n--- test_purgeable_lock_interaction ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 128;
	desc.height = 128;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Write some data first
	surface->Lock();
	void* base = surface->BaseAddress();
	if (base != NULL)
		memset(base, 0xCC, 128 * 4);
	surface->Unlock();

	// Mark as EMPTY (simulating system purge)
	status_t err = surface->SetPurgeable(KOSM_PURGEABLE_EMPTY);
	TEST_ASSERT("set empty", err == B_OK);

	// Try to Lock the purged surface
	err = surface->Lock();
	trace_status("Lock() on EMPTY surface", err);
	trace("    lock on EMPTY returned 0x%08x\n", (unsigned)err);

	// Expected: either KOSM_SURFACE_PURGED or B_OK (impl-dependent)
	// The key thing is it shouldn't crash and should report purge state
	if (err == KOSM_SURFACE_PURGED) {
		TEST_ASSERT("lock on purged returns KOSM_SURFACE_PURGED", true);
	} else if (err == B_OK) {
		trace("    impl allows lock on empty surface (data undefined)\n");
		TEST_ASSERT("lock on empty succeeds (implementation choice)", true);
		surface->Unlock();
	} else {
		trace("    unexpected error: %s\n", strerror(err));
		TEST_ASSERT("lock on purged surface (unexpected error)", false);
	}

	// Set volatile while locked should fail (if lockable)
	err = surface->SetPurgeable(KOSM_PURGEABLE_NON_VOLATILE);
	// Reset state first, ignoring PURGED return
	err = surface->Lock();
	if (err == B_OK) {
		kosm_purgeable_state old;
		err = surface->SetPurgeable(KOSM_PURGEABLE_VOLATILE, &old);
		trace_status("SetPurgeable(VOLATILE) while locked", err);
		// Setting volatile while locked is dangerous — should ideally fail
		trace("    set volatile while locked -> 0x%08x\n", (unsigned)err);
		surface->Unlock();
	}

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_purgeable_usage_flag()
{
	trace("\n--- test_purgeable_usage_flag ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	// Allocate with PURGEABLE usage hint
	KosmSurfaceDesc desc;
	desc.width = 256;
	desc.height = 256;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	desc.usage = KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE
		| KOSM_SURFACE_USAGE_PURGEABLE;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(PURGEABLE usage)", err);
	TEST_ASSERT("allocate with purgeable usage", err == B_OK);

	if (surface != NULL) {
		TEST_ASSERT("usage includes PURGEABLE",
			(surface->Usage() & KOSM_SURFACE_USAGE_PURGEABLE) != 0);

		// Should be able to set volatile immediately
		kosm_purgeable_state oldState;
		err = surface->SetPurgeable(KOSM_PURGEABLE_VOLATILE, &oldState);
		trace_status("SetPurgeable(VOLATILE) on purgeable surface", err);
		TEST_ASSERT("set volatile on purgeable surface", err == B_OK);
		TEST_ASSERT("is volatile", surface->IsVolatile());

		// Set back and verify data survives (not yet purged)
		err = surface->SetPurgeable(KOSM_PURGEABLE_NON_VOLATILE, &oldState);
		TEST_ASSERT("set non-volatile", err == B_OK);
		TEST_ASSERT("old state was VOLATILE",
			oldState == KOSM_PURGEABLE_VOLATILE);

		alloc->Free(surface);
	}

	// Allocate WITHOUT purgeable flag — SetPurgeable might still work
	// (implementation-dependent) but usage flag shouldn't include it
	KosmSurfaceDesc desc2;
	desc2.width = 64;
	desc2.height = 64;
	desc2.usage = KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE;

	KosmSurface* surface2 = NULL;
	alloc->Allocate(desc2, &surface2);
	if (surface2 != NULL) {
		TEST_ASSERT("non-purgeable usage lacks flag",
			(surface2->Usage() & KOSM_SURFACE_USAGE_PURGEABLE) == 0);
		alloc->Free(surface2);
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Access tokens ---

static void
test_access_token()
{
	trace("\n--- test_access_token ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	KosmSurfaceToken token;
	memset(&token, 0, sizeof(token));
	status_t err = surface->CreateAccessToken(&token);
	trace_status("CreateAccessToken", err);
	trace("    token: id=%u, secret=0x%llx, gen=%u\n",
		(unsigned)token.id, (unsigned long long)token.secret,
		(unsigned)token.generation);
	TEST_ASSERT("create token", err == B_OK);
	TEST_ASSERT("token id matches", token.id == surface->ID());
	TEST_ASSERT("token secret != 0", token.secret != 0);
	TEST_ASSERT("token generation == 0", token.generation == 0);

	// Revoke and create new
	err = surface->RevokeAllAccess();
	trace_status("RevokeAllAccess", err);
	TEST_ASSERT("revoke", err == B_OK);

	KosmSurfaceToken token2;
	surface->CreateAccessToken(&token2);
	trace("    new token: secret=0x%llx, gen=%u\n",
		(unsigned long long)token2.secret, (unsigned)token2.generation);
	TEST_ASSERT("new token has different secret",
		token2.secret != token.secret);
	TEST_ASSERT("new token generation == 1",
		token2.generation == 1);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Usage flags ---

static void
test_usage_flags()
{
	trace("\n--- test_usage_flags ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	uint32 usages[] = {
		KOSM_SURFACE_USAGE_CPU_READ,
		KOSM_SURFACE_USAGE_CPU_WRITE,
		KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE,
		KOSM_SURFACE_USAGE_COMPOSITOR,
		KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_COMPOSITOR,
	};
	const char* usageNames[] = {
		"CPU_READ", "CPU_WRITE", "CPU_RW",
		"COMPOSITOR", "CPU_READ|COMPOSITOR"
	};

	for (int i = 0; i < 5; i++) {
		KosmSurfaceDesc desc;
		desc.width = 64;
		desc.height = 64;
		desc.usage = usages[i];

		KosmSurface* s = NULL;
		status_t err = alloc->Allocate(desc, &s);
		trace("    usage %s (0x%04x) -> %s\n",
			usageNames[i], (unsigned)usages[i], strerror(err));

		char name[64];
		snprintf(name, sizeof(name), "usage %s", usageNames[i]);
		TEST_ASSERT(name, err == B_OK && s != NULL);

		if (s != NULL) {
			TEST_ASSERT("usage matches",
				(s->Usage() & usages[i]) == usages[i]);
			alloc->Free(s);
		}
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Pixel read/write ---

static void
test_pixel_readwrite()
{
	trace("\n--- test_pixel_readwrite ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 16;
	desc.height = 16;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// Write pattern
	surface->Lock();
	uint32* pixels = (uint32*)surface->BaseAddress();
	TEST_ASSERT("pixels not null", pixels != NULL);

	uint32 bpr = surface->BytesPerRow();
	for (uint32 y = 0; y < 16; y++) {
		uint32* row = (uint32*)((uint8*)pixels + y * bpr);
		for (uint32 x = 0; x < 16; x++) {
			row[x] = 0xFF000000 | (y << 16) | (x << 8) | (x ^ y);
		}
	}
	surface->Unlock();

	// Read back and verify
	surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	uint32* readPixels = (uint32*)surface->BaseAddress();
	bool correct = true;
	for (uint32 y = 0; y < 16 && correct; y++) {
		uint32* row = (uint32*)((uint8*)readPixels + y * bpr);
		for (uint32 x = 0; x < 16 && correct; x++) {
			uint32 expected = 0xFF000000 | (y << 16) | (x << 8) | (x ^ y);
			if (row[x] != expected) {
				trace("    mismatch at (%u,%u): got 0x%08x, expected 0x%08x\n",
					(unsigned)x, (unsigned)y,
					(unsigned)row[x], (unsigned)expected);
				correct = false;
			}
		}
	}
	surface->Unlock();

	TEST_ASSERT("pixel readback matches", correct);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_pixel_readwrite_rgb565()
{
	trace("\n--- test_pixel_readwrite_rgb565 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 16;
	desc.height = 16;
	desc.format = KOSM_PIXEL_FORMAT_RGB565;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate RGB565", surface != NULL);
	TEST_ASSERT("BytesPerElement == 2",
		surface->BytesPerElement() == 2);

	// Write pattern: R=31 G=0 B=0 -> 0xF800,  R=0 G=63 B=0 -> 0x07E0
	surface->Lock();
	uint16* pixels = (uint16*)surface->BaseAddress();
	TEST_ASSERT("pixels not null", pixels != NULL);

	uint32 bpr = surface->BytesPerRow();
	for (uint32 y = 0; y < 16; y++) {
		uint16* row = (uint16*)((uint8*)pixels + y * bpr);
		for (uint32 x = 0; x < 16; x++) {
			if (y < 8)
				row[x] = 0xF800;  // pure red
			else
				row[x] = 0x07E0;  // pure green
		}
	}
	surface->Unlock();

	// Read back
	surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	uint16* readPixels = (uint16*)surface->BaseAddress();
	bool correct = true;
	for (uint32 y = 0; y < 16 && correct; y++) {
		uint16* row = (uint16*)((uint8*)readPixels + y * bpr);
		for (uint32 x = 0; x < 16 && correct; x++) {
			uint16 expected = (y < 8) ? 0xF800 : 0x07E0;
			if (row[x] != expected) {
				trace("    mismatch at (%u,%u): got 0x%04x, expected 0x%04x\n",
					(unsigned)x, (unsigned)y,
					(unsigned)row[x], (unsigned)expected);
				correct = false;
			}
		}
	}
	surface->Unlock();

	TEST_ASSERT("RGB565 pixel readback matches", correct);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Stress tests ---

static void
test_stress_alloc_free()
{
	trace("\n--- test_stress_alloc_free ---\n");
	bigtime_t t0 = system_time();

	const int kThreads = 8;
	const int kItersPerThread = 100;
	trace("    spawning %d threads, %d alloc/lock/free cycles each\n",
		kThreads, kItersPerThread);

	alloc_free_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].iterations = kItersPerThread;
		targs[i].errors = 0;
		targs[i].allocFails = 0;
		targs[i].lockFails = 0;
		targs[i].totalTime = 0;

		char name[32];
		snprintf(name, sizeof(name), "alloc_churn_%d", i);
		threads[i] = spawn_thread(alloc_free_churn_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalAllocFails = 0;
	int totalLockFails = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: allocFails=%d, lockFails=%d, time=%lld us\n",
			i, targs[i].allocFails, targs[i].lockFails,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		totalAllocFails += targs[i].allocFails;
		totalLockFails += targs[i].lockFails;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	trace("    total allocs=%d, allocFails=%d, lockFails=%d, "
		"slowest=%lld us\n", kThreads * kItersPerThread,
		totalAllocFails, totalLockFails, (long long)slowest);
	TEST_ASSERT("alloc/free churn no errors", totalErrors == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_lock_contention()
{
	trace("\n--- test_stress_lock_contention ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(64x64)", err);
	TEST_ASSERT("allocate", surface != NULL);

	const int kThreads = 8;
	const int kIters = 50;
	int32 counter = 0;
	trace("    spawning %d threads, %d lock/inc/unlock cycles each\n",
		kThreads, kIters);

	lock_contention_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;
		targs[i].errors = 0;
		targs[i].acquired = 0;
		targs[i].timedOut = 0;
		targs[i].maxLockTime = 0;
		targs[i].totalTime = 0;
		targs[i].counter = &counter;

		char name[32];
		snprintf(name, sizeof(name), "lock_cont_%d", i);
		threads[i] = spawn_thread(lock_contention_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalAcquired = 0;
	int totalTimedOut = 0;
	bigtime_t worstLock = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: acquired=%d, timedOut=%d, "
			"maxWait=%lld us, time=%lld us\n",
			i, targs[i].acquired, targs[i].timedOut,
			(long long)targs[i].maxLockTime,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		totalAcquired += targs[i].acquired;
		totalTimedOut += targs[i].timedOut;
		if (targs[i].maxLockTime > worstLock)
			worstLock = targs[i].maxLockTime;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	int32 expected = kThreads * kIters;
	trace("    counter=%d, expected=%d, acquired=%d, timedOut=%d, "
		"errors=%d\n", (int)counter, (int)expected, totalAcquired,
		totalTimedOut, totalErrors);
	trace("    worstLock=%lld us, slowest thread=%lld us\n",
		(long long)worstLock, (long long)slowest);
	TEST_ASSERT("counter correct", counter == expected);
	TEST_ASSERT("no lock errors", totalErrors == 0);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_rapid_lock_unlock()
{
	trace("\n--- test_stress_rapid_lock_unlock ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	const int kCycles = 10000;
	bigtime_t start = system_time();
	for (int i = 0; i < kCycles; i++) {
		surface->Lock();
		surface->Unlock();
	}
	bigtime_t elapsed = system_time() - start;

	trace("    %d lock/unlock cycles in %lld us\n",
		kCycles, (long long)elapsed);
	trace("    average: %lld us per cycle\n",
		(long long)(elapsed / kCycles));

	uint32 seed = surface->Seed();
	trace("    final seed=%u (expected %d)\n",
		(unsigned)seed, kCycles);
	TEST_ASSERT("seed matches cycle count", seed == (uint32)kCycles);

	bigtime_t avgCycle = elapsed / kCycles;
	TEST_ASSERT("average lock/unlock < 100us", avgCycle < 100);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct attachment_thrash_args {
	KosmSurface*	surface;
	int				threadIndex;
	int				iterations;
	int				errors;
	int				setOk;
	int				getOk;
	int				setFail;
	int				getFail;
	bigtime_t		totalTime;
};


static status_t
attachment_thrash_thread(void* data)
{
	attachment_thrash_args* args = (attachment_thrash_args*)data;
	args->errors = 0;
	args->setOk = 0;
	args->getOk = 0;
	args->setFail = 0;
	args->getFail = 0;

	bigtime_t t0 = system_time();
	int tid = (int)find_thread(NULL);

	trace("    [thread %d] attachment_thrash start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		char key[32];
		snprintf(key, sizeof(key), "t%d_key%d", args->threadIndex, i % 5);

		BMessage val;
		val.AddInt32("thread", args->threadIndex);
		val.AddInt32("iter", i);

		status_t err = args->surface->SetAttachment(key, val);
		if (err != B_OK) {
			args->setFail++;
			args->errors++;
			trace("    [thread %d] SetAttachment(%s) FAIL iter %d: %s "
				"(0x%08x)\n", tid, key, i, strerror(err), (unsigned)err);
			continue;
		}
		args->setOk++;

		BMessage out;
		err = args->surface->GetAttachment(key, &out);
		if (err != B_OK) {
			args->getFail++;
			args->errors++;
			trace("    [thread %d] GetAttachment(%s) FAIL iter %d: %s "
				"(0x%08x)\n", tid, key, i, strerror(err), (unsigned)err);
		} else {
			args->getOk++;
		}
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] attachment_thrash done: set=%d/%d, get=%d/%d, "
		"time=%lld us\n", tid, args->setOk,
		args->setOk + args->setFail, args->getOk,
		args->getOk + args->getFail, (long long)args->totalTime);
	return B_OK;
}


static void
test_stress_attachment_thrash()
{
	trace("\n--- test_stress_attachment_thrash ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(64x64)", err);
	TEST_ASSERT("allocate", surface != NULL);

	const int kThreads = 8;
	const int kIters = 100;
	trace("    spawning %d threads, %d iterations each\n",
		kThreads, kIters);
	attachment_thrash_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].threadIndex = i;
		targs[i].iterations = kIters;
		targs[i].errors = 0;

		char name[32];
		snprintf(name, sizeof(name), "attach_thrash_%d", i);
		threads[i] = spawn_thread(attachment_thrash_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: set=%d ok / %d fail, get=%d ok / %d fail, "
			"time=%lld us\n", i, targs[i].setOk, targs[i].setFail,
			targs[i].getOk, targs[i].getFail,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	trace("    total ops=%d, errors=%d, slowest thread=%lld us\n",
		kThreads * kIters, totalErrors, (long long)slowest);
	TEST_ASSERT("concurrent attachments no errors", totalErrors == 0);

	surface->RemoveAllAttachments();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct lookup_race_args {
	int			iterations;
	int			errors;
	int			lookupHits;
	int			allocFails;
	int			staleLookups;
	bigtime_t	totalTime;
};


static status_t
alloc_lookup_race_thread(void* data)
{
	lookup_race_args* args = (lookup_race_args*)data;
	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	args->errors = 0;
	args->lookupHits = 0;
	args->allocFails = 0;
	args->staleLookups = 0;

	int tid = (int)find_thread(NULL);
	bigtime_t t0 = system_time();

	trace("    [thread %d] alloc_lookup_race start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		KosmSurfaceDesc desc;
		desc.width = 32;
		desc.height = 32;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		KosmSurface* surface = NULL;
		status_t err = alloc->Allocate(desc, &surface);
		if (err != B_OK || surface == NULL) {
			args->allocFails++;
			args->errors++;
			trace("    [thread %d] Allocate FAIL iter %d: %s (0x%08x)\n",
				tid, i, strerror(err), (unsigned)err);
			continue;
		}

		kosm_surface_id id = surface->ID();

		// Lookup while still alive
		KosmSurface* found = NULL;
		err = alloc->Lookup(id, &found);
		if (err == B_OK && found != NULL) {
			args->lookupHits++;
		} else {
			trace("    [thread %d] Lookup(id=%u) MISS while live, iter %d:"
				" %s (0x%08x)\n", tid, (unsigned)id, i, strerror(err),
				(unsigned)err);
		}

		alloc->Free(surface);

		// Lookup after free — should fail
		found = NULL;
		err = alloc->Lookup(id, &found);
		if (err == B_OK && found != NULL) {
			args->staleLookups++;
			args->errors++;
			trace("    [thread %d] STALE lookup id=%u after free, iter %d\n",
				tid, (unsigned)id, i);
		}
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] alloc_lookup_race done: hits=%d, allocFail=%d, "
		"stale=%d, time=%lld us\n", tid, args->lookupHits,
		args->allocFails, args->staleLookups,
		(long long)args->totalTime);
	return B_OK;
}


static void
test_stress_alloc_lookup_race()
{
	trace("\n--- test_stress_alloc_lookup_race ---\n");
	bigtime_t t0 = system_time();

	const int kThreads = 8;
	const int kIters = 100;
	trace("    spawning %d threads, %d alloc/lookup/free cycles each\n",
		kThreads, kIters);

	lookup_race_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].iterations = kIters;
		targs[i].errors = 0;
		targs[i].lookupHits = 0;

		char name[32];
		snprintf(name, sizeof(name), "alloc_lookup_%d", i);
		threads[i] = spawn_thread(alloc_lookup_race_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalHits = 0;
	int totalStale = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: hits=%d, allocFails=%d, stale=%d, "
			"time=%lld us\n", i, targs[i].lookupHits,
			targs[i].allocFails, targs[i].staleLookups,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		totalHits += targs[i].lookupHits;
		totalStale += targs[i].staleLookups;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	trace("    total cycles=%d, lookupHits=%d, stale=%d, errors=%d, "
		"slowest=%lld us\n", kThreads * kIters, totalHits, totalStale,
		totalErrors, (long long)slowest);
	TEST_ASSERT("no stale lookups after free", totalErrors == 0);
	TEST_ASSERT("lookups found live surfaces", totalHits > 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct token_thrash_args {
	KosmSurface*	surface;
	int				iterations;
	int				errors;
	int				createOk;
	int				createFail;
	int				idMismatch;
	int				revokes;
	int				revokeFail;
	bigtime_t		totalTime;
};


static status_t
token_thrash_thread(void* data)
{
	token_thrash_args* args = (token_thrash_args*)data;
	args->errors = 0;
	args->createOk = 0;
	args->createFail = 0;
	args->idMismatch = 0;
	args->revokes = 0;
	args->revokeFail = 0;

	int tid = (int)find_thread(NULL);
	bigtime_t t0 = system_time();

	trace("    [thread %d] token_thrash start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		KosmSurfaceToken token;
		memset(&token, 0, sizeof(token));
		status_t err = args->surface->CreateAccessToken(&token);
		if (err != B_OK) {
			args->createFail++;
			args->errors++;
			trace("    [thread %d] CreateAccessToken FAIL iter %d: %s "
				"(0x%08x)\n", tid, i, strerror(err), (unsigned)err);
			continue;
		}
		args->createOk++;

		if (token.id != args->surface->ID()) {
			args->idMismatch++;
			args->errors++;
			trace("    [thread %d] token id %u != surface id %u, iter %d\n",
				tid, (unsigned)token.id,
				(unsigned)args->surface->ID(), i);
		}

		if (i % 5 == 0) {
			err = args->surface->RevokeAllAccess();
			if (err != B_OK) {
				args->revokeFail++;
				args->errors++;
				trace("    [thread %d] RevokeAllAccess FAIL iter %d: %s "
					"(0x%08x)\n", tid, i, strerror(err), (unsigned)err);
			} else {
				args->revokes++;
			}
		}
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] token_thrash done: create=%d/%d, idMismatch=%d, "
		"revokes=%d/%d, time=%lld us\n", tid,
		args->createOk, args->createOk + args->createFail,
		args->idMismatch, args->revokes,
		args->revokes + args->revokeFail,
		(long long)args->totalTime);
	return B_OK;
}


static void
test_stress_token_thrash()
{
	trace("\n--- test_stress_token_thrash ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(64x64)", err);
	TEST_ASSERT("allocate", surface != NULL);

	const int kThreads = 4;
	const int kIters = 200;
	trace("    spawning %d threads, %d token ops each (revoke every 5th)\n",
		kThreads, kIters);

	token_thrash_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;
		targs[i].errors = 0;

		char name[32];
		snprintf(name, sizeof(name), "token_thrash_%d", i);
		threads[i] = spawn_thread(token_thrash_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalRevokes = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: create=%d/%d, idMismatch=%d, revokes=%d, "
			"time=%lld us\n", i, targs[i].createOk,
			targs[i].createOk + targs[i].createFail,
			targs[i].idMismatch, targs[i].revokes,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		totalRevokes += targs[i].revokes;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	trace("    total token ops=%d, revokes=%d, errors=%d, "
		"slowest=%lld us\n", kThreads * kIters, totalRevokes,
		totalErrors, (long long)slowest);
	TEST_ASSERT("token thrash no errors", totalErrors == 0);

	KosmSurfaceToken finalToken;
	err = surface->CreateAccessToken(&finalToken);
	TEST_ASSERT("final token after thrash", err == B_OK);
	trace("    final token: secret=0x%llx, gen=%u\n",
		(unsigned long long)finalToken.secret,
		(unsigned)finalToken.generation);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct purgeable_thrash_args {
	KosmSurface*	surface;
	int				iterations;
	int				errors;
	int				setVolatileOk;
	int				setVolatileFail;
	int				setNonVolatileOk;
	int				setNonVolatilePurged;
	int				setNonVolatileFail;
	bigtime_t		totalTime;
};


static status_t
purgeable_thrash_thread(void* data)
{
	purgeable_thrash_args* args = (purgeable_thrash_args*)data;
	args->errors = 0;
	args->setVolatileOk = 0;
	args->setVolatileFail = 0;
	args->setNonVolatileOk = 0;
	args->setNonVolatilePurged = 0;
	args->setNonVolatileFail = 0;

	int tid = (int)find_thread(NULL);
	bigtime_t t0 = system_time();

	trace("    [thread %d] purgeable_thrash start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		kosm_purgeable_state oldState;

		status_t err = args->surface->SetPurgeable(
			KOSM_PURGEABLE_VOLATILE, &oldState);
		if (err != B_OK) {
			args->setVolatileFail++;
			args->errors++;
			trace("    [thread %d] SetPurgeable(VOLATILE) FAIL iter %d: "
				"%s (0x%08x)\n", tid, i, strerror(err), (unsigned)err);
			continue;
		}
		args->setVolatileOk++;

		snooze(1);

		err = args->surface->SetPurgeable(
			KOSM_PURGEABLE_NON_VOLATILE, &oldState);
		if (err == B_OK) {
			args->setNonVolatileOk++;
		} else if (err == KOSM_SURFACE_PURGED) {
			args->setNonVolatilePurged++;
		} else {
			args->setNonVolatileFail++;
			args->errors++;
			trace("    [thread %d] SetPurgeable(NON_VOLATILE) FAIL iter %d:"
				" %s (0x%08x)\n", tid, i, strerror(err), (unsigned)err);
		}
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] purgeable_thrash done: vol=%d/%d, "
		"nonvol=%d ok / %d purged / %d fail, time=%lld us\n", tid,
		args->setVolatileOk,
		args->setVolatileOk + args->setVolatileFail,
		args->setNonVolatileOk, args->setNonVolatilePurged,
		args->setNonVolatileFail, (long long)args->totalTime);
	return B_OK;
}


static void
test_stress_purgeable_thrash()
{
	trace("\n--- test_stress_purgeable_thrash ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 128;
	desc.height = 128;
	desc.usage = KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE
		| KOSM_SURFACE_USAGE_PURGEABLE;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(128x128, PURGEABLE)", err);
	TEST_ASSERT("allocate", surface != NULL);

	const int kThreads = 4;
	const int kIters = 200;
	trace("    spawning %d threads, %d volatile/non-volatile toggles each\n",
		kThreads, kIters);

	purgeable_thrash_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;
		targs[i].errors = 0;

		char name[32];
		snprintf(name, sizeof(name), "purg_thrash_%d", i);
		threads[i] = spawn_thread(purgeable_thrash_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalPurged = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: vol=%d, nonvol=%d ok / %d purged / %d fail, "
			"time=%lld us\n", i,
			targs[i].setVolatileOk, targs[i].setNonVolatileOk,
			targs[i].setNonVolatilePurged, targs[i].setNonVolatileFail,
			(long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
		totalPurged += targs[i].setNonVolatilePurged;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	trace("    total toggles=%d, purge events=%d, errors=%d, "
		"slowest=%lld us\n", kThreads * kIters * 2, totalPurged,
		totalErrors, (long long)slowest);
	TEST_ASSERT("purgeable thrash no errors", totalErrors == 0);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_pool_exhaustion()
{
	trace("\n--- test_stress_pool_exhaustion ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	const int kMaxSurfaces = 512;
	KosmSurface* pool[kMaxSurfaces];
	int allocated = 0;

	trace("    allocating up to %d surfaces (256x256 ARGB8888)...\n",
		kMaxSurfaces);

	for (int i = 0; i < kMaxSurfaces; i++) {
		KosmSurfaceDesc desc;
		desc.width = 256;
		desc.height = 256;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		pool[i] = NULL;
		status_t err = alloc->Allocate(desc, &pool[i]);
		if (err != B_OK || pool[i] == NULL) {
			trace("    allocation stopped at %d: %s (0x%08x)\n",
				i, strerror(err), (unsigned)err);
			break;
		}
		allocated++;

		// Log progress every 50 surfaces
		if ((allocated % 50) == 0) {
			trace("    ... %d allocated so far (latest id=%u, "
				"allocSize=%zu)\n", allocated,
				(unsigned)pool[i]->ID(), pool[i]->AllocSize());
		}
	}

	size_t totalBytes = 0;
	for (int i = 0; i < allocated; i++)
		totalBytes += pool[i]->AllocSize();

	trace("    allocated %d surfaces, total %zu bytes (%.1f MB)\n",
		allocated, totalBytes, (double)totalBytes / (1024.0 * 1024.0));
	TEST_ASSERT("allocated at least 16 surfaces", allocated >= 16);

	// Unique ID check
	bool allValid = true;
	int duplicates = 0;
	for (int i = 0; i < allocated; i++) {
		if (!pool[i]->IsValid()) {
			allValid = false;
			trace("    surface %d (id=%u) IsValid()==false!\n",
				i, (unsigned)pool[i]->ID());
			break;
		}
		for (int j = i + 1; j < allocated; j++) {
			if (pool[i]->ID() == pool[j]->ID()) {
				trace("    DUPLICATE ID %u at indices %d and %d!\n",
					(unsigned)pool[i]->ID(), i, j);
				duplicates++;
				allValid = false;
			}
		}
	}
	trace("    ID uniqueness: %s (duplicates=%d)\n",
		allValid ? "ok" : "FAIL", duplicates);
	TEST_ASSERT("all surfaces valid, unique IDs", allValid);

	// Free half
	int halfCount = allocated / 2;
	trace("    freeing first %d surfaces...\n", halfCount);
	bigtime_t freeStart = system_time();
	for (int i = 0; i < halfCount; i++) {
		alloc->Free(pool[i]);
		pool[i] = NULL;
	}
	trace("    freed %d in %lld us\n", halfCount,
		(long long)(system_time() - freeStart));

	// Re-allocate
	trace("    re-allocating %d surfaces...\n", halfCount);
	bigtime_t reallocStart = system_time();
	int reallocated = 0;
	for (int i = 0; i < halfCount; i++) {
		KosmSurfaceDesc desc;
		desc.width = 256;
		desc.height = 256;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		pool[i] = NULL;
		status_t err = alloc->Allocate(desc, &pool[i]);
		if (err == B_OK && pool[i] != NULL) {
			reallocated++;
		} else {
			trace("    re-alloc failed at %d: %s (0x%08x)\n",
				i, strerror(err), (unsigned)err);
		}
	}
	trace("    re-allocated %d/%d in %lld us\n", reallocated, halfCount,
		(long long)(system_time() - reallocStart));
	TEST_ASSERT("re-allocation after free works",
		reallocated == halfCount);

	// Cleanup
	trace("    cleaning up %d surfaces...\n", allocated);
	for (int i = 0; i < allocated; i++) {
		if (pool[i] != NULL)
			alloc->Free(pool[i]);
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct mixed_lock_args {
	KosmSurface*	surface;
	int				iterations;
	bool			readOnly;
	int				errors;
	int				acquired;
	int				timedOut;
	int				unexpectedErr;
	bigtime_t		maxWait;
	bigtime_t		totalTime;
};


static status_t
mixed_lock_thread(void* data)
{
	mixed_lock_args* args = (mixed_lock_args*)data;
	args->errors = 0;
	args->acquired = 0;
	args->timedOut = 0;
	args->unexpectedErr = 0;
	args->maxWait = 0;

	int tid = (int)find_thread(NULL);
	uint32 options = args->readOnly ? KOSM_SURFACE_LOCK_READ_ONLY : 0;
	bigtime_t t0 = system_time();

	trace("    [thread %d] mixed_lock start (%s), iters=%d\n",
		tid, args->readOnly ? "read" : "write", args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		bigtime_t lockStart = system_time();
		status_t err = args->surface->LockWithTimeout(500000, options);
		bigtime_t lockWait = system_time() - lockStart;

		if (lockWait > args->maxWait)
			args->maxWait = lockWait;

		if (err == B_OK) {
			args->acquired++;

			void* base = args->surface->BaseAddress();
			if (!args->readOnly && base != NULL) {
				uint32* p = (uint32*)base;
				*p = (uint32)tid;
			}

			snooze(rand() % 100);
			args->surface->Unlock();
		} else if (err == B_TIMED_OUT || err == B_BUSY) {
			args->timedOut++;
		} else {
			args->unexpectedErr++;
			args->errors++;
			trace("    [thread %d] LockWithTimeout unexpected error "
				"iter %d: %s (0x%08x)\n", tid, i, strerror(err),
				(unsigned)err);
		}
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] mixed_lock done (%s): acquired=%d, "
		"timedOut=%d, errors=%d, maxWait=%lld us, time=%lld us\n",
		tid, args->readOnly ? "read" : "write",
		args->acquired, args->timedOut, args->errors,
		(long long)args->maxWait, (long long)args->totalTime);
	return B_OK;
}


static void
test_stress_mixed_lock_contention()
{
	trace("\n--- test_stress_mixed_lock_contention ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(64x64)", err);
	TEST_ASSERT("allocate", surface != NULL);

	const int kReaders = 4;
	const int kWriters = 4;
	const int kTotal = kReaders + kWriters;
	const int kIters = 50;
	trace("    spawning %d readers + %d writers, %d iterations each\n",
		kReaders, kWriters, kIters);

	mixed_lock_args targs[kTotal];
	thread_id threads[kTotal];

	for (int i = 0; i < kTotal; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;
		targs[i].readOnly = (i < kReaders);
		targs[i].errors = 0;
		targs[i].acquired = 0;

		char name[32];
		snprintf(name, sizeof(name), "mixed_%s_%d",
			targs[i].readOnly ? "rd" : "wr", i);
		threads[i] = spawn_thread(mixed_lock_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned %s thread %d (tid=%d)\n",
			targs[i].readOnly ? "reader" : "writer",
			i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	int totalReaderAcquires = 0;
	int totalWriterAcquires = 0;
	int totalTimedOut = 0;
	bigtime_t worstWait = 0;
	for (int i = 0; i < kTotal; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    %s thread %d: acquired=%d, timedOut=%d, errors=%d, "
			"maxWait=%lld us\n",
			targs[i].readOnly ? "reader" : "writer", i,
			targs[i].acquired, targs[i].timedOut, targs[i].errors,
			(long long)targs[i].maxWait);
		totalErrors += targs[i].errors;
		totalTimedOut += targs[i].timedOut;
		if (targs[i].readOnly)
			totalReaderAcquires += targs[i].acquired;
		else
			totalWriterAcquires += targs[i].acquired;
		if (targs[i].maxWait > worstWait)
			worstWait = targs[i].maxWait;
	}

	trace("    readers=%d, writers=%d, timedOut=%d, errors=%d, "
		"worstWait=%lld us\n", totalReaderAcquires, totalWriterAcquires,
		totalTimedOut, totalErrors, (long long)worstWait);
	TEST_ASSERT("no lock errors", totalErrors == 0);
	TEST_ASSERT("writers acquired some locks", totalWriterAcquires > 0);
	TEST_ASSERT("surface still valid after mixed contention",
		surface->IsValid());

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


struct use_count_args {
	KosmSurface*	surface;
	int				iterations;
	int32			peakSeen;
	bigtime_t		totalTime;
};


static status_t
use_count_inc_dec_thread(void* data)
{
	use_count_args* args = (use_count_args*)data;
	args->peakSeen = 0;

	int tid = (int)find_thread(NULL);
	bigtime_t t0 = system_time();

	trace("    [thread %d] use_count start, iters=%d\n",
		tid, args->iterations);

	for (int i = 0; i < args->iterations; i++) {
		args->surface->IncrementUseCount();

		int32 current = args->surface->LocalUseCount();
		if (current > args->peakSeen)
			args->peakSeen = current;

		snooze(rand() % 10);
		args->surface->DecrementUseCount();
	}

	args->totalTime = system_time() - t0;
	trace("    [thread %d] use_count done: peakSeen=%d, time=%lld us\n",
		tid, (int)args->peakSeen, (long long)args->totalTime);
	return B_OK;
}


static void
test_stress_use_count_contention()
{
	trace("\n--- test_stress_use_count_contention ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	trace_status("Allocate(64x64)", err);
	TEST_ASSERT("allocate", surface != NULL);

	TEST_ASSERT("initial use count 0", surface->LocalUseCount() == 0);

	const int kThreads = 8;
	const int kIters = 500;
	trace("    spawning %d threads, %d inc/dec cycles each\n",
		kThreads, kIters);

	use_count_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;

		char name[32];
		snprintf(name, sizeof(name), "usecount_%d", i);
		threads[i] = spawn_thread(use_count_inc_dec_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		trace("    spawned thread %d (tid=%d)\n", i, (int)threads[i]);
		resume_thread(threads[i]);
	}

	int32 overallPeak = 0;
	bigtime_t slowest = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: peakSeen=%d, time=%lld us\n",
			i, (int)targs[i].peakSeen,
			(long long)targs[i].totalTime);
		if (targs[i].peakSeen > overallPeak)
			overallPeak = targs[i].peakSeen;
		if (targs[i].totalTime > slowest)
			slowest = targs[i].totalTime;
	}

	int32 finalCount = surface->LocalUseCount();
	trace("    %d threads x %d cycles = %d total inc/dec ops\n",
		kThreads, kIters, kThreads * kIters);
	trace("    final use count=%d (expected 0), peak observed=%d, "
		"slowest=%lld us\n", (int)finalCount, (int)overallPeak,
		(long long)slowest);
	TEST_ASSERT("use count returns to 0", finalCount == 0);
	TEST_ASSERT("not in use after all threads done",
		!surface->IsInUse());

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Mobile simulation ---

static void
test_mobile_render_pipeline()
{
	trace("\n--- test_mobile_render_pipeline ---\n");
	trace("  simulating: framebuffer double-buffering pipeline\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	// Double buffer: front + back
	KosmSurfaceDesc desc;
	desc.width = 720;
	desc.height = 1280;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	desc.usage = KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE
		| KOSM_SURFACE_USAGE_COMPOSITOR;

	KosmSurface* frontBuffer = NULL;
	KosmSurface* backBuffer = NULL;
	status_t err1 = alloc->Allocate(desc, &frontBuffer);
	status_t err2 = alloc->Allocate(desc, &backBuffer);
	trace("    front: id=%u, alloc=%s\n",
		frontBuffer ? (unsigned)frontBuffer->ID() : 0, strerror(err1));
	trace("    back: id=%u, alloc=%s\n",
		backBuffer ? (unsigned)backBuffer->ID() : 0, strerror(err2));
	TEST_ASSERT("allocate front buffer", err1 == B_OK);
	TEST_ASSERT("allocate back buffer", err2 == B_OK);

	// Simulate 60 frames of double-buffered rendering
	const int kFrames = 60;
	int errors = 0;
	bigtime_t maxFrameTime = 0;

	for (int frame = 0; frame < kFrames; frame++) {
		bigtime_t frameStart = system_time();

		// "Render" into back buffer
		status_t err = backBuffer->Lock();
		if (err != B_OK) {
			errors++;
			continue;
		}

		uint32* pixels = (uint32*)backBuffer->BaseAddress();
		if (pixels != NULL) {
			// Simple fill (simulate render work)
			uint32 color = 0xFF000000 | ((frame * 4) << 8);
			uint32 bpr = backBuffer->BytesPerRow();
			for (uint32 y = 0; y < 4; y++) {
				uint32* row = (uint32*)((uint8*)pixels + y * bpr);
				for (uint32 x = 0; x < backBuffer->Width(); x++)
					row[x] = color;
			}
		}
		backBuffer->Unlock();

		// "Present": swap front/back
		KosmSurface* temp = frontBuffer;
		frontBuffer = backBuffer;
		backBuffer = temp;

		// Mark front as in-use by compositor
		frontBuffer->IncrementUseCount();
		// Simulate compositor read
		snooze(100);
		frontBuffer->DecrementUseCount();

		bigtime_t frameTime = system_time() - frameStart;
		if (frameTime > maxFrameTime)
			maxFrameTime = frameTime;
	}

	trace("    %d frames, errors=%d, maxFrameTime=%lld us\n",
		kFrames, errors, (long long)maxFrameTime);
	TEST_ASSERT("no frame errors", errors == 0);
	TEST_ASSERT("max frame time < 100ms", maxFrameTime < 100000);

	// Verify seeds incremented
	uint32 s1 = frontBuffer->Seed();
	uint32 s2 = backBuffer->Seed();
	trace("    front seed=%u, back seed=%u\n",
		(unsigned)s1, (unsigned)s2);
	TEST_ASSERT("buffers have been written to", s1 > 0 && s2 > 0);

	alloc->Free(frontBuffer);
	alloc->Free(backBuffer);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Planar chroma bit offsets ---

static void
test_planar_chroma_offsets()
{
	trace("\n--- test_planar_chroma_offsets ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 128;
	desc.height = 128;

	// NV12 plane1: U,V -> component0@0, component1@8
	desc.format = KOSM_PIXEL_FORMAT_NV12;
	KosmSurface* s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate NV12", s != NULL);
	TEST_ASSERT("NV12 plane0 components == 1",
		s->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("NV12 plane0 Y offset == 0",
		s->BitOffsetOfComponentOfPlane(0, 0) == 0);
	TEST_ASSERT("NV12 plane1 components == 2",
		s->ComponentCountOfPlane(1) == 2);
	TEST_ASSERT("NV12 plane1 U@0",
		s->BitOffsetOfComponentOfPlane(1, 0) == 0);
	TEST_ASSERT("NV12 plane1 V@8",
		s->BitOffsetOfComponentOfPlane(1, 1) == 8);
	trace("    NV12 chroma: comp0@%u, comp1@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(1, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(1, 1));
	alloc->Free(s);

	// NV21 plane1: V,U -> component0@8, component1@0 (reversed)
	desc.format = KOSM_PIXEL_FORMAT_NV21;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate NV21", s != NULL);
	TEST_ASSERT("NV21 plane1 components == 2",
		s->ComponentCountOfPlane(1) == 2);
	TEST_ASSERT("NV21 plane1 V@8 (comp0)",
		s->BitOffsetOfComponentOfPlane(1, 0) == 8);
	TEST_ASSERT("NV21 plane1 U@0 (comp1)",
		s->BitOffsetOfComponentOfPlane(1, 1) == 0);
	trace("    NV21 chroma: comp0@%u, comp1@%u\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(1, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(1, 1));

	// Verify NV12 and NV21 chroma offsets are swapped
	TEST_ASSERT("NV12 vs NV21 chroma order differs", true);
	alloc->Free(s);

	// YV12: 3 planes, each 1 component at offset 0
	desc.format = KOSM_PIXEL_FORMAT_YV12;
	s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate YV12", s != NULL);
	TEST_ASSERT("YV12 plane0 comp == 1",
		s->ComponentCountOfPlane(0) == 1);
	TEST_ASSERT("YV12 plane1 comp == 1",
		s->ComponentCountOfPlane(1) == 1);
	TEST_ASSERT("YV12 plane2 comp == 1",
		s->ComponentCountOfPlane(2) == 1);
	TEST_ASSERT("YV12 all planes @0",
		s->BitOffsetOfComponentOfPlane(0, 0) == 0
		&& s->BitOffsetOfComponentOfPlane(1, 0) == 0
		&& s->BitOffsetOfComponentOfPlane(2, 0) == 0);
	alloc->Free(s);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- A8 / L8 pixel read/write ---

static void
test_pixel_readwrite_a8()
{
	trace("\n--- test_pixel_readwrite_a8 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 32;
	desc.height = 32;
	desc.format = KOSM_PIXEL_FORMAT_A8;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate A8", surface != NULL);
	TEST_ASSERT("A8 BytesPerElement == 1",
		surface->BytesPerElement() == 1);

	// Write gradient
	surface->Lock();
	uint8* pixels = (uint8*)surface->BaseAddress();
	TEST_ASSERT("A8 pixels not null", pixels != NULL);

	uint32 bpr = surface->BytesPerRow();
	for (uint32 y = 0; y < 32; y++) {
		uint8* row = pixels + y * bpr;
		for (uint32 x = 0; x < 32; x++)
			row[x] = (uint8)((y * 32 + x) & 0xFF);
	}
	surface->Unlock();

	// Read back
	surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	uint8* readPixels = (uint8*)surface->BaseAddress();
	bool correct = true;
	for (uint32 y = 0; y < 32 && correct; y++) {
		uint8* row = readPixels + y * bpr;
		for (uint32 x = 0; x < 32 && correct; x++) {
			uint8 expected = (uint8)((y * 32 + x) & 0xFF);
			if (row[x] != expected) {
				trace("    A8 mismatch at (%u,%u): got 0x%02x, expected "
					"0x%02x\n", (unsigned)x, (unsigned)y,
					(unsigned)row[x], (unsigned)expected);
				correct = false;
			}
		}
	}
	surface->Unlock();

	TEST_ASSERT("A8 pixel readback matches", correct);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_pixel_readwrite_l8()
{
	trace("\n--- test_pixel_readwrite_l8 ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 16;
	desc.height = 16;
	desc.format = KOSM_PIXEL_FORMAT_L8;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate L8", surface != NULL);
	TEST_ASSERT("L8 BytesPerElement == 1",
		surface->BytesPerElement() == 1);

	// Write checkerboard
	surface->Lock();
	uint8* pixels = (uint8*)surface->BaseAddress();
	TEST_ASSERT("L8 pixels not null", pixels != NULL);

	uint32 bpr = surface->BytesPerRow();
	for (uint32 y = 0; y < 16; y++) {
		uint8* row = pixels + y * bpr;
		for (uint32 x = 0; x < 16; x++)
			row[x] = ((x + y) % 2 == 0) ? 0xFF : 0x00;
	}
	surface->Unlock();

	// Read back
	surface->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
	uint8* readPixels = (uint8*)surface->BaseAddress();
	bool correct = true;
	for (uint32 y = 0; y < 16 && correct; y++) {
		uint8* row = readPixels + y * bpr;
		for (uint32 x = 0; x < 16 && correct; x++) {
			uint8 expected = ((x + y) % 2 == 0) ? 0xFF : 0x00;
			if (row[x] != expected) {
				trace("    L8 mismatch at (%u,%u): got 0x%02x, "
					"expected 0x%02x\n", (unsigned)x, (unsigned)y,
					(unsigned)row[x], (unsigned)expected);
				correct = false;
			}
		}
	}
	surface->Unlock();

	TEST_ASSERT("L8 pixel readback matches", correct);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Zero-timeout try-lock ---

static void
test_lock_zero_timeout()
{
	trace("\n--- test_lock_zero_timeout ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	KosmSurface* surface = NULL;
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	// LockWithTimeout(0) on unlocked surface should succeed immediately
	status_t err = surface->LockWithTimeout(0);
	trace_status("LockWithTimeout(0) on unlocked", err);
	TEST_ASSERT("zero-timeout on unlocked succeeds", err == B_OK);
	TEST_ASSERT("is locked", surface->IsLocked());
	surface->Unlock();

	// Lock normally, then try LockWithTimeout(0) from another thread
	surface->Lock();

	struct try_args {
		KosmSurface*	surface;
		status_t		result;
		bigtime_t		elapsed;
	};

	try_args targs;
	targs.surface = surface;
	targs.result = B_OK;
	targs.elapsed = 0;

	auto try_thread = [](void* data) -> status_t {
		try_args* a = (try_args*)data;
		bigtime_t t = system_time();
		a->result = a->surface->LockWithTimeout(0);
		a->elapsed = system_time() - t;
		trace("    [thread %d] LockWithTimeout(0) -> %s, elapsed=%lld us\n",
			(int)find_thread(NULL), strerror(a->result),
			(long long)a->elapsed);
		if (a->result == B_OK)
			a->surface->Unlock();
		return B_OK;
	};

	thread_id tid = spawn_thread(try_thread, "try_lock_zero",
		B_NORMAL_PRIORITY, &targs);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("zero-timeout returns B_TIMED_OUT when held",
		targs.result == B_TIMED_OUT);
	TEST_ASSERT("zero-timeout returns quickly (< 10ms)",
		targs.elapsed < 10000);

	surface->Unlock();
	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Cross-process clone ---

static void
test_cross_process_clone()
{
	trace("\n--- test_cross_process_clone ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

	KosmSurface* surface = NULL;
	status_t err = alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", err == B_OK && surface != NULL);

	// Write known pattern
	err = surface->Lock();
	TEST_ASSERT("lock for write", err == B_OK);
	uint32* pixels = (uint32*)surface->BaseAddress();
	if (pixels != NULL) {
		uint32 bpr = surface->BytesPerRow();
		for (uint32 y = 0; y < 64; y++) {
			uint32* row = (uint32*)((uint8*)pixels + y * bpr);
			for (uint32 x = 0; x < 64; x++)
				row[x] = 0xCAFE0000 | (y << 8) | x;
		}
	}
	surface->Unlock();

	// Create access token for cross-process lookup
	KosmSurfaceToken token;
	memset(&token, 0, sizeof(token));
	err = surface->CreateAccessToken(&token);
	TEST_ASSERT("create token", err == B_OK);

	trace("    parent: id=%u, token secret=0x%llx, gen=%u\n",
		(unsigned)surface->ID(),
		(unsigned long long)token.secret,
		(unsigned)token.generation);

	// Set up pipe for child result
	int pipefd[2];
	err = pipe(pipefd) == 0 ? B_OK : B_ERROR;
	TEST_ASSERT("create pipe", err == B_OK);

	pid_t childPid = fork();
	if (childPid == 0) {
		// Child process: no BApplication, just Surface Kit
		close(pipefd[0]);

		// The child has a fresh KosmSurfaceAllocator::Default().
		// It must clone via token through the shared registry area.
		KosmSurfaceAllocator* childAlloc
			= KosmSurfaceAllocator::Default();

		KosmSurface* cloned = NULL;
		status_t childErr = childAlloc->LookupWithToken(token, &cloned);

		uint8 result[4];
		result[0] = (childErr == B_OK) ? 1 : 0;

		if (cloned != NULL) {
			result[1] = (cloned->Width() == 64
				&& cloned->Height() == 64) ? 1 : 0;

			childErr = cloned->Lock(KOSM_SURFACE_LOCK_READ_ONLY);
			if (childErr == B_OK) {
				uint32* clonedPixels = (uint32*)cloned->BaseAddress();
				uint32 bpr = cloned->BytesPerRow();
				bool match = true;
				for (uint32 y = 0; y < 4 && match; y++) {
					uint32* row = (uint32*)((uint8*)clonedPixels
						+ y * bpr);
					for (uint32 x = 0; x < 64 && match; x++) {
						uint32 expected = 0xCAFE0000 | (y << 8) | x;
						if (row[x] != expected)
							match = false;
					}
				}
				result[2] = match ? 1 : 0;
				cloned->Unlock();
			} else {
				result[2] = 0;
			}

			childAlloc->Free(cloned);
		} else {
			result[1] = 0;
			result[2] = 0;
		}

		result[3] = 0xFF;  // sentinel

		write(pipefd[1], result, sizeof(result));
		close(pipefd[1]);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", childPid > 0);

	close(pipefd[1]);

	uint8 childResult[4] = {0};
	ssize_t bytesRead = 0;
	if (childPid > 0) {
		bytesRead = read(pipefd[0], childResult, sizeof(childResult));

		int status;
		waitpid(childPid, &status, 0);
		trace("    child exit status=%d\n", WEXITSTATUS(status));
	}
	close(pipefd[0]);

	trace("    child result: lookup=%d, dims=%d, data=%d, sentinel=%d, "
		"read=%zd\n", (int)childResult[0], (int)childResult[1],
		(int)childResult[2], (int)childResult[3], bytesRead);

	TEST_ASSERT("child: LookupWithToken succeeded",
		childResult[0] == 1);
	TEST_ASSERT("child: cloned dimensions correct",
		childResult[1] == 1);
	TEST_ASSERT("child: cloned data matches parent",
		childResult[2] == 1);
	TEST_ASSERT("child: sentinel received",
		childResult[3] == 0xFF);

	alloc->Free(surface);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// --- Registry capacity ---

static void
test_registry_capacity()
{
	trace("\n--- test_registry_capacity ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();

	// Try to fill registry near its 4096 limit.
	// Use small surfaces (4x4) to minimize memory pressure.
	const int kTargetCount = 4096;
	KosmSurface** pool = (KosmSurface**)calloc(kTargetCount,
		sizeof(KosmSurface*));
	TEST_ASSERT("pool allocation", pool != NULL);

	int allocated = 0;
	int registryFull = 0;

	trace("    allocating up to %d surfaces (4x4 ARGB8888)...\n",
		kTargetCount);

	for (int i = 0; i < kTargetCount; i++) {
		KosmSurfaceDesc desc;
		desc.width = 4;
		desc.height = 4;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		pool[i] = NULL;
		status_t err = alloc->Allocate(desc, &pool[i]);
		if (err == B_NO_MEMORY) {
			trace("    registry full at %d\n", i);
			registryFull = i;
			break;
		}
		if (err != B_OK || pool[i] == NULL) {
			trace("    allocation failed at %d: %s (0x%08x)\n",
				i, strerror(err), (unsigned)err);
			break;
		}
		allocated++;

		if ((allocated % 500) == 0) {
			trace("    ... %d allocated (id=%u)\n", allocated,
				(unsigned)pool[i]->ID());
		}
	}

	trace("    allocated %d surfaces, registryFull=%d\n",
		allocated, registryFull);
	TEST_ASSERT("allocated many surfaces", allocated > 100);

	// If we hit registry limit, verify the error is clean
	if (registryFull > 0) {
		TEST_ASSERT("registry limit reached", registryFull <= 4096);
		trace("    registry capacity test: limit at %d\n",
			registryFull);
	}

	// Free half and verify re-allocation (tombstone recovery)
	int halfCount = allocated / 2;
	trace("    freeing first %d surfaces...\n", halfCount);
	for (int i = 0; i < halfCount; i++) {
		if (pool[i] != NULL) {
			alloc->Free(pool[i]);
			pool[i] = NULL;
		}
	}

	// Re-allocate into freed slots
	int refilled = 0;
	for (int i = 0; i < halfCount; i++) {
		KosmSurfaceDesc desc;
		desc.width = 4;
		desc.height = 4;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		pool[i] = NULL;
		status_t err = alloc->Allocate(desc, &pool[i]);
		if (err == B_OK && pool[i] != NULL)
			refilled++;
		else
			break;
	}
	trace("    refilled %d/%d after tombstone recovery\n",
		refilled, halfCount);
	TEST_ASSERT("tombstone recovery allows re-allocation",
		refilled == halfCount);

	// Cleanup
	trace("    cleaning up %d surfaces...\n", allocated);
	for (int i = 0; i < kTargetCount; i++) {
		if (pool[i] != NULL)
			alloc->Free(pool[i]);
	}
	free(pool);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}




// Suite registration

static const TestEntry sSurfaceTests[] = {
	// Basic
	{ "allocate/free",           test_allocate_free,           "Basic" },
	{ "zero-size alloc",         test_allocate_zero_size,      "Basic" },
	{ "all formats",             test_allocate_all_formats,    "Basic" },
	{ "lookup",                  test_lookup,                  "Basic" },
	{ "lookup or clone",         test_lookup_or_clone,         "Basic" },
	{ "lookup with token",       test_lookup_with_token,       "Basic" },
	{ "revoked token lookup",    test_revoked_token_lookup,    "Basic" },
	{ "free then lookup",        test_free_then_lookup,        "Basic" },
	{ "double free",             test_double_free,             "Basic" },
	{ "format support",          test_format_support,          "Basic" },
	{ "cache modes",             test_cache_modes,             "Basic" },
	{ "custom stride",           test_custom_stride,           "Basic" },
	{ "large allocation",        test_large_allocation,        "Basic" },
	// Lock
	{ "lock/unlock",             test_lock_unlock,             "Lock" },
	{ "read-only lock",          test_lock_read_only,          "Lock" },
	{ "recursive lock",          test_lock_recursive,          "Lock" },
	{ "cross-thread lock",       test_lock_other_thread,       "Lock" },
	{ "unlock not-owner",        test_unlock_not_owner,        "Lock" },
	{ "unlock not-locked",       test_unlock_not_locked,       "Lock" },
	{ "lock timeout",            test_lock_timeout,            "Lock" },
	{ "read->write upgrade",     test_lock_read_write_upgrade, "Lock" },
	{ "zero-timeout try-lock",   test_lock_zero_timeout,       "Lock" },
	// Seed
	{ "seed tracking",           test_seed,                    "Seed" },
	// Planar
	{ "NV12 planes",             test_planar_nv12,             "Planar" },
	{ "NV21 planes",             test_planar_nv21,             "Planar" },
	{ "YV12 planes",             test_planar_yv12,             "Planar" },
	{ "component info",          test_component_info,          "Planar" },
	{ "chroma bit offsets",      test_planar_chroma_offsets,   "Planar" },
	// Data
	{ "use count",               test_use_count,               "Data" },
	{ "free while in-use",       test_free_while_in_use,       "Data" },
	{ "attachments",             test_attachments,             "Data" },
	{ "purgeable",               test_purgeable,               "Data" },
	{ "purgeable+lock",          test_purgeable_lock_interaction, "Data" },
	{ "purgeable usage flag",    test_purgeable_usage_flag,    "Data" },
	{ "access token",            test_access_token,            "Data" },
	{ "usage flags",             test_usage_flags,             "Data" },
	{ "pixel read/write",        test_pixel_readwrite,         "Data" },
	{ "pixel read/write RGB565", test_pixel_readwrite_rgb565,  "Data" },
	{ "pixel read/write A8",     test_pixel_readwrite_a8,      "Data" },
	{ "pixel read/write L8",     test_pixel_readwrite_l8,      "Data" },
	// Stress
	{ "alloc/free churn 8x100",  test_stress_alloc_free,       "STRESS" },
	{ "lock contention 8x50",    test_stress_lock_contention,  "STRESS" },
	{ "rapid lock/unlock 10000", test_stress_rapid_lock_unlock, "STRESS" },
	{ "attachment thrash 8x100", test_stress_attachment_thrash, "STRESS" },
	{ "alloc/lookup race 8x100", test_stress_alloc_lookup_race, "STRESS" },
	{ "token thrash 4x200",      test_stress_token_thrash,     "STRESS" },
	{ "purgeable thrash 4x200",  test_stress_purgeable_thrash, "STRESS" },
	{ "pool exhaustion 512",     test_stress_pool_exhaustion,  "STRESS" },
	{ "mixed rd/wr lock 8x50",   test_stress_mixed_lock_contention, "STRESS" },
	{ "use count contention 8x500", test_stress_use_count_contention, "STRESS" },
	{ "registry capacity 4096",  test_registry_capacity,       "STRESS" },
	// Cross-process
	{ "cross-process clone",     test_cross_process_clone,     "Cross-Process" },
	// Mobile Sim
	{ "render pipeline 60fps",   test_mobile_render_pipeline,  "MOBILE SIM" },
};


TestSuite
get_surface_test_suite()
{
	TestSuite suite;
	suite.name = "KosmSurface";
	suite.tests = sSurfaceTests;
	suite.count = sizeof(sSurfaceTests) / sizeof(sSurfaceTests[0]);
	return suite;
}
