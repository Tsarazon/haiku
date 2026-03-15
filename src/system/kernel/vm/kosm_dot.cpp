/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * kosm_dot — kernel implementation.
 *
 * Independent VM primitive parallel to Haiku area.
 * See kosm_dot.h for architecture overview.
 */

#include <vm/kosm_dot.h>

#include <stdlib.h>
#include <string.h>
#include <new>

#include <AutoDeleter.h>
#include <KernelExport.h>

#include <arch/vm.h>
#include <heap.h>
#include <kernel.h>
#include <lock.h>
#include <low_resource_manager.h>
#include <slab/Slab.h>
#include <smp.h>
#include <team.h>
#include <thread.h>
#include <tracing.h>
#include <util/AutoLock.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMCache.h>
#include <vm/VMTranslationMap.h>

#include <vfs.h>
#include <util/iovec_support.h>

#include "VMAnonymousNoSwapCache.h"
#include "KosmDotPurgeableCache.h"
#include "../cache/vnode_store.h"

// B_PHYSICAL_IO_REQUEST is defined in device_manager/IORequest.h
// but pulling that header brings unwanted dependencies.
#ifndef B_PHYSICAL_IO_REQUEST
#	define B_PHYSICAL_IO_REQUEST	0x01
#endif


//#define TRACE_KOSM_DOT
#ifdef TRACE_KOSM_DOT
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) do {} while (false)
#endif


static const int32 kDotHashSize = 1024;
static const int32 kPerAspaceHashSize = 64;


// ID allocator
static int32 sNextDotID = 1;

// Global ID → KosmDot hash table
static KosmDotHashTable sDotTable;
static rw_lock sDotTableLock = RW_LOCK_INITIALIZER("kosm_dot table");

// Per-address-space data: team_id → tree of kosm_dots by address
struct PerAspaceData {
	team_id				team;
	rw_lock				lock;
	KosmDotAddrTree		tree;
	PerAspaceData*		hash_link;

	PerAspaceData(team_id _team)
		: team(_team)
	{
		rw_lock_init(&lock, "kosm_dot aspace");
	}

	~PerAspaceData()
	{
		rw_lock_destroy(&lock);
	}
};

struct PerAspaceHashDefinition {
	typedef team_id			KeyType;
	typedef PerAspaceData	ValueType;

	size_t HashKey(team_id key) const		{ return (size_t)key; }
	size_t Hash(const PerAspaceData* v) const { return HashKey(v->team); }
	bool Compare(team_id key, const PerAspaceData* v) const
		{ return v->team == key; }
	PerAspaceData*& GetLink(PerAspaceData* v) const
		{ return v->hash_link; }
};

typedef BOpenHashTable<PerAspaceHashDefinition> PerAspaceTable;

static PerAspaceTable sPerAspaceTable;
static rw_lock sPerAspaceTableLock = RW_LOCK_INITIALIZER("kosm_dot aspaces");

// Slab caches
static ObjectCache* sDotCache;
static ObjectCache* sMappingCache;
static ObjectCache* sTreeNodeCache;
static ObjectCache* sWiredRangeCache;
static ObjectCache* sPerAspaceCache;

// Memory tag accounting
struct TagCounter {
	int32		dot_count;
	int64		total_size;
	int64		resident_size;
};

static TagCounter sTagCounters[KOSM_TAG_USER_BASE];
static mutex sTagLock = MUTEX_INITIALIZER("kosm_dot tags");

// Page reclaim scanner for reclaimable dots
static const int32 kPageUsageMax = 64;
static const int32 kPageUsageAdvance = 3;
static const int32 kPageUsageDecline = 1;
static const bigtime_t kScannerIdleInterval = 500000;	// 500ms

static thread_id sDotScannerThread = -1;
static ConditionVariable sDotScannerCondition;
static int32 sDotScannerActive = 0;
static bool sDotScannerShouldExit = false;


// Per-aspace helpers

static PerAspaceData*
get_or_create_per_aspace(team_id team)
{
	rw_lock_read_lock(&sPerAspaceTableLock);
	PerAspaceData* data = sPerAspaceTable.Lookup(team);
	rw_lock_read_unlock(&sPerAspaceTableLock);

	if (data != NULL)
		return data;

	PerAspaceData* newData = new(sPerAspaceCache, 0) PerAspaceData(team);
	if (newData == NULL)
		return NULL;

	rw_lock_write_lock(&sPerAspaceTableLock);

	data = sPerAspaceTable.Lookup(team);
	if (data != NULL) {
		rw_lock_write_unlock(&sPerAspaceTableLock);
		newData->~PerAspaceData();
		object_cache_delete(sPerAspaceCache, newData);
		return data;
	}

	sPerAspaceTable.InsertUnchecked(newData);
	rw_lock_write_unlock(&sPerAspaceTableLock);
	return newData;
}


static PerAspaceData*
lookup_per_aspace(team_id team)
{
	rw_lock_read_lock(&sPerAspaceTableLock);
	PerAspaceData* data = sPerAspaceTable.Lookup(team);
	rw_lock_read_unlock(&sPerAspaceTableLock);
	return data;
}


static void
remove_per_aspace(team_id team)
{
	rw_lock_write_lock(&sPerAspaceTableLock);
	PerAspaceData* data = sPerAspaceTable.Lookup(team);
	if (data != NULL)
		sPerAspaceTable.RemoveUnchecked(data);
	rw_lock_write_unlock(&sPerAspaceTableLock);

	if (data != NULL) {
		data->~PerAspaceData();
		object_cache_delete(sPerAspaceCache, data);
	}
}


// Tag accounting helpers

static inline void
tag_add(uint32 tag, size_t size)
{
	if (tag < KOSM_TAG_USER_BASE) {
		MutexLocker locker(sTagLock);
		sTagCounters[tag].dot_count++;
		sTagCounters[tag].total_size += size;
	}
}


static inline void
tag_remove(uint32 tag, size_t size)
{
	if (tag < KOSM_TAG_USER_BASE) {
		MutexLocker locker(sTagLock);
		sTagCounters[tag].dot_count--;
		sTagCounters[tag].total_size -= size;
	}
}


static inline void
tag_add_resident(uint32 tag, size_t pages)
{
	if (tag < KOSM_TAG_USER_BASE)
		atomic_add64(&sTagCounters[tag].resident_size, pages * B_PAGE_SIZE);
}


static inline void
tag_remove_resident(uint32 tag, size_t pages)
{
	if (tag < KOSM_TAG_USER_BASE)
		atomic_add64(&sTagCounters[tag].resident_size, -(int64)(pages * B_PAGE_SIZE));
}


// KosmDot methods

KosmDotMapping*
KosmDot::FindMapping(team_id team) const
{
	for (KosmDotMappingList::ConstIterator it = mappings.GetIterator();
			KosmDotMapping* m = it.Next();) {
		if (m->team == team)
			return m;
	}
	return NULL;
}


uint32
KosmDot::MemoryType() const
{
	return (uint32)memory_type << MEMORY_TYPE_SHIFT;
}


void
KosmDot::SetMemoryType(uint32 type)
{
	memory_type = type >> MEMORY_TYPE_SHIFT;
}


/*static*/ uint32
KosmDot::ToHaikuProtection(uint32 kosmProt)
{
	uint32 haikuProt = 0;
	if (kosmProt & KOSM_PROT_READ) {
		haikuProt |= B_READ_AREA | B_KERNEL_READ_AREA;
	}
	if (kosmProt & KOSM_PROT_WRITE) {
		haikuProt |= B_WRITE_AREA | B_KERNEL_WRITE_AREA;
	}
	if (kosmProt & KOSM_PROT_EXEC) {
		haikuProt |= B_EXECUTE_AREA | B_KERNEL_EXECUTE_AREA;
	}
	return haikuProt;
}


// Wiring mode derived from flags
static uint32
derive_wiring(uint32 flags)
{
	if (flags & KOSM_DOT_CONTIGUOUS)
		return B_CONTIGUOUS;
	if (flags & KOSM_DOT_WIRED)
		return B_FULL_LOCK;
	return B_NO_LOCK;
}


// Insert a tree node into a per-aspace tree
static status_t
insert_tree_node(PerAspaceData* aspaceData, KosmDot* dot,
	team_id team, addr_t base, size_t size)
{
	KosmDotTreeNode* node = new(sTreeNodeCache, 0) KosmDotTreeNode;
	if (node == NULL)
		return B_NO_MEMORY;

	node->base = base;
	node->size = size;
	node->kosm_dot = dot;
	node->team = team;

	rw_lock_write_lock(&aspaceData->lock);
	aspaceData->tree.Insert(node);
	rw_lock_write_unlock(&aspaceData->lock);
	return B_OK;
}


// Remove a tree node from a per-aspace tree
static void
remove_tree_node(PerAspaceData* aspaceData, addr_t address)
{
	rw_lock_write_lock(&aspaceData->lock);
	KosmDotTreeNode* node = aspaceData->tree.Find(address);
	if (node != NULL)
		aspaceData->tree.Remove(node);
	rw_lock_write_unlock(&aspaceData->lock);

	if (node != NULL)
		object_cache_delete(sTreeNodeCache, node);
}


// Unmap all pages of a kosm_dot from a specific mapping
static void
unmap_dot_pages(KosmDot* dot, KosmDotMapping* mapping,
	VMAddressSpace* aspace)
{
	if (dot->IsDevice()) {
		VMTranslationMap* map = aspace->TranslationMap();
		map->Lock();
		map->Unmap(mapping->base, mapping->base + mapping->size - 1);
		map->Unlock();
		return;
	}

	VMCache* cache = dot->cache;
	if (cache == NULL)
		return;

	VMTranslationMap* map = aspace->TranslationMap();

	cache->Lock();
	map->Lock();

	for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
			vm_page* page = it.Next();) {
		addr_t pageAddr = mapping->base
			+ ((off_t)page->cache_offset << PAGE_SHIFT);
		if (pageAddr >= mapping->base
			&& pageAddr < mapping->base + mapping->size) {
			map->Unmap(pageAddr, pageAddr + B_PAGE_SIZE - 1);
			page->DecrementWiredCount();
		}
	}

	map->Unlock();
	cache->Unlock();
}


// Allocate and zero-initialize a new kosm_dot from slab.
// Caller must set all fields before use (especially mutex_init,
// condition.Init, list heads).
static KosmDot*
allocate_dot()
{
	void* mem = object_cache_alloc(sDotCache, 0);
	if (mem == NULL)
		return NULL;

	memset(mem, 0, sizeof(KosmDot));

	// Placement-construct lists so their invariants hold even
	// if the caller bails before full initialization.
	KosmDot* dot = (KosmDot*)mem;
	new(&dot->mappings) KosmDotMappingList;
	new(&dot->wired_ranges) KosmDotWiredRangeList;
	return dot;
}


// Free a kosm_dot object (called only when last ref is released)
static void
free_dot(KosmDot* dot)
{
	mutex_destroy(&dot->lock);
	dot->condition.Unpublish();
	object_cache_delete(sDotCache, dot);
}


// Release a reference. If this was the last ref, free the struct.
// This is the standard "put" pattern -- use after lookup_dot().
static void
put_dot(KosmDot* dot)
{
	if (dot->ReleaseRef())
		free_dot(dot);
}


// Look up and acquire a reference
static KosmDot*
lookup_dot(kosm_dot_id id)
{
	rw_lock_read_lock(&sDotTableLock);
	KosmDot* dot = sDotTable.Lookup(id);
	if (dot != NULL && dot->IsActive())
		dot->AcquireRef();
	else
		dot = NULL;
	rw_lock_read_unlock(&sDotTableLock);
	return dot;
}


