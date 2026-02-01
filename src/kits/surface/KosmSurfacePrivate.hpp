/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_PRIVATE_HPP
#define _KOSM_SURFACE_PRIVATE_HPP

#include <KosmSurface.hpp>
#include "SurfaceBuffer.hpp"

struct KosmSurface::Data {
	KosmSurfaceBuffer*	buffer;
};

#endif
