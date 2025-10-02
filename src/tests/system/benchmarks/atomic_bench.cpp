/*
 * Copyright 2024, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Atomic Operations Performance Benchmark Suite
 * Measures performance characteristics of atomic operations with different
 * memory ordering semantics across single-threaded and multi-threaded
 * scenarios.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <SupportDefs.h>
#include <atomic_ordered.h>

/* Benchmark configuration */
#define ITERATIONS 10000000
#define THREAD_COUNT 4
#define WARMUP_ITERATIONS 1000000

/* Architecture detection */
#ifdef __x86_64__
	#define ARCH_NAME "x86_64"
#elif defined(__i386__)
	#define ARCH_NAME "x86"
#elif defined(__aarch64__)
	#define ARCH_NAME "ARM64"
#elif defined(__arm__)
	#define ARCH_NAME "ARM32"
#elif defined(__riscv)
	#define ARCH_NAME "RISC-V"
#else
	#define ARCH_NAME "Unknown"
#endif


/* Thread synchronization structure */
typedef struct {
	int32 ready_count;
	int32 start_flag;
	int32 complete_count;
	int32* shared_counter;
	int32 iterations;
	memory_order_t order;
	int32 thread_id;
	bigtime_t thread_duration;
} thread_data_t;


/* Helper function to format large numbers with thousands separators */
static void
format_number(int64 num, char* buffer, size_t size)
{
	char temp[64];
	snprintf(temp, sizeof(temp), "%lld", num);

	int len = strlen(temp);
	int commas = (len - 1) / 3;
	int pos = 0;

	for (int i = 0; i < len; i++) {
		if (i > 0 && (len - i) % 3 == 0) {
			buffer[pos++] = ',';
		}
		buffer[pos++] = temp[i];
	}
	buffer[pos] = '\0';
}


/* Calculate operations per second with precision */
static void
print_performance(const char* label, bigtime_t duration, int64 iterations)
{
	double ops_per_sec = (double)iterations / ((double)duration / 1000000.0);
	double ns_per_op = ((double)duration * 1000.0) / (double)iterations;

	char formatted_ops[64];
	char formatted_iter[64];

	format_number((int64)ops_per_sec, formatted_ops, sizeof(formatted_ops));
	format_number(iterations, formatted_iter, sizeof(formatted_iter));

	if (ops_per_sec >= 1000000000.0) {
		printf("  %-32s %7.1f G ops/sec  (%6.1f ns/op)  [%s iters]\n",
		       label, ops_per_sec / 1000000000.0, ns_per_op, formatted_iter);
	} else if (ops_per_sec >= 1000000.0) {
		printf("  %-32s %7.1f M ops/sec  (%6.1f ns/op)  [%s iters]\n",
		       label, ops_per_sec / 1000000.0, ns_per_op, formatted_iter);
	} else if (ops_per_sec >= 1000.0) {
		printf("  %-32s %7.1f K ops/sec  (%6.1f ns/op)  [%s iters]\n",
		       label, ops_per_sec / 1000.0, ns_per_op, formatted_iter);
	} else {
		printf("  %-32s %7.1f   ops/sec  (%6.1f ns/op)  [%s iters]\n",
		       label, ops_per_sec, ns_per_op, formatted_iter);
	}
}


/* Benchmark 1: Single-threaded atomic increment with different orderings */
static void
benchmark_relaxed_ordering(void)
{
	printf("\n[1] Single-threaded Atomic Increment\n");
	printf("=====================================\n");

	int32 counter = 0;
	bigtime_t start, end;

	/* Warmup */
	for (int i = 0; i < WARMUP_ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_RELAXED);
	}

	/* Benchmark: Relaxed ordering */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_RELAXED);
	}
	end = system_time();
	print_performance("Relaxed ordering", end - start, ITERATIONS);

	/* Benchmark: Acquire ordering */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_ACQUIRE);
	}
	end = system_time();
	print_performance("Acquire ordering", end - start, ITERATIONS);

	/* Benchmark: Release ordering */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_RELEASE);
	}
	end = system_time();
	print_performance("Release ordering", end - start, ITERATIONS);

	/* Benchmark: Acq_rel ordering */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_ACQ_REL);
	}
	end = system_time();
	print_performance("Acq_rel ordering", end - start, ITERATIONS);

	/* Benchmark: Seq_cst ordering (new API) */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_SEQ_CST);
	}
	end = system_time();
	print_performance("Seq_cst ordering (new API)", end - start, ITERATIONS);

	/* Benchmark: Legacy atomic_add (seq_cst) */
	counter = 0;
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_add(&counter, 1);
	}
	end = system_time();
	print_performance("Legacy atomic_add (seq_cst)", end - start, ITERATIONS);
}


