/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * GUI test application for KosmSurface / Surface Kit.
 * Runs all tests on launch, displays results in a BView,
 * writes detailed trace log to ~/Desktop/kosm_surface_trace.log.
 */


#include <Application.h>
#include <FindDirectory.h>
#include <Font.h>
#include <KosmSurface.hpp>
#include <KosmSurfaceAllocator.hpp>
#include <Message.h>
#include <Path.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static FILE* sTraceFile = NULL;
static int sPassCount = 0;
static int sFailCount = 0;


static void
trace(const char* fmt, ...)
{
	if (sTraceFile == NULL)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(sTraceFile, fmt, args);
	va_end(args);
	fflush(sTraceFile);
}


static void
trace_status(const char* func, status_t result)
{
	trace("    %s -> %s (0x%08x)\n", func, strerror(result),
		(unsigned)result);
}


#define TEST_ASSERT(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			trace("  FAIL: %s  (line %d, %lld us)\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
		} \
	} while (0)


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
	bigtime_t		totalTime;
};


static status_t
alloc_free_churn_thread(void* data)
{
	alloc_free_args* args = (alloc_free_args*)data;
	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	args->errors = 0;

	bigtime_t t0 = system_time();
	for (int i = 0; i < args->iterations; i++) {
		KosmSurfaceDesc desc;
		desc.width = 64;
		desc.height = 64;
		desc.format = KOSM_PIXEL_FORMAT_ARGB8888;

		KosmSurface* surface = NULL;
		status_t err = alloc->Allocate(desc, &surface);
		if (err != B_OK || surface == NULL) {
			args->errors++;
			continue;
		}

		err = surface->Lock();
		if (err == B_OK) {
			void* base = surface->BaseAddress();
			if (base != NULL)
				memset(base, 0xFF, 64 * 4);
			surface->Unlock();
		}

		alloc->Free(surface);
	}
	args->totalTime = system_time() - t0;
	return B_OK;
}


struct lock_contention_args {
	KosmSurface*	surface;
	int				iterations;
	int				errors;
	bigtime_t		maxLockTime;
	int32*	counter;
};


static status_t
lock_contention_thread(void* data)
{
	lock_contention_args* args = (lock_contention_args*)data;
	args->errors = 0;
	args->maxLockTime = 0;

	for (int i = 0; i < args->iterations; i++) {
		bigtime_t t0 = system_time();
		status_t err = args->surface->LockWithTimeout(1000000);
		bigtime_t dt = system_time() - t0;

		if (dt > args->maxLockTime)
			args->maxLockTime = dt;

		if (err != B_OK) {
			args->errors++;
			continue;
		}

		atomic_add(args->counter, 1);
		snooze(1);
		args->surface->Unlock();
	}
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
test_component_info()
{
	trace("\n--- test_component_info ---\n");
	bigtime_t t0 = system_time();

	KosmSurfaceAllocator* alloc = KosmSurfaceAllocator::Default();
	KosmSurfaceDesc desc;
	desc.width = 64;
	desc.height = 64;

	// ARGB8888: 4 components, all 8-bit
	desc.format = KOSM_PIXEL_FORMAT_ARGB8888;
	KosmSurface* s = NULL;
	alloc->Allocate(desc, &s);
	TEST_ASSERT("allocate ARGB", s != NULL);

	TEST_ASSERT("ARGB components == 4",
		s->ComponentCountOfPlane(0) == 4);
	TEST_ASSERT("ARGB R depth == 8",
		s->BitDepthOfComponentOfPlane(0, 0) == 8);
	TEST_ASSERT("ARGB R offset == 16",
		s->BitOffsetOfComponentOfPlane(0, 0) == 16);
	TEST_ASSERT("ARGB A offset == 24",
		s->BitOffsetOfComponentOfPlane(0, 3) == 24);
	trace("    ARGB8888: R@%u, G@%u, B@%u, A@%u (all %ubit)\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 3),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 0));
	alloc->Free(s);

	// RGB565: 3 components, R=5 G=6 B=5
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
	trace("    RGB565: R@%u(%u), G@%u(%u), B@%u(%u)\n",
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 0),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 0),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 1),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 1),
		(unsigned)s->BitOffsetOfComponentOfPlane(0, 2),
		(unsigned)s->BitDepthOfComponentOfPlane(0, 2));
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

	// CopyAllAttachments
	BMessage all;
	err = surface->CopyAllAttachments(&all);
	trace_status("CopyAllAttachments", err);
	TEST_ASSERT("copy all", err == B_OK);

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


