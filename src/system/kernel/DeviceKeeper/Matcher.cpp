/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Matcher.h"
#include "Node.h"

#include <new>
#include <string.h>

#include <KernelExport.h>
#include <module.h>
#include <util/AutoLock.h>


// DkDriverIndex

DkDriverIndex::DkDriverIndex()
{
	memset(fBuckets, 0, sizeof(fBuckets));
}


/*static*/ const char*
DkDriverIndex::_ExtractBusName(const dk_match_rule* rules)
{
	if (rules == NULL)
		return NULL;

	for (; rules->name != NULL; rules++) {
		if (strcmp(rules->name, KOSM_DEVICE_BUS) == 0
			&& rules->type == B_STRING_TYPE) {
			return rules->value.string;
		}
	}
	return NULL;
}


/*static*/ uint32
DkDriverIndex::_BucketFor(const char* busName)
{
	if (busName == NULL)
		return kWildcardBucket;

	// djb2 hash
	uint32 hash = 5381;
	for (const char* p = busName; *p != '\0'; p++)
		hash = hash * 33 + (uint8)*p;

	// map to [1, kBucketCount-1]; bucket 0 is reserved for wildcard
	return (hash % (kBucketCount - 1)) + 1;
}


void
DkDriverIndex::Insert(DkDriverRecord* record)
{
	const char* busName = NULL;
	if (record->info != NULL && record->info->match != NULL)
		busName = _ExtractBusName(record->info->match->rules);

	uint32 bucket = _BucketFor(busName);
	record->index_bucket = bucket;
	record->index_next = fBuckets[bucket];
	fBuckets[bucket] = record;
}


void
DkDriverIndex::Remove(DkDriverRecord* record)
{
	uint32 bucket = record->index_bucket;

	DkDriverRecord** pp = &fBuckets[bucket];
	while (*pp != NULL) {
		if (*pp == record) {
			*pp = record->index_next;
			record->index_next = NULL;
			return;
		}
		pp = &(*pp)->index_next;
	}
}


// DkMatcher

DkMatcher::DkMatcher()
{
	rw_lock_init(&fLock, "dk matcher");
}


DkMatcher::~DkMatcher()
{
	while (DkDriverRecord* record = fDrivers.RemoveHead()) {
		if (record->loaded && record->info != NULL)
			put_module(record->module_name);
		delete record;
	}
	rw_lock_destroy(&fLock);
}


status_t
DkMatcher::RegisterDriver(const char* moduleName)
{
	WriteLocker locker(&fLock);
	return _RegisterDriverLocked(moduleName);
}


status_t
DkMatcher::_RegisterDriverLocked(const char* moduleName)
{
	// caller must hold fLock for write

	// check for duplicates
	DriverList::Iterator it = fDrivers.GetIterator();
	while (it.HasNext()) {
		DkDriverRecord* record = it.Next();
		if (strcmp(record->module_name, moduleName) == 0)
			return B_NAME_IN_USE;
	}

	dk_driver_info* info;
	status_t status = get_module(moduleName, (module_info**)&info);
	if (status != B_OK)
		return status;

	DkDriverRecord* record = new(std::nothrow) DkDriverRecord();
	if (record == NULL) {
		put_module(moduleName);
		return B_NO_MEMORY;
	}

	record->module_name = info->info.name;
	record->info = info;
	record->loaded = true;
	record->index_next = NULL;
	record->index_bucket = 0;

	fDrivers.Add(record);
	fIndex.Insert(record);
	return B_OK;
}


void
DkMatcher::UnregisterDriver(const char* moduleName)
{
	WriteLocker locker(&fLock);

	DriverList::Iterator it = fDrivers.GetIterator();
	while (it.HasNext()) {
		DkDriverRecord* record = it.Next();
		if (strcmp(record->module_name, moduleName) == 0) {
			it.Remove();
			fIndex.Remove(record);
			if (record->loaded)
				put_module(record->module_name);
			delete record;
			return;
		}
	}
}


