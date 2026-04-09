/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_MATCHER_H
#define DEVICE_KEEPER_MATCHER_H

#include <device_keeper.h>
#include <lock.h>
#include <module.h>
#include <util/DoublyLinkedList.h>


class DkNode;
class DkPropertyStore;


// DkModuleRef: move-style RAII wrapper for a dk_driver_info* acquired
// via get_module(). Releases the reference on destruction unless the
// caller explicitly calls Detach() to transfer ownership (e.g. to a
// node that will put_module() on detach).
//
// Not copyable. Assignment is also suppressed so that accidental
// double-put bugs are caught at compile time. Transfer ownership via
// an explicit sink signature: either store the pointer in a node via
// Detach(), or call Reset() to release immediately.
class DkModuleRef {
public:
						DkModuleRef() : fInfo(NULL) {}
	explicit			DkModuleRef(dk_driver_info* info) : fInfo(info) {}
						~DkModuleRef()
						{
							if (fInfo != NULL)
								put_module(fInfo->info.name);
						}

	bool				IsSet() const { return fInfo != NULL; }
	dk_driver_info*		Get() const { return fInfo; }
	dk_driver_info*		operator->() const { return fInfo; }

	// Transfer ownership. Caller becomes responsible for put_module().
	dk_driver_info*		Detach()
						{
							dk_driver_info* r = fInfo;
							fInfo = NULL;
							return r;
						}

	// Release current reference (if any) and take ownership of info.
	void				Reset(dk_driver_info* info = NULL)
						{
							if (fInfo != NULL && fInfo != info)
								put_module(fInfo->info.name);
							fInfo = info;
						}

private:
	dk_driver_info*		fInfo;

	// Not copyable, not assignable.
						DkModuleRef(const DkModuleRef&);
	DkModuleRef&		operator=(const DkModuleRef&);
};


struct DkDriverRecord : DoublyLinkedListLinkImpl<DkDriverRecord> {
	const char*			module_name;
	dk_driver_info*		info;
	bool				loaded;
	DkDriverRecord*		index_next;		// per-bus index chain
	uint32				index_bucket;	// bucket in DkDriverIndex
};


struct DkMatchResult {
	dk_driver_info*		driver;
	int32				specificity;
	int32				priority;
	float				probe_score;
};


// Bus-type index: hashes KOSM_DEVICE_BUS match value to narrow
// candidate set in FindBestDriver from O(N_total) to O(N_bus).
// Bucket 0 is reserved for wildcard drivers whose first match
// rule is not KOSM_DEVICE_BUS (always checked).
class DkDriverIndex {
public:
	static const uint32	kBucketCount = 32;
	static const uint32	kWildcardBucket = 0;

						DkDriverIndex();

	void				Insert(DkDriverRecord* record);
	void				Remove(DkDriverRecord* record);

	// iterate candidates: call visitor(record) for each record
	// in the bus-specific bucket and the wildcard bucket.
	template<typename Visitor>
	void				ForEachCandidate(const char* busName,
							Visitor& visitor) const;

	static const char*	_ExtractBusName(const dk_match_rule* rules);

private:
	static uint32		_BucketFor(const char* busName);

	DkDriverRecord*		fBuckets[kBucketCount];
};


template<typename Visitor>
void
DkDriverIndex::ForEachCandidate(const char* busName, Visitor& visitor) const
{
	// always check wildcard bucket (drivers without bus-specific match)
	for (DkDriverRecord* r = fBuckets[kWildcardBucket]; r != NULL;
		r = r->index_next) {
		visitor(r);
	}

	// check bus-specific bucket if applicable
	uint32 bucket = _BucketFor(busName);
	if (bucket != kWildcardBucket) {
		for (DkDriverRecord* r = fBuckets[bucket]; r != NULL;
			r = r->index_next) {
			visitor(r);
		}
	}
}


class DkMatcher {
public:
						DkMatcher();
						~DkMatcher();

	// driver registration (called when module is discovered/loaded)
	status_t			RegisterDriver(const char* moduleName);
	void				UnregisterDriver(const char* moduleName);

	// scan module directories for dk_driver_info modules
	// path example: "drivers/dev", "busses/pci"
	status_t			DiscoverDrivers(const char* path);

	// find best matching driver for a node (indexed by bus type).
	// On success, _ref owns a module reference that must be transferred
	// to the node via SetDriver (via Detach()) or released (via dtor).
	status_t			FindBestDriver(DkNode* node,
							DkModuleRef& _ref);

	// find all matching drivers (for KOSM_FIND_MULTIPLE_CHILDREN).
	// Each DkMatchResult entry owns a module reference that caller
	// must release via put_module or transfer to a node.
	status_t			FindAllDrivers(DkNode* node,
							DkMatchResult* results, int32 maxResults,
							int32* _count);

	// Collect unique bus names from match rules of drivers whose
	// module name starts with the given prefix. Used by DemandProbe
	// to narrow tree traversal to relevant bus types.
	// busNames: caller-provided array, filled with pointers to
	// internal strings (valid under read lock only).
	// Returns number of unique bus names found.
	int32				CollectBusNames(const char* modulePrefix,
							const char** busNames, int32 maxNames);

	// Debugger dump: print the driver registry to kernel console.
	// Safe to call from KDL context (no mutex — KDL is single-thread).
	void				DumpRegistry() const;

	static int32		_ComputeSpecificity(const dk_match_rule* rules,
							const DkPropertyStore& store);

private:
	// Lock-free internal registration (caller must hold write lock)
	status_t			_RegisterDriverLocked(const char* moduleName);

	// Single-phase fallback when heap allocation fails
	status_t			_DiscoverDriversSinglePhase(const char* path);

	static int32		_CountMatchRules(const dk_match_rule* rules);

	typedef DoublyLinkedList<DkDriverRecord> DriverList;
	rw_lock				fLock;
	DriverList			fDrivers;
	DkDriverIndex		fIndex;
};


#endif // DEVICE_KEEPER_MATCHER_H