// Delete: atomically mark deleted, remove from lookups, cleanup resources.
// The dot struct is freed when the last reference is released (via put_dot).
// Caller must hold a reference (from lookup_dot or creation).
static status_t
delete_dot(KosmDot* dot)
{
	TRACE("delete_dot: id %d\n", (int)dot->id);

	// Atomic state transition: only one caller can succeed.
	// Provides release semantics so all prior writes are visible.
	int32 oldState = atomic_test_and_set(&dot->state,
		DOT_STATE_DELETED, DOT_STATE_ACTIVE);
	if (oldState != DOT_STATE_ACTIVE)
		return B_BAD_VALUE;

	// Remove from global hash first — prevents new lookup_dot() calls
	// from finding this dot. Must happen before cleanup so no new
	// refs can be acquired.
	rw_lock_write_lock(&sDotTableLock);
	sDotTable.RemoveUnchecked(dot);
	rw_lock_write_unlock(&sDotTableLock);

	// Remove all tree nodes — prevents new faults from finding this dot.
	// Done before unmapping so kosm_dot_fault() can't race with cleanup.
	for (KosmDotMappingList::Iterator it = dot->mappings.GetIterator();
			KosmDotMapping* mapping = it.Next();) {
		PerAspaceData* aspaceData = lookup_per_aspace(mapping->team);
		if (aspaceData != NULL)
			remove_tree_node(aspaceData, mapping->base);
	}

	// At this point no new refs or faults are possible. In-progress faults
	// hold a ref and will serialize on the cache lock during cleanup below.

	// Unmap all mappings
	while (KosmDotMapping* mapping = dot->mappings.RemoveHead()) {
		VMAddressSpace* aspace = VMAddressSpace::Get(mapping->team);
		if (aspace != NULL) {
			unmap_dot_pages(dot, mapping, aspace);

			// Unreserve address range
			aspace->WriteLock();
			aspace->UnreserveAddressRange(mapping->base, mapping->size, 0);
			aspace->WriteUnlock();
			aspace->Put();
		} else {
			// Team's aspace is already gone (team death race).
			// PTEs are already destroyed, but wired counts on pages
			// still reflect this mapping. Decrement them manually.
			if (dot->cache != NULL) {
				VMCache* cache = dot->cache;
				cache->Lock();
				for (VMCachePagesTree::Iterator it
						= cache->pages.GetIterator();
						vm_page* page = it.Next();) {
					if (page->WiredCount() > 0)
						page->DecrementWiredCount();
				}
				cache->Unlock();
			}
		}
		object_cache_delete(sMappingCache, mapping);
	}

	// Free wired ranges
	while (KosmDotWiredRange* range = dot->wired_ranges.RemoveHead()) {
		object_cache_delete(sWiredRangeCache, range);
	}

	// Release cache. The cache lock serializes with any in-progress fault
	// that's between cache->Lock() and cache->Unlock().
	if (dot->cache != NULL) {
		VMCache* cache = dot->cache;
		cache->Lock();

		// Count pages for tag accounting, and force any remaining
		// wired counts to 0 (e.g. from DMA wiring or missed unmap).
		size_t pagesInCache = cache->page_count;
		for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
				vm_page* page = it.Next();) {
			while (page->WiredCount() > 0)
				page->DecrementWiredCount();
		}

		cache->ReleaseRefAndUnlock();
		dot->cache = NULL;

		tag_remove_resident(dot->tag, pagesInCache);
	}

	// Tag accounting
	tag_remove(dot->tag, dot->size);

	// Notify waiters
	dot->condition.NotifyAll();

	// Release the creation ref. If no other refs outstanding, frees
	// the dot struct. If concurrent users still hold refs (e.g.
	// in-progress fault return path), struct stays alive until their
	// put_dot() call.
	put_dot(dot);
	return B_OK;
}


// Handle table integration

// Unmap a specific team's mapping without destroying the dot.
// Called when a team closes its handle (but other handles may remain).
static void
close_dot_mapping_for_team(KosmDot* dot, team_id team)
{
	MutexLocker locker(dot->lock);
	KosmDotMapping* mapping = dot->FindMapping(team);
	if (mapping == NULL)
		return;

	dot->mappings.Remove(mapping);
	locker.Unlock();

	VMAddressSpace* aspace = VMAddressSpace::Get(team);
	if (aspace != NULL) {
		unmap_dot_pages(dot, mapping, aspace);

		PerAspaceData* aspaceData = lookup_per_aspace(team);
		if (aspaceData != NULL)
			remove_tree_node(aspaceData, mapping->base);

		aspace->WriteLock();
		aspace->UnreserveAddressRange(mapping->base, mapping->size, 0);
		aspace->WriteUnlock();
		aspace->Put();
	}
	object_cache_delete(sMappingCache, mapping);
}


// Called when the last handle to a dot is released (KosmDotObject destructor).
// Triggers full dot destruction: mark deleted, remove from hash, unmap all,
// release cache, free struct when last internal ref goes.
KosmDotObject::~KosmDotObject()
{
	kosm_dot_destroy(internal_id);
}


void
kosm_dot_destroy(int32 internalId)
{
	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return;

	delete_dot(dot);
	put_dot(dot);	// release lookup ref
}


status_t
kosm_dot_close_for_team(int32 internalId, team_id team)
{
	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	close_dot_mapping_for_team(dot, team);
	put_dot(dot);
	return B_OK;
}


int32
kosm_dot_object_id(KernelReferenceable* object)
{
	KosmDotObject* dotObj = static_cast<KosmDotObject*>(object);
	return dotObj->internal_id;
}


status_t
kosm_dot_resolve_handle(kosm_handle_t handle, uint32 requiredRights,
	int32* outInternalId)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	KernelReferenceable* object;
	status_t status = table->Lookup(handle, KOSM_HANDLE_DOT,
		requiredRights, &object);
	if (status != B_OK)
		return status;

	KosmDotObject* dotObj = static_cast<KosmDotObject*>(object);
	*outInternalId = dotObj->internal_id;
	object->ReleaseReference();
	return B_OK;
}


// Page fault handler (internal)
static status_t
kosm_dot_soft_fault(KosmDot* dot, addr_t mappingBase,
	VMAddressSpace* aspace, addr_t address,
	bool isWrite, bool isExecute, bool isUser)
{
	TRACE("kosm_dot_soft_fault: dot %d, addr %#lx, write %d\n",
		dot->id, address, isWrite);

	// Protection check
	uint32 prot = dot->protection;
	if (isWrite && !(prot & KOSM_PROT_WRITE))
		return B_PERMISSION_DENIED;
	if (isExecute && !(prot & KOSM_PROT_EXEC))
		return B_PERMISSION_DENIED;
	if (!isWrite && !isExecute && !(prot & KOSM_PROT_READ))
		return B_PERMISSION_DENIED;

	// Device dots are fully mapped at creation
	if (dot->IsDevice())
		return B_BAD_ADDRESS;

	// Purgeable dots in VOLATILE or EMPTY state must not fault in pages.
	// VOLATILE: system may purge at any moment, faulting would race.
	// EMPTY: data was purged, app must call mark_nonvolatile() first.
	// Use atomic_get for ARM64 ordering — this read has no lock and
	// must see the store from purge_volatile (done under dot->lock).
	if (dot->IsPurgeable()
		&& atomic_get((int32*)&dot->purgeable_state)
			!= KOSM_PURGE_NONVOLATILE) {
		return B_PERMISSION_DENIED;
	}

	// Stack guard check
	if (dot->IsStack()) {
		addr_t guardEnd = mappingBase + dot->guard_size;
		if (address < guardEnd)
			return B_BAD_ADDRESS;
	}

	// Reserve pages
	VMTranslationMap* map = aspace->TranslationMap();
	vm_page_reservation reservation;
	size_t reservePages = 1 + map->MaxPagesNeededToMap(address, address);
	vm_page_reserve_pages(&reservation, reservePages,
		aspace == VMAddressSpace::Kernel()
			? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);

	// Lock cache, look up or allocate page
	VMCache* cache = dot->cache;
	cache->Lock();

	// Re-check purgeable state under cache lock. purge_volatile sets
	// EMPTY under dot->lock then takes cache->Lock() to unmap/purge.
	// The cache mutex provides happens-before ordering: if we acquire
	// the cache lock after purge released it, we see the EMPTY state.
	if (dot->IsPurgeable()
		&& atomic_get((int32*)&dot->purgeable_state)
			!= KOSM_PURGE_NONVOLATILE) {
		cache->Unlock();
		vm_page_unreserve_pages(&reservation);
		return B_PERMISSION_DENIED;
	}

	off_t cacheOffset = (off_t)(address - mappingBase);
	vm_page* page = cache->LookupPage(cacheOffset);

	if (page != NULL && page->busy) {
		// Wait for the page to become unbusy
		cache->WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, false);
		// Cache was unlocked by WaitForPageEvents
		vm_page_unreserve_pages(&reservation);
		return B_OK; // caller retries
	}

	if (page != NULL && page->WiredCount() == 0) {
		// Page was reclaimed by the dot scanner but still in cache.
		// Re-map it without allocation or I/O. Data is preserved.
		DEBUG_PAGE_ACCESS_START(page);
		vm_page_set_state(page, PAGE_STATE_ACTIVE);

		uint32 haikuProt = KosmDot::ToHaikuProtection(dot->protection);
		map->Lock();
		map->Map(address, page->physical_page_number * B_PAGE_SIZE,
			haikuProt, dot->MemoryType(), &reservation);
		map->Unlock();

		page->IncrementWiredCount();
		tag_add_resident(dot->tag, 1);

		DEBUG_PAGE_ACCESS_END(page);
		cache->Unlock();
		vm_page_unreserve_pages(&reservation);
		return B_OK;
	}

	if (page == NULL && dot->IsFileBacked()) {
		// File-backed dot: allocate page and read content from file.
		// VMVnodeCache::Commit is a no-op so we skip commit logic.
		uint32 pageFlags = PAGE_STATE_ACTIVE | VM_PAGE_ALLOC_BUSY;
		page = vm_page_allocate_page(&reservation, pageFlags);
		cache->InsertPage(page, cacheOffset);

		// Build physical I/O vector for the page
		phys_addr_t physAddr
			= page->physical_page_number * B_PAGE_SIZE;
		generic_io_vec vec;
		vec.base = physAddr;
		vec.length = B_PAGE_SIZE;
		generic_size_t numBytes = B_PAGE_SIZE;

		// Read from file. Must unlock cache during I/O (may block).
		off_t fileOffset = dot->cache_offset + cacheOffset;
		cache->Unlock();

		status_t readStatus = cache->Read(fileOffset,
			&vec, 1, B_PHYSICAL_IO_REQUEST, &numBytes);

		cache->Lock();

		if (readStatus != B_OK) {
			cache->RemovePage(page);
			cache->Unlock();
			vm_page_free(NULL, page);
			vm_page_unreserve_pages(&reservation);
			return readStatus;
		}

		tag_add_resident(dot->tag, 1);
		// Fall through to mapping below
	} else if (page == NULL) {
		// Anonymous dot: commit memory and allocate a fresh page.
		off_t neededCommitment = (off_t)(cache->page_count + 1) * B_PAGE_SIZE;
		if (neededCommitment > cache->committed_size) {
			status_t commitStatus = cache->Commit(neededCommitment,
				aspace == VMAddressSpace::Kernel()
					? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);
			if (commitStatus != B_OK) {
				cache->Unlock();
				vm_page_unreserve_pages(&reservation);
				return B_NO_MEMORY;
			}
		}

		uint32 pageFlags = PAGE_STATE_WIRED | VM_PAGE_ALLOC_BUSY;
		if (!(dot->flags & KOSM_DOT_NOCLEAR))
			pageFlags |= VM_PAGE_ALLOC_CLEAR;

		page = vm_page_allocate_page(&reservation, pageFlags);
		cache->InsertPage(page, cacheOffset);
		tag_add_resident(dot->tag, 1);
	}

	// Map the page
	uint32 haikuProt = KosmDot::ToHaikuProtection(dot->protection);
	map->Lock();
	map->Map(address, page->physical_page_number * B_PAGE_SIZE,
		haikuProt, dot->MemoryType(), &reservation);
	map->Unlock();

	// Wired count tracks the mapping (no vm_page_mapping objects)
	page->IncrementWiredCount();

	if (page->busy)
		cache->MarkPageUnbusy(page);

	DEBUG_PAGE_ACCESS_END(page);
	cache->Unlock();

	vm_page_unreserve_pages(&reservation);
	return B_OK;
}


