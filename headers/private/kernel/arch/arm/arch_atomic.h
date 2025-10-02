/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck IV <kallisti5@unixzen.com>
 */
#ifndef _KERNEL_ARCH_ARM_ATOMIC_H
#define _KERNEL_ARCH_ARM_ATOMIC_H


#include <arch_cpu.h>


/* ARMv7+ supports LDREX/STREX - native atomic operations */
#if __ARM_ARCH__ >= 7

static inline void
memory_relaxed_barrier_inline(void)
{
	/* Compiler barrier only - no CPU memory ordering */
	__asm__ __volatile__("" : : : "memory");
}


static inline void
memory_acquire_barrier_inline(void)
{
	/* ARMv7 load-acquire barrier: prevents reordering of
	 * this load with subsequent loads and stores.
	 * DMB provides full memory barrier on ARMv7. */
	dmb();
}


static inline void
memory_release_barrier_inline(void)
{
	/* ARMv7 store-release barrier: prevents reordering of
	 * prior loads and stores with this store.
	 * DMB provides full memory barrier on ARMv7. */
	dmb();
}


static inline void
memory_acq_rel_barrier_inline(void)
{
	/* Full acquire-release barrier */
	dmb();
}


static inline void
memory_seq_cst_barrier_inline(void)
{
	/* Sequential consistency barrier */
	dmb();
}


/* Native ARMv7 atomic CAS using LDREX/STREX
 * 
 * LDREX/STREX instruction pair implements Load-Exclusive/Store-Exclusive:
 * - LDREX marks a memory location for exclusive access
 * - STREX attempts to store only if no other core has accessed the location
 * - The retry loop (label 1b) handles concurrent access failures
 * - DMB ensures proper memory ordering after successful CAS
 * 
 * Returns: true if the exchange was successful (old value == expected),
 *          false otherwise
 */
static inline bool
arch_atomic_cas32_armv7(volatile int32* ptr, int32 expected, int32 desired)
{
	int32 old_val, success;
	__asm__ __volatile__(
		"1: ldrex   %[old], [%[ptr]]        \n"
		"   cmp     %[old], %[expected]     \n"
		"   bne     2f                      \n"  /* Exit if old != expected */
		"   strex   %[success], %[desired], [%[ptr]] \n"
		"   cmp     %[success], #0          \n"
		"   bne     1b                      \n"  /* Retry if store failed */
		"   dmb                             \n"  /* Memory barrier on success */
		"2:                                 \n"
		: [old] "=&r" (old_val), [success] "=&r" (success), "+Qo" (*ptr)
		: [ptr] "r" (ptr), [expected] "r" (expected), [desired] "r" (desired)
		: "cc", "memory");
	
	return old_val == expected;
}


#define ARCH_HAS_NATIVE_CAS32 1


/* New barrier API with explicit memory ordering semantics */
#define memory_relaxed_barrier    memory_relaxed_barrier_inline
#define memory_acquire_barrier    memory_acquire_barrier_inline
#define memory_release_barrier    memory_release_barrier_inline
#define memory_acq_rel_barrier    memory_acq_rel_barrier_inline
#define memory_seq_cst_barrier    memory_seq_cst_barrier_inline

#endif /* __ARM_ARCH__ >= 7 */


/* Legacy barrier API - preserved for backward compatibility
 * These are always available regardless of ARM version */
static inline void
memory_read_barrier_inline(void)
{
	dmb();
}


static inline void
memory_write_barrier_inline(void)
{
	dmb();
}


static inline void
memory_full_barrier_inline(void)
{
	dmb();
}


#define memory_read_barrier memory_read_barrier_inline
#define memory_write_barrier memory_write_barrier_inline
#define memory_full_barrier memory_full_barrier_inline


/* Note about ARMv6 and earlier:
 * ARMv6 and earlier ARM architectures do not have native atomic CAS instructions.
 * For these platforms, atomic operations must fall back to kernel syscalls or
 * other synchronization mechanisms. The ARMv7+ LDREX/STREX instructions provide
 * hardware-assisted atomic operations that are significantly faster.
 *
 * ARMv7 LDREX/STREX advantages:
 * - No kernel syscall overhead
 * - Hardware retry mechanism
 * - Proper memory ordering with DMB
 * - Compatible with SMP systems
 */


#endif	// _KERNEL_ARCH_ARM_ATOMIC_H
