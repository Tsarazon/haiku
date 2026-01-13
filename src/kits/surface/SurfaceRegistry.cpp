/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceRegistry.h"

#include <new>
#include <string.h>


SurfaceRegistry*
SurfaceRegistry::Default()
{
	static SurfaceRegistry sDefault;
	return &sDefault;
}


SurfaceRegistry::SurfaceRegistry()
	:
	fRegistryArea(-1),
	fEntries(NULL),
	fLock(-1)
{
	_InitSharedArea();
}


SurfaceRegistry::~SurfaceRegistry()
{
	if (fLock >= 0)
		delete_sem(fLock);
	if (fRegistryArea >= 0)
		delete_area(fRegistryArea);
}


status_t
SurfaceRegistry::_InitSharedArea()
{
	size_t size = sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	void* address = NULL;
	fRegistryArea = create_area("surface_registry", &address,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fEntries = (SurfaceRegistryEntry*)address;
	memset(fEntries, 0, size);

	fLock = create_sem(1, "surface_registry_lock");
	if (fLock < 0) {
		delete_area(fRegistryArea);
		fRegistryArea = -1;
		return fLock;
	}

	return B_OK;
}


int32
SurfaceRegistry::_IndexFor(surface_id id) const
{
	return (id - 1) % SURFACE_REGISTRY_MAX_ENTRIES;
}


status_t
SurfaceRegistry::Register(surface_id id, area_id sourceArea)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	// Linear probing to find empty slot or existing entry
	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == 0 || entry.id == SURFACE_ID_TOMBSTONE || entry.id == id) {
			entry.id = id;
			entry.globalUseCount = 1;

			thread_info info;
			get_thread_info(find_thread(NULL), &info);
			entry.ownerTeam = info.team;

			entry.sourceArea = sourceArea;

			release_sem(fLock);
			return B_OK;
		}

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NO_MEMORY;
}


status_t
SurfaceRegistry::Unregister(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	// Linear probing to find entry
	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			if (entry.globalUseCount > 0) {
				release_sem(fLock);
				return B_SURFACE_IN_USE;
			}

			entry.id = SURFACE_ID_TOMBSTONE;
			entry.globalUseCount = 0;
			entry.ownerTeam = -1;
			entry.sourceArea = -1;

			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;  // Empty slot - entry not found (tombstone continues search)

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


status_t
SurfaceRegistry::IncrementGlobalUseCount(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			atomic_add(&entry.globalUseCount, 1);
			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


status_t
SurfaceRegistry::DecrementGlobalUseCount(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			atomic_add(&entry.globalUseCount, -1);
			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


int32
SurfaceRegistry::GlobalUseCount(surface_id id) const
{
	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id)
			return atomic_get(const_cast<int32*>(&entry.globalUseCount));

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return 0;
}


bool
SurfaceRegistry::IsInUse(surface_id id) const
{
	return GlobalUseCount(id) > 0;
}


status_t
SurfaceRegistry::LookupArea(surface_id id, area_id* outArea) const
{
	if (outArea == NULL)
		return B_BAD_VALUE;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			*outArea = entry.sourceArea;
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return B_NAME_NOT_FOUND;
}
