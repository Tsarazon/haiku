/*
 * Copyright 2019-2025 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 (AArch64) ELF Runtime Relocation Support
 * 
 * This module implements dynamic relocation processing for ARM64 ELF binaries.
 * It handles RELA relocations used by the runtime loader when loading shared
 * libraries and executables.
 *
 * Key Features:
 * - Standard relocations (ABS64, RELATIVE, GLOB_DAT, JUMP_SLOT)
 * - TLS relocations (DTPMOD64, DTPREL64, TPREL64, TLSDESC)
 * - Indirect function relocations (IRELATIVE)
 * - COPY relocations for data initialization
 *
 * References:
 * - ELF for the Arm 64-bit Architecture (AArch64)
 *   https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "runtime_loader_private.h"

#include <runtime_loader.h>


//#define TRACE_RLD
#ifdef TRACE_RLD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static status_t
relocate_rela(image_t* rootImage, image_t* image, Elf64_Rela* rel,
	size_t relLength, SymbolLookupCache* cache)
{
	for (size_t i = 0; i < relLength / sizeof(Elf64_Rela); i++) {
		int type = ELF64_R_TYPE(rel[i].r_info);
		int symIndex = ELF64_R_SYM(rel[i].r_info);
		Elf64_Addr symAddr = 0;
		image_t* symbolImage = NULL;

		// Resolve the symbol, if any.
		if (symIndex != 0) {
			Elf64_Sym* sym = SYMBOL(image, symIndex);

			status_t status = resolve_symbol(rootImage, image, sym, cache,
				&symAddr, &symbolImage);
			if (status != B_OK) {
				TRACE(("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					SYMNAME(image, sym), status));
				printf("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					SYMNAME(image, sym), status);
				return status;
			}
		}

		// Address of the relocation.
		Elf64_Addr relocAddr = image->regions[0].delta + rel[i].r_offset;

		// Calculate the relocation value.
		Elf64_Addr relocValue;
		bool write64 = true;  // Most relocations are 64-bit
		
		switch (type) {
			case R_AARCH64_NONE:
				// No relocation needed
				continue;

			case R_AARCH64_ABS64:
				// S + A
				// Absolute 64-bit address
				relocValue = symAddr + rel[i].r_addend;
				break;

			case R_AARCH64_ABS32:
				// S + A
				// Absolute 32-bit address with overflow check
				relocValue = symAddr + rel[i].r_addend;
				write64 = false;
				// TODO: Add overflow check
				break;

			case R_AARCH64_ABS16:
				// S + A
				// Absolute 16-bit address with overflow check
				relocValue = symAddr + rel[i].r_addend;
				// TODO: Write as 16-bit and add overflow check
				write64 = false;
				break;

			case R_AARCH64_PREL64:
				// S + A - P
				// PC-relative 64-bit address
				relocValue = symAddr + rel[i].r_addend - rel[i].r_offset;
				break;

			case R_AARCH64_PREL32:
				// S + A - P
				// PC-relative 32-bit address with overflow check
				relocValue = symAddr + rel[i].r_addend - rel[i].r_offset;
				write64 = false;
				// TODO: Add overflow check
				break;

			case R_AARCH64_PREL16:
				// S + A - P
				// PC-relative 16-bit address with overflow check
				relocValue = symAddr + rel[i].r_addend - rel[i].r_offset;
				// TODO: Write as 16-bit and add overflow check
				write64 = false;
				break;

			case R_AARCH64_GLOB_DAT:
				// S + A
				// Set GOT entry to data address
				relocValue = symAddr + rel[i].r_addend;
				break;

			case R_AARCH64_JUMP_SLOT:
				// S + A
				// Set GOT entry to code address (PLT entry)
				relocValue = symAddr + rel[i].r_addend;
				break;

			case R_AARCH64_RELATIVE:
				// Delta(S) + A
				// Adjust by program base address
				relocValue = image->regions[0].delta + rel[i].r_addend;
				break;

			case R_AARCH64_TLS_DTPMOD64:
				// LDM(S)
				// TLS module ID
				relocValue = symbolImage == NULL
							? image->dso_tls_id : symbolImage->dso_tls_id;
				break;

			case R_AARCH64_TLS_DTPREL64:
				// DTPREL(S+A)
				// TLS offset relative to module's TLS block
				relocValue = symAddr + rel[i].r_addend;
				break;

			case R_AARCH64_TLS_TPREL64:
				// TPREL(S+A)
				// TLS offset relative to thread pointer
				// Note: This requires TLS support to be fully implemented
				relocValue = symAddr + rel[i].r_addend;
				// TODO: Add proper TP-relative calculation when TLS is implemented
				break;

			case R_AARCH64_TLSDESC:
				// TLSDESC(S+A)
				// TLS descriptor
				// This is a complex relocation that requires descriptor setup
				// For now, we handle it similarly to DTPREL64
				relocValue = symAddr + rel[i].r_addend;
				// TODO: Implement full TLS descriptor support
				break;

			case R_AARCH64_COPY:
				// Copy data from shared object
				// This should have been handled during symbol resolution
				// Nothing to do here
				continue;

			case R_AARCH64_IRELATIVE:
				// Indirect function relocation
				// Call resolver function to get actual address
				{
					// Resolver function address
					Elf64_Addr resolverAddr = image->regions[0].delta + rel[i].r_addend;
					
					// Call resolver (it returns the actual function address)
					typedef Elf64_Addr (*ResolverFunc)(void);
					ResolverFunc resolver = (ResolverFunc)resolverAddr;
					relocValue = resolver();
				}
				break;

			case R_AARCH64_TSTBR14:
			case R_AARCH64_CONDBR19:
			case R_AARCH64_JUMP26:
			case R_AARCH64_CALL26:
				// These are static relocations that should be handled by the linker
				// The runtime loader should not encounter them
				TRACE(("unexpected static relocation type %d\n", type));
				printf("runtime_loader: unexpected static relocation type %d\n", type);
				return B_BAD_DATA;

			default:
				TRACE(("unhandled relocation type %d\n", type));
				printf("runtime_loader: unhandled relocation type %d\n", type);
				return B_BAD_DATA;
		}

		// Write the relocation value
		if (type == R_AARCH64_ABS16 || type == R_AARCH64_PREL16) {
			*(Elf64_Half *)relocAddr = (Elf64_Half)relocValue;
		} else if (type == R_AARCH64_ABS32 || type == R_AARCH64_PREL32 || !write64) {
			*(Elf32_Addr *)relocAddr = (Elf32_Addr)relocValue;
		} else {
			*(Elf64_Addr *)relocAddr = relocValue;
		}
	}

	return B_OK;
}


status_t
arch_relocate_image(image_t* rootImage, image_t* image,
	SymbolLookupCache* cache)
{
	status_t status;

	TRACE(("ARM64: Relocating image %s\n", image->name));

	// ARM64 uses RELA relocations only (no REL)

	// Perform RELA relocations
	if (image->rela) {
		TRACE(("ARM64: Processing %lu RELA relocations\n", 
			image->rela_len / sizeof(Elf64_Rela)));
		status = relocate_rela(rootImage, image, image->rela, image->rela_len,
			cache);
		if (status != B_OK) {
			printf("runtime_loader: RELA relocation failed for %s\n", image->name);
			return status;
		}
	}

	// PLT relocations (they are RELA on ARM64)
	if (image->pltrel) {
		TRACE(("ARM64: Processing %lu PLT relocations\n",
			image->pltrel_len / sizeof(Elf64_Rela)));
		status = relocate_rela(rootImage, image, (Elf64_Rela*)image->pltrel,
			image->pltrel_len, cache);
		if (status != B_OK) {
			printf("runtime_loader: PLT relocation failed for %s\n", image->name);
			return status;
		}
	}

	TRACE(("ARM64: Image %s relocated successfully\n", image->name));

	return B_OK;
}
