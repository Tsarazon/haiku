/*
 * Copyright 2003, Marcus Overhagen. All rights reserved.
 * Copyright 2024, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <SupportDefs.h>
#include <atomic_ordered.h>

#include <syscalls.h>


/* Runtime ARM architecture capability detection */
static bool sAtomicCapsInitialized = false;
static uint32 sAtomicCapabilities = 0;

#define ATOMIC_CAP_CAS32        (1 << 0)
#define ATOMIC_CAP_CAS64        (1 << 1)
#define ATOMIC_CAP_FETCH_ADD    (1 << 2)
#define ATOMIC_CAP_WEAK_CAS     (1 << 3)


static void
detect_atomic_capabilities(void)
{
	if (sAtomicCapsInitialized)
		return;
		
#ifdef __ARM_ARCH__
	/* Detect ARMv7+ LDREX/STREX support */
	if (__ARM_ARCH__ >= 7) {
		sAtomicCapabilities |= ATOMIC_CAP_CAS32;
		sAtomicCapabilities |= ATOMIC_CAP_FETCH_ADD;
		sAtomicCapabilities |= ATOMIC_CAP_WEAK_CAS;
	}
	if (__ARM_ARCH__ >= 8) {
		sAtomicCapabilities |= ATOMIC_CAP_CAS64;
	}
#elif defined(__x86_64__) || defined(__i386__)
	/* x86 always supports atomic operations natively */
	sAtomicCapabilities = ATOMIC_CAP_CAS32 | ATOMIC_CAP_CAS64 
		                 | ATOMIC_CAP_FETCH_ADD | ATOMIC_CAP_WEAK_CAS;
#endif
	
	sAtomicCapsInitialized = true;
}


#ifdef ATOMIC_FUNCS_ARE_SYSCALLS

void
atomic_set(int32 *value, int32 newValue)
{
	_kern_atomic_set(value, newValue);
}


int32
atomic_get_and_set(int32 *value, int32 newValue)
{
	return _kern_atomic_get_and_set(value, newValue);
}


int32
atomic_test_and_set(int32 *value, int32 newValue, int32 testAgainst)
{
	return _kern_atomic_test_and_set(value, newValue, testAgainst);
}


int32
atomic_add(int32 *value, int32 addValue)
{
	return _kern_atomic_add(value, addValue);
}


int32
atomic_and(int32 *value, int32 andValue)
{
	return _kern_atomic_and(value, andValue);
}


int32
atomic_or(int32 *value, int32 orValue)
{
	return _kern_atomic_or(value, orValue);
}


int32
atomic_get(int32 *value)
{
	return _kern_atomic_get(value);
}


#endif	/* ATOMIC_FUNCS_ARE_SYSCALLS */

#ifdef ATOMIC64_FUNCS_ARE_SYSCALLS

void
atomic_set64(int64 *value, int64 newValue)
{
	_kern_atomic_set64(value, newValue);
}


int64
atomic_test_and_set64(int64 *value, int64 newValue, int64 testAgainst)
{
	return _kern_atomic_test_and_set64(value, newValue, testAgainst);
}


int64
atomic_add64(int64 *value, int64 addValue)
{
	return _kern_atomic_add64(value, addValue);
}


int64
atomic_and64(int64 *value, int64 andValue)
{
	return _kern_atomic_and64(value, andValue);
}


int64
atomic_or64(int64 *value, int64 orValue)
{
	return _kern_atomic_or64(value, orValue);
}


int64
atomic_get64(int64 *value)
{
	return _kern_atomic_get64(value);
}


#endif	/* ATOMIC64_FUNCS_ARE_SYSCALLS */

#if defined(__arm__) && !defined(__clang__)

/* GCC compatibility: libstdc++ needs this one.
 * TODO: Update libstdc++ and drop this.
 * cf. http://fedoraproject.org/wiki/Architectures/ARM/GCCBuiltInAtomicOperations
 */
extern int32_t __sync_fetch_and_add_4(int32_t *value, int32_t addValue);

extern int32_t __sync_fetch_and_add_4(int32_t *value, int32_t addValue)
{
	return atomic_add((int32 *)value, addValue);
}

#endif


/* Runtime fallback implementations for _ordered functions
 * These provide syscall-based fallbacks for ARMv6 and older architectures
 * that lack native atomic instruction support (LDREX/STREX). */

#if !(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7) || defined(__clang__))

