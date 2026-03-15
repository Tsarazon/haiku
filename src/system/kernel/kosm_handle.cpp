/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmOS Handle Table implementation.
 *
 * Per-process capability table, Zircon-inspired.
 * See kosm_handle.h for design overview.
 */


#include <kosm_handle.h>

#include <algorithm>
#include <stdlib.h>
#include <string.h>

#include <heap.h>
#include <kernel.h>
#include <kosm_mutex.h>
#include <kosm_ray.h>
#include <team.h>
#include <thread.h>
#include <vm/vm.h>


//#define TRACE_KOSM_HANDLE
#ifdef TRACE_KOSM_HANDLE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


// Handle encoding helpers

/*static*/ inline uint32
KosmHandleTable::_IndexOf(kosm_handle_t handle)
{
	return (uint32)handle & 0xFFFF;
}


/*static*/ inline uint16
KosmHandleTable::_GenerationOf(kosm_handle_t handle)
{
	return (uint16)((uint32)handle >> 16);
}


/*static*/ inline kosm_handle_t
KosmHandleTable::_MakeHandle(uint32 index, uint16 generation)
{
	// Mask generation to 15 bits so handle is always positive (bit 31 = 0)
	return (kosm_handle_t)(((uint32)(generation & 0x7FFF) << 16) | (index & 0xFFFF));
}


KosmHandleTable::KosmHandleTable()
	:
	fEntries(NULL),
	fCapacity(0),
	fCount(0),
	fFreeHead(-1)
{
	mutex_init(&fLock, "kosm handles");
}


KosmHandleTable::~KosmHandleTable()
{
	// Release all remaining entries
	if (fEntries != NULL) {
		for (uint32 i = 1; i < fCapacity; i++) {
			KosmHandleEntry* entry = &fEntries[i];
			if (entry->type == KOSM_HANDLE_TYPE_NONE)
				continue;

			switch (entry->type) {
				case KOSM_HANDLE_RAY:
				case KOSM_HANDLE_MUTEX:
					// Refcounted objects: release reference
					if (entry->object != NULL)
						entry->object->ReleaseReference();
					break;

				case KOSM_HANDLE_AREA:
				case KOSM_HANDLE_SEM:
				case KOSM_HANDLE_FD:
					// Legacy objects: no reference to release.
					// The respective subsystem handles cleanup.
					break;
			}
		}

		free(fEntries);
	}

	mutex_destroy(&fLock);
}


status_t
KosmHandleTable::Init(uint32 capacity)
{
	if (capacity < 16)
		capacity = 16;
	if (capacity > KOSM_HANDLE_MAX_PER_TEAM)
		capacity = KOSM_HANDLE_MAX_PER_TEAM;

	fEntries = (KosmHandleEntry*)calloc(capacity, sizeof(KosmHandleEntry));
	if (fEntries == NULL)
		return B_NO_MEMORY;

	fCapacity = capacity;
	fCount = 0;

	// Build free list. Index 0 is reserved (KOSM_HANDLE_INVALID).
	// Chain: 1 -> 2 -> 3 -> ... -> (capacity-1) -> -1
	// We store the next free index in the legacy_id field of unused entries.
	fFreeHead = 1;
	for (uint32 i = 1; i < capacity - 1; i++) {
		fEntries[i].type = KOSM_HANDLE_TYPE_NONE;
		fEntries[i].legacy_id = (int32)(i + 1);
	}
	fEntries[capacity - 1].type = KOSM_HANDLE_TYPE_NONE;
	fEntries[capacity - 1].legacy_id = -1;

	return B_OK;
}


status_t
KosmHandleTable::_Grow()
{
	uint32 newCapacity = fCapacity * 2;
	if (newCapacity > KOSM_HANDLE_MAX_PER_TEAM)
		newCapacity = KOSM_HANDLE_MAX_PER_TEAM;
	if (newCapacity <= fCapacity)
		return B_NO_MORE_PORTS;

	KosmHandleEntry* newEntries = (KosmHandleEntry*)realloc(fEntries,
		newCapacity * sizeof(KosmHandleEntry));
	if (newEntries == NULL)
		return B_NO_MEMORY;

	// Initialize new entries and chain them into free list
	memset(&newEntries[fCapacity], 0,
		(newCapacity - fCapacity) * sizeof(KosmHandleEntry));

	for (uint32 i = fCapacity; i < newCapacity - 1; i++) {
		newEntries[i].type = KOSM_HANDLE_TYPE_NONE;
		newEntries[i].legacy_id = (int32)(i + 1);
	}
	newEntries[newCapacity - 1].type = KOSM_HANDLE_TYPE_NONE;
	newEntries[newCapacity - 1].legacy_id = fFreeHead;

	fFreeHead = (int32)fCapacity;
	fEntries = newEntries;
	fCapacity = newCapacity;
	return B_OK;
}


