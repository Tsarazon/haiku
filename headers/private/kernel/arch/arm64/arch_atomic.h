/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_ARM64_ARCH_ATOMIC_H_
#define _KERNEL_ARCH_ARM64_ARCH_ATOMIC_H_


/* Memory barriers for different ordering semantics */

static inline void
memory_relaxed_barrier_inline(void)
{
	/* No barrier for relaxed ordering */
	__asm__ __volatile__("" : : : "memory");
}


static inline void
memory_acquire_barrier_inline(void)
{
	/* ARMv8 load-acquire barrier: prevents reordering of
	 * this load with subsequent loads and stores */
	__asm__ __volatile__("dmb ishld" : : : "memory");
}


static inline void
memory_release_barrier_inline(void)
{
	/* ARMv8 store-release barrier: prevents reordering of
	 * prior loads and stores with this store.
	 * Note: ishst is insufficient for full release semantics.
	 * Using full DMB ISH for proper release ordering. */
	__asm__ __volatile__("dmb ish" : : : "memory");
}


static inline void
memory_acq_rel_barrier_inline(void)
{
	/* Full acquire-release barrier */
	__asm__ __volatile__("dmb ish" : : : "memory");
}


static inline void
memory_seq_cst_barrier_inline(void)
{
	/* Sequential consistency barrier.
	 * Note: DMB ISH is sufficient for memory ordering between cores.
	 * DSB SY is only needed when device I/O ordering is required. */
	__asm__ __volatile__("dmb ish" : : : "memory");
}


/* Legacy barrier names - mapped to seq_cst for compatibility */
static inline void
memory_read_barrier_inline(void)
{
	__asm__ __volatile__("dmb ishld" : : : "memory");
}


static inline void
memory_write_barrier_inline(void)
{
	__asm__ __volatile__("dmb ish" : : : "memory");
}


static inline void
memory_full_barrier_inline(void)
{
	__asm__ __volatile__("dmb ish" : : : "memory");
}


/* New barrier API with explicit memory ordering semantics */
#define memory_relaxed_barrier    memory_relaxed_barrier_inline
#define memory_acquire_barrier    memory_acquire_barrier_inline
#define memory_release_barrier    memory_release_barrier_inline
#define memory_acq_rel_barrier    memory_acq_rel_barrier_inline
#define memory_seq_cst_barrier    memory_seq_cst_barrier_inline

/* Legacy barrier API - preserved for backward compatibility */
#define memory_read_barrier       memory_read_barrier_inline
#define memory_write_barrier      memory_write_barrier_inline
#define memory_full_barrier       memory_full_barrier_inline


/* Note about ARMv8 load-acquire/store-release instructions:
 * ARMv8 provides dedicated LDAR/STLR instructions that combine
 * memory access with acquire/release semantics in a single instruction.
 * These are more efficient than separate load/store + DMB barrier:
 *
 * Load-acquire:  LDAR Xt, [Xn]      (vs LDR + DMB ISHLD)
 * Store-release: STLR Xt, [Xn]      (vs DMB ISH + STR)
 *
 * The atomic operations in this file can be enhanced to use LDAR/STLR
 * when compiler intrinsics are available, providing better performance
 * on ARMv8 platforms while maintaining correctness on all ARM64 CPUs.
 */


#endif /* _KERNEL_ARCH_ARM64_ARCH_ATOMIC_H_ */
