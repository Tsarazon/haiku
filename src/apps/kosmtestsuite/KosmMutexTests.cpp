/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmMutex test functions for the unified test suite.
 */


#include "TestCommon.h"

#include <KosmOS.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>


// Thread helpers

static status_t
acquire_and_exit_thread(void* data)
{
	kosm_mutex_id id = (kosm_mutex_id)(addr_t)data;
	kosm_acquire_mutex(id);
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
	args->result = kosm_acquire_mutex_etc(args->id, args->flags,
		args->timeout);
	args->elapsed = system_time() - t0;
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
	return B_OK;
}


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
	args->done = true;
	if (args->result == KOSM_MUTEX_OWNER_DEAD) {
		kosm_mark_mutex_consistent(args->id);
		kosm_release_mutex(args->id);
	} else if (args->result == B_OK) {
		kosm_release_mutex(args->id);
	}
	return B_OK;
}


struct recursive_death_args {
	kosm_mutex_id	id;
	int				depth;
};


static status_t
recursive_acquire_and_exit_thread(void* data)
{
	recursive_death_args* args = (recursive_death_args*)data;
	for (int i = 0; i < args->depth; i++)
		kosm_acquire_mutex(args->id);
	return B_OK;
}


struct pi_holder_args {
	kosm_mutex_id	id;
	int32			originalPriority;
	int32			boostedPriority;
	volatile bool	acquired;
	volatile bool	shouldRelease;
};


struct pi_waiter_args {
	kosm_mutex_id	id;
	volatile bool	started;
	status_t		result;
	bigtime_t		elapsed;
};


static status_t
pi_waiter_thread_func(void* data)
{
	pi_waiter_args* args = (pi_waiter_args*)data;
	args->started = true;
	bigtime_t t0 = system_time();
	args->result = kosm_acquire_mutex_etc(args->id, B_RELATIVE_TIMEOUT,
		2000000);
	args->elapsed = system_time() - t0;
	if (args->result == B_OK)
		kosm_release_mutex(args->id);
	return B_OK;
}


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

	while (!args->shouldRelease)
		snooze(1000);

	get_thread_info(me, &tinfo);
	args->boostedPriority = tinfo.priority;

	kosm_release_mutex(args->id);
	return B_OK;
}


static int32 sSignalReceived = 0;

static void
test_signal_handler(int)
{
	atomic_add(&sSignalReceived, 1);
}


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

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = test_signal_handler;
	sigaction(SIGUSR1, &sa, NULL);

	args->started = true;
	bigtime_t t0 = system_time();
	args->result = kosm_acquire_mutex_etc(args->id,
		B_RELATIVE_TIMEOUT, 5000000);
	args->elapsed = system_time() - t0;
	return B_OK;
}


struct mutex_stress_args {
	kosm_mutex_id	id;
	volatile int64*	counter;
	int				iterations;
	int				errors;
	bigtime_t		maxAcquireTime;
	bigtime_t		totalTime;
};


static status_t
mutex_stress_thread_func(void* data)
{
	mutex_stress_args* args = (mutex_stress_args*)data;
	args->errors = 0;
	args->maxAcquireTime = 0;

	bigtime_t tStart = system_time();
	for (int i = 0; i < args->iterations; i++) {
		bigtime_t t0 = system_time();
		status_t err = kosm_acquire_mutex(args->id);
		bigtime_t acquireTime = system_time() - t0;

		if (acquireTime > args->maxAcquireTime)
			args->maxAcquireTime = acquireTime;

		if (err != B_OK) { args->errors++; continue; }
		(*args->counter)++;
		kosm_release_mutex(args->id);
	}
	args->totalTime = system_time() - tStart;
	return B_OK;
}


struct mutex_churn_args {
	int			iterations;
	int			errors;
	bigtime_t	totalTime;
};


