/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceRegistry.hpp"

#include <new>
#include <string.h>


KosmSurfaceRegistry*
KosmSurfaceRegistry::Default()
{
	static KosmSurfaceRegistry sDefault;
	return &sDefault;
}


KosmSurfaceRegistry::KosmSurfaceRegistry()
	:
	fRegistryArea(-1),
	fHeader(NULL),
	fEntries(NULL),
	fIsOwner(false)
{
	_InitSharedArea();
}


KosmSurfaceRegistry::~KosmSurfaceRegistry()
{
	if (fRegistryArea >= 0) {
		if (fIsOwner && fHeader != NULL && fHeader->lock >= 0)
			kosm_delete_mutex(fHeader->lock);
		delete_area(fRegistryArea);
	}
}


status_t
KosmSurfaceRegistry::_InitSharedArea()
{
	area_id existingArea = find_area(KOSM_SURFACE_REGISTRY_AREA_NAME);

	if (existingArea >= 0)
		return _CloneSharedArea(existingArea);

	status_t status = _CreateSharedArea();
	if (status != B_OK) {
		existingArea = find_area(KOSM_SURFACE_REGISTRY_AREA_NAME);
		if (existingArea >= 0)
			return _CloneSharedArea(existingArea);
	}

	return status;
}


status_t
KosmSurfaceRegistry::_CreateSharedArea()
{
	size_t size = sizeof(KosmSurfaceRegistryHeader)
		+ sizeof(KosmSurfaceRegistryEntry) * KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	void* address = NULL;
	fRegistryArea = create_area(KOSM_SURFACE_REGISTRY_AREA_NAME, &address,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fHeader = (KosmSurfaceRegistryHeader*)address;
	fEntries = (KosmSurfaceRegistryEntry*)(fHeader + 1);

	fHeader->lock = kosm_create_mutex(KOSM_SURFACE_REGISTRY_MUTEX_NAME,
		KOSM_MUTEX_SHARED);
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

	memset(fEntries, 0,
		sizeof(KosmSurfaceRegistryEntry) * KOSM_SURFACE_REGISTRY_MAX_ENTRIES);

	fIsOwner = true;
	return B_OK;
}


status_t
KosmSurfaceRegistry::_CloneSharedArea(area_id sourceArea)
{
	void* address = NULL;
	fRegistryArea = clone_area("kosm_surface_registry_clone", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fHeader = (KosmSurfaceRegistryHeader*)address;
	fEntries = (KosmSurfaceRegistryEntry*)(fHeader + 1);
	fIsOwner = false;

	return B_OK;
}


status_t
KosmSurfaceRegistry::_Lock() const
{
	status_t status = kosm_acquire_mutex(fHeader->lock);

	if (status == KOSM_MUTEX_OWNER_DEAD) {
		// Previous holder died while modifying the registry.
		// The data may be inconsistent, but our open-addressing
		// hash table is self-describing enough to survive: entries
		// are either valid (non-zero id), empty (0), or tombstoned.
		// Mark consistent and proceed.
		kosm_mark_mutex_consistent(fHeader->lock);
		return B_OK;
	}

	return status;
}


status_t
KosmSurfaceRegistry::_Unlock() const
{
	return kosm_release_mutex(fHeader->lock);
}


int32
KosmSurfaceRegistry::_FindSlot(kosm_surface_id id) const
{
	if (id == 0)
		return -1;

	int32 startIndex = (id - 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
	int32 index = startIndex;

	do {
		const KosmSurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id)
			return index;

		if (entry.id == 0)
			return -1;

		index = (index + 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return -1;
}


int32
KosmSurfaceRegistry::_FindEmptySlot(kosm_surface_id id) const
{
	if (id == 0)
		return -1;

	int32 startIndex = (id - 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
	int32 index = startIndex;

	do {
		const KosmSurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == 0 || entry.id == KOSM_SURFACE_ID_TOMBSTONE)
			return index;

		if (entry.id == id)
			return index;

		index = (index + 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return -1;
}


void
KosmSurfaceRegistry::_Compact()
{
	KosmSurfaceRegistryEntry* temp = new(std::nothrow)
		KosmSurfaceRegistryEntry[KOSM_SURFACE_REGISTRY_MAX_ENTRIES];
	if (temp == NULL)
		return;

	memset(temp, 0,
		sizeof(KosmSurfaceRegistryEntry) * KOSM_SURFACE_REGISTRY_MAX_ENTRIES);

	for (int32 i = 0; i < KOSM_SURFACE_REGISTRY_MAX_ENTRIES; i++) {
		const KosmSurfaceRegistryEntry& entry = fEntries[i];
		if (entry.id != 0 && entry.id != KOSM_SURFACE_ID_TOMBSTONE) {
			int32 newIndex = (entry.id - 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
			while (temp[newIndex].id != 0) {
				newIndex = (newIndex + 1) % KOSM_SURFACE_REGISTRY_MAX_ENTRIES;
			}
			temp[newIndex] = entry;
		}
	}

	memcpy(fEntries, temp,
		sizeof(KosmSurfaceRegistryEntry) * KOSM_SURFACE_REGISTRY_MAX_ENTRIES);
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
KosmSurfaceRegistry::Register(kosm_surface_id id, area_id sourceArea,
	const KosmSurfaceDesc& desc, size_t allocSize, uint32 planeCount)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindEmptySlot(id);
	if (index < 0) {
		_Unlock();
		return B_NO_MEMORY;
	}

	KosmSurfaceRegistryEntry& entry = fEntries[index];

	if (entry.id == id) {
		_Unlock();
		return KOSM_SURFACE_ID_EXISTS;
	}

	if (entry.id == KOSM_SURFACE_ID_TOMBSTONE)
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

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::Unregister(kosm_surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	KosmSurfaceRegistryEntry& entry = fEntries[index];

	if (entry.globalUseCount > 0) {
		_Unlock();
		return KOSM_SURFACE_IN_USE;
	}

	entry.id = KOSM_SURFACE_ID_TOMBSTONE;
	entry.globalUseCount = 0;
	entry.ownerTeam = -1;
	entry.sourceArea = -1;

	fHeader->entryCount--;
	fHeader->tombstoneCount++;

	if (fHeader->tombstoneCount > KOSM_SURFACE_REGISTRY_TOMBSTONE_THRESHOLD)
		_Compact();

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::IncrementGlobalUseCount(kosm_surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	fEntries[index].globalUseCount++;

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::DecrementGlobalUseCount(kosm_surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	if (fEntries[index].globalUseCount > 0)
		fEntries[index].globalUseCount--;

	_Unlock();
	return B_OK;
}


int32
KosmSurfaceRegistry::GlobalUseCount(kosm_surface_id id) const
{
	if (id == 0)
		return 0;

	if (fHeader == NULL)
		return 0;

	if (_Lock() != B_OK)
		return 0;

	int32 result = 0;
	int32 index = _FindSlot(id);
	if (index >= 0)
		result = fEntries[index].globalUseCount;

	_Unlock();
	return result;
}


bool
KosmSurfaceRegistry::IsInUse(kosm_surface_id id) const
{
	return GlobalUseCount(id) > 0;
}


status_t
KosmSurfaceRegistry::LookupInfo(kosm_surface_id id,
	KosmSurfaceDesc* outDesc, area_id* outArea,
	size_t* outAllocSize, uint32* outPlaneCount) const
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	const KosmSurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		_Unlock();
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

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::CreateAccessToken(kosm_surface_id id,
	KosmSurfaceToken* outToken)
{
	if (id == 0 || outToken == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	const KosmSurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		_Unlock();
		return B_NOT_ALLOWED;
	}

	outToken->id = id;
	outToken->secret = entry.accessSecret;
	outToken->generation = entry.secretGeneration;

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::ValidateToken(const KosmSurfaceToken& token)
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	const KosmSurfaceRegistryEntry& entry = fEntries[index];

	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		_Unlock();
		return B_NOT_ALLOWED;
	}

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::RevokeAllAccess(kosm_surface_id id)
{
	if (id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	KosmSurfaceRegistryEntry& entry = fEntries[index];

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	if (entry.ownerTeam != info.team) {
		_Unlock();
		return B_NOT_ALLOWED;
	}

	entry.accessSecret = generate_secret();
	entry.secretGeneration++;

	_Unlock();
	return B_OK;
}


status_t
KosmSurfaceRegistry::LookupInfoWithToken(const KosmSurfaceToken& token,
	KosmSurfaceDesc* outDesc, area_id* outArea,
	size_t* outAllocSize, uint32* outPlaneCount) const
{
	if (token.id == 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t status = _Lock();
	if (status != B_OK)
		return status;

	int32 index = _FindSlot(token.id);
	if (index < 0) {
		_Unlock();
		return B_NAME_NOT_FOUND;
	}

	const KosmSurfaceRegistryEntry& entry = fEntries[index];

	if (entry.accessSecret != token.secret
		|| entry.secretGeneration != token.generation) {
		_Unlock();
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

	_Unlock();
	return B_OK;
}