/* Fallback implementations when compiler doesn't support __atomic builtins */

bool
atomic_compare_exchange_weak_ordered(int32* value, int32* expected, 
	int32 desired, memory_order_t success, memory_order_t failure)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_WEAK_CAS) {
		/* Use native implementation with normalized failure ordering */
		return __atomic_compare_exchange_n(value, expected, desired, true,
		                                 __haiku_to_gcc_order(success),
		                                 __haiku_normalize_failure_order(success,
		                                 	failure));
	} else {
		/* Fallback through syscall for ARMv6 and older */
		int32 old = _kern_atomic_test_and_set(value, desired, *expected);
		if (old == *expected) {
			return true;
		} else {
			*expected = old;
			return false;
		}
	}
}


bool
atomic_compare_exchange_strong_ordered(int32* value, int32* expected,
	int32 desired, memory_order_t success, memory_order_t failure)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		return __atomic_compare_exchange_n(value, expected, desired, false,
		                                 __haiku_to_gcc_order(success),
		                                 __haiku_normalize_failure_order(success,
		                                 	failure));
	} else {
		/* Fallback: strong CAS requires retry loop until success or value
		 * changes. This ensures the atomicity guarantee of strong CAS. */
		int32 current = *expected;
		while (true) {
			int32 old = _kern_atomic_test_and_set(value, desired, current);
			if (old == current)
				return true;
			/* Value changed - update expected and return false */
			*expected = old;
			return false;
		}
	}
}


int32
atomic_fetch_add_ordered(int32* value, int32 addend, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_FETCH_ADD) {
		return __atomic_fetch_add(value, addend, __haiku_to_gcc_order(order));
	} else {
		/* Fallback to syscall for ARMv6 */
		return _kern_atomic_add(value, addend);
	}
}


int32
atomic_fetch_sub_ordered(int32* value, int32 subtrahend, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_FETCH_ADD) {
		return __atomic_fetch_sub(value, subtrahend, 
			__haiku_to_gcc_order(order));
	} else {
		/* Fallback to syscall - negate subtrahend for atomic_add */
		return _kern_atomic_add(value, -subtrahend);
	}
}


int32
atomic_fetch_and_ordered(int32* value, int32 operand, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_FETCH_ADD) {
		/* ARMv7+ has native bitwise operations */
		return __atomic_fetch_and(value, operand, __haiku_to_gcc_order(order));
	} else {
		/* Fallback to syscall */
		return _kern_atomic_and(value, operand);
	}
}


int32
atomic_fetch_or_ordered(int32* value, int32 operand, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_FETCH_ADD) {
		return __atomic_fetch_or(value, operand, __haiku_to_gcc_order(order));
	} else {
		return _kern_atomic_or(value, operand);
	}
}


int32
atomic_fetch_xor_ordered(int32* value, int32 operand, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_FETCH_ADD) {
		return __atomic_fetch_xor(value, operand, __haiku_to_gcc_order(order));
	} else {
		/* No syscall for XOR - implement via CAS loop */
		int32 expected, desired;
		do {
			expected = _kern_atomic_get(value);
			desired = expected ^ operand;
		} while (_kern_atomic_test_and_set(value, desired, expected) 
			!= expected);
		return expected;
	}
}


int32
atomic_exchange_ordered(int32* value, int32 newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		return __atomic_exchange_n(value, newValue, 
			__haiku_to_gcc_order(order));
	} else {
		/* Use get_and_set syscall */
		return _kern_atomic_get_and_set(value, newValue);
	}
}


void
atomic_store_ordered(int32* value, int32 newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		__atomic_store_n(value, newValue, __haiku_to_gcc_order(order));
	} else {
		/* Fallback via syscall */
		_kern_atomic_set(value, newValue);
	}
}


int32
atomic_load_ordered(int32* value, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		return __atomic_load_n(value, __haiku_to_gcc_order(order));
	} else {
		return _kern_atomic_get(value);
	}
}


/* 64-bit variants with runtime fallback */

bool
atomic_compare_exchange_weak64_ordered(int64* value, int64* expected,
	int64 desired, memory_order_t success, memory_order_t failure)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS64) {
		return __atomic_compare_exchange_n(value, expected, desired, true,
		                                 __haiku_to_gcc_order(success),
		                                 __haiku_normalize_failure_order(success,
		                                 	failure));
	} else {
		/* Fallback through syscall */
		int64 old = _kern_atomic_test_and_set64(value, desired, *expected);
		if (old == *expected) {
			return true;
		} else {
			*expected = old;
			return false;
		}
	}
}


