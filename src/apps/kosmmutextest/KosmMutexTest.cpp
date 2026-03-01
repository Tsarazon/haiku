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

#include <stdio.h>
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


// Logs a syscall with its result in strerror + hex form
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


// -- thread helpers --

static status_t
acquire_and_exit_thread(void* data)
{
	kosm_mutex_id id = (kosm_mutex_id)(addr_t)data;
	status_t err = kosm_acquire_mutex(id);
	trace("    [thread %d] acquire_and_exit: acquire -> %s (0x%08x)\n",
		(int)find_thread(NULL), strerror(err), (unsigned)err);
	// exit without releasing — simulates owner death
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


// -- tests --

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

	// try_acquire from another thread while we hold it
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

	// check recursion count via info
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
	args.timeout = 100000; // 100ms
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

	// check that actual elapsed time is close to requested timeout
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

	// check mutex state via info before acquire
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

	// release WITHOUT calling mark_consistent — triggers NOT_RECOVERABLE
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

	trace("    iterating mutexes for team 0 (current):\n");
	while (kosm_get_next_mutex_info(0, &cookie, &info) == B_OK) {
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


// -- GUI --

static const int kMaxLines = 128;
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
		BView(BRect(0, 0, 599, 799), "ResultView", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(30, 30, 40);
	}

	virtual void Draw(BRect updateRect)
	{
		BFont font(be_fixed_font);
		font.SetSize(12.0f);
		SetFont(&font);

		float lineHeight = 16.0f;
		float y = 20.0f;

		for (int i = 0; i < sLineCount; i++) {
			if (strstr(sLines[i], "FAIL"))
				SetHighColor(255, 80, 80);
			else if (strstr(sLines[i], "PASS"))
				SetHighColor(80, 220, 80);
			else if (strstr(sLines[i], "==="))
				SetHighColor(255, 220, 100);
			else if (strstr(sLines[i], "---"))
				SetHighColor(130, 170, 255);
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
		BWindow(BRect(100, 100, 700, 900), "KosmMutex Test",
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

	void _RunTests()
	{
		bigtime_t totalStart = system_time();

		log_line("=== KosmMutex Test Suite ===");
		log_line("");

		struct {
			const char* name;
			void (*func)();
		} tests[] = {
			{ "create/delete",       test_create_delete },
			{ "acquire/release",     test_acquire_release },
			{ "try_acquire",         test_try_acquire },
			{ "recursive",           test_recursive },
			{ "deadlock detection",  test_deadlock_detection },
			{ "contention (4x50)",   test_contention },
			{ "timed acquire",       test_timed_acquire },
			{ "find by name",        test_find_by_name },
			{ "not-owner release",   test_not_owner_release },
			{ "owner death",         test_owner_death },
			{ "not-recoverable",     test_not_recoverable },
			{ "get_mutex_info",      test_get_mutex_info },
			{ "get_next_mutex_info", test_get_next_mutex_info },
		};

		int count = sizeof(tests) / sizeof(tests[0]);
		int prevPass, prevFail;

		for (int i = 0; i < count; i++) {
			prevPass = sPassCount;
			prevFail = sFailCount;

			bigtime_t t0 = system_time();
			tests[i].func();
			bigtime_t dt = system_time() - t0;

			int newPass = sPassCount - prevPass;
			int newFail = sFailCount - prevFail;

			if (newFail == 0) {
				log_line("PASS  %s  (%d checks, %lld us)",
					tests[i].name, newPass, (long long)dt);
			} else {
				log_line("FAIL  %s  (%d pass, %d fail, %lld us)",
					tests[i].name, newPass, newFail, (long long)dt);
			}
		}

		bigtime_t totalTime = system_time() - totalStart;

		log_line("");
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
