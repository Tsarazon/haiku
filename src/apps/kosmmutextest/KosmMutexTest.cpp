/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * GUI test application for kosm_mutex kernel primitive.
 * Runs all tests on launch, displays results in a BView,
 * writes detailed trace log to ~/Desktop/kosm_mutex_trace.log.
 */


#include <Application.h>
#include <FindDirectory.h>
#include <Font.h>
#include <KosmOS.h>
#include <Path.h>
#include <View.h>
#include <Window.h>
#include <image.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static FILE* sTraceFile = NULL;
static int sPassCount = 0;
static int sFailCount = 0;


static void
trace(const char* fmt, ...)
{
	if (sTraceFile == NULL)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(sTraceFile, fmt, args);
	va_end(args);
	fflush(sTraceFile);
}


static void
trace_call(const char* func, status_t result)
{
	trace("    %s -> %s (0x%08x)\n", func, strerror(result),
		(unsigned)result);
}


static void
trace_call_id(const char* func, int32 result)
{
	if (result >= 0)
		trace("    %s -> id=%d\n", func, (int)result);
	else
		trace("    %s -> %s (0x%08x)\n", func, strerror(result),
			(unsigned)result);
}


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


// ============================================================
// Thread helpers
// ============================================================

static status_t
acquire_and_exit_thread(void* data)
{
	kosm_mutex_id id = (kosm_mutex_id)(addr_t)data;
	status_t err = kosm_acquire_mutex(id);
	trace("    [thread %d] acquire_and_exit: acquire -> %s (0x%08x)\n",
		(int)find_thread(NULL), strerror(err), (unsigned)err);
	return B_OK;
}


struct try_acquire_args {
	kosm_mutex_id	id;
	status_t		result;
};


static status_t
try_acquire_thread_func(void* data)
{
	try_acquire_args* args = (try_acquire_args*)data;
	args->result = kosm_try_acquire_mutex(args->id);
	trace("    [thread %d] try_acquire(id=%d) -> %s (0x%08x)\n",
		(int)find_thread(NULL), (int)args->id,
		strerror(args->result), (unsigned)args->result);
	return B_OK;
}


struct timed_acquire_args {
	kosm_mutex_id	id;
	uint32			flags;
	bigtime_t		timeout;
	status_t		result;
	bigtime_t		elapsed;
};


static status_t
timed_acquire_thread_func(void* data)
{
	timed_acquire_args* args = (timed_acquire_args*)data;
	bigtime_t t0 = system_time();
	args->result = kosm_acquire_mutex_etc(args->id, args->flags, args->timeout);
	args->elapsed = system_time() - t0;
	trace("    [thread %d] acquire_etc(id=%d, flags=0x%x, timeout=%lld) "
		"-> %s (0x%08x), elapsed=%lld us\n",
		(int)find_thread(NULL), (int)args->id, (unsigned)args->flags,
		(long long)args->timeout, strerror(args->result),
		(unsigned)args->result, (long long)args->elapsed);
	return B_OK;
}


struct contention_args {
	kosm_mutex_id	id;
	volatile int32*	counter;
	int				iterations;
	bigtime_t		totalAcquireTime;
	bigtime_t		totalHoldTime;
};


static status_t
contention_thread_func(void* data)
{
	contention_args* args = (contention_args*)data;
	thread_id me = find_thread(NULL);
	args->totalAcquireTime = 0;
	args->totalHoldTime = 0;

	for (int i = 0; i < args->iterations; i++) {
		bigtime_t t0 = system_time();
		kosm_acquire_mutex(args->id);
		bigtime_t t1 = system_time();
		args->totalAcquireTime += (t1 - t0);

		int32 val = *args->counter;
		snooze(1);
		*args->counter = val + 1;

		bigtime_t t2 = system_time();
		args->totalHoldTime += (t2 - t1);
		kosm_release_mutex(args->id);
	}

	trace("    [thread %d] contention done: %d iters, "
		"acquire_total=%lld us, hold_total=%lld us\n",
		(int)me, args->iterations,
		(long long)args->totalAcquireTime,
		(long long)args->totalHoldTime);
	return B_OK;
}


struct release_other_args {
	kosm_mutex_id	id;
	status_t		result;
};


static status_t
release_from_other_thread_func(void* data)
{
	release_other_args* args = (release_other_args*)data;
	args->result = kosm_release_mutex(args->id);
	trace("    [thread %d] release_from_other(id=%d) -> %s (0x%08x)\n",
		(int)find_thread(NULL), (int)args->id,
		strerror(args->result), (unsigned)args->result);
	return B_OK;
}


// Waiter that blocks on acquire, reports result
struct waiter_args {
	kosm_mutex_id	id;
	status_t		result;
	volatile bool	started;
	volatile bool	done;
};


static status_t
waiter_thread_func(void* data)
{
	waiter_args* args = (waiter_args*)data;
	args->started = true;
	args->result = kosm_acquire_mutex(args->id);
	trace("    [thread %d] waiter: acquire -> %s (0x%08x)\n",
		(int)find_thread(NULL), strerror(args->result),
		(unsigned)args->result);
	args->done = true;
	// if acquired, hold briefly then release
	if (args->result == KOSM_MUTEX_OWNER_DEAD) {
		kosm_mark_mutex_consistent(args->id);
		kosm_release_mutex(args->id);
	} else if (args->result == B_OK) {
		kosm_release_mutex(args->id);
	}
	return B_OK;
}


// Recursive acquire N times then exit
struct recursive_death_args {
	kosm_mutex_id	id;
	int				depth;
};


static status_t
recursive_acquire_and_exit_thread(void* data)
{
	recursive_death_args* args = (recursive_death_args*)data;
	for (int i = 0; i < args->depth; i++) {
		status_t err = kosm_acquire_mutex(args->id);
		trace("    [thread %d] recursive acquire %d -> %s\n",
			(int)find_thread(NULL), i + 1, strerror(err));
	}
	// exit without releasing
	return B_OK;
}


// Priority inheritance: hold mutex, log priority changes
struct pi_holder_args {
	kosm_mutex_id	id;
	int32			originalPriority;
	int32			boostedPriority;
	volatile bool	acquired;
	volatile bool	shouldRelease;
};


