/*
 * Copyright 2025 KosmOS Project. All rights reserved.
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

			status_t			Allocate(const KosmSurfaceDesc& desc,
									KosmSurface** outSurface);
			void				Free(KosmSurface* surface);

			status_t			Lookup(kosm_surface_id id,
									KosmSurface** outSurface);
			status_t			LookupOrClone(kosm_surface_id id,
									KosmSurface** outSurface);
			status_t			LookupWithToken(const KosmSurfaceToken& token,
									KosmSurface** outSurface);

			size_t				GetPropertyMaximum(const char* property) const;
			size_t				GetPropertyAlignment(const char* property) const;

			bool				IsFormatSupported(kosm_pixel_format format) const;

private:
								KosmSurfaceAllocator();
								~KosmSurfaceAllocator();

			status_t			_CreateFromClone(kosm_surface_id id,
									KosmSurface** outSurface);
			status_t			_CreateFromCloneWithToken(
									const KosmSurfaceToken& token,
									KosmSurface** outSurface);
			status_t			_CloneFromRegistry(kosm_surface_id id,
									const KosmSurfaceDesc& desc,
									area_id sourceArea,
									size_t allocSize, uint32 planeCount,
									KosmSurface** outSurface);

			struct Impl;
			Impl*				fImpl;
};

#endif
