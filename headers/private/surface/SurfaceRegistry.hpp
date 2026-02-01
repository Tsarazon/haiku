/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * System-wide surface registry for cross-process buffer sharing.
 * Similar to iOS IOSurface global/local use count model.
 *
 * Registry uses a shared area discoverable via find_area() to enable
 * cross-process surface lookup. First process creates the area,
 * subsequent processes clone it.
 */
#ifndef _SURFACE_REGISTRY_HPP
#define _SURFACE_REGISTRY_HPP

#include <OS.h>
#include <SurfaceTypes.hpp>

#define SURFACE_REGISTRY_MAX_ENTRIES 4096
#define SURFACE_REGISTRY_AREA_NAME "kosm_surface_registry"
#define SURFACE_ID_TOMBSTONE ((surface_id)-1)

// Compaction threshold: rehash when tombstones exceed 25% of capacity
#define SURFACE_REGISTRY_TOMBSTONE_THRESHOLD (SURFACE_REGISTRY_MAX_ENTRIES / 4)

struct SurfaceRegistryHeader {
	sem_id			lock;
	int32			entryCount;
	int32			tombstoneCount;
	uint32			_reserved[5];
};

struct SurfaceRegistryEntry {
	surface_id		id;
	int32			globalUseCount;
	team_id			ownerTeam;
	area_id			sourceArea;

	uint32			width;
	uint32			height;
	pixel_format	format;
	uint32			bytesPerRow;
	uint32			bytesPerElement;
	size_t			allocSize;
	uint32			planeCount;

	uint64			accessSecret;
	uint32			secretGeneration;
};

class SurfaceRegistry {
public:
	static	SurfaceRegistry*	Default();

			status_t			Register(surface_id id, area_id sourceArea,
									const surface_desc& desc, size_t allocSize,
									uint32 planeCount);
			status_t			Unregister(surface_id id);

			status_t			CreateAccessToken(surface_id id,
									surface_token* outToken);
			status_t			ValidateToken(const surface_token& token);
			status_t			RevokeAllAccess(surface_id id);

			status_t			LookupInfo(surface_id id,
									surface_desc* outDesc,
									area_id* outArea,
									size_t* outAllocSize = NULL,
									uint32* outPlaneCount = NULL) const;

			status_t			LookupInfoWithToken(const surface_token& token,
									surface_desc* outDesc,
									area_id* outArea,
									size_t* outAllocSize = NULL,
									uint32* outPlaneCount = NULL) const;

			status_t			IncrementGlobalUseCount(surface_id id);
			status_t			DecrementGlobalUseCount(surface_id id);
			int32				GlobalUseCount(surface_id id) const;
			bool				IsInUse(surface_id id) const;

private:
								SurfaceRegistry();
								~SurfaceRegistry();

			status_t			_InitSharedArea();
			status_t			_CreateSharedArea();
			status_t			_CloneSharedArea(area_id sourceArea);

			int32				_FindSlot(surface_id id) const;
			int32				_FindEmptySlot(surface_id id) const;
			void				_Compact();

			area_id				fRegistryArea;
			SurfaceRegistryHeader* fHeader;
			SurfaceRegistryEntry* fEntries;
			bool				fIsOwner;
};

#endif
