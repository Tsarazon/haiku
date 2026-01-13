/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_BUFFER_H
#define _SURFACE_BUFFER_H

#include <Locker.h>
#include <Message.h>
#include <SurfaceTypes.h>

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

			int32				lockState;
			thread_id			lockOwner;
			uint32				seed;

			int32				localUseCount;

			BMessage			attachments;
			BLocker				lock;

private:
								SurfaceBuffer(const SurfaceBuffer&);
			SurfaceBuffer&		operator=(const SurfaceBuffer&);
};

#endif /* _SURFACE_BUFFER_H */
