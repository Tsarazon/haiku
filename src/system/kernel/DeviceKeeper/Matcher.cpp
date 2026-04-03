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

	// Phase 1: collect candidate names without holding fLock.
	// open_module_list_etc/read_next_module_name only scan the
	// module directory metadata, which is fast. The expensive
	// get_module() calls happen in Phase 2.
	static const int32 kMaxCandidates = 128;
	char names[kMaxCandidates][B_FILE_NAME_LENGTH];
	int32 candidateCount = 0;

	while (candidateCount < kMaxCandidates) {
		size_t nameLength = B_FILE_NAME_LENGTH;
		if (read_next_module_name(list, names[candidateCount],
				&nameLength) != B_OK) {
			break;
		}
		candidateCount++;
	}

	close_module_list(list);

	if (candidateCount == 0) {
		dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): no candidates\n",
			path);
		return B_ENTRY_NOT_FOUND;
	}

	dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): %d candidates\n",
		path, (int)candidateCount);

	// Phase 2: filter already-registered under read lock (fast).
	// Collect indices of genuinely new candidates.
	int32 newIndices[kMaxCandidates];
	int32 newCount = 0;

	{
		ReadLocker locker(&fLock);

		for (int32 c = 0; c < candidateCount; c++) {
			bool exists = false;
			DriverList::Iterator it = fDrivers.GetIterator();
			while (it.HasNext()) {
				DkDriverRecord* record = it.Next();
				if (strcmp(record->module_name, names[c]) == 0) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				newIndices[newCount++] = c;
			} else {
				dprintf("DeviceKeeper:   %s -> already registered\n",
					names[c]);
			}
		}
	}

	if (newCount == 0) {
		dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): all already "
			"registered\n", path);
		return B_ENTRY_NOT_FOUND;
	}

	// Phase 3: load modules WITHOUT lock. get_module() may do disk
	// I/O, and we don't want to block FindBestDriver callers.
	struct LoadedModule {
		dk_driver_info*	info;
		bool			valid;
	};

	LoadedModule* loaded = (LoadedModule*)malloc(
		newCount * sizeof(LoadedModule));
	if (loaded == NULL) {
		// Fallback: use the old single-phase path under write lock.
		// Less concurrent but still correct.
		WriteLocker locker(&fLock);
		int32 found = 0;
		for (int32 i = 0; i < newCount; i++) {
			status_t status = _RegisterDriverLocked(names[newIndices[i]]);
			if (status == B_OK)
				found++;
		}
		return found > 0 ? B_OK : B_ENTRY_NOT_FOUND;
	}

	for (int32 i = 0; i < newCount; i++) {
		loaded[i].valid = false;
		dk_driver_info* info;
		status_t s = get_module(names[newIndices[i]],
			(module_info**)&info);
		if (s == B_OK) {
			loaded[i].info = info;
			loaded[i].valid = true;
			dprintf("DeviceKeeper:   loaded %s\n", names[newIndices[i]]);
		} else {
			dprintf("DeviceKeeper:   load failed %s: %s\n",
				names[newIndices[i]], strerror(s));
		}
	}

	// Phase 4: insert under write lock (fast — no I/O).
	int32 registered = 0;

	{
		WriteLocker locker(&fLock);

		for (int32 i = 0; i < newCount; i++) {
			if (!loaded[i].valid)
				continue;

			// Re-check for duplicates: another thread may have
			// registered the same module between Phase 2 and now.
			bool exists = false;
			DriverList::Iterator it = fDrivers.GetIterator();
			while (it.HasNext()) {
				DkDriverRecord* record = it.Next();
				if (strcmp(record->module_name,
						names[newIndices[i]]) == 0) {
					exists = true;
					break;
				}
			}

			if (exists) {
				put_module(names[newIndices[i]]);
				continue;
			}

			DkDriverRecord* record = new(std::nothrow) DkDriverRecord();
			if (record == NULL) {
				put_module(names[newIndices[i]]);
				continue;
			}

			record->module_name = loaded[i].info->info.name;
			record->info = loaded[i].info;
			record->loaded = true;
			record->index_next = NULL;
			record->index_bucket = 0;

			fDrivers.Add(record);
			fIndex.Insert(record);
			registered++;
			dprintf("DeviceKeeper:   registered %s\n",
				names[newIndices[i]]);
		}
	}

	free(loaded);

	dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): registered %d of "
		"%d new candidates\n", path, (int)registered, (int)newCount);
	return registered > 0 ? B_OK : B_ENTRY_NOT_FOUND;
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

	// Take an extra module reference before releasing the read lock.
	// This ensures the module stays loaded even if a concurrent
	// UnregisterDriver removes the DkDriverRecord and calls
	// put_module() between our unlock and the caller's attach().
	// The caller must call put_module() if attach() fails.
	status_t refStatus = get_module(bestDriver->info.name,
		(module_info**)&bestDriver);
	if (refStatus != B_OK) {
		dprintf("DeviceKeeper: FindBestDriver(%s): get_module ref "
			"failed for %s: %s\n",
			node->ModuleName(), bestDriver->info.name, strerror(refStatus));
		return refStatus;
	}

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

	// Take an extra module reference on each matched driver before
	// releasing the read lock. This protects against concurrent
	// UnregisterDriver unloading the module between our unlock and
	// the caller's attach() calls.
	// Callers must put_module() for drivers they don't successfully
	// attach.
	for (int32 i = 0; i < count; i++) {
		module_info* ref;
		status_t s = get_module(results[i].driver->info.name, &ref);
		if (s != B_OK) {
			// Module vanished — remove this entry by shifting
			dprintf("DeviceKeeper: FindAll: get_module ref failed "
				"for %s\n", results[i].driver->info.name);
			for (int32 j = i; j < count - 1; j++)
				results[j] = results[j + 1];
			count--;
			i--;	// re-check this index
		}
	}

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