// Internal create for a specific team.
// Returns handle value (> 0) or negative error.
kosm_handle_t
kosm_create_dot_for(team_id team, const char* name,
	void** address, uint32 addressSpec, size_t size,
	uint32 protection, uint32 flags, uint32 tag,
	phys_addr_t physicalAddress)
{
	if (size == 0)
		return B_BAD_VALUE;

	size = ROUNDUP(size, B_PAGE_SIZE);

	bool isDevice = (flags & KOSM_DOT_DEVICE) != 0;

	// Validate device constraints
	if (isDevice) {
		if (physicalAddress == 0 || physicalAddress % B_PAGE_SIZE != 0)
			return B_BAD_VALUE;
		if (flags & (KOSM_DOT_PURGEABLE | KOSM_DOT_STACK | KOSM_DOT_NOCLEAR))
			return B_BAD_VALUE;
	}

	// Validate flag combinations
	if ((flags & KOSM_DOT_CONTIGUOUS) && (flags & KOSM_DOT_PURGEABLE))
		return B_BAD_VALUE;

	// Get handle table for target team
	KosmHandleTable* table = KosmHandleTable::TableFor(team);
	if (table == NULL)
		return B_BAD_TEAM_ID;

	// Get address space
	VMAddressSpace* aspace = VMAddressSpace::Get(team);
	if (aspace == NULL)
		return B_BAD_TEAM_ID;

	// Ensure per-aspace data exists
	PerAspaceData* aspaceData = get_or_create_per_aspace(team);
	if (aspaceData == NULL) {
		aspace->Put();
		return B_NO_MEMORY;
	}

	// Allocate the kosm_dot struct
	KosmDot* dot = allocate_dot();
	if (dot == NULL) {
		aspace->Put();
		return B_NO_MEMORY;
	}

	// Assign ID
	kosm_dot_id id = atomic_add(&sNextDotID, 1);

	// Initialize
	dot->id = id;
	if (name != NULL)
		strlcpy(dot->name, name, KOSM_DOT_NAME_LENGTH);
	else
		strlcpy(dot->name, "unnamed", KOSM_DOT_NAME_LENGTH);

	dot->state = DOT_STATE_ACTIVE;
	dot->ref_count = 1;
	mutex_init(&dot->lock, "kosm_dot");
	dot->owner_team = team;
	dot->size = size;
	dot->guard_size = 0;
	dot->protection = protection;
	dot->flags = flags;
	dot->cache_policy = isDevice ? KOSM_CACHE_DEVICE : KOSM_CACHE_DEFAULT;
	dot->purgeable_state = KOSM_PURGE_NONVOLATILE;
	dot->tag = tag;
	dot->wiring = derive_wiring(flags);
	dot->cache = NULL;
	dot->cache_offset = 0;
	dot->phys_base = isDevice ? physicalAddress : 0;
	dot->memory_type = 0;

	char condName[32];
	snprintf(condName, sizeof(condName), "kosm_dot %d", (int)id);
	dot->condition.Init(dot, condName);

	// Guard pages for stack dots
	if (flags & KOSM_DOT_STACK) {
		dot->guard_size = B_PAGE_SIZE; // one guard page by default
	}

	// Reserve virtual address range
	aspace->WriteLock();

	void* reservedAddr = *address;
	virtual_address_restrictions addressRestrictions = {};
	addressRestrictions.address = reservedAddr;
	addressRestrictions.address_specification = addressSpec;

	status_t status = aspace->ReserveAddressRange(size,
		&addressRestrictions, 0, 0, &reservedAddr);

	if (status != B_OK) {
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return status;
	}

	addr_t base = (addr_t)reservedAddr;
	aspace->WriteUnlock();

	// Create VMCache (not for device dots)
	if (!isDevice) {
		VMCache* cache;
		bool canOvercommit = (flags & KOSM_DOT_STACK) != 0;
		int32 guardPages = dot->guard_size / B_PAGE_SIZE;
		bool isPurgeable = (flags & KOSM_DOT_PURGEABLE) != 0;

		if (isPurgeable) {
			// Purgeable cache: no overcommit, no swap, discardable
			uint32 allocFlags = HEAP_DONT_WAIT_FOR_MEMORY
				| HEAP_DONT_LOCK_KERNEL_SPACE;
			KosmDotPurgeableCache* purgeCache
				= new(gKosmDotPurgeableCacheObjectCache, allocFlags)
					KosmDotPurgeableCache;
			if (purgeCache == NULL) {
				aspace->WriteLock();
				aspace->UnreserveAddressRange(base, size, 0);
				aspace->WriteUnlock();
				aspace->Put();
				free_dot(dot);
				return B_NO_MEMORY;
			}

			status = purgeCache->Init(allocFlags);
			if (status != B_OK) {
				purgeCache->Delete();
				aspace->WriteLock();
				aspace->UnreserveAddressRange(base, size, 0);
				aspace->WriteUnlock();
				aspace->Put();
				free_dot(dot);
				return status;
			}

			purgeCache->SetDot(dot);
			cache = purgeCache;
		} else {
			// Standard anonymous cache
			status = VMCacheFactory::CreateAnonymousCache(cache,
				canOvercommit, 0, guardPages, false,
				team == VMAddressSpace::KernelID()
					? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);

			if (status != B_OK) {
				aspace->WriteLock();
				aspace->UnreserveAddressRange(base, size, 0);
				aspace->WriteUnlock();
				aspace->Put();
				free_dot(dot);
				return status;
			}
		}

		cache->Lock();
		cache->temporary = 1;
		cache->virtual_base = 0;
		cache->virtual_end = size;

		// For wired/contiguous: commit all memory upfront
		if (flags & (KOSM_DOT_WIRED | KOSM_DOT_CONTIGUOUS)) {
			status = cache->Commit(size,
				team == VMAddressSpace::KernelID()
					? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);
			if (status != B_OK) {
				cache->ReleaseRefAndUnlock();
				aspace->WriteLock();
				aspace->UnreserveAddressRange(base, size, 0);
				aspace->WriteUnlock();
				aspace->Put();
				free_dot(dot);
				return status;
			}
		}

		cache->Unlock();
		dot->cache = cache;
	}

	// Create owner mapping
	KosmDotMapping* mapping = new(sMappingCache, 0) KosmDotMapping;
	if (mapping == NULL) {
		if (dot->cache != NULL) {
			dot->cache->Lock();
			dot->cache->ReleaseRefAndUnlock();
		}
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return B_NO_MEMORY;
	}

	mapping->dot_id = id;
	mapping->team = team;
	mapping->base = base;
	mapping->size = size;
	mapping->granted_protection = protection;
	mapping->is_owner = true;
	dot->mappings.Add(mapping);

	// Insert tree node
	status = insert_tree_node(aspaceData, dot, team, base, size);
	if (status != B_OK) {
		dot->mappings.Remove(mapping);
		object_cache_delete(sMappingCache, mapping);
		if (dot->cache != NULL) {
			dot->cache->Lock();
			dot->cache->ReleaseRefAndUnlock();
		}
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return status;
	}

	// For device dots: map physical range immediately
	if (isDevice) {
		VMTranslationMap* map = aspace->TranslationMap();

		// Set memory type
		uint32 memoryType;
		if (dot->cache_policy == KOSM_CACHE_DEVICE)
			memoryType = B_UNCACHED_MEMORY;
		else if (dot->cache_policy == KOSM_CACHE_WRITECOMBINE)
			memoryType = B_WRITE_COMBINING_MEMORY;
		else
			memoryType = B_UNCACHED_MEMORY;
		dot->SetMemoryType(memoryType);

		vm_page_reservation reservation;
		size_t reservePages = map->MaxPagesNeededToMap(base, base + size - 1);
		vm_page_reserve_pages(&reservation, reservePages,
			VM_PRIORITY_SYSTEM);

		uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
		map->Lock();

		for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
			map->Map(base + offset, physicalAddress + offset,
				haikuProt, dot->MemoryType(), &reservation);
		}

		map->Unlock();
		vm_page_unreserve_pages(&reservation);
	}

	// For wired dots: allocate and map all pages now
	if (!isDevice && (flags & KOSM_DOT_WIRED)) {
		VMCache* cache = dot->cache;
		VMTranslationMap* map = aspace->TranslationMap();

		vm_page_reservation reservation;
		size_t reservePages = (size / B_PAGE_SIZE)
			+ map->MaxPagesNeededToMap(base, base + size - 1);
		vm_page_reserve_pages(&reservation, reservePages,
			team == VMAddressSpace::KernelID()
				? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);

		cache->Lock();

		uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
		uint32 pageFlags = PAGE_STATE_WIRED;
		if (!(flags & KOSM_DOT_NOCLEAR))
			pageFlags |= VM_PAGE_ALLOC_CLEAR;

		map->Lock();

		for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
			// Skip guard pages for stack dots
			if (dot->IsStack() && offset < dot->guard_size)
				continue;

			vm_page* page = vm_page_allocate_page(&reservation, pageFlags);
			cache->InsertPage(page, (off_t)offset);

			map->Map(base + offset, page->physical_page_number * B_PAGE_SIZE,
				haikuProt, dot->MemoryType(), &reservation);
			page->IncrementWiredCount();
			tag_add_resident(dot->tag, 1);

			DEBUG_PAGE_ACCESS_END(page);
		}

		map->Unlock();
		cache->Unlock();
		vm_page_unreserve_pages(&reservation);
	}

	// For contiguous dots: allocate a page run and map all at once
	if (!isDevice && (flags & KOSM_DOT_CONTIGUOUS)) {
		VMCache* cache = dot->cache;
		VMTranslationMap* map = aspace->TranslationMap();
		page_num_t pageCount = size / B_PAGE_SIZE;

		physical_address_restrictions physRestrictions = {};
		vm_page* pageRun = vm_page_allocate_page_run(
			PAGE_STATE_WIRED | ((flags & KOSM_DOT_NOCLEAR) ? 0 : VM_PAGE_ALLOC_CLEAR),
			pageCount, &physRestrictions,
			team == VMAddressSpace::KernelID()
				? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER);

		if (pageRun == NULL) {
			// Cleanup on failure
			remove_tree_node(aspaceData, base);
			dot->mappings.Remove(mapping);
			object_cache_delete(sMappingCache, mapping);
			cache->Lock();
			cache->ReleaseRefAndUnlock();
			aspace->WriteLock();
			aspace->UnreserveAddressRange(base, size, 0);
			aspace->WriteUnlock();
			aspace->Put();
			free_dot(dot);
			return B_NO_MEMORY;
		}

		vm_page_reservation reservation;
		size_t reserveMapPages = map->MaxPagesNeededToMap(base, base + size - 1);
		vm_page_reserve_pages(&reservation, reserveMapPages,
			VM_PRIORITY_SYSTEM);

		cache->Lock();

		uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
		phys_addr_t physAddr = pageRun->physical_page_number * B_PAGE_SIZE;

		map->Lock();

		for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
			vm_page* page = vm_lookup_page(
				(physAddr + offset) / B_PAGE_SIZE);
			cache->InsertPage(page, (off_t)offset);
			map->Map(base + offset, physAddr + offset,
				haikuProt, dot->MemoryType(), &reservation);
			page->IncrementWiredCount();
			tag_add_resident(dot->tag, 1);
			DEBUG_PAGE_ACCESS_END(page);
		}

		map->Unlock();
		cache->Unlock();
		vm_page_unreserve_pages(&reservation);
	}

	// Insert into global hash
	rw_lock_write_lock(&sDotTableLock);
	sDotTable.InsertUnchecked(dot);
	rw_lock_write_unlock(&sDotTableLock);

	// Tag accounting
	tag_add(tag, size);

	// Create KosmDotObject wrapper for handle table
	KosmDotObject* dotObj = new(std::nothrow) KosmDotObject(id);
	if (dotObj == NULL) {
		// Undo: remove from hash, cleanup
		rw_lock_write_lock(&sDotTableLock);
		sDotTable.RemoveUnchecked(dot);
		rw_lock_write_unlock(&sDotTableLock);
		tag_remove(tag, size);
		// Unmap + free mapping + tree node + cache + VA reservation
		remove_tree_node(aspaceData, base);
		while (KosmDotMapping* m = dot->mappings.RemoveHead())
			object_cache_delete(sMappingCache, m);
		if (dot->cache != NULL) {
			dot->cache->Lock();
			dot->cache->ReleaseRefAndUnlock();
		}
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return B_NO_MEMORY;
	}

	// Insert handle into team's table
	kosm_handle_t handle = table->Insert(dotObj, KOSM_HANDLE_DOT,
		KOSM_RIGHT_DOT_DEFAULT);
	if (handle < 0) {
		// Undo everything
		delete dotObj;  // no refs acquired yet by Insert
		rw_lock_write_lock(&sDotTableLock);
		sDotTable.RemoveUnchecked(dot);
		rw_lock_write_unlock(&sDotTableLock);
		tag_remove(tag, size);
		remove_tree_node(aspaceData, base);
		while (KosmDotMapping* m = dot->mappings.RemoveHead())
			object_cache_delete(sMappingCache, m);
		if (dot->cache != NULL) {
			dot->cache->Lock();
			dot->cache->ReleaseRefAndUnlock();
		}
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return (status_t)handle;
	}

	aspace->Put();

	*address = (void*)base;

	TRACE("kosm_create_dot: id %d, handle %d, base %#lx, size %#lx\n",
		(int)id, (int)handle, base, size);
	return handle;
}