KosmHandleEntry*
KosmHandleTable::_EntryFor(kosm_handle_t handle)
{
	uint32 index = _IndexOf(handle);
	uint16 generation = _GenerationOf(handle);

	if (index == 0 || index >= fCapacity)
		return NULL;

	KosmHandleEntry* entry = &fEntries[index];
	if (entry->type == KOSM_HANDLE_TYPE_NONE)
		return NULL;
	if (entry->generation != generation)
		return NULL;

	return entry;
}


kosm_handle_t
KosmHandleTable::_AllocateSlot(uint16 type, uint32 rights)
{
	// Must be called with fLock held.

	if (fFreeHead < 0) {
		status_t status = _Grow();
		if (status != B_OK)
			return status;
	}

	uint32 index = (uint32)fFreeHead;
	KosmHandleEntry* entry = &fEntries[index];

	// Advance free list
	fFreeHead = entry->legacy_id;

	// Initialize entry
	uint16 generation = entry->generation;		// preserved from last use
	entry->type = type;
	entry->rights = rights;
	// generation is already set (incremented on removal)
	// object/legacy_id will be set by caller

	fCount++;
	return _MakeHandle(index, generation);
}


kosm_handle_t
KosmHandleTable::Insert(KernelReferenceable* object, uint16 type, uint32 rights)
{
	if (object == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	kosm_handle_t handle = _AllocateSlot(type, rights);
	if (handle < 0)
		return handle;

	uint32 index = _IndexOf(handle);
	fEntries[index].object = object;
	object->AcquireReference();

	TRACE(("kosm_handle: insert type=%u handle=%" B_PRId32
		" index=%" B_PRIu32 " gen=%u\n",
		type, handle, index, _GenerationOf(handle)));
	return handle;
}


kosm_handle_t
KosmHandleTable::InsertLegacy(int32 legacyID, uint16 type, uint32 rights)
{
	MutexLocker locker(fLock);

	kosm_handle_t handle = _AllocateSlot(type, rights);
	if (handle < 0)
		return handle;

	uint32 index = _IndexOf(handle);
	fEntries[index].legacy_id = legacyID;
	return handle;
}


status_t
KosmHandleTable::Lookup(kosm_handle_t handle, uint16 expectedType,
	uint32 requiredRights, KernelReferenceable** outObject)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if (entry->type != expectedType)
		return B_BAD_VALUE;

	if ((entry->rights & requiredRights) != requiredRights)
		return B_NOT_ALLOWED;

	if (outObject != NULL) {
		entry->object->AcquireReference();
		*outObject = entry->object;
	}

	return B_OK;
}


status_t
KosmHandleTable::LookupAny(kosm_handle_t handle, uint32 requiredRights,
	KernelReferenceable** outObject, uint16* outType)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if ((entry->rights & requiredRights) != requiredRights)
		return B_NOT_ALLOWED;

	if (outType != NULL)
		*outType = entry->type;

	if (outObject != NULL) {
		// Legacy types (area, sem, fd) store an int32 in the union,
		// not a pointer. Dereferencing would be a kernel panic.
		if (entry->type == KOSM_HANDLE_AREA
			|| entry->type == KOSM_HANDLE_SEM
			|| entry->type == KOSM_HANDLE_FD) {
			*outObject = NULL;
			return B_BAD_VALUE;
		}
		entry->object->AcquireReference();
		*outObject = entry->object;
	}

	return B_OK;
}


status_t
KosmHandleTable::LookupLegacy(kosm_handle_t handle, uint16 expectedType,
	uint32 requiredRights, int32* outLegacyID)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if (entry->type != expectedType)
		return B_BAD_VALUE;

	if ((entry->rights & requiredRights) != requiredRights)
		return B_NOT_ALLOWED;

	if (outLegacyID != NULL)
		*outLegacyID = entry->legacy_id;

	return B_OK;
}


