/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmDot test functions for the unified test suite.
 *
 * All output goes to both trace file AND kernel serial debug output,
 * so results are visible in QEMU debug console immediately.
 */


#include "TestCommon.h"

#include <KosmOS.h>
#include <OS.h>
#include <FindDirectory.h>
#include <Path.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>


// Override TEST_ASSERT for dot tests: dual output to file + serial
#define DOT_ASSERT(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			debug_trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			debug_trace("  FAIL: %s  (line %d, %lld us)\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
		} \
	} while (0)

// Return-on-fail variant — prevents NULL dereference after failed allocation
#define DOT_REQUIRE(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			debug_trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			debug_trace("  FAIL: %s  (line %d, %lld us) ** ABORT **\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
			return; \
		} \
	} while (0)


// ===================== BASIC =====================

// 1. Create and delete
static void
test_create_delete()
{
	debug_trace("  [test_create_delete]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("test_basic",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);
	DOT_ASSERT("address non-null", addr != NULL);
	DOT_ASSERT("address page-aligned", ((addr_t)addr % B_PAGE_SIZE) == 0);

	// Write to verify it's mapped and writable
	memset(addr, 0xAB, B_PAGE_SIZE);
	DOT_ASSERT("write succeeded", ((uint8*)addr)[0] == 0xAB);
	DOT_ASSERT("write full page", ((uint8*)addr)[B_PAGE_SIZE - 1] == 0xAB);

	status_t status = kosm_delete_dot(dot);
	DOT_ASSERT("delete dot", status == B_OK);
}


// 2. Create with name, verify via info
static void
test_create_named()
{
	debug_trace("  [test_create_named]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("my_named_dot",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_UI, 0);

	DOT_REQUIRE("create named dot", dot >= 0);

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));
	status_t status = kosm_get_dot_info(dot, &info);
	DOT_ASSERT("get_dot_info ok", status == B_OK);
	DOT_ASSERT("info name match",
		strcmp(info.name, "my_named_dot") == 0);
	DOT_ASSERT("info size", info.size == B_PAGE_SIZE * 4);
	DOT_ASSERT("info tag", info.tag == KOSM_TAG_UI);
	DOT_ASSERT("info protection read",
		(info.protection & KOSM_PROT_READ) != 0);
	DOT_ASSERT("info protection write",
		(info.protection & KOSM_PROT_WRITE) != 0);

	kosm_delete_dot(dot);
}


// 3. Multiple pages — write pattern, read back
static void
test_multi_page()
{
	debug_trace("  [test_multi_page]\n");

	const size_t kSize = B_PAGE_SIZE * 8;
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("multi_page",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create 8-page dot", dot >= 0);

	// Write different pattern per page
	for (size_t i = 0; i < 8; i++) {
		memset((uint8*)addr + i * B_PAGE_SIZE,
			(uint8)(i + 1), B_PAGE_SIZE);
	}

	// Verify
	bool allOk = true;
	for (size_t i = 0; i < 8; i++) {
		uint8 expected = (uint8)(i + 1);
		uint8 actual = ((uint8*)addr)[i * B_PAGE_SIZE];
		if (actual != expected) {
			debug_trace("    page %zu: expected %u got %u\n",
				i, expected, actual);
			allOk = false;
		}
	}
	DOT_ASSERT("all pages correct", allOk);

	kosm_delete_dot(dot);
}


// 4. Double delete — second should fail
static void
test_double_delete()
{
	debug_trace("  [test_double_delete]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("double_del",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	status_t s1 = kosm_delete_dot(dot);
	DOT_ASSERT("first delete ok", s1 == B_OK);

	status_t s2 = kosm_delete_dot(dot);
	DOT_ASSERT("second delete fails", s2 != B_OK);
}


// 5. Read-only protection
static void
test_read_only()
{
	debug_trace("  [test_read_only]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("read_only",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create read-only dot", dot >= 0);

	// Reading should work — touch the page to fault it in
	// (page comes in zeroed)
	volatile uint8* p = (volatile uint8*)addr;
	uint8 val = p[0];
	DOT_ASSERT("read from read-only", val == 0);

	// NOTE: writing to read-only memory would SIGSEGV.
	// We don't test that here to avoid crashing the suite.

	kosm_delete_dot(dot);
}


// ===================== PROTECTION =====================

// 6. Change protection from RO to RW
static void
test_protect_change()
{
	debug_trace("  [test_protect_change]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("prot_change",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create RO dot", dot >= 0);

	// Upgrade to RW
	status_t status = kosm_protect_dot(dot,
		KOSM_PROT_READ | KOSM_PROT_WRITE);
	DOT_ASSERT("protect RO->RW", status == B_OK);

	// Now writing should work
	memset(addr, 0xCC, B_PAGE_SIZE);
	DOT_ASSERT("write after protect change",
		((uint8*)addr)[0] == 0xCC);

	kosm_delete_dot(dot);
}


// ===================== INFO =====================

// 7. get_dot_info
static void
test_get_info()
{
	debug_trace("  [test_get_info]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("info_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 2,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_GRAPHICS, 0);

	DOT_REQUIRE("create dot for info", dot >= 0);

	// Touch both pages so they're resident
	memset(addr, 0x11, B_PAGE_SIZE * 2);

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));
	status_t status = kosm_get_dot_info(dot, &info);

	DOT_ASSERT("info ok", status == B_OK);
	DOT_ASSERT("info.name", strcmp(info.name, "info_test") == 0);
	DOT_ASSERT("info.size", info.size == B_PAGE_SIZE * 2);
	DOT_ASSERT("info.tag", info.tag == KOSM_TAG_GRAPHICS);
	DOT_ASSERT("info.address non-zero", info.address != 0);

	debug_trace("    info: base=%p size=%#lx prot=%#x flags=%#x "
		"tag=%u resident=%#lx\n",
		(void*)info.address, info.size, info.protection,
		info.flags, info.tag, info.resident_size);

	kosm_delete_dot(dot);
}


