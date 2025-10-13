/*
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/vm_translation_map.h>

#include <boot/kernel_args.h>
#include <safemode.h>

#ifdef __x86_64__
#	include "paging/64bit/X86PagingMethod64Bit.h"
#else
#	include "paging/32bit/X86PagingMethod32Bit.h"
#	include "paging/pae/X86PagingMethodPAE.h"
#endif


//#define TRACE_VM_TMAP
#ifdef TRACE_VM_TMAP
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


static union {
	uint64	align;
	#ifdef __x86_64__
	char	sixty_four[sizeof(X86PagingMethod64Bit)];
	#else
	char	thirty_two[sizeof(X86PagingMethod32Bit)];
	#if B_HAIKU_PHYSICAL_BITS == 64
	char	pae[sizeof(X86PagingMethodPAE)];
	#endif
	#endif
} sPagingMethodBuffer;


// #pragma mark - Initialization


static bool
validate_physical_memory_ranges(kernel_args* args)
{
	// Sanity check the memory ranges before we trust them
	if (args->num_physical_memory_ranges > MAX_PHYSICAL_MEMORY_RANGE) {
		dprintf("ERROR: Too many physical memory ranges (%" B_PRIu32 " > %u)\n",
				args->num_physical_memory_ranges, MAX_PHYSICAL_MEMORY_RANGE);
		return false;
	}

	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t start = args->physical_memory_range[i].start;
		phys_addr_t size = args->physical_memory_range[i].size;
		if (size == 0) {
			dprintf("WARNING: Empty physical memory range at index %" B_PRIu32 "\n", i);
			continue;
		}
		if (start + size < start) {
			dprintf("ERROR: Physical memory range %" B_PRIu32 " overflows "
			"(%#" B_PRIxPHYSADDR " + %#" B_PRIxPHYSADDR ")\n",
					i, start, size);
			return false;
		}
	}

	return true;
}


#ifdef __x86_64__

static status_t
init_64bit_paging_method(kernel_args* args)
{
	bool la57Available = x86_check_feature(IA32_FEATURE_LA57, FEATURE_7_ECX);
	bool la57Enabled = false;

	if (la57Available) {
		uint64 cr4 = x86_read_cr4();
		la57Enabled = (cr4 & IA32_CR4_LA57) != 0;

		// Verify LA57 is actually working if enabled
		if (la57Enabled) {
			uint64 testCr4 = x86_read_cr4();
			if ((testCr4 & IA32_CR4_LA57) == 0) {
				dprintf("WARNING: LA57 enabled but CR4.LA57 reads back as 0\n");
				la57Enabled = false;
			}
		}
	}

	if (la57Enabled)
		dprintf("using 5-level paging (LA57)\n");
	else
		dprintf("using 4-level paging\n");

	gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethod64Bit(la57Enabled);

	// Verify placement new succeeded by checking vtable pointer is non-null
	if (gX86PagingMethod == NULL || *(void**)gX86PagingMethod == NULL) {
		panic("failed to construct X86PagingMethod64Bit");
		return B_NO_MEMORY;
	}

	return B_OK;
}

#else // !__x86_64__

static bool
is_pae_needed(kernel_args* args)
{
	// PAE required if NX bit is available (security feature)
	if (x86_check_feature(IA32_FEATURE_AMD_EXT_NX, FEATURE_EXT_AMD))
		return true;

	// PAE required if any physical memory above 4GB
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t end = args->physical_memory_range[i].start
		+ args->physical_memory_range[i].size;
		if (end > 0x100000000ULL)
			return true;
	}

	return false;
}


static status_t
init_32bit_paging_method(kernel_args* args)
{
	bool paeAvailable = x86_check_feature(IA32_FEATURE_PAE, FEATURE_COMMON);
	bool paeNeeded = is_pae_needed(args);
	bool paeDisabled = get_safemode_boolean_early(args,
												  B_SAFEMODE_4_GB_MEMORY_LIMIT, false);

	bool usePAE = paeAvailable && paeNeeded && !paeDisabled;

	#if B_HAIKU_PHYSICAL_BITS == 64
	if (usePAE) {
		// Verify PAE actually works by checking CR4
		uint64 cr4 = x86_read_cr4();
		if ((cr4 & IA32_CR4_PAE) != 0) {
			// Already enabled, verify it reads back
			uint64 testCr4 = x86_read_cr4();
			if ((testCr4 & IA32_CR4_PAE) == 0) {
				dprintf("WARNING: PAE enabled but CR4.PAE reads back as 0\n");
				usePAE = false;
			}
		}
	}

	if (usePAE) {
		dprintf("using PAE paging\n");
		gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethodPAE;
	} else {
		if (paeNeeded && !paeAvailable) {
			dprintf("ERROR: PAE needed but not available\n");
			return B_NOT_SUPPORTED;
		}
		if (paeNeeded && paeDisabled) {
			dprintf("WARNING: PAE needed but disabled via safemode\n");
		}
		dprintf("using 32-bit paging\n");
		gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethod32Bit;
	}
	#else
	dprintf("using 32-bit paging\n");
	gX86PagingMethod = new(&sPagingMethodBuffer) X86PagingMethod32Bit;
	#endif

	if (gX86PagingMethod == NULL || *(void**)gX86PagingMethod == NULL) {
		panic("failed to construct paging method");
		return B_NO_MEMORY;
	}

	return B_OK;
}

#endif // !__x86_64__


// #pragma mark - VM API


status_t
arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** _map)
{
	if (gX86PagingMethod == NULL)
		panic("arch_vm_translation_map_create_map: paging method not initialized");

	return gX86PagingMethod->CreateTranslationMap(kernel, _map);
}


status_t
arch_vm_translation_map_init(kernel_args *args,
							 VMPhysicalPageMapper** _physicalPageMapper)
{
	TRACE("vm_translation_map_init: entry\n");

	if (args == NULL) {
		panic("arch_vm_translation_map_init: NULL kernel_args");
		return B_BAD_VALUE;
	}

	if (!validate_physical_memory_ranges(args))
		return B_BAD_DATA;

	#ifdef TRACE_VM_TMAP
	TRACE("physical memory ranges:\n");
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t start = args->physical_memory_range[i].start;
		phys_addr_t end = start + args->physical_memory_range[i].size;
		TRACE("  %#10" B_PRIxPHYSADDR " - %#10" B_PRIxPHYSADDR "\n", start, end);
	}

	TRACE("allocated physical ranges:\n");
	for (uint32 i = 0; i < args->num_physical_allocated_ranges; i++) {
		phys_addr_t start = args->physical_allocated_range[i].start;
		phys_addr_t end = start + args->physical_allocated_range[i].size;
		TRACE("  %#10" B_PRIxPHYSADDR " - %#10" B_PRIxPHYSADDR "\n", start, end);
	}

	TRACE("allocated virtual ranges:\n");
	for (uint32 i = 0; i < args->num_virtual_allocated_ranges; i++) {
		addr_t start = args->virtual_allocated_range[i].start;
		addr_t end = start + args->virtual_allocated_range[i].size;
		TRACE("  %#10" B_PRIxADDR " - %#10" B_PRIxADDR "\n", start, end);
	}
	#endif

	status_t status;
	#ifdef __x86_64__
	status = init_64bit_paging_method(args);
	#else
	status = init_32bit_paging_method(args);
	#endif

	if (status != B_OK)
		return status;

	ASSERT(gX86PagingMethod != NULL);
	return gX86PagingMethod->Init(args, _physicalPageMapper);
}


status_t
arch_vm_translation_map_init_post_sem(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_translation_map_init_post_area(kernel_args *args)
{
	TRACE("vm_translation_map_init_post_area: entry\n");

	if (gX86PagingMethod == NULL) {
		panic("arch_vm_translation_map_init_post_area: paging method not initialized");
		return B_NO_INIT;
	}

	return gX86PagingMethod->InitPostArea(args);
}


status_t
arch_vm_translation_map_early_map(kernel_args *args, addr_t va, phys_addr_t pa,
								  uint8 attributes)
{
	TRACE("early_tmap: entry pa %#" B_PRIxPHYSADDR " va %#" B_PRIxADDR "\n", pa, va);

	if (gX86PagingMethod == NULL) {
		panic("arch_vm_translation_map_early_map: paging method not initialized");
		return B_NO_INIT;
	}

	// Validate address ranges to catch bootloader bugs
	if (va < KERNEL_LOAD_BASE || va >= KERNEL_TOP) {
		dprintf("ERROR: early_map virtual address %#" B_PRIxADDR " outside kernel space\n", va);
		return B_BAD_ADDRESS;
	}

	return gX86PagingMethod->MapEarly(args, va, pa, attributes);
}


/*!	Verifies that the page at the given virtual address can be accessed in the
 * current context.
 *
 * This function is invoked in the kernel debugger. Paranoid checking is in
 * order.
 *
 * \param virtualAddress The virtual address to be checked.
 * \param protection The area protection for which to check. Valid is a bitwise
 *	or of one or more of \c B_KERNEL_READ_AREA or \c B_KERNEL_WRITE_AREA.
 * \return \c true, if the address can be accessed in all ways specified by
 *	\a protection, \c false otherwise.
 */
bool
arch_vm_translation_map_is_kernel_page_accessible(addr_t virtualAddress,
												  uint32 protection)
{
	// Early boot or paging not initialized - be conservative
	if (gX86PagingMethod == NULL)
		return true;

	// Sanity check the virtual address
	if (virtualAddress >= KERNEL_TOP) {
		// Clearly invalid kernel address
		return false;
	}

	// Let paging method do detailed check
	return gX86PagingMethod->IsKernelPageAccessible(virtualAddress, protection);
}
