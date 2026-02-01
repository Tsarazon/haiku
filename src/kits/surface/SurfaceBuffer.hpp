/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_BUFFER_HPP
#define _KOSM_SURFACE_BUFFER_HPP

#include <Locker.h>
#include <Message.h>
#include <SurfaceTypes.hpp>

#define KOSM_SURFACE_MAX_PLANES 4

struct KosmSurfaceBuffer {
								KosmSurfaceBuffer();

			kosm_surface_id		id;

			KosmSurfaceDesc		desc;
			size_t				allocSize;

			uint32				planeCount;
			KosmPlaneInfo		planes[KOSM_SURFACE_MAX_PLANES];

			area_id				areaId;
			void*				baseAddress;
			bool				ownsArea;

			int32				lockCount;
			thread_id			lockOwner;
			bool				lockedReadOnly;
			uint32				seed;

			int32				localUseCount;

			kosm_purgeable_state purgeableState;
			bool				contentsPurged;

			BMessage			attachments;
			BLocker				lock;

private:
								KosmSurfaceBuffer(const KosmSurfaceBuffer&);
			KosmSurfaceBuffer&	operator=(const KosmSurfaceBuffer&);
};

#endif
