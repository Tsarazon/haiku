/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Claude AI Assistant
 */


/*! NUMA-Aware Thread Scheduler Implementation */


#include <OS.h>

#include <cpu.h>
#include <kscheduler.h>
#include <smp.h>
#include <thread.h>
#include <util/AutoLock.h>

#include "scheduler_common.h"
#include "scheduler_cpu.h"
#include "scheduler_thread.h"


// NUMA topology data structures
static cpu_topology_info sCpuTopologyInfo[SMP_MAX_CPUS];
static int32 sNumaNodeCount = 0;
static bool sNumaInitialized = false;
static rw_lock sNumaTopologyLock = RW_LOCK_INITIALIZER("numa_topology");

// CPU load tracking (atomic access via vint32)
static vint32 sCpuLoad[SMP_MAX_CPUS];

// Extended scheduler thread data with NUMA information
struct scheduler_thread_data_numa {
	// NUMA-specific extensions
	int32		preferred_cpu;
	int32		numa_node;
	bigtime_t	last_migration_time;
	vint32		cache_hot_level;		// Atomic counter 0-100
	
	// Migration statistics
	struct {
		bigtime_t	total_wait_time;
		bigtime_t	avg_run_quantum;
		vint32		migration_count;
		uint32		cpu_affinity_score;	// 0-100
	} stats;
};


// Helper functions
static inline scheduler_thread_data_numa*
get_numa_thread_data(Thread* thread)
{
	// Extension pattern - store NUMA data in thread's user_data or similar
	// For now, we'll track this separately until proper integration
	return NULL;  // TODO: Implement proper extension storage
}


static inline bool
scheduler_cpu_enabled(int32 cpu)
{
	return cpu >= 0 && cpu < smp_get_num_cpus();
}


// Parse ACPI SRAT table for NUMA topology
// Note: Simplified implementation - full ACPI parsing would be needed in production
static status_t
parse_srat_table(void* srat_table, cpu_topology_info* topology, int32* node_count)
{
	// TODO: Implement proper ACPI SRAT parsing
	// For now, return error to trigger UMA fallback
	return B_NOT_SUPPORTED;
}


// API Implementation

status_t
scheduler_init_numa_info(void)
{
	if (sNumaInitialized)
		return B_OK;

	WriteLocker locker(sNumaTopologyLock);
	
	// Double-checked locking
	if (sNumaInitialized)
		return B_OK;
	
	// Try to get ACPI SRAT table for NUMA information
	// Note: ACPI functions would be called here in production
	void* srat = NULL;
	status_t status = B_NOT_SUPPORTED; // acpi_get_table(ACPI_SIG_SRAT, 0, &srat);
	
	if (status != B_OK || srat == NULL) {
		// NUMA not supported - use UMA (single node) fallback
		sNumaNodeCount = 1;
		
		for (int32 cpu = 0; cpu < smp_get_num_cpus(); cpu++) {
			sCpuTopologyInfo[cpu].numa_node = 0;
			sCpuTopologyInfo[cpu].core_id = cpu;
			sCpuTopologyInfo[cpu].package_id = 0;
			sCpuTopologyInfo[cpu].cache_line_size = 64;
			sCpuTopologyInfo[cpu].relative_performance = 1.0f;
			
			atomic_set(&sCpuLoad[cpu], 0);
		}
		
		sNumaInitialized = true;
		return B_OK;
	}
	
	// Parse SRAT table
	status = parse_srat_table(srat, sCpuTopologyInfo, &sNumaNodeCount);
	// acpi_put_table(srat);  // Would be called in production
	
	if (status != B_OK) {
		// Fallback to UMA
		sNumaNodeCount = 1;
		for (int32 cpu = 0; cpu < smp_get_num_cpus(); cpu++) {
			sCpuTopologyInfo[cpu].numa_node = 0;
			sCpuTopologyInfo[cpu].core_id = cpu;
			sCpuTopologyInfo[cpu].package_id = 0;
			sCpuTopologyInfo[cpu].cache_line_size = 64;
			sCpuTopologyInfo[cpu].relative_performance = 1.0f;
			
			atomic_set(&sCpuLoad[cpu], 0);
		}
	} else {
		// Initialize load counters
		for (int32 cpu = 0; cpu < smp_get_num_cpus(); cpu++) {
			atomic_set(&sCpuLoad[cpu], 0);
		}
	}
	
	sNumaInitialized = true;
	return B_OK;
}


