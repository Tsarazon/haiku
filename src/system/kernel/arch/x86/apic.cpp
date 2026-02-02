/*
 * Copyright 2010, Michael Lotz, mmlr@mlotz.ch. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <arch/x86/apic.h>
#include <arch/x86/msi.h>

#include <debug.h>
#include <kernel/cpu.h>
#include <safemode.h>
#include <vm/vm.h>
#include <util/AutoLock.h>

#include "timers/apic_timer.h"


//#define TRACE_APIC
#ifdef TRACE_APIC
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


/*! x2APIC MSR base for register access.
	MSR address = X2APIC_MSR_BASE + (MMIO_offset >> 4).
	Intel SDM Vol. 3, Section 10.12.1, Table 10-6.
*/
#define X2APIC_MSR_BASE		0x800


static void* sLocalAPIC = NULL;
static bool sX2APIC = false;

// Register access backends, set once in apic_init().
static uint32 (*sApicRead)(uint32 offset);
static void (*sApicWrite)(uint32 offset, uint32 data);


// #pragma mark - xAPIC backend (Memory-Mapped I/O)


static uint32
xapic_read(uint32 offset)
{
	return *(volatile uint32*)((char*)sLocalAPIC + offset);
}


static void
xapic_write(uint32 offset, uint32 data)
{
	*(volatile uint32*)((char*)sLocalAPIC + offset) = data;
}


// #pragma mark - x2APIC backend (MSR)


static uint32
x2apic_read(uint32 offset)
{
	return (uint32)x86_read_msr(X2APIC_MSR_BASE + (offset >> 4));
}


static void
x2apic_write(uint32 offset, uint32 data)
{
	x86_write_msr(X2APIC_MSR_BASE + (offset >> 4), data);
}


// #pragma mark - Public API


bool
apic_available()
{
	return sLocalAPIC != NULL || sX2APIC;
}


bool
x2apic_available()
{
	return sX2APIC;
}


uint32
apic_local_id()
{
	uint32 id = sApicRead(APIC_ID);
	// xAPIC stores the ID in bits [31:24], x2APIC uses the full register
	return sX2APIC ? id : ((id & 0xffffffff) >> 24);
}


uint32
apic_local_version()
{
	return sApicRead(APIC_VERSION);
}


uint32
apic_task_priority()
{
	return sApicRead(APIC_TASK_PRIORITY);
}


void
apic_set_task_priority(uint32 config)
{
	sApicWrite(APIC_TASK_PRIORITY, config);
}


void
apic_end_of_interrupt()
{
	sApicWrite(APIC_EOI, 0);
}


uint32
apic_logical_apic_id()
{
	return sApicRead(APIC_LOGICAL_DEST);
}


void
apic_disable_local_ints()
{
	sApicWrite(APIC_LVT_LINT0, APIC_LVT_MASKED);
	sApicWrite(APIC_LVT_LINT1, APIC_LVT_MASKED);
}


uint32
apic_spurious_intr_vector()
{
	return sApicRead(APIC_SPURIOUS_INTR_VECTOR);
}


void
apic_set_spurious_intr_vector(uint32 config)
{
	sApicWrite(APIC_SPURIOUS_INTR_VECTOR, config);
}


void
apic_set_interrupt_command(uint32 destination, uint32 mode)
{
	if (sX2APIC) {
		// x2APIC: single 64-bit MSR write, full destination in upper 32 bits
		uint64 command = (uint64)destination << 32 | mode;
		x86_write_msr(IA32_MSR_APIC_INTR_COMMAND, command);
	} else {
		// xAPIC: two separate 32-bit writes, destination in bits [31:24]
		sApicWrite(APIC_INTR_COMMAND_2, destination << 24);
		sApicWrite(APIC_INTR_COMMAND_1, mode);
	}
}


bool
apic_interrupt_delivered(void)
{
	// x2APIC writes are serializing, delivery is immediate
	if (sX2APIC)
		return true;
	return (sApicRead(APIC_INTR_COMMAND_1) & APIC_DELIVERY_STATUS) == 0;
}


uint32
apic_lvt_timer()
{
	return sApicRead(APIC_LVT_TIMER);
}


void
apic_set_lvt_timer(uint32 config)
{
	sApicWrite(APIC_LVT_TIMER, config);
}


uint32
apic_lvt_error()
{
	return sApicRead(APIC_LVT_ERROR);
}


void
apic_set_lvt_error(uint32 config)
{
	sApicWrite(APIC_LVT_ERROR, config);
}


uint32
apic_lvt_initial_timer_count()
{
	return sApicRead(APIC_INITIAL_TIMER_COUNT);
}


void
apic_set_lvt_initial_timer_count(uint32 config)
{
	sApicWrite(APIC_INITIAL_TIMER_COUNT, config);
}


uint32
apic_lvt_current_timer_count()
{
	return sApicRead(APIC_CURRENT_TIMER_COUNT);
}


uint32
apic_lvt_timer_divide_config()
{
	return sApicRead(APIC_TIMER_DIVIDE_CONFIG);
}


void
apic_set_lvt_timer_divide_config(uint32 config)
{
	sApicWrite(APIC_TIMER_DIVIDE_CONFIG, config);
}


