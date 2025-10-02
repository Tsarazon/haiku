/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_X86_64_ATOMIC_H
#define _KERNEL_ARCH_X86_64_ATOMIC_H


static inline void
memory_read_barrier_inline(void)
{
	asm volatile("lfence" : : : "memory");
}


static inline void
memory_write_barrier_inline(void)
{
	asm volatile("sfence" : : : "memory");
}


static inline void
memory_full_barrier_inline(void)
{
	asm volatile("mfence" : : : "memory");
}


static inline void
memory_relaxed_barrier_inline(void)
{
	/* No barrier needed on x86_64 for relaxed ordering.
	 * x86-64 has a Total Store Ordering (TSO) memory model which provides
	 * strong ordering guarantees. Only a compiler barrier is needed to
	 * prevent compiler reordering of memory operations. */
	asm volatile("" : : : "memory");
}


static inline void
memory_acquire_barrier_inline(void)
{
	/* x86-64 TSO memory model provides acquire semantics automatically.
	 * The TSO model guarantees that:
	 * - Loads are not reordered with older loads
	 * - Loads are not reordered with older stores
	 * Therefore, only a compiler barrier is needed to prevent the compiler
	 * from reordering memory operations. No CPU fence instruction required. */
	asm volatile("" : : : "memory");
}


static inline void
memory_release_barrier_inline(void)
{
	/* x86-64 TSO memory model provides release semantics automatically.
	 * The TSO model guarantees that:
	 * - Stores are not reordered with older loads
	 * - Stores are not reordered with older stores
	 * Therefore, only a compiler barrier is needed to prevent the compiler
	 * from reordering memory operations. No CPU fence instruction required. */
	asm volatile("" : : : "memory");
}


static inline void
memory_acq_rel_barrier_inline(void)
{
	/* Combined acquire-release semantic.
	 * On x86-64 TSO, this is equivalent to acquire or release barriers
	 * as both only require compiler barriers. The only reordering that
	 * x86-64 allows is store-load reordering, which is prevented by
	 * the store and load ordering guarantees mentioned above. */
	asm volatile("" : : : "memory");
}


#define memory_read_barrier		memory_read_barrier_inline
#define memory_write_barrier	memory_write_barrier_inline
#define memory_full_barrier		memory_full_barrier_inline
#define memory_relaxed_barrier	memory_relaxed_barrier_inline
#define memory_acquire_barrier	memory_acquire_barrier_inline
#define memory_release_barrier	memory_release_barrier_inline
#define memory_acq_rel_barrier	memory_acq_rel_barrier_inline

#endif	// _KERNEL_ARCH_X86_64_ATOMIC_H