const cpu_topology_info*
scheduler_get_cpu_info(int32 cpu)
{
	if (cpu < 0 || cpu >= SMP_MAX_CPUS)
		return NULL;
		
	ReadLocker locker(sNumaTopologyLock);
	
	if (!sNumaInitialized)
		return NULL;
		
	return &sCpuTopologyInfo[cpu];
}


int32
scheduler_select_optimal_cpu(Thread* thread, int32 preferred_cpu)
{
	if (!sNumaInitialized)
		return preferred_cpu;
		
	ReadLocker locker(sNumaTopologyLock);
	
	if (preferred_cpu < 0 || preferred_cpu >= smp_get_num_cpus())
		return 0;  // Fallback to CPU 0
	
	const cpu_topology_info* preferred_info = &sCpuTopologyInfo[preferred_cpu];
	int32 best_cpu = preferred_cpu;
	int32 best_load = 100;  // Percent load
	
	// Find least loaded CPU in the same NUMA node
	for (int32 cpu = 0; cpu < smp_get_num_cpus(); cpu++) {
		if (!scheduler_cpu_enabled(cpu))
			continue;
		
		const cpu_topology_info* cpu_info = &sCpuTopologyInfo[cpu];
		
		// Prefer CPUs in the same NUMA node
		if (cpu_info->numa_node == preferred_info->numa_node) {
			// Atomic read of CPU load (in percent)
			int32 load = atomic_get(&sCpuLoad[cpu]);
			
			if (load < best_load && load < 80) {  // 80% threshold
				best_cpu = cpu;
				best_load = load;
			}
		}
	}
	
	return best_cpu;
}


bool
scheduler_should_migrate(Thread* thread, int32 target_cpu)
{
	if (!thread || target_cpu < 0)
		return false;
		
	// Don't migrate if already on target CPU
	if (thread->cpu && thread->cpu->cpu_num == target_cpu)
		return false;
	
	// Anti-migration heuristic
	bigtime_t now = system_time();
	
	// Use ThreadData for scheduler-specific data
	Scheduler::ThreadData* threadData = thread->scheduler_data;
	if (!threadData)
		return false;
	
	// Check thread sleep time - don't migrate recently active threads
	bigtime_t sleep_time = now - threadData->WentSleep();
	
	// Minimum 10ms between migrations to avoid thrashing
	if (sleep_time < 10000)
		return false;
	
	// Don't migrate if cache is still "hot"
	// Threads that were active recently have hot cache
	bigtime_t active_time = now - threadData->WentSleepActive();
	
	// Cache considered "hot" for 50ms after last activity
	return active_time >= 50000;
}


// Update CPU load (called by scheduler)
void
scheduler_update_cpu_load(int32 cpu, int32 load_percent)
{
	if (cpu < 0 || cpu >= SMP_MAX_CPUS)
		return;
	
	// Clamp load to 0-100 range
	if (load_percent < 0)
		load_percent = 0;
	if (load_percent > 100)
		load_percent = 100;
	
	atomic_set(&sCpuLoad[cpu], load_percent);
}


// Extension point for thread creation
void
scheduler_numa_on_thread_create(Thread* thread)
{
	// TODO: Allocate and initialize scheduler_thread_data_numa
	// This would be integrated with the scheduler's thread data system
}


// Extension point for thread destruction
void
scheduler_numa_on_thread_destroy(Thread* thread)
{
	// TODO: Free scheduler_thread_data_numa
}