// #pragma mark - Initialization


status_t
apic_init(kernel_args* args)
{
	if (args->arch_args.apic == NULL)
		return B_NO_INIT;

	uint64 apic_base = x86_read_msr(IA32_MSR_APIC_BASE);

	if (x86_check_feature(IA32_FEATURE_EXT_X2APIC, FEATURE_EXT)
		&& (x86_check_feature(IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT)
			|| ((apic_base & IA32_MSR_APIC_BASE_X2APIC) != 0))) {
		TRACE("found x2apic\n");

		if (get_safemode_boolean(B_SAFEMODE_DISABLE_X2APIC, false)) {
			TRACE("x2apic disabled per safemode setting\n");
		} else {
			sX2APIC = true;
			sApicRead = x2apic_read;
			sApicWrite = x2apic_write;
			return B_OK;
		}
	}

	sLocalAPIC = args->arch_args.apic;
	TRACE("mapping local apic at %p\n", sLocalAPIC);
	if (vm_map_physical_memory(B_SYSTEM_TEAM, "local apic", &sLocalAPIC,
			B_EXACT_ADDRESS, B_PAGE_SIZE,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
			args->arch_args.apic_phys, true) < 0) {
		panic("mapping the local apic failed");
		return B_ERROR;
	}

	sApicRead = xapic_read;
	sApicWrite = xapic_write;

	return B_OK;
}


status_t
apic_per_cpu_init(kernel_args* args, int32 cpu)
{
	if (sX2APIC) {
		// Ensure x2APIC mode is enabled on this CPU
		uint64 apic_base = x86_read_msr(IA32_MSR_APIC_BASE);
		if ((apic_base & IA32_MSR_APIC_BASE_X2APIC) == 0) {
			x86_write_msr(IA32_MSR_APIC_BASE, apic_base
				| IA32_MSR_APIC_BASE_X2APIC);
		}
	}

	TRACE("setting up %sapic for CPU %" B_PRId32 ": apic id %" B_PRIu32 ", "
		"version %" B_PRIu32 "\n", sX2APIC ? "x2" : "", cpu, apic_local_id(),
		apic_local_version());

	// xAPIC flat logical destination mode (not used in x2APIC)
	if (!sX2APIC && cpu < 8) {
		sApicWrite(APIC_DEST_FORMAT, uint32(-1));

		uint8 logical_apic_id = 1 << cpu;
		uint32 value = sApicRead(APIC_LOGICAL_DEST);
		value &= 0xffffff;
		sApicWrite(APIC_LOGICAL_DEST, value | (logical_apic_id << 24));
	}

	// get logical APIC ID
	gCPU[cpu].arch.logical_apic_id = apic_logical_apic_id();
	if (!sX2APIC)
		gCPU[cpu].arch.logical_apic_id >>= 24;
	TRACE("CPU %" B_PRId32 ": logical apic id: %#" B_PRIx32 "\n", cpu,
		gCPU[cpu].arch.logical_apic_id);

	gCPU[cpu].arch.acpi_processor_id = -1;

	/* set spurious interrupt vector to 0xff */
	uint32 config = apic_spurious_intr_vector() & 0xffffff00;
	config |= APIC_ENABLE | 0xff;
	apic_set_spurious_intr_vector(config);

	// don't touch the LINT0/1 configuration in virtual wire mode
	// ToDo: implement support for other modes...
#if 0
	if (cpu == 0) {
		/* setup LINT0 as ExtINT */
		config = (sApicRead(APIC_LINT0) & 0xffff00ff);
		config |= APIC_LVT_DM_ExtINT | APIC_LVT_IIPP | APIC_LVT_TM;
		sApicWrite(APIC_LINT0, config);

		/* setup LINT1 as NMI */
		config = (sApicRead(APIC_LINT1) & 0xffff00ff);
		config |= APIC_LVT_DM_NMI | APIC_LVT_IIPP;
		sApicWrite(APIC_LINT1, config);
	}
	if (cpu > 0) {
		dprintf("LINT0: %p\n", (void*)sApicRead(APIC_LINT0));
		dprintf("LINT1: %p\n", (void*)sApicRead(APIC_LINT1));

		/* disable LINT0/1 */
		config = sApicRead(APIC_LINT0);
		sApicWrite(APIC_LINT0, config | APIC_LVT_MASKED);

		config = sApicRead(APIC_LINT1);
		sApicWrite(APIC_LINT1, config | APIC_LVT_MASKED);
	} else {
		dprintf("0: LINT0: %p\n", (void*)sApicRead(APIC_LINT0));
		dprintf("0: LINT1: %p\n", (void*)sApicRead(APIC_LINT1));
	}
#endif

	apic_timer_per_cpu_init(args, cpu);

	/* setup error vector to 0xfe */
	config = (apic_lvt_error() & 0xffffff00) | 0xfe;
	apic_set_lvt_error(config);

	/* accept all interrupts */
	config = apic_task_priority() & 0xffffff00;
	apic_set_task_priority(config);

	config = apic_spurious_intr_vector();
	apic_end_of_interrupt();

	return B_OK;
}
