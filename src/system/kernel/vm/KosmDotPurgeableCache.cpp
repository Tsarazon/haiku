/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmDotPurgeableCache — purgeable VMCache for kosm_dot.
 *
 * This cache backs purgeable kosm_dots (KOSM_DOT_PURGEABLE flag).
 * Under memory pressure the system can discard all pages via Purge(),
 * transitioning the owning kosm_dot to KOSM_PURGE_EMPTY state.
 * When the app calls mark_nonvolatile(), Recommit() re-reserves
 * memory for future page faults. The app is responsible for refilling
 * the data — faulted pages come in zeroed.
 *
 * No swap. No backing store. No page daemon interaction.
 * Pages use wired count (PAGE_STATE_WIRED) and are managed entirely
 * by kosm_dot.cpp and this cache.
 *
 * Integration with low_resource_handler:
 *   kosm_dot.cpp registers a low_resource_handler that calls
 *   kosm_dot_purge_volatile(). That function iterates volatile
 *   purgeable dots, unmaps pages via translation map, decrements
 *   wired counts, then calls Purge() on this cache to free the
 *   pages and release commitment. This separation keeps unmap
 *   logic (which needs KosmDot and mapping info) in kosm_dot.cpp
 *   and cache-level page management here.
 */

#include "KosmDotPurgeableCache.h"

#include <new>

#include <KernelExport.h>

#include <heap.h>
#include <slab/Slab.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>

#include <vm/kosm_dot.h>


//#define TRACE_PURGEABLE
#ifdef TRACE_PURGEABLE
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) do {} while (false)
#endif


ObjectCache* gKosmDotPurgeableCacheObjectCache;


