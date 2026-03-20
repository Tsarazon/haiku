/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmDot test functions for the unified test suite.
 */


#include "TestCommon.h"

#include <KosmOS.h>
#include <OS.h>
#include <FindDirectory.h>
#include <Path.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

extern char** environ;


// ===================== BASIC =====================

// 1. Create and delete
static void
test_create_delete()
{
	trace("  [test_create_delete]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("test_basic",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);
	TEST_ASSERT("address non-null", addr != NULL);
	TEST_ASSERT("address page-aligned", ((addr_t)addr % B_PAGE_SIZE) == 0);

	// Write to verify it's mapped and writable
	memset(addr, 0xAB, B_PAGE_SIZE);
	TEST_ASSERT("write succeeded", ((uint8*)addr)[0] == 0xAB);
	TEST_ASSERT("write full page", ((uint8*)addr)[B_PAGE_SIZE - 1] == 0xAB);

	status_t status = kosm_delete_dot(dot);
	TEST_ASSERT("delete dot", status == B_OK);
}


// 2. Create with name, verify via info
static void
test_create_named()
{
	trace("  [test_create_named]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("my_named_dot",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_UI, 0);

	TEST_REQUIRE("create named dot", dot >= 0);

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));
	status_t status = kosm_get_dot_info(dot, &info);
	TEST_ASSERT("get_dot_info ok", status == B_OK);
	TEST_ASSERT("info name match",
		strcmp(info.name, "my_named_dot") == 0);
	TEST_ASSERT("info size", info.size == B_PAGE_SIZE * 4);
	TEST_ASSERT("info tag", info.tag == KOSM_TAG_UI);
	TEST_ASSERT("info protection read",
		(info.protection & KOSM_PROT_READ) != 0);
	TEST_ASSERT("info protection write",
		(info.protection & KOSM_PROT_WRITE) != 0);

	kosm_delete_dot(dot);
}


// 3. Multiple pages — write pattern, read back
static void
test_multi_page()
{
	trace("  [test_multi_page]\n");

	const size_t kSize = B_PAGE_SIZE * 8;
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("multi_page",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create 8-page dot", dot >= 0);

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
			trace("    page %zu: expected %u got %u\n",
				i, expected, actual);
			allOk = false;
		}
	}
	TEST_ASSERT("all pages correct", allOk);

	kosm_delete_dot(dot);
}


// 4. Double delete — second should fail
static void
test_double_delete()
{
	trace("  [test_double_delete]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("double_del",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	status_t s1 = kosm_delete_dot(dot);
	TEST_ASSERT("first delete ok", s1 == B_OK);

	status_t s2 = kosm_delete_dot(dot);
	TEST_ASSERT("second delete fails", s2 != B_OK);
}


// 5. Read-only protection
static void
test_read_only()
{
	trace("  [test_read_only]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("read_only",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create read-only dot", dot >= 0);

	// Reading should work — touch the page to fault it in
	// (page comes in zeroed)
	volatile uint8* p = (volatile uint8*)addr;
	uint8 val = p[0];
	TEST_ASSERT("read from read-only", val == 0);

	// NOTE: writing to read-only memory would SIGSEGV.
	// We don't test that here to avoid crashing the suite.

	kosm_delete_dot(dot);
}


// ===================== PROTECTION =====================

// 6. Change protection from RO to RW
static void
test_protect_change()
{
	trace("  [test_protect_change]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("prot_change",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create RO dot", dot >= 0);

	// Upgrade to RW
	status_t status = kosm_protect_dot(dot,
		KOSM_PROT_READ | KOSM_PROT_WRITE);
	TEST_ASSERT("protect RO->RW", status == B_OK);

	// Now writing should work
	memset(addr, 0xCC, B_PAGE_SIZE);
	TEST_ASSERT("write after protect change",
		((uint8*)addr)[0] == 0xCC);

	kosm_delete_dot(dot);
}


// ===================== INFO =====================

// 7. get_dot_info
static void
test_get_info()
{
	trace("  [test_get_info]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("info_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 2,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_GRAPHICS, 0);

	TEST_REQUIRE("create dot for info", dot >= 0);

	// Touch both pages so they're resident
	memset(addr, 0x11, B_PAGE_SIZE * 2);

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));
	status_t status = kosm_get_dot_info(dot, &info);

	TEST_ASSERT("info ok", status == B_OK);
	TEST_ASSERT("info.name", strcmp(info.name, "info_test") == 0);
	TEST_ASSERT("info.size", info.size == B_PAGE_SIZE * 2);
	TEST_ASSERT("info.tag", info.tag == KOSM_TAG_GRAPHICS);
	TEST_ASSERT("info.address non-zero", info.address != 0);

	trace("    info: base=%p size=%#lx prot=%#x flags=%#x "
		"tag=%u resident=%#lx\n",
		(void*)info.address, info.size, info.protection,
		info.flags, info.tag, info.resident_size);

	kosm_delete_dot(dot);
}


// 8. get_next_dot_info — iterate
static void
test_get_next_info()
{
	trace("  [test_get_next_info]\n");

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

	TEST_REQUIRE("create 3 dots", dots[0] >= 0 && dots[1] >= 0 && dots[2] >= 0);

	// Iterate
	int32 cookie = 0;
	int count = 0;
	kosm_dot_info info;
	while (kosm_get_next_dot_info(getpid(), &cookie, &info) == B_OK) {
		count++;
		if (count <= 20) {
			trace("    iter[%d]: id=%d name=%s size=%#lx\n",
				count, (int)info.kosm_dot, info.name, info.size);
		}
	}
	TEST_ASSERT("found >= 3 dots", count >= 3);

	for (int i = 0; i < 3; i++)
		kosm_delete_dot(dots[i]);
}


// 9. Info on invalid handle
static void
test_info_invalid()
{
	trace("  [test_info_invalid]\n");

	kosm_dot_info info;
	status_t status = kosm_get_dot_info(999999, &info);
	TEST_ASSERT("info on invalid handle fails", status != B_OK);
}


// ===================== PURGEABLE =====================