status_t
KosmHandleTable::GetInfo(kosm_handle_t handle, uint16* outType,
	uint32* outRights)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if (outType != NULL)
		*outType = entry->type;
	if (outRights != NULL)
		*outRights = entry->rights;

	return B_OK;
}


kosm_handle_t
KosmHandleTable::FindHandleForObject(KernelReferenceable* object,
	uint16 expectedType)
{
	MutexLocker locker(fLock);

	for (uint32 i = 1; i < fCapacity; i++) {
		KosmHandleEntry* entry = &fEntries[i];
		if (entry->type != expectedType)
			continue;
		if (entry->object == object)
			return _MakeHandle(i, entry->generation);
	}
	return KOSM_HANDLE_INVALID;
}


status_t
KosmHandleTable::Remove(kosm_handle_t handle,
	KernelReferenceable** outObject, uint16* outType)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	uint32 index = _IndexOf(handle);
	KernelReferenceable* object = entry->object;
	uint16 type = entry->type;

	// Clear entry and return to free list
	entry->type = KOSM_HANDLE_TYPE_NONE;
	entry->generation = (entry->generation + 1) & 0x7FFF;
	entry->legacy_id = fFreeHead;
	fFreeHead = (int32)index;
	fCount--;

	locker.Unlock();

	if (outType != NULL)
		*outType = type;

	if (outObject != NULL) {
		*outObject = object;
		// Caller takes ownership of the reference
	} else {
		// Release the reference held by the handle
		if (type == KOSM_HANDLE_RAY || type == KOSM_HANDLE_MUTEX)
			object->ReleaseReference();
	}

	TRACE(("kosm_handle: remove handle=%" B_PRId32 " type=%u\n",
		handle, type));
	return B_OK;
}


status_t
KosmHandleTable::RemoveLegacy(kosm_handle_t handle,
	int32* outLegacyID, uint16* outType)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	uint32 index = _IndexOf(handle);

	if (outLegacyID != NULL)
		*outLegacyID = entry->legacy_id;
	if (outType != NULL)
		*outType = entry->type;

	// Clear entry and return to free list
	entry->type = KOSM_HANDLE_TYPE_NONE;
	entry->generation = (entry->generation + 1) & 0x7FFF;
	entry->legacy_id = fFreeHead;
	fFreeHead = (int32)index;
	fCount--;

	return B_OK;
}


kosm_handle_t
KosmHandleTable::Duplicate(kosm_handle_t handle, uint32 newRights)
{
	if (handle <= 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);

	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if ((entry->rights & KOSM_RIGHT_DUPLICATE) == 0)
		return B_NOT_ALLOWED;

	// New rights must be a subset of existing rights
	if ((newRights & ~entry->rights) != 0)
		return B_NOT_ALLOWED;

	uint16 type = entry->type;
	uint32 rights = newRights;
	KernelReferenceable* object = entry->object;
	int32 legacyID = entry->legacy_id;

	kosm_handle_t newHandle = _AllocateSlot(type, rights);
	if (newHandle < 0)
		return newHandle;

	uint32 newIndex = _IndexOf(newHandle);
	if (type == KOSM_HANDLE_RAY || type == KOSM_HANDLE_MUTEX) {
		fEntries[newIndex].object = object;
		object->AcquireReference();
	} else {
		fEntries[newIndex].legacy_id = legacyID;
	}

	return newHandle;
}


status_t
KosmHandleTable::Transfer(kosm_handle_t handle,
	KosmHandleTable* target, kosm_handle_t* outHandle)
{
	if (handle <= 0 || target == NULL || outHandle == NULL)
		return B_BAD_VALUE;

	// Lock both tables in consistent order (by address, since we don't
	// have team_id here). Lower address first.
	KosmHandleTable* first = this < target ? this : target;
	KosmHandleTable* second = this < target ? target : this;

	MutexLocker firstLocker(first->fLock);
	MutexLocker secondLocker;
	if (first != second)
		secondLocker.SetTo(second->fLock, false);

	// Validate source handle
	KosmHandleEntry* entry = _EntryFor(handle);
	if (entry == NULL)
		return B_BAD_VALUE;

	if ((entry->rights & KOSM_RIGHT_TRANSFER) == 0)
		return B_NOT_ALLOWED;

	// Allocate slot in target table
	kosm_handle_t newHandle = target->_AllocateSlot(entry->type,
		entry->rights);
	if (newHandle < 0)
		return (status_t)newHandle;

	// Copy entry data to target
	uint32 newIndex = _IndexOf(newHandle);
	target->fEntries[newIndex].object = entry->object;
	// Reference moves with the entry, no acquire/release needed

	// Remove from source table
	uint32 oldIndex = _IndexOf(handle);
	entry->type = KOSM_HANDLE_TYPE_NONE;
	entry->generation = (entry->generation + 1) & 0x7FFF;
	entry->legacy_id = fFreeHead;
	fFreeHead = (int32)oldIndex;
	fCount--;

	*outHandle = newHandle;
	return B_OK;
}


