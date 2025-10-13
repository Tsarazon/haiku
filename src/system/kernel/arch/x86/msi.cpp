/*
 * Copyright 2010-2011, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 */

#include <arch/x86/apic.h>
#include <arch/x86/msi.h>
#include <arch/x86/arch_smp.h>

#include <debug.h>
#include <interrupts.h>
#include <lock.h>


struct MSIConfiguration {
	uint64*	fAddress;
	uint32* fData;
};


// MSI configuration table: stores pointers to driver-owned address/data
// variables. Only the base vector of an MSI block is stored (MSI standard
// behavior - all vectors in a block share the same address/data template).
static MSIConfiguration sMSIConfigurations[NUM_IO_VECTORS];
static spinlock sMSILock = B_SPINLOCK_INITIALIZER;

static bool sMSISupported = false;
static uint32 sBootCPUAPICId = 0;


void
msi_init(kernel_args* args)
{
	if (!apic_available()) {
		dprintf("disabling MSI: APIC not available\n");
		return;
	}

	if (args->num_cpus == 0) {
		dprintf("disabling MSI: no CPUs detected\n");
		return;
	}

	sBootCPUAPICId = args->arch_args.cpu_apic_id[0];

	memory_write_barrier();
	sMSISupported = true;
	memory_write_barrier();

	dprintf("MSI support enabled (boot CPU APIC ID %#" B_PRIx32 ")\n",
			sBootCPUAPICId);
}


bool
msi_supported()
{
	memory_read_barrier();
	return sMSISupported;
}


status_t
msi_allocate_vectors(uint32 count, uint32 *startVector, uint64 *address,
					 uint32 *data)
{
	if (!msi_supported())
		return B_UNSUPPORTED;

	if (count == 0 || count > NUM_IO_VECTORS
		|| startVector == NULL || address == NULL || data == NULL) {
		return B_BAD_VALUE;
		}

		int32 vector;
	status_t result = allocate_io_interrupt_vectors(count, &vector,
													INTERRUPT_TYPE_IRQ);
	if (result != B_OK)
		return result;

	if (vector < 0 || vector >= NUM_IO_VECTORS) {
		free_io_interrupt_vectors(count, vector);
		panic("allocate_io_interrupt_vectors returned invalid vector %" B_PRId32,
			  vector);
		return B_ERROR;
	}

	if ((uint32)vector + count > NUM_IO_VECTORS) {
		free_io_interrupt_vectors(count, vector);
		dprintf("msi_allocate_vectors: allocated range exceeds maximum\n");
		return B_NO_MEMORY;
	}

	cpu_status state = disable_interrupts();
	acquire_spinlock(&sMSILock);

	sMSIConfigurations[vector].fAddress = address;
	sMSIConfigurations[vector].fData = data;

	release_spinlock(&sMSILock);
	restore_interrupts(state);

	x86_set_irq_source(vector, IRQ_SOURCE_MSI);

	*startVector = (uint32)vector;
	*address = MSI_ADDRESS_BASE
	| ((uint64)sBootCPUAPICId << MSI_DESTINATION_ID_SHIFT)
	| MSI_NO_REDIRECTION
	| MSI_DESTINATION_MODE_PHYSICAL;
	*data = MSI_TRIGGER_MODE_EDGE
	| MSI_DELIVERY_MODE_FIXED
	| ((uint16)vector + ARCH_INTERRUPT_BASE);

	dprintf("msi_allocate_vectors: allocated %" B_PRIu32 " vectors starting "
	"from %" B_PRIu32 "\n", count, *startVector);
	return B_OK;
}


void
msi_free_vectors(uint32 count, uint32 startVector)
{
	if (!msi_supported()) {
		panic("msi_free_vectors: MSI not supported");
		return;
	}

	if (count == 0 || startVector >= NUM_IO_VECTORS) {
		panic("msi_free_vectors: invalid parameters (count %" B_PRIu32
		", start %" B_PRIu32 ")", count, startVector);
		return;
	}

	if (startVector + count > NUM_IO_VECTORS) {
		panic("msi_free_vectors: range exceeds NUM_IO_VECTORS");
		return;
	}

	dprintf("msi_free_vectors: freeing %" B_PRIu32 " vectors starting from %"
	B_PRIu32 "\n", count, startVector);

	cpu_status state = disable_interrupts();
	acquire_spinlock(&sMSILock);

	for (uint32 i = 0; i < count; i++) {
		sMSIConfigurations[startVector + i].fAddress = NULL;
		sMSIConfigurations[startVector + i].fData = NULL;
	}

	release_spinlock(&sMSILock);
	restore_interrupts(state);

	free_io_interrupt_vectors(count, startVector);
}


void
msi_assign_interrupt_to_cpu(uint32 irq, int32 cpu)
{
	if (!msi_supported()) {
		dprintf("msi_assign_interrupt_to_cpu: MSI not supported\n");
		return;
	}

	if (cpu < 0 || cpu >= smp_get_num_cpus()) {
		dprintf("msi_assign_interrupt_to_cpu: invalid CPU %" B_PRId32 "\n",
				cpu);
		return;
	}

	if (irq >= NUM_IO_VECTORS) {
		dprintf("msi_assign_interrupt_to_cpu: invalid IRQ %" B_PRIu32 "\n",
				irq);
		return;
	}

	uint32 apicId = x86_get_cpu_apic_id(cpu);

	cpu_status state = disable_interrupts();
	acquire_spinlock(&sMSILock);

	uint64* address = sMSIConfigurations[irq].fAddress;
	if (address == NULL) {
		release_spinlock(&sMSILock);
		restore_interrupts(state);
		dprintf("msi_assign_interrupt_to_cpu: IRQ %" B_PRIu32
		" not configured for MSI\n", irq);
		return;
	}

	release_spinlock(&sMSILock);
	restore_interrupts(state);

	// Update driver's MSI address variable (driver will copy to device)
	*address = MSI_ADDRESS_BASE
	| ((uint64)apicId << MSI_DESTINATION_ID_SHIFT)
	| MSI_NO_REDIRECTION
	| MSI_DESTINATION_MODE_PHYSICAL;
}