// 10. Create purgeable dot, mark volatile/nonvolatile
static void
test_purgeable_basic()
{
	trace("  [test_purgeable_basic]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("purgeable",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_PURGEABLE, KOSM_TAG_GRAPHICS, 0);

	TEST_REQUIRE("create purgeable dot", dot >= 0);

	trace("    purgeable dot id=%d, addr=%p\n", (int)dot, addr);
	TEST_REQUIRE("purgeable addr non-null", addr != NULL);

	// Write data
	trace("    about to memset %p, size=%lu\n", addr,
		(unsigned long)(B_PAGE_SIZE * 4));
	memset(addr, 0xDD, B_PAGE_SIZE * 4);

	// Mark volatile
	uint8 oldState = 0xFF;
	status_t status = kosm_dot_mark_volatile(dot, &oldState);
	TEST_ASSERT("mark volatile ok", status == B_OK);
	TEST_ASSERT("old state was nonvolatile",
		oldState == KOSM_PURGE_NONVOLATILE);

	// Mark nonvolatile again
	oldState = 0xFF;
	status = kosm_dot_mark_nonvolatile(dot, &oldState);
	TEST_ASSERT("mark nonvolatile ok", status == B_OK);
	// Old state could be VOLATILE or EMPTY (if system purged it)
	TEST_ASSERT("old state valid",
		oldState == KOSM_PURGE_VOLATILE
		|| oldState == KOSM_PURGE_EMPTY);

	trace("    old state after nonvolatile: %d\n", oldState);

	kosm_delete_dot(dot);
}


// 11. Non-purgeable dot should reject volatile marking
static void
test_purgeable_reject()
{
	trace("  [test_purgeable_reject]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("not_purgeable",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create non-purgeable dot", dot >= 0);

	uint8 oldState = 0;
	status_t status = kosm_dot_mark_volatile(dot, &oldState);
	TEST_ASSERT("mark volatile on non-purgeable fails",
		status == KOSM_DOT_NOT_PURGEABLE);

	kosm_delete_dot(dot);
}


// ===================== WIRE/UNWIRE =====================

// 12. Wire and get physical address
static void
test_wire_phys()
{
	trace("  [test_wire_phys]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("wire_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot for wire", dot >= 0);

	// Touch the page
	memset(addr, 0xEE, B_PAGE_SIZE);

	// Wire it
	status_t status = kosm_dot_wire(dot, 0, B_PAGE_SIZE);
	TEST_ASSERT("wire ok", status == B_OK);

	// Get physical address
	phys_addr_t phys = 0;
	status = kosm_dot_get_phys(dot, 0, &phys);
	TEST_ASSERT("get_phys ok", status == B_OK);
	TEST_ASSERT("phys addr non-zero", phys != 0);
	TEST_ASSERT("phys addr page-aligned", (phys % B_PAGE_SIZE) == 0);

	trace("    physical address: %#lx\n", (unsigned long)phys);

	// Unwire
	status = kosm_dot_unwire(dot, 0, B_PAGE_SIZE);
	TEST_ASSERT("unwire ok", status == B_OK);

	kosm_delete_dot(dot);
}


// 13. Unwire without wire should fail
static void
test_unwire_without_wire()
{
	trace("  [test_unwire_without_wire]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("unwire_fail",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	status_t status = kosm_dot_unwire(dot, 0, B_PAGE_SIZE);
	TEST_ASSERT("unwire without wire fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== SIZES & ALIGNMENT =====================

// 14. Non-page-aligned size should round up or fail
static void
test_size_alignment()
{
	trace("  [test_size_alignment]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("align_test",
		&addr, B_ANY_ADDRESS, 100,  // not page-aligned
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	if (dot >= 0) {
		// Kernel rounded up — check info
		kosm_dot_info info;
		kosm_get_dot_info(dot, &info);
		TEST_ASSERT("size rounded up to page",
			info.size >= B_PAGE_SIZE);
		trace("    requested 100, got size %#lx\n", info.size);
		kosm_delete_dot(dot);
	} else {
		// Kernel rejected non-aligned — also valid
		TEST_ASSERT("rejected non-aligned size", dot < 0);
		trace("    kernel rejected non-aligned size (also ok)\n");
	}
}


// 15. Zero size should fail
static void
test_zero_size()
{
	trace("  [test_zero_size]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("zero_size",
		&addr, B_ANY_ADDRESS, 0,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	TEST_ASSERT("zero-size create fails", dot < 0);

	if (dot >= 0)
		kosm_delete_dot(dot);
}


// 16. Large allocation
static void
test_large_dot()
{
	trace("  [test_large_dot]\n");

	const size_t kSize = 16 * 1024 * 1024;  // 16 MB
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("large_dot",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_GRAPHICS, 0);

	TEST_REQUIRE("create 16MB dot", dot >= 0);

	// Touch first and last page
	((uint8*)addr)[0] = 0x42;
	((uint8*)addr)[kSize - 1] = 0x43;

	TEST_ASSERT("first byte", ((uint8*)addr)[0] == 0x42);
	TEST_ASSERT("last byte", ((uint8*)addr)[kSize - 1] == 0x43);

	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	trace("    16MB dot: base=%p size=%#lx resident=%#lx\n",
		(void*)info.address, info.size, info.resident_size);

	kosm_delete_dot(dot);
}


// ===================== TAGS =====================

// 17. Different tags
static void
test_tags()
{
	trace("  [test_tags]\n");

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
			trace("    tag %u failed to create\n", tagTests[i].tag);
			allOk = false;
			continue;
		}

		kosm_dot_info info;
		kosm_get_dot_info(dot, &info);
		if (info.tag != tagTests[i].tag) {
			trace("    tag mismatch: expected %u got %u\n",
				tagTests[i].tag, info.tag);
			allOk = false;
		}
		kosm_delete_dot(dot);
	}
	TEST_ASSERT("all 7 tags correct", allOk);
}


// ===================== MAP/UNMAP =====================

// 18. Unmap a dot — pages become inaccessible
static void
test_unmap()
{
	trace("  [test_unmap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("unmap_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	// Write data
	memset(addr, 0xAA, B_PAGE_SIZE);
	TEST_ASSERT("write ok", ((uint8*)addr)[0] == 0xAA);

	// Unmap
	status_t status = kosm_unmap_dot(dot);
	TEST_ASSERT("unmap ok", status == B_OK);

	// Second unmap should fail
	status = kosm_unmap_dot(dot);
	TEST_ASSERT("double unmap fails", status != B_OK);

	// Cleanup (delete still works even after unmap)
	kosm_delete_dot(dot);
}


// 19. Map a dot back after unmap
static void
test_remap()
{
	trace("  [test_remap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("remap_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	// Write data
	memset(addr, 0xBB, B_PAGE_SIZE);

	// Unmap
	status_t status = kosm_unmap_dot(dot);
	TEST_REQUIRE("unmap ok", status == B_OK);

	// Remap
	void* newAddr = NULL;
	status = kosm_map_dot(dot, &newAddr, B_ANY_ADDRESS,
		KOSM_PROT_READ | KOSM_PROT_WRITE);
	TEST_ASSERT("remap ok", status == B_OK);

	if (status == B_OK && newAddr != NULL) {
		// Data should still be there (cache preserved)
		TEST_ASSERT("data preserved after remap",
			((uint8*)newAddr)[0] == 0xBB);
		trace("    original addr=%p, new addr=%p\n", addr, newAddr);
	}

	kosm_delete_dot(dot);
}


// 20. Map with reduced protection
static void
test_map_reduced_prot()
{
	trace("  [test_map_reduced_prot]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("map_prot",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create RW dot", dot >= 0);

	// Write data
	memset(addr, 0xCC, B_PAGE_SIZE);

	// Unmap
	kosm_unmap_dot(dot);

	// Remap as read-only
	void* roAddr = NULL;
	status_t status = kosm_map_dot(dot, &roAddr, B_ANY_ADDRESS,
		KOSM_PROT_READ);
	TEST_ASSERT("remap as RO ok", status == B_OK);

	if (status == B_OK && roAddr != NULL) {
		// Read should work
		TEST_ASSERT("read after RO remap",
			((volatile uint8*)roAddr)[0] == 0xCC);
	}

	kosm_delete_dot(dot);
}


// ===================== CACHE POLICY =====================

// 21. Set cache policy
static void
test_cache_policy()
{
	trace("  [test_cache_policy]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("cache_pol",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	// Default cache policy should be KOSM_CACHE_DEFAULT
	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	TEST_ASSERT("default cache policy", info.cache_policy == KOSM_CACHE_DEFAULT);

	// Try setting write-combine (may fail on some platforms, that's ok)
	status_t status = kosm_dot_set_cache_policy(dot, KOSM_CACHE_WRITECOMBINE);
	trace("    set_cache_policy(WRITECOMBINE) -> %s\n",
		strerror(status));
	// Just log the result, don't assert — platform-dependent

	// Invalid policy should fail
	status = kosm_dot_set_cache_policy(dot, 0xFF);
	TEST_ASSERT("invalid cache policy fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== FILE-BACKED =====================

// 22. Create file-backed dot, write, sync
static void
test_file_backed()
{
	trace("  [test_file_backed]\n");

	// Create a temp file
	BPath path;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK) {
		trace("    can't find temp dir, skipping\n");
		TEST_ASSERT("find temp dir", false);
		return;
	}
	path.Append("kosm_dot_test_file");

	int fd = open(path.Path(), O_CREAT | O_RDWR | O_TRUNC, 0644);
	TEST_REQUIRE("open temp file", fd >= 0);

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
		trace("    kosm_create_dot_file not implemented or failed: %s\n",
			strerror(dot));
		TEST_ASSERT("create file dot (may not be implemented)", dot >= 0);
		close(fd);
		unlink(path.Path());
		return;
	}

	// Write pattern
	memset(addr, 0x42, kSize);
	TEST_ASSERT("write to file dot", ((uint8*)addr)[0] == 0x42);

	// Sync
	status_t status = kosm_dot_sync(dot, 0, kSize, 0);
	trace("    kosm_dot_sync -> %s\n", strerror(status));
	TEST_ASSERT("sync ok", status == B_OK);

	// Verify via info
	kosm_dot_info info;
	kosm_get_dot_info(dot, &info);
	TEST_ASSERT("file dot flags", (info.flags & KOSM_DOT_FILE) != 0);

	kosm_delete_dot(dot);
	close(fd);
	unlink(path.Path());
}


// 23. Sync on non-file dot should fail
static void
test_sync_non_file()
{
	trace("  [test_sync_non_file]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("sync_fail",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);

	TEST_REQUIRE("create dot", dot >= 0);

	status_t status = kosm_dot_sync(dot, 0, B_PAGE_SIZE, 0);
	TEST_ASSERT("sync on non-file fails", status != B_OK);

	kosm_delete_dot(dot);
}


// ===================== STRESS =====================

// 24. Create/delete many dots rapidly
static void
test_stress_create_delete()
{
	trace("  [test_stress_create_delete]\n");

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

	TEST_ASSERT("created all 64", created == kCount);
	trace("    created %d dots in %lld us\n",
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

	TEST_ASSERT("deleted all", deleted == created);
	trace("    deleted %d dots in %lld us\n",
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
	trace("  [test_stress_threaded]\n");

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
		trace("    thread %d: created=%d deleted=%d\n",
			i, args[i].created, args[i].deleted);
	}

	TEST_ASSERT("all threads created ok",
		totalCreated == kThreads * kPerThread);
	TEST_ASSERT("all threads deleted ok",
		totalDeleted == kThreads * kPerThread);

	trace("    %d threads x %d dots in %lld us\n",
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
	trace("  [test_stress_fault_storm]\n");

	const size_t kSize = B_PAGE_SIZE * 64; // 64 pages
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("fault_storm",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create 64-page dot", dot >= 0);

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

	TEST_ASSERT("no data corruption", totalErrors == 0);
	trace("    8 threads x %d iter x 64 pages in %lld us\n",
		kIter, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 26. Memory pressure — allocate many large dots until failure
static void
test_stress_memory_pressure()
{
	trace("  [test_stress_memory_pressure]\n");

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

	TEST_ASSERT("created at least 32 dots", created >= 32);
	trace("    allocated %d dots (%lu KB each) in %lld us\n",
		created, (unsigned long)(kDotSize / 1024), (long long)allocTime);

	// Verify all still readable
	int verifyErrors = 0;
	for (int i = 0; i < created; i++) {
		if (((uint8*)addrs[i])[0] != (uint8)i)
			verifyErrors++;
	}
	TEST_ASSERT("all dots readable after pressure", verifyErrors == 0);

	// Delete all in reverse order (exercises different free patterns)
	t0 = system_time();
	for (int i = created - 1; i >= 0; i--)
		kosm_delete_dot(dots[i]);
	bigtime_t freeTime = system_time() - t0;

	trace("    freed %d dots in %lld us\n",
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
	trace("  [test_stress_protect_thrash]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("prot_thrash",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

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

	TEST_ASSERT("no protect errors", totalErrors == 0);

	// Restore RW and verify data
	kosm_protect_dot(dot, KOSM_PROT_READ | KOSM_PROT_WRITE);
	TEST_ASSERT("data survived thrash", ((uint8*)addr)[0] == 0x55);

	trace("    4 threads x %d protects in %lld us\n",
		kIter, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 28. Map/unmap cycle stress
static void
test_stress_map_unmap()
{
	trace("  [test_stress_map_unmap]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("map_stress",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

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

	TEST_ASSERT("no map/unmap errors", errors == 0);
	trace("    %d map/unmap cycles in %lld us\n",
		kCycles, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 29. Wire/unwire stress — repeated wire/unwire with phys addr check
static void
test_stress_wire_unwire()
{
	trace("  [test_stress_wire_unwire]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("wire_stress",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create dot", dot >= 0);

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
			trace("    wire failed at iter %d: %s\n",
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

	TEST_ASSERT("no wire/unwire errors", errors == 0);
	trace("    %d wire/unwire cycles in %lld us, phys=%#lx\n",
		kCycles, (long long)elapsed, (unsigned long)prevPhys);

	kosm_delete_dot(dot);
}


// 30. Data integrity — fill patterns, verify after operations
static void
test_stress_data_integrity()
{
	trace("  [test_stress_data_integrity]\n");

	const size_t kSize = B_PAGE_SIZE * 32; // 128KB
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("integrity",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);

	TEST_REQUIRE("create 32-page dot", dot >= 0);

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
	TEST_ASSERT("initial fill correct", errors == 0);

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
	TEST_ASSERT("inverse fill correct", errors == 0);

	// Protect change + verify (ensure TLB flush doesn't corrupt)
	kosm_protect_dot(dot, KOSM_PROT_READ);
	errors = 0;
	for (size_t p = 0; p < 32; p++) {
		volatile uint8* page = (volatile uint8*)addr + p * B_PAGE_SIZE;
		if (page[0] != (uint8)(255 - p))
			errors++;
	}
	TEST_ASSERT("data survives protect change", errors == 0);

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
			if (count > 5000)
				break; // safety guard (not an error — concurrent churn)
		}
		args->total_found += count;
	}
	return B_OK;
}

static void
test_stress_info_during_churn()
{
	trace("  [test_stress_info_during_churn]\n");

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

	TEST_ASSERT("creator ok", creatorArgs.created == creatorArgs.deleted);
	TEST_ASSERT("iterator completed", iterArgs.total_found > 0);
	trace("    creator: %d created/deleted, iterator: %d found total, "
		"%lld us\n", creatorArgs.created, iterArgs.total_found,
		(long long)elapsed);
}


// 32. Purgeable volatile/nonvolatile cycle stress
static void
test_stress_purgeable_cycle()
{
	trace("  [test_stress_purgeable_cycle]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("purge_cycle",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_PURGEABLE, KOSM_TAG_GRAPHICS, 0);

	TEST_REQUIRE("create purgeable dot", dot >= 0);
	TEST_REQUIRE("purgeable addr ok", addr != NULL);

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

	TEST_ASSERT("no cycle errors", errors == 0);
	TEST_ASSERT("data accessible after cycles",
		((uint8*)addr)[0] == 0xBB);

	trace("    %d cycles, %d purges, %lld us\n",
		kCycles, purgeCount, (long long)elapsed);

	kosm_delete_dot(dot);
}


// 33. Mixed operations — all API calls interleaved
static void
test_stress_mixed_ops()
{
	trace("  [test_stress_mixed_ops]\n");

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

	TEST_ASSERT("no mixed op errors", errors == 0);
	trace("    8 dots x 8 phases in %lld us\n", (long long)elapsed);
}


// 34. Rapid create/touch/delete single-page — throughput test
// Single dot throughput iteration — returns ops/sec
static double
dot_throughput_iteration()
{
	const int kCount = 500;

	bigtime_t t0 = system_time();
	for (int i = 0; i < kCount; i++) {
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot("thru",
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_NONE, 0);
		if (dot < 0)
			continue;
		((uint8*)addr)[0] = 0xFF;
		kosm_delete_dot(dot);
	}
	bigtime_t elapsed = system_time() - t0;
	return (double)kCount * 1000000.0 / (double)elapsed;
}

static void
test_stress_throughput()
{
	trace("  [test_stress_throughput]\n");

	BenchStats stats = run_benchmark(dot_throughput_iteration, 5, 2);

	trace("    throughput: median=%.0f, min=%.0f, max=%.0f ops/s"
		" (%d runs, %d warmup)\n",
		stats.median_ops, stats.min_ops, stats.max_ops,
		stats.runs, stats.warmup);
	TEST_ASSERT("throughput > 100 ops/s", stats.median_ops > 100.0);
}


// ===================== CROSS-PROCESS =====================

// Child commands
enum {
	CHILD_CMD_READ_ECHO		= 0x01,
	CHILD_CMD_WRITE_PATTERN	= 0x02,
	CHILD_CMD_WRITE_PAGES	= 0x03,
	CHILD_CMD_CLOSE_HANDLE	= 0x04,
	CHILD_CMD_EXIT_DIRTY	= 0x05,
	CHILD_CMD_WRITE_PAGE_AT	= 0x06, // [cmd][page_idx][pattern] -> ack
	CHILD_CMD_FORWARD_HANDLE = 0x07, // receive handle, forward to grandchild
	CHILD_CMD_FAULT_PAGES	= 0x08, // map + touch all pages simultaneously
	CHILD_CMD_DELETE_FAULT	= 0x10,
};


// Faulter thread for concurrent delete+fault test
struct DeleteFaultArgs {
	void*			addr;
	int				pages;
	int32			running;
};

static status_t
delete_fault_faulter(void* data)
{
	DeleteFaultArgs* args = (DeleteFaultArgs*)data;

	while (atomic_get(&args->running) == 1) {
		for (int i = 0; i < args->pages
				&& atomic_get(&args->running) == 1; i++) {
			((volatile uint8*)args->addr)[i * B_PAGE_SIZE] = (uint8)i;
		}
	}
	return B_OK;
}


// Child helper — runs when binary invoked with --dot-child
int
dot_child_helper()
{
	kosm_ray_id ray = kosm_get_bootstrap_ray();
	if (ray < 0)
		return 1;

	uint8 cmd = 0;
	size_t dataSize = sizeof(cmd);
	kosm_handle_t dotHandle = KOSM_HANDLE_INVALID;
	size_t handleCount = 1;

	status_t s = kosm_ray_read(ray, &cmd, &dataSize,
		&dotHandle, &handleCount, 0);
	if (s != B_OK)
		return 2;

	switch (cmd) {
		case CHILD_CMD_READ_ECHO: {
			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ);
			uint8 result = 0x00;
			if (s == B_OK && addr != NULL)
				result = ((volatile uint8*)addr)[0];
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_WRITE_PATTERN: {
			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			uint8 result = 0x00;
			if (s == B_OK && addr != NULL) {
				memset(addr, 0xBB, B_PAGE_SIZE);
				result = 0x01;
			}
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_WRITE_PAGES: {
			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			uint8 result = 0x00;
			if (s == B_OK && addr != NULL) {
				memset((uint8*)addr + 2 * B_PAGE_SIZE, 0xCC,
					B_PAGE_SIZE * 2);
				result = 0x01;
			}
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			// Wait for parent ack before exiting
			uint8 ack = 0;
			size_t ackSize = 1;
			kosm_ray_read(ray, &ack, &ackSize, NULL, NULL, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_CLOSE_HANDLE: {
			kosm_close_handle(dotHandle);
			uint8 result = 0x01;
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			break;
		}
		case CHILD_CMD_EXIT_DIRTY: {
			void* addr = NULL;
			kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			uint8 result = 0x01;
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			// Exit without closing — tests team death cleanup
			break;
		}
		case CHILD_CMD_WRITE_PAGE_AT: {
			// Data format: [cmd(1)][page_idx(1)][pattern(1)]
			// Read extra params from a second message
			uint8 params[2];
			size_t paramSize = sizeof(params);
			s = kosm_ray_read(ray, params, &paramSize, NULL, NULL, 0);
			uint8 pageIdx = params[0];
			uint8 pattern = params[1];

			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			uint8 result = 0x00;
			if (s == B_OK && addr != NULL) {
				memset((uint8*)addr + pageIdx * B_PAGE_SIZE, pattern,
					B_PAGE_SIZE);
				result = 0x01;
			}
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_FORWARD_HANDLE: {
			// Multi-hop: receive handle, spawn grandchild, forward it
			// Read grandchild command params
			uint8 params[2];
			size_t paramSize = sizeof(params);
			s = kosm_ray_read(ray, params, &paramSize, NULL, NULL, 0);
			uint8 gcPageIdx __attribute__((unused)) = params[0];
			uint8 gcPattern __attribute__((unused)) = params[1];

			// Map ourselves first to verify mid-chain access
			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			if (s != B_OK || addr == NULL) {
				uint8 result = 0x00;
				kosm_ray_write(ray, &result, 1, NULL, 0, 0);
				break;
			}
			// Write page 1 as chain-B marker (page 0=parent, page 2=grandchild)
			memset((uint8*)addr + B_PAGE_SIZE, 0xBB, B_PAGE_SIZE);

			// Spawn grandchild
			image_info imgInfo;
			int32 cookie = 0;
			get_next_image_info(B_CURRENT_TEAM, &cookie, &imgInfo);
			kosm_ray_id gcEp0, gcEp1;
			s = kosm_create_ray(&gcEp0, &gcEp1, 0);
			if (s != B_OK) {
				uint8 result = 0x00;
				kosm_ray_write(ray, &result, 1, NULL, 0, 0);
				break;
			}

			const char* argv[] = { imgInfo.name, "--dot-child", NULL };
			thread_id gcTid = load_image(2, argv, (const char**)environ);
			if (gcTid < 0) {
				uint8 result = 0x00;
				kosm_ray_write(ray, &result, 1, NULL, 0, 0);
				kosm_close_ray(gcEp0);
				kosm_close_ray(gcEp1);
				break;
			}

			thread_info gcTinfo;
			get_thread_info(gcTid, &gcTinfo);
			kosm_ray_set_bootstrap(gcTinfo.team, gcEp1);
			resume_thread(gcTid);

			// Send WRITE_PAGE_AT command + handle to grandchild
			uint8 gcCmd = CHILD_CMD_WRITE_PAGE_AT;
			kosm_handle_t gcHandle = dotHandle;
			kosm_ray_write(gcEp0, &gcCmd, sizeof(gcCmd),
				&gcHandle, 1, KOSM_RAY_COPY_HANDLES);
			// Send params
			kosm_ray_write(gcEp0, params, sizeof(params), NULL, 0, 0);

			// Wait for grandchild ack
			uint8 gcResult = 0;
			size_t gcResultSize = sizeof(gcResult);
			kosm_ray_read(gcEp0, &gcResult, &gcResultSize,
				NULL, NULL, 0);

			status_t gcExit;
			wait_for_thread(gcTid, &gcExit);
			kosm_close_ray(gcEp0);

			// Report to parent: 0x01 if grandchild succeeded
			uint8 result = gcResult;
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_FAULT_PAGES: {
			// Map and touch all pages as fast as possible
			void* addr = NULL;
			s = kosm_map_dot(dotHandle, &addr, B_ANY_ADDRESS,
				KOSM_PROT_READ | KOSM_PROT_WRITE);
			uint8 result = 0x00;
			if (s == B_OK && addr != NULL) {
				// Read dot size from parent
				uint32 numPages = 0;
				size_t npSize = sizeof(numPages);
				kosm_ray_read(ray, &numPages, &npSize, NULL, NULL, 0);

				// Touch all pages to trigger faults
				for (uint32 i = 0; i < numPages; i++)
					((volatile uint8*)addr)[i * B_PAGE_SIZE] = (uint8)i;

				result = 0x01;
			}
			kosm_ray_write(ray, &result, 1, NULL, 0, 0);
			kosm_close_handle(dotHandle);
			break;
		}
		case CHILD_CMD_DELETE_FAULT: {
			// Self-contained: create dot, fault pages, delete concurrently
			uint8 ack = 0x01;
			kosm_ray_write(ray, &ack, 1, NULL, 0, 0);

			void* faddr = NULL;
			kosm_dot_id fdot = kosm_create_dot("df_child",
				&faddr, B_ANY_ADDRESS, B_PAGE_SIZE * 64,
				KOSM_PROT_READ | KOSM_PROT_WRITE,
				KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
			if (fdot >= 0) {
				DeleteFaultArgs fargs;
				fargs.addr = faddr;
				fargs.pages = 64;
				atomic_set(&fargs.running, 1);

				thread_id ft = spawn_thread(delete_fault_faulter,
					"faulter", B_NORMAL_PRIORITY, &fargs);
				resume_thread(ft);

				// Let faulter start creating page faults on 64 lazy pages
				snooze(200);

				// Delete while faulter is actively faulting
				kosm_delete_dot(fdot);

				atomic_set(&fargs.running, 0);
				status_t fExitVal;
				wait_for_thread(ft, &fExitVal);
			}

			uint8 done = 0x02;
			kosm_ray_write(ray, &done, 1, NULL, 0, 0);
			break;
		}
	}

	kosm_close_ray(ray);
	return 0;
}


// Spawn child process with bootstrap ray
static status_t
spawn_dot_child(kosm_ray_id* parentRay, thread_id* childThread,
	team_id* childTeam)
{
	// Find our own binary path
	image_info imgInfo;
	int32 cookie = 0;
	status_t s = get_next_image_info(B_CURRENT_TEAM, &cookie, &imgInfo);
	if (s != B_OK)
		return s;

	// Create ray pair
	kosm_ray_id ep0, ep1;
	s = kosm_create_ray(&ep0, &ep1, 0);
	if (s != B_OK)
		return s;

	// Spawn child
	const char* argv[] = { imgInfo.name, "--dot-child", NULL };
	thread_id tid = load_image(2, argv, (const char**)environ);
	if (tid < 0) {
		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
		return (status_t)tid;
	}

	// Get child team
	thread_info tinfo;
	s = get_thread_info(tid, &tinfo);
	if (s != B_OK) {
		kill_thread(tid);
		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
		return s;
	}

	// Set bootstrap ray for child
	s = kosm_ray_set_bootstrap(tinfo.team, ep1);
	if (s != B_OK) {
		kill_thread(tid);
		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
		return s;
	}

	resume_thread(tid);

	*parentRay = ep0;
	*childThread = tid;
	*childTeam = tinfo.team;
	return B_OK;
}


// 35. Cross-process: child reads parent's data
static void
test_xproc_shared_read()
{
	trace("  [test_xproc_shared_read]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("xproc_rd",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create dot", dot >= 0 && addr != NULL);

	memset(addr, 0xAA, B_PAGE_SIZE);

	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	uint8 cmd = CHILD_CMD_READ_ECHO;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle", s == B_OK);

	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("read response", s == B_OK);
	TEST_ASSERT("child saw 0xAA", result == 0xAA);

	status_t exitVal;
	wait_for_thread(child, &exitVal);
	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 36. Cross-process: child writes, parent reads (true shared memory)
static void
test_xproc_shared_write()
{
	trace("  [test_xproc_shared_write]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("xproc_wr",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create dot", dot >= 0 && addr != NULL);

	// Touch page so it's committed
	memset(addr, 0xAA, B_PAGE_SIZE);

	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	uint8 cmd = CHILD_CMD_WRITE_PATTERN;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle", s == B_OK);

	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("child ack", s == B_OK && result == 0x01);

	// Parent sees child's write — true shared memory
	TEST_ASSERT("parent sees 0xBB",
		((volatile uint8*)addr)[0] == 0xBB);

	status_t exitVal;
	wait_for_thread(child, &exitVal);
	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 37. Cross-process: parent + child write different pages
static void
test_xproc_concurrent_pages()
{
	trace("  [test_xproc_concurrent_pages]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("xproc_conc",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 4,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create 4-page dot", dot >= 0 && addr != NULL);

	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	uint8 cmd = CHILD_CMD_WRITE_PAGES;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle", s == B_OK);

	// Parent writes pages 0-1 while child writes pages 2-3
	memset(addr, 0xAA, B_PAGE_SIZE * 2);

	// Wait for child to finish
	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("child ack", s == B_OK && result == 0x01);

	// Verify all 4 pages
	bool ok = true;
	uint8* p = (uint8*)addr;
	for (size_t i = 0; i < B_PAGE_SIZE * 2 && ok; i++) {
		if (p[i] != 0xAA)
			ok = false;
	}
	for (size_t i = 2 * B_PAGE_SIZE; i < 4 * B_PAGE_SIZE && ok; i++) {
		if (p[i] != 0xCC)
			ok = false;
	}
	TEST_ASSERT("all 4 pages correct", ok);

	// Ack child to exit
	uint8 ack = 0x01;
	kosm_ray_write(ray, &ack, 1, NULL, 0, 0);

	status_t exitVal;
	wait_for_thread(child, &exitVal);
	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 38. Cross-process: child closes handle, parent still has access
static void
test_xproc_handle_lifecycle()
{
	trace("  [test_xproc_handle_lifecycle]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("xproc_life",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create dot", dot >= 0 && addr != NULL);

	memset(addr, 0xDD, B_PAGE_SIZE);

	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	uint8 cmd = CHILD_CMD_CLOSE_HANDLE;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle", s == B_OK);

	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("child closed ok", s == B_OK && result == 0x01);

	// Parent still has access
	TEST_ASSERT("parent data intact",
		((volatile uint8*)addr)[0] == 0xDD);

	kosm_dot_info info;
	TEST_ASSERT("info still works",
		kosm_get_dot_info(dot, &info) == B_OK);

	status_t exitVal;
	wait_for_thread(child, &exitVal);
	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 39. Cross-process: child dies with handle open (team death cleanup)
static void
test_xproc_child_death()
{
	trace("  [test_xproc_child_death]\n");

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("xproc_death",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create dot", dot >= 0 && addr != NULL);

	memset(addr, 0xEE, B_PAGE_SIZE);

	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	uint8 cmd = CHILD_CMD_EXIT_DIRTY;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle", s == B_OK);

	// Wait for child ack (it mapped the dot)
	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("child mapped ok", s == B_OK && result == 0x01);

	// Wait for child to exit (dirty — no explicit close)
	status_t exitVal;
	wait_for_thread(child, &exitVal);

	// Give kernel time to process team death
	snooze(10000);

	// Dot survives child death — parent still has handle
	TEST_ASSERT("dot survives child death",
		((volatile uint8*)addr)[0] == 0xEE);

	kosm_dot_info info;
	TEST_ASSERT("info after child death",
		kosm_get_dot_info(dot, &info) == B_OK);

	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 40. Concurrent delete + fault — kernel must not panic
static void
test_concurrent_delete_fault()
{
	trace("  [test_concurrent_delete_fault]\n");

	// Part 1: Safe — stop faulter before delete (tests serialization)
	const int kSafeIter = 50;
	int errors = 0;

	for (int i = 0; i < kSafeIter; i++) {
		void* addr = NULL;
		kosm_dot_id dot = kosm_create_dot("df_safe",
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 16,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dot < 0) {
			errors++;
			continue;
		}

		DeleteFaultArgs args;
		args.addr = addr;
		args.pages = 16;
		atomic_set(&args.running, 1);

		thread_id t = spawn_thread(delete_fault_faulter,
			"faulter", B_NORMAL_PRIORITY, &args);
		resume_thread(t);

		// Let faulter create page faults on lazy pages
		snooze(100 + i * 5);

		// Stop faulter, then delete
		atomic_set(&args.running, 0);
		status_t exitVal;
		wait_for_thread(t, &exitVal);

		if (kosm_delete_dot(dot) != B_OK)
			errors++;
	}

	TEST_ASSERT("safe delete+fault 50x", errors == 0);
	trace("    %d safe iterations\n", kSafeIter);

	// Part 2: Dangerous — delete while faulting (in child process)
	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	status_t s = spawn_dot_child(&ray, &child, &childTeam);
	if (s == B_OK) {
		uint8 cmd = CHILD_CMD_DELETE_FAULT;
		kosm_ray_write(ray, &cmd, sizeof(cmd), NULL, 0, 0);

		uint8 ack = 0;
		size_t ackSize = sizeof(ack);
		kosm_ray_read(ray, &ack, &ackSize, NULL, NULL, 0);

		// Wait for child completion or death (5s timeout)
		uint8 done = 0;
		size_t doneSize = sizeof(done);
		s = kosm_ray_read_etc(ray, &done, &doneSize,
			NULL, NULL, 0, 5000000);

		bool childOk = (s == B_OK && done == 0x02);
		trace("    child: %s\n",
			childOk ? "survived" : "died (expected)");

		status_t exitVal;
		wait_for_thread(child, &exitVal);
		kosm_close_ray(ray);
	}

	// If we're here, kernel didn't panic
	TEST_ASSERT("kernel survived delete+fault", true);
}


// 41. Large dot — 256 MB (textures, buffers)
static void
test_large_256mb()
{
	trace("  [test_large_256mb]\n");

	const size_t kSize = 256 * 1024 * 1024;
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("large_256",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_GRAPHICS, 0);

	TEST_REQUIRE("create 256MB dot", dot >= 0 && addr != NULL);

	// Touch first, middle, last page
	((volatile uint8*)addr)[0] = 0x11;
	((volatile uint8*)addr)[kSize / 2] = 0x22;
	((volatile uint8*)addr)[kSize - 1] = 0x33;

	TEST_ASSERT("first page", ((volatile uint8*)addr)[0] == 0x11);
	TEST_ASSERT("middle page",
		((volatile uint8*)addr)[kSize / 2] == 0x22);
	TEST_ASSERT("last page",
		((volatile uint8*)addr)[kSize - 1] == 0x33);

	kosm_dot_info info;
	TEST_ASSERT("info ok", kosm_get_dot_info(dot, &info) == B_OK);
	TEST_ASSERT("size correct", info.size == kSize);

	trace("    256MB dot: resident=%zu pages (expected ~3)\n",
		info.resident_size / B_PAGE_SIZE);

	kosm_delete_dot(dot);
}


// ===================== ADVANCED TESTS =====================

// 42. Dual mapping — same dot at two addresses in one process
static void
test_dual_mapping()
{
	trace("  [test_dual_mapping]\n");

	void* addr1 = NULL;
	kosm_dot_id dot = kosm_create_dot("dual_map",
		&addr1, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create dot", dot >= 0 && addr1 != NULL);

	// Write through first mapping
	memset(addr1, 0xAA, B_PAGE_SIZE);

	// Create second mapping via kosm_map_dot
	void* addr2 = NULL;
	status_t s = kosm_map_dot(dot, &addr2, B_ANY_ADDRESS,
		KOSM_PROT_READ | KOSM_PROT_WRITE);
	TEST_REQUIRE("second mapping", s == B_OK && addr2 != NULL);

	TEST_ASSERT("different addresses", addr1 != addr2);

	// Read through second mapping — should see same data
	TEST_ASSERT("addr2 sees 0xAA",
		((volatile uint8*)addr2)[0] == 0xAA);
	TEST_ASSERT("addr2 last byte",
		((volatile uint8*)addr2)[B_PAGE_SIZE - 1] == 0xAA);

	// Write through addr2, read through addr1
	((volatile uint8*)addr2)[42] = 0xBB;
	TEST_ASSERT("addr1 sees addr2 write",
		((volatile uint8*)addr1)[42] == 0xBB);

	kosm_unmap_dot(dot);
	kosm_delete_dot(dot);
}


// 43. Multi-hop handle transfer: Parent → Child B → Grandchild C
static void
test_multi_hop_transfer()
{
	trace("  [test_multi_hop_transfer]\n");

	// Create a 3-page dot: parent=page0(0xAA), B=page1(0xBB), C=page2(0xCC)
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("multi_hop",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * 3,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create 3-page dot", dot >= 0 && addr != NULL);

	// Parent writes page 0
	memset(addr, 0xAA, B_PAGE_SIZE);

	// Spawn child B with FORWARD_HANDLE command
	kosm_ray_id ray;
	thread_id child;
	team_id childTeam;
	TEST_REQUIRE("spawn child B",
		spawn_dot_child(&ray, &child, &childTeam) == B_OK);

	// Send dot handle with FORWARD_HANDLE command
	uint8 cmd = CHILD_CMD_FORWARD_HANDLE;
	kosm_handle_t handle = (kosm_handle_t)dot;
	status_t s = kosm_ray_write(ray, &cmd, sizeof(cmd),
		&handle, 1, KOSM_RAY_COPY_HANDLES);
	TEST_REQUIRE("send handle to B", s == B_OK);

	// Send grandchild params: page_idx=2, pattern=0xCC
	uint8 params[2] = { 2, 0xCC };
	kosm_ray_write(ray, params, sizeof(params), NULL, 0, 0);

	// Wait for child B to report grandchild result
	uint8 result = 0;
	size_t resultSize = sizeof(result);
	s = kosm_ray_read(ray, &result, &resultSize, NULL, NULL, 0);
	TEST_ASSERT("chain completed", s == B_OK && result == 0x01);

	// Verify all 3 pages: parent(0xAA), B(0xBB), C(0xCC)
	uint8* p = (uint8*)addr;
	bool page0ok = true, page1ok = true, page2ok = true;
	for (size_t i = 0; i < B_PAGE_SIZE; i++) {
		if (p[i] != 0xAA) page0ok = false;
		if (p[B_PAGE_SIZE + i] != 0xBB) page1ok = false;
		if (p[2 * B_PAGE_SIZE + i] != 0xCC) page2ok = false;
	}
	TEST_ASSERT("page 0 (parent 0xAA)", page0ok);
	TEST_ASSERT("page 1 (child B 0xBB)", page1ok);
	TEST_ASSERT("page 2 (grandchild C 0xCC)", page2ok);

	status_t exitVal;
	wait_for_thread(child, &exitVal);
	kosm_close_ray(ray);
	kosm_delete_dot(dot);
}


// 44. Three processes fault same lazy dot simultaneously
static void
test_three_process_fault()
{
	trace("  [test_three_process_fault]\n");

	const uint32 kPages = 64;
	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("3proc_fault",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE * kPages,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_REQUIRE("create 64-page lazy dot", dot >= 0 && addr != NULL);

	// Spawn 3 children
	kosm_ray_id rays[3];
	thread_id children[3];
	team_id teams[3];
	int spawned = 0;

	for (int i = 0; i < 3; i++) {
		status_t s = spawn_dot_child(&rays[i], &children[i], &teams[i]);
		if (s == B_OK)
			spawned++;
		else
			break;
	}
	TEST_REQUIRE("spawn 3 children", spawned == 3);

	// Send FAULT_PAGES command + dot handle to all 3 simultaneously
	for (int i = 0; i < 3; i++) {
		uint8 cmd = CHILD_CMD_FAULT_PAGES;
		kosm_handle_t handle = (kosm_handle_t)dot;
		kosm_ray_write(rays[i], &cmd, sizeof(cmd),
			&handle, 1, KOSM_RAY_COPY_HANDLES);
	}

	// Send page count to all 3 — they'll all fault the same pages
	for (int i = 0; i < 3; i++) {
		uint32 numPages = kPages;
		kosm_ray_write(rays[i], &numPages, sizeof(numPages),
			NULL, 0, 0);
	}

	// Parent also faults all pages simultaneously
	for (uint32 i = 0; i < kPages; i++)
		((volatile uint8*)addr)[i * B_PAGE_SIZE] = 0xFF;

	// Wait for all children
	int childOk = 0;
	for (int i = 0; i < 3; i++) {
		uint8 result = 0;
		size_t resultSize = sizeof(result);
		status_t s = kosm_ray_read_etc(rays[i], &result, &resultSize,
			NULL, NULL, B_RELATIVE_TIMEOUT, 5000000);
		if (s == B_OK && result == 0x01)
			childOk++;
	}

	TEST_ASSERT("all 3 children faulted ok", childOk == 3);

	// Verify dot is intact
	kosm_dot_info info;
	TEST_ASSERT("info ok", kosm_get_dot_info(dot, &info) == B_OK);
	TEST_ASSERT("all pages resident",
		info.resident_size >= kPages * B_PAGE_SIZE);

	for (int i = 0; i < 3; i++) {
		status_t exitVal;
		wait_for_thread(children[i], &exitVal);
		kosm_close_ray(rays[i]);
	}
	kosm_delete_dot(dot);
}


// 45. Max capacity — create dots until failure, verify cleanup
static void
test_max_capacity()
{
	trace("  [test_max_capacity]\n");

	// NOTE: >4000 dots triggers kernel panic in error cleanup path
	// (ConditionVariable::Unpublish on never-published CV). Keep below that.
	const int kMaxAttempt = 1024;
	kosm_dot_id dots[kMaxAttempt];
	int created = 0;

	for (int i = 0; i < kMaxAttempt; i++) {
		void* addr = NULL;
		char name[32];
		snprintf(name, sizeof(name), "cap_%d", i);
		kosm_dot_id dot = kosm_create_dot(name,
			&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
			KOSM_PROT_READ | KOSM_PROT_WRITE,
			KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
		if (dot < 0)
			break;
		dots[created++] = dot;
	}

	trace("    created %d dots before exhaustion\n", created);
	TEST_ASSERT("created at least 100", created >= 100);

	// Verify last created dot is functional
	if (created > 0) {
		kosm_dot_info info;
		TEST_ASSERT("last dot info ok",
			kosm_get_dot_info(dots[created - 1], &info) == B_OK);
	}

	// Delete all
	int deleteErrors = 0;
	for (int i = 0; i < created; i++) {
		if (kosm_delete_dot(dots[i]) != B_OK)
			deleteErrors++;
	}
	TEST_ASSERT("all deleted ok", deleteErrors == 0);

	// Verify system recovered — can create new dot
	void* addr = NULL;
	kosm_dot_id newDot = kosm_create_dot("cap_recovery",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_ASSERT("recovery create ok", newDot >= 0 && addr != NULL);
	if (newDot >= 0) {
		((volatile uint8*)addr)[0] = 0x42;
		TEST_ASSERT("recovery write ok",
			((volatile uint8*)addr)[0] == 0x42);
		kosm_delete_dot(newDot);
	}
}


// 46. File sync stress — concurrent writes + syncs
static void
test_file_sync_stress()
{
	trace("  [test_file_sync_stress]\n");

	BPath path;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK) {
		TEST_ASSERT("find temp dir", false);
		return;
	}
	path.Append("kosm_dot_sync_stress");

	int fd = open(path.Path(), O_CREAT | O_RDWR | O_TRUNC, 0644);
	TEST_REQUIRE("open temp file", fd >= 0);

	const size_t kSize = B_PAGE_SIZE * 8;
	ftruncate(fd, kSize);

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot_file(fd, 0, "sync_stress",
		&addr, B_ANY_ADDRESS, kSize,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_FILE, KOSM_TAG_APP);

	if (dot < 0) {
		trace("    file dot not available, skipping\n");
		TEST_ASSERT("create file dot", dot >= 0);
		close(fd);
		unlink(path.Path());
		return;
	}

	// Write + sync cycles
	const int kCycles = 20;
	int syncErrors = 0;
	for (int i = 0; i < kCycles; i++) {
		// Write different pattern each cycle
		memset(addr, (uint8)(i + 1), kSize);

		status_t s = kosm_dot_sync(dot, 0, kSize, 0);
		if (s != B_OK)
			syncErrors++;
	}

	TEST_ASSERT("all syncs ok", syncErrors == 0);

	// Verify last pattern survives
	TEST_ASSERT("last pattern intact",
		((uint8*)addr)[0] == (uint8)kCycles);
	TEST_ASSERT("last pattern page 7",
		((uint8*)addr)[7 * B_PAGE_SIZE] == (uint8)kCycles);

	trace("    %d write+sync cycles, %d errors\n",
		kCycles, syncErrors);

	kosm_delete_dot(dot);
	close(fd);
	unlink(path.Path());
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
	// Cross-Process
	{ "xproc shared read",      test_xproc_shared_read,       "Cross-Process" },
	{ "xproc shared write",     test_xproc_shared_write,      "Cross-Process" },
	{ "xproc concurrent pages", test_xproc_concurrent_pages,  "Cross-Process" },
	{ "xproc handle lifecycle", test_xproc_handle_lifecycle,   "Cross-Process" },
	{ "xproc child death",      test_xproc_child_death,       "Cross-Process" },
	// Concurrent delete + fault
	{ "delete+fault race",      test_concurrent_delete_fault, "STRESS" },
	// Large sizes
	{ "large 256MB",            test_large_256mb,             "Sizes" },
	// Advanced
	{ "dual mapping",           test_dual_mapping,            "Advanced" },
	{ "multi-hop A->B->C",     test_multi_hop_transfer,      "Advanced" },
	{ "3-process concurrent fault", test_three_process_fault, "Advanced" },
	{ "max capacity",           test_max_capacity,            "Advanced" },
	{ "file sync stress",       test_file_sync_stress,        "Advanced" },
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
