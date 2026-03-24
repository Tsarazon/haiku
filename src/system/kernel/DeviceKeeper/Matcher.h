/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_MATCHER_H
#define DEVICE_KEEPER_MATCHER_H

#include <device_keeper.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>


class DkNode;
class DkPropertyStore;


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

private:
	static uint32		_BucketFor(const char* busName);
	static const char*	_ExtractBusName(const dk_match_rule* rules);

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

	// find best matching driver for a node (indexed by bus type)
	status_t			FindBestDriver(DkNode* node,
							dk_driver_info** _driver);

	// find all matching drivers (for KOSM_FIND_MULTIPLE_CHILDREN)
	status_t			FindAllDrivers(DkNode* node,
							DkMatchResult* results, int32 maxResults,
							int32* _count);

	static int32		_ComputeSpecificity(const dk_match_rule* rules,
							const DkPropertyStore& store);

private:
	static int32		_CountMatchRules(const dk_match_rule* rules);

	typedef DoublyLinkedList<DkDriverRecord> DriverList;
	rw_lock				fLock;
	DriverList			fDrivers;
	DkDriverIndex		fIndex;
};


#endif // DEVICE_KEEPER_MATCHER_H