// Kernel API

status_t
kosm_dot_init(void)
{
	TRACE("kosm_dot_init\n");

	sDotCache = create_object_cache("kosm_dot", sizeof(KosmDot), 0);
	sMappingCache = create_object_cache("kosm_dot mapping",
		sizeof(KosmDotMapping), 0);
	sTreeNodeCache = create_object_cache("kosm_dot tree node",
		sizeof(KosmDotTreeNode), 0);
	sWiredRangeCache = create_object_cache("kosm_dot wired range",
		sizeof(KosmDotWiredRange), 0);
	sPerAspaceCache = create_object_cache("kosm_dot per-aspace",
		sizeof(PerAspaceData), 0);

	if (sDotCache == NULL || sMappingCache == NULL
		|| sTreeNodeCache == NULL || sWiredRangeCache == NULL
		|| sPerAspaceCache == NULL) {
		panic("kosm_dot_init: failed to create object caches");
		return B_NO_MEMORY;
	}

	{
		new(&sDotTable) KosmDotHashTable;
		status_t error = sDotTable.Init(kDotHashSize);
		if (error != B_OK) {
			panic("kosm_dot_init: failed to init dot hash table");
			return error;
		}
	}

	{
		new(&sPerAspaceTable) PerAspaceTable;
		status_t error = sPerAspaceTable.Init(kPerAspaceHashSize);
		if (error != B_OK) {
			panic("kosm_dot_init: failed to init per-aspace hash table");
			return error;
		}
	}

	memset(sTagCounters, 0, sizeof(sTagCounters));

	// Initialize purgeable cache slab
	status_t purgeInitError = kosm_dot_purgeable_cache_init();
	if (purgeInitError != B_OK) {
		panic("kosm_dot_init: failed to init purgeable cache slab");
		return purgeInitError;
	}

	return B_OK;
}


// Low resource handler for purgeable kosm_dots
static void
kosm_dot_low_resource_handler(void* /*data*/, uint32 resources,
	int32 level)
{
	if ((resources & B_KERNEL_RESOURCE_PAGES) == 0
		&& (resources & B_KERNEL_RESOURCE_MEMORY) == 0)
		return;

	size_t targetPages;
	switch (level) {
		case B_LOW_RESOURCE_NOTE:
			targetPages = 32;
			break;
		case B_LOW_RESOURCE_WARNING:
			targetPages = 256;
			break;
		case B_LOW_RESOURCE_CRITICAL:
		default:
			targetPages = 4096;
			break;
	}

	kosm_dot_purge_volatile(targetPages);

	// Also wake the page reclaim scanner for reclaimable dots.
	// The scanner will aggressively reclaim pages from lazy dots.
	if (level >= B_LOW_RESOURCE_WARNING) {
		atomic_set(&sDotScannerActive, 1);
		sDotScannerCondition.NotifyAll();
	}
}


// Page fault hook
status_t
kosm_dot_fault(VMAddressSpace* addressSpace,
	addr_t address, bool isWrite, bool isExecute, bool isUser)
{
	PerAspaceData* data = lookup_per_aspace(addressSpace->ID());
	if (data == NULL)
		return B_BAD_ADDRESS;

	KosmDot* dot;
	addr_t mappingBase;

	rw_lock_read_lock(&data->lock);
	KosmDotTreeNode* node = data->tree.Find(address);
	if (node != NULL) {
		dot = node->kosm_dot;
		mappingBase = node->base;
		// Acquire ref while node is still valid (under lock).
		// This prevents delete_dot() from freeing the dot while
		// we use it in soft_fault.
		dot->AcquireRef();
	}
	rw_lock_read_unlock(&data->lock);

	if (node == NULL)
		return B_BAD_ADDRESS;

	if (!dot->IsActive()) {
		put_dot(dot);
		return B_BAD_ADDRESS;
	}

	status_t status = kosm_dot_soft_fault(dot, mappingBase, addressSpace,
		address, isWrite, isExecute, isUser);

	put_dot(dot);

	return status;
}


// Team death: unmap all mappings belonging to this team.
// Must be called BEFORE the address space is destroyed and BEFORE
// the handle table destructor runs. The handle table destructor
// releases KosmDotObject refs, which may trigger dot destruction
// (kosm_dot_destroy) if this team held the last handle.
void
kosm_dot_team_gone(team_id team)
{
	TRACE("kosm_dot_team_gone: team %d\n", (int)team);

	// Collect dot IDs that have mappings for this team
	struct DotEntry {
		kosm_dot_id id;
		DotEntry* next;
	};
	DotEntry* list = NULL;

	rw_lock_read_lock(&sDotTableLock);
	KosmDotHashTable::Iterator it = sDotTable.GetIterator();
	while (KosmDot* dot = it.Next()) {
		if (dot->FindMapping(team) == NULL)
			continue;

		DotEntry* entry = (DotEntry*)malloc(sizeof(DotEntry));
		if (entry == NULL)
			continue;
		entry->id = dot->id;
		entry->next = list;
		list = entry;
	}
	rw_lock_read_unlock(&sDotTableLock);

	// Unmap all mappings for this team.
	// No dot destruction here -- the handle table destructor handles
	// ref release, which triggers kosm_dot_destroy when last ref goes.
	for (DotEntry* e = list; e != NULL;) {
		DotEntry* next = e->next;
		KosmDot* dot = lookup_dot(e->id);
		if (dot != NULL) {
			close_dot_mapping_for_team(dot, team);
			put_dot(dot);
		}
		free(e);
		e = next;
	}
}


void
kosm_dot_aspace_gone(team_id team)
{
	remove_per_aspace(team);
}


// Memory pressure: purge volatile dots
size_t
kosm_dot_purge_volatile(size_t targetPages)
{
	size_t freedPages = 0;

	// Collect volatile dot IDs first, then process them individually.
	// We cannot hold the hash table lock while doing purge work (it
	// takes dot->lock and cache->Lock), and BOpenHashTable::Iterator
	// is invalidated by concurrent modifications between unlock/relock.
	static const int kBatchSize = 32;
	kosm_dot_id batch[kBatchSize];

	while (freedPages < targetPages) {
		int batchCount = 0;

		rw_lock_read_lock(&sDotTableLock);
		KosmDotHashTable::Iterator it = sDotTable.GetIterator();
		while (KosmDot* dot = it.Next()) {
			if (batchCount >= kBatchSize)
				break;
			if (!dot->IsPurgeable() || !dot->IsActive())
				continue;
			if (atomic_get((int32*)&dot->purgeable_state)
				!= KOSM_PURGE_VOLATILE)
				continue;
			batch[batchCount++] = dot->id;
		}
		rw_lock_read_unlock(&sDotTableLock);

		if (batchCount == 0)
			break;

		for (int i = 0; i < batchCount && freedPages < targetPages; i++) {
			KosmDot* dot = lookup_dot(batch[i]);
			if (dot == NULL)
				continue;

			MutexLocker locker(dot->lock);

			if (dot->purgeable_state == KOSM_PURGE_VOLATILE
				&& dot->cache != NULL) {

				// Transition to EMPTY first. This prevents concurrent faults
				// from mapping new pages into this dot (soft_fault re-checks
				// purgeable_state under cache lock after we unmap).
				dot->purgeable_state = KOSM_PURGE_EMPTY;

				// Unmap all pages from all mappings (PTEs + wired counts)
				for (KosmDotMappingList::Iterator mIt
						= dot->mappings.GetIterator();
						KosmDotMapping* mapping = mIt.Next();) {
					VMAddressSpace* aspace = VMAddressSpace::Get(mapping->team);
					if (aspace != NULL) {
						unmap_dot_pages(dot, mapping, aspace);
						aspace->Put();
					} else {
						// Aspace gone — PTEs already destroyed. Manually
						// decrement wired counts so Purge() can free pages.
						VMCache* cache = dot->cache;
						cache->Lock();
						for (VMCachePagesTree::Iterator pIt
								= cache->pages.GetIterator();
								vm_page* page = pIt.Next();) {
							if (page->WiredCount() > 0)
								page->DecrementWiredCount();
						}
						cache->Unlock();
					}
				}

				// Delegate page discard + commitment release to the cache
				KosmDotPurgeableCache* purgeCache
					= static_cast<KosmDotPurgeableCache*>(dot->cache);
				purgeCache->Lock();
				size_t pagesFreed = purgeCache->Purge();
				purgeCache->Unlock();

				freedPages += pagesFreed;
				tag_remove_resident(dot->tag, pagesFreed);

				dot->condition.NotifyAll();
			}

			locker.Unlock();
			put_dot(dot);
		}
	}

	return freedPages;
}


