/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_ALLOCATOR_HPP
#define _KOSM_SURFACE_ALLOCATOR_HPP

#include <OS.h>
#include <SurfaceTypes.hpp>

class KosmSurface;

class KosmSurfaceAllocator {
public:
	static	KosmSurfaceAllocator* Default();

			status_t			Allocate(const surface_desc& desc,
									KosmSurface** outSurface);
			void				Free(KosmSurface* surface);

			status_t			Lookup(surface_id id,
									KosmSurface** outSurface);

			// Create surface from cloned area (cross-process)
			status_t			CreateFromClone(surface_id id,
									area_id clonedArea,
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

#endif /* _KOSM_SURFACE_ALLOCATOR_HPP */
