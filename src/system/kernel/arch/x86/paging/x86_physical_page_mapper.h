/*
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef KERNEL_ARCH_X86_PAGING_X86_PHYSICAL_PAGE_MAPPER_H
#define KERNEL_ARCH_X86_PAGING_X86_PHYSICAL_PAGE_MAPPER_H


#include <vm/VMTranslationMap.h>


struct kernel_args;


class TranslationMapPhysicalPageMapper {
public:
	void	Delete();

	void*	GetPageTableAt(phys_addr_t physicalAddress);
		// Must be invoked with thread pinned to current CPU.
};


class X86PhysicalPageMapper final : public VMPhysicalPageMapper {
public:
	status_t	CreateTranslationMapPhysicalPageMapper(
					TranslationMapPhysicalPageMapper** _mapper);

	void*		InterruptGetPageTableAt(phys_addr_t physicalAddress);

	status_t	GetPage(phys_addr_t physicalAddress, addr_t* virtualAddress,
					void** handle) override;
	status_t	PutPage(addr_t virtualAddress, void* handle) override;

	status_t	GetPageCurrentCPU(phys_addr_t physicalAddress,
					addr_t* virtualAddress, void** handle) override;
	status_t	PutPageCurrentCPU(addr_t virtualAddress, void* handle) override;

	status_t	GetPageDebug(phys_addr_t physicalAddress,
					addr_t* virtualAddress, void** handle) override;
	status_t	PutPageDebug(addr_t virtualAddress, void* handle) override;

	status_t	MemsetPhysical(phys_addr_t address, int value,
					phys_size_t length) override;
	status_t	MemcpyFromPhysical(void* to, phys_addr_t from, size_t length,
					bool user) override;
	status_t	MemcpyToPhysical(phys_addr_t to, const void* from,
					size_t length, bool user) override;
	void		MemcpyPhysicalPage(phys_addr_t to, phys_addr_t from) override;
};


#include "paging/x86_physical_page_mapper_mapped.h"


#endif	// KERNEL_ARCH_X86_PAGING_X86_PHYSICAL_PAGE_MAPPER_H