void
KosmHandleTable::IterateEntries(IterateCallback callback, void* cookie)
{
	MutexLocker locker(fLock);

	for (uint32 i = 1; i < fCapacity; i++) {
		KosmHandleEntry* entry = &fEntries[i];
		if (entry->type == KOSM_HANDLE_TYPE_NONE)
			continue;
		if (!callback(entry, i, cookie))
			break;
	}
}


// Per-team table access via Team struct


/*!	Return the handle table for the given team.
	The returned pointer is valid only while the target team exists.
	Callers must ensure the team cannot be destroyed while using the
	table (e.g. by holding a reference to the team, or by being in a
	context where the team is known to be alive such as set_bootstrap
	from a parent to a child it just created).
*/
/*static*/ KosmHandleTable*
KosmHandleTable::TableFor(team_id teamId)
{
	Team* team = Team::Get(teamId);
	if (team == NULL)
		return NULL;

	KosmHandleTable* table = team->kosm_handle_table;
	team->ReleaseReference();
	return table;
}


/*static*/ KosmHandleTable*
KosmHandleTable::TableForCurrent()
{
	Thread* thread = thread_get_current_thread();
	if (thread == NULL || thread->team == NULL)
		return NULL;
	return thread->team->kosm_handle_table;
}


// Kernel init


status_t
kosm_handle_init(kernel_args* /*args*/)
{
	// Nothing to initialize globally. Each Team allocates its own
	// KosmHandleTable in Team::Create and frees it in ~Team.
	return B_OK;
}


// Syscalls


status_t
_user_kosm_close_handle(kosm_handle_t handle)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	// Determine type to perform type-specific cleanup
	uint16 type;
	status_t status = table->GetInfo(handle, &type, NULL);
	if (status != B_OK)
		return status;

	switch (type) {
		case KOSM_HANDLE_RAY:
		{
			KernelReferenceable* object;
			status = table->Remove(handle, &object);
			if (status != B_OK)
				return status;
			// Extract internal ID before releasing our reference,
			// then close.  Matches _user_kosm_close_ray ordering.
			kosm_ray_id internalId = kosm_ray_object_id(object);
			object->ReleaseReference();
			return kosm_close_ray_internal(internalId);
		}

		case KOSM_HANDLE_MUTEX:
		{
			KernelReferenceable* object;
			status = table->Remove(handle, &object);
			if (status != B_OK)
				return status;
			KosmMutexObject* mobj = static_cast<KosmMutexObject*>(object);
			kosm_mutex_id internalId = mobj->internal_id;
			object->ReleaseReference();
			return kosm_delete_mutex_internal(internalId);
		}

		case KOSM_HANDLE_AREA:
		case KOSM_HANDLE_SEM:
		case KOSM_HANDLE_FD:
			return table->RemoveLegacy(handle);

		default:
			return B_BAD_VALUE;
	}
}


kosm_handle_t
_user_kosm_duplicate_handle(kosm_handle_t handle, uint32 rights)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	return table->Duplicate(handle, rights);
}


status_t
_user_kosm_handle_get_info(kosm_handle_t handle,
	kosm_handle_info* userInfo)
{
	if (userInfo == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return B_NOT_INITIALIZED;

	uint16 type;
	uint32 rights;
	status_t status = table->GetInfo(handle, &type, &rights);
	if (status != B_OK)
		return status;

	kosm_handle_info info;
	info.handle = handle;
	info.type = type;
	info.rights = rights;

	if (user_memcpy(userInfo, &info, sizeof(info)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}
