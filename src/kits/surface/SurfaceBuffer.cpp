/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceBuffer.hpp"

#include <string.h>


KosmSurfaceBuffer::KosmSurfaceBuffer()
	:
	id(0),
	allocSize(0),
	planeCount(1),
	areaId(-1),
	baseAddress(NULL),
	ownsArea(true),
	lockCount(0),
	lockOwner(-1),
	lockedReadOnly(false),
	seed(0),
	localUseCount(0),
	purgeableState(KOSM_PURGEABLE_NON_VOLATILE),
	contentsPurged(false),
	lock("kosm_surface_buffer")
{
	memset(planes, 0, sizeof(planes));
}