bool
atomic_compare_exchange_strong64_ordered(int64* value, int64* expected,
	int64 desired, memory_order_t success, memory_order_t failure)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS64) {
		return __atomic_compare_exchange_n(value, expected, desired, false,
		                                 __haiku_to_gcc_order(success),
		                                 __haiku_normalize_failure_order(success,
		                                 	failure));
	} else {
		/* Strong CAS with retry for spurious failures */
		int64 current = *expected;
		while (true) {
			int64 old = _kern_atomic_test_and_set64(value, desired, current);
			if (old == current)
				return true;
			*expected = old;
			return false;
		}
	}
}


int64
atomic_exchange64_ordered(int64* value, int64 newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS64) {
		return __atomic_exchange_n(value, newValue, 
			__haiku_to_gcc_order(order));
	} else {
		/* Implement via CAS loop using syscall */
		int64 expected;
		do {
			expected = _kern_atomic_get64(value);
		} while (_kern_atomic_test_and_set64(value, newValue, expected) 
			!= expected);
		return expected;
	}
}


void
atomic_store64_ordered(int64* value, int64 newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS64) {
		__atomic_store_n(value, newValue, __haiku_to_gcc_order(order));
	} else {
		_kern_atomic_set64(value, newValue);
	}
}


int64
atomic_load64_ordered(int64* value, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS64) {
		return __atomic_load_n(value, __haiku_to_gcc_order(order));
	} else {
		return _kern_atomic_get64(value);
	}
}


/* Pointer variants */

void*
atomic_load_ptr_ordered(void** value, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		return __atomic_load_n(value, __haiku_to_gcc_order(order));
	} else {
		/* Fallback - treat as int32/int64 depending on pointer size */
#ifdef __LP64__
		return (void*)_kern_atomic_get64((int64*)value);
#else
		return (void*)_kern_atomic_get((int32*)value);
#endif
	}
}


void
atomic_store_ptr_ordered(void** value, void* newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		__atomic_store_n(value, newValue, __haiku_to_gcc_order(order));
	} else {
#ifdef __LP64__
		_kern_atomic_set64((int64*)value, (int64)newValue);
#else
		_kern_atomic_set((int32*)value, (int32)newValue);
#endif
	}
}


void*
atomic_exchange_ptr_ordered(void** value, void* newValue, memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		return __atomic_exchange_n(value, newValue, 
			__haiku_to_gcc_order(order));
	} else {
#ifdef __LP64__
		return (void*)_kern_atomic_get_and_set64((int64*)value, 
			(int64)newValue);
#else
		return (void*)_kern_atomic_get_and_set((int32*)value, 
			(int32)newValue);
#endif
	}
}


bool
atomic_compare_exchange_weak_ptr_ordered(void** value, void** expected,
	void* desired, memory_order_t success, memory_order_t failure)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_WEAK_CAS) {
		return __atomic_compare_exchange_n(value, expected, desired, true,
		                                 __haiku_to_gcc_order(success),
		                                 __haiku_normalize_failure_order(success,
		                                 	failure));
	} else {
#ifdef __LP64__
		int64 old = _kern_atomic_test_and_set64((int64*)value, (int64)desired,
			(int64)*expected);
		if (old == (int64)*expected) {
			return true;
		} else {
			*expected = (void*)old;
			return false;
		}
#else
		int32 old = _kern_atomic_test_and_set((int32*)value, (int32)desired,
			(int32)*expected);
		if (old == (int32)*expected) {
			return true;
		} else {
			*expected = (void*)old;
			return false;
		}
#endif
	}
}


/* Fence operations */

void
atomic_thread_fence(memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		__atomic_thread_fence(__haiku_to_gcc_order(order));
	} else {
		/* On platforms without native atomics, use full memory barrier
		 * This is conservative but ensures correctness. */
		__asm__ __volatile__("" : : : "memory");
	}
}


void
atomic_signal_fence(memory_order_t order)
{
	detect_atomic_capabilities();
	
	if (sAtomicCapabilities & ATOMIC_CAP_CAS32) {
		__atomic_signal_fence(__haiku_to_gcc_order(order));
	} else {
		/* Signal fence is always a compiler barrier */
		__asm__ __volatile__("" : : : "memory");
	}
}

#endif  /* Compiler doesn't support __atomic builtins */
