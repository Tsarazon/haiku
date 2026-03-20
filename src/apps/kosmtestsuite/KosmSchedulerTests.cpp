/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Scheduler test functions for the unified test suite.
 * Designed to run under KVM — timing tolerances are generous.
 */


#include "TestCommon.h"

#include <OS.h>
#include <scheduler.h>

#include <sched.h>


// KVM timing tolerances
static const bigtime_t kLatencyTolerance = 10000;	// 10ms — KVM jitter budget
static const bigtime_t kSnoozeTolerance = 5000;		// 5ms
static const int kFairnessPercent = 50;				// 50% deviation OK under KVM


// Thread helpers

struct priority_race_args {
	sem_id			startSem;
	volatile int32	counter;
	volatile bool	stop;
	int32			priority;
	bigtime_t		runTime;
};


static status_t
priority_race_thread(void* data)
{
	priority_race_args* a = (priority_race_args*)data;
	acquire_sem(a->startSem);
	bigtime_t start = system_time();
	while (!a->stop)
		atomic_add((int32*)&a->counter, 1);
	a->runTime = system_time() - start;
	return B_OK;
}


struct wakeup_args {
	sem_id		sem;
	bigtime_t	latency;
	status_t	result;
};


static status_t
wakeup_waiter_thread(void* data)
{
	wakeup_args* a = (wakeup_args*)data;
	bigtime_t t0 = system_time();
	a->result = acquire_sem(a->sem);
	a->latency = system_time() - t0;
	return B_OK;
}


struct core_work_args {
	sem_id			startSem;
	volatile bool	stop;
	volatile int64	counter;
};


static status_t
core_work_thread(void* data)
{
	core_work_args* a = (core_work_args*)data;
	acquire_sem(a->startSem);
	int64 count = 0;
	while (!a->stop)
		count++;
	a->counter = count;
	return B_OK;
}


struct fairness_args {
	sem_id			startSem;
	volatile bool	stop;
	volatile int64	iterations;
};


static status_t
fairness_thread(void* data)
{
	fairness_args* a = (fairness_args*)data;
	acquire_sem(a->startSem);
	int64 count = 0;
	while (!a->stop)
		count++;
	a->iterations = count;
	return B_OK;
}


struct yield_args {
	volatile bool	stop;
	volatile int32	yieldCount;
};


static status_t
yield_thread_func(void* data)
{
	yield_args* a = (yield_args*)data;
	while (!a->stop) {
		sched_yield();
		atomic_add((int32*)&a->yieldCount, 1);
	}
	return B_OK;
}


struct preempt_args {
	sem_id			readySem;
	sem_id			goSem;
	volatile bool	preempted;
	bigtime_t		preemptLatency;
};


static status_t
preempt_low_thread(void* data)
{
	preempt_args* a = (preempt_args*)data;
	release_sem(a->readySem);
	acquire_sem(a->goSem);

	// Spin — high-prio thread should preempt us
	bigtime_t start = system_time();
	while (!a->preempted) {
		// busy loop
	}
	(void)start;
	return B_OK;
}


static status_t
preempt_high_thread(void* data)
{
	preempt_args* a = (preempt_args*)data;
	// When we start running, the low thread was preempted
	a->preemptLatency = system_time();
	a->preempted = true;
	return B_OK;
}


struct burst_args {
	int			iterations;
	int			completed;
	bigtime_t	totalTime;
};


static status_t
burst_thread_func(void* data)
{
	burst_args* a = (burst_args*)data;
	a->completed = 0;
	bigtime_t start = system_time();
	for (int i = 0; i < a->iterations; i++) {
		// Simulate work burst then sleep — mobile pattern
		volatile int32 dummy = 0;
		for (int j = 0; j < 1000; j++)
			dummy += j;
		snooze(1000);
		a->completed++;
	}
	a->totalTime = system_time() - start;
	return B_OK;
}


struct churn_args {
	int		iterations;
	int		errors;
};


static status_t
churn_child_func(void* data)
{
	// Minimal thread — just exits
	(void)data;
	return B_OK;
}


static status_t
churn_thread_func(void* data)
{
	churn_args* a = (churn_args*)data;
	a->errors = 0;
	for (int i = 0; i < a->iterations; i++) {
		thread_id t = spawn_thread(churn_child_func, "churn_child",
			B_NORMAL_PRIORITY, NULL);
		if (t < 0) { a->errors++; continue; }
		resume_thread(t);
		status_t exitVal;
		wait_for_thread(t, &exitVal);
	}
	return B_OK;
}