// Called once during kernel init (from kosm_dot_init)
status_t
kosm_dot_purgeable_cache_init()
{
	gKosmDotPurgeableCacheObjectCache = create_object_cache(
		"kosm_dot purgeable caches",
		sizeof(KosmDotPurgeableCache), 0);
	if (gKosmDotPurgeableCacheObjectCache == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


KosmDotPurgeableCache::~KosmDotPurgeableCache()
{
	// Parent destructor handles vm_unreserve_memory(committed_size)
}


status_t
KosmDotPurgeableCache::Init(uint32 allocationFlags)
{
	TRACE("KosmDotPurgeableCache::Init()\n");

	fDot = NULL;

	// Initialize as a non-overcommitting, no-guard, no-precommit cache.
	// kosm_dot.cpp handles commitment explicitly via Commit() calls.
	// canOvercommit = false: we track every byte.
	// numPrecommittedPages = 0: lazy faulting, pages committed on demand.
	// numGuardPages = 0: guard pages are handled by kosm_dot, not the cache.
	status_t error = VMAnonymousNoSwapCache::Init(
		false,	// canOvercommit
		0,		// numPrecommittedPages
		0,		// numGuardPages
		allocationFlags);

	return error;
}


/*
 * Purge all pages from the cache and release commitment.
 *
 * Preconditions (enforced by caller in kosm_dot.cpp):
 *   - Cache is locked
 *   - All pages have been unmapped from all address spaces
 *   - Wired count on each page has been decremented to account
 *     for the removed mappings
 *
 * What we do:
 *   1. Remove each page from the cache
 *   2. Free each page back to the page allocator
 *   3. Release all committed memory reservation
 *
 * Pages may still have wired_count > 0 if DMA/GPU wire ranges
 * exist (kosm_dot_wire). We don't purge those — the caller
 * should skip dots with active wire ranges. If we encounter
 * a page with wired_count > 0 here, we leave it alone and
 * don't count it as freed. This is a safety measure.
 */
size_t
KosmDotPurgeableCache::Purge()
{
	AssertLocked();

	TRACE("KosmDotPurgeableCache::Purge(): cache %p, %u pages\n",
		this, page_count);

	size_t freedCount = 0;
	vm_page_reservation reservation = {};

	VMCachePagesTree::Iterator it = pages.GetIterator();
	while (vm_page* page = it.Next()) {
		// Safety: skip pages that are still wired (DMA in progress)
		if (page->WiredCount() > 0) {
			TRACE("KosmDotPurgeableCache::Purge(): skipping wired page "
				"%p (wired_count %u)\n", page, page->WiredCount());
			continue;
		}

		if (page->busy) {
			// Page is busy (being faulted in). This shouldn't happen
			// if the caller properly prevented concurrent faults, but
			// handle it gracefully by skipping.
			TRACE("KosmDotPurgeableCache::Purge(): skipping busy page "
				"%p\n", page);
			continue;
		}

		DEBUG_PAGE_ACCESS_START(page);

		// Remove from cache. Safe during IteratableSplayTree iteration.
		RemovePage(page);

		// Free the page
		vm_page_free_etc(this, page, &reservation);

		freedCount++;
	}

	// Release any page reservation surplus
	vm_page_unreserve_pages(&reservation);

	// Release all committed memory back to the system.
	// vm_unreserve_memory() is called by the Commit path when
	// shrinking, so we go through Commit(0).
	if (committed_size > 0) {
		// Direct release: we know there are no pages left (or only
		// wired stragglers), so we can safely drop to the page_count
		// worth of commitment (for any remaining wired pages).
		off_t remainingCommitment = (off_t)page_count * B_PAGE_SIZE;
		if (remainingCommitment < committed_size) {
			TRACE("KosmDotPurgeableCache::Purge(): commitment "
				"%" B_PRIdOFF " -> %" B_PRIdOFF "\n",
				committed_size, remainingCommitment);
			vm_unreserve_memory(committed_size - remainingCommitment);
			committed_size = remainingCommitment;
		}
	}

	TRACE("KosmDotPurgeableCache::Purge(): freed %lu pages\n", freedCount);
	return freedCount;
}


/*
 * Recommit memory after a purge.
 *
 * When a purgeable kosm_dot transitions from EMPTY back to NONVOLATILE,
 * we need to re-reserve memory so that future page faults can allocate
 * pages. This doesn't allocate any pages — it just reserves the right
 * to allocate them later.
 *
 * The size parameter is the full dot size. We commit up to that amount,
 * minus any existing commitment (for wired stragglers that survived
 * the purge).
 */
status_t
KosmDotPurgeableCache::Recommit(off_t size, int priority)
{
	AssertLocked();

	TRACE("KosmDotPurgeableCache::Recommit(%" B_PRIdOFF ", priority %d), "
		"current commitment %" B_PRIdOFF "\n", size, priority, committed_size);

	if (size <= committed_size)
		return B_OK;

	off_t needed = size - committed_size;
	status_t status = vm_try_reserve_memory(needed, priority, 1000000);
	if (status != B_OK) {
		TRACE("KosmDotPurgeableCache::Recommit(): failed to reserve "
			"%" B_PRIdOFF " bytes\n", needed);
		return B_NO_MEMORY;
	}

	committed_size = size;

	// Update virtual_end if needed (it may have been left at 0
	// after purge if the cache was freshly created with no pages)
	if (virtual_end < size)
		virtual_end = size;

	return B_OK;
}


bool
KosmDotPurgeableCache::StoreHasPage(off_t offset)
{
	// Purgeable caches have no backing store. Period.
	// If a page isn't in the cache's page tree, it doesn't exist.
	// The fault handler will allocate a fresh zeroed page.
	return false;
}


status_t
KosmDotPurgeableCache::Read(off_t offset, const generic_io_vec* vecs,
	size_t count, uint32 flags, generic_size_t* _numBytes)
{
	// This should never be called. StoreHasPage() returns false,
	// so the fault path never attempts to read from a backing store.
	// If we end up here, something is seriously wrong.
	panic("KosmDotPurgeableCache::Read() called — no backing store exists");
	return B_ERROR;
}


bool
KosmDotPurgeableCache::CanWritePage(off_t offset)
{
	// kosm_dot pages are wired (PAGE_STATE_WIRED) and should never
	// reach the modified queue or trigger page-out. If the page daemon
	// somehow asks about our pages, refuse. We manage our own lifecycle.
	return false;
}


/*
 * Fault handler override.
 *
 * NOTE: This method is currently UNUSED. kosm_dot uses its own fault
 * path (kosm_dot_soft_fault) which is invoked from kosm_dot_fault(),
 * not from vm_soft_fault(). The standard vm_soft_fault → cache->Fault()
 * path is never reached for kosm_dot pages.
 *
 * Kept as a safety net in case kosm_dot pages are ever routed through
 * the standard Haiku fault machinery. If that happens, this provides
 * commitment logic for purgeable caches in the EMPTY state.
 */
status_t
KosmDotPurgeableCache::Fault(struct VMAddressSpace* aspace, off_t offset)
{
	// If we have no commitment at all (purged state, app didn't recommit),
	// try to commit enough for one page so the fault can proceed.
	if (committed_size == 0) {
		int priority = (aspace == VMAddressSpace::Kernel())
			? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER;
		status_t status = vm_try_reserve_memory(B_PAGE_SIZE, priority, 0);
		if (status != B_OK) {
			dprintf("KosmDotPurgeableCache::Fault(): no memory to "
				"recommit after purge (offset %" B_PRIdOFF ")\n", offset);
			return B_NO_MEMORY;
		}
		committed_size += B_PAGE_SIZE;
	} else if (committed_size <= (off_t)page_count * B_PAGE_SIZE) {
		// All committed memory is used by existing pages.
		// Try to commit one more page.
		int priority = (aspace == VMAddressSpace::Kernel())
			? VM_PRIORITY_SYSTEM : VM_PRIORITY_USER;
		status_t status = vm_try_reserve_memory(B_PAGE_SIZE, priority, 0);
		if (status != B_OK)
			return B_NO_MEMORY;
		committed_size += B_PAGE_SIZE;
	}

	// Tell the caller to proceed with page allocation.
	return B_BAD_HANDLER;
}


void
KosmDotPurgeableCache::DeleteObject()
{
	object_cache_delete(gKosmDotPurgeableCacheObjectCache, this);
}