// Address lookup
KosmDotTreeNode*
kosm_dot_lookup_address(team_id team, addr_t address)
{
	PerAspaceData* data = lookup_per_aspace(team);
	if (data == NULL)
		return NULL;

	rw_lock_read_lock(&data->lock);
	KosmDotTreeNode* node = data->tree.Find(address);
	rw_lock_read_unlock(&data->lock);
	return node;
}


// Tag stats
status_t
kosm_dot_get_tag_stats(uint32 tag, kosm_dot_tag_stats* stats)
{
	if (tag >= KOSM_TAG_USER_BASE)
		return B_BAD_VALUE;

	MutexLocker locker(sTagLock);
	stats->tag = tag;
	stats->total_size = sTagCounters[tag].total_size;
	stats->resident_size = sTagCounters[tag].resident_size;
	stats->dot_count = sTagCounters[tag].dot_count;
	stats->purgeable_volatile = 0;
	stats->purgeable_empty = 0;
	return B_OK;
}


status_t
kosm_dot_get_next_tag_stats(int32* cookie, kosm_dot_tag_stats* stats)
{
	int32 index = *cookie;
	while (index < (int32)KOSM_TAG_USER_BASE) {
		if (sTagCounters[index].dot_count > 0) {
			status_t status = kosm_dot_get_tag_stats(index, stats);
			*cookie = index + 1;
			return status;
		}
		index++;
	}
	return B_ENTRY_NOT_FOUND;
}


// Page reclaim scanner for reclaimable dots.
//
// Periodically checks accessed bits on pages of KOSM_DOT_RECLAIMABLE dots.
// Pages not accessed since the last scan have their usage_count decremented.
// When usage_count reaches 0, pages are unmapped from all mappings and
// transitioned to PAGE_STATE_CACHED (anonymous) or PAGE_STATE_MODIFIED
// (file-backed dirty). The page daemon can then steal these pages if needed.
//
// On re-fault: if page is still in cache, it is re-mapped without I/O.
// If page was stolen: anonymous dots get a fresh zeroed page (data lost),
// file-backed dots re-read from file (no data loss).

static void
dot_scanner_pass(bool aggressive)
{
	static const int kBatchSize = 32;
	kosm_dot_id batch[kBatchSize];
	int batchCount = 0;

	rw_lock_read_lock(&sDotTableLock);
	KosmDotHashTable::Iterator it = sDotTable.GetIterator();
	while (KosmDot* dot = it.Next()) {
		if (batchCount >= kBatchSize)
			break;
		if (!dot->IsActive())
			continue;
		// Only scan reclaimable lazy dots (not wired, device, purgeable)
		if (!dot->IsReclaimable())
			continue;
		if (dot->IsDevice() || dot->IsPurgeable())
			continue;
		if (dot->wiring != B_NO_LOCK)
			continue;
		batch[batchCount++] = dot->id;
	}
	rw_lock_read_unlock(&sDotTableLock);

	for (int i = 0; i < batchCount; i++) {
		KosmDot* dot = lookup_dot(batch[i]);
		if (dot == NULL)
			continue;

		VMCache* cache = dot->cache;
		if (cache == NULL) {
			put_dot(dot);
			continue;
		}

		cache->Lock();

		for (VMCachePagesTree::Iterator pIt = cache->pages.GetIterator();
				vm_page* page = pIt.Next();) {

			if (page->busy)
				continue;

			// Skip pages that are not wired (already reclaimed)
			if (page->WiredCount() == 0)
				continue;

			// Check accessed bit via each mapping's translation map
			bool accessed = false;
			uint32 haikuProt = KosmDot::ToHaikuProtection(
				dot->protection);

			for (KosmDotMappingList::Iterator mIt
					= dot->mappings.GetIterator();
					KosmDotMapping* mapping = mIt.Next();) {
				VMAddressSpace* aspace
					= VMAddressSpace::Get(mapping->team);
				if (aspace == NULL)
					continue;

				VMTranslationMap* map = aspace->TranslationMap();
				addr_t pageAddr = mapping->base
					+ ((off_t)page->cache_offset * B_PAGE_SIZE);

				if (pageAddr < mapping->base
					|| pageAddr >= mapping->base + mapping->size) {
					aspace->Put();
					continue;
				}

				phys_addr_t phys;
				uint32 flags;
				map->Lock();
				if (map->Query(pageAddr, &phys, &flags) == B_OK
					&& (flags & PAGE_PRESENT) != 0
					&& (flags & PAGE_ACCESSED) != 0) {
					// Clear accessed bit by re-protecting with
					// same protection (rewrites PTE, clears HW
					// accessed/modified flags). Does not require
					// VMArea unlike ClearAccessedAndModified.
					uint32 mapProt = haikuProt;
					if (!mapping->is_owner)
						mapProt = KosmDot::ToHaikuProtection(
							dot->protection
								& mapping->granted_protection);

					if (flags & PAGE_MODIFIED)
						page->modified = true;

					map->Protect(pageAddr,
						pageAddr + B_PAGE_SIZE - 1,
						mapProt, dot->MemoryType());
					accessed = true;
				}
				map->Unlock();
				aspace->Put();
			}

			if (accessed) {
				page->usage_count = kPageUsageMax;
				continue;
			}

			// Page not accessed since last scan.
			int32 decline = aggressive
				? kPageUsageMax : kPageUsageDecline;
			page->usage_count -= decline;
			if (page->usage_count > 0)
				continue;
			page->usage_count = 0;

			// Reclaim: unmap from all mappings
			DEBUG_PAGE_ACCESS_START(page);

			for (KosmDotMappingList::Iterator mIt
					= dot->mappings.GetIterator();
					KosmDotMapping* mapping = mIt.Next();) {
				VMAddressSpace* aspace
					= VMAddressSpace::Get(mapping->team);
				if (aspace == NULL)
					continue;

				VMTranslationMap* map = aspace->TranslationMap();
				addr_t pageAddr = mapping->base
					+ ((off_t)page->cache_offset * B_PAGE_SIZE);

				if (pageAddr < mapping->base
					|| pageAddr >= mapping->base + mapping->size) {
					aspace->Put();
					continue;
				}

				map->Lock();
				// Harvest modified bit before unmapping
				phys_addr_t phys;
				uint32 flags;
				if (map->Query(pageAddr, &phys, &flags) == B_OK
					&& (flags & PAGE_MODIFIED) != 0) {
					page->modified = true;
				}
				map->Unmap(pageAddr, pageAddr + B_PAGE_SIZE - 1);
				map->Unlock();

				page->DecrementWiredCount();
				aspace->Put();
			}

			// Page is now unmapped, wired_count == 0, still in cache.
			if (page->modified)
				vm_page_set_state(page, PAGE_STATE_MODIFIED);
			else
				vm_page_set_state(page, PAGE_STATE_CACHED);

			tag_remove_resident(dot->tag, 1);

			DEBUG_PAGE_ACCESS_END(page);
		}

		cache->Unlock();
		put_dot(dot);
	}
}


static status_t
dot_scanner_thread(void* /*unused*/)
{
	sDotScannerCondition.Init(NULL, "kosm_dot scanner");

	while (!sDotScannerShouldExit) {
		bool aggressive = atomic_get(&sDotScannerActive) > 0;
		atomic_set(&sDotScannerActive, 0);

		dot_scanner_pass(aggressive);

		sDotScannerCondition.Wait(B_RELATIVE_TIMEOUT, kScannerIdleInterval);
	}
	return B_OK;
}


status_t
kosm_dot_init_post_sem(void)
{
	// Register low resource handler for purgeable dots
	register_low_resource_handler(kosm_dot_low_resource_handler,
		NULL, B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY, 0);

	// NOTE: scanner thread is spawned later in kosm_dot_init_post_thread()
	// because thread creation requires slab caches that aren't ready yet.

	// Register KDL commands
	add_debugger_command("kosm_dots", [](int argc, char** argv) -> int {
		if (argc > 1 && strcmp(argv[1], "-t") == 0 && argc > 2) {
			uint32 tag = strtoul(argv[2], NULL, 0);
			kprintf("kosm_dots with tag %u:\n", tag);
			kprintf("  %*s  %5s  %*s  %*s  prot  flags  state  name\n",
				B_PRINTF_POINTER_WIDTH, "dot", "id",
				B_PRINTF_POINTER_WIDTH, "base",
				B_PRINTF_POINTER_WIDTH, "size");

			KosmDotHashTable::Iterator it = sDotTable.GetIterator();
			while (KosmDot* dot = it.Next()) {
				if (dot->tag == tag) {
					KosmDotMapping* m = dot->mappings.Head();
					kprintf("  %p  %5d  %p  %p  %04x  %04x   %d     %s\n",
						dot, (int)dot->id,
						m ? (void*)m->base : NULL,
						(void*)dot->size,
						dot->protection, dot->flags,
						dot->state, dot->name);
				}
			}
			return 0;
		}

		team_id filterTeam = -1;
		if (argc > 1)
			filterTeam = strtol(argv[1], NULL, 0);

		kprintf("kosm_dots%s:\n",
			filterTeam >= 0 ? " for team" : "");
		kprintf("  %*s  %5s  team  %*s  %*s  prot  flags  state  name\n",
			B_PRINTF_POINTER_WIDTH, "dot", "id",
			B_PRINTF_POINTER_WIDTH, "base",
			B_PRINTF_POINTER_WIDTH, "size");

		KosmDotHashTable::Iterator it = sDotTable.GetIterator();
		while (KosmDot* dot = it.Next()) {
			if (filterTeam >= 0 && dot->owner_team != filterTeam)
				continue;
			KosmDotMapping* m = dot->mappings.Head();
			kprintf("  %p  %5d  %4d  %p  %p  %04x  %04x   %d     %s\n",
				dot, (int)dot->id, (int)dot->owner_team,
				m ? (void*)m->base : NULL,
				(void*)dot->size,
				dot->protection, dot->flags,
				dot->state, dot->name);
		}
		return 0;
	}, "List all kosm_dots [<team>] [-t <tag>]");

	add_debugger_command("kosm_dot", [](int argc, char** argv) -> int {
		if (argc < 2) {
			kprintf("usage: kosm_dot <id|address>\n");
			return 0;
		}
		addr_t val = parse_expression(argv[1]);

		KosmDot* dot;
		if (val < 100000) {
			dot = sDotTable.Lookup((kosm_dot_id)val);
		} else {
			dot = (KosmDot*)val;
		}

		if (dot == NULL) {
			kprintf("kosm_dot not found\n");
			return 0;
		}

		kprintf("KOSM_DOT %p:\n", dot);
		kprintf("  id:          %d\n", (int)dot->id);
		kprintf("  name:        %s\n", dot->name);
		kprintf("  state:       %d\n", dot->state);
		kprintf("  ref_count:   %d\n", (int)dot->ref_count);
		kprintf("  owner_team:  %d\n", (int)dot->owner_team);
		kprintf("  size:        %#lx\n", dot->size);
		kprintf("  protection:  %#x\n", dot->protection);
		kprintf("  flags:       %#x\n", dot->flags);
		kprintf("  cache_policy:%d\n", dot->cache_policy);
		kprintf("  purgeable:   %d\n", dot->purgeable_state);
		kprintf("  tag:         %u\n", dot->tag);
		kprintf("  wiring:      %u\n", dot->wiring);
		kprintf("  cache:       %p\n", dot->cache);
		kprintf("  phys_base:   %#" B_PRIxPHYSADDR "\n", dot->phys_base);

		kprintf("  mappings:\n");
		for (KosmDotMappingList::Iterator it = dot->mappings.GetIterator();
				KosmDotMapping* m = it.Next();) {
			kprintf("    team %d: base %#lx size %#lx %s\n",
				(int)m->team, m->base, m->size,
				m->is_owner ? "(owner)" : "");
		}

		if (!dot->wired_ranges.IsEmpty()) {
			kprintf("  wired ranges:\n");
			for (KosmDotWiredRangeList::Iterator it
					= dot->wired_ranges.GetIterator();
					KosmDotWiredRange* r = it.Next();) {
				kprintf("    offset %#lx size %#lx count %d\n",
					r->offset, r->size, (int)r->wire_count);
			}
		}

		return 0;
	}, "Show detailed info for a kosm_dot");

	add_debugger_command("kosm_purge", [](int argc, char** argv) -> int {
		size_t freed = kosm_dot_purge_volatile(1024);
		kprintf("purged %lu pages\n", freed);
		return 0;
	}, "Manually trigger volatile kosm_dot purge");

	return B_OK;
}