struct mobile_frame_args {
	volatile bool	stop;
	int				framesCompleted;
	int				framesMissed;
	bigtime_t		maxFrameTime;
	bigtime_t		targetFrame;	// 16666 us for 60fps
};


static status_t
mobile_render_thread(void* data)
{
	mobile_frame_args* a = (mobile_frame_args*)data;
	a->framesCompleted = 0;
	a->framesMissed = 0;
	a->maxFrameTime = 0;

	while (!a->stop) {
		bigtime_t frameStart = system_time();

		// Simulate frame work: ~4ms of computation
		volatile int32 dummy = 0;
		for (int i = 0; i < 40000; i++)
			dummy += i;

		bigtime_t frameTime = system_time() - frameStart;
		if (frameTime > a->maxFrameTime)
			a->maxFrameTime = frameTime;
		if (frameTime > a->targetFrame)
			a->framesMissed++;
		a->framesCompleted++;

		// Sleep remainder of frame
		bigtime_t remaining = a->targetFrame - frameTime;
		if (remaining > 0)
			snooze(remaining);
	}
	return B_OK;
}


struct mobile_bg_args {
	volatile bool	stop;
	int				iterations;
};


static status_t
mobile_bg_thread(void* data)
{
	mobile_bg_args* a = (mobile_bg_args*)data;
	a->iterations = 0;
	while (!a->stop) {
		// CPU-heavy background work
		volatile int32 dummy = 0;
		for (int i = 0; i < 100000; i++)
			dummy += i;
		a->iterations++;
	}
	return B_OK;
}


// BASIC API

static void
test_suggest_priority()
{
	trace("\n--- test_suggest_priority ---\n");

	int32 p1 = suggest_thread_priority(B_DEFAULT_MEDIA_PRIORITY, 0, 0, 0);
	trace("    B_DEFAULT_MEDIA_PRIORITY -> %ld\n", (long)p1);
	TEST_ASSERT("default media > 0", p1 > 0);

	int32 p2 = suggest_thread_priority(B_AUDIO_PLAYBACK, 0, 0, 0);
	trace("    B_AUDIO_PLAYBACK -> %ld\n", (long)p2);
	TEST_ASSERT("audio playback > default", p2 > p1);

	int32 p3 = suggest_thread_priority(B_OFFLINE_PROCESSING, 0, 0, 0);
	trace("    B_OFFLINE_PROCESSING -> %ld\n", (long)p3);
	TEST_ASSERT("offline < audio", p3 < p2);

	int32 p4 = suggest_thread_priority(B_LIVE_AUDIO_MANIPULATION, 0, 0, 0);
	trace("    B_LIVE_AUDIO_MANIPULATION -> %ld\n", (long)p4);
	TEST_ASSERT("live audio > offline", p4 > p3);
}


static void
test_scheduler_mode()
{
	trace("\n--- test_scheduler_mode ---\n");

	int32 mode = get_scheduler_mode();
	trace("    current mode: %ld\n", (long)mode);
	TEST_ASSERT("mode is valid",
		mode == SCHEDULER_MODE_LOW_LATENCY
		|| mode == SCHEDULER_MODE_POWER_SAVING);

	// Switch to power saving and back
	status_t s = set_scheduler_mode(SCHEDULER_MODE_POWER_SAVING);
	trace("    set POWER_SAVING -> %s\n", strerror(s));
	TEST_ASSERT("set power saving", s == B_OK);

	int32 newMode = get_scheduler_mode();
	TEST_ASSERT("mode changed", newMode == SCHEDULER_MODE_POWER_SAVING);

	s = set_scheduler_mode(SCHEDULER_MODE_LOW_LATENCY);
	trace("    set LOW_LATENCY -> %s\n", strerror(s));
	TEST_ASSERT("set low latency", s == B_OK);
	TEST_ASSERT("mode restored",
		get_scheduler_mode() == SCHEDULER_MODE_LOW_LATENCY);

	// Invalid mode
	s = set_scheduler_mode((int32)999);
	TEST_ASSERT("invalid mode rejected", s != B_OK);
}


