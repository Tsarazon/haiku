/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_PRIVATE_H
#define _KOSM_SURFACE_PRIVATE_H

#include <KosmSurface.h>

struct SurfaceBuffer;

struct KosmSurface::Data {
	SurfaceBuffer*	buffer;
};

#endif /* _KOSM_SURFACE_PRIVATE_H */
