/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_ALLOCATOR_H
#define _KOSM_SURFACE_ALLOCATOR_H

#include <SurfaceTypes.h>

class KosmSurface;

class KosmSurfaceAllocator {
public:
	static	KosmSurfaceAllocator* Default();

			status_t			Allocate(const surface_desc& desc,
									KosmSurface** outSurface);
			void				Free(KosmSurface* surface);

			status_t			Lookup(surface_id id,
									KosmSurface** outSurface);

			size_t				GetPropertyMaximum(const char* property) const;
			size_t				GetPropertyAlignment(const char* property) const;

			bool				IsFormatSupported(pixel_format format) const;

private:
								KosmSurfaceAllocator();
								~KosmSurfaceAllocator();

			struct Impl;
			Impl*				fImpl;
};

#endif /* _KOSM_SURFACE_ALLOCATOR_H */
