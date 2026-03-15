/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmDotPurgeableCache — VMCache subclass for purgeable kosm_dots.
 *
 * Extends VMAnonymousNoSwapCache with the ability to discard all pages
 * under memory pressure and recommit when the dot transitions back to
 * nonvolatile. No swap on mobile — pages are simply discarded, and
 * re-faulted as zeroed pages when accessed again.
 *
 * Lifecycle:
 *
 *   NONVOLATILE  ── mark_volatile() ──►  VOLATILE
 *        ▲                                    │
 *        │                            (system purges under
 *   mark_nonvolatile()                 memory pressure)
 *   + Recommit()                              │
 *        │                                    ▼
 *        └────────────────────────────    EMPTY
 *         returns KOSM_DOT_WAS_PURGED   (committed_size = 0,
 *         (app must refill data)         all pages freed)
 *
 * The purgeable_state is stored on KosmDot, not here.
 * This cache provides the mechanism; KosmDot drives the policy.
 *
 * Page tracking:
 *   All kosm_dot pages use wired count (PAGE_STATE_WIRED), keeping
 *   them invisible to the page daemon. The page daemon never touches
 *   our pages — we manage lifecycle ourselves via Purge().
 *
 * Memory accounting:
 *   Inherits VMAnonymousNoSwapCache::Commit() which calls
 *   vm_try_reserve_memory() / vm_unreserve_memory().
 *   Purge() releases commitment. Recommit() re-acquires it.
 */
#ifndef _KERNEL_VM_KOSM_DOT_PURGEABLE_CACHE_H
#define _KERNEL_VM_KOSM_DOT_PURGEABLE_CACHE_H

#include "VMAnonymousNoSwapCache.h"

struct KosmDot;


class KosmDotPurgeableCache final : public VMAnonymousNoSwapCache {
public:
	virtual						~KosmDotPurgeableCache();

			status_t			Init(uint32 allocationFlags);

	/*
	 * Purge: discard all pages and release commitment.
	 *
	 * The caller (kosm_dot.cpp) is responsible for:
	 *   1. Unmapping pages from all mappings (via translation map)
	 *   2. Decrementing wired count on each page
	 *   3. Holding the kosm_dot lock
	 *
	 * This method then:
	 *   1. Removes all pages from the cache
	 *   2. Frees the pages
	 *   3. Releases commitment (vm_unreserve_memory)
	 *
	 * Cache must be locked. Returns number of pages freed.
	 */
			size_t				Purge();

	/*
	 * Recommit memory after a purge.
	 *
	 * Called when mark_nonvolatile() transitions from EMPTY.
	 * Reserves memory for future page faults, but does not
	 * allocate pages. Pages are faulted in on access.
	 *
	 * Cache must be locked.
	 * Returns B_OK on success, B_NO_MEMORY if reservation fails.
	 */
			status_t			Recommit(off_t size, int priority);

	/*
	 * Owning KosmDot, set once after creation.
	 * Used for tag accounting during purge.
	 */
			void				SetDot(KosmDot* dot)	{ fDot = dot; }
			KosmDot*			Dot() const				{ return fDot; }

	// VMCache overrides

	virtual	bool				StoreHasPage(off_t offset) override;

	virtual	status_t			Read(off_t offset, const generic_io_vec* vecs,
									size_t count, uint32 flags,
									generic_size_t* _numBytes) override;

	virtual	bool				CanWritePage(off_t offset) override;

	virtual	status_t			Fault(struct VMAddressSpace* aspace,
									off_t offset) override;

protected:
	virtual	void				DeleteObject() override;

private:
			KosmDot*			fDot;
};


extern ObjectCache* gKosmDotPurgeableCacheObjectCache;

status_t kosm_dot_purgeable_cache_init();


#endif