status_t
DkMatcher::DiscoverDrivers(const char* path)
{
	dprintf("DeviceKeeper: DiscoverDrivers(\"%s\", suffix=\"%s\")\n",
		path, DK_DRIVER_MODULE_SUFFIX);

	void* list = open_module_list_etc(path, DK_DRIVER_MODULE_SUFFIX);
	if (list == NULL) {
		dprintf("DeviceKeeper: open_module_list_etc returned NULL\n");
		return B_ENTRY_NOT_FOUND;
	}

	char name[B_FILE_NAME_LENGTH];
	size_t nameLength;
	int32 found = 0;
	int32 scanned = 0;

	// NOTE: write lock is held for the entire scan, including
	// get_module() calls inside _RegisterDriverLocked() which may
	// perform disk I/O. This blocks all concurrent FindBestDriver()
	// calls. Acceptable during boot (sequential), but DemandProbe
	// paths should be aware of the latency.
	// TODO: two-phase scan — collect names under lock, load modules
	// without lock, insert under write lock — to reduce contention.
	WriteLocker locker(&fLock);

	while (true) {
		nameLength = sizeof(name);
		if (read_next_module_name(list, name, &nameLength) != B_OK)
			break;

		scanned++;
		dprintf("DeviceKeeper:   candidate: \"%s\"\n", name);

		// skip already registered
		bool exists = false;
		DriverList::Iterator it = fDrivers.GetIterator();
		while (it.HasNext()) {
			DkDriverRecord* record = it.Next();
			if (strcmp(record->module_name, name) == 0) {
				exists = true;
				break;
			}
		}
		if (exists) {
			dprintf("DeviceKeeper:   -> already registered\n");
			continue;
		}

		status_t status = _RegisterDriverLocked(name);
		if (status == B_OK) {
			found++;
			dprintf("DeviceKeeper:   -> registered\n");
		} else {
			dprintf("DeviceKeeper:   -> RegisterDriver failed: %s\n",
				strerror(status));
		}
	}

	close_module_list(list);
	dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): scanned %d, registered %d\n",
		path, (int)scanned, (int)found);
	return found > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


status_t
DkMatcher::FindBestDriver(DkNode* node, dk_driver_info** _driver)
{
	// NOTE: probe() callbacks are invoked under fLock read lock.
	// Drivers must not block on I/O or acquire locks that could
	// contend with RegisterDriver/UnregisterDriver (write lock).
	// probe() should only inspect node properties and return a score.
	ReadLocker locker(&fLock);

	dk_driver_info* bestDriver = NULL;
	int32 bestSpecificity = -1;
	int32 bestPriority = -1;
	float bestProbe = 0.0f;

	struct BestVisitor {
		DkNode*			node;
		dk_driver_info**	bestDriver;
		int32*			bestSpecificity;
		int32*			bestPriority;
		float*			bestProbe;

		void operator()(DkDriverRecord* record)
		{
			dk_driver_info* info = record->info;

			// Driver without attach() cannot be used — skip early
			// to avoid selecting a match-only stub as best driver.
			if (info->attach == NULL) {
				dprintf("DeviceKeeper: FindBest: skipping %s "
					"(attach=NULL)\n", info->info.name);
				return;
			}

			// If match dict present, check declarative rules first
			if (info->match != NULL) {
				const dk_match_rule* rules = info->match->rules;
				if (!node->Properties().Matches(rules))
					return;
			}

			int32 specificity = (info->match != NULL)
				? DkMatcher::_ComputeSpecificity(info->match->rules,
					node->Properties())
				: 0;
			int32 priority = (info->match != NULL)
				? info->match->priority : 0;
			float probeScore = 0.0f;

			if (info->probe != NULL) {
				probeScore = info->probe(
					reinterpret_cast<dk_node*>(node));
				if (probeScore <= 0.0f)
					return;
			} else if (info->match == NULL) {
				return;
			}

			bool better = false;
			if (*bestDriver == NULL)
				better = true;
			else if (specificity > *bestSpecificity)
				better = true;
			else if (specificity == *bestSpecificity
				&& priority > *bestPriority)
				better = true;
			else if (specificity == *bestSpecificity
				&& priority == *bestPriority
				&& probeScore > *bestProbe)
				better = true;

			if (better) {
				*bestDriver = info;
				*bestSpecificity = specificity;
				*bestPriority = priority;
				*bestProbe = probeScore;
			}
		}
	};

	BestVisitor visitor = {node, &bestDriver, &bestSpecificity,
		&bestPriority, &bestProbe};

	const char* busName = NULL;
	node->Properties().GetString(KOSM_DEVICE_BUS, &busName);
	fIndex.ForEachCandidate(busName, visitor);

	if (bestDriver == NULL)
		return B_ENTRY_NOT_FOUND;

	dprintf("DeviceKeeper: FindBestDriver(%s): selected %s "
		"(attach=%p spec=%" B_PRId32 " prio=%" B_PRId32 " probe=%.2f)\n",
		node->ModuleName(), bestDriver->info.name,
		bestDriver->attach, bestSpecificity, bestPriority, bestProbe);

	*_driver = bestDriver;
	return B_OK;
}


