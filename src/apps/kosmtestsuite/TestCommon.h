/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Unified test suite infrastructure for KosmOS kernel primitives.
 */
#ifndef KOSM_TEST_COMMON_H
#define KOSM_TEST_COMMON_H


#include <Application.h>
#include <Button.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Message.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include <OS.h>
#include <image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Maximum log lines displayed in GUI
enum { kMaxLines = 600 };


// Global state (defined in TestCommon.cpp)
extern FILE*	sTraceFile;
extern int		sPassCount;
extern int		sFailCount;
extern char		sLines[kMaxLines][256];
extern int		sLineCount;


// Trace to log file and kernel serial debug output
void	trace(const char* fmt, ...);
void	trace_call(const char* func, status_t result);
void	trace_call_id(const char* func, int32 result);

// Log line to GUI display
void	log_line(const char* fmt, ...);

// Reset counters and log between suite runs
void	reset_results();


// TEST_ASSERT macro — measures time, logs PASS/FAIL to trace file
#define TEST_ASSERT(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			trace("  FAIL: %s  (line %d, %lld us)\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
		} \
	} while (0)

// TEST_REQUIRE — same as TEST_ASSERT but returns from the calling
// function on failure (prevents NULL dereference after failed alloc)
#define TEST_REQUIRE(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			trace("  FAIL: %s  (line %d, %lld us) ** ABORT **\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
			return; \
		} \
	} while (0)


// Test registration

typedef void (*test_func_t)();

struct TestEntry {
	const char*		name;
	test_func_t		func;
	const char*		category;
};

struct TestSuite {
	const char*		name;
	const TestEntry*	tests;
	int				count;
};


// Run a single test, log results to GUI and trace file
void	run_test(const TestEntry& entry);


// Suite registration functions (implemented in each test file)
TestSuite	get_ray_test_suite();
TestSuite	get_mutex_test_suite();
TestSuite	get_surface_test_suite();
TestSuite	get_dot_test_suite();
TestSuite	get_scheduler_test_suite();

// Child helper for cross-process dot tests (runs when invoked with --dot-child)
int			dot_child_helper();


// System diagnostics — prints CPU count, RAM, timer resolution to serial
void	print_system_info();


// Benchmark helper — runs func N times with warmup, reports min/max/median
struct BenchStats {
	double		min_ops;
	double		max_ops;
	double		median_ops;
	int			runs;
	int			warmup;
};

typedef double (*bench_func_t)();	// returns ops/sec for one iteration
BenchStats	run_benchmark(bench_func_t func, int runs, int warmup);


// Trace file management
void	open_trace(const char* filename);
void	close_trace();


// GUI message codes
enum {
	kMsgRunRay		= 'rRAY',
	kMsgRunMutex	= 'rMTX',
	kMsgRunSurface	= 'rSRF',
	kMsgRunDot		= 'rDOT',
	kMsgRunSched	= 'rSCH',
	kMsgRunAll		= 'rALL',
};


#endif // KOSM_TEST_COMMON_H