static status_t
pi_holder_thread_func(void* data)
{
	pi_holder_args* args = (pi_holder_args*)data;
	thread_id me = find_thread(NULL);

	thread_info tinfo;
	get_thread_info(me, &tinfo);
	args->originalPriority = tinfo.priority;

	kosm_acquire_mutex(args->id);
	args->acquired = true;
	trace("    [thread %d] PI holder: acquired, priority=%d\n",
		(int)me, (int)args->originalPriority);

	// wait for high-pri thread to block, then check our priority
	while (!args->shouldRelease)
		snooze(1000);

	get_thread_info(me, &tinfo);
	args->boostedPriority = tinfo.priority;
	trace("    [thread %d] PI holder: priority now=%d (was %d)\n",
		(int)me, (int)args->boostedPriority,
		(int)args->originalPriority);

	kosm_release_mutex(args->id);
	return B_OK;
}


// Signal handler for interrupt test
static int32 sSignalReceived = 0;

static void
test_signal_handler(int sig)
{
	atomic_add(&sSignalReceived, 1);
}


// Thread that acquires with no timeout (blocks), can be interrupted
struct interruptible_args {
	kosm_mutex_id	id;
	status_t		result;
	volatile bool	started;
	bigtime_t		elapsed;
};


static status_t
interruptible_acquire_thread(void* data)
{
	interruptible_args* args = (interruptible_args*)data;

	// install signal handler
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = test_signal_handler;
	sigaction(SIGUSR1, &sa, NULL);

	args->started = true;
	bigtime_t t0 = system_time();
	args->result = kosm_acquire_mutex_etc(args->id,
		B_RELATIVE_TIMEOUT, 5000000); // 5 sec max
	args->elapsed = system_time() - t0;
	trace("    [thread %d] interruptible acquire -> %s (0x%08x), "
		"elapsed=%lld us\n",
		(int)find_thread(NULL), strerror(args->result),
		(unsigned)args->result, (long long)args->elapsed);
	return B_OK;
}


// Stress test thread — rapid acquire/release cycles
struct stress_args {
	kosm_mutex_id	id;
	volatile int64*	counter;
	int				iterations;
	int				errors;
	bigtime_t		maxAcquireTime;
	bigtime_t		totalTime;
};


static status_t
stress_thread_func(void* data)
{
	stress_args* args = (stress_args*)data;
	args->errors = 0;
	args->maxAcquireTime = 0;

	bigtime_t tStart = system_time();

	for (int i = 0; i < args->iterations; i++) {
		bigtime_t t0 = system_time();
		status_t err = kosm_acquire_mutex(args->id);
		bigtime_t acquireTime = system_time() - t0;

		if (acquireTime > args->maxAcquireTime)
			args->maxAcquireTime = acquireTime;

		if (err != B_OK) {
			args->errors++;
			continue;
		}

		// critical section: increment atomically-observable counter
		(*args->counter)++;

		kosm_release_mutex(args->id);
	}

	args->totalTime = system_time() - tStart;
	return B_OK;
}


// Create/destroy churn thread
struct churn_args {
	int			iterations;
	int			errors;
	bigtime_t	totalTime;
};


static status_t
churn_thread_func(void* data)
{
	churn_args* args = (churn_args*)data;
	args->errors = 0;

	bigtime_t tStart = system_time();

	for (int i = 0; i < args->iterations; i++) {
		char name[B_OS_NAME_LENGTH];
		snprintf(name, sizeof(name), "churn_%d_%d",
			(int)find_thread(NULL), i);

		kosm_mutex_id id = kosm_create_mutex(name, 0);
		if (id < 0) {
			args->errors++;
			continue;
		}

		kosm_acquire_mutex(id);
		kosm_release_mutex(id);
		kosm_delete_mutex(id);
	}

	args->totalTime = system_time() - tStart;
	return B_OK;
}


// Mobile OS simulation: "UI thread" + background workers
struct mobile_sim_args {
	kosm_mutex_id	dataMutex;
	kosm_mutex_id	renderMutex;
	volatile int32*	sharedData;
	int				iterations;
	bigtime_t		maxLatency;
	int				errors;
};


static status_t
mobile_ui_thread_func(void* data)
{
	mobile_sim_args* args = (mobile_sim_args*)data;
	args->maxLatency = 0;
	args->errors = 0;

	for (int i = 0; i < args->iterations; i++) {
		// simulate 60fps frame: lock data, read, lock render, write
		bigtime_t t0 = system_time();

		status_t err = kosm_acquire_mutex_etc(args->dataMutex,
			B_RELATIVE_TIMEOUT, 16667); // 16ms frame budget
		if (err != B_OK) {
			args->errors++;
			trace("    [UI thread %d] frame %d: data lock timeout!\n",
				(int)find_thread(NULL), i);
			continue;
		}

		int32 val = *args->sharedData;
		kosm_release_mutex(args->dataMutex);

		kosm_acquire_mutex(args->renderMutex);
		// "render" the data
		(void)val;
		kosm_release_mutex(args->renderMutex);

		bigtime_t latency = system_time() - t0;
		if (latency > args->maxLatency)
			args->maxLatency = latency;

		// simulate vsync
		snooze(1000);
	}

	trace("    [UI thread %d] done: %d frames, maxLatency=%lld us, "
		"errors=%d\n",
		(int)find_thread(NULL), args->iterations,
		(long long)args->maxLatency, args->errors);
	return B_OK;
}


static status_t
mobile_bg_worker_func(void* data)
{
	mobile_sim_args* args = (mobile_sim_args*)data;

	for (int i = 0; i < args->iterations; i++) {
		// simulate background data update
		kosm_acquire_mutex(args->dataMutex);
		(*args->sharedData)++;
		snooze(500); // simulate work under lock
		kosm_release_mutex(args->dataMutex);
		snooze(2000); // simulate work without lock
	}

	trace("    [BG worker %d] done: %d updates\n",
		(int)find_thread(NULL), args->iterations);
	return B_OK;
}


// ============================================================
// Tests — basic
// ============================================================

