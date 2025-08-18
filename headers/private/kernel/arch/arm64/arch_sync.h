/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Synchronization Primitives Header
 * 
 * This header provides ARM64-specific synchronization primitives including
 * memory barriers, atomic operations, and cache coherency management for
 * proper SMP operation.
 */

#ifndef _KERNEL_ARCH_ARM64_ARCH_SYNC_H
#define _KERNEL_ARCH_ARM64_ARCH_SYNC_H

#include <SupportDefs.h>

// Memory barrier domain types
#define ARM64_BARRIER_SY		0xF		// Full system barrier
#define ARM64_BARRIER_ST		0xE		// Store barrier
#define ARM64_BARRIER_LD		0xD		// Load barrier
#define ARM64_BARRIER_ISH		0xB		// Inner shareable
#define ARM64_BARRIER_ISHST		0xA		// Inner shareable store
#define ARM64_BARRIER_ISHLD		0x9		// Inner shareable load
#define ARM64_BARRIER_NSH		0x7		// Non-shareable
#define ARM64_BARRIER_NSHST		0x6		// Non-shareable store
#define ARM64_BARRIER_NSHLD		0x5		// Non-shareable load
#define ARM64_BARRIER_OSH		0x3		// Outer shareable
#define ARM64_BARRIER_OSHST		0x2		// Outer shareable store
#define ARM64_BARRIER_OSHLD		0x1		// Outer shareable load

// Cache operation types
#define ARM64_CACHE_OP_CLEAN		0		// Clean cache lines
#define ARM64_CACHE_OP_INVALIDATE	1		// Invalidate cache lines
#define ARM64_CACHE_OP_CLEAN_INVAL	2		// Clean and invalidate
#define ARM64_CACHE_OP_ZERO			3		// Zero cache lines

// ARM64 spinlock structure
typedef struct {
	volatile uint32 locked;
	volatile uint32 owner_cpu;
	volatile uint64 lock_time;
} arm64_spinlock_t;

// ARM64 reader-writer lock structure
typedef struct {
	volatile int32 readers;			// Number of active readers (negative = writer)
	volatile uint32 writer_cpu;		// CPU holding write lock
	volatile uint32 waiting_writers;	// Number of waiting writers
} arm64_rwlock_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Memory Barrier Functions
 */

// High-level memory barriers
void arch_memory_barrier_full(void);
void arch_memory_barrier_read(void);
void arch_memory_barrier_write(void);
void arch_memory_barrier_smp(void);
void arch_store_buffer_flush(void);
void arch_instruction_barrier(void);

/*
 * Atomic Operations
 */

// Compare and swap operations
int32 arch_atomic_cas32(volatile int32* ptr, int32 expected, int32 desired);
int64 arch_atomic_cas64(volatile int64* ptr, int64 expected, int64 desired);
void* arch_atomic_cas_ptr(volatile void** ptr, void* expected, void* desired);

// Exchange operations
int32 arch_atomic_exchange32(volatile int32* ptr, int32 value);
int64 arch_atomic_exchange64(volatile int64* ptr, int64 value);

// Add operations
int32 arch_atomic_add32(volatile int32* ptr, int32 value);
int64 arch_atomic_add64(volatile int64* ptr, int64 value);
int32 arch_atomic_fetch_add32(volatile int32* ptr, int32 value);

// Bit operations
int32 arch_atomic_test_and_set_bit(volatile uint32* ptr, int bit);
int32 arch_atomic_test_and_clear_bit(volatile uint32* ptr, int bit);

/*
 * Cache Coherency Management
 */

// Cache operations on address ranges
void arch_cache_operation_range(addr_t start, size_t length, uint32 operation);
void arch_cache_clean_range(addr_t start, size_t length);
void arch_cache_invalidate_range(addr_t start, size_t length);
void arch_cache_flush_range(addr_t start, size_t length);
void arch_cache_zero_range(addr_t start, size_t length);

// Full cache operations
void arch_cache_clean_all(void);
void arch_cache_invalidate_all(void);
void arch_cache_flush_all(void);

/*
 * TLB Management
 */

// TLB invalidation operations
void arch_tlb_invalidate_all(void);
void arch_tlb_invalidate_asid(uint32 asid);
void arch_tlb_invalidate_page(addr_t virtual_addr, uint32 asid);
void arch_tlb_invalidate_range(addr_t start, size_t length, uint32 asid);

/*
 * CPU Control and Wait Instructions
 */

// CPU pause and event instructions
void arch_cpu_pause(void);
void arch_cpu_wait_for_event(void);
void arch_cpu_send_event(void);
void arch_cpu_send_event_local(void);

/*
 * Spinlock Operations
 */

// Spinlock management
void arch_spinlock_init(arm64_spinlock_t* lock);
void arch_spinlock_lock(arm64_spinlock_t* lock);
bool arch_spinlock_trylock(arm64_spinlock_t* lock);
void arch_spinlock_unlock(arm64_spinlock_t* lock);

/*
 * Reader-Writer Lock Operations
 */

// Reader-writer lock management
void arch_rwlock_init(arm64_rwlock_t* lock);
void arch_rwlock_read_lock(arm64_rwlock_t* lock);
void arch_rwlock_read_unlock(arm64_rwlock_t* lock);
void arch_rwlock_write_lock(arm64_rwlock_t* lock);
void arch_rwlock_write_unlock(arm64_rwlock_t* lock);

/*
 * Performance Monitoring
 */

// Performance counters and timing
uint64 arch_get_cycle_count(void);
uint64 arch_get_cpu_frequency(void);
void arch_pmu_enable_cycle_counter(void);
void arch_pmu_disable_cycle_counter(void);
uint64 arch_pmu_read_cycle_counter(void);

#ifdef __cplusplus
}
#endif

/*
 * Inline Helper Macros
 */

// Memory barrier shortcuts
#define memory_barrier()		arch_memory_barrier_full()
#define read_barrier()			arch_memory_barrier_read()
#define write_barrier()			arch_memory_barrier_write()
#define smp_barrier()			arch_memory_barrier_smp()

// Atomic operation shortcuts
#define atomic_cas32(ptr, old, new)		arch_atomic_cas32(ptr, old, new)
#define atomic_cas64(ptr, old, new)		arch_atomic_cas64(ptr, old, new)
#define atomic_cas_ptr(ptr, old, new)	arch_atomic_cas_ptr(ptr, old, new)
#define atomic_add32(ptr, val)			arch_atomic_add32(ptr, val)
#define atomic_add64(ptr, val)			arch_atomic_add64(ptr, val)

// Cache operation shortcuts
#define cache_flush_range(start, len)	arch_cache_flush_range(start, len)
#define cache_clean_range(start, len)	arch_cache_clean_range(start, len)
#define cache_inval_range(start, len)	arch_cache_invalidate_range(start, len)

// CPU control shortcuts
#define cpu_pause()				arch_cpu_pause()
#define cpu_wait_event()		arch_cpu_wait_for_event()
#define cpu_send_event()		arch_cpu_send_event()

#endif /* _KERNEL_ARCH_ARM64_ARCH_SYNC_H */