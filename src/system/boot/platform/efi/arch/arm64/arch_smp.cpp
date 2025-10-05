/*
 * Copyright 2021-2025, Haiku, Inc. All rights reserved.
 * Released under the terms of the MIT License.
*/


#include "arch_smp.h"

#include <string.h>

#include <KernelExport.h>

#include <kernel.h>
#include <safemode.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/menu.h>


//#define TRACE_SMP
#ifdef TRACE_SMP
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static uint32 sCPUCount = 0;


void
arch_smp_register_cpu(platform_cpu_info** cpu)
{
	TRACE(("arch_smp_register_cpu: CPU #%lu\n", sCPUCount));

	if (sCPUCount >= SMP_MAX_CPUS) {
		dprintf("arch_smp_register_cpu: WARNING: Too many CPUs (max %d)\n",
			SMP_MAX_CPUS);
		*cpu = NULL;
		return;
	}

	// Allocate CPU info structure
	static platform_cpu_info sCPUInfos[SMP_MAX_CPUS];
	*cpu = &sCPUInfos[sCPUCount];

	// Initialize basic CPU info
	memset(*cpu, 0, sizeof(platform_cpu_info));

	sCPUCount++;

	TRACE(("arch_smp_register_cpu: registered CPU #%lu\n", sCPUCount - 1));
}


int
arch_smp_get_current_cpu(void)
{
	// TODO: Read MPIDR_EL1 and match against registered CPUs
	// For now, assume boot CPU is always CPU 0
	return 0;
}


void
arch_smp_init_other_cpus(void)
{
	// Set actual CPU count from FDT parsing
	gKernelArgs.num_cpus = sCPUCount;

	if (sCPUCount == 0) {
		// No CPUs found via FDT, assume single CPU
		dprintf("WARNING: No CPUs found via FDT, assuming single CPU\n");
		gKernelArgs.num_cpus = 1;

		// Set MPIDR for boot CPU (read from hardware)
		uint64 mpidr;
		asm("mrs %0, mpidr_el1" : "=r"(mpidr));
		gKernelArgs.arch_args.cpu_mpidr[0] = mpidr;
	}

	dprintf("arch_smp_init_other_cpus: found %lu CPU(s)\n", gKernelArgs.num_cpus);
}


void
arch_smp_boot_other_cpus(uint32 pml4, uint64 kernelEntry, addr_t virtKernelArgs)
{
	// Secondary CPUs will be started by kernel via PSCI
	// Bootloader just passes MPIDR values to kernel
	TRACE(("arch_smp_boot_other_cpus: %lu CPUs registered\n", sCPUCount));
}


void
arch_smp_add_safemode_menus(Menu *menu)
{
	MenuItem *item;

	if (gKernelArgs.num_cpus < 2)
		return;

	item = new(nothrow) MenuItem("Disable SMP");
	menu->AddItem(item);
	item->SetData(B_SAFEMODE_DISABLE_SMP);
	item->SetType(MENU_ITEM_MARKABLE);
	item->SetHelpText("Disables all but one CPU core.");
}


void
arch_smp_init(void)
{
	// Nothing to do here, CPUs are registered via FDT parsing
	TRACE(("arch_smp_init\n"));
}
