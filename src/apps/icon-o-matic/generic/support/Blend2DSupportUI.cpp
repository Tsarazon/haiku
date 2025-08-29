/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D support functions for Icon-O-Matic UI
 */

#include "Blend2DSupportUI.h"

#include "ui_defines.h"

BLStrokeCap
convert_cap_mode(uint32 mode)
{
    BLStrokeCap blCap = BL_STROKE_CAP_BUTT;
    switch (mode) {
        case CAP_MODE_BUTT:
            blCap = BL_STROKE_CAP_BUTT;
            break;
        case CAP_MODE_SQUARE:
            blCap = BL_STROKE_CAP_SQUARE;
            break;
        case CAP_MODE_ROUND:
            blCap = BL_STROKE_CAP_ROUND;
            break;
    }
    return blCap;
}

BLStrokeJoin
convert_join_mode(uint32 mode)
{
    BLStrokeJoin blJoin = BL_STROKE_JOIN_MITER_CLIP;
    switch (mode) {
        case JOIN_MODE_MITER:
            blJoin = BL_STROKE_JOIN_MITER_CLIP;
            break;
        case JOIN_MODE_ROUND:
            blJoin = BL_STROKE_JOIN_ROUND;
            break;
        case JOIN_MODE_BEVEL:
            blJoin = BL_STROKE_JOIN_BEVEL;
            break;
    }
    return blJoin;
}

uint32
convert_cap_mode_from_blend2d(BLStrokeCap cap)
{
    uint32 mode = CAP_MODE_BUTT;
    switch (cap) {
        case BL_STROKE_CAP_BUTT:
            mode = CAP_MODE_BUTT;
            break;
        case BL_STROKE_CAP_SQUARE:
            mode = CAP_MODE_SQUARE;
            break;
        case BL_STROKE_CAP_ROUND:
            mode = CAP_MODE_ROUND;
            break;
        default:
            mode = CAP_MODE_BUTT;
            break;
    }
    return mode;
}

uint32
convert_join_mode_from_blend2d(BLStrokeJoin join)
{
    uint32 mode = JOIN_MODE_MITER;
    switch (join) {
        case BL_STROKE_JOIN_MITER_CLIP:
        case BL_STROKE_JOIN_MITER_BEVEL:
        case BL_STROKE_JOIN_MITER_ROUND:
            mode = JOIN_MODE_MITER;
            break;
        case BL_STROKE_JOIN_ROUND:
            mode = JOIN_MODE_ROUND;
            break;
        case BL_STROKE_JOIN_BEVEL:
            mode = JOIN_MODE_BEVEL;
            break;
        default:
            mode = JOIN_MODE_MITER;
            break;
    }
    return mode;
}