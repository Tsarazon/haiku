/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmOS Handle Table: per-process capability table.
 *
 * Design: Zircon-style per-process handle table with generation counters.
 * All kernel objects (rays, dots, mutexes) are accessed through handles.
 * A handle is valid only within the process that owns it.
 *
 * Handle encoding (int32, always > 0):
 *   bits [0:15]  = index (1..65535, 0 is reserved)
 *   bits [16:30] = generation (0..32767, wraps)
 *   bit  [31]    = always 0
 *
 * Locking:
 *   KosmHandleTable::fLock protects the table contents.
 *   When transferring between tables, lock ordering is by address
 *   (lower pointer first).
 *
 * Reference model:
 *   Each handle entry holds one reference on the underlying object.
 *   Insert acquires, Remove releases. Transfer moves without touching refcount.
 *   For legacy objects (area_id, sem_id), no kernel reference is held;
 *   lifetime is managed by the respective subsystem.
 *
 * INTEGRATION: Team struct (team.h) must have the following field:
 *   KosmHandleTable*  kosm_handle_table;
 * Initialize to NULL in Team constructor. Allocate in Team::Create:
 *   kosm_handle_table = new(std::nothrow) KosmHandleTable();
 *   kosm_handle_table->Init();
 * Delete in Team::~Team (after kosm_ray_delete_owned):
 *   delete kosm_handle_table;
 * Reset on exec (destroy + recreate).
 */
#ifndef _KERNEL_KOSM_HANDLE_H
#define _KERNEL_KOSM_HANDLE_H


#include <KosmOS.h>
#include <util/AutoLock.h>
#include <util/KernelReferenceable.h>


struct kernel_args;

#ifdef __cplusplus
namespace BKernel {
	struct Team;
}
using BKernel::Team;
#endif


/* Handle object types (match KOSM_HANDLE_* from KosmOS.h) */

#define KOSM_HANDLE_TYPE_NONE		0x00


/* Default rights for newly created objects (kernel-private policy) */

#define KOSM_RIGHT_RAY_DEFAULT		KOSM_RIGHT_ALL
#define KOSM_RIGHT_MUTEX_DEFAULT	(KOSM_RIGHT_READ | KOSM_RIGHT_WRITE \
									| KOSM_RIGHT_TRANSFER | KOSM_RIGHT_MANAGE)


/* Handle table limits */

#define KOSM_HANDLE_MAX_PER_TEAM	4096
#define KOSM_HANDLE_INITIAL_CAPACITY	64


#ifdef __cplusplus


struct KosmHandleEntry {
	union {
		KernelReferenceable*	object;		// ray, mutex, dot (refcounted)
		int32					legacy_id;	// area, sem, fd (subsystem-managed)
	};
	uint32		rights;
	uint16		type;			// KOSM_HANDLE_RAY, _MUTEX, _AREA, ...
	uint16		generation;		// use-after-close protection
};


class KosmHandleTable {
public:
							KosmHandleTable();
							~KosmHandleTable();

	status_t				Init(uint32 capacity = KOSM_HANDLE_INITIAL_CAPACITY);

	// Insert a refcounted kernel object. Acquires one reference.
	// Returns handle value (> 0), or negative error.
	kosm_handle_t			Insert(KernelReferenceable* object,
								uint16 type, uint32 rights);

	// Insert a legacy (non-refcounted) object by ID.
	kosm_handle_t			InsertLegacy(int32 legacyID,
								uint16 type, uint32 rights);

	// Lookup: validate handle, check type and rights, return object.
	// Acquires one reference on the object before returning.
	// Caller must release when done.
	status_t				Lookup(kosm_handle_t handle,
								uint16 expectedType,
								uint32 requiredRights,
								KernelReferenceable** outObject);

	// Lookup without type check. Type is returned via outType.
	status_t				LookupAny(kosm_handle_t handle,
								uint32 requiredRights,
								KernelReferenceable** outObject,
								uint16* outType);

	// Lookup a legacy object by handle.
	status_t				LookupLegacy(kosm_handle_t handle,
								uint16 expectedType,
								uint32 requiredRights,
								int32* outLegacyID);

	// Get the entry details without acquiring a reference.
	// Caller must hold no table lock. Validates handle.
	status_t				GetInfo(kosm_handle_t handle,
								uint16* outType, uint32* outRights);

	// Remove handle. Releases one reference on the object.
	// If outObject is non-NULL, the reference is NOT released
	// but transferred to the caller.
	status_t				Remove(kosm_handle_t handle,
								KernelReferenceable** outObject = NULL,
								uint16* outType = NULL);

	// Remove legacy handle. Returns the legacy_id.
	status_t				RemoveLegacy(kosm_handle_t handle,
								int32* outLegacyID = NULL,
								uint16* outType = NULL);

	// Duplicate: create a new handle pointing to the same object.
	// New rights must be a subset of existing rights.
	// Acquires an additional reference on the object.
	kosm_handle_t			Duplicate(kosm_handle_t handle,
								uint32 newRights);

	// Transfer: atomically move handle from this table to target.
	// The entry is removed from this table and inserted into target.
	// No reference count change (reference moves with the entry).
	// Lock ordering: lower address first.
	status_t				Transfer(kosm_handle_t handle,
								KosmHandleTable* target,
								kosm_handle_t* outHandle);

	uint32					Count() const { return fCount; }
	mutex*					Lock() { return &fLock; }

	// Iteration for team cleanup. Callback receives entry and cookie.
	// Return true to continue, false to stop.
	typedef bool			(*IterateCallback)(KosmHandleEntry* entry,
								uint32 index, void* cookie);
	void					IterateEntries(IterateCallback callback,
								void* cookie);

	// Access via Team struct (team->kosm_handle_table).
	static KosmHandleTable*	TableFor(team_id team);
	static KosmHandleTable*	TableForCurrent();

private:
	status_t				_Grow();
	KosmHandleEntry*		_EntryFor(kosm_handle_t handle);
	kosm_handle_t			_AllocateSlot(uint16 type, uint32 rights);

	static inline uint32	_IndexOf(kosm_handle_t handle);
	static inline uint16	_GenerationOf(kosm_handle_t handle);
	static inline kosm_handle_t _MakeHandle(uint32 index,
								uint16 generation);

	KosmHandleEntry*		fEntries;
	uint32					fCapacity;
	uint32					fCount;
	int32					fFreeHead;		// free list: next index, -1 = empty
	mutex					fLock;
};


extern "C" {
#endif /* __cplusplus */


status_t	kosm_handle_init(struct kernel_args* args);

/* Syscalls for generic handle operations */

status_t	_user_kosm_close_handle(kosm_handle_t handle);
kosm_handle_t _user_kosm_duplicate_handle(kosm_handle_t handle,
				uint32 rights);
status_t	_user_kosm_handle_get_info(kosm_handle_t handle,
				kosm_handle_info* userInfo);


#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_KOSM_HANDLE_H */