status_t
kosm_dot_init_post_thread(void)
{
	// Spawn page reclaim scanner thread.
	// Deferred from kosm_dot_init_post_sem() because thread creation
	// requires slab caches that are initialized in slab_init_post_thread().
	sDotScannerThread = spawn_kernel_thread(dot_scanner_thread,
		"kosm_dot scanner", B_NORMAL_PRIORITY - 1, NULL);
	if (sDotScannerThread >= 0)
		resume_thread(sDotScannerThread);

	return B_OK;
}


// File-backed dot creation

kosm_handle_t
kosm_create_dot_file_for(team_id team, int fd, off_t fileOffset,
	const char* name, void** address, uint32 addressSpec, size_t size,
	uint32 protection, uint32 flags, uint32 tag)
{
	if (size == 0 || fileOffset < 0)
		return B_BAD_VALUE;
	if (fileOffset % B_PAGE_SIZE != 0)
		return B_BAD_VALUE;

	size = ROUNDUP(size, B_PAGE_SIZE);

	// File-backed dots are always reclaimable (pages can be re-read)
	flags |= KOSM_DOT_FILE | KOSM_DOT_RECLAIMABLE;
	// Incompatible flags
	if (flags & (KOSM_DOT_DEVICE | KOSM_DOT_PURGEABLE | KOSM_DOT_WIRED
			| KOSM_DOT_CONTIGUOUS)) {
		return B_BAD_VALUE;
	}

	// Get handle table for target team
	KosmHandleTable* table = KosmHandleTable::TableFor(team);
	if (table == NULL)
		return B_BAD_TEAM_ID;

	// Get vnode from fd
	struct vnode* vnode;
	status_t status = vfs_get_vnode_from_fd(fd, true, &vnode);
	if (status != B_OK)
		return status;

	// Get or create the VMVnodeCache for this vnode
	VMCache* cache;
	status = vfs_get_vnode_cache(vnode, &cache, false);
	if (status != B_OK) {
		vfs_put_vnode(vnode);
		return status;
	}

	// Acquire an extra ref for the dot (cache already has 1 from
	// vfs_get_vnode_cache)
	cache->Lock();
	cache->AcquireRefLocked();
	cache->Unlock();

	vfs_put_vnode(vnode);

	// Get address space
	VMAddressSpace* aspace = VMAddressSpace::Get(team);
	if (aspace == NULL) {
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		return B_BAD_TEAM_ID;
	}

	// Ensure per-aspace data exists
	PerAspaceData* aspaceData = get_or_create_per_aspace(team);
	if (aspaceData == NULL) {
		aspace->Put();
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		return B_NO_MEMORY;
	}

	// Allocate the kosm_dot struct
	KosmDot* dot = allocate_dot();
	if (dot == NULL) {
		aspace->Put();
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		return B_NO_MEMORY;
	}

	// Assign ID
	kosm_dot_id id = atomic_add(&sNextDotID, 1);

	// Initialize
	dot->id = id;
	if (name != NULL)
		strlcpy(dot->name, name, KOSM_DOT_NAME_LENGTH);
	else
		strlcpy(dot->name, "file_dot", KOSM_DOT_NAME_LENGTH);

	dot->state = DOT_STATE_ACTIVE;
	dot->ref_count = 1;
	mutex_init(&dot->lock, "kosm_dot");
	dot->owner_team = team;
	dot->size = size;
	dot->guard_size = 0;
	dot->protection = protection;
	dot->flags = flags;
	dot->cache_policy = KOSM_CACHE_DEFAULT;
	dot->purgeable_state = KOSM_PURGE_NONVOLATILE;
	dot->tag = tag;
	dot->wiring = B_NO_LOCK;
	dot->cache = cache;
	dot->cache_offset = fileOffset;
	dot->phys_base = 0;
	dot->memory_type = 0;

	char condName[32];
	snprintf(condName, sizeof(condName), "kosm_dot %d", (int)id);
	dot->condition.Init(dot, condName);

	// Reserve virtual address range
	aspace->WriteLock();

	void* reservedAddr = *address;
	virtual_address_restrictions addressRestrictions = {};
	addressRestrictions.address = reservedAddr;
	addressRestrictions.address_specification = addressSpec;

	status = aspace->ReserveAddressRange(size,
		&addressRestrictions, 0, 0, &reservedAddr);

	if (status != B_OK) {
		aspace->WriteUnlock();
		aspace->Put();
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		free_dot(dot);
		return status;
	}

	addr_t base = (addr_t)reservedAddr;
	aspace->WriteUnlock();

	// Create owner mapping
	KosmDotMapping* mapping = new(sMappingCache, 0) KosmDotMapping;
	if (mapping == NULL) {
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return B_NO_MEMORY;
	}

	mapping->dot_id = id;
	mapping->team = team;
	mapping->base = base;
	mapping->size = size;
	mapping->granted_protection = protection;
	mapping->is_owner = true;
	dot->mappings.Add(mapping);

	// Insert tree node
	status = insert_tree_node(aspaceData, dot, team, base, size);
	if (status != B_OK) {
		dot->mappings.Remove(mapping);
		object_cache_delete(sMappingCache, mapping);
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return status;
	}

	// Map any pages already in cache (shared with other users of the file)
	cache->Lock();
	VMTranslationMap* map = aspace->TranslationMap();

	vm_page_reservation mapReservation;
	size_t reserveMapPages = map->MaxPagesNeededToMap(base, base + size - 1);
	vm_page_reserve_pages(&mapReservation, reserveMapPages,
		VM_PRIORITY_USER);

	uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
	map->Lock();

	for (VMCachePagesTree::Iterator pIt = cache->pages.GetIterator();
			vm_page* page = pIt.Next();) {
		off_t pageFileOffset
			= (off_t)page->cache_offset * B_PAGE_SIZE;
		if (pageFileOffset < fileOffset
			|| pageFileOffset >= fileOffset + (off_t)size) {
			continue;
		}
		addr_t pageAddr = base + (pageFileOffset - fileOffset);
		map->Map(pageAddr, page->physical_page_number * B_PAGE_SIZE,
			haikuProt, dot->MemoryType(), &mapReservation);
		page->IncrementWiredCount();
	}

	map->Unlock();
	cache->Unlock();
	vm_page_unreserve_pages(&mapReservation);

	// Insert into global hash
	rw_lock_write_lock(&sDotTableLock);
	sDotTable.InsertUnchecked(dot);
	rw_lock_write_unlock(&sDotTableLock);

	// Tag accounting
	tag_add(tag, size);

	// Create KosmDotObject wrapper for handle table
	KosmDotObject* dotObj = new(std::nothrow) KosmDotObject(id);
	if (dotObj == NULL) {
		rw_lock_write_lock(&sDotTableLock);
		sDotTable.RemoveUnchecked(dot);
		rw_lock_write_unlock(&sDotTableLock);
		tag_remove(tag, size);
		remove_tree_node(aspaceData, base);
		while (KosmDotMapping* m = dot->mappings.RemoveHead())
			object_cache_delete(sMappingCache, m);
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return B_NO_MEMORY;
	}

	kosm_handle_t handle = table->Insert(dotObj, KOSM_HANDLE_DOT,
		KOSM_RIGHT_DOT_DEFAULT);
	if (handle < 0) {
		delete dotObj;
		rw_lock_write_lock(&sDotTableLock);
		sDotTable.RemoveUnchecked(dot);
		rw_lock_write_unlock(&sDotTableLock);
		tag_remove(tag, size);
		remove_tree_node(aspaceData, base);
		while (KosmDotMapping* m = dot->mappings.RemoveHead())
			object_cache_delete(sMappingCache, m);
		cache->Lock();
		cache->ReleaseRefAndUnlock();
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		free_dot(dot);
		return (status_t)handle;
	}

	aspace->Put();

	*address = (void*)base;

	TRACE("kosm_create_dot_file: id %d, handle %d, base %#lx, size %#lx\n",
		(int)id, (int)handle, base, size);
	return handle;
}


// Sync dirty pages of a file-backed dot back to disk

status_t
kosm_dot_sync_internal(int32 internalId, size_t offset, size_t size)
{
	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	if (!dot->IsFileBacked()) {
		put_dot(dot);
		return B_NOT_ALLOWED;
	}

	offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	size = ROUNDUP(size, B_PAGE_SIZE);
	if (offset + size > dot->size)
		size = dot->size - offset;

	VMCache* cache = dot->cache;
	if (cache == NULL) {
		put_dot(dot);
		return B_BAD_VALUE;
	}

	// Write back dirty pages one at a time. We must unlock the cache
	// during I/O, which invalidates the iterator, so we restart from
	// the target offset range each time.
	off_t syncStart = dot->cache_offset + (off_t)offset;
	off_t syncEnd = syncStart + (off_t)size;

	for (off_t pageOffset = syncStart; pageOffset < syncEnd;
			pageOffset += B_PAGE_SIZE) {
		cache->Lock();
		vm_page* page = cache->LookupPage(pageOffset);
		if (page == NULL || !page->modified || page->busy) {
			cache->Unlock();
			continue;
		}

		// Mark busy to prevent concurrent modification
		page->busy = true;

		phys_addr_t physAddr
			= page->physical_page_number * B_PAGE_SIZE;
		generic_io_vec vec;
		vec.base = physAddr;
		vec.length = B_PAGE_SIZE;
		generic_size_t numBytes = B_PAGE_SIZE;

		cache->Unlock();

		cache->Write(pageOffset, &vec, 1,
			B_PHYSICAL_IO_REQUEST, &numBytes);

		cache->Lock();
		page = cache->LookupPage(pageOffset);
		if (page != NULL) {
			page->modified = false;
			page->busy = false;
			cache->NotifyPageEvents(page, PAGE_EVENT_NOT_BUSY);
		}
		cache->Unlock();
	}

	put_dot(dot);
	return B_OK;
}