static status_t
mutex_churn_thread_func(void* data)
{
	mutex_churn_args* args = (mutex_churn_args*)data;
	args->errors = 0;

	bigtime_t tStart = system_time();
	for (int i = 0; i < args->iterations; i++) {
		char name[B_OS_NAME_LENGTH];
		snprintf(name, sizeof(name), "churn_%d_%d",
			(int)find_thread(NULL), i);

		kosm_mutex_id id = kosm_create_mutex(name, 0);
		if (id < 0) { args->errors++; continue; }

		kosm_acquire_mutex(id);
		kosm_release_mutex(id);
		kosm_delete_mutex(id);
	}
	args->totalTime = system_time() - tStart;
	return B_OK;
}


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
		bigtime_t t0 = system_time();

		status_t err = kosm_acquire_mutex_etc(args->dataMutex,
			B_RELATIVE_TIMEOUT, 16667);
		if (err != B_OK) { args->errors++; continue; }

		int32 val = *args->sharedData;
		kosm_release_mutex(args->dataMutex);

		kosm_acquire_mutex(args->renderMutex);
		(void)val;
		kosm_release_mutex(args->renderMutex);

		bigtime_t latency = system_time() - t0;
		if (latency > args->maxLatency)
			args->maxLatency = latency;

		snooze(1000);
	}
	return B_OK;
}


static status_t
mobile_bg_worker_func(void* data)
{
	mobile_sim_args* args = (mobile_sim_args*)data;

	for (int i = 0; i < args->iterations; i++) {
		kosm_acquire_mutex(args->dataMutex);
		(*args->sharedData)++;
		snooze(500);
		kosm_release_mutex(args->dataMutex);
		snooze(2000);
	}
	return B_OK;
}


// Tests — basic

static void test_create_delete()
{
	trace("\n--- test_create_delete ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_cd", 0);
	TEST_ASSERT("create returns valid id", id >= 0);
	status_t err = kosm_delete_mutex(id);
	TEST_ASSERT("delete succeeds", err == B_OK);
	err = kosm_delete_mutex(id);
	TEST_ASSERT("double delete fails", err != B_OK);
}


static void test_acquire_release()
{
	trace("\n--- test_acquire_release ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_ar", 0);
	TEST_ASSERT("create", id >= 0);
	status_t err = kosm_acquire_mutex(id);
	TEST_ASSERT("acquire", err == B_OK);
	err = kosm_release_mutex(id);
	TEST_ASSERT("release", err == B_OK);
	kosm_delete_mutex(id);
}


