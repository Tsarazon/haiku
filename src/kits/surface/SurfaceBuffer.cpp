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
	baseAddress(nullptr),
	ownsArea(true),
	isOriginal(true),
	lockCount(0),
	lockOwner(-1),
	lockedReadOnly(false),
	seed(0),
	waitSem(create_sem(0, "kosm_surface_wait")),
	localUseCount(0),
	purgeableState(KOSM_PURGEABLE_NON_VOLATILE),
	contentsPurged(false),
	lock("kosm_surface_buffer")
{
	memset(planes, 0, sizeof(planes));
}


KosmSurfaceBuffer::~KosmSurfaceBuffer()
{
	if (waitSem >= 0)
		delete_sem(waitSem);
}