// Syscall implementations
//
// All syscalls take per-process handles, not internal IDs.
// Handle resolution + rights checking happens at the syscall boundary.
// Access control is implicit: if you have the handle, you may use it
// (subject to the rights mask on the handle).

kosm_dot_id
_user_kosm_create_dot(const char* userName, void** userAddress,
	uint32 addressSpec, size_t size, uint32 protection,
	uint32 flags, uint32 tag, phys_addr_t physicalAddress)
{
	char name[B_OS_NAME_LENGTH];
	void* address;

	if (userName != NULL) {
		if (!IS_USER_ADDRESS(userName)
			|| user_strlcpy(name, userName, sizeof(name)) < B_OK)
			return B_BAD_ADDRESS;
	} else {
		strlcpy(name, "user_dot", sizeof(name));
	}

	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	// Device dots are kernel-only
	if (flags & KOSM_DOT_DEVICE)
		return B_NOT_ALLOWED;

	team_id team = thread_get_current_thread()->team->id;

	// kosm_create_dot_for returns a handle (> 0) or negative error
	kosm_handle_t handle = kosm_create_dot_for(team, name, &address,
		addressSpec, size, protection, flags, tag, physicalAddress);
	if (handle < B_OK)
		return handle;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
		// Roll back: close the handle (triggers unmap + ref release)
		KosmHandleTable* table = KosmHandleTable::TableForCurrent();
		if (table != NULL) {
			KernelReferenceable* obj;
			if (table->Remove(handle, &obj) == B_OK) {
				KosmDotObject* dotObj
					= static_cast<KosmDotObject*>(obj);
				kosm_dot_close_for_team(dotObj->internal_id, team);
				obj->ReleaseReference();
			}
		}
		return B_BAD_ADDRESS;
	}

	return handle;
}


status_t
_user_kosm_delete_dot(kosm_dot_id handle)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	// Remove handle from table, get the dot object
	KernelReferenceable* object;
	uint16 type;
	status_t status = table->Remove(handle, &object, &type);
	if (status != B_OK)
		return status;
	if (type != KOSM_HANDLE_DOT) {
		object->ReleaseReference();
		return B_BAD_VALUE;
	}

	KosmDotObject* dotObj = static_cast<KosmDotObject*>(object);
	int32 internalId = dotObj->internal_id;
	team_id team = thread_get_current_thread()->team->id;

	// Unmap this team's mapping (if any)
	kosm_dot_close_for_team(internalId, team);

	// Release the handle's reference. If this was the last handle,
	// KosmDotObject destructor triggers kosm_dot_destroy.
	object->ReleaseReference();
	return B_OK;
}


status_t
_user_kosm_map_dot(kosm_dot_id handle, void** userAddress,
	uint32 addressSpec, uint32 protection)
{
	void* address;
	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	// Resolve handle to internal ID (requires READ right)
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_READ,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	team_id team = thread_get_current_thread()->team->id;

	// Protection ceiling: cannot exceed dot's base protection
	uint32 ownerProt = dot->protection;
	if ((protection & ownerProt) != protection) {
		put_dot(dot);
		return B_PERMISSION_DENIED;
	}

	VMAddressSpace* aspace = VMAddressSpace::Get(team);
	if (aspace == NULL) {
		put_dot(dot);
		return B_BAD_TEAM_ID;
	}

	PerAspaceData* aspaceData = get_or_create_per_aspace(team);
	if (aspaceData == NULL) {
		aspace->Put();
		put_dot(dot);
		return B_NO_MEMORY;
	}

	size_t size = dot->size;

	// Reserve address range
	virtual_address_restrictions mapRestrictions = {};
	mapRestrictions.address = address;
	mapRestrictions.address_specification = addressSpec;

	aspace->WriteLock();
	void* reservedAddr = address;
	status = aspace->ReserveAddressRange(size,
		&mapRestrictions, 0, 0, &reservedAddr);
	aspace->WriteUnlock();

	if (status != B_OK) {
		aspace->Put();
		put_dot(dot);
		return status;
	}

	addr_t base = (addr_t)reservedAddr;

	// Create mapping entry
	KosmDotMapping* mapping = new(sMappingCache, 0) KosmDotMapping;
	if (mapping == NULL) {
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		put_dot(dot);
		return B_NO_MEMORY;
	}

	mapping->dot_id = internalId;
	mapping->team = team;
	mapping->base = base;
	mapping->size = size;
	mapping->granted_protection = protection;
	mapping->is_owner = false;

	// Insert tree node
	status = insert_tree_node(aspaceData, dot, team, base, size);
	if (status != B_OK) {
		object_cache_delete(sMappingCache, mapping);
		aspace->WriteLock();
		aspace->UnreserveAddressRange(base, size, 0);
		aspace->WriteUnlock();
		aspace->Put();
		put_dot(dot);
		return status;
	}

	// Map existing pages
	if (dot->cache != NULL) {
		VMCache* cache = dot->cache;
		VMTranslationMap* map = aspace->TranslationMap();

		vm_page_reservation reservation;
		size_t reservePages = map->MaxPagesNeededToMap(base, base + size - 1);
		vm_page_reserve_pages(&reservation, reservePages,
			VM_PRIORITY_USER);

		cache->Lock();
		uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
		map->Lock();

		for (VMCachePagesTree::Iterator pIt = cache->pages.GetIterator();
				vm_page* page = pIt.Next();) {
			addr_t pageAddr = base
				+ ((off_t)page->cache_offset << PAGE_SHIFT);
			if (pageAddr >= base && pageAddr < base + size) {
				map->Map(pageAddr,
					page->physical_page_number * B_PAGE_SIZE,
					haikuProt, dot->MemoryType(), &reservation);
				page->IncrementWiredCount();
			}
		}

		map->Unlock();
		cache->Unlock();
		vm_page_unreserve_pages(&reservation);
	} else if (dot->IsDevice()) {
		// Map device range
		VMTranslationMap* map = aspace->TranslationMap();
		vm_page_reservation reservation;
		size_t reservePages = map->MaxPagesNeededToMap(base, base + size - 1);
		vm_page_reserve_pages(&reservation, reservePages,
			VM_PRIORITY_SYSTEM);

		uint32 haikuProt = KosmDot::ToHaikuProtection(protection);
		map->Lock();
		for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
			map->Map(base + offset, dot->phys_base + offset,
				haikuProt, dot->MemoryType(), &reservation);
		}
		map->Unlock();
		vm_page_unreserve_pages(&reservation);
	}

	// Add mapping to dot (under lock)
	MutexLocker dotLocker(dot->lock);
	dot->mappings.Add(mapping);
	dotLocker.Unlock();

	aspace->Put();

	if (user_memcpy(userAddress, &base, sizeof(base)) < B_OK) {
		// Rollback
		close_dot_mapping_for_team(dot, team);
		put_dot(dot);
		return B_BAD_ADDRESS;
	}

	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_unmap_dot(kosm_dot_id handle)
{
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_READ,
		&internalId);
	if (status != B_OK)
		return status;

	team_id team = thread_get_current_thread()->team->id;
	return kosm_dot_close_for_team(internalId, team);
}


