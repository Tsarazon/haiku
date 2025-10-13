/*
 * Copyright 2023, Puck Meerburg, puck@puckipedia.com.
 * Copyright 2013, Paweł Dziepak, pdziepak@quarnos.org.
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <boot/kernel_args.h>
#include <vm/vm.h>
#include <cpu.h>
#include <interrupts.h>
#include <smp.h>
#include <smp_priv.h>

#include <arch/atomic.h>
#include <arch/cpu.h>
#include <arch/vm.h>
#include <arch/smp.h>

#include <arch/x86/apic.h>
#include <arch/x86/arch_smp.h>
#include <arch/x86/smp_priv.h>
#include <arch/x86/timer.h>

#include <string.h>
#include <stdio.h>

#include <algorithm>


//#define TRACE_ARCH_SMP
#ifdef TRACE_ARCH_SMP
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static constexpr uint32 ICI_VECTOR = 0xfd;
static constexpr bigtime_t IPI_DELIVERY_TIMEOUT_US = 100000;

static uint32 sCPUAPICIds[SMP_MAX_CPUS];
static uint32 sAPICVersions[SMP_MAX_CPUS];


static inline bool
wait_for_apic_interrupt_delivered(bigtime_t timeout_us)
{
	bigtime_t start = system_time();
	while (!apic_interrupt_delivered()) {
		if (system_time() - start > timeout_us)
			return false;
		cpu_pause();
	}
	return true;
}


static int32
x86_ici_interrupt(void *data)
{
	int cpu = smp_get_current_cpu();
	TRACE(("inter-cpu interrupt on cpu %d\n", cpu));
	return smp_intercpu_interrupt_handler(cpu);
}


static int32
x86_spurious_interrupt(void *data)
{
	TRACE(("spurious interrupt on cpu %" B_PRId32 "\n", smp_get_current_cpu()));

	// Spurious interrupts don't require EOI
	return B_HANDLED_INTERRUPT;
}


static int32
x86_smp_error_interrupt(void *data)
{
	TRACE(("smp error interrupt on cpu %" B_PRId32 "\n", smp_get_current_cpu()));
	return B_HANDLED_INTERRUPT;
}


uint32
x86_get_cpu_apic_id(int32 cpu)
{
	ASSERT(cpu >= 0 && cpu < SMP_MAX_CPUS);
	return sCPUAPICIds[cpu];
}


status_t
arch_smp_init(kernel_args *args)
{
	TRACE(("%s: entry\n", __func__));

	if (!apic_available()) {
		TRACE(("%s: apic not available for smp\n", __func__));
		return B_OK;
	}

	memcpy(sCPUAPICIds, args->arch_args.cpu_apic_id, sizeof(args->arch_args.cpu_apic_id));
	memcpy(sAPICVersions, args->arch_args.cpu_apic_version, sizeof(args->arch_args.cpu_apic_version));

	arch_smp_per_cpu_init(args, 0);

	if (args->num_cpus > 1) {
		// Interrupts shifted by ARCH_INTERRUPT_BASE
		reserve_io_interrupt_vectors(3, 0xfd - ARCH_INTERRUPT_BASE,
									 INTERRUPT_TYPE_ICI);
		install_io_interrupt_handler(0xfd - ARCH_INTERRUPT_BASE, &x86_ici_interrupt, NULL, B_NO_LOCK_VECTOR);
		install_io_interrupt_handler(0xfe - ARCH_INTERRUPT_BASE, &x86_smp_error_interrupt, NULL, B_NO_LOCK_VECTOR);
		install_io_interrupt_handler(0xff - ARCH_INTERRUPT_BASE, &x86_spurious_interrupt, NULL, B_NO_LOCK_VECTOR);
	}

	return B_OK;
}


status_t
arch_smp_per_cpu_init(kernel_args *args, int32 cpu)
{
	TRACE(("arch_smp_init_percpu: setting up the apic on cpu %" B_PRId32 "\n",
		   cpu));

	apic_per_cpu_init(args, cpu);
	x86_init_fpu();

	return B_OK;
}


static void
send_multicast_ici_physical(CPUSet& cpuSet)
{
	int32 cpuCount = smp_get_num_cpus();
	int32 currentCpu = smp_get_current_cpu();

	for (int32 i = 0; i < cpuCount; i++) {
		ASSERT(i < SMP_MAX_CPUS);

		if (cpuSet.GetBit(i) && i != currentCpu) {
			uint32 destination = sCPUAPICIds[i];
			uint32 mode = ICI_VECTOR | APIC_DELIVERY_MODE_FIXED
			| APIC_INTR_COMMAND_1_ASSERT
			| APIC_INTR_COMMAND_1_DEST_MODE_PHYSICAL
			| APIC_INTR_COMMAND_1_DEST_FIELD;

			if (!wait_for_apic_interrupt_delivered(IPI_DELIVERY_TIMEOUT_US)) {
				panic("IPI delivery timeout: cpu %" B_PRId32 " -> cpu %" B_PRId32
				" (apic %#" B_PRIx32 ")", currentCpu, i, destination);
			}
			apic_set_interrupt_command(destination, mode);
		}
	}
}


void
arch_smp_send_multicast_ici(CPUSet& cpuSet)
{
	#if KDEBUG
	if (are_interrupts_enabled())
		panic("arch_smp_send_multicast_ici: called with interrupts enabled");
	#endif

	memory_write_barrier();

	if (!x2apic_available()) {
		send_multicast_ici_physical(cpuSet);
		return;
	}

	// WRMSR on the x2APIC MSRs is neither serializing, nor a load-store
	// operation, requiring both memory serialization *and* a load fence, which
	// is the only way to ensure the MSR doesn't get executed before the write
	// barrier.
	memory_read_barrier();

	int32 cpuCount = smp_get_num_cpus();
	int32 currentCpu = smp_get_current_cpu();

	uint32 mode = ICI_VECTOR | APIC_DELIVERY_MODE_FIXED
	| APIC_INTR_COMMAND_1_ASSERT
	| APIC_INTR_COMMAND_1_DEST_MODE_LOGICAL
	| APIC_INTR_COMMAND_1_DEST_FIELD;

	for (int32 i = 0; i < cpuCount; i++) {
		ASSERT(i < SMP_MAX_CPUS);

		if (!cpuSet.GetBit(i) || i == currentCpu)
			continue;

		uint32 logicalId = gCPU[i].arch.logical_apic_id;
		apic_set_interrupt_command(logicalId, mode);
	}
}


void
arch_smp_send_broadcast_ici(void)
{
	#if KDEBUG
	if (are_interrupts_enabled())
		panic("arch_smp_send_broadcast_ici: called with interrupts enabled");
	#endif

	memory_write_barrier();

	uint32 mode = ICI_VECTOR | APIC_DELIVERY_MODE_FIXED
	| APIC_INTR_COMMAND_1_ASSERT
	| APIC_INTR_COMMAND_1_DEST_MODE_PHYSICAL
	| APIC_INTR_COMMAND_1_DEST_ALL_BUT_SELF;

	if (!wait_for_apic_interrupt_delivered(IPI_DELIVERY_TIMEOUT_US)) {
		panic("broadcast IPI delivery timeout from cpu %" B_PRId32,
			  smp_get_current_cpu());
	}
	apic_set_interrupt_command(0, mode);
}


void
arch_smp_send_ici(int32 target_cpu)
{
	#if KDEBUG
	if (are_interrupts_enabled())
		panic("arch_smp_send_ici: called with interrupts enabled");
	#endif

	if (target_cpu < 0 || target_cpu >= smp_get_num_cpus()) {
		panic("arch_smp_send_ici: invalid target cpu %" B_PRId32, target_cpu);
		return;
	}

	if (target_cpu == smp_get_current_cpu()) {
		panic("arch_smp_send_ici: target is current cpu");
		return;
	}

	memory_write_barrier();

	uint32 destination = sCPUAPICIds[target_cpu];
	uint32 mode = ICI_VECTOR | APIC_DELIVERY_MODE_FIXED
	| APIC_INTR_COMMAND_1_ASSERT
	| APIC_INTR_COMMAND_1_DEST_MODE_PHYSICAL
	| APIC_INTR_COMMAND_1_DEST_FIELD;

	if (!wait_for_apic_interrupt_delivered(IPI_DELIVERY_TIMEOUT_US)) {
		panic("IPI delivery timeout: cpu %" B_PRId32 " -> cpu %" B_PRId32
		" (apic %#" B_PRIx32 ")", smp_get_current_cpu(), target_cpu,
			  destination);
	}
	apic_set_interrupt_command(destination, mode);
}
