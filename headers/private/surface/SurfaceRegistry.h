/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * System-wide surface registry for global use count tracking.
 * Similar to iOS IOSurface global/local use count model.
 */
#ifndef _SURFACE_REGISTRY_H
#define _SURFACE_REGISTRY_H

#include <OS.h>
#include <SurfaceTypes.h>

#define SURFACE_REGISTRY_MAX_ENTRIES 4096

// Special marker for deleted entries (linear probing tombstone)
#define SURFACE_ID_TOMBSTONE ((surface_id)-1)

struct SurfaceRegistryEntry {
	surface_id		id;			// 0 = empty, TOMBSTONE = deleted, other = valid
	int32			globalUseCount;
	team_id			ownerTeam;
	area_id			sourceArea;
};

class SurfaceRegistry {
public:
	static	SurfaceRegistry*	Default();

			status_t			Register(surface_id id, area_id sourceArea);
			status_t			Unregister(surface_id id);

			status_t			IncrementGlobalUseCount(surface_id id);
			status_t			DecrementGlobalUseCount(surface_id id);
			int32				GlobalUseCount(surface_id id) const;
			bool				IsInUse(surface_id id) const;

			status_t			LookupArea(surface_id id,
									area_id* outArea) const;

private:
								SurfaceRegistry();
								~SurfaceRegistry();

			status_t			_InitSharedArea();
			int32				_IndexFor(surface_id id) const;

			area_id				fRegistryArea;
			SurfaceRegistryEntry* fEntries;
			sem_id				fLock;
};

#endif /* _SURFACE_REGISTRY_H */