status_t
_user_kosm_protect_dot(kosm_dot_id handle, uint32 newProtection)
{
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_MANAGE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(dot->lock);
	dot->protection = newProtection;

	// Re-protect all mappings
	for (KosmDotMappingList::Iterator it = dot->mappings.GetIterator();
			KosmDotMapping* mapping = it.Next();) {
		VMAddressSpace* aspace = VMAddressSpace::Get(mapping->team);
		if (aspace == NULL)
			continue;

		uint32 mapProt = newProtection;
		if (!mapping->is_owner) {
			// Non-owner mappings: intersection with what was granted
			// at map time. This enforces the protection ceiling —
			// a non-owner mapping granted only READ cannot gain WRITE
			// even if the owner upgrades the dot protection.
			mapProt = newProtection & mapping->granted_protection;
		}

		VMTranslationMap* map = aspace->TranslationMap();
		uint32 haikuProt = KosmDot::ToHaikuProtection(mapProt);
		map->Lock();
		map->Protect(mapping->base, mapping->base + mapping->size - 1,
			haikuProt, dot->MemoryType());
		map->Unlock();
		aspace->Put();
	}

	locker.Unlock();
	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_dot_set_cache_policy(kosm_dot_id handle, uint8 cachePolicy)
{
	if (cachePolicy > KOSM_CACHE_DEVICE)
		return B_BAD_VALUE;

	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_MANAGE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(dot->lock);
	dot->cache_policy = cachePolicy;

	// Translate to Haiku memory type
	uint32 memoryType;
	switch (cachePolicy) {
		case KOSM_CACHE_WRITECOMBINE:	memoryType = B_WRITE_COMBINING_MEMORY; break;
		case KOSM_CACHE_UNCACHED:		memoryType = B_UNCACHED_MEMORY; break;
		case KOSM_CACHE_WRITETHROUGH:	memoryType = B_WRITE_THROUGH_MEMORY; break;
		case KOSM_CACHE_DEVICE:			memoryType = B_UNCACHED_MEMORY; break;
		default:						memoryType = 0; break;
	}
	dot->SetMemoryType(memoryType);

	// Update PTEs for all mappings
	for (KosmDotMappingList::Iterator it = dot->mappings.GetIterator();
			KosmDotMapping* mapping = it.Next();) {
		VMAddressSpace* aspace = VMAddressSpace::Get(mapping->team);
		if (aspace == NULL)
			continue;
		VMTranslationMap* map = aspace->TranslationMap();
		uint32 mapProt = dot->protection;
		if (!mapping->is_owner)
			mapProt = dot->protection & mapping->granted_protection;
		uint32 haikuProt = KosmDot::ToHaikuProtection(mapProt);
		map->Lock();
		map->Protect(mapping->base, mapping->base + mapping->size - 1,
			haikuProt, dot->MemoryType());
		map->Unlock();
		aspace->Put();
	}

	locker.Unlock();
	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_dot_mark_volatile(kosm_dot_id handle, uint8* userOldState)
{
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_MANAGE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	if (!dot->IsPurgeable()) {
		put_dot(dot);
		return KOSM_DOT_NOT_PURGEABLE;
	}

	MutexLocker locker(dot->lock);
	uint8 oldState = dot->purgeable_state;

	if (oldState == KOSM_PURGE_NONVOLATILE)
		dot->purgeable_state = KOSM_PURGE_VOLATILE;

	locker.Unlock();

	if (userOldState != NULL && IS_USER_ADDRESS(userOldState))
		user_memcpy(userOldState, &oldState, sizeof(oldState));

	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_dot_mark_nonvolatile(kosm_dot_id handle, uint8* userOldState)
{
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_MANAGE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	if (!dot->IsPurgeable()) {
		put_dot(dot);
		return KOSM_DOT_NOT_PURGEABLE;
	}

	MutexLocker locker(dot->lock);
	uint8 oldState = dot->purgeable_state;
	status_t result = B_OK;

	if (oldState == KOSM_PURGE_VOLATILE) {
		dot->purgeable_state = KOSM_PURGE_NONVOLATILE;
	} else if (oldState == KOSM_PURGE_EMPTY) {
		// Re-prepare the cache for faulting in fresh pages.
		if (dot->cache != NULL) {
			KosmDotPurgeableCache* purgeCache
				= static_cast<KosmDotPurgeableCache*>(dot->cache);
			purgeCache->Lock();
			status_t commitStatus = purgeCache->Recommit(
				dot->size, VM_PRIORITY_USER);
			purgeCache->Unlock();
			if (commitStatus != B_OK) {
				locker.Unlock();
				put_dot(dot);
				return B_NO_MEMORY;
			}
		}
		dot->purgeable_state = KOSM_PURGE_NONVOLATILE;
		result = KOSM_DOT_WAS_PURGED;
	}

	locker.Unlock();

	if (userOldState != NULL && IS_USER_ADDRESS(userOldState))
		user_memcpy(userOldState, &oldState, sizeof(oldState));

	put_dot(dot);
	return result;
}


status_t
_user_kosm_dot_wire(kosm_dot_id handle, size_t offset, size_t size)
{
	offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	size = ROUNDUP(size, B_PAGE_SIZE);

	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_WRITE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	team_id team = thread_get_current_thread()->team->id;

	if (dot->IsDevice() || offset + size > dot->size) {
		put_dot(dot);
		return B_BAD_VALUE;
	}

	// For lazy dots, force fault-in of all pages in range.
	// Use this team's mapping (the handle holder).
	KosmDotMapping* mapping = dot->FindMapping(team);
	if (mapping == NULL) {
		put_dot(dot);
		return KOSM_DOT_NOT_MAPPED;
	}

	VMAddressSpace* aspace = VMAddressSpace::Get(team);
	if (aspace == NULL) {
		put_dot(dot);
		return B_BAD_TEAM_ID;
	}

	// Fault in each page
	for (size_t off = offset; off < offset + size; off += B_PAGE_SIZE) {
		addr_t addr = mapping->base + off;

		VMTranslationMap* map = aspace->TranslationMap();
		map->Lock();
		phys_addr_t phys;
		uint32 mapFlags;
		bool mapped = (map->Query(addr, &phys, &mapFlags) == B_OK
			&& (mapFlags & PAGE_PRESENT) != 0);
		map->Unlock();

		if (!mapped) {
			status = kosm_dot_soft_fault(dot, mapping->base,
				aspace, addr, false, false, false);
			if (status != B_OK) {
				aspace->Put();
				put_dot(dot);
				return status;
			}
		}
	}

	// Record wired range
	KosmDotWiredRange* range = new(sWiredRangeCache, 0) KosmDotWiredRange;
	if (range == NULL) {
		aspace->Put();
		put_dot(dot);
		return B_NO_MEMORY;
	}

	range->offset = offset;
	range->size = size;
	range->writable = (dot->protection & KOSM_PROT_WRITE) != 0;
	range->wire_count = 1;

	MutexLocker locker(dot->lock);
	dot->wired_ranges.Add(range);
	locker.Unlock();

	aspace->Put();
	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_dot_unwire(kosm_dot_id handle, size_t offset, size_t size)
{
	offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	size = ROUNDUP(size, B_PAGE_SIZE);

	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_WRITE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(dot->lock);

	// Find matching wired range
	KosmDotWiredRange* found = NULL;
	for (KosmDotWiredRangeList::Iterator it = dot->wired_ranges.GetIterator();
			KosmDotWiredRange* r = it.Next();) {
		if (r->offset == offset && r->size == size) {
			found = r;
			break;
		}
	}

	if (found == NULL) {
		locker.Unlock();
		put_dot(dot);
		return B_BAD_VALUE;
	}

	dot->wired_ranges.Remove(found);
	locker.Unlock();

	object_cache_delete(sWiredRangeCache, found);

	dot->condition.NotifyAll();

	put_dot(dot);
	return B_OK;
}


status_t
_user_kosm_dot_get_phys(kosm_dot_id handle, size_t offset,
	phys_addr_t* userPhysicalAddress)
{
	if (!IS_USER_ADDRESS(userPhysicalAddress))
		return B_BAD_ADDRESS;

	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_WRITE,
		&internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	team_id team = thread_get_current_thread()->team->id;

	if (offset >= dot->size) {
		put_dot(dot);
		return B_BAD_VALUE;
	}

	phys_addr_t phys;

	if (dot->IsDevice()) {
		phys = dot->phys_base + offset;
	} else if (dot->wiring == B_CONTIGUOUS) {
		if (dot->cache == NULL) {
			put_dot(dot);
			return KOSM_DOT_NOT_WIRED;
		}
		VMCache* cache = dot->cache;
		cache->Lock();
		vm_page* firstPage = cache->LookupPage(0);
		if (firstPage == NULL) {
			cache->Unlock();
			put_dot(dot);
			return KOSM_DOT_NOT_WIRED;
		}
		phys = firstPage->physical_page_number * B_PAGE_SIZE + offset;
		cache->Unlock();
	} else {
		// Check if the page at this offset is wired
		bool isWired = false;
		MutexLocker locker(dot->lock);
		for (KosmDotWiredRangeList::Iterator it
				= dot->wired_ranges.GetIterator();
				KosmDotWiredRange* r = it.Next();) {
			if (offset >= r->offset && offset < r->offset + r->size) {
				isWired = true;
				break;
			}
		}
		if (dot->wiring == B_FULL_LOCK)
			isWired = true;
		locker.Unlock();

		if (!isWired) {
			put_dot(dot);
			return KOSM_DOT_NOT_WIRED;
		}

		// Look up via this team's mapping
		KosmDotMapping* mapping = dot->FindMapping(team);
		if (mapping == NULL) {
			put_dot(dot);
			return KOSM_DOT_NOT_MAPPED;
		}

		VMAddressSpace* aspace = VMAddressSpace::Get(team);
		if (aspace == NULL) {
			put_dot(dot);
			return B_BAD_TEAM_ID;
		}

		VMTranslationMap* map = aspace->TranslationMap();
		uint32 flags;
		map->Lock();
		status = map->Query(mapping->base + offset, &phys, &flags);
		map->Unlock();
		aspace->Put();

		if (status != B_OK || !(flags & PAGE_PRESENT)) {
			put_dot(dot);
			return KOSM_DOT_WOULD_FAULT;
		}
	}

	put_dot(dot);

	return user_memcpy(userPhysicalAddress, &phys, sizeof(phys));
}


status_t
_user_kosm_get_dot_info(kosm_dot_id handle, kosm_dot_info* userInfo,
	size_t size)
{
	if (!IS_USER_ADDRESS(userInfo) || size < sizeof(kosm_dot_info))
		return B_BAD_ADDRESS;

	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, 0, &internalId);
	if (status != B_OK)
		return status;

	KosmDot* dot = lookup_dot(internalId);
	if (dot == NULL)
		return B_BAD_VALUE;

	team_id team = thread_get_current_thread()->team->id;

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));

	// Return the handle value, not the internal ID
	info.kosm_dot = handle;
	info.team = dot->owner_team;
	strlcpy(info.name, dot->name, B_OS_NAME_LENGTH);

	// Show this team's mapping address (not the owner's)
	KosmDotMapping* mapping = dot->FindMapping(team);
	info.address = mapping ? (void*)mapping->base : NULL;
	info.size = dot->size;
	info.protection = dot->protection;
	info.flags = dot->flags;
	info.cache_policy = dot->cache_policy;
	info.purgeable_state = dot->purgeable_state;
	info.tag = dot->tag;
	info.phys_base = dot->phys_base;

	if (dot->cache != NULL) {
		info.resident_size = dot->cache->page_count * B_PAGE_SIZE;
	}

	put_dot(dot);

	return user_memcpy(userInfo, &info, sizeof(info));
}


status_t
_user_kosm_get_next_dot_info(team_id team, int32* userCookie,
	kosm_dot_info* userInfo, size_t size)
{
	if (!IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| size < sizeof(kosm_dot_info))
		return B_BAD_ADDRESS;

	int32 cookie;
	if (user_memcpy(&cookie, userCookie, sizeof(cookie)) < B_OK)
		return B_BAD_ADDRESS;

	// Iterate through dots owned by this team, starting after cookie.
	// Note: cookie uses internal IDs (opaque to userspace in this
	// iteration context -- they only need to be monotonically increasing).
	rw_lock_read_lock(&sDotTableLock);

	KosmDot* found = NULL;
	KosmDotHashTable::Iterator it = sDotTable.GetIterator();
	while (KosmDot* dot = it.Next()) {
		if (dot->owner_team != team || !dot->IsActive())
			continue;
		if (dot->id > cookie) {
			if (found == NULL || dot->id < found->id)
				found = dot;
		}
	}

	if (found != NULL)
		found->AcquireRef();

	rw_lock_read_unlock(&sDotTableLock);

	if (found == NULL)
		return B_ENTRY_NOT_FOUND;

	kosm_dot_info info;
	memset(&info, 0, sizeof(info));

	// For iteration, kosm_dot field stores internal ID as cookie
	info.kosm_dot = found->id;
	info.team = found->owner_team;
	strlcpy(info.name, found->name, B_OS_NAME_LENGTH);

	KosmDotMapping* mapping = found->FindMapping(found->owner_team);
	info.address = mapping ? (void*)mapping->base : NULL;
	info.size = found->size;
	info.protection = found->protection;
	info.flags = found->flags;
	info.cache_policy = found->cache_policy;
	info.purgeable_state = found->purgeable_state;
	info.tag = found->tag;
	info.phys_base = found->phys_base;

	if (found->cache != NULL)
		info.resident_size = found->cache->page_count * B_PAGE_SIZE;

	int32 newCookie = found->id;

	put_dot(found);

	status_t status = user_memcpy(userInfo, &info, sizeof(info));
	if (status != B_OK)
		return status;

	return user_memcpy(userCookie, &newCookie, sizeof(newCookie));
}


kosm_dot_id
_user_kosm_create_dot_file(int fd, off_t fileOffset,
	const char* userName, void** userAddress,
	uint32 addressSpec, size_t size, uint32 protection,
	uint32 flags, uint32 tag)
{
	char name[B_OS_NAME_LENGTH];
	void* address;

	if (userName != NULL) {
		if (!IS_USER_ADDRESS(userName)
			|| user_strlcpy(name, userName, sizeof(name)) < B_OK)
			return B_BAD_ADDRESS;
	} else {
		strlcpy(name, "user_file_dot", sizeof(name));
	}

	if (!IS_USER_ADDRESS(userAddress)
		|| user_memcpy(&address, userAddress, sizeof(address)) < B_OK)
		return B_BAD_ADDRESS;

	// Device dots are kernel-only; file dots are user-facing
	if (flags & KOSM_DOT_DEVICE)
		return B_NOT_ALLOWED;

	team_id team = thread_get_current_thread()->team->id;

	kosm_handle_t handle = kosm_create_dot_file_for(team, fd, fileOffset,
		name, &address, addressSpec, size, protection, flags, tag);
	if (handle < B_OK)
		return handle;

	if (user_memcpy(userAddress, &address, sizeof(address)) < B_OK) {
		KosmHandleTable* table = KosmHandleTable::TableForCurrent();
		if (table != NULL) {
			KernelReferenceable* obj;
			if (table->Remove(handle, &obj) == B_OK) {
				KosmDotObject* dotObj
					= static_cast<KosmDotObject*>(obj);
				kosm_dot_close_for_team(dotObj->internal_id, team);
				obj->ReleaseReference();
			}
		}
		return B_BAD_ADDRESS;
	}

	return handle;
}


status_t
_user_kosm_dot_sync(kosm_dot_id handle, size_t offset, size_t size)
{
	int32 internalId;
	status_t status = kosm_dot_resolve_handle(handle, KOSM_RIGHT_WRITE,
		&internalId);
	if (status != B_OK)
		return status;

	return kosm_dot_sync_internal(internalId, offset, size);
}