// --- Stress tests ---

static void
test_stress_alloc_free()
{
	trace("\n--- test_stress_alloc_free ---\n");
	bigtime_t t0 = system_time();

	const int kThreads = 8;
	const int kItersPerThread = 100;

	alloc_free_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].iterations = kItersPerThread;
		targs[i].errors = 0;
		targs[i].totalTime = 0;

		char name[32];
		snprintf(name, sizeof(name), "alloc_churn_%d", i);
		threads[i] = spawn_thread(alloc_free_churn_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: errors=%d, time=%lld us\n",
			i, targs[i].errors, (long long)targs[i].totalTime);
		totalErrors += targs[i].errors;
	}

	trace("    total allocs=%d, total errors=%d\n",
		kThreads * kItersPerThread, totalErrors);
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
	alloc->Allocate(desc, &surface);
	TEST_ASSERT("allocate", surface != NULL);

	const int kThreads = 8;
	const int kIters = 50;
	int32 counter = 0;

	lock_contention_args targs[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		targs[i].surface = surface;
		targs[i].iterations = kIters;
		targs[i].errors = 0;
		targs[i].maxLockTime = 0;
		targs[i].counter = &counter;

		char name[32];
		snprintf(name, sizeof(name), "lock_cont_%d", i);
		threads[i] = spawn_thread(lock_contention_thread, name,
			B_NORMAL_PRIORITY, &targs[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	bigtime_t worstLock = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		trace("    thread %d: errors=%d, maxLock=%lld us\n",
			i, targs[i].errors, (long long)targs[i].maxLockTime);
		totalErrors += targs[i].errors;
		if (targs[i].maxLockTime > worstLock)
			worstLock = targs[i].maxLockTime;
	}

	int32 expected = kThreads * kIters;
	trace("    counter=%d, expected=%d, totalErrors=%d, worstLock=%lld us\n",
		(int)counter, (int)expected, totalErrors,
		(long long)worstLock);
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


// ============================================================
// GUI
// ============================================================

static const int kMaxLogLines = 200;
static char sLogLines[kMaxLogLines][256];
static int sLogLineCount = 0;
static bool sLogLinePass[kMaxLogLines];


static void
log_line(const char* text, bool pass)
{
	if (sLogLineCount < kMaxLogLines) {
		strlcpy(sLogLines[sLogLineCount], text,
			sizeof(sLogLines[0]));
		sLogLinePass[sLogLineCount] = pass;
		sLogLineCount++;
	}
}


class ResultView : public BView {
public:
	ResultView(BRect frame)
		: BView(frame, "results", B_FOLLOW_ALL_SIDES,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
	}

	void Draw(BRect updateRect)
	{
		SetLowColor(255, 255, 255);
		FillRect(Bounds(), B_SOLID_LOW);

		BFont font(be_fixed_font);
		font.SetSize(11);
		SetFont(&font);

		float lineHeight = 14;
		float y = 20;

		for (int i = 0; i < sLogLineCount; i++) {
			if (sLogLinePass[i])
				SetHighColor(0, 128, 0);
			else
				SetHighColor(200, 0, 0);
			DrawString(sLogLines[i], BPoint(10, y));
			y += lineHeight;
		}
	}
};


typedef void (*test_func_t)();

static void
_RunTest(const char* label, test_func_t func, const char* category)
{
	int passBefore = sPassCount;
	int failBefore = sFailCount;

	func();

	int checks = (sPassCount - passBefore) + (sFailCount - failBefore);
	int fails = sFailCount - failBefore;
	char line[256];
	if (fails == 0) {
		snprintf(line, sizeof(line),
			"[%s] PASS: %s  (%d checks)", category, label, checks);
		log_line(line, true);
		trace("  [%s] PASS: %s  (%d checks)\n", category, label, checks);
	} else {
		snprintf(line, sizeof(line),
			"[%s] FAIL: %s  (%d pass, %d fail)",
			category, label, sPassCount - passBefore, fails);
		log_line(line, false);
		trace("  [%s] FAIL: %s  (%d pass, %d fail)\n",
			category, label, sPassCount - passBefore, fails);
	}
}


class TestApp : public BApplication {
public:
	TestApp()
		: BApplication("application/x-vnd.KosmOS-SurfaceTest")
	{
	}

	void ReadyToRun()
	{
		// Open trace file
		BPath path;
		find_directory(B_DESKTOP_DIRECTORY, &path);
		path.Append("kosm_surface_trace.log");
		sTraceFile = fopen(path.Path(), "w");

		trace("# KosmSurface test trace\n");
		trace("# date: %s %s\n", __DATE__, __TIME__);
		trace("# system_time at start: %lld us\n",
			(long long)system_time());
		trace("# main thread: %d, team: %d\n",
			(int)find_thread(NULL), (int)getpid());
		trace("#\n");

		// --- BASIC ---
		trace("\n# ========== BASIC ==========\n");
		_RunTest("allocate/free",           test_allocate_free, "basic");
		_RunTest("zero-size alloc",         test_allocate_zero_size, "basic");
		_RunTest("all formats",             test_allocate_all_formats, "basic");
		_RunTest("lookup",                  test_lookup, "basic");
		_RunTest("format support",          test_format_support, "basic");

		// --- LOCK ---
		trace("\n# ========== LOCK ==========\n");
		_RunTest("lock/unlock",             test_lock_unlock, "lock");
		_RunTest("read-only lock",          test_lock_read_only, "lock");
		_RunTest("recursive lock",          test_lock_recursive, "lock");
		_RunTest("cross-thread lock",       test_lock_other_thread, "lock");
		_RunTest("unlock not-owner",        test_unlock_not_owner, "lock");
		_RunTest("unlock not-locked",       test_unlock_not_locked, "lock");
		_RunTest("lock timeout",            test_lock_timeout, "lock");
		_RunTest("read->write upgrade",     test_lock_read_write_upgrade, "lock");

		// --- SEED ---
		trace("\n# ========== SEED ==========\n");
		_RunTest("seed tracking",           test_seed, "seed");

		// --- PLANAR ---
		trace("\n# ========== PLANAR ==========\n");
		_RunTest("NV12 planes",             test_planar_nv12, "planar");
		_RunTest("YV12 planes",             test_planar_yv12, "planar");
		_RunTest("component info",          test_component_info, "planar");

		// --- DATA ---
		trace("\n# ========== DATA ==========\n");
		_RunTest("use count",               test_use_count, "data");
		_RunTest("attachments",             test_attachments, "data");
		_RunTest("purgeable",               test_purgeable, "data");
		_RunTest("access token",            test_access_token, "data");
		_RunTest("usage flags",             test_usage_flags, "data");
		_RunTest("pixel read/write",        test_pixel_readwrite, "data");

		// --- STRESS ---
		trace("\n# ========== STRESS ==========\n");
		_RunTest("alloc/free churn 8x100",  test_stress_alloc_free, "stress");
		_RunTest("lock contention 8x50",    test_stress_lock_contention, "stress");
		_RunTest("rapid lock/unlock 10000", test_stress_rapid_lock_unlock, "stress");

		// --- MOBILE SIM ---
		trace("\n# ========== MOBILE SIM ==========\n");
		_RunTest("render pipeline 60fps",   test_mobile_render_pipeline, "mobile");

		// Final summary
		trace("\n# ================================\n");
		trace("# FINAL: %d passed, %d failed\n", sPassCount, sFailCount);
		trace("# total time: %lld us\n",
			(long long)(system_time()));
		trace("# ================================\n");

		char summary[128];
		snprintf(summary, sizeof(summary),
			"TOTAL: %d passed, %d failed", sPassCount, sFailCount);
		log_line(summary, sFailCount == 0);

		if (sTraceFile != NULL) {
			fclose(sTraceFile);
			sTraceFile = NULL;
		}

		// Show results window
		BRect frame(100, 100, 700, 100 + sLogLineCount * 14 + 40);
		BWindow* window = new BWindow(frame, "KosmSurface Test Results",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
		window->AddChild(new ResultView(window->Bounds()));
		window->Show();
	}
};


int
main()
{
	TestApp app;
	app.Run();
	return 0;
}