/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_BUFFER_HPP
#define _SURFACE_BUFFER_HPP

#include <Locker.h>
#include <Message.h>
#include <SurfaceTypes.hpp>

#define SURFACE_MAX_PLANES 4

struct SurfaceBuffer {
								SurfaceBuffer();

			surface_id			id;

			surface_desc		desc;
			size_t				allocSize;

			uint32				planeCount;
			plane_info			planes[SURFACE_MAX_PLANES];

			area_id				areaId;
			void*				baseAddress;
			bool				ownsArea;

			int32				lockCount;
			thread_id			lockOwner;
			bool				lockedReadOnly;
			uint32				seed;

			int32				localUseCount;

			surface_purgeable_state	purgeableState;
			bool				contentsPurged;

			BMessage			attachments;
			BLocker				lock;

private:
								SurfaceBuffer(const SurfaceBuffer&);
			SurfaceBuffer&		operator=(const SurfaceBuffer&);
};

#endif