/* Benchmark 2: CAS retry loop performance */
static void
benchmark_cas_performance(void)
{
	printf("\n[2] Compare-And-Swap Performance\n");
	printf("=================================\n");

	int32 counter = 0;
	bigtime_t start, end;
	int64 retry_count = 0;

	/* Weak CAS with relaxed failure ordering */
	counter = 0;
	retry_count = 0;
	start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		int32 old_val = atomic_load_ordered(&counter, B_MEMORY_ORDER_RELAXED);
		int32 retries = 0;
		while (!atomic_compare_exchange_weak_ordered(&counter, &old_val,
		                                            old_val + 1,
		                                            B_MEMORY_ORDER_RELEASE,
		                                            B_MEMORY_ORDER_RELAXED)) {
			retries++;
		}
		retry_count += retries;
	}

	end = system_time();
	print_performance("Weak CAS (relaxed failure)", end - start, ITERATIONS);
	printf("    Average retries per operation: %.2f\n",
	       (double)retry_count / ITERATIONS);

	/* Strong CAS with seq_cst */
	counter = 0;
	retry_count = 0;
	start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		int32 expected = i;
		int32 retries = 0;
		while (!atomic_compare_exchange_strong_ordered(&counter, &expected,
		                                              expected + 1,
		                                              B_MEMORY_ORDER_SEQ_CST,
		                                              B_MEMORY_ORDER_SEQ_CST)) {
			retries++;
			expected = counter;
		}
		retry_count += retries;
	}

	end = system_time();
	print_performance("Strong CAS (seq_cst)", end - start, ITERATIONS);
	printf("    Average retries per operation: %.2f\n",
	       (double)retry_count / ITERATIONS);

	/* Legacy test_and_set */
	counter = 0;
	start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		atomic_test_and_set(&counter, i + 1, i);
	}

	end = system_time();
	print_performance("Legacy test_and_set", end - start, ITERATIONS);
}


/* Thread worker for multi-core scaling test */
static int32
multicore_worker(void* data)
{
	thread_data_t* td = (thread_data_t*)data;

	/* Signal ready */
	atomic_fetch_add_ordered(&td->ready_count, 1, B_MEMORY_ORDER_RELEASE);

	/* Wait for start signal */
	while (atomic_load_ordered(&td->start_flag, B_MEMORY_ORDER_ACQUIRE) == 0) {
		snooze(100);
	}

	/* Perform atomic operations */
	bigtime_t start = system_time();

	for (int i = 0; i < td->iterations; i++) {
		atomic_fetch_add_ordered(td->shared_counter, 1, td->order);
	}

	bigtime_t end = system_time();
	td->thread_duration = end - start;

	/* Signal completion */
	atomic_fetch_add_ordered(&td->complete_count, 1, B_MEMORY_ORDER_RELEASE);

	return 0;
}


