/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D Debug Logging System
 * Provides unified debug logging with compile-time control
 */
#ifndef BLEND2D_DEBUG_H
#define BLEND2D_DEBUG_H

#include <blend2d.h>

#ifdef DEBUG
	#include <Debug.h>
	#define BLEND2D_ERROR(x...) debug_printf("[Blend2D ERROR] " x)
	#define BLEND2D_WARNING(x...) debug_printf("[Blend2D WARN] " x)
	#define BLEND2D_TRACE(x...) debug_printf("[Blend2D] " x)
#else
	#define BLEND2D_ERROR(x...)
	#define BLEND2D_WARNING(x...)
	#define BLEND2D_TRACE(x...)
#endif

// Macro for checking BLResult and executing action on failure
#define BLEND2D_CHECK(expr, action) \
	do { \
		BLResult _result = (expr); \
		if (_result != BL_SUCCESS) { \
			BLEND2D_ERROR("%s:%d: %s failed with code %d\n", \
				__FILE__, __LINE__, #expr, (int)_result); \
			action; \
		} \
	} while (0)

// Macro for checking BLResult with no action (just logging)
#define BLEND2D_CHECK_WARN(expr) \
	do { \
		BLResult _result = (expr); \
		if (_result != BL_SUCCESS) { \
			BLEND2D_WARNING("%s:%d: %s failed with code %d\n", \
				__FILE__, __LINE__, #expr, (int)_result); \
		} \
	} while (0)

#endif // BLEND2D_DEBUG_H