status_t
DkMatcher::FindAllDrivers(DkNode* node, DkMatchResult* results,
	int32 maxResults, int32* _count)
{
	ReadLocker locker(&fLock);

	int32 count = 0;

	struct AllVisitor {
		DkNode*			node;
		DkMatchResult*	results;
		int32			maxResults;
		int32*			count;

		void operator()(DkDriverRecord* record)
		{
			if (*count >= maxResults)
				return;

			dk_driver_info* info = record->info;
			if (info == NULL || info->attach == NULL)
				return;

			dprintf("DeviceKeeper: FindAll: trying %s (match=%p probe=%p)\n",
				info->info.name, info->match, info->probe);

			// If match dict present, check declarative rules first
			if (info->match != NULL) {
				const dk_match_rule* rules = info->match->rules;
				if (rules != NULL && !node->Properties().Matches(rules)) {
					dprintf("DeviceKeeper: FindAll:   %s -> match rejected\n",
						info->info.name);
					return;
				}
			}

			// probe() is the imperative matching fallback
			float probeScore = 0.0f;
			if (info->probe != NULL) {
				probeScore = info->probe(
					reinterpret_cast<dk_node*>(node));
				if (probeScore <= 0.0f) {
					dprintf("DeviceKeeper: FindAll:   %s -> probe rejected (%.2f)\n",
						info->info.name, probeScore);
					return;
				}
				dprintf("DeviceKeeper: FindAll:   %s -> probe score %.2f\n",
					info->info.name, probeScore);
			} else if (info->match == NULL) {
				// No match dict and no probe — can't match anything
				return;
			}

			results[*count].driver = info;
			results[*count].specificity = (info->match != NULL)
				? DkMatcher::_ComputeSpecificity(info->match->rules,
					node->Properties())
				: 0;
			results[*count].priority = (info->match != NULL)
				? info->match->priority : 0;
			results[*count].probe_score = probeScore;
			(*count)++;
		}
	};

	AllVisitor visitor = {node, results, maxResults, &count};

	const char* busName = NULL;
	node->Properties().GetString(KOSM_DEVICE_BUS, &busName);
	fIndex.ForEachCandidate(busName, visitor);

	*_count = count;
	return count > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


/*static*/ int32
DkMatcher::_CountMatchRules(const dk_match_rule* rules)
{
	int32 count = 0;
	if (rules != NULL) {
		for (; rules->name != NULL; rules++)
			count++;
	}
	return count;
}


/*static*/ int32
DkMatcher::_ComputeSpecificity(const dk_match_rule* rules,
	const DkPropertyStore& store)
{
	return _CountMatchRules(rules);
}