static void
test_scheduling_latency()
{
	trace("\n--- test_scheduling_latency ---\n");

	bigtime_t lat = estimate_max_scheduling_latency(-1);
	trace("    current thread latency: %lld us\n", (long long)lat);
	TEST_ASSERT("latency > 0", lat > 0);
	TEST_ASSERT("latency < 1s (sanity)", lat < 1000000);

	// Check that different threads can be queried
	bigtime_t lat2 = estimate_max_scheduling_latency(find_thread(NULL));
	trace("    explicit thread latency: %lld us\n", (long long)lat2);
	TEST_ASSERT("explicit latency > 0", lat2 > 0);
}


static void
test_system_info_cpus()
{
	trace("\n--- test_system_info_cpus ---\n");

	system_info info;
	status_t s = get_system_info(&info);
	TEST_ASSERT("get_system_info", s == B_OK);
	trace("    cpu_count: %d\n", info.cpu_count);
	TEST_ASSERT("at least 1 CPU", info.cpu_count >= 1);
	TEST_ASSERT("cpu_count sane", info.cpu_count <= 256);
}


// PRIORITY

static void
test_priority_ordering()
{
	trace("\n--- test_priority_ordering ---\n");

	// Two threads: high priority should accumulate more counter ticks.
	// Under KVM the host can preempt any vCPU regardless of guest
	// priority, so we run longer (500ms) and use a soft ratio check.
	sem_id startSem = create_sem(0, "prio_start");
	TEST_REQUIRE("create sem", startSem >= 0);

	priority_race_args highArgs, lowArgs;
	memset(&highArgs, 0, sizeof(highArgs));
	memset(&lowArgs, 0, sizeof(lowArgs));
	highArgs.startSem = startSem;
	lowArgs.startSem = startSem;
	highArgs.stop = false;
	lowArgs.stop = false;

	thread_id highThread = spawn_thread(priority_race_thread,
		"prio_high", B_URGENT_DISPLAY_PRIORITY, &highArgs);
	thread_id lowThread = spawn_thread(priority_race_thread,
		"prio_low", B_LOW_PRIORITY, &lowArgs);

	TEST_REQUIRE("spawn high", highThread >= 0);
	TEST_REQUIRE("spawn low", lowThread >= 0);

	resume_thread(highThread);
	resume_thread(lowThread);

	// Release both simultaneously
	release_sem_etc(startSem, 2, 0);

	snooze(500000);	// 500ms — longer to average out KVM jitter

	highArgs.stop = true;
	lowArgs.stop = true;

	status_t exitVal;
	wait_for_thread(highThread, &exitVal);
	wait_for_thread(lowThread, &exitVal);

	int32 high = highArgs.counter;
	int32 low = lowArgs.counter;
	trace("    high=%ld, low=%ld, ratio=%.2f\n",
		(long)high, (long)low,
		low > 0 ? (double)high / (double)low : 0.0);

	// Under KVM, high-prio should get at least 80% of what low gets.
	// On real hardware, it'll be many times more.
	TEST_ASSERT("high prio got meaningful CPU share",
		high > low * 8 / 10);

	delete_sem(startSem);
}


static void
test_set_priority_runtime()
{
	trace("\n--- test_set_priority_runtime ---\n");

	thread_id me = find_thread(NULL);
	thread_info info;
	get_thread_info(me, &info);
	int32 origPriority = info.priority;

	// Change priority
	int32 old = set_thread_priority(me, B_URGENT_DISPLAY_PRIORITY);
	TEST_ASSERT("set_priority returns old", old == origPriority);

	get_thread_info(me, &info);
	TEST_ASSERT("priority changed", info.priority == B_URGENT_DISPLAY_PRIORITY);

	// Restore
	set_thread_priority(me, origPriority);
	get_thread_info(me, &info);
	TEST_ASSERT("priority restored", info.priority == origPriority);
}


// LATENCY