/* Benchmark 3: Multi-core scaling */
static void
benchmark_multicore_scaling(void)
{
	printf("\n[3] Multi-core Scaling (Shared Counter)\n");
	printf("========================================\n");

	int32 thread_counts[] = {1, 2, 4, 8};
	int num_configs = sizeof(thread_counts) / sizeof(thread_counts[0]);

	/* Test with relaxed ordering */
	printf("\nRelaxed ordering:\n");
	double baseline_perf = 0.0;

	for (int cfg = 0; cfg < num_configs; cfg++) {
		int num_threads = thread_counts[cfg];
		int32 shared_counter = 0;
		int32 ready_count = 0;
		int32 start_flag = 0;
		int32 complete_count = 0;

		thread_data_t thread_data[8];
		thread_id threads[8];

		/* Create threads */
		for (int i = 0; i < num_threads; i++) {
			thread_data[i].ready_count = &ready_count;
			thread_data[i].start_flag = &start_flag;
			thread_data[i].complete_count = &complete_count;
			thread_data[i].shared_counter = &shared_counter;
			thread_data[i].iterations = ITERATIONS / num_threads;
			thread_data[i].order = B_MEMORY_ORDER_RELAXED;
			thread_data[i].thread_id = i;
			thread_data[i].thread_duration = 0;

			char thread_name[32];
			snprintf(thread_name, sizeof(thread_name), "atomic_worker_%d", i);
			threads[i] = spawn_thread(multicore_worker, thread_name,
			                         B_NORMAL_PRIORITY, &thread_data[i]);
			resume_thread(threads[i]);
		}

		/* Wait for all threads ready */
		while (atomic_load_ordered(&ready_count, B_MEMORY_ORDER_ACQUIRE)
		       < num_threads) {
			snooze(1000);
		}

		/* Start all threads */
		bigtime_t start = system_time();
		atomic_store_ordered(&start_flag, 1, B_MEMORY_ORDER_RELEASE);

		/* Wait for completion */
		while (atomic_load_ordered(&complete_count, B_MEMORY_ORDER_ACQUIRE)
		       < num_threads) {
			snooze(1000);
		}
		bigtime_t end = system_time();

		/* Wait for threads to exit */
		for (int i = 0; i < num_threads; i++) {
			status_t exit_value;
			wait_for_thread(threads[i], &exit_value);
		}

		/* Calculate statistics */
		double total_perf = (double)ITERATIONS / ((double)(end - start) / 1000000.0);

		if (num_threads == 1) {
			baseline_perf = total_perf;
		}

		double speedup = total_perf / baseline_perf;

		char perf_str[64];
		format_number((int64)total_perf, perf_str, sizeof(perf_str));

		printf("  %d thread%s:  %s ops/sec  (%.2fx speedup)\n",
		       num_threads, num_threads > 1 ? "s" : " ",
		       perf_str, speedup);

		/* Verify correctness */
		if (shared_counter != ITERATIONS) {
			printf("    WARNING: Counter mismatch! Expected %d, got %d\n",
			       ITERATIONS, shared_counter);
		}
	}

	/* Test with seq_cst ordering */
	printf("\nSeq_cst ordering:\n");
	baseline_perf = 0.0;

	for (int cfg = 0; cfg < num_configs; cfg++) {
		int num_threads = thread_counts[cfg];
		int32 shared_counter = 0;
		int32 ready_count = 0;
		int32 start_flag = 0;
		int32 complete_count = 0;

		thread_data_t thread_data[8];
		thread_id threads[8];

		/* Create threads */
		for (int i = 0; i < num_threads; i++) {
			thread_data[i].ready_count = &ready_count;
			thread_data[i].start_flag = &start_flag;
			thread_data[i].complete_count = &complete_count;
			thread_data[i].shared_counter = &shared_counter;
			thread_data[i].iterations = ITERATIONS / num_threads;
			thread_data[i].order = B_MEMORY_ORDER_SEQ_CST;
			thread_data[i].thread_id = i;
			thread_data[i].thread_duration = 0;

			char thread_name[32];
			snprintf(thread_name, sizeof(thread_name), "atomic_worker_%d", i);
			threads[i] = spawn_thread(multicore_worker, thread_name,
			                         B_NORMAL_PRIORITY, &thread_data[i]);
			resume_thread(threads[i]);
		}

		/* Wait for all threads ready */
		while (atomic_load_ordered(&ready_count, B_MEMORY_ORDER_ACQUIRE)
		       < num_threads) {
			snooze(1000);
		}

		/* Start all threads */
		bigtime_t start = system_time();
		atomic_store_ordered(&start_flag, 1, B_MEMORY_ORDER_RELEASE);

		/* Wait for completion */
		while (atomic_load_ordered(&complete_count, B_MEMORY_ORDER_ACQUIRE)
		       < num_threads) {
			snooze(1000);
		}
		bigtime_t end = system_time();

		/* Wait for threads to exit */
		for (int i = 0; i < num_threads; i++) {
			status_t exit_value;
			wait_for_thread(threads[i], &exit_value);
		}

		/* Calculate statistics */
		double total_perf = (double)ITERATIONS / ((double)(end - start) / 1000000.0);

		if (num_threads == 1) {
			baseline_perf = total_perf;
		}

		double speedup = total_perf / baseline_perf;

		char perf_str[64];
		format_number((int64)total_perf, perf_str, sizeof(perf_str));

		printf("  %d thread%s:  %s ops/sec  (%.2fx speedup)\n",
		       num_threads, num_threads > 1 ? "s" : " ",
		       perf_str, speedup);
	}
}


