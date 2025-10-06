/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef UTILITY_H
#define UTILITY_H

#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))


//==============================================================================
// Unified Logging System for Intel Extreme Graphics Driver
//==============================================================================

// Logging levels
#define INTEL_LOG_LEVEL_ERROR   0
#define INTEL_LOG_LEVEL_WARNING 1
#define INTEL_LOG_LEVEL_INFO    2
#define INTEL_LOG_LEVEL_DEBUG   3
#define INTEL_LOG_LEVEL_TRACE   4

// Current logging level (can be overridden per file or via driver settings)
#ifndef INTEL_LOG_LEVEL
#	ifdef DEBUG
#		define INTEL_LOG_LEVEL INTEL_LOG_LEVEL_DEBUG
#	else
#		define INTEL_LOG_LEVEL INTEL_LOG_LEVEL_INFO
#	endif
#endif

// Base macro for logging with level checking
#define INTEL_LOG(level, prefix, fmt, args...) \
	do { \
		if (level <= INTEL_LOG_LEVEL) \
			_sPrintf("intel_extreme: " prefix fmt, ##args); \
	} while (0)

// Level-specific logging macros (can be used as drop-in replacements)
#ifndef ERROR
#	define ERROR(fmt, args...)   INTEL_LOG(INTEL_LOG_LEVEL_ERROR, "[ERROR] ", fmt, ##args)
#endif

#ifndef WARNING
#	define WARNING(fmt, args...) INTEL_LOG(INTEL_LOG_LEVEL_WARNING, "[WARN] ", fmt, ##args)
#endif

#ifndef INFO
#	define INFO(fmt, args...)    INTEL_LOG(INTEL_LOG_LEVEL_INFO, "", fmt, ##args)
#endif

#ifndef TRACE
#	define TRACE(fmt, args...)   INTEL_LOG(INTEL_LOG_LEVEL_DEBUG, "", fmt, ##args)
#endif

#ifndef CALLED
#	define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)
#endif

// Macro to check if TRACE is enabled (for conditional code)
#define TRACE_ENABLED (INTEL_LOG_LEVEL >= INTEL_LOG_LEVEL_DEBUG)

// Convenience macros for common patterns
#define TRACE_ENTER() TRACE("%s: enter\n", __func__)
#define TRACE_EXIT()  TRACE("%s: exit\n", __func__)
#define TRACE_EXIT_RES(res) TRACE("%s: exit (result: %s)\n", __func__, strerror(res))

#endif	/* UTILITY_H */
