/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ALLOCATION_BACKEND_H
#define _ALLOCATION_BACKEND_H

#include <SurfaceTypes.h>

struct SurfaceBuffer;

class AllocationBackend {
public:
	virtual						~AllocationBackend();

	virtual	status_t			Allocate(const surface_desc& desc,
									SurfaceBuffer** outBuffer) = 0;
	virtual	void				Free(SurfaceBuffer* buffer) = 0;

	virtual	status_t			Map(SurfaceBuffer* buffer) = 0;
	virtual	status_t			Unmap(SurfaceBuffer* buffer) = 0;

	virtual	size_t				GetStrideAlignment(pixel_format format) = 0;
	virtual	size_t				GetMaxWidth() = 0;
	virtual	size_t				GetMaxHeight() = 0;
	virtual	bool				SupportsFormat(pixel_format format) = 0;
	virtual	bool				SupportsUsage(uint32 usage) = 0;
};

#endif /* _ALLOCATION_BACKEND_H */