static void
test_create_delete()
{
	trace("\n--- test_create_delete ---\n");
	trace("  main thread: %d, team: %d\n",
		(int)find_thread(NULL), (int)getpid());
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_cd", 0);
	trace_call_id("kosm_create_mutex(\"test_cd\", 0)", id);
	TEST_ASSERT("create returns valid id", id >= 0);

	status_t err = kosm_delete_mutex(id);
	trace_call("kosm_delete_mutex(first)", err);
	TEST_ASSERT("delete succeeds", err == B_OK);

	err = kosm_delete_mutex(id);
	trace_call("kosm_delete_mutex(second/double)", err);
	TEST_ASSERT("double delete fails", err != B_OK);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_acquire_release()
{
	trace("\n--- test_acquire_release ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_ar", 0);
	trace_call_id("kosm_create_mutex(\"test_ar\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex", err);
	TEST_ASSERT("acquire", err == B_OK);

	err = kosm_release_mutex(id);
	trace_call("kosm_release_mutex", err);
	TEST_ASSERT("release", err == B_OK);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_try_acquire()
{
	trace("\n--- test_try_acquire ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_try", 0);
	trace_call_id("kosm_create_mutex(\"test_try\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_try_acquire_mutex(id);
	trace_call("kosm_try_acquire_mutex (free)", err);
	TEST_ASSERT("try_acquire on free mutex", err == B_OK);

	try_acquire_args args;
	args.id = id;
	args.result = B_OK;

	thread_id tid = spawn_thread(try_acquire_thread_func, "try_thread",
		B_NORMAL_PRIORITY, &args);
	trace("    spawned thread %d for try_acquire\n", (int)tid);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("try_acquire from other thread returns WOULD_BLOCK",
		args.result == B_WOULD_BLOCK);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_recursive()
{
	trace("\n--- test_recursive ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_rec", KOSM_MUTEX_RECURSIVE);
	trace_call_id("kosm_create_mutex(\"test_rec\", RECURSIVE)", id);
	TEST_ASSERT("create recursive", id >= 0);

	status_t err;
	err = kosm_acquire_mutex(id);
	trace_call("acquire 1", err);
	TEST_ASSERT("acquire 1", err == B_OK);

	err = kosm_acquire_mutex(id);
	trace_call("acquire 2", err);
	TEST_ASSERT("acquire 2 (recursive)", err == B_OK);

	err = kosm_acquire_mutex(id);
	trace_call("acquire 3", err);
	TEST_ASSERT("acquire 3 (recursive)", err == B_OK);

	kosm_mutex_info info;
	err = kosm_get_mutex_info(id, &info);
	trace_call("kosm_get_mutex_info (during recursion)", err);
	trace("    info: holder=%d, recursion=%d, flags=0x%x\n",
		(int)info.holder, (int)info.recursion, (unsigned)info.flags);
	TEST_ASSERT("get_info during recursion", err == B_OK);
	TEST_ASSERT("recursion count is 3", info.recursion == 3);

	err = kosm_release_mutex(id);
	trace_call("release 1", err);
	TEST_ASSERT("release 1", err == B_OK);

	err = kosm_release_mutex(id);
	trace_call("release 2", err);
	TEST_ASSERT("release 2", err == B_OK);

	err = kosm_release_mutex(id);
	trace_call("release 3", err);
	TEST_ASSERT("release 3", err == B_OK);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_deadlock_detection()
{
	trace("\n--- test_deadlock_detection ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_dl", 0);
	trace_call_id("kosm_create_mutex(\"test_dl\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex", err);
	TEST_ASSERT("first acquire", err == B_OK);

	err = kosm_try_acquire_mutex(id);
	trace_call("kosm_try_acquire_mutex (self, non-recursive)", err);
	TEST_ASSERT("self-deadlock detected",
		err == B_WOULD_BLOCK || err == KOSM_MUTEX_DEADLOCK);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_contention()
{
	trace("\n--- test_contention ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_cont", 0);
	trace_call_id("kosm_create_mutex(\"test_cont\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	volatile int32 counter = 0;
	const int kThreadCount = 4;
	const int kIterations = 50;

	contention_args args[kThreadCount];
	thread_id threads[kThreadCount];

	for (int i = 0; i < kThreadCount; i++) {
		args[i].id = id;
		args[i].counter = &counter;
		args[i].iterations = kIterations;
		args[i].totalAcquireTime = 0;
		args[i].totalHoldTime = 0;

		char name[32];
		snprintf(name, sizeof(name), "contention_%d", i);
		threads[i] = spawn_thread(contention_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		trace("    spawned thread %d (%s)\n", (int)threads[i], name);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	trace("    counter=%d, expected=%d\n",
		(int)counter, kThreadCount * kIterations);
	TEST_ASSERT("counter correct (serialized access)",
		counter == kThreadCount * kIterations);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_timed_acquire()
{
	trace("\n--- test_timed_acquire ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_timed", 0);
	trace_call_id("kosm_create_mutex(\"test_timed\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (hold)", err);

	timed_acquire_args args;
	args.id = id;
	args.flags = B_RELATIVE_TIMEOUT;
	args.timeout = 100000;
	args.result = B_OK;
	args.elapsed = 0;

	thread_id tid = spawn_thread(timed_acquire_thread_func, "timed_thread",
		B_NORMAL_PRIORITY, &args);
	trace("    spawned thread %d for timed acquire "
		"(timeout=%lld us)\n", (int)tid, (long long)args.timeout);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("timed acquire expired", args.result == B_TIMED_OUT);

	bigtime_t drift = args.elapsed - args.timeout;
	trace("    timeout requested=%lld us, actual elapsed=%lld us, "
		"drift=%lld us\n",
		(long long)args.timeout, (long long)args.elapsed,
		(long long)drift);
	TEST_ASSERT("timeout accuracy (drift < 50ms)",
		drift >= 0 && drift < 50000);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_find_by_name()
{
	trace("\n--- test_find_by_name ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("kosm_find_test", 0);
	trace_call_id("kosm_create_mutex(\"kosm_find_test\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	kosm_mutex_id found = kosm_find_mutex("kosm_find_test");
	trace_call_id("kosm_find_mutex(\"kosm_find_test\")", found);
	TEST_ASSERT("find returns same id", found == id);

	kosm_mutex_id notFound = kosm_find_mutex("nonexistent_mutex_xyz");
	trace_call_id("kosm_find_mutex(\"nonexistent_mutex_xyz\")", notFound);
	TEST_ASSERT("find nonexistent fails", notFound < 0);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_not_owner_release()
{
	trace("\n--- test_not_owner_release ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_notown", 0);
	trace_call_id("kosm_create_mutex(\"test_notown\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex", err);
	trace("    holder thread: %d\n", (int)find_thread(NULL));

	release_other_args args;
	args.id = id;
	args.result = B_OK;

	thread_id tid = spawn_thread(release_from_other_thread_func,
		"notown_thread", B_NORMAL_PRIORITY, &args);
	trace("    spawned thread %d to release from non-owner\n", (int)tid);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("release from non-owner fails",
		args.result == KOSM_MUTEX_NOT_OWNER);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — robust mutex
// ============================================================

static void
test_owner_death()
{
	trace("\n--- test_owner_death ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_death", 0);
	trace_call_id("kosm_create_mutex(\"test_death\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	thread_id tid = spawn_thread(acquire_and_exit_thread, "dying_thread",
		B_NORMAL_PRIORITY, (void*)(addr_t)id);
	trace("    spawned dying thread %d\n", (int)tid);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	trace("    dying thread %d exited (status=%s)\n",
		(int)tid, strerror(exitVal));

	kosm_mutex_info info;
	status_t err = kosm_get_mutex_info(id, &info);
	trace("    pre-acquire info: holder=%d, name=%s, flags=0x%x\n",
		(int)info.holder, info.name, (unsigned)info.flags);

	err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (after owner death)", err);
	TEST_ASSERT("acquire after owner death returns OWNER_DEAD",
		err == KOSM_MUTEX_OWNER_DEAD);

	err = kosm_mark_mutex_consistent(id);
	trace_call("kosm_mark_mutex_consistent", err);
	TEST_ASSERT("mark_consistent succeeds", err == B_OK);

	err = kosm_release_mutex(id);
	trace_call("kosm_release_mutex (after mark_consistent)", err);
	TEST_ASSERT("release after mark_consistent", err == B_OK);

	err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (after recovery)", err);
	TEST_ASSERT("acquire after recovery", err == B_OK);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_not_recoverable()
{
	trace("\n--- test_not_recoverable ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_norec", 0);
	trace_call_id("kosm_create_mutex(\"test_norec\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	thread_id tid = spawn_thread(acquire_and_exit_thread, "dying_thread2",
		B_NORMAL_PRIORITY, (void*)(addr_t)id);
	trace("    spawned dying thread %d\n", (int)tid);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	trace("    dying thread %d exited\n", (int)tid);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (expect OWNER_DEAD)", err);
	TEST_ASSERT("get OWNER_DEAD", err == KOSM_MUTEX_OWNER_DEAD);

	err = kosm_release_mutex(id);
	trace_call("kosm_release_mutex (without mark_consistent)", err);
	TEST_ASSERT("release without mark_consistent", err == B_OK);

	err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (expect NOT_RECOVERABLE)", err);
	TEST_ASSERT("acquire returns NOT_RECOVERABLE",
		err == KOSM_MUTEX_NOT_RECOVERABLE);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_multi_waiter_owner_death()
{
	trace("\n--- test_multi_waiter_owner_death ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_mw_death", 0);
	trace_call_id("kosm_create_mutex(\"test_mw_death\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	// holder thread acquires and dies
	thread_id holder = spawn_thread(acquire_and_exit_thread,
		"holder_dying", B_NORMAL_PRIORITY, (void*)(addr_t)id);
	resume_thread(holder);
	status_t exitVal;
	wait_for_thread(holder, &exitVal);
	trace("    holder thread %d died\n", (int)holder);

	// spawn 4 waiters — all should get OWNER_DEAD
	const int kWaiters = 4;
	waiter_args wargs[kWaiters];
	thread_id waiters[kWaiters];

	for (int i = 0; i < kWaiters; i++) {
		wargs[i].id = id;
		wargs[i].result = B_ERROR;
		wargs[i].started = false;
		wargs[i].done = false;

		char name[32];
		snprintf(name, sizeof(name), "waiter_%d", i);
		waiters[i] = spawn_thread(waiter_thread_func, name,
			B_NORMAL_PRIORITY, &wargs[i]);
		trace("    spawned waiter %d (thread %d)\n", i, (int)waiters[i]);
		resume_thread(waiters[i]);
	}

	for (int i = 0; i < kWaiters; i++) {
		wait_for_thread(waiters[i], &exitVal);
	}

	// first waiter gets OWNER_DEAD and calls mark_consistent + release;
	// remaining waiters may get B_OK, OWNER_DEAD, or NOT_RECOVERABLE
	// depending on race timing
	int gotOwnerDead = 0;
	int gotOK = 0;
	int gotNotRecoverable = 0;
	for (int i = 0; i < kWaiters; i++) {
		trace("    waiter %d result: %s (0x%08x)\n", i,
			strerror(wargs[i].result), (unsigned)wargs[i].result);
		if (wargs[i].result == KOSM_MUTEX_OWNER_DEAD)
			gotOwnerDead++;
		else if (wargs[i].result == B_OK)
			gotOK++;
		else if (wargs[i].result == KOSM_MUTEX_NOT_RECOVERABLE)
			gotNotRecoverable++;
	}

	trace("    gotOwnerDead=%d, gotOK=%d, gotNotRecoverable=%d\n",
		gotOwnerDead, gotOK, gotNotRecoverable);
	TEST_ASSERT("at least one waiter got OWNER_DEAD", gotOwnerDead >= 1);
	TEST_ASSERT("all waiters completed",
		gotOwnerDead + gotOK + gotNotRecoverable == kWaiters);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_recursive_owner_death()
{
	trace("\n--- test_recursive_owner_death ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_recdeath",
		KOSM_MUTEX_RECURSIVE);
	trace_call_id("kosm_create_mutex(RECURSIVE)", id);
	TEST_ASSERT("create", id >= 0);

	// thread acquires 3 times then exits
	recursive_death_args rdargs;
	rdargs.id = id;
	rdargs.depth = 3;

	thread_id tid = spawn_thread(recursive_acquire_and_exit_thread,
		"rec_dying", B_NORMAL_PRIORITY, &rdargs);
	trace("    spawned thread %d, will acquire %d times then die\n",
		(int)tid, rdargs.depth);
	resume_thread(tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	// acquire — should get OWNER_DEAD despite recursion > 1
	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex (after recursive death)", err);
	TEST_ASSERT("acquire returns OWNER_DEAD", err == KOSM_MUTEX_OWNER_DEAD);

	kosm_mutex_info info;
	kosm_get_mutex_info(id, &info);
	trace("    post-death info: holder=%d, recursion=%d\n",
		(int)info.holder, (int)info.recursion);

	err = kosm_mark_mutex_consistent(id);
	trace_call("mark_consistent", err);
	TEST_ASSERT("mark_consistent after recursive death", err == B_OK);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_delete_with_waiters()
{
	trace("\n--- test_delete_with_waiters ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_delwait", 0);
	trace_call_id("kosm_create_mutex(\"test_delwait\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	// hold the mutex
	kosm_acquire_mutex(id);

	// spawn waiters that will block
	const int kWaiters = 3;
	waiter_args wargs[kWaiters];
	thread_id waiters[kWaiters];

	for (int i = 0; i < kWaiters; i++) {
		wargs[i].id = id;
		wargs[i].result = B_ERROR;
		wargs[i].started = false;
		wargs[i].done = false;

		char name[32];
		snprintf(name, sizeof(name), "delwait_%d", i);
		waiters[i] = spawn_thread(waiter_thread_func, name,
			B_NORMAL_PRIORITY, &wargs[i]);
		resume_thread(waiters[i]);
	}

	// wait for all to start blocking
	snooze(50000);

	// release and delete while they're waiting
	kosm_release_mutex(id);

	// let first waiter get it, then delete
	snooze(10000);
	status_t err = kosm_delete_mutex(id);
	trace_call("kosm_delete_mutex (with potential waiters)", err);

	// wait for all waiters to finish
	for (int i = 0; i < kWaiters; i++) {
		status_t exitVal;
		wait_for_thread(waiters[i], &exitVal);
		trace("    waiter %d: result=%s (0x%08x)\n", i,
			strerror(wargs[i].result), (unsigned)wargs[i].result);
	}

	// at least some should have completed
	int completed = 0;
	for (int i = 0; i < kWaiters; i++) {
		if (wargs[i].done)
			completed++;
	}
	trace("    %d/%d waiters completed\n", completed, kWaiters);
	TEST_ASSERT("all waiter threads finished", completed == kWaiters);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — info
// ============================================================

static void
test_get_mutex_info()
{
	trace("\n--- test_get_mutex_info ---\n");
	bigtime_t t0 = system_time();

	uint32 flags = KOSM_MUTEX_RECURSIVE | KOSM_MUTEX_PRIO_INHERIT;
	kosm_mutex_id id = kosm_create_mutex("test_info", flags);
	trace_call_id("kosm_create_mutex(\"test_info\", REC|PI)", id);
	TEST_ASSERT("create", id >= 0);

	status_t err = kosm_acquire_mutex(id);
	trace_call("kosm_acquire_mutex", err);

	kosm_mutex_info info;
	memset(&info, 0, sizeof(info));
	err = kosm_get_mutex_info(id, &info);
	trace_call("kosm_get_mutex_info", err);
	trace("    info dump:\n");
	trace("      mutex     = %d\n", (int)info.mutex);
	trace("      team      = %d\n", (int)info.team);
	trace("      name      = \"%s\"\n", info.name);
	trace("      holder    = %d (expected %d)\n",
		(int)info.holder, (int)find_thread(NULL));
	trace("      recursion = %d\n", (int)info.recursion);
	trace("      flags     = 0x%x\n", (unsigned)info.flags);

	TEST_ASSERT("get_mutex_info succeeds", err == B_OK);
	TEST_ASSERT("info.mutex matches id", info.mutex == id);
	TEST_ASSERT("info.name matches",
		strcmp(info.name, "test_info") == 0);
	TEST_ASSERT("info.holder is current thread",
		info.holder == find_thread(NULL));
	TEST_ASSERT("info.flags has RECURSIVE",
		(info.flags & KOSM_MUTEX_RECURSIVE) != 0);
	TEST_ASSERT("info.flags has PRIO_INHERIT",
		(info.flags & KOSM_MUTEX_PRIO_INHERIT) != 0);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_get_next_mutex_info()
{
	trace("\n--- test_get_next_mutex_info ---\n");
	bigtime_t t0 = system_time();

	const int kCount = 3;
	kosm_mutex_id ids[kCount];
	char names[kCount][B_OS_NAME_LENGTH];

	for (int i = 0; i < kCount; i++) {
		snprintf(names[i], sizeof(names[i]), "test_iter_%d", i);
		ids[i] = kosm_create_mutex(names[i], 0);
		trace("    created \"%s\" -> id=%d\n", names[i], (int)ids[i]);
		TEST_ASSERT("create for iteration", ids[i] >= 0);
	}

	int32 cookie = 0;
	kosm_mutex_info info;
	int found = 0;
	int total = 0;

	team_id myTeam = getpid();
	trace("    iterating mutexes for team %d:\n", (int)myTeam);
	while (kosm_get_next_mutex_info(myTeam, &cookie, &info) == B_OK) {
		total++;
		bool ours = false;
		for (int i = 0; i < kCount; i++) {
			if (info.mutex == ids[i]) {
				found++;
				ours = true;
			}
		}
		trace("      [%d] id=%d name=\"%s\" holder=%d flags=0x%x%s\n",
			total, (int)info.mutex, info.name, (int)info.holder,
			(unsigned)info.flags, ours ? " (OURS)" : "");
	}

	trace("    iterated %d total, found %d/%d ours\n",
		total, found, kCount);
	TEST_ASSERT("found all created mutexes", found == kCount);

	for (int i = 0; i < kCount; i++)
		kosm_delete_mutex(ids[i]);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — shared mutex
// ============================================================

static void
test_shared_mutex()
{
	trace("\n--- test_shared_mutex ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_shared",
		KOSM_MUTEX_SHARED);
	trace_call_id("kosm_create_mutex(SHARED)", id);
	TEST_ASSERT("create shared", id >= 0);

	kosm_mutex_info info;
	kosm_get_mutex_info(id, &info);
	trace("    flags=0x%x\n", (unsigned)info.flags);
	TEST_ASSERT("SHARED flag stored",
		(info.flags & KOSM_MUTEX_SHARED) != 0);

	// find by name should work for shared mutexes
	kosm_mutex_id found = kosm_find_mutex("test_shared");
	trace_call_id("kosm_find_mutex(\"test_shared\")", found);
	TEST_ASSERT("find shared mutex by name", found == id);

	// basic acquire/release with shared flag
	status_t err = kosm_acquire_mutex(id);
	trace_call("acquire shared", err);
	TEST_ASSERT("acquire shared mutex", err == B_OK);

	err = kosm_release_mutex(id);
	trace_call("release shared", err);
	TEST_ASSERT("release shared mutex", err == B_OK);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — priority inheritance
// ============================================================

static void
test_priority_inheritance()
{
	trace("\n--- test_priority_inheritance ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_pi", KOSM_MUTEX_PRIO_INHERIT);
	trace_call_id("kosm_create_mutex(PRIO_INHERIT)", id);
	TEST_ASSERT("create PI mutex", id >= 0);

	// spawn low-priority holder
	pi_holder_args hargs;
	hargs.id = id;
	hargs.originalPriority = 0;
	hargs.boostedPriority = 0;
	hargs.acquired = false;
	hargs.shouldRelease = false;

	thread_id lowThread = spawn_thread(pi_holder_thread_func,
		"pi_low", 5, &hargs);
	trace("    spawned low-pri holder thread %d (pri=5)\n", (int)lowThread);
	resume_thread(lowThread);

	// wait for it to acquire
	while (!hargs.acquired)
		snooze(1000);

	// spawn high-pri thread that will block on the mutex
	timed_acquire_args hiargs;
	hiargs.id = id;
	hiargs.flags = B_RELATIVE_TIMEOUT;
	hiargs.timeout = 2000000; // 2 sec
	hiargs.result = B_OK;
	hiargs.elapsed = 0;

	thread_id hiThread = spawn_thread(timed_acquire_thread_func,
		"pi_high", 25, &hiargs);
	trace("    spawned high-pri thread %d (pri=25)\n", (int)hiThread);
	resume_thread(hiThread);

	// give time for the high-pri thread to block
	snooze(50000);

	// now tell holder to check its priority and release
	hargs.shouldRelease = true;

	status_t exitVal;
	wait_for_thread(lowThread, &exitVal);
	wait_for_thread(hiThread, &exitVal);

	trace("    holder original pri=%d, boosted pri=%d\n",
		(int)hargs.originalPriority, (int)hargs.boostedPriority);
	trace("    high-pri thread result: %s\n", strerror(hiargs.result));

	TEST_ASSERT("holder was boosted above original priority",
		hargs.boostedPriority > hargs.originalPriority);
	TEST_ASSERT("high-pri thread acquired successfully",
		hiargs.result == B_OK);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — signal interruption
// ============================================================

static void
test_signal_interrupt()
{
	trace("\n--- test_signal_interrupt ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_signal", 0);
	trace_call_id("kosm_create_mutex(\"test_signal\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	// hold the mutex
	kosm_acquire_mutex(id);

	interruptible_args iargs;
	iargs.id = id;
	iargs.result = B_OK;
	iargs.started = false;
	iargs.elapsed = 0;

	sSignalReceived = 0;

	thread_id tid = spawn_thread(interruptible_acquire_thread,
		"sig_waiter", B_NORMAL_PRIORITY, &iargs);
	trace("    spawned thread %d for signal test\n", (int)tid);
	resume_thread(tid);

	// wait for it to start blocking
	while (!iargs.started)
		snooze(1000);
	snooze(50000); // let it actually block in kernel

	// send signal
	send_signal(tid, SIGUSR1);
	trace("    sent SIGUSR1 to thread %d\n", (int)tid);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	trace("    waiter result: %s (0x%08x), elapsed=%lld us, "
		"signal_received=%d\n",
		strerror(iargs.result), (unsigned)iargs.result,
		(long long)iargs.elapsed, (int)sSignalReceived);

	// The thread should have been interrupted or timed out
	TEST_ASSERT("acquire was interrupted or timed out",
		iargs.result == B_INTERRUPTED || iargs.result == B_TIMED_OUT);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — stress
// ============================================================

static void
test_stress_heavy_contention()
{
	trace("\n--- test_stress_heavy_contention ---\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id id = kosm_create_mutex("test_stress", 0);
	trace_call_id("kosm_create_mutex(\"test_stress\", 0)", id);
	TEST_ASSERT("create", id >= 0);

	const int kThreadCount = 16;
	const int kIterations = 1000;
	volatile int64 counter = 0;

	stress_args args[kThreadCount];
	thread_id threads[kThreadCount];

	for (int i = 0; i < kThreadCount; i++) {
		args[i].id = id;
		args[i].counter = &counter;
		args[i].iterations = kIterations;
		args[i].errors = 0;
		args[i].maxAcquireTime = 0;
		args[i].totalTime = 0;

		char name[32];
		snprintf(name, sizeof(name), "stress_%d", i);
		threads[i] = spawn_thread(stress_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	int totalErrors = 0;
	bigtime_t worstLatency = 0;
	for (int i = 0; i < kThreadCount; i++) {
		totalErrors += args[i].errors;
		if (args[i].maxAcquireTime > worstLatency)
			worstLatency = args[i].maxAcquireTime;
		trace("    thread %d: errors=%d, maxAcquire=%lld us, "
			"total=%lld us\n",
			i, args[i].errors, (long long)args[i].maxAcquireTime,
			(long long)args[i].totalTime);
	}

	trace("    counter=%lld, expected=%d\n",
		(long long)counter, kThreadCount * kIterations);
	trace("    totalErrors=%d, worstLatency=%lld us\n",
		totalErrors, (long long)worstLatency);

	TEST_ASSERT("counter correct (16x1000)",
		counter == (int64)kThreadCount * kIterations);
	TEST_ASSERT("no acquire errors", totalErrors == 0);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_create_destroy_churn()
{
	trace("\n--- test_stress_create_destroy_churn ---\n");
	bigtime_t t0 = system_time();

	const int kThreadCount = 8;
	const int kIterations = 200;

	churn_args args[kThreadCount];
	thread_id threads[kThreadCount];

	for (int i = 0; i < kThreadCount; i++) {
		args[i].iterations = kIterations;
		args[i].errors = 0;
		args[i].totalTime = 0;

		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		threads[i] = spawn_thread(churn_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreadCount; i++) {
		totalErrors += args[i].errors;
		trace("    thread %d: errors=%d, total=%lld us\n",
			i, args[i].errors, (long long)args[i].totalTime);
	}

	trace("    total churn cycles=%d, errors=%d\n",
		kThreadCount * kIterations, totalErrors);
	TEST_ASSERT("create/destroy churn no errors", totalErrors == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_mixed_flags()
{
	trace("\n--- test_stress_mixed_flags ---\n");
	bigtime_t t0 = system_time();

	// stress test with all flag combinations
	struct {
		const char*	name;
		uint32		flags;
	} configs[] = {
		{ "plain",         0 },
		{ "recursive",     KOSM_MUTEX_RECURSIVE },
		{ "pi",            KOSM_MUTEX_PRIO_INHERIT },
		{ "shared",        KOSM_MUTEX_SHARED },
		{ "rec+pi",        KOSM_MUTEX_RECURSIVE | KOSM_MUTEX_PRIO_INHERIT },
		{ "shared+rec",    KOSM_MUTEX_SHARED | KOSM_MUTEX_RECURSIVE },
		{ "shared+pi",     KOSM_MUTEX_SHARED | KOSM_MUTEX_PRIO_INHERIT },
		{ "all",           KOSM_MUTEX_SHARED | KOSM_MUTEX_RECURSIVE
		                   | KOSM_MUTEX_PRIO_INHERIT },
	};

	int configCount = sizeof(configs) / sizeof(configs[0]);

	for (int c = 0; c < configCount; c++) {
		kosm_mutex_id id = kosm_create_mutex(configs[c].name,
			configs[c].flags);
		trace("    config \"%s\" (flags=0x%x) -> id=%d\n",
			configs[c].name, (unsigned)configs[c].flags, (int)id);

		if (id < 0) {
			TEST_ASSERT("create with flags", false);
			continue;
		}

		// rapid acquire/release cycles
		const int kCycles = 100;
		int errors = 0;
		for (int i = 0; i < kCycles; i++) {
			status_t err = kosm_acquire_mutex(id);
			if (err != B_OK) {
				errors++;
				break;
			}
			// if recursive, do nested acquire
			if (configs[c].flags & KOSM_MUTEX_RECURSIVE) {
				kosm_acquire_mutex(id);
				kosm_release_mutex(id);
			}
			kosm_release_mutex(id);
		}

		trace("    \"%s\": %d cycles, %d errors\n",
			configs[c].name, kCycles, errors);

		char assertName[64];
		snprintf(assertName, sizeof(assertName),
			"flag combo '%s' works", configs[c].name);
		TEST_ASSERT(assertName, errors == 0);

		kosm_delete_mutex(id);
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests — mobile OS simulation
// ============================================================

static void
test_mobile_ui_simulation()
{
	trace("\n--- test_mobile_ui_simulation ---\n");
	trace("  simulating: 1 UI thread (60fps) + 3 background workers\n");
	bigtime_t t0 = system_time();

	kosm_mutex_id dataMutex = kosm_create_mutex("ui_data",
		KOSM_MUTEX_PRIO_INHERIT);
	kosm_mutex_id renderMutex = kosm_create_mutex("ui_render", 0);
	trace("    dataMutex=%d (PI), renderMutex=%d\n",
		(int)dataMutex, (int)renderMutex);
	TEST_ASSERT("create data mutex", dataMutex >= 0);
	TEST_ASSERT("create render mutex", renderMutex >= 0);

	volatile int32 sharedData = 0;

	// UI thread at high priority
	mobile_sim_args uiArgs;
	uiArgs.dataMutex = dataMutex;
	uiArgs.renderMutex = renderMutex;
	uiArgs.sharedData = &sharedData;
	uiArgs.iterations = 120; // ~2 seconds at 60fps
	uiArgs.maxLatency = 0;
	uiArgs.errors = 0;

	thread_id uiThread = spawn_thread(mobile_ui_thread_func,
		"ui_thread", 20, &uiArgs);
	trace("    spawned UI thread %d (pri=20)\n", (int)uiThread);

	// background workers at low priority
	const int kWorkers = 3;
	mobile_sim_args bgArgs[kWorkers];
	thread_id bgThreads[kWorkers];

	for (int i = 0; i < kWorkers; i++) {
		bgArgs[i].dataMutex = dataMutex;
		bgArgs[i].renderMutex = renderMutex;
		bgArgs[i].sharedData = &sharedData;
		bgArgs[i].iterations = 200;
		bgArgs[i].maxLatency = 0;
		bgArgs[i].errors = 0;

		char name[32];
		snprintf(name, sizeof(name), "bg_worker_%d", i);
		bgThreads[i] = spawn_thread(mobile_bg_worker_func, name,
			5, &bgArgs[i]);
		trace("    spawned BG worker %d (thread %d, pri=5)\n",
			i, (int)bgThreads[i]);
	}

	// start all
	resume_thread(uiThread);
	for (int i = 0; i < kWorkers; i++)
		resume_thread(bgThreads[i]);

	// wait for UI thread first (it's the important one)
	status_t exitVal;
	wait_for_thread(uiThread, &exitVal);

	for (int i = 0; i < kWorkers; i++)
		wait_for_thread(bgThreads[i], &exitVal);

	trace("    UI thread: maxLatency=%lld us, errors=%d\n",
		(long long)uiArgs.maxLatency, uiArgs.errors);
	trace("    sharedData final value: %d\n", (int)sharedData);

	// UI thread should not have missed too many frames
	TEST_ASSERT("UI thread frame errors < 10%",
		uiArgs.errors < uiArgs.iterations / 10);
	TEST_ASSERT("UI max latency < 100ms (mobile budget)",
		uiArgs.maxLatency < 100000);

	kosm_delete_mutex(dataMutex);
	kosm_delete_mutex(renderMutex);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_rapid_lock_unlock()
{
	trace("\n--- test_rapid_lock_unlock ---\n");
	bigtime_t t0 = system_time();

	// single-thread: how fast can we acquire/release?
	kosm_mutex_id id = kosm_create_mutex("test_rapid", 0);
	TEST_ASSERT("create", id >= 0);

	const int kIterations = 10000;
	bigtime_t start = system_time();

	for (int i = 0; i < kIterations; i++) {
		kosm_acquire_mutex(id);
		kosm_release_mutex(id);
	}

	bigtime_t elapsed = system_time() - start;
	bigtime_t perOp = elapsed / kIterations;

	trace("    %d acquire/release cycles in %lld us\n",
		kIterations, (long long)elapsed);
	trace("    average: %lld us per cycle\n", (long long)perOp);
	trace("    throughput: %lld ops/sec\n",
		kIterations * 1000000LL / elapsed);

	TEST_ASSERT("rapid lock/unlock completes", true);
	TEST_ASSERT("average cycle < 100us", perOp < 100);

	kosm_delete_mutex(id);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// GUI
// ============================================================

static const int kMaxLines = 256;
static char sLines[kMaxLines][256];
static int sLineCount = 0;


static void
log_line(const char* fmt, ...)
{
	if (sLineCount >= kMaxLines)
		return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(sLines[sLineCount], sizeof(sLines[0]), fmt, args);
	va_end(args);
	sLineCount++;
}


class ResultView : public BView {
public:
	ResultView()
		:
		BView(BRect(0, 0, 699, 999), "ResultView", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(30, 30, 40);
	}

	virtual void Draw(BRect updateRect)
	{
		BFont font(be_fixed_font);
		font.SetSize(11.0f);
		SetFont(&font);

		float lineHeight = 14.0f;
		float y = 16.0f;

		for (int i = 0; i < sLineCount; i++) {
			if (strstr(sLines[i], "FAIL"))
				SetHighColor(255, 80, 80);
			else if (strstr(sLines[i], "PASS"))
				SetHighColor(80, 220, 80);
			else if (strstr(sLines[i], "==="))
				SetHighColor(255, 220, 100);
			else if (strstr(sLines[i], "---"))
				SetHighColor(130, 170, 255);
			else if (strstr(sLines[i], "STRESS")
				|| strstr(sLines[i], "MOBILE"))
				SetHighColor(255, 180, 60);
			else
				SetHighColor(200, 200, 200);

			DrawString(sLines[i], BPoint(10, y));
			y += lineHeight;
		}
	}
};


class ResultWindow : public BWindow {
public:
	ResultWindow()
		:
		BWindow(BRect(80, 50, 780, 1050), "KosmMutex Test Suite",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new ResultView());
	}
};


class KosmMutexTestApp : public BApplication {
public:
	KosmMutexTestApp()
		:
		BApplication("application/x-vnd.KosmOS-MutexTest")
	{
	}

	virtual void ReadyToRun()
	{
		_OpenTrace();
		_RunTests();
		(new ResultWindow())->Show();
	}

private:
	void _OpenTrace()
	{
		BPath desktop;
		if (find_directory(B_DESKTOP_DIRECTORY, &desktop) != B_OK)
			return;
		BPath path(desktop);
		path.Append("kosm_mutex_trace.log");
		sTraceFile = fopen(path.Path(), "w");
		if (sTraceFile) {
			trace("# KosmMutex test trace\n");
			trace("# date: %s %s\n", __DATE__, __TIME__);
			trace("# system_time at start: %lld us\n",
				(long long)system_time());
			trace("# main thread: %d, team: %d\n",
				(int)find_thread(NULL), (int)getpid());
			trace("#\n");
		}
	}

	void _RunTest(const char* name, void (*func)(), const char* category)
	{
		int prevPass = sPassCount;
		int prevFail = sFailCount;

		bigtime_t t0 = system_time();
		func();
		bigtime_t dt = system_time() - t0;

		int newPass = sPassCount - prevPass;
		int newFail = sFailCount - prevFail;

		if (newFail == 0) {
			log_line("PASS  %s  (%d checks, %lld us)",
				name, newPass, (long long)dt);
			trace("  [%s] PASS: %s  (%d checks, %lld us)\n",
				category, name, newPass, (long long)dt);
		} else {
			log_line("FAIL  %s  (%d pass, %d fail, %lld us)",
				name, newPass, newFail, (long long)dt);
			trace("  [%s] FAIL: %s  (%d pass, %d fail, %lld us)\n",
				category, name, newPass, newFail, (long long)dt);
		}
	}

	void _RunTests()
	{
		bigtime_t totalStart = system_time();

		log_line("=== KosmMutex Test Suite ===");
		log_line("");

		// Basic tests
		log_line("--- Basic ---");
		trace("\n# ========== BASIC ==========\n");
		_RunTest("create/delete",       test_create_delete, "basic");
		_RunTest("acquire/release",     test_acquire_release, "basic");
		_RunTest("try_acquire",         test_try_acquire, "basic");
		_RunTest("recursive",           test_recursive, "basic");
		_RunTest("deadlock detection",  test_deadlock_detection, "basic");
		_RunTest("contention (4x50)",   test_contention, "basic");
		_RunTest("timed acquire",       test_timed_acquire, "basic");
		_RunTest("find by name",        test_find_by_name, "basic");
		_RunTest("not-owner release",   test_not_owner_release, "basic");
		log_line("");

		// Robust mutex
		log_line("--- Robust Mutex ---");
		trace("\n# ========== ROBUST ==========\n");
		_RunTest("owner death",              test_owner_death, "robust");
		_RunTest("not-recoverable",          test_not_recoverable, "robust");
		_RunTest("multi-waiter death",       test_multi_waiter_owner_death, "robust");
		_RunTest("recursive owner death",    test_recursive_owner_death, "robust");
		_RunTest("delete with waiters",      test_delete_with_waiters, "robust");
		log_line("");

		// Features
		log_line("--- Features ---");
		trace("\n# ========== FEATURES ==========\n");
		_RunTest("shared mutex flag",        test_shared_mutex, "feat");
		_RunTest("priority inheritance",     test_priority_inheritance, "feat");
		_RunTest("signal interrupt",         test_signal_interrupt, "feat");
		log_line("");

		// Info
		log_line("--- Info ---");
		trace("\n# ========== INFO ==========\n");
		_RunTest("get_mutex_info",           test_get_mutex_info, "info");
		_RunTest("get_next_mutex_info",      test_get_next_mutex_info, "info");
		log_line("");

		// Stress
		log_line("--- STRESS ---");
		trace("\n# ========== STRESS ==========\n");
		_RunTest("heavy contention 16x1000", test_stress_heavy_contention, "stress");
		_RunTest("create/destroy churn 8x200", test_stress_create_destroy_churn, "stress");
		_RunTest("mixed flag combos",        test_stress_mixed_flags, "stress");
		_RunTest("rapid lock/unlock 10000",  test_rapid_lock_unlock, "stress");
		log_line("");

		// Mobile simulation
		log_line("--- MOBILE SIM ---");
		trace("\n# ========== MOBILE SIM ==========\n");
		_RunTest("UI + 3 BG workers",       test_mobile_ui_simulation, "mobile");
		log_line("");

		bigtime_t totalTime = system_time() - totalStart;

		log_line("=== Results: %d passed, %d failed  (%lld us total) ===",
			sPassCount, sFailCount, (long long)totalTime);

		trace("\n# ================================\n");
		trace("# FINAL: %d passed, %d failed\n", sPassCount, sFailCount);
		trace("# total time: %lld us\n", (long long)totalTime);
		trace("# ================================\n");

		if (sTraceFile) {
			fclose(sTraceFile);
			sTraceFile = NULL;
		}

		log_line("");
		log_line("Trace: ~/Desktop/kosm_mutex_trace.log");
	}
};


int
main()
{
	KosmMutexTestApp app;
	app.Run();
	return 0;
}
