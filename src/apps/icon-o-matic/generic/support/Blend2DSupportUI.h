/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D support functions for Icon-O-Matic UI
 */
#ifndef BLEND2D_SUPPORT_UI_H
#define BLEND2D_SUPPORT_UI_H

#include <SupportDefs.h>
#include <blend2d.h>

// Convert Haiku cap modes to Blend2D
BLStrokeCap convert_cap_mode(uint32 mode);

// Convert Haiku join modes to Blend2D  
BLStrokeJoin convert_join_mode(uint32 mode);

// Convert Blend2D cap modes back to Haiku
uint32 convert_cap_mode_from_blend2d(BLStrokeCap cap);

// Convert Blend2D join modes back to Haiku
uint32 convert_join_mode_from_blend2d(BLStrokeJoin join);

#endif // BLEND2D_SUPPORT_UI_H