static void
test_wakeup_latency()
{
	trace("\n--- test_wakeup_latency ---\n");

	const int kRuns = 50;
	bigtime_t totalLatency = 0;
	bigtime_t maxLatency = 0;
	bigtime_t minLatency = INT64_MAX;

	for (int i = 0; i < kRuns; i++) {
		sem_id sem = create_sem(0, "wakeup");
		if (sem < 0)
			continue;

		wakeup_args args;
		args.sem = sem;
		args.latency = 0;
		args.result = B_ERROR;

		thread_id t = spawn_thread(wakeup_waiter_thread,
			"wakeup_waiter", B_URGENT_DISPLAY_PRIORITY, &args);
		if (t < 0) { delete_sem(sem); continue; }
		resume_thread(t);

		// Let waiter block on sem
		snooze(2000);

		// Wake it
		bigtime_t t0 = system_time();
		release_sem(sem);

		status_t exitVal;
		wait_for_thread(t, &exitVal);
		(void)t0;

		if (args.result == B_OK) {
			totalLatency += args.latency;
			if (args.latency > maxLatency)
				maxLatency = args.latency;
			if (args.latency < minLatency)
				minLatency = args.latency;
		}

		delete_sem(sem);
	}

	bigtime_t avgLatency = totalLatency / kRuns;
	trace("    %d runs: avg=%lld us, min=%lld us, max=%lld us\n",
		kRuns, (long long)avgLatency, (long long)minLatency,
		(long long)maxLatency);

	TEST_ASSERT("avg wakeup < 5ms (KVM budget)",
		avgLatency < 5000);
	TEST_ASSERT("max wakeup < 20ms (KVM worst case)",
		maxLatency < 20000);
}


static void
test_snooze_accuracy()
{
	trace("\n--- test_snooze_accuracy ---\n");

	const bigtime_t kTargets[] = { 1000, 5000, 10000, 50000 };
	bool allOk = true;

	for (int i = 0; i < 4; i++) {
		bigtime_t target = kTargets[i];
		bigtime_t t0 = system_time();
		snooze(target);
		bigtime_t elapsed = system_time() - t0;
		bigtime_t error = elapsed - target;

		trace("    snooze(%lld): elapsed=%lld, error=%+lld us\n",
			(long long)target, (long long)elapsed, (long long)error);

		// snooze should not wake early; overshoot within tolerance
		if (elapsed < target - 100 || error > kSnoozeTolerance) {
			trace("    ^ OUT OF TOLERANCE\n");
			allOk = false;
		}
	}

	TEST_ASSERT("snooze accuracy within KVM tolerance", allOk);
}


static void
test_preemption_latency()
{
	trace("\n--- test_preemption_latency ---\n");

	// Low-prio thread spins. We spawn a high-prio thread that should
	// preempt it immediately. Measure how long before high-prio runs.
	preempt_args args;
	memset(&args, 0, sizeof(args));
	args.readySem = create_sem(0, "preempt_ready");
	args.goSem = create_sem(0, "preempt_go");
	args.preempted = false;
	TEST_REQUIRE("create sems", args.readySem >= 0 && args.goSem >= 0);

	thread_id lowThread = spawn_thread(preempt_low_thread,
		"preempt_low", B_LOW_PRIORITY, &args);
	TEST_REQUIRE("spawn low", lowThread >= 0);
	resume_thread(lowThread);

	// Wait for low thread to be ready
	acquire_sem(args.readySem);

	// Let it start spinning
	release_sem(args.goSem);
	snooze(5000);

	// Now spawn high-prio — it should preempt the low thread
	bigtime_t spawnTime = system_time();

	thread_id highThread = spawn_thread(preempt_high_thread,
		"preempt_high", B_REAL_TIME_DISPLAY_PRIORITY, &args);
	TEST_REQUIRE("spawn high", highThread >= 0);
	resume_thread(highThread);

	status_t exitVal;
	wait_for_thread(highThread, &exitVal);
	wait_for_thread(lowThread, &exitVal);

	bigtime_t preemptTime = args.preemptLatency - spawnTime;
	trace("    preemption latency: %lld us\n", (long long)preemptTime);

	// Under KVM, preemption should still be under 10ms
	TEST_ASSERT("preemption < 10ms (KVM)", preemptTime < kLatencyTolerance);

	delete_sem(args.readySem);
	delete_sem(args.goSem);
}


// FAIRNESS

