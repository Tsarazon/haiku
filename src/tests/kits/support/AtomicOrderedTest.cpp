/*
 * Copyright 2024, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Haiku Development Team
 *
 * Comprehensive atomic operations test suite for ordered atomic API.
 * Tests memory ordering semantics, weak CAS, performance, and backward compatibility.
 */

#include <TestCase.h>
#include <TestSuite.h>
#include <TestSuiteAddon.h>

#include <SupportDefs.h>
#include <OS.h>
#include <atomic_ordered.h>

#include <stdio.h>
#include <string.h>


class AtomicOrderedTest : public CppUnit::TestCase {
public:
	static CppUnit::Test* Suite();

	void testBasicOperations();
	void testMemoryOrdering();
	void testWeakCAS();
	void testWeakCASPerformance();
	void testPerformance();
	void testBackwardCompatibility();
	void testAlignment();
	void testConcurrency();
	void testFences();
	void test64BitOperations();
	void testPointerOperations();
};


//	#pragma mark - Test Implementation


void
AtomicOrderedTest::testBasicOperations()
{
	// Test atomic_store_ordered and atomic_load_ordered
	int32 value = 0;
	atomic_store_ordered(&value, 42, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(42, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_exchange_ordered
	int32 old = atomic_exchange_ordered(&value, 100, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(42, old);
	CPPUNIT_ASSERT_EQUAL(100, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_compare_exchange_strong_ordered - success case
	int32 expected = 100;
	bool success = atomic_compare_exchange_strong_ordered(&value, &expected, 200,
		B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT(success);
	CPPUNIT_ASSERT_EQUAL(100, expected);  // Expected unchanged on success
	CPPUNIT_ASSERT_EQUAL(200, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_compare_exchange_strong_ordered - failure case
	expected = 999;
	success = atomic_compare_exchange_strong_ordered(&value, &expected, 300,
		B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT(!success);
	CPPUNIT_ASSERT_EQUAL(200, expected);  // Expected updated to current value on failure
	CPPUNIT_ASSERT_EQUAL(200, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_fetch_add_ordered
	atomic_store_ordered(&value, 10, B_MEMORY_ORDER_SEQ_CST);
	old = atomic_fetch_add_ordered(&value, 5, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(10, old);
	CPPUNIT_ASSERT_EQUAL(15, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_fetch_sub_ordered
	old = atomic_fetch_sub_ordered(&value, 3, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(15, old);
	CPPUNIT_ASSERT_EQUAL(12, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_fetch_and_ordered
	atomic_store_ordered(&value, 0xFF, B_MEMORY_ORDER_SEQ_CST);
	old = atomic_fetch_and_ordered(&value, 0x0F, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(0xFF, old);
	CPPUNIT_ASSERT_EQUAL(0x0F, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_fetch_or_ordered
	atomic_store_ordered(&value, 0xF0, B_MEMORY_ORDER_SEQ_CST);
	old = atomic_fetch_or_ordered(&value, 0x0F, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(0xF0, old);
	CPPUNIT_ASSERT_EQUAL(0xFF, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	// Test atomic_fetch_xor_ordered
	atomic_store_ordered(&value, 0xFF, B_MEMORY_ORDER_SEQ_CST);
	old = atomic_fetch_xor_ordered(&value, 0x0F, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(0xFF, old);
	CPPUNIT_ASSERT_EQUAL(0xF0, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));
}


void
AtomicOrderedTest::testMemoryOrdering()
{
	/* Test acquire-release ordering with producer-consumer pattern */
	int32 flag = 0;
	int32 data = 0;

	/* Producer: write data, then release flag */
	atomic_store_ordered(&data, 42, B_MEMORY_ORDER_RELAXED);
	atomic_store_ordered(&flag, 1, B_MEMORY_ORDER_RELEASE);

	/* Consumer: acquire flag, then read data */
	while (atomic_load_ordered(&flag, B_MEMORY_ORDER_ACQUIRE) == 0) {
		snooze(1);
	}

	int32 value = atomic_load_ordered(&data, B_MEMORY_ORDER_RELAXED);
	CPPUNIT_ASSERT_EQUAL(42, value);

	/* Test seq_cst ordering (total order guarantee) */
	atomic_store_ordered(&flag, 0, B_MEMORY_ORDER_SEQ_CST);
	atomic_store_ordered(&data, 100, B_MEMORY_ORDER_SEQ_CST);
	atomic_store_ordered(&flag, 1, B_MEMORY_ORDER_SEQ_CST);

	while (atomic_load_ordered(&flag, B_MEMORY_ORDER_SEQ_CST) == 0) {
		snooze(1);
	}

	value = atomic_load_ordered(&data, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(100, value);
}


void
AtomicOrderedTest::testWeakCAS()
{
	int32 value = 100;
	int32 expected = 100;

	/* Weak CAS can spuriously fail - retry in loop */
	bool success = false;
	int attempts = 0;
	const int MAX_ATTEMPTS = 100;

	while (!success && attempts < MAX_ATTEMPTS) {
		expected = 100;
		success = atomic_compare_exchange_weak_ordered(&value, &expected, 200,
			B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
		attempts++;
	}

	CPPUNIT_ASSERT(success);
	CPPUNIT_ASSERT_EQUAL(200, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Test failure case - expected value is updated */
	expected = 999;
	success = atomic_compare_exchange_weak_ordered(&value, &expected, 300,
		B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT(!success);
	CPPUNIT_ASSERT_EQUAL(200, expected);
}


void
AtomicOrderedTest::testWeakCASPerformance()
{
	/* Demonstrate proper weak CAS usage pattern for increment operation */
	int32 counter = 0;

	/* Typical increment loop with weak CAS */
	for (int i = 0; i < 1000; i++) {
		int32 old_val = atomic_load_ordered(&counter, B_MEMORY_ORDER_RELAXED);
		while (!atomic_compare_exchange_weak_ordered(&counter, &old_val, 
			old_val + 1, B_MEMORY_ORDER_RELEASE, B_MEMORY_ORDER_RELAXED)) {
			/* old_val is updated with current value on failure, retry */
		}
	}

	CPPUNIT_ASSERT_EQUAL(1000, atomic_load_ordered(&counter, B_MEMORY_ORDER_SEQ_CST));

	/* Test weak CAS with acquire-release ordering */
	atomic_store_ordered(&counter, 0, B_MEMORY_ORDER_SEQ_CST);

	for (int i = 0; i < 500; i++) {
		int32 old_val = atomic_load_ordered(&counter, B_MEMORY_ORDER_ACQUIRE);
		while (!atomic_compare_exchange_weak_ordered(&counter, &old_val, 
			old_val + 1, B_MEMORY_ORDER_ACQ_REL, B_MEMORY_ORDER_ACQUIRE)) {
			/* Retry on spurious failure */
		}
	}

	CPPUNIT_ASSERT_EQUAL(500, atomic_load_ordered(&counter, B_MEMORY_ORDER_SEQ_CST));
}


void
AtomicOrderedTest::testPerformance()
{
	const int ITERATIONS = 1000000;
	int32 counter = 0;

	/* Benchmark relaxed ordering */
	bigtime_t start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_RELAXED);
	}

	bigtime_t end = system_time();
	bigtime_t duration_relaxed = end - start;

	CPPUNIT_ASSERT_EQUAL(ITERATIONS, 
		atomic_load_ordered(&counter, B_MEMORY_ORDER_SEQ_CST));

	printf("\n[Performance] Atomic increment (RELAXED): %lld ops/sec (%lld μs)\n",
		(ITERATIONS * 1000000LL) / (duration_relaxed > 0 ? duration_relaxed : 1),
		duration_relaxed);

	/* Compare with seq_cst ordering (old API) */
	counter = 0;
	start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		atomic_add(&counter, 1);  /* Old API uses SEQ_CST */
	}

	end = system_time();
	bigtime_t duration_seqcst = end - start;

	CPPUNIT_ASSERT_EQUAL(ITERATIONS, atomic_get(&counter));

	printf("[Performance] Atomic increment (SEQ_CST): %lld ops/sec (%lld μs)\n",
		(ITERATIONS * 1000000LL) / (duration_seqcst > 0 ? duration_seqcst : 1),
		duration_seqcst);

	/* Calculate speedup */
	if (duration_seqcst > 0 && duration_relaxed > 0) {
		double speedup = (double)duration_seqcst / (double)duration_relaxed;
		printf("[Performance] RELAXED speedup over SEQ_CST: %.2fx\n\n", speedup);
	}

	/* Benchmark acquire-release ordering */
	counter = 0;
	start = system_time();

	for (int i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add_ordered(&counter, 1, B_MEMORY_ORDER_ACQ_REL);
	}

	end = system_time();
	bigtime_t duration_acqrel = end - start;

	CPPUNIT_ASSERT_EQUAL(ITERATIONS, 
		atomic_load_ordered(&counter, B_MEMORY_ORDER_SEQ_CST));

	printf("[Performance] Atomic increment (ACQ_REL): %lld ops/sec (%lld μs)\n",
		(ITERATIONS * 1000000LL) / (duration_acqrel > 0 ? duration_acqrel : 1),
		duration_acqrel);
}


void
AtomicOrderedTest::testBackwardCompatibility()
{
	/* Verify old API still works and uses SEQ_CST ordering */
	int32 value = 0;

	/* Old API: atomic_set/get should behave identically to SEQ_CST ordered ops */
	atomic_set(&value, 42);
	CPPUNIT_ASSERT_EQUAL(42, atomic_get(&value));
	CPPUNIT_ASSERT_EQUAL(42, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Old API: atomic_add */
	int32 old = atomic_add(&value, 10);
	CPPUNIT_ASSERT_EQUAL(42, old);
	CPPUNIT_ASSERT_EQUAL(52, atomic_get(&value));

	/* Old API: atomic_and */
	atomic_set(&value, 0xFF);
	old = atomic_and(&value, 0x0F);
	CPPUNIT_ASSERT_EQUAL(0xFF, old);
	CPPUNIT_ASSERT_EQUAL(0x0F, atomic_get(&value));

	/* Old API: atomic_or */
	old = atomic_or(&value, 0xF0);
	CPPUNIT_ASSERT_EQUAL(0x0F, old);
	CPPUNIT_ASSERT_EQUAL(0xFF, atomic_get(&value));

	/* Old API: atomic_get_and_set */
	old = atomic_get_and_set(&value, 123);
	CPPUNIT_ASSERT_EQUAL(0xFF, old);
	CPPUNIT_ASSERT_EQUAL(123, atomic_get(&value));

	/* Old API: atomic_test_and_set */
	old = atomic_test_and_set(&value, 456, 123);
	CPPUNIT_ASSERT_EQUAL(123, old);
	CPPUNIT_ASSERT_EQUAL(456, atomic_get(&value));

	/* Verify new API can interoperate with old API */
	atomic_store_ordered(&value, 789, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(789, atomic_get(&value));

	atomic_set(&value, 321);
	CPPUNIT_ASSERT_EQUAL(321, atomic_load_ordered(&value, B_MEMORY_ORDER_SEQ_CST));
}


void
AtomicOrderedTest::testAlignment()
{
	/* Verify atomic operations work on properly aligned addresses */
	int32 value32;
	CPPUNIT_ASSERT(((addr_t)&value32 % sizeof(int32)) == 0);

	atomic_store_ordered(&value32, 42, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(42, atomic_load_ordered(&value32, B_MEMORY_ORDER_SEQ_CST));

	/* Test array alignment */
	int32 array32[10];
	for (size_t i = 0; i < 10; i++) {
		CPPUNIT_ASSERT(((addr_t)&array32[i] % sizeof(int32)) == 0);
		atomic_store_ordered(&array32[i], (int32)i, B_MEMORY_ORDER_SEQ_CST);
		CPPUNIT_ASSERT_EQUAL((int32)i, 
			atomic_load_ordered(&array32[i], B_MEMORY_ORDER_SEQ_CST));
	}

	/* Verify 64-bit alignment */
	int64 value64;
	CPPUNIT_ASSERT(((addr_t)&value64 % sizeof(int64)) == 0);

	atomic_store64_ordered(&value64, 0x123456789ABCDEFLL, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL((int64)0x123456789ABCDEFLL, 
		atomic_load64_ordered(&value64, B_MEMORY_ORDER_SEQ_CST));
}


struct ConcurrencyTestData {
	int32 counter;
	int32 iterations;
	int32 barrier;
	int32 num_threads;
};


static int32
concurrency_worker(void* arg)
{
	ConcurrencyTestData* data = (ConcurrencyTestData*)arg;

	/* Wait for all threads to be ready */
	atomic_fetch_add_ordered(&data->barrier, 1, B_MEMORY_ORDER_ACQ_REL);
	while (atomic_load_ordered(&data->barrier, B_MEMORY_ORDER_ACQUIRE) 
		< data->num_threads) {
		snooze(100);
	}

	/* Perform atomic increments */
	for (int32 i = 0; i < data->iterations; i++) {
		atomic_fetch_add_ordered(&data->counter, 1, B_MEMORY_ORDER_RELAXED);
	}

	return 0;
}


void
AtomicOrderedTest::testConcurrency()
{
	const int32 NUM_THREADS = 4;
	const int32 ITERATIONS_PER_THREAD = 10000;

	ConcurrencyTestData data;
	data.counter = 0;
	data.iterations = ITERATIONS_PER_THREAD;
	data.barrier = 0;
	data.num_threads = NUM_THREADS;

	thread_id threads[NUM_THREADS];

	/* Spawn worker threads */
	for (int32 i = 0; i < NUM_THREADS; i++) {
		char name[64];
		snprintf(name, sizeof(name), "concurrency_worker_%d", (int)i);
		threads[i] = spawn_thread(concurrency_worker, name, 
			B_NORMAL_PRIORITY, &data);
		CPPUNIT_ASSERT(threads[i] >= 0);
	}

	/* Start all threads */
	for (int32 i = 0; i < NUM_THREADS; i++) {
		resume_thread(threads[i]);
	}

	/* Wait for completion */
	for (int32 i = 0; i < NUM_THREADS; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
		CPPUNIT_ASSERT_EQUAL((status_t)0, result);
	}

	/* Verify final count */
	int32 expected = NUM_THREADS * ITERATIONS_PER_THREAD;
	int32 actual = atomic_load_ordered(&data.counter, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(expected, actual);

	printf("[Concurrency] %d threads × %d iterations = %d (verified)\n",
		(int)NUM_THREADS, (int)ITERATIONS_PER_THREAD, (int)expected);
}


void
AtomicOrderedTest::testFences()
{
	int32 data1 = 0;
	int32 data2 = 0;
	int32 flag = 0;

	/* Test acquire fence */
	atomic_store_ordered(&data1, 100, B_MEMORY_ORDER_RELAXED);
	atomic_store_ordered(&data2, 200, B_MEMORY_ORDER_RELAXED);
	atomic_thread_fence(B_MEMORY_ORDER_RELEASE);
	atomic_store_ordered(&flag, 1, B_MEMORY_ORDER_RELAXED);

	/* Consumer: acquire fence ensures data reads happen after flag read */
	while (atomic_load_ordered(&flag, B_MEMORY_ORDER_RELAXED) == 0) {
		snooze(1);
	}
	atomic_thread_fence(B_MEMORY_ORDER_ACQUIRE);
	
	int32 val1 = atomic_load_ordered(&data1, B_MEMORY_ORDER_RELAXED);
	int32 val2 = atomic_load_ordered(&data2, B_MEMORY_ORDER_RELAXED);
	
	CPPUNIT_ASSERT_EQUAL(100, val1);
	CPPUNIT_ASSERT_EQUAL(200, val2);

	/* Test seq_cst fence */
	atomic_store_ordered(&flag, 0, B_MEMORY_ORDER_SEQ_CST);
	atomic_thread_fence(B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(0, atomic_load_ordered(&flag, B_MEMORY_ORDER_SEQ_CST));

	/* Test signal fence (compiler barrier only) */
	atomic_signal_fence(B_MEMORY_ORDER_SEQ_CST);
}


void
AtomicOrderedTest::test64BitOperations()
{
	/* Test 64-bit atomic operations */
	int64 value = 0;

	/* Store and load */
	atomic_store64_ordered(&value, 0x123456789ABCDEFLL, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL((int64)0x123456789ABCDEFLL, 
		atomic_load64_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Exchange */
	int64 old = atomic_exchange64_ordered(&value, 0xFEDCBA987654321LL, 
		B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL((int64)0x123456789ABCDEFLL, old);
	CPPUNIT_ASSERT_EQUAL((int64)0xFEDCBA987654321LL, 
		atomic_load64_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Compare-exchange strong - success */
	int64 expected = 0xFEDCBA987654321LL;
	bool success = atomic_compare_exchange_strong64_ordered(&value, &expected, 
		999LL, B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT(success);
	CPPUNIT_ASSERT_EQUAL(999LL, atomic_load64_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Compare-exchange weak */
	atomic_store64_ordered(&value, 1000LL, B_MEMORY_ORDER_SEQ_CST);
	expected = 1000LL;
	success = false;
	int attempts = 0;
	
	while (!success && attempts < 100) {
		expected = 1000LL;
		success = atomic_compare_exchange_weak64_ordered(&value, &expected, 
			2000LL, B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
		attempts++;
	}
	
	CPPUNIT_ASSERT(success);
	CPPUNIT_ASSERT_EQUAL(2000LL, 
		atomic_load64_ordered(&value, B_MEMORY_ORDER_SEQ_CST));

	/* Verify backward compatibility with old 64-bit API */
	atomic_set64(&value, 0x123456789ABCDEFLL);
	CPPUNIT_ASSERT_EQUAL((int64)0x123456789ABCDEFLL, atomic_get64(&value));
	CPPUNIT_ASSERT_EQUAL((int64)0x123456789ABCDEFLL, 
		atomic_load64_ordered(&value, B_MEMORY_ORDER_SEQ_CST));
}


void
AtomicOrderedTest::testPointerOperations()
{
	/* Test pointer atomic operations */
	void* ptr = nullptr;
	void* value1 = (void*)0x1234;
	void* value2 = (void*)0x5678;

	/* Store and load */
	atomic_store_ptr_ordered(&ptr, value1, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(value1, atomic_load_ptr_ordered(&ptr, B_MEMORY_ORDER_SEQ_CST));

	/* Exchange */
	void* old = atomic_exchange_ptr_ordered(&ptr, value2, B_MEMORY_ORDER_SEQ_CST);
	CPPUNIT_ASSERT_EQUAL(value1, old);
	CPPUNIT_ASSERT_EQUAL(value2, atomic_load_ptr_ordered(&ptr, B_MEMORY_ORDER_SEQ_CST));

	/* Compare-exchange weak */
	void* expected = value2;
	bool success = false;
	int attempts = 0;
	
	while (!success && attempts < 100) {
		expected = value2;
		success = atomic_compare_exchange_weak_ptr_ordered(&ptr, &expected, 
			value1, B_MEMORY_ORDER_SEQ_CST, B_MEMORY_ORDER_SEQ_CST);
		attempts++;
	}
	
	CPPUNIT_ASSERT(success);
	CPPUNIT_ASSERT_EQUAL(value1, atomic_load_ptr_ordered(&ptr, B_MEMORY_ORDER_SEQ_CST));

	/* Test with acquire-release ordering */
	atomic_store_ptr_ordered(&ptr, nullptr, B_MEMORY_ORDER_RELEASE);
	CPPUNIT_ASSERT_EQUAL((void*)nullptr, 
		atomic_load_ptr_ordered(&ptr, B_MEMORY_ORDER_ACQUIRE));
}


//	#pragma mark - Test Suite


CppUnit::Test*
AtomicOrderedTest::Suite()
{
	CppUnit::TestSuite* suite = new CppUnit::TestSuite("AtomicOrdered");

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testBasicOperations", 
		&AtomicOrderedTest::testBasicOperations));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testMemoryOrdering", 
		&AtomicOrderedTest::testMemoryOrdering));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testWeakCAS", 
		&AtomicOrderedTest::testWeakCAS));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testWeakCASPerformance", 
		&AtomicOrderedTest::testWeakCASPerformance));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testPerformance", 
		&AtomicOrderedTest::testPerformance));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testBackwardCompatibility", 
		&AtomicOrderedTest::testBackwardCompatibility));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testAlignment", 
		&AtomicOrderedTest::testAlignment));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testConcurrency", 
		&AtomicOrderedTest::testConcurrency));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testFences", 
		&AtomicOrderedTest::testFences));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::test64BitOperations", 
		&AtomicOrderedTest::test64BitOperations));

	suite->addTest(new CppUnit::TestCaller<AtomicOrderedTest>(
		"AtomicOrderedTest::testPointerOperations", 
		&AtomicOrderedTest::testPointerOperations));

	return suite;
}