static void test_try_acquire()
{
	trace("\n--- test_try_acquire ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_try", 0);
	TEST_ASSERT("create", id >= 0);
	status_t err = kosm_try_acquire_mutex(id);
	TEST_ASSERT("try_acquire on free mutex", err == B_OK);

	try_acquire_args args;
	args.id = id;
	args.result = B_OK;
	thread_id tid = spawn_thread(try_acquire_thread_func, "try_thread",
		B_NORMAL_PRIORITY, &args);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	TEST_ASSERT("try_acquire from other thread returns WOULD_BLOCK",
		args.result == B_WOULD_BLOCK);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_recursive()
{
	trace("\n--- test_recursive ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_rec", KOSM_MUTEX_RECURSIVE);
	TEST_ASSERT("create recursive", id >= 0);

	TEST_ASSERT("acquire 1", kosm_acquire_mutex(id) == B_OK);
	TEST_ASSERT("acquire 2 (recursive)", kosm_acquire_mutex(id) == B_OK);
	TEST_ASSERT("acquire 3 (recursive)", kosm_acquire_mutex(id) == B_OK);

	kosm_mutex_info info;
	kosm_get_mutex_info(id, &info);
	TEST_ASSERT("recursion count is 3", info.recursion == 3);

	kosm_release_mutex(id);
	kosm_release_mutex(id);
	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_deadlock_detection()
{
	trace("\n--- test_deadlock_detection ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_dl", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);
	status_t err = kosm_try_acquire_mutex(id);
	TEST_ASSERT("self-deadlock detected",
		err == B_WOULD_BLOCK || err == KOSM_MUTEX_DEADLOCK);
	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_contention()
{
	trace("\n--- test_contention ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_cont", 0);
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
		char name[32];
		snprintf(name, sizeof(name), "contention_%d", i);
		threads[i] = spawn_thread(contention_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
	}

	TEST_ASSERT("counter correct (serialized access)",
		counter == kThreadCount * kIterations);
	kosm_delete_mutex(id);
}


static void test_timed_acquire()
{
	trace("\n--- test_timed_acquire ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_timed", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);

	timed_acquire_args args;
	args.id = id;
	args.flags = B_RELATIVE_TIMEOUT;
	args.timeout = 100000;

	thread_id tid = spawn_thread(timed_acquire_thread_func, "timed_thread",
		B_NORMAL_PRIORITY, &args);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("timed acquire expired", args.result == B_TIMED_OUT);
	bigtime_t drift = args.elapsed - args.timeout;
	TEST_ASSERT("timeout accuracy (drift < 50ms)",
		drift >= 0 && drift < 50000);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_find_by_name()
{
	trace("\n--- test_find_by_name ---\n");
	kosm_mutex_id id = kosm_create_mutex("kosm_find_test", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_mutex_id found = kosm_find_mutex("kosm_find_test");
	TEST_ASSERT("find returns valid handle", found >= 0);
	TEST_ASSERT("find nonexistent fails",
		kosm_find_mutex("nonexistent_mutex_xyz") < 0);
	kosm_delete_mutex(id);
}


static void test_not_owner_release()
{
	trace("\n--- test_not_owner_release ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_notown", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);

	release_other_args args;
	args.id = id;
	args.result = B_OK;
	thread_id tid = spawn_thread(release_from_other_thread_func,
		"notown_thread", B_NORMAL_PRIORITY, &args);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("release from non-owner fails",
		args.result == KOSM_MUTEX_NOT_OWNER);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


// Robust mutex

static void test_owner_death()
{
	trace("\n--- test_owner_death ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_death", 0);
	TEST_ASSERT("create", id >= 0);

	thread_id tid = spawn_thread(acquire_and_exit_thread, "dying_thread",
		B_NORMAL_PRIORITY, (void*)(addr_t)id);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	status_t err = kosm_acquire_mutex(id);
	TEST_ASSERT("acquire after owner death returns OWNER_DEAD",
		err == KOSM_MUTEX_OWNER_DEAD);
	TEST_ASSERT("mark_consistent succeeds",
		kosm_mark_mutex_consistent(id) == B_OK);
	kosm_release_mutex(id);

	err = kosm_acquire_mutex(id);
	TEST_ASSERT("acquire after recovery", err == B_OK);
	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_not_recoverable()
{
	trace("\n--- test_not_recoverable ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_norec", 0);
	TEST_ASSERT("create", id >= 0);

	thread_id tid = spawn_thread(acquire_and_exit_thread, "dying_thread2",
		B_NORMAL_PRIORITY, (void*)(addr_t)id);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	status_t err = kosm_acquire_mutex(id);
	TEST_ASSERT("get OWNER_DEAD", err == KOSM_MUTEX_OWNER_DEAD);
	kosm_release_mutex(id);

	err = kosm_acquire_mutex(id);
	TEST_ASSERT("acquire returns NOT_RECOVERABLE",
		err == KOSM_MUTEX_NOT_RECOVERABLE);
	kosm_delete_mutex(id);
}


static void test_multi_waiter_owner_death()
{
	trace("\n--- test_multi_waiter_owner_death ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_mw_death", 0);
	TEST_ASSERT("create", id >= 0);

	thread_id holder = spawn_thread(acquire_and_exit_thread,
		"holder_dying", B_NORMAL_PRIORITY, (void*)(addr_t)id);
	resume_thread(holder);
	status_t exitVal;
	wait_for_thread(holder, &exitVal);

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
		resume_thread(waiters[i]);
	}

	for (int i = 0; i < kWaiters; i++)
		wait_for_thread(waiters[i], &exitVal);

	int gotOwnerDead = 0, gotOK = 0, gotNotRecoverable = 0;
	for (int i = 0; i < kWaiters; i++) {
		if (wargs[i].result == KOSM_MUTEX_OWNER_DEAD) gotOwnerDead++;
		else if (wargs[i].result == B_OK) gotOK++;
		else if (wargs[i].result == KOSM_MUTEX_NOT_RECOVERABLE)
			gotNotRecoverable++;
	}

	TEST_ASSERT("at least one waiter got OWNER_DEAD", gotOwnerDead >= 1);
	TEST_ASSERT("all waiters completed",
		gotOwnerDead + gotOK + gotNotRecoverable == kWaiters);
	kosm_delete_mutex(id);
}


static void test_recursive_owner_death()
{
	trace("\n--- test_recursive_owner_death ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_recdeath",
		KOSM_MUTEX_RECURSIVE);
	TEST_ASSERT("create", id >= 0);

	recursive_death_args rdargs;
	rdargs.id = id;
	rdargs.depth = 3;
	thread_id tid = spawn_thread(recursive_acquire_and_exit_thread,
		"rec_dying", B_NORMAL_PRIORITY, &rdargs);
	resume_thread(tid);
	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	status_t err = kosm_acquire_mutex(id);
	TEST_ASSERT("acquire returns OWNER_DEAD",
		err == KOSM_MUTEX_OWNER_DEAD);
	kosm_mark_mutex_consistent(id);
	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_delete_with_waiters()
{
	trace("\n--- test_delete_with_waiters ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_delwait", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);

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

	snooze(50000);
	kosm_release_mutex(id);
	snooze(10000);
	kosm_delete_mutex(id);

	int completed = 0;
	for (int i = 0; i < kWaiters; i++) {
		status_t exitVal;
		wait_for_thread(waiters[i], &exitVal);
		if (wargs[i].done) completed++;
	}
	TEST_ASSERT("all waiter threads finished", completed == kWaiters);
}


// Features

static void test_get_mutex_info()
{
	trace("\n--- test_get_mutex_info ---\n");
	uint32 flags = KOSM_MUTEX_RECURSIVE | KOSM_MUTEX_PRIO_INHERIT;
	kosm_mutex_id id = kosm_create_mutex("test_info", flags);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);

	kosm_mutex_info info;
	memset(&info, 0, sizeof(info));
	status_t err = kosm_get_mutex_info(id, &info);
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
}


static void test_get_next_mutex_info()
{
	trace("\n--- test_get_next_mutex_info ---\n");
	const int kCount = 3;
	kosm_mutex_id ids[kCount];
	char names[kCount][B_OS_NAME_LENGTH];
	for (int i = 0; i < kCount; i++) {
		snprintf(names[i], sizeof(names[i]), "test_iter_%d", i);
		ids[i] = kosm_create_mutex(names[i], 0);
		TEST_ASSERT("create for iteration", ids[i] >= 0);
	}

	int32 cookie = 0;
	kosm_mutex_info info;
	int found = 0;
	team_id myTeam = getpid();
	while (kosm_get_next_mutex_info(myTeam, &cookie, &info) == B_OK) {
		for (int i = 0; i < kCount; i++) {
			if (info.mutex == ids[i]) found++;
		}
	}
	TEST_ASSERT("found all created mutexes", found == kCount);

	for (int i = 0; i < kCount; i++)
		kosm_delete_mutex(ids[i]);
}


static void test_shared_mutex()
{
	trace("\n--- test_shared_mutex ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_shared",
		KOSM_MUTEX_SHARED);
	TEST_ASSERT("create shared", id >= 0);

	kosm_mutex_info info;
	kosm_get_mutex_info(id, &info);
	TEST_ASSERT("SHARED flag stored",
		(info.flags & KOSM_MUTEX_SHARED) != 0);
	kosm_mutex_id foundShared = kosm_find_mutex("test_shared");
	TEST_ASSERT("find shared mutex by name", foundShared >= 0);

	TEST_ASSERT("acquire shared mutex",
		kosm_acquire_mutex(id) == B_OK);
	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


static void test_priority_inheritance()
{
	trace("\n--- test_priority_inheritance ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_pi",
		KOSM_MUTEX_PRIO_INHERIT);
	TEST_ASSERT("create PI mutex", id >= 0);

	pi_holder_args hargs;
	hargs.id = id;
	hargs.originalPriority = 0;
	hargs.boostedPriority = 0;
	hargs.acquired = false;
	hargs.shouldRelease = false;

	thread_id lowThread = spawn_thread(pi_holder_thread_func,
		"pi_low", 5, &hargs);
	resume_thread(lowThread);
	while (!hargs.acquired) snooze(1000);

	pi_waiter_args hiargs;
	hiargs.id = id;
	hiargs.started = false;
	thread_id hiThread = spawn_thread(pi_waiter_thread_func,
		"pi_high", 25, &hiargs);
	resume_thread(hiThread);
	while (!hiargs.started) snooze(1000);
	snooze(50000);

	hargs.shouldRelease = true;

	status_t exitVal;
	wait_for_thread(lowThread, &exitVal);
	wait_for_thread(hiThread, &exitVal);

	TEST_ASSERT("holder was boosted above original priority",
		hargs.boostedPriority > hargs.originalPriority);
	TEST_ASSERT("high-pri thread acquired successfully",
		hiargs.result == B_OK);
	kosm_delete_mutex(id);
}


static void test_signal_interrupt()
{
	trace("\n--- test_signal_interrupt ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_signal", 0);
	TEST_ASSERT("create", id >= 0);
	kosm_acquire_mutex(id);

	interruptible_args iargs;
	iargs.id = id;
	iargs.started = false;
	sSignalReceived = 0;

	thread_id tid = spawn_thread(interruptible_acquire_thread,
		"sig_waiter", B_NORMAL_PRIORITY, &iargs);
	resume_thread(tid);
	while (!iargs.started) snooze(1000);
	snooze(50000);

	send_signal(tid, SIGUSR1);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	TEST_ASSERT("acquire was interrupted or timed out",
		iargs.result == B_INTERRUPTED || iargs.result == B_TIMED_OUT);

	kosm_release_mutex(id);
	kosm_delete_mutex(id);
}


// Stress

static void test_stress_heavy_contention()
{
	trace("\n--- test_stress_heavy_contention ---\n");
	kosm_mutex_id id = kosm_create_mutex("test_stress", 0);
	TEST_ASSERT("create", id >= 0);

	const int kThreadCount = 16;
	const int kIterations = 1000;
	volatile int64 counter = 0;

	mutex_stress_args args[kThreadCount];
	thread_id threads[kThreadCount];

	for (int i = 0; i < kThreadCount; i++) {
		args[i].id = id;
		args[i].counter = &counter;
		args[i].iterations = kIterations;
		char name[32];
		snprintf(name, sizeof(name), "stress_%d", i);
		threads[i] = spawn_thread(mutex_stress_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		totalErrors += args[i].errors;
	}

	TEST_ASSERT("counter correct (16x1000)",
		counter == (int64)kThreadCount * kIterations);
	TEST_ASSERT("no acquire errors", totalErrors == 0);
	kosm_delete_mutex(id);
}


static void test_stress_create_destroy_churn()
{
	trace("\n--- test_stress_create_destroy_churn ---\n");
	const int kThreadCount = 8;
	const int kIterations = 200;

	mutex_churn_args args[kThreadCount];
	thread_id threads[kThreadCount];

	for (int i = 0; i < kThreadCount; i++) {
		args[i].iterations = kIterations;
		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		threads[i] = spawn_thread(mutex_churn_thread_func, name,
			B_NORMAL_PRIORITY, &args[i]);
		resume_thread(threads[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreadCount; i++) {
		status_t exitVal;
		wait_for_thread(threads[i], &exitVal);
		totalErrors += args[i].errors;
	}

	TEST_ASSERT("create/destroy churn no errors", totalErrors == 0);
}


static void test_stress_mixed_flags()
{
	trace("\n--- test_stress_mixed_flags ---\n");
	struct { const char* name; uint32 flags; } configs[] = {
		{ "plain",       0 },
		{ "recursive",   KOSM_MUTEX_RECURSIVE },
		{ "pi",          KOSM_MUTEX_PRIO_INHERIT },
		{ "shared",      KOSM_MUTEX_SHARED },
		{ "rec+pi",      KOSM_MUTEX_RECURSIVE | KOSM_MUTEX_PRIO_INHERIT },
		{ "shared+rec",  KOSM_MUTEX_SHARED | KOSM_MUTEX_RECURSIVE },
		{ "shared+pi",   KOSM_MUTEX_SHARED | KOSM_MUTEX_PRIO_INHERIT },
		{ "all",         KOSM_MUTEX_SHARED | KOSM_MUTEX_RECURSIVE
		                 | KOSM_MUTEX_PRIO_INHERIT },
	};
	int configCount = sizeof(configs) / sizeof(configs[0]);

	for (int c = 0; c < configCount; c++) {
		kosm_mutex_id id = kosm_create_mutex(configs[c].name,
			configs[c].flags);
		if (id < 0) { TEST_ASSERT("create with flags", false); continue; }

		int errors = 0;
		for (int i = 0; i < 100; i++) {
			if (kosm_acquire_mutex(id) != B_OK) { errors++; break; }
			if (configs[c].flags & KOSM_MUTEX_RECURSIVE) {
				kosm_acquire_mutex(id);
				kosm_release_mutex(id);
			}
			kosm_release_mutex(id);
		}

		char assertName[64];
		snprintf(assertName, sizeof(assertName),
			"flag combo '%s' works", configs[c].name);
		TEST_ASSERT(assertName, errors == 0);
		kosm_delete_mutex(id);
	}
}


static void test_rapid_lock_unlock()
{
	trace("\n--- test_rapid_lock_unlock ---\n");
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

	trace("    %d cycles in %lld us, avg %lld us/cycle\n",
		kIterations, (long long)elapsed, (long long)perOp);
	TEST_ASSERT("rapid lock/unlock completes", true);
	TEST_ASSERT("average cycle < 100us", perOp < 100);

	kosm_delete_mutex(id);
}


// Mobile simulation

static void test_mobile_ui_simulation()
{
	trace("\n--- test_mobile_ui_simulation ---\n");
	kosm_mutex_id dataMutex = kosm_create_mutex("ui_data",
		KOSM_MUTEX_PRIO_INHERIT);
	kosm_mutex_id renderMutex = kosm_create_mutex("ui_render", 0);
	TEST_ASSERT("create data mutex", dataMutex >= 0);
	TEST_ASSERT("create render mutex", renderMutex >= 0);

	volatile int32 sharedData = 0;

	mobile_sim_args uiArgs;
	uiArgs.dataMutex = dataMutex;
	uiArgs.renderMutex = renderMutex;
	uiArgs.sharedData = &sharedData;
	uiArgs.iterations = 120;

	thread_id uiThread = spawn_thread(mobile_ui_thread_func,
		"ui_thread", 20, &uiArgs);

	const int kWorkers = 3;
	mobile_sim_args bgArgs[kWorkers];
	thread_id bgThreads[kWorkers];
	for (int i = 0; i < kWorkers; i++) {
		bgArgs[i].dataMutex = dataMutex;
		bgArgs[i].renderMutex = renderMutex;
		bgArgs[i].sharedData = &sharedData;
		bgArgs[i].iterations = 200;
		char name[32];
		snprintf(name, sizeof(name), "bg_worker_%d", i);
		bgThreads[i] = spawn_thread(mobile_bg_worker_func, name,
			5, &bgArgs[i]);
	}

	resume_thread(uiThread);
	for (int i = 0; i < kWorkers; i++)
		resume_thread(bgThreads[i]);

	status_t exitVal;
	wait_for_thread(uiThread, &exitVal);
	for (int i = 0; i < kWorkers; i++)
		wait_for_thread(bgThreads[i], &exitVal);

	TEST_ASSERT("UI thread frame errors < 10%",
		uiArgs.errors < uiArgs.iterations / 10);
	TEST_ASSERT("UI max latency < 100ms (mobile budget)",
		uiArgs.maxLatency < 100000);

	kosm_delete_mutex(dataMutex);
	kosm_delete_mutex(renderMutex);
}


// Cross-process

static void
test_cross_process_shared_mutex()
{
	trace("\n--- test_cross_process_shared_mutex ---\n");

	// Parent creates SHARED mutex, child finds it by name.
	// Both use it to protect a shared memory region.
	kosm_mutex_id id = kosm_create_mutex("xproc_shared_test",
		KOSM_MUTEX_SHARED);
	TEST_ASSERT("create shared mutex", id >= 0);

	// Create shared area for counter
	volatile int32* shared = NULL;
	area_id aid = create_area("xproc_mutex_area", (void**)&shared,
		B_ANY_ADDRESS, B_PAGE_SIZE, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	TEST_ASSERT("create shared area", aid >= 0);
	*shared = 0;

	int pipefd[2];
	TEST_ASSERT("create pipe", pipe(pipefd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(pipefd[0]);
		uint8 result[3] = {0};

		// Delete inherited COW copy and clone parent's area
		area_id myArea = area_for((void*)shared);
		if (myArea >= 0)
			delete_area(myArea);

		volatile int32* childShared = NULL;
		area_id cloned = clone_area("xproc_shared_clone",
			(void**)&childShared, B_ANY_ADDRESS,
			B_READ_AREA | B_WRITE_AREA, aid);
		if (cloned < 0) {
			write(pipefd[1], result, sizeof(result));
			close(pipefd[1]);
			_exit(1);
		}

		// Find mutex by name
		kosm_mutex_id childId = kosm_find_mutex("xproc_shared_test");
		result[0] = (childId >= 0) ? 1 : 0;

		if (childId >= 0) {
			// Increment counter 100 times under lock
			const int kIters = 100;
			int errors = 0;
			for (int i = 0; i < kIters; i++) {
				status_t err = kosm_acquire_mutex_etc(childId,
					B_RELATIVE_TIMEOUT, 1000000);
				if (err != B_OK) { errors++; continue; }
				int32 val = *childShared;
				snooze(1);
				*childShared = val + 1;
				kosm_release_mutex(childId);
			}
			result[1] = (errors == 0) ? 1 : 0;
		}

		result[2] = 0xFF;

		write(pipefd[1], result, sizeof(result));
		close(pipefd[1]);
		delete_area(cloned);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(pipefd[1]);

	// Parent: also increment 100 times under lock
	const int kIters = 100;
	int parentErrors = 0;
	for (int i = 0; i < kIters; i++) {
		status_t err = kosm_acquire_mutex_etc(id,
			B_RELATIVE_TIMEOUT, 1000000);
		if (err != B_OK) { parentErrors++; continue; }
		int32 val = *shared;
		snooze(1);
		*shared = val + 1;
		kosm_release_mutex(id);
	}
	TEST_ASSERT("parent: no acquire errors", parentErrors == 0);

	// Wait for child
	uint8 childResult[3] = {0};
	read(pipefd[0], childResult, sizeof(childResult));
	close(pipefd[0]);

	int status;
	waitpid(child, &status, 0);

	int32 finalCount = *shared;
	trace("    child exit=%d, counter=%d (expected %d)\n",
		WEXITSTATUS(status), (int)finalCount, kIters * 2);
	trace("    child: find=%d, locks=%d, sentinel=%d\n",
		(int)childResult[0], (int)childResult[1],
		(int)childResult[2]);

	TEST_ASSERT("child: found mutex by name",
		childResult[0] == 1);
	TEST_ASSERT("child: all lock/unlock succeeded",
		childResult[1] == 1);
	TEST_ASSERT("child: sentinel received",
		childResult[2] == 0xFF);
	TEST_ASSERT("counter correct (serialized across processes)",
		finalCount == kIters * 2);

	delete_area(aid);
	kosm_delete_mutex(id);
}


// Suite registration

static const TestEntry sMutexTests[] = {
	// Basic
	{ "create/delete",           test_create_delete,           "Basic" },
	{ "acquire/release",         test_acquire_release,         "Basic" },
	{ "try_acquire",             test_try_acquire,             "Basic" },
	{ "recursive",               test_recursive,               "Basic" },
	{ "deadlock detection",      test_deadlock_detection,      "Basic" },
	{ "contention (4x50)",       test_contention,              "Basic" },
	{ "timed acquire",           test_timed_acquire,           "Basic" },
	{ "find by name",            test_find_by_name,            "Basic" },
	{ "not-owner release",       test_not_owner_release,       "Basic" },
	// Robust
	{ "owner death",             test_owner_death,             "Robust Mutex" },
	{ "not-recoverable",         test_not_recoverable,         "Robust Mutex" },
	{ "multi-waiter death",      test_multi_waiter_owner_death, "Robust Mutex" },
	{ "recursive owner death",   test_recursive_owner_death,   "Robust Mutex" },
	{ "delete with waiters",     test_delete_with_waiters,     "Robust Mutex" },
	// Features
	{ "shared mutex flag",       test_shared_mutex,            "Features" },
	{ "priority inheritance",    test_priority_inheritance,    "Features" },
	{ "signal interrupt",        test_signal_interrupt,        "Features" },
	// Info
	{ "get_mutex_info",          test_get_mutex_info,          "Info" },
	{ "get_next_mutex_info",     test_get_next_mutex_info,     "Info" },
	// Stress
	{ "heavy contention 16x1000", test_stress_heavy_contention, "STRESS" },
	{ "create/destroy churn 8x200", test_stress_create_destroy_churn, "STRESS" },
	{ "mixed flag combos",       test_stress_mixed_flags,      "STRESS" },
	{ "rapid lock/unlock 10000", test_rapid_lock_unlock,       "STRESS" },
	// Mobile
	{ "UI + 3 BG workers",      test_mobile_ui_simulation,    "MOBILE SIM" },
	// Cross-Process
	{ "shared mutex cross-process", test_cross_process_shared_mutex, "Cross-Process" },
};


TestSuite
get_mutex_test_suite()
{
	TestSuite suite;
	suite.name = "KosmMutex";
	suite.tests = sMutexTests;
	suite.count = sizeof(sMutexTests) / sizeof(sMutexTests[0]);
	return suite;
}
