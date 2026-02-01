/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_PLANAR_LAYOUT_HPP
#define _KOSM_PLANAR_LAYOUT_HPP

#include <SurfaceTypes.hpp>

uint32		kosm_planar_get_plane_count(kosm_pixel_format format);
uint32		kosm_planar_get_bytes_per_pixel(kosm_pixel_format format);
bool		kosm_planar_is_planar(kosm_pixel_format format);

void		kosm_planar_calculate_plane(kosm_pixel_format format,
				uint32 planeIndex, uint32 width, uint32 height,
				size_t strideAlignment, KosmPlaneInfo* outInfo);

size_t		kosm_planar_calculate_total_size(kosm_pixel_format format,
				uint32 width, uint32 height, size_t strideAlignment);

uint32		kosm_planar_get_component_count(kosm_pixel_format format,
				uint32 planeIndex);
uint32		kosm_planar_get_bit_depth(kosm_pixel_format format,
				uint32 planeIndex, uint32 componentIndex);
uint32		kosm_planar_get_bit_offset(kosm_pixel_format format,
				uint32 planeIndex, uint32 componentIndex);

#endif
