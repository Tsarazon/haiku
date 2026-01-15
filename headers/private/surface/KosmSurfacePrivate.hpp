/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_PRIVATE_HPP
#define _KOSM_SURFACE_PRIVATE_HPP

#include <KosmSurface.hpp>

struct SurfaceBuffer;

struct KosmSurface::Data {
	SurfaceBuffer*	buffer;
};

#endif /* _KOSM_SURFACE_PRIVATE_HPP */
