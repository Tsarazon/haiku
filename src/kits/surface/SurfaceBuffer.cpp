/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceBuffer.h"

#include <string.h>


SurfaceBuffer::SurfaceBuffer()
	:
	id(0),
	allocSize(0),
	planeCount(1),
	areaId(-1),
	baseAddress(NULL),
	lockState(0),
	lockOwner(-1),
	seed(0),
	localUseCount(0),
	lock("surface_buffer")
{
	surface_desc_init(&desc);
	memset(planes, 0, sizeof(planes));
}
