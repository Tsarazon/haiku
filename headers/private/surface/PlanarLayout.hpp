/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PLANAR_LAYOUT_HPP
#define _PLANAR_LAYOUT_HPP

#include <SurfaceTypes.hpp>

uint32		planar_get_plane_count(pixel_format format);
uint32		planar_get_bytes_per_pixel(pixel_format format);
bool		planar_is_format_planar(pixel_format format);

void		planar_calculate_plane(pixel_format format, uint32 planeIndex,
				uint32 width, uint32 height, size_t strideAlignment,
				plane_info* outInfo);

size_t		planar_calculate_total_size(pixel_format format,
				uint32 width, uint32 height, size_t strideAlignment);

uint32		planar_get_component_count(pixel_format format, uint32 planeIndex);
uint32		planar_get_bit_depth(pixel_format format, uint32 planeIndex,
				uint32 componentIndex);
uint32		planar_get_bit_offset(pixel_format format, uint32 planeIndex,
				uint32 componentIndex);

#endif
