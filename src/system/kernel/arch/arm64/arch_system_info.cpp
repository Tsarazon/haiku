/*
 * Copyright 2019-2025 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include <OS.h>

#include <arch_cpu.h>
#include <arch/system_info.h>
#include <boot/kernel_args.h>
#include <cpu.h>
#include <smp.h>


status_t
arch_get_system_info(system_info *info, size_t size)
{
	// Fill in ARM64-specific CPU information
	info->cpu_type = B_CPU_ARM_64;
	info->cpu_count = smp_get_num_cpus();

	// Get CPU frequency from Generic Timer
	uint64 freq;
	asm("mrs %0, cntfrq_el0" : "=r"(freq));
	info->cpu_clock_speed = freq;

	// Read CPU ID registers for more info
	uint64 midr;
	asm("mrs %0, midr_el1" : "=r"(midr));

	// Extract implementer and variant
	info->cpu_revision = (midr >> 20) & 0xF;  // Variant

	return B_OK;
}


void
arch_fill_topology_node(cpu_topology_node_info* node, int32 cpu)
{
	// Read CPU identification registers
	uint64 midr;
	asm("mrs %0, midr_el1" : "=r"(midr));

	uint64 mpidr;
	asm("mrs %0, mpidr_el1" : "=r"(mpidr));

	uint64 ctr;
	asm("mrs %0, ctr_el0" : "=r"(ctr));

	switch (node->type) {
	case B_TOPOLOGY_ROOT:
		node->data.root.platform = B_CPU_ARM_64;
		break;

	case B_TOPOLOGY_PACKAGE:
		// Extract implementer ID from MIDR
		node->data.package.vendor = (midr >> 24) & 0xFF;
		// Extract cache line size from CTR_EL0
		node->data.package.cache_line_size = 4 << (ctr & 0xF);
		break;

	case B_TOPOLOGY_CORE:
		// Extract part number (CPU model)
		node->data.core.model = (midr >> 4) & 0xFFF;
		// Use timer frequency as default frequency
		uint64 freq;
		asm("mrs %0, cntfrq_el0" : "=r"(freq));
		node->data.core.default_frequency = freq;
		break;

	case B_TOPOLOGY_SMT:
		// ARM64 typically doesn't have SMT, but fill in if needed
		break;
	}
}


status_t
arch_system_info_init(struct kernel_args *args)
{
	// Initialize CPU topology detection
	// This would parse MPIDR and build topology tree
	// For now, simple initialization
	return B_OK;
}


status_t
arch_get_frequency(uint64 *frequency, int32 cpu)
{
	// Return Generic Timer frequency
	asm("mrs %0, cntfrq_el0" : "=r"(*frequency));
	return B_OK;
}
