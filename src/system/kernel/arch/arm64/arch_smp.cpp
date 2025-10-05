/*
 * Copyright 2019-2025 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>

#include <boot/stage2.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <arch_psci.h>
#include <debug.h>
#include <interrupts.h>
#include <smp.h>


// PSCI function IDs (ARM PSCI specification)
#define PSCI_0_2_FN_BASE		0x84000000
#define PSCI_0_2_FN(n)			(PSCI_0_2_FN_BASE + (n))
#define PSCI_0_2_64BIT			0x40000000
#define PSCI_0_2_FN64(n)		(PSCI_0_2_FN(n) | PSCI_0_2_64BIT)

#define PSCI_0_2_FN64_CPU_ON		PSCI_0_2_FN64(3)
#define PSCI_0_2_FN_CPU_OFF		PSCI_0_2_FN(2)
#define PSCI_0_2_FN64_AFFINITY_INFO	PSCI_0_2_FN64(4)


// PSCI return codes
#define PSCI_SUCCESS		0
#define PSCI_NOT_SUPPORTED	-1
#define PSCI_INVALID_PARAMS	-2
#define PSCI_DENIED		-3
#define PSCI_ALREADY_ON		-4
#define PSCI_ON_PENDING		-5
#define PSCI_INTERNAL_FAILURE	-6
#define PSCI_NOT_PRESENT	-7
#define PSCI_DISABLED		-8
#define PSCI_INVALID_ADDRESS	-9


static bool sPSCIAvailable = false;
static uint32 sPSCIMethod = 1; // Default to SMC


// Make a PSCI call using SMC or HVC instruction
static inline int64
psci_call(uint32 function, uint64 arg0, uint64 arg1, uint64 arg2)
{
	register uint64 x0 asm("x0") = function;
	register uint64 x1 asm("x1") = arg0;
	register uint64 x2 asm("x2") = arg1;
	register uint64 x3 asm("x3") = arg2;

	// Use SMC or HVC based on FDT configuration
	if (sPSCIMethod == 2) {
		// HVC (Hypervisor Call)
		asm volatile(
			"hvc #0"
			: "+r"(x0)
			: "r"(x1), "r"(x2), "r"(x3)
			: "memory");
	} else {
		// SMC (Secure Monitor Call) - default
		asm volatile(
			"smc #0"
			: "+r"(x0)
			: "r"(x1), "r"(x2), "r"(x3)
			: "memory");
	}

	return x0;
}


status_t
arch_smp_init(kernel_args *args)
{
	// Get PSCI configuration from FDT
	sPSCIMethod = args->arch_args.psci_method;
	sPSCIAvailable = (sPSCIMethod != 0);

	if (sPSCIAvailable) {
		dprintf("arch_smp_init: PSCI available, method=%s\n",
			sPSCIMethod == 2 ? "HVC" : "SMC");
	} else {
		dprintf("arch_smp_init: WARNING - PSCI not available, defaulting to SMC\n");
		sPSCIMethod = 1; // Default to SMC
		sPSCIAvailable = true;
	}

	dprintf("arch_smp_init: %d CPUs detected\n", args->num_cpus);

	// Copy MPIDR values from kernel_args to gCPU structures
	for (int32 i = 0; i < args->num_cpus && i < SMP_MAX_CPUS; i++) {
		gCPU[i].arch.mpidr = args->arch_args.cpu_mpidr[i];
		dprintf("  CPU %d: MPIDR=0x%lx\n", i, gCPU[i].arch.mpidr);
	}

	if (args->num_cpus <= 1) {
		dprintf("arch_smp_init: single CPU system, skipping SMP init\n");
		return B_OK;
	}

	// Secondary CPUs will be started later in arch_smp_per_cpu_init
	return B_OK;
}


status_t
arch_smp_per_cpu_init(kernel_args *args, int32 cpu)
{
	if (cpu == 0) {
		// Primary CPU, already running
		return B_OK;
	}

	if (!sPSCIAvailable) {
		dprintf("arch_smp_per_cpu_init: PSCI not available\n");
		return B_ERROR;
	}

	// Get MPIDR for target CPU
	// In real implementation, this would come from FDT
	uint64 mpidr = gCPU[cpu].arch.mpidr;

	// Get entry point for secondary CPU
	extern void _start_secondary_cpu();
	uint64 entry_point = (uint64)&_start_secondary_cpu;

	// Context ID to pass to secondary CPU (pointer to cpu_ent)
	uint64 context_id = (uint64)&gCPU[cpu];

	dprintf("arch_smp_per_cpu_init: Starting CPU %d (MPIDR 0x%lx) at 0x%lx\n",
		cpu, mpidr, entry_point);

	// Start the CPU using PSCI CPU_ON
	int64 result = psci_call(PSCI_0_2_FN64_CPU_ON, mpidr, entry_point, context_id);

	if (result != PSCI_SUCCESS) {
		dprintf("arch_smp_per_cpu_init: PSCI CPU_ON failed for CPU %d: %lld\n",
			cpu, result);
		return B_ERROR;
	}

	dprintf("arch_smp_per_cpu_init: Successfully started CPU %d\n", cpu);
	return B_OK;
}


void
arch_smp_send_ici(int32 target_cpu)
{
	if (target_cpu < 0 || target_cpu >= (int32)smp_get_num_cpus())
		return;

	// Get target CPU's MPIDR
	uint64 mpidr = gCPU[target_cpu].arch.mpidr;

	// Extract affinity levels from MPIDR
	uint64 aff0 = (mpidr >> 0) & 0xFF;
	uint64 aff1 = (mpidr >> 8) & 0xFF;
	uint64 aff2 = (mpidr >> 16) & 0xFF;
	uint64 aff3 = (mpidr >> 32) & 0xFF;

	// Build SGI1R value to send IPI
	// Format: [55:48]=Aff3, [39:32]=Aff2, [23:16]=Aff1, [15:0]=target list
	uint64 sgi1r = (aff3 << 48) | (aff2 << 32) | (aff1 << 16) | (1UL << aff0);

	// Send SGI (Software Generated Interrupt) to target CPU
	// IPI ID 0 (can be configured)
	sgi1r |= (0UL << 24);  // INTID = 0

	// Write to ICC_SGI1R_EL1 system register
	asm volatile("msr ICC_SGI1R_EL1, %0" :: "r"(sgi1r) : "memory");
	asm volatile("isb" ::: "memory");
}


void
arch_smp_send_multicast_ici(CPUSet& cpuSet)
{
	// For each CPU in the set, send IPI
	int32 cpu = cpuSet.GetFirstSet();
	while (cpu >= 0) {
		arch_smp_send_ici(cpu);
		cpu = cpuSet.GetNextSet(cpu);
	}
}


void
arch_smp_send_broadcast_ici()
{
	// Send IPI to all CPUs except current
	int32 currentCPU = smp_get_current_cpu();
	int32 numCPUs = smp_get_num_cpus();

	for (int32 cpu = 0; cpu < numCPUs; cpu++) {
		if (cpu != currentCPU)
			arch_smp_send_ici(cpu);
	}
}
