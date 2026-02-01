/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceRegistry.hpp"

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
	fHeader(NULL),
	fEntries(NULL),
	fIsOwner(false)
{
	_InitSharedArea();
}


SurfaceRegistry::~SurfaceRegistry()
{
	if (fRegistryArea >= 0) {
		// Only delete semaphore if we created it
		if (fIsOwner && fHeader != NULL && fHeader->lock >= 0)
			delete_sem(fHeader->lock);
		delete_area(fRegistryArea);
	}
}


status_t
SurfaceRegistry::_InitSharedArea()
{
	// Try to find existing registry created by another process
	area_id existingArea = find_area(SURFACE_REGISTRY_AREA_NAME);

	if (existingArea >= 0)
		return _CloneSharedArea(existingArea);

	// First process - create new registry
	return _CreateSharedArea();
}


status_t
SurfaceRegistry::_CreateSharedArea()
{
	size_t size = sizeof(SurfaceRegistryHeader)
		+ sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	void* address = NULL;
	fRegistryArea = create_area(SURFACE_REGISTRY_AREA_NAME, &address,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fHeader = (SurfaceRegistryHeader*)address;
	fEntries = (SurfaceRegistryEntry*)(fHeader + 1);

	// Initialize header
	fHeader->lock = create_sem(1, "kosm_surface_registry_lock");
	if (fHeader->lock < 0) {
		status_t error = fHeader->lock;
		delete_area(fRegistryArea);
		fRegistryArea = -1;
		fHeader = NULL;
		fEntries = NULL;
		return error;
	}

	fHeader->entryCount = 0;
	fHeader->tombstoneCount = 0;
	memset(fHeader->_reserved, 0, sizeof(fHeader->_reserved));

	// Initialize entries
	memset(fEntries, 0, sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES);

	fIsOwner = true;
	return B_OK;
}


status_t
SurfaceRegistry::_CloneSharedArea(area_id sourceArea)
{
	void* address = NULL;
	fRegistryArea = clone_area("kosm_surface_registry_clone", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fHeader = (SurfaceRegistryHeader*)address;
	fEntries = (SurfaceRegistryEntry*)(fHeader + 1);
	fIsOwner = false;

	return B_OK;
}


int32
SurfaceRegistry::_FindSlot(surface_id id) const
{
	if (id == 0)
		return -1;

	int32 startIndex = (id - 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id)
			return index;

		if (entry.id == 0)
			return -1;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return -1;
}


int32
SurfaceRegistry::_FindEmptySlot(surface_id id) const
{
	if (id == 0)
		return -1;

	int32 startIndex = (id - 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == 0 || entry.id == SURFACE_ID_TOMBSTONE)
			return index;

		if (entry.id == id)
			return index;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return -1;
}


void
SurfaceRegistry::_Compact()
{
	// Rehash all live entries to eliminate tombstones
	// Must be called with lock held

	SurfaceRegistryEntry* temp = new(std::nothrow) SurfaceRegistryEntry[
		SURFACE_REGISTRY_MAX_ENTRIES];
	if (temp == NULL)
		return;

	memset(temp, 0, sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES);

	// Copy live entries to temp with new hash positions
	for (int32 i = 0; i < SURFACE_REGISTRY_MAX_ENTRIES; i++) {
		const SurfaceRegistryEntry& entry = fEntries[i];
		if (entry.id != 0 && entry.id != SURFACE_ID_TOMBSTONE) {
			// Find slot in temp array
			int32 newIndex = (entry.id - 1) % SURFACE_REGISTRY_MAX_ENTRIES;
			while (temp[newIndex].id != 0) {
				newIndex = (newIndex + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
			}
			temp[newIndex] = entry;
		}
	}

	memcpy(fEntries, temp, sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES);
	delete[] temp;

	fHeader->tombstoneCount = 0;
}


static uint64
generate_secret()
{
	bigtime_t time = system_time();
	static int32 sCounter = 0;
	int32 counter = atomic_add(&sCounter, 1);
	return (uint64)time ^ ((uint64)counter << 32) ^ 0x5DEECE66DULL;
}


status_t
SurfaceRegistry::Register(surface_id id, area_id sourceArea,
	const surface_desc& desc, size_t allocSize, uint32 planeCount)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindEmptySlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NO_MEMORY;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.id == SURFACE_ID_TOMBSTONE)
		fHeader->tombstoneCount--;

	entry.id = id;
	entry.globalUseCount = 0;

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	entry.ownerTeam = info.team;

	entry.sourceArea = sourceArea;
	entry.width = desc.width;
	entry.height = desc.height;
	entry.format = desc.format;
	entry.bytesPerRow = desc.bytesPerRow;
	entry.bytesPerElement = desc.bytesPerElement;
	entry.allocSize = allocSize;
	entry.planeCount = planeCount;

	entry.accessSecret = generate_secret();
	entry.secretGeneration = 0;

	fHeader->entryCount++;

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::Unregister(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.globalUseCount > 0) {
		release_sem(fHeader->lock);
		return B_SURFACE_IN_USE;
	}

	entry.id = SURFACE_ID_TOMBSTONE;
	entry.globalUseCount = 0;
	entry.ownerTeam = -1;
	entry.sourceArea = -1;

	fHeader->entryCount--;
	fHeader->tombstoneCount++;

	// Compact if too many tombstones
	if (fHeader->tombstoneCount > SURFACE_REGISTRY_TOMBSTONE_THRESHOLD)
		_Compact();

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::IncrementGlobalUseCount(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	atomic_add(&fEntries[index].globalUseCount, 1);

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::DecrementGlobalUseCount(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	atomic_add(&fEntries[index].globalUseCount, -1);

	release_sem(fHeader->lock);
	return B_OK;
}


int32
SurfaceRegistry::GlobalUseCount(surface_id id) const
{
	if (id == 0)
		return 0;

	if (fHeader == NULL)
		return 0;

	if (acquire_sem(fHeader->lock) != B_OK)
		return 0;

	int32 result = 0;
	int32 index = _FindSlot(id);
	if (index >= 0)
		result = fEntries[index].globalUseCount;

	release_sem(fHeader->lock);
	return result;
}


bool
SurfaceRegistry::IsInUse(surface_id id) const
{
	return GlobalUseCount(id) > 0;
}


status_t
SurfaceRegistry::LookupInfo(surface_id id, surface_desc* outDesc,
	area_id* outArea, size_t* outAllocSize, uint32* outPlaneCount) const
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	// Cross-process lookup allowed for same-team only (without token)
	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fHeader->lock);
		return B_NOT_ALLOWED;
	}

	if (outDesc != NULL) {
		outDesc->width = entry.width;
		outDesc->height = entry.height;
		outDesc->format = entry.format;
		outDesc->bytesPerRow = entry.bytesPerRow;
		outDesc->bytesPerElement = entry.bytesPerElement;
	}

	if (outArea != NULL)
		*outArea = entry.sourceArea;

	if (outAllocSize != NULL)
		*outAllocSize = entry.allocSize;

	if (outPlaneCount != NULL)
		*outPlaneCount = entry.planeCount;

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::CreateAccessToken(surface_id id, surface_token* outToken)
{
	if (id == 0 || outToken == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fHeader->lock);
		return B_NOT_ALLOWED;
	}

	outToken->id = id;
	outToken->secret = entry.accessSecret;
	outToken->generation = entry.secretGeneration;

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::ValidateToken(const surface_token& token)
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		release_sem(fHeader->lock);
		return B_NOT_ALLOWED;
	}

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::RevokeAllAccess(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fHeader->lock);
		return B_NOT_ALLOWED;
	}

	entry.accessSecret = generate_secret();
	entry.secretGeneration++;

	release_sem(fHeader->lock);
	return B_OK;
}


status_t
SurfaceRegistry::LookupInfoWithToken(const surface_token& token,
	surface_desc* outDesc, area_id* outArea, size_t* outAllocSize,
	uint32* outPlaneCount) const
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (acquire_sem(fHeader->lock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		release_sem(fHeader->lock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	// Validate token for cross-process access
	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		release_sem(fHeader->lock);
		return B_NOT_ALLOWED;
	}

	if (outDesc != NULL) {
		outDesc->width = entry.width;
		outDesc->height = entry.height;
		outDesc->format = entry.format;
		outDesc->bytesPerRow = entry.bytesPerRow;
		outDesc->bytesPerElement = entry.bytesPerElement;
	}

	if (outArea != NULL)
		*outArea = entry.sourceArea;

	if (outAllocSize != NULL)
		*outAllocSize = entry.allocSize;

	if (outPlaneCount != NULL)
		*outPlaneCount = entry.planeCount;

	release_sem(fHeader->lock);
	return B_OK;
}
