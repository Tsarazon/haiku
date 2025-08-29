/*
 * Copyright 2005-2006, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * global definitions for the Painter framework, migrated from AGG to Blend2D
 *
 */

#ifndef DEFINES_H
#define DEFINES_H

#include <blend2d.h>
#include "GlobalSubpixelSettings.h"
#include "drawing_modes/PixelFormat.h"

#define ALIASED_DRAWING 0

// Main pixel format and renderer types (Blend2D-based)
typedef BLFormat                        pixfmt;
typedef BLContext                       renderer_base;

#if ALIASED_DRAWING
    // Aliased rendering configuration (not recommended)
    typedef BLContext                   outline_renderer_type;
    typedef BLContext                   outline_rasterizer_type;
    typedef BLContext                   scanline_unpacked_type;
    typedef BLContext                   scanline_packed_type;
    typedef BLContext                   renderer_type;
#else
    // Anti-aliased rendering configuration (default)
    typedef BLContext                   outline_renderer_type;
    typedef BLContext                   outline_rasterizer_type;

    // Scanline types - Blend2D handles internally
    typedef BLContext                   scanline_unpacked_type;
    typedef BLContext                   scanline_packed_type;
    
#ifdef AVERAGE_BASED_SUBPIXEL_FILTERING
    typedef BLContext                   scanline_packed_subpix_type;
    typedef BLContext                   scanline_unpacked_subpix_type;
#else
    typedef BLContext                   scanline_packed_subpix_type;
    typedef BLContext                   scanline_unpacked_subpix_type;
#endif

    typedef BLContext                   scanline_unpacked_masked_type;
    typedef BLContext                   renderer_type;
#endif // !ALIASED_DRAWING

    // All renderer types map to BLContext
    typedef BLContext                   renderer_bin_type;
    typedef BLContext                   renderer_subpix_type;

    // Rasterizer types - Blend2D handles internally
    typedef BLContext                   rasterizer_type;
    typedef BLContext                   rasterizer_subpix_type;

#endif // DEFINES_H


