/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * System-wide surface registry for cross-process buffer sharing.
 * Similar to iOS IOSurface global/local use count model.
 *
 * Registry uses a shared area discoverable via find_area() to enable
 * cross-process surface lookup. First process creates the area,
 * subsequent processes clone it.
 *
 * Synchronization uses kosm_mutex (robust mutex with owner-death
 * detection) instead of plain semaphores.
 */
#ifndef _KOSM_SURFACE_REGISTRY_HPP
#define _KOSM_SURFACE_REGISTRY_HPP

#include <KosmOS.h>
#include <SurfaceTypes.hpp>

#define KOSM_SURFACE_REGISTRY_MAX_ENTRIES		4096
#define KOSM_SURFACE_REGISTRY_AREA_NAME			"kosm_surface_registry"
#define KOSM_SURFACE_REGISTRY_MUTEX_NAME		"kosm_surface_registry_lock"
#define KOSM_SURFACE_ID_TOMBSTONE				((kosm_surface_id)-1)

// Compaction threshold: rehash when tombstones exceed 25% of capacity
#define KOSM_SURFACE_REGISTRY_TOMBSTONE_THRESHOLD \
	(KOSM_SURFACE_REGISTRY_MAX_ENTRIES / 4)

struct KosmSurfaceRegistryHeader {
	kosm_mutex_id	lock;
	int32			entryCount;
	int32			tombstoneCount;
	uint32			_reserved[5];
};

struct KosmSurfaceRegistryEntry {
	kosm_surface_id	id;
	int32			globalUseCount;
	team_id			ownerTeam;
	area_id			sourceArea;

	uint32			width;
	uint32			height;
	kosm_pixel_format format;
	uint32			bytesPerRow;
	uint32			bytesPerElement;
	size_t			allocSize;
	uint32			planeCount;

	uint64			accessSecret;
	uint32			secretGeneration;
};

class KosmSurfaceRegistry {
public:
	static	KosmSurfaceRegistry* Default();

			status_t			Register(kosm_surface_id id,
									area_id sourceArea,
									const KosmSurfaceDesc& desc,
									size_t allocSize, uint32 planeCount);
			status_t			Unregister(kosm_surface_id id);

			status_t			CreateAccessToken(kosm_surface_id id,
									KosmSurfaceToken* outToken);
			status_t			ValidateToken(const KosmSurfaceToken& token);
			status_t			RevokeAllAccess(kosm_surface_id id);

			status_t			LookupInfo(kosm_surface_id id,
									KosmSurfaceDesc* outDesc,
									area_id* outArea,
									size_t* outAllocSize = NULL,
									uint32* outPlaneCount = NULL) const;

			status_t			LookupInfoWithToken(
									const KosmSurfaceToken& token,
									KosmSurfaceDesc* outDesc,
									area_id* outArea,
									size_t* outAllocSize = NULL,
									uint32* outPlaneCount = NULL) const;

			status_t			IncrementGlobalUseCount(kosm_surface_id id);
			status_t			DecrementGlobalUseCount(kosm_surface_id id);
			int32				GlobalUseCount(kosm_surface_id id) const;
			bool				IsInUse(kosm_surface_id id) const;

private:
								KosmSurfaceRegistry();
								~KosmSurfaceRegistry();

			status_t			_InitSharedArea();
			status_t			_CreateSharedArea();
			status_t			_CloneSharedArea(area_id sourceArea);

			status_t			_Lock() const;
			status_t			_Unlock() const;

			int32				_FindSlot(kosm_surface_id id) const;
			int32				_FindEmptySlot(kosm_surface_id id) const;
			void				_Compact();

			area_id				fRegistryArea;
			KosmSurfaceRegistryHeader* fHeader;
			KosmSurfaceRegistryEntry* fEntries;
			bool				fIsOwner;
};

#endif
