/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Heap Management
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/heap.h>

#include "efi_platform.h"


static void* sHeapBase = NULL;
static size_t sHeapSize = 0;


extern "C" status_t
platform_init_heap(stage2_args* args, void** _base, void** _top)
{
	// Initialize heap using EFI boot services
	size_t heapSize = 64 * 1024 * 1024; // 64MB initial heap
	
	if (kBootServices == NULL)
		return B_ERROR;

	size_t pages = (heapSize + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
	efi_physical_addr addr = 0;
	
	efi_status status = kBootServices->AllocatePages(AllocateAnyPages,
		EfiLoaderData, pages, &addr);
		
	if (status != EFI_SUCCESS) {
		dprintf("Failed to allocate heap memory\n");
		return B_NO_MEMORY;
	}

	sHeapBase = (void*)addr;
	sHeapSize = pages * B_PAGE_SIZE;

	*_base = sHeapBase;
	*_top = (void*)((addr_t)sHeapBase + sHeapSize);

	dprintf("ARM64 EFI heap: %p - %p (%zu bytes)\n", 
		*_base, *_top, sHeapSize);

	return B_OK;
}


extern "C" void
platform_release_heap(stage2_args* args, void* base)
{
	// Release heap memory back to EFI
	if (sHeapBase != NULL && kBootServices != NULL) {
		size_t pages = sHeapSize / B_PAGE_SIZE;
		kBootServices->FreePages((efi_physical_addr)sHeapBase, pages);
		sHeapBase = NULL;
		sHeapSize = 0;
	}
}