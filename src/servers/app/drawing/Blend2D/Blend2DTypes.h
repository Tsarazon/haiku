/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Type aliases mapping AGG types to Blend2D equivalents for gradual migration
 */
#ifndef BLEND2D_TYPES_H
#define BLEND2D_TYPES_H

#include <blend2d.h>

// Forward declarations for compatibility
class BLContext;
class BLPath;
class BLImage;
class BLFont;
class BLGradient;
class BLMatrix2D;

// Core rendering type aliases (replaces AGG pipeline types)
typedef BLFormat                pixfmt;
typedef BLContext               renderer_base;
typedef BLContext               renderer_type;
typedef BLContext               renderer_bin_type;
typedef BLContext               renderer_subpix_type;
typedef BLContext               rasterizer_type;
typedef BLContext               rasterizer_subpix_type;

// Scanline type aliases (handled internally by BLContext)
typedef void*                   scanline_unpacked_type;
typedef void*                   scanline_packed_type;
typedef void*                   scanline_packed_subpix_type;
typedef void*                   scanline_unpacked_subpix_type;
typedef void*                   scanline_unpacked_masked_type;

// Path and transformation aliases
typedef BLPath                  path_storage_type;
typedef BLMatrix2D              trans_affine_type;

// Outline rendering type aliases
typedef BLContext               outline_renderer_type;
typedef BLContext               outline_rasterizer_type;

// Color and pixel format constants
#define BLEND2D_FORMAT_PRGB32   BL_FORMAT_PRGB32
#define BLEND2D_FORMAT_XRGB32   BL_FORMAT_XRGB32
#define BLEND2D_FORMAT_A8       BL_FORMAT_A8

// Composition operation mapping
enum blend2d_comp_op {
    BLEND2D_COMP_OP_SRC_OVER    = BL_COMP_OP_SRC_OVER,
    BLEND2D_COMP_OP_SRC_COPY    = BL_COMP_OP_SRC_COPY,
    BLEND2D_COMP_OP_SRC_IN      = BL_COMP_OP_SRC_IN,
    BLEND2D_COMP_OP_SRC_OUT     = BL_COMP_OP_SRC_OUT,
    BLEND2D_COMP_OP_SRC_ATOP    = BL_COMP_OP_SRC_ATOP,
    BLEND2D_COMP_OP_DST_OVER    = BL_COMP_OP_DST_OVER,
    BLEND2D_COMP_OP_DST_COPY    = BL_COMP_OP_DST_COPY,
    BLEND2D_COMP_OP_DST_IN      = BL_COMP_OP_DST_IN,
    BLEND2D_COMP_OP_DST_OUT     = BL_COMP_OP_DST_OUT,
    BLEND2D_COMP_OP_DST_ATOP    = BL_COMP_OP_DST_ATOP,
    BLEND2D_COMP_OP_XOR         = BL_COMP_OP_XOR,
    BLEND2D_COMP_OP_CLEAR       = BL_COMP_OP_CLEAR,
    BLEND2D_COMP_OP_PLUS        = BL_COMP_OP_PLUS,
    BLEND2D_COMP_OP_MINUS       = BL_COMP_OP_MINUS,
    BLEND2D_COMP_OP_MODULATE    = BL_COMP_OP_MODULATE,
    BLEND2D_COMP_OP_MULTIPLY    = BL_COMP_OP_MULTIPLY,
    BLEND2D_COMP_OP_SCREEN      = BL_COMP_OP_SCREEN,
    BLEND2D_COMP_OP_OVERLAY     = BL_COMP_OP_OVERLAY,
    BLEND2D_COMP_OP_DARKEN      = BL_COMP_OP_DARKEN,
    BLEND2D_COMP_OP_LIGHTEN     = BL_COMP_OP_LIGHTEN,
    BLEND2D_COMP_OP_COLOR_DODGE = BL_COMP_OP_COLOR_DODGE,
    BLEND2D_COMP_OP_COLOR_BURN  = BL_COMP_OP_COLOR_BURN,
    BLEND2D_COMP_OP_HARD_LIGHT  = BL_COMP_OP_HARD_LIGHT,
    BLEND2D_COMP_OP_SOFT_LIGHT  = BL_COMP_OP_SOFT_LIGHT,
    BLEND2D_COMP_OP_DIFFERENCE  = BL_COMP_OP_DIFFERENCE,
    BLEND2D_COMP_OP_EXCLUSION   = BL_COMP_OP_EXCLUSION
};

// Fill rule mapping
enum blend2d_fill_rule {
    BLEND2D_FILL_RULE_NON_ZERO  = BL_FILL_RULE_NON_ZERO,
    BLEND2D_FILL_RULE_EVEN_ODD  = BL_FILL_RULE_EVEN_ODD
};

#endif // BLEND2D_TYPES_H