// 8. get_next_dot_info — iterate
static void
test_get_next_info()
{
	debug_trace("  [test_get_next_info]\n");

	// Create 3 dots
	void* addrs[3];
	kosm_dot_id dots[3];
	for (int i = 0; i < 3; i++) {
		char name[32];
		snprintf(name, sizeof(name), "iter_%d", i);
		addrs[i] = NULL;
		dots[i] = kosm_create_dot(name,
			&addrs[i], B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	}

	DOT_REQUIRE("create 3 dots", dots[0] >= 0 && dots[1] >= 0 && dots[2] >= 0);

	// Iterate
	int32 cookie = 0;
	int count = 0;
	kosm_dot_info info;
	while (kosm_get_next_dot_info(getpid(), &cookie, &info) == B_OK) {
		count++;
		if (count <= 20) {
			debug_trace("    iter[%d]: id=%d name=%s size=%#lx\n",
				count, (int)info.kosm_dot, info.name, info.size);
		}
	}
	DOT_ASSERT("found >= 3 dots", count >= 3);

	for (int i = 0; i < 3; i++)
		kosm_delete_dot(dots[i]);
}


// 9. Info on invalid handle
static void
test_info_invalid()
{
	debug_trace("  [test_info_invalid]\n");

	kosm_dot_info info;
	status_t status = kosm_get_dot_info(999999, &info);
	DOT_ASSERT("info on invalid handle fails", status != B_OK);
}


// ===================== PURGEABLE =====================

// 10. Create purgeable dot, mark volatile/nonvolatile
static void
test_purgeable_basic()
{
	debug_trace("  [test_purgeable_basic]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("purgeable",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_PURGEABLE, KOSM_TAG_GRAPHICS, 0);

	DOT_REQUIRE("create purgeable dot", dot >= 0);

	debug_trace("    purgeable dot id=%d, addr=%p\n", (int)dot, addr);
	DOT_REQUIRE("purgeable addr non-null", addr != NULL);

	// Write data
	debug_trace("    about to memset %p, size=%lu\n", addr,
		(unsigned long)(B_PAGE_SIZE * 4));
	memset(addr, 0xDD, B_PAGE_SIZE * 4);

	// Mark volatile
	uint8 oldState = 0xFF;
	status_t status = kosm_dot_mark_volatile(dot, &oldState);
	DOT_ASSERT("mark volatile ok", status == B_OK);
	DOT_ASSERT("old state was nonvolatile",
		oldState == KOSM_PURGE_NONVOLATILE);

	// Mark nonvolatile again
	oldState = 0xFF;
	status = kosm_dot_mark_nonvolatile(dot, &oldState);
	DOT_ASSERT("mark nonvolatile ok", status == B_OK);
	// Old state could be VOLATILE or EMPTY (if system purged it)
	DOT_ASSERT("old state valid",
		oldState == KOSM_PURGE_VOLATILE
		|| oldState == KOSM_PURGE_EMPTY);

	debug_trace("    old state after nonvolatile: %d\n", oldState);

	kosm_delete_dot(dot);
}


// 11. Non-purgeable dot should reject volatile marking
static void
test_purgeable_reject()
{
	debug_trace("  [test_purgeable_reject]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("not_purgeable",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create non-purgeable dot", dot >= 0);

	uint8 oldState = 0;
	status_t status = kosm_dot_mark_volatile(dot, &oldState);
	DOT_ASSERT("mark volatile on non-purgeable fails",
		status == KOSM_DOT_NOT_PURGEABLE);

	kosm_delete_dot(dot);
}


// ===================== WIRE/UNWIRE =====================

// 12. Wire and get physical address
static void
test_wire_phys()
{
	debug_trace("  [test_wire_phys]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("wire_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot for wire", dot >= 0);

	// Touch the page
	memset(addr, 0xEE, B_PAGE_SIZE);

	// Wire it
	status_t status = kosm_dot_wire(dot, 0, B_PAGE_SIZE);
	DOT_ASSERT("wire ok", status == B_OK);

	// Get physical address
	phys_addr_t phys = 0;
	status = kosm_dot_get_phys(dot, 0, &phys);
	DOT_ASSERT("get_phys ok", status == B_OK);
	DOT_ASSERT("phys addr non-zero", phys != 0);
	DOT_ASSERT("phys addr page-aligned", (phys % B_PAGE_SIZE) == 0);

	debug_trace("    physical address: %#lx\n", (unsigned long)phys);

	// Unwire
	status = kosm_dot_unwire(dot, 0, B_PAGE_SIZE);
	DOT_ASSERT("unwire ok", status == B_OK);

	kosm_delete_dot(dot);
}


// 13. Unwire without wire should fail
static void
test_unwire_without_wire()
{
	debug_trace("  [test_unwire_without_wire]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("unwire_fail",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	status_t status = kosm_dot_unwire(dot, 0, B_PAGE_SIZE);
	DOT_ASSERT("unwire without wire fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== SIZES & ALIGNMENT =====================

// 14. Non-page-aligned size should round up or fail
static void
test_size_alignment()
{
	debug_trace("  [test_size_alignment]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("align_test",
		&addr, B_ANY_ADDRESS, 100,  // not page-aligned
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	if (dot >= 0) {
		// Kernel rounded up — check info
		kosm_dot_info info;
		kosm_get_dot_info(dot, &info);
		DOT_ASSERT("size rounded up to page",
			info.size >= B_PAGE_SIZE);
		debug_trace("    requested 100, got size %#lx\n", info.size);
		kosm_delete_dot(dot);
	} else {
		// Kernel rejected non-aligned — also valid
		DOT_ASSERT("rejected non-aligned size", dot < 0);
		debug_trace("    kernel rejected non-aligned size (also ok)\n");
	}
}


// 15. Zero size should fail
static void
test_zero_size()
{
	debug_trace("  [test_zero_size]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("zero_size",
		&addr, B_ANY_ADDRESS, 0,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	DOT_ASSERT("zero-size create fails", dot < 0);

	if (dot >= 0)
		kosm_delete_dot(dot);
}


// 16. Large allocation
static void
test_large_dot()
{
	debug_trace("  [test_large_dot]\n");

	const size_t kSize = 16 * 1024 * 1024;  // 16 MB
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("large_dot",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_GRAPHICS, 0);

	DOT_REQUIRE("create 16MB dot", dot >= 0);

	// Touch first and last page
	((uint8*)addr)[0] = 0x42;
	((uint8*)addr)[kSize - 1] = 0x43;

	DOT_ASSERT("first byte", ((uint8*)addr)[0] == 0x42);
	DOT_ASSERT("last byte", ((uint8*)addr)[kSize - 1] == 0x43);

	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	debug_trace("    16MB dot: base=%p size=%#lx resident=%#lx\n",
		(void*)info.address, info.size, info.resident_size);

	kosm_delete_dot(dot);
}


// ===================== TAGS =====================

// 17. Different tags
static void
test_tags()
{
	debug_trace("  [test_tags]\n");

	struct {
		uint32 tag;
		const char* name;
	} tagTests[] = {
		{ KOSM_TAG_GRAPHICS, "tag_gfx" },
		{ KOSM_TAG_UI, "tag_ui" },
		{ KOSM_TAG_MEDIA, "tag_media" },
		{ KOSM_TAG_NETWORK, "tag_net" },
		{ KOSM_TAG_APP, "tag_app" },
		{ KOSM_TAG_HEAP, "tag_heap" },
		{ KOSM_TAG_STACK, "tag_stack" },
	};

	bool allOk = true;
	for (size_t i = 0; i < sizeof(tagTests) / sizeof(tagTests[0]); i++) {
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot(tagTests[i].name,
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, tagTests[i].tag, 0);

		if (dot < 0) {
			debug_trace("    tag %u failed to create\n", tagTests[i].tag);
			allOk = false;
			continue;
		}

		kosm_dot_info info;
		kosm_get_dot_info(dot, &info);
		if (info.tag != tagTests[i].tag) {
			debug_trace("    tag mismatch: expected %u got %u\n",
				tagTests[i].tag, info.tag);
			allOk = false;
		}
		kosm_delete_dot(dot);
	}
	DOT_ASSERT("all 7 tags correct", allOk);
}


// ===================== MAP/UNMAP =====================

// 18. Unmap a dot — pages become inaccessible
static void
test_unmap()
{
	debug_trace("  [test_unmap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("unmap_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Write data
	memset(addr, 0xAA, B_PAGE_SIZE);
	DOT_ASSERT("write ok", ((uint8*)addr)[0] == 0xAA);

	// Unmap
	status_t status = kosm_unmap_dot(dot);
	DOT_ASSERT("unmap ok", status == B_OK);

	// Second unmap should fail
	status = kosm_unmap_dot(dot);
	DOT_ASSERT("double unmap fails", status != B_OK);

	// Cleanup (delete still works even after unmap)
	kosm_delete_dot(dot);
}


// 19. Map a dot back after unmap
static void
test_remap()
{
	debug_trace("  [test_remap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("remap_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Write data
	memset(addr, 0xBB, B_PAGE_SIZE);

	// Unmap
	status_t status = kosm_unmap_dot(dot);
	DOT_REQUIRE("unmap ok", status == B_OK);

	// Remap
	void* newAddr = NULL;
	status = kosm_map_dot(dot, &newAddr, B_ANY_ADDRESS,
		KOSM_PROT_READ | KOSM_PROT_WRITE);
	DOT_ASSERT("remap ok", status == B_OK);

	if (status == B_OK && newAddr != NULL) {
		// Data should still be there (cache preserved)
		DOT_ASSERT("data preserved after remap",
			((uint8*)newAddr)[0] == 0xBB);
		debug_trace("    original addr=%p, new addr=%p\n", addr, newAddr);
	}

	kosm_delete_dot(dot);
}


// 20. Map with reduced protection
static void
test_map_reduced_prot()
{
	debug_trace("  [test_map_reduced_prot]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("map_prot",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create RW dot", dot >= 0);

	// Write data
	memset(addr, 0xCC, B_PAGE_SIZE);

	// Unmap
	kosm_unmap_dot(dot);

	// Remap as read-only
	void* roAddr = NULL;
	status_t status = kosm_map_dot(dot, &roAddr, B_ANY_ADDRESS,
		KOSM_PROT_READ);
	DOT_ASSERT("remap as RO ok", status == B_OK);

	if (status == B_OK && roAddr != NULL) {
		// Read should work
		DOT_ASSERT("read after RO remap",
			((volatile uint8*)roAddr)[0] == 0xCC);
	}

	kosm_delete_dot(dot);
}


// ===================== CACHE POLICY =====================

// 21. Set cache policy
static void
test_cache_policy()
{
	debug_trace("  [test_cache_policy]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("cache_pol",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Default cache policy should be KOSM_CACHE_DEFAULT
	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	DOT_ASSERT("default cache policy", info.cache_policy == KOSM_CACHE_DEFAULT);

	// Try setting write-combine (may fail on some platforms, that's ok)
	status_t status = kosm_dot_set_cache_policy(dot, KOSM_CACHE_WRITECOMBINE);
	debug_trace("    set_cache_policy(WRITECOMBINE) -> %s\n",
		strerror(status));
	// Just log the result, don't assert — platform-dependent

	// Invalid policy should fail
	status = kosm_dot_set_cache_policy(dot, 0xFF);
	DOT_ASSERT("invalid cache policy fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== FILE-BACKED =====================

// 22. Create file-backed dot, write, sync
static void
test_file_backed()
{
	debug_trace("  [test_file_backed]\n");

	// Create a temp file
	BPath path;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK) {
		debug_trace("    can't find temp dir, skipping\n");
		DOT_ASSERT("find temp dir", false);
		return;
	}
	path.Append("kosm_dot_test_file");

	int fd = open(path.Path(), O_CREAT | O_RDWR | O_TRUNC, 0644);
	DOT_REQUIRE("open temp file", fd >= 0);

	// Extend file to 2 pages
	const size_t kSize = B_PAGE_SIZE * 2;
	ftruncate(fd, kSize);

	// Create file-backed dot
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot_file(fd, 0, "file_dot",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_FILE, KOSM_TAG_APP);

	if (dot < 0) {
		debug_trace("    kosm_create_dot_file not implemented or failed: %s\n",
			strerror(dot));
		DOT_ASSERT("create file dot (may not be implemented)", dot >= 0);
		close(fd);
		unlink(path.Path());
		return;
	}

	// Write pattern
	memset(addr, 0x42, kSize);
	DOT_ASSERT("write to file dot", ((uint8*)addr)[0] == 0x42);

	// Sync
	status_t status = kosm_dot_sync(dot, 0, kSize);
	debug_trace("    kosm_dot_sync -> %s\n", strerror(status));
	DOT_ASSERT("sync ok", status == B_OK);

	// Verify via info
	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	DOT_ASSERT("file dot flags", (info.flags & KOSM_DOT_FILE) != 0);

	kosm_delete_dot(dot);
	close(fd);
	unlink(path.Path());
}


// 23. Sync on non-file dot should fail
static void
test_sync_non_file()
{
	debug_trace("  [test_sync_non_file]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("sync_fail",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	status_t status = kosm_dot_sync(dot, 0, B_PAGE_SIZE);
	DOT_ASSERT("sync on non-file fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== STRESS =====================

// 24. Create/delete many dots rapidly
static void
test_stress_create_delete()
{
	debug_trace("  [test_stress_create_delete]\n");

	const int kCount = 64;
	kosm_dot_id dots[kCount];
	void* addrs[kCount];
	int created = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kCount; i++) {
		char name[32];
		snprintf(name, sizeof(name), "stress_%d", i);
		addrs[i] = NULL;
		dots[i] = kosm_create_dot(name,
			&addrs[i], B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dots[i] >= 0)
			created++;
	}

	bigtime_t createTime = system_time() - t0;

	DOT_ASSERT("created all 64", created == kCount);
	debug_trace("    created %d dots in %lld us\n",
		created, (long long)createTime);

	// Touch each one
	for (int i = 0; i < kCount; i++) {
		if (dots[i] >= 0 && addrs[i] != NULL)
			((uint8*)addrs[i])[0] = (uint8)i;
	}

	// Delete all
	t0 = system_time();
	int deleted = 0;
	for (int i = 0; i < kCount; i++) {
		if (dots[i] >= 0) {
			if (kosm_delete_dot(dots[i]) == B_OK)
				deleted++;
		}
	}
	bigtime_t deleteTime = system_time() - t0;

	DOT_ASSERT("deleted all", deleted == created);
	debug_trace("    deleted %d dots in %lld us\n",
		deleted, (long long)deleteTime);
}


// 19. Thread contention — multiple threads creating/deleting dots
struct thread_dot_args {
	int		thread_index;
	int		count;
	int		created;
	int		deleted;
};

static status_t
dot_thread_func(void* data)
{
	thread_dot_args* args = (thread_dot_args*)data;
	args->created = 0;
	args->deleted = 0;

	for (int i = 0; i < args->count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "t%d_%d", args->thread_index, i);
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot(name,
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dot >= 0) {
			args->created++;
			if (addr != NULL)
				((uint8*)addr)[0] = (uint8)i;
			if (kosm_delete_dot(dot) == B_OK)
				args->deleted++;
		}
	}
	return B_OK;
}

static void
test_stress_threaded()
{
	debug_trace("  [test_stress_threaded]\n");

	const int kThreads = 4;
	const int kPerThread = 32;
	thread_id threads[kThreads];
	thread_dot_args args[kThreads];

	bigtime_t t0 = system_time();

	for (int i = 0; i < kThreads; i++) {
		args[i].thread_index = i;
		args[i].count = kPerThread;
		threads[i] = spawn_thread(dot_thread_func,
			"dot_stress", B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	bigtime_t elapsed = system_time() - t0;

	int totalCreated = 0, totalDeleted = 0;
	for (int i = 0; i < kThreads; i++) {
		totalCreated += args[i].created;
		totalDeleted += args[i].deleted;
		debug_trace("    thread %d: created=%d deleted=%d\n",
			i, args[i].created, args[i].deleted);
	}

	DOT_ASSERT("all threads created ok",
		totalCreated == kThreads * kPerThread);
	DOT_ASSERT("all threads deleted ok",
		totalDeleted == kThreads * kPerThread);

	debug_trace("    %d threads x %d dots in %lld us\n",
		kThreads, kPerThread, (long long)elapsed);
}


// 25. Page fault storm — 8 threads hammering same large dot
struct fault_storm_args {
	void*		addr;
	size_t		size;
	int			thread_index;
	int			iterations;
	int			errors;
};

static status_t
fault_storm_func(void* data)
{
	fault_storm_args* args = (fault_storm_args*)data;
	args->errors = 0;
	uint8* base = (uint8*)args->addr;
	size_t pages = args->size / B_PAGE_SIZE;
	uint8 tag = (uint8)(args->thread_index + 1);

	for (int iter = 0; iter < args->iterations; iter++) {
		// Touch all pages in strided pattern to create fault storms
		for (size_t p = args->thread_index; p < pages; p += 8) {
			base[p * B_PAGE_SIZE] = tag;
			if (base[p * B_PAGE_SIZE] != tag)
				args->errors++;
		}
	}
	return B_OK;
}

static void
test_stress_fault_storm()
{
	debug_trace("  [test_stress_fault_storm]\n");

	const size_t kSize = B_PAGE_SIZE * 64; // 64 pages
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("fault_storm",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create 64-page dot", dot >= 0);

	const int kThreads = 8;
	const int kIter = 50;
	thread_id threads[kThreads];
	fault_storm_args args[kThreads];

	bigtime_t t0 = system_time();

	for (int i = 0; i < kThreads; i++) {
		args[i].addr = addr;
		args[i].size = kSize;
		args[i].thread_index = i;
		args[i].iterations = kIter;
		threads[i] = spawn_thread(fault_storm_func,
			"fault_storm", B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	bigtime_t elapsed = system_time() - t0;

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++)
		totalErrors += args[i].errors;

	DOT_ASSERT("no data corruption", totalErrors == 0);
	debug_trace("    8 threads x %d iter x 64 pages in %lld us\n",
		kIter, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 26. Memory pressure — allocate many large dots until failure
static void
test_stress_memory_pressure()
{
	debug_trace("  [test_stress_memory_pressure]\n");

	const int kMaxDots = 128;
	const size_t kDotSize = B_PAGE_SIZE * 16; // 64KB each, up to 8MB total
	kosm_dot_id dots[kMaxDots];
	void* addrs[kMaxDots];
	int created = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kMaxDots; i++) {
		char name[32];
		snprintf(name, sizeof(name), "pressure_%d", i);
		addrs[i] = NULL;
		dots[i] = kosm_create_dot(name,
			&addrs[i], B_ANY_ADDRESS, kDotSize,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dots[i] < 0)
			break;
		created++;
		// Touch first page to fault it in
		((uint8*)addrs[i])[0] = (uint8)i;
	}

	bigtime_t allocTime = system_time() - t0;

	DOT_ASSERT("created at least 32 dots", created >= 32);
	debug_trace("    allocated %d dots (%lu KB each) in %lld us\n",
		created, (unsigned long)(kDotSize / 1024), (long long)allocTime);

	// Verify all still readable
	int verifyErrors = 0;
	for (int i = 0; i < created; i++) {
		if (((uint8*)addrs[i])[0] != (uint8)i)
			verifyErrors++;
	}
	DOT_ASSERT("all dots readable after pressure", verifyErrors == 0);

	// Delete all in reverse order (exercises different free patterns)
	t0 = system_time();
	for (int i = created - 1; i >= 0; i--)
		kosm_delete_dot(dots[i]);
	bigtime_t freeTime = system_time() - t0;

	debug_trace("    freed %d dots in %lld us\n",
		created, (long long)freeTime);
}


// 27. Protect thrash — rapidly toggle RO/RW from multiple threads
struct protect_thrash_args {
	kosm_dot_id	dot;
	int			iterations;
	int			errors;
};

static status_t
protect_thrash_func(void* data)
{
	protect_thrash_args* args = (protect_thrash_args*)data;
	args->errors = 0;

	for (int i = 0; i < args->iterations; i++) {
		status_t s;
		if (i % 2 == 0)
			s = kosm_protect_dot(args->dot, KOSM_PROT_READ);
		else
			s = kosm_protect_dot(args->dot,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
		if (s != B_OK)
			args->errors++;
	}
	return B_OK;
}

static void
test_stress_protect_thrash()
{
	debug_trace("  [test_stress_protect_thrash]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("prot_thrash",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Touch so page is resident
	memset(addr, 0x55, B_PAGE_SIZE);

	const int kThreads = 4;
	const int kIter = 200;
	thread_id threads[kThreads];
	protect_thrash_args args[kThreads];

	bigtime_t t0 = system_time();

	for (int i = 0; i < kThreads; i++) {
		args[i].dot = dot;
		args[i].iterations = kIter;
		threads[i] = spawn_thread(protect_thrash_func,
			"prot_thrash", B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	bigtime_t elapsed = system_time() - t0;

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++)
		totalErrors += args[i].errors;

	DOT_ASSERT("no protect errors", totalErrors == 0);

	// Restore RW and verify data
	kosm_protect_dot(dot, KOSM_PROT_READ | KOSM_PROT_WRITE);
	DOT_ASSERT("data survived thrash", ((uint8*)addr)[0] == 0x55);

	debug_trace("    4 threads x %d protects in %lld us\n",
		kIter, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 28. Map/unmap cycle stress
static void
test_stress_map_unmap()
{
	debug_trace("  [test_stress_map_unmap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("map_stress",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Write initial data
	memset(addr, 0xAA, B_PAGE_SIZE * 4);

	const int kCycles = 50;
	int errors = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kCycles; i++) {
		status_t s = kosm_unmap_dot(dot);
		if (s != B_OK) {
			errors++;
			break;
		}

		void* newAddr = NULL;
		s = kosm_map_dot(dot, &newAddr, B_ANY_ADDRESS,
			KOSM_PROT_READ | KOSM_PROT_WRITE);
		if (s != B_OK || newAddr == NULL) {
			errors++;
			break;
		}

		// Verify data preserved
		if (((uint8*)newAddr)[0] != 0xAA)
			errors++;

		// Update pattern
		((uint8*)newAddr)[0] = 0xAA;
		addr = newAddr;
	}

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("no map/unmap errors", errors == 0);
	debug_trace("    %d map/unmap cycles in %lld us\n",
		kCycles, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 29. Wire/unwire stress — repeated wire/unwire with phys addr check
static void
test_stress_wire_unwire()
{
	debug_trace("  [test_stress_wire_unwire]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("wire_stress",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create dot", dot >= 0);

	// Touch all pages
	memset(addr, 0x77, B_PAGE_SIZE * 4);

	const int kCycles = 100;
	int errors = 0;
	phys_addr_t prevPhys = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kCycles; i++) {
		status_t s = kosm_dot_wire(dot, 0, B_PAGE_SIZE * 4);
		if (s != B_OK) {
			errors++;
			debug_trace("    wire failed at iter %d: %s\n",
				i, strerror(s));
			break;
		}

		phys_addr_t phys = 0;
		s = kosm_dot_get_phys(dot, 0, &phys);
		if (s != B_OK || phys == 0)
			errors++;

		// Physical address should be stable for same page
		if (prevPhys != 0 && phys != prevPhys)
			errors++;
		prevPhys = phys;

		s = kosm_dot_unwire(dot, 0, B_PAGE_SIZE * 4);
		if (s != B_OK)
			errors++;
	}

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("no wire/unwire errors", errors == 0);
	debug_trace("    %d wire/unwire cycles in %lld us, phys=%#lx\n",
		kCycles, (long long)elapsed, (unsigned long)prevPhys);

	kosm_delete_dot(dot);
}


// 30. Data integrity — fill patterns, verify after operations
static void
test_stress_data_integrity()
{
	debug_trace("  [test_stress_data_integrity]\n");

	const size_t kSize = B_PAGE_SIZE * 32; // 128KB
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("integrity",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	DOT_REQUIRE("create 32-page dot", dot >= 0);

	// Fill with known pattern: each page gets its page index
	for (size_t p = 0; p < 32; p++)
		memset((uint8*)addr + p * B_PAGE_SIZE, (uint8)(p + 1), B_PAGE_SIZE);

	// Verify all pages
	int errors = 0;
	for (size_t p = 0; p < 32; p++) {
		uint8 expected = (uint8)(p + 1);
		uint8* page = (uint8*)addr + p * B_PAGE_SIZE;
		for (size_t b = 0; b < B_PAGE_SIZE; b++) {
			if (page[b] != expected) {
				errors++;
				break; // one error per page is enough
			}
		}
	}
	DOT_ASSERT("initial fill correct", errors == 0);

	// Overwrite with inverse pattern
	for (size_t p = 0; p < 32; p++)
		memset((uint8*)addr + p * B_PAGE_SIZE, (uint8)(255 - p), B_PAGE_SIZE);

	// Verify inverse
	errors = 0;
	for (size_t p = 0; p < 32; p++) {
		uint8 expected = (uint8)(255 - p);
		uint8* page = (uint8*)addr + p * B_PAGE_SIZE;
		// Check first, middle, last byte
		if (page[0] != expected || page[B_PAGE_SIZE / 2] != expected
			|| page[B_PAGE_SIZE - 1] != expected)
			errors++;
	}
	DOT_ASSERT("inverse fill correct", errors == 0);

	// Protect change + verify (ensure TLB flush doesn't corrupt)
	kosm_protect_dot(dot, KOSM_PROT_READ);
	errors = 0;
	for (size_t p = 0; p < 32; p++) {
		volatile uint8* page = (volatile uint8*)addr + p * B_PAGE_SIZE;
		if (page[0] != (uint8)(255 - p))
			errors++;
	}
	DOT_ASSERT("data survives protect change", errors == 0);

	kosm_delete_dot(dot);
}


// 31. Concurrent info iteration + create/delete
struct info_churn_args {
	int		iterations;
	int		created;
	int		deleted;
};

static status_t
info_churn_creator(void* data)
{
	info_churn_args* args = (info_churn_args*)data;
	args->created = 0;
	args->deleted = 0;

	for (int i = 0; i < args->iterations; i++) {
		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot(name,
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dot >= 0) {
			args->created++;
			if (addr != NULL)
				((uint8*)addr)[0] = 0x42;
			snooze(100); // small delay to interleave with iterator
			if (kosm_delete_dot(dot) == B_OK)
				args->deleted++;
		}
	}
	return B_OK;
}

struct info_iter_args {
	int		iterations;
	int		total_found;
	int		errors;
};

static status_t
info_churn_iterator(void* data)
{
	info_iter_args* args = (info_iter_args*)data;
	args->total_found = 0;
	args->errors = 0;

	for (int i = 0; i < args->iterations; i++) {
		int32 cookie = 0;
		kosm_dot_info info;
		int count = 0;
		while (kosm_get_next_dot_info(getpid(), &cookie, &info) == B_OK) {
			count++;
			if (count > 500) {
				args->errors++;
				break; // infinite loop guard
			}
		}
		args->total_found += count;
	}
	return B_OK;
}

static void
test_stress_info_during_churn()
{
	debug_trace("  [test_stress_info_during_churn]\n");

	info_churn_args creatorArgs;
	creatorArgs.iterations = 50;

	info_iter_args iterArgs;
	iterArgs.iterations = 20;

	bigtime_t t0 = system_time();

	thread_id creator = spawn_thread(info_churn_creator,
		"dot_churn", B_NORMAL_PRIORITY, &creatorArgs);
	thread_id iterator = spawn_thread(info_churn_iterator,
		"dot_iter", B_NORMAL_PRIORITY, &iterArgs);

	resume_thread(creator);
	resume_thread(iterator);

	status_t exitVal;
	wait_for_thread(creator, &exitVal);
	wait_for_thread(iterator, &exitVal);

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("creator ok", creatorArgs.created == creatorArgs.deleted);
	DOT_ASSERT("iterator no errors", iterArgs.errors == 0);
	debug_trace("    creator: %d created/deleted, iterator: %d found total, "
		"%lld us\n", creatorArgs.created, iterArgs.total_found,
		(long long)elapsed);
}


// 32. Purgeable volatile/nonvolatile cycle stress
static void
test_stress_purgeable_cycle()
{
	debug_trace("  [test_stress_purgeable_cycle]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("purge_cycle",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_PURGEABLE, KOSM_TAG_GRAPHICS, 0);

	DOT_REQUIRE("create purgeable dot", dot >= 0);
	DOT_REQUIRE("purgeable addr ok", addr != NULL);

	// Initial fill
	memset(addr, 0xBB, B_PAGE_SIZE * 4);

	const int kCycles = 100;
	int errors = 0;
	int purgeCount = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kCycles; i++) {
		// Mark volatile
		uint8 oldState = 0xFF;
		status_t s = kosm_dot_mark_volatile(dot, &oldState);
		if (s != B_OK) {
			errors++;
			break;
		}

		// Mark nonvolatile
		oldState = 0xFF;
		s = kosm_dot_mark_nonvolatile(dot, &oldState);
		if (s != B_OK) {
			errors++;
			break;
		}

		if (oldState == KOSM_PURGE_EMPTY)
			purgeCount++;

		// Refill if purged (pages come back zeroed)
		memset(addr, 0xBB, B_PAGE_SIZE * 4);
	}

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("no cycle errors", errors == 0);
	DOT_ASSERT("data accessible after cycles",
		((uint8*)addr)[0] == 0xBB);

	debug_trace("    %d cycles, %d purges, %lld us\n",
		kCycles, purgeCount, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 33. Mixed operations — all API calls interleaved
static void
test_stress_mixed_ops()
{
	debug_trace("  [test_stress_mixed_ops]\n");

	const int kDots = 8;
	kosm_dot_id dots[kDots];
	void* addrs[kDots];
	int errors = 0;

	bigtime_t t0 = system_time();

	// Phase 1: create all
	for (int i = 0; i < kDots; i++) {
		char name[32];
		snprintf(name, sizeof(name), "mixed_%d", i);
		addrs[i] = NULL;
		dots[i] = kosm_create_dot(name,
			&addrs[i], B_ANY_ADDRESS, B_PAGE_SIZE * 2,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dots[i] < 0)
			errors++;
	}

	// Phase 2: write patterns
	for (int i = 0; i < kDots; i++) {
		if (dots[i] >= 0 && addrs[i] != NULL)
			memset(addrs[i], (uint8)(i + 0x10), B_PAGE_SIZE * 2);
	}

	// Phase 3: get info on all
	for (int i = 0; i < kDots; i++) {
		if (dots[i] >= 0) {
			kosm_dot_info info;
			if (kosm_get_dot_info(dots[i], &info) != B_OK)
				errors++;
		}
	}

	// Phase 4: wire half, protect other half
	for (int i = 0; i < kDots; i++) {
		if (dots[i] < 0)
			continue;
		if (i % 2 == 0) {
			if (kosm_dot_wire(dots[i], 0, B_PAGE_SIZE) != B_OK)
				errors++;
		} else {
			if (kosm_protect_dot(dots[i], KOSM_PROT_READ) != B_OK)
				errors++;
		}
	}

	// Phase 5: verify data still accessible (read-only for odd)
	for (int i = 0; i < kDots; i++) {
		if (dots[i] >= 0 && addrs[i] != NULL) {
			volatile uint8* p = (volatile uint8*)addrs[i];
			if (p[0] != (uint8)(i + 0x10))
				errors++;
		}
	}

	// Phase 6: unwire, restore RW
	for (int i = 0; i < kDots; i++) {
		if (dots[i] < 0)
			continue;
		if (i % 2 == 0)
			kosm_dot_unwire(dots[i], 0, B_PAGE_SIZE);
		else
			kosm_protect_dot(dots[i], KOSM_PROT_READ | KOSM_PROT_WRITE);
	}

	// Phase 7: unmap/remap half
	for (int i = 0; i < kDots; i += 2) {
		if (dots[i] < 0)
			continue;
		kosm_unmap_dot(dots[i]);
		void* newAddr = NULL;
		status_t s = kosm_map_dot(dots[i], &newAddr, B_ANY_ADDRESS,
			KOSM_PROT_READ | KOSM_PROT_WRITE);
		if (s == B_OK && newAddr != NULL) {
			if (((uint8*)newAddr)[0] != (uint8)(i + 0x10))
				errors++;
			addrs[i] = newAddr;
		} else {
			errors++;
		}
	}

	// Phase 8: delete all
	for (int i = 0; i < kDots; i++) {
		if (dots[i] >= 0)
			kosm_delete_dot(dots[i]);
	}

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("no mixed op errors", errors == 0);
	debug_trace("    8 dots x 8 phases in %lld us\n", (long long)elapsed);
}


// 34. Rapid create/touch/delete single-page — throughput test
static void
test_stress_throughput()
{
	debug_trace("  [test_stress_throughput]\n");

	const int kCount = 500;
	int errors = 0;

	bigtime_t t0 = system_time();

	for (int i = 0; i < kCount; i++) {
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot("thru",
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);
		if (dot < 0 || addr == NULL) {
			errors++;
			if (dot >= 0)
				kosm_delete_dot(dot);
			continue;
		}
		((uint8*)addr)[0] = 0xFF;
		if (kosm_delete_dot(dot) != B_OK)
			errors++;
	}

	bigtime_t elapsed = system_time() - t0;

	DOT_ASSERT("no throughput errors", errors == 0);

	double opsPerSec = (double)kCount * 1000000.0 / (double)elapsed;
	debug_trace("    %d create/touch/delete in %lld us (%.0f ops/s)\n",
		kCount, (long long)elapsed, opsPerSec);
	DOT_ASSERT("throughput > 100 ops/s", opsPerSec > 100.0);
}


// ===================== SUITE =====================

static const TestEntry sDotTests[] = {
	// Basic
	{ "create/delete",          test_create_delete,        "Basic" },
	{ "create named",           test_create_named,         "Basic" },
	{ "multi page R/W",         test_multi_page,           "Basic" },
	{ "double delete",          test_double_delete,        "Basic" },
	{ "read-only protection",   test_read_only,            "Basic" },
	// Protection
	{ "protect change RO->RW",  test_protect_change,       "Protection" },
	// Info
	{ "get_dot_info",           test_get_info,             "Info" },
	{ "get_next_dot_info",      test_get_next_info,        "Info" },
	{ "info invalid handle",    test_info_invalid,         "Info" },
	// Purgeable
	{ "purgeable basic",        test_purgeable_basic,      "Purgeable" },
	{ "purgeable reject",       test_purgeable_reject,     "Purgeable" },
	// Wire/Phys
	{ "wire + get_phys",        test_wire_phys,            "Wire" },
	{ "unwire without wire",    test_unwire_without_wire,  "Wire" },
	// Map/Unmap
	{ "unmap dot",              test_unmap,                "Map/Unmap" },
	{ "remap after unmap",      test_remap,                "Map/Unmap" },
	{ "map with reduced prot",  test_map_reduced_prot,     "Map/Unmap" },
	// Cache Policy
	{ "set cache policy",       test_cache_policy,         "Cache Policy" },
	// Sizes
	{ "size alignment",         test_size_alignment,       "Sizes" },
	{ "zero size",              test_zero_size,            "Sizes" },
	{ "large 16MB",             test_large_dot,            "Sizes" },
	// Tags
	{ "all tags",               test_tags,                 "Tags" },
	// File-backed
	{ "file-backed dot+sync",   test_file_backed,          "File" },
	{ "sync non-file fails",    test_sync_non_file,        "File" },
	// Stress
	{ "create/delete 64x",      test_stress_create_delete, "STRESS" },
	{ "4 threads x 32",         test_stress_threaded,      "STRESS" },
	{ "fault storm 8 threads",  test_stress_fault_storm,   "STRESS" },
	{ "memory pressure 128x",   test_stress_memory_pressure, "STRESS" },
	{ "protect thrash 4x200",   test_stress_protect_thrash, "STRESS" },
	{ "map/unmap 50 cycles",    test_stress_map_unmap,     "STRESS" },
	{ "wire/unwire 100 cycles", test_stress_wire_unwire,   "STRESS" },
	{ "data integrity 32pg",    test_stress_data_integrity, "STRESS" },
	{ "info during churn",      test_stress_info_during_churn, "STRESS" },
	{ "purgeable cycle 100x",   test_stress_purgeable_cycle, "STRESS" },
	{ "mixed ops 8 phases",     test_stress_mixed_ops,     "STRESS" },
	{ "throughput 500x",        test_stress_throughput,    "STRESS" },
};


TestSuite
get_dot_test_suite()
{
	TestSuite suite;
	suite.name = "KosmDot";
	suite.tests = sDotTests;
	suite.count = sizeof(sDotTests) / sizeof(sDotTests[0]);
	return suite;
}