/* Benchmark 4: Memory barrier overhead */
static void
benchmark_memory_barriers(void)
{
	printf("\n[4] Memory Barrier Overhead\n");
	printf("===========================\n");

	bigtime_t start, end;

	/* Baseline: No operation */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		__asm__ __volatile__("" ::: "memory");
	}
	end = system_time();
	print_performance("Compiler barrier only", end - start, ITERATIONS);

	/* Relaxed fence (should be same as compiler barrier) */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_thread_fence(B_MEMORY_ORDER_RELAXED);
	}
	end = system_time();
	print_performance("Relaxed fence", end - start, ITERATIONS);

	/* Acquire fence */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_thread_fence(B_MEMORY_ORDER_ACQUIRE);
	}
	end = system_time();
	print_performance("Acquire fence", end - start, ITERATIONS);

	/* Release fence */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_thread_fence(B_MEMORY_ORDER_RELEASE);
	}
	end = system_time();
	print_performance("Release fence", end - start, ITERATIONS);

	/* Acq_rel fence */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_thread_fence(B_MEMORY_ORDER_ACQ_REL);
	}
	end = system_time();
	print_performance("Acq_rel fence", end - start, ITERATIONS);

	/* Seq_cst fence */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_thread_fence(B_MEMORY_ORDER_SEQ_CST);
	}
	end = system_time();
	print_performance("Seq_cst fence", end - start, ITERATIONS);
}


/* Benchmark 5: Load/Store operations */
static void
benchmark_load_store(void)
{
	printf("\n[5] Atomic Load/Store Operations\n");
	printf("=================================\n");

	int32 value = 42;
	bigtime_t start, end;
	volatile int32 sink;

	/* Relaxed load */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		sink = atomic_load_ordered(&value, B_MEMORY_ORDER_RELAXED);
	}
	end = system_time();
	print_performance("Load (relaxed)", end - start, ITERATIONS);

	/* Acquire load */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		sink = atomic_load_ordered(&value, B_MEMORY_ORDER_ACQUIRE);
	}
	end = system_time();
	print_performance("Load (acquire)", end - start, ITERATIONS);

	/* Seq_cst load */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		sink = atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST);
	}
	end = system_time();
	print_performance("Load (seq_cst)", end - start, ITERATIONS);

	/* Relaxed store */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_store_ordered(&value, i, B_MEMORY_ORDER_RELAXED);
	}
	end = system_time();
	print_performance("Store (relaxed)", end - start, ITERATIONS);

	/* Release store */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_store_ordered(&value, i, B_MEMORY_ORDER_RELEASE);
	}
	end = system_time();
	print_performance("Store (release)", end - start, ITERATIONS);

	/* Seq_cst store */
	start = system_time();
	for (int i = 0; i < ITERATIONS; i++) {
		atomic_store_ordered(&value, i, B_MEMORY_ORDER_SEQ_CST);
	}
	end = system_time();
	print_performance("Store (seq_cst)", end - start, ITERATIONS);

	(void)sink; /* Prevent unused variable warning */
}


int
main(int argc, char* argv[])
{
	printf("===========================================\n");
	printf("  Atomic Operations Benchmark Suite\n");
	printf("===========================================\n");
	printf("Architecture: %s\n", ARCH_NAME);
	printf("Compiler:     GCC %d.%d.%d\n",
	       __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
	printf("Iterations:   %d (per test)\n", ITERATIONS);
	printf("Threads:      Up to %d\n", THREAD_COUNT);
	printf("-------------------------------------------\n");

	/* Run all benchmarks */
	benchmark_relaxed_ordering();
	benchmark_cas_performance();
	benchmark_multicore_scaling();
	benchmark_memory_barriers();
	benchmark_load_store();

	printf("\n===========================================\n");
	printf("  Benchmark Complete\n");
	printf("===========================================\n");

	return 0;
}