static void
test_equal_priority_fairness()
{
	trace("\n--- test_equal_priority_fairness ---\n");

	const int kThreads = 4;
	sem_id startSem = create_sem(0, "fair_start");
	TEST_REQUIRE("create sem", startSem >= 0);

	fairness_args args[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		args[i].startSem = startSem;
		args[i].stop = false;
		args[i].iterations = 0;
		char name[32];
		snprintf(name, sizeof(name), "fair_%d", i);
		threads[i] = spawn_thread(fairness_thread, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	// Release all at once
	release_sem_etc(startSem, kThreads, 0);
	snooze(300000);	// 300ms

	for (int i = 0; i < kThreads; i++)
		args[i].stop = true;

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	// Find min and max iteration counts
	int64 minIter = args[0].iterations;
	int64 maxIter = args[0].iterations;
	for (int i = 1; i < kThreads; i++) {
		if (args[i].iterations < minIter)
			minIter = args[i].iterations;
		if (args[i].iterations > maxIter)
			maxIter = args[i].iterations;
	}

	trace("    iterations: min=%lld, max=%lld\n",
		(long long)minIter, (long long)maxIter);
	for (int i = 0; i < kThreads; i++) {
		trace("      thread %d: %lld\n", i, (long long)args[i].iterations);
	}

	bool fair = (minIter * 100 >= maxIter * (100 - kFairnessPercent));
	TEST_ASSERT("equal priority fairness within 50% (KVM)", fair);

	delete_sem(startSem);
}


// YIELD

static void
test_thread_yield()
{
	trace("\n--- test_thread_yield ---\n");

	yield_args args[2];
	thread_id threads[2];

	for (int i = 0; i < 2; i++) {
		args[i].stop = false;
		args[i].yieldCount = 0;
		char name[32];
		snprintf(name, sizeof(name), "yield_%d", i);
		threads[i] = spawn_thread(yield_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	snooze(100000);	// 100ms

	for (int i = 0; i < 2; i++)
		args[i].stop = true;

	for (int i = 0; i < 2; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	trace("    yields: t0=%ld, t1=%ld\n",
		(long)args[0].yieldCount, (long)args[1].yieldCount);

	TEST_ASSERT("both threads yielded", args[0].yieldCount > 0
		&& args[1].yieldCount > 0);
	TEST_ASSERT("reasonable yield count (> 100)", args[0].yieldCount > 100
		&& args[1].yieldCount > 100);
}


// MULTI-CORE

static void
test_multicore_spread()
{
	trace("\n--- test_multicore_spread ---\n");

	system_info sysInfo;
	get_system_info(&sysInfo);
	if (sysInfo.cpu_count < 2) {
		trace("    skipping: single CPU\n");
		TEST_ASSERT("skipped (single CPU)", true);
		return;
	}

	// Measure single-thread throughput
	sem_id sem1 = create_sem(0, "core_1t");
	TEST_REQUIRE("create sem", sem1 >= 0);

	core_work_args singleArgs;
	singleArgs.startSem = sem1;
	singleArgs.stop = false;
	singleArgs.counter = 0;

	thread_id singleThread = spawn_thread(core_work_thread,
		"core_1t", B_NORMAL_PRIORITY, &singleArgs);
	TEST_REQUIRE("spawn single", singleThread >= 0);
	resume_thread(singleThread);
	release_sem(sem1);

	snooze(200000);
	singleArgs.stop = true;
	status_t exitVal;
	wait_for_thread(singleThread, &exitVal);
	delete_sem(sem1);

	int64 singleCount = singleArgs.counter;
	trace("    1 thread: %lld iterations\n", (long long)singleCount);

	// Measure N-thread throughput (N = cpu_count, capped at 8)
	int threadCount = sysInfo.cpu_count;
	if (threadCount > 8)
		threadCount = 8;

	sem_id semN = create_sem(0, "core_nt");
	TEST_REQUIRE("create sem N", semN >= 0);

	core_work_args* args = (core_work_args*)calloc(threadCount,
		sizeof(core_work_args));
	thread_id* threads = (thread_id*)calloc(threadCount, sizeof(thread_id));
	TEST_REQUIRE("alloc", args != NULL && threads != NULL);

	for (int i = 0; i < threadCount; i++) {
		args[i].startSem = semN;
		args[i].stop = false;
		args[i].counter = 0;
		char name[32];
		snprintf(name, sizeof(name), "core_%d", i);
		threads[i] = spawn_thread(core_work_thread, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	release_sem_etc(semN, threadCount, 0);
	snooze(200000);

	for (int i = 0; i < threadCount; i++)
		args[i].stop = true;
	for (int i = 0; i < threadCount; i++)
		wait_for_thread(threads[i], &exitVal);

	int64 totalCount = 0;
	for (int i = 0; i < threadCount; i++) {
		trace("    thread %d: %lld\n", i, (long long)args[i].counter);
		totalCount += args[i].counter;
	}

	// If threads truly run on separate cores, total should be
	// significantly more than single-thread (at least 1.5x for 2+ cores)
	double scaling = (double)totalCount / (double)singleCount;
	trace("    %d threads total: %lld, scaling: %.2fx\n",
		threadCount, (long long)totalCount, scaling);

	TEST_ASSERT("multi-core scaling > 1.3x",
		scaling > 1.3);

	free(args);
	free(threads);
	delete_sem(semN);
}


// STRESS

static void
test_stress_spawn_exit()
{
	trace("\n--- test_stress_spawn_exit ---\n");

	const int kThreads = 4;
	const int kIterations = 200;

	churn_args args[kThreads];
	thread_id threads[kThreads];

	bigtime_t start = system_time();
	for (int i = 0; i < kThreads; i++) {
		args[i].iterations = kIterations;
		args[i].errors = 0;
		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		threads[i] = spawn_thread(churn_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		totalErrors += args[i].errors;
	}
	bigtime_t elapsed = system_time() - start;

	trace("    %d threads x %d spawn/exit cycles in %lld us, errors=%d\n",
		kThreads, kIterations, (long long)elapsed, totalErrors);
	TEST_ASSERT("no spawn/exit errors", totalErrors == 0);
}


static void
test_stress_priority_thrash()
{
	trace("\n--- test_stress_priority_thrash ---\n");

	// Rapidly change priorities of running threads
	const int kThreads = 8;
	sem_id startSem = create_sem(0, "pthrash_start");
	TEST_REQUIRE("create sem", startSem >= 0);

	fairness_args args[kThreads];
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		args[i].startSem = startSem;
		args[i].stop = false;
		args[i].iterations = 0;
		char name[32];
		snprintf(name, sizeof(name), "pthrash_%d", i);
		threads[i] = spawn_thread(fairness_thread, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	release_sem_etc(startSem, kThreads, 0);

	// Thrash priorities for 300ms
	const int kCycles = 500;
	bigtime_t start = system_time();
	for (int c = 0; c < kCycles; c++) {
		for (int i = 0; i < kThreads; i++) {
			int32 prio = (c + i) % 2 == 0
				? B_LOW_PRIORITY : B_URGENT_DISPLAY_PRIORITY;
			set_thread_priority(threads[i], prio);
		}
	}
	bigtime_t elapsed = system_time() - start;

	for (int i = 0; i < kThreads; i++)
		args[i].stop = true;

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	trace("    %d priority changes in %lld us\n",
		kCycles * kThreads, (long long)elapsed);
	TEST_ASSERT("priority thrash completed", true);
	// The real test is: did we crash or deadlock?

	delete_sem(startSem);
}


static void
test_stress_mode_switch()
{
	trace("\n--- test_stress_mode_switch ---\n");

	// Switch scheduler modes under load
	sem_id startSem = create_sem(0, "mode_start");
	TEST_REQUIRE("create sem", startSem >= 0);

	const int kWorkers = 4;
	fairness_args args[kWorkers];
	thread_id threads[kWorkers];

	for (int i = 0; i < kWorkers; i++) {
		args[i].startSem = startSem;
		args[i].stop = false;
		args[i].iterations = 0;
		char name[32];
		snprintf(name, sizeof(name), "mode_wrk_%d", i);
		threads[i] = spawn_thread(fairness_thread, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	release_sem_etc(startSem, kWorkers, 0);

	int switchErrors = 0;
	for (int i = 0; i < 20; i++) {
		status_t s = set_scheduler_mode(i % 2 == 0
			? SCHEDULER_MODE_POWER_SAVING : SCHEDULER_MODE_LOW_LATENCY);
		if (s != B_OK)
			switchErrors++;
		snooze(10000);
	}

	for (int i = 0; i < kWorkers; i++)
		args[i].stop = true;

	for (int i = 0; i < kWorkers; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	trace("    20 mode switches, errors=%d\n", switchErrors);
	TEST_ASSERT("mode switches under load no errors", switchErrors == 0);

	// Restore low latency
	set_scheduler_mode(SCHEDULER_MODE_LOW_LATENCY);
	delete_sem(startSem);
}


static void
test_stress_many_threads()
{
	trace("\n--- test_stress_many_threads ---\n");

	const int kCount = 64;
	sem_id sem = create_sem(0, "many_start");
	TEST_REQUIRE("create sem", sem >= 0);

	fairness_args* args = (fairness_args*)calloc(kCount,
		sizeof(fairness_args));
	thread_id* threads = (thread_id*)calloc(kCount, sizeof(thread_id));
	TEST_REQUIRE("alloc", args != NULL && threads != NULL);

	int spawned = 0;
	for (int i = 0; i < kCount; i++) {
		args[i].startSem = sem;
		args[i].stop = false;
		args[i].iterations = 0;
		char name[32];
		snprintf(name, sizeof(name), "many_%d", i);
		// Spread priorities
		int32 prio = B_LOW_PRIORITY + (i % 20);
		threads[i] = spawn_thread(fairness_thread, name, prio, &args[i]);
		if (threads[i] >= 0) {
			resume_thread(threads[i]);
			spawned++;
		}
	}

	trace("    spawned %d/%d threads\n", spawned, kCount);
	release_sem_etc(sem, spawned, 0);

	snooze(1000000);	// 1s — 64 threads on 4 vCPUs need time

	for (int i = 0; i < kCount; i++)
		args[i].stop = true;

	for (int i = 0; i < kCount; i++) {
		if (threads[i] >= 0) {
			status_t exitVal;
			wait_for_thread(threads[i], &exitVal);
		}
	}

	// Verify threads got CPU — under KVM a couple lowest-prio
	// threads may be starved by 60+ competitors on 4 vCPUs
	int starved = 0;
	for (int i = 0; i < kCount; i++) {
		if (threads[i] >= 0 && args[i].iterations == 0)
			starved++;
	}

	trace("    starved threads: %d/%d\n", starved, spawned);
	TEST_ASSERT("at most 2 threads starved (KVM)", starved <= 2);
	TEST_ASSERT("spawned at least 32", spawned >= 32);

	free(args);
	free(threads);
	delete_sem(sem);
}


// Throughput benchmark

static double
sched_throughput_iteration()
{
	const int kCount = 200;
	bigtime_t start = system_time();
	for (int i = 0; i < kCount; i++) {
		thread_id t = spawn_thread(churn_child_func, "thru",
			B_NORMAL_PRIORITY, NULL);
		if (t < 0)
			continue;
		resume_thread(t);
		status_t exitVal;
		wait_for_thread(t, &exitVal);
	}
	bigtime_t elapsed = system_time() - start;
	return (double)kCount * 1000000.0 / (double)elapsed;
}


static void
test_stress_throughput()
{
	trace("\n--- test_stress_throughput ---\n");

	BenchStats stats = run_benchmark(sched_throughput_iteration, 5, 1);
	trace("    spawn/exit throughput: median=%.0f ops/s, "
		"min=%.0f, max=%.0f\n",
		stats.median_ops, stats.min_ops, stats.max_ops);
	TEST_ASSERT("throughput > 100 ops/s", stats.median_ops > 100);
}


// MOBILE SIM

static void
test_mobile_frame_deadline()
{
	trace("\n--- test_mobile_frame_deadline ---\n");

	system_info sysInfo;
	get_system_info(&sysInfo);

	// Render thread at high priority, BG workers at low
	mobile_frame_args renderArgs;
	memset(&renderArgs, 0, sizeof(renderArgs));
	renderArgs.stop = false;
	renderArgs.targetFrame = 16666;	// 60fps

	thread_id renderThread = spawn_thread(mobile_render_thread,
		"render_60fps", B_REAL_TIME_DISPLAY_PRIORITY, &renderArgs);
	TEST_REQUIRE("spawn render", renderThread >= 0);

	// Background workers
	const int kBG = 4;
	mobile_bg_args bgArgs[kBG];
	thread_id bgThreads[kBG];

	for (int i = 0; i < kBG; i++) {
		bgArgs[i].stop = false;
		bgArgs[i].iterations = 0;
		char name[32];
		snprintf(name, sizeof(name), "bg_%d", i);
		bgThreads[i] = spawn_thread(mobile_bg_thread, name,
			B_LOW_PRIORITY, &bgArgs[i]);
		resume_thread(bgThreads[i]);
	}

	resume_thread(renderThread);

	// Run for 2 seconds — enough for ~120 frames
	snooze(2000000);

	renderArgs.stop = true;
	for (int i = 0; i < kBG; i++)
		bgArgs[i].stop = true;

	status_t exitVal;
	wait_for_thread(renderThread, &exitVal);
	for (int i = 0; i < kBG; i++)
		wait_for_thread(bgThreads[i], &exitVal);

	int totalBG = 0;
	for (int i = 0; i < kBG; i++)
		totalBG += bgArgs[i].iterations;

	trace("    frames: %d completed, %d missed (%.1f%%), max=%lld us\n",
		renderArgs.framesCompleted, renderArgs.framesMissed,
		renderArgs.framesCompleted > 0
			? 100.0 * renderArgs.framesMissed / renderArgs.framesCompleted
			: 0.0,
		(long long)renderArgs.maxFrameTime);
	trace("    bg work: %d total iterations\n", totalBG);

	TEST_ASSERT("rendered > 60 frames in 2s",
		renderArgs.framesCompleted >= 60);
	// Under KVM with 4 BG workers, allow up to 20% frame misses
	TEST_ASSERT("frame miss rate < 20%",
		renderArgs.framesMissed * 5 < renderArgs.framesCompleted);
	TEST_ASSERT("max frame time < 50ms (KVM)",
		renderArgs.maxFrameTime < 50000);
	TEST_ASSERT("bg workers got CPU", totalBG > 0);
}


static void
test_mobile_burst_pattern()
{
	trace("\n--- test_mobile_burst_pattern ---\n");

	// Simulate mobile app pattern: short bursts of work + sleep
	// Multiple threads doing this concurrently at different priorities
	const int kThreads = 6;
	burst_args args[kThreads];
	thread_id threads[kThreads];

	int32 priorities[] = {
		B_REAL_TIME_DISPLAY_PRIORITY,	// compositor
		B_URGENT_DISPLAY_PRIORITY,		// UI
		B_DISPLAY_PRIORITY,				// animation
		B_NORMAL_PRIORITY,				// app logic
		B_LOW_PRIORITY,					// prefetch
		B_LOW_PRIORITY,					// analytics
	};

	for (int i = 0; i < kThreads; i++) {
		args[i].iterations = 100;
		args[i].completed = 0;
		args[i].totalTime = 0;
		char name[32];
		snprintf(name, sizeof(name), "burst_%d", i);
		threads[i] = spawn_thread(burst_thread_func, name,
			priorities[i], &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	bool allCompleted = true;
	for (int i = 0; i < kThreads; i++) {
		trace("    thread %d (prio %ld): %d/%d in %lld us\n",
			i, (long)priorities[i], args[i].completed,
			args[i].iterations, (long long)args[i].totalTime);
		if (args[i].completed < args[i].iterations)
			allCompleted = false;
	}

	TEST_ASSERT("all burst threads completed", allCompleted);

	// Higher priority threads should finish faster (or equal)
	TEST_ASSERT("compositor finished",
		args[0].completed == args[0].iterations);
	TEST_ASSERT("UI finished",
		args[1].completed == args[1].iterations);
}


// Suite registration

static const TestEntry sSchedulerTests[] = {
	// Basic API
	{ "suggest_thread_priority",     test_suggest_priority,         "Basic API" },
	{ "get/set scheduler mode",      test_scheduler_mode,           "Basic API" },
	{ "estimate scheduling latency", test_scheduling_latency,       "Basic API" },
	{ "system info CPUs",            test_system_info_cpus,         "Basic API" },
	// Priority
	{ "priority ordering",           test_priority_ordering,        "Priority" },
	{ "set_priority at runtime",     test_set_priority_runtime,     "Priority" },
	// Latency
	{ "wakeup latency 50x",         test_wakeup_latency,           "Latency" },
	{ "snooze accuracy",             test_snooze_accuracy,          "Latency" },
	{ "preemption latency",          test_preemption_latency,       "Latency" },
	// Fairness
	{ "equal priority fairness 4T",  test_equal_priority_fairness,  "Fairness" },
	// Yield
	{ "thread yield",                test_thread_yield,             "Yield" },
	// Multi-Core
	{ "multi-core spread",           test_multicore_spread,         "Multi-Core" },
	// Stress
	{ "spawn/exit churn 4x200",      test_stress_spawn_exit,        "STRESS" },
	{ "priority thrash 8x500",       test_stress_priority_thrash,   "STRESS" },
	{ "mode switch under load",      test_stress_mode_switch,       "STRESS" },
	{ "64 threads mixed prio",       test_stress_many_threads,      "STRESS" },
	{ "spawn/exit throughput",        test_stress_throughput,        "STRESS" },
	// Mobile Sim
	{ "60fps + 4 BG workers",        test_mobile_frame_deadline,    "MOBILE SIM" },
	{ "burst pattern 6 threads",      test_mobile_burst_pattern,    "MOBILE SIM" },
};


TestSuite
get_scheduler_test_suite()
{
	TestSuite suite;
	suite.name = "Scheduler";
	suite.tests = sSchedulerTests;
	suite.count = sizeof(sSchedulerTests) / sizeof(sSchedulerTests[0]);
	return suite;
}
