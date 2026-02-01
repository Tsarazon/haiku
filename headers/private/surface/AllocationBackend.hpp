/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_ALLOCATION_BACKEND_HPP
#define _KOSM_ALLOCATION_BACKEND_HPP

#include <SurfaceTypes.hpp>

struct KosmSurfaceBuffer;

class KosmAllocationBackend {
public:
	virtual						~KosmAllocationBackend();

	virtual	status_t			Allocate(const KosmSurfaceDesc& desc,
									KosmSurfaceBuffer** outBuffer) = 0;
	virtual	void				Free(KosmSurfaceBuffer* buffer) = 0;

	virtual	status_t			Map(KosmSurfaceBuffer* buffer) = 0;
	virtual	status_t			Unmap(KosmSurfaceBuffer* buffer) = 0;

	virtual	size_t				GetStrideAlignment(kosm_pixel_format format) = 0;
	virtual	size_t				GetMaxWidth() = 0;
	virtual	size_t				GetMaxHeight() = 0;
	virtual	bool				SupportsFormat(kosm_pixel_format format) = 0;
	virtual	bool				SupportsUsage(uint32 usage) = 0;
};

#endif
