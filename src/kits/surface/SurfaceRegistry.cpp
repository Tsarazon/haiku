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
	fEntries(NULL),
	fLock(-1),
	fTombstoneCount(0)
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

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindEmptySlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NO_MEMORY;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.id == SURFACE_ID_TOMBSTONE)
		fTombstoneCount--;

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

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::Unregister(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.globalUseCount > 0) {
		release_sem(fLock);
		return B_SURFACE_IN_USE;
	}

	entry.id = SURFACE_ID_TOMBSTONE;
	entry.globalUseCount = 0;
	entry.ownerTeam = -1;
	entry.sourceArea = -1;
	fTombstoneCount++;

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::IncrementGlobalUseCount(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	atomic_add(&fEntries[index].globalUseCount, 1);

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::DecrementGlobalUseCount(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	atomic_add(&fEntries[index].globalUseCount, -1);

	release_sem(fLock);
	return B_OK;
}


int32
SurfaceRegistry::GlobalUseCount(surface_id id) const
{
	if (id == 0)
		return 0;

	if (acquire_sem(fLock) != B_OK)
		return 0;

	int32 result = 0;
	int32 index = _FindSlot(id);
	if (index >= 0)
		result = fEntries[index].globalUseCount;

	release_sem(fLock);
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

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fLock);
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

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::CreateAccessToken(surface_id id, surface_token* outToken)
{
	if (id == 0 || outToken == NULL)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fLock);
		return B_NOT_ALLOWED;
	}

	outToken->id = id;
	outToken->secret = entry.accessSecret;
	outToken->generation = entry.secretGeneration;

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::ValidateToken(const surface_token& token)
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		release_sem(fLock);
		return B_NOT_ALLOWED;
	}

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::RevokeAllAccess(surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	SurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		release_sem(fLock);
		return B_NOT_ALLOWED;
	}

	entry.accessSecret = generate_secret();
	entry.secretGeneration++;

	release_sem(fLock);
	return B_OK;
}


status_t
SurfaceRegistry::LookupInfoWithToken(const surface_token& token,
	surface_desc* outDesc, area_id* outArea, size_t* outAllocSize,
	uint32* outPlaneCount) const
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		release_sem(fLock);
		return B_NAME_NOT_FOUND;
	}

	const SurfaceRegistryEntry& entry = fEntries[index];

	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		release_sem(fLock);
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

	release_sem(fLock);
	return B_OK;
}
