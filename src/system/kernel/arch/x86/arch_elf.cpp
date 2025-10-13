/*
 * Copyright 2004-2018, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 *
 * Copyright 2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#ifdef _BOOT_MODE
#	include <boot/arch.h>
#endif

#include <KernelExport.h>

#include <elf_priv.h>
#include <arch/elf.h>


//#define TRACE_ARCH_ELF
#ifdef TRACE_ARCH_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// Maximum relocations to process (prevents DoS from malformed ELF)
#define MAX_RELOCATION_COUNT 1000000


#ifndef _BOOT_MODE
static bool
is_in_image(struct elf_image_info *image, addr_t address)
{
	return (address >= image->text_region.start
	&& address < image->text_region.start + image->text_region.size)
	|| (address >= image->data_region.start
	&& address < image->data_region.start + image->data_region.size);
}
#endif	// !_BOOT_MODE


#if !defined(__x86_64__) || defined(ELF32_COMPAT)	\
|| (defined(_BOOT_MODE) && defined(BOOT_SUPPORT_ELF32))


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rel(struct preloaded_elf32_image *image, Elf32_Rel *rel,
						   int relLength)
#else
int
arch_elf_relocate_rel(struct elf_image_info *image,
					  struct elf_image_info *resolveImage, Elf32_Rel *rel, int relLength)
#endif
{
	if (rel == NULL || relLength < 0)
		return B_BAD_VALUE;

	// Validate relocation table alignment and size
	if (relLength % sizeof(Elf32_Rel) != 0) {
		dprintf("arch_elf_relocate_rel: invalid relocation table size %d\n",
				relLength);
		return B_BAD_DATA;
	}

	int relCount = relLength / sizeof(Elf32_Rel);
	if (relCount > MAX_RELOCATION_COUNT) {
		dprintf("arch_elf_relocate_rel: excessive relocation count %d "
		"(max %d)\n", relCount, MAX_RELOCATION_COUNT);
		return B_BAD_DATA;
	}

	for (int i = 0; i < relCount; i++) {
		int type = ELF32_R_TYPE(rel[i].r_info);
		int symIndex = ELF32_R_SYM(rel[i].r_info);
		Elf32_Addr S = 0;
		uint32 A = 0;
		uint32 P = 0;
		uint32 finalAddress;

		TRACE(("rel[%d]: offset %#lx, type %d, symIndex %d\n",
			   i, (unsigned long)rel[i].r_offset, type, symIndex));

		// Calculate S (symbol value)
		switch (type) {
			case R_386_32:
			case R_386_PC32:
			case R_386_GLOB_DAT:
			case R_386_JMP_SLOT:
			case R_386_GOTOFF:
			{
				Elf32_Sym *symbol = SYMBOL(image, symIndex);

				#ifdef _BOOT_MODE
				status_t status = boot_elf_resolve_symbol(image, symbol, &S);
				#else
				status_t status = elf_resolve_symbol(image, symbol,
													 resolveImage, &S);
				#endif
				if (status != B_OK) {
					dprintf("arch_elf_relocate_rel: failed to resolve symbol "
					"%d for relocation %d (type %d) at offset %#lx\n",
							symIndex, i, type, (unsigned long)rel[i].r_offset);
					return status;
				}
				TRACE(("S = %#lx\n", (unsigned long)S));
				break;
			}
		}

		// Calculate A (addend)
		switch (type) {
			case R_386_32:
			case R_386_PC32:
			case R_386_GOT32:
			case R_386_PLT32:
			case R_386_RELATIVE:
			case R_386_GOTOFF:
			case R_386_GOTPC:
				#ifndef _BOOT_MODE
				A = *(uint32 *)(image->text_region.delta + rel[i].r_offset);
				#else
				A = boot_elf32_get_relocation(image->text_region.delta
				+ rel[i].r_offset);
				#endif
				TRACE(("A = %#lx\n", (unsigned long)A));
				break;
		}

		// Calculate P (place/address being relocated)
		switch (type) {
			case R_386_PC32:
			case R_386_GOT32:
			case R_386_PLT32:
			case R_386_GOTPC:
				P = image->text_region.delta + rel[i].r_offset;
				TRACE(("P = %#lx\n", (unsigned long)P));
				break;
		}

		// Calculate final address
		switch (type) {
			case R_386_NONE:
				continue;

			case R_386_32:
				finalAddress = S + A;
				break;

			case R_386_PC32:
				finalAddress = S + A - P;
				break;

			case R_386_RELATIVE:
				finalAddress = image->text_region.delta + A;
				break;

			case R_386_JMP_SLOT:
			case R_386_GLOB_DAT:
				finalAddress = S;
				break;

			default:
				dprintf("arch_elf_relocate_rel: unhandled relocation type %d "
				"at index %d, offset %#lx\n", type, i,
			(unsigned long)rel[i].r_offset);
				return B_BAD_DATA;
		}

		uint32 *resolveAddress = (uint32 *)(addr_t)(image->text_region.delta
		+ rel[i].r_offset);

		#ifndef _BOOT_MODE
		if (!is_in_image(image, (addr_t)resolveAddress)) {
			dprintf("arch_elf_relocate_rel: invalid offset %#lx for "
			"relocation %d\n", (unsigned long)rel[i].r_offset, i);
			return B_BAD_ADDRESS;
		}
		*resolveAddress = finalAddress;
		#else
		boot_elf32_set_relocation((Elf32_Addr)(addr_t)resolveAddress,
								  finalAddress);
		#endif

		TRACE(("-> %#lx = %#lx\n", (unsigned long)resolveAddress,
			   (unsigned long)finalAddress));
	}

	return B_OK;
}


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rela(struct preloaded_elf32_image *image,
							Elf32_Rela *rel, int relLength)
#else
int
arch_elf_relocate_rela(struct elf_image_info *image,
					   struct elf_image_info *resolveImage, Elf32_Rela *rel, int relLength)
#endif
{
	dprintf("arch_elf_relocate_rela: not supported on x86_32\n");
	return B_ERROR;
}


#endif	// !defined(__x86_64__) || defined(ELF32_COMPAT)


#if (defined(__x86_64__) && !defined(ELF32_COMPAT)) || \
(defined(_BOOT_MODE) && defined(BOOT_SUPPORT_ELF64))


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rel(preloaded_elf64_image* image, Elf64_Rel* rel,
						   int relLength)
#else
int
arch_elf_relocate_rel(struct elf_image_info *image,
					  struct elf_image_info *resolveImage, Elf64_Rel *rel, int relLength)
#endif
{
	dprintf("arch_elf_relocate_rel: not supported on x86_64\n");
	return B_ERROR;
}


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rela(preloaded_elf64_image* image, Elf64_Rela* rel,
							int relLength)
#else
int
arch_elf_relocate_rela(struct elf_image_info *image,
					   struct elf_image_info *resolveImage, Elf64_Rela *rel, int relLength)
#endif
{
	if (rel == NULL || relLength < 0)
		return B_BAD_VALUE;

	// Validate relocation table alignment and size
	if (relLength % sizeof(Elf64_Rela) != 0) {
		dprintf("arch_elf_relocate_rela: invalid relocation table size %d\n",
				relLength);
		return B_BAD_DATA;
	}

	int relCount = relLength / sizeof(Elf64_Rela);
	if (relCount > MAX_RELOCATION_COUNT) {
		dprintf("arch_elf_relocate_rela: excessive relocation count %d "
		"(max %d)\n", relCount, MAX_RELOCATION_COUNT);
		return B_BAD_DATA;
	}

	for (int i = 0; i < relCount; i++) {
		int type = ELF64_R_TYPE(rel[i].r_info);
		int symIndex = ELF64_R_SYM(rel[i].r_info);
		Elf64_Addr symAddr = 0;

		// Skip R_X86_64_NONE
		if (type == 0)
			continue;

		// Resolve symbol if present
		if (symIndex != 0) {
			Elf64_Sym* symbol = SYMBOL(image, symIndex);

			#ifdef _BOOT_MODE
			status_t status = boot_elf_resolve_symbol(image, symbol, &symAddr);
			#else
			status_t status = elf_resolve_symbol(image, symbol, resolveImage,
												 &symAddr);
			#endif
			if (status != B_OK) {
				dprintf("arch_elf_relocate_rela: failed to resolve symbol %d "
				"for relocation %d (type %d) at offset %#lx\n",
						symIndex, i, type, (unsigned long)rel[i].r_offset);
				return status;
			}
		}

		// Calculate relocation address
		Elf64_Addr relocAddr = image->text_region.delta + rel[i].r_offset;

		// Calculate final value
		Elf64_Addr relocValue;
		switch (type) {
			case 0: // R_X86_64_NONE
				continue;

			case 1: // R_X86_64_64
				relocValue = symAddr + rel[i].r_addend;
				break;

			case 2: // R_X86_64_PC32
			{
				// Must fit in 32-bit signed to prevent silent overflow
				Elf64_Sxword temp = (Elf64_Sxword)(symAddr + rel[i].r_addend
				- rel[i].r_offset);
				if (temp != (int32)temp) {
					dprintf("arch_elf_relocate_rela: R_X86_64_PC32 overflow "
					"at relocation %d (value %#lx)\n", i,
							(unsigned long)temp);
					return B_BAD_DATA;
				}
				relocValue = (Elf64_Addr)temp;
				break;
			}

			case 6: // R_X86_64_GLOB_DAT
			case 7: // R_X86_64_JUMP_SLOT
				relocValue = symAddr + rel[i].r_addend;
				break;

			case 8: // R_X86_64_RELATIVE
				relocValue = image->text_region.delta + rel[i].r_addend;
				break;

			default:
				dprintf("arch_elf_relocate_rela: unhandled relocation type %d "
				"at index %d, offset %#lx\n", type, i,
			(unsigned long)rel[i].r_offset);
				return B_BAD_DATA;
		}

		#ifdef _BOOT_MODE
		boot_elf64_set_relocation(relocAddr, relocValue);
		#else
		if (!is_in_image(image, relocAddr)) {
			dprintf("arch_elf_relocate_rela: invalid offset %#lx for "
			"relocation %d\n", (unsigned long)rel[i].r_offset, i);
			return B_BAD_ADDRESS;
		}

		if (type == 2) // R_X86_64_PC32
			*(Elf32_Addr *)relocAddr = (Elf32_Addr)relocValue;
		else
			*(Elf64_Addr *)relocAddr = relocValue;
		#endif

		TRACE(("rela[%d]: offset %#lx, type %d -> %#lx\n",
			   i, (unsigned long)rel[i].r_offset, type,
			   (unsigned long)relocValue));
	}

	return B_OK;
}


#endif	// (defined(__x86_64__) && !defined(ELF32_COMPAT))
