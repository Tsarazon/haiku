/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 UEFI Boot Loader Declarations
 */
#ifndef ARM64_UEFI_H
#define ARM64_UEFI_H


#include <SupportDefs.h>


#ifdef __cplusplus
extern "C" {
#endif


// ARM64 Exception Level Management
uint32 arm64_detect_exception_level(void);
uint32 arm64_init_exception_level(void);
uint32 arm64_transition_to_el1(void);
uint32 arm64_setup_el1_environment(void);

// ARM64 CPU Information
uint64 arm64_get_midr(void);
uint64 arm64_get_mpidr(void);
uint32 arm64_get_current_el(void);

// ARM64 Cache Management
void arm64_cache_flush_range(addr_t start, size_t size);
void arm64_cache_flush_all(void);
void arm64_invalidate_icache(void);
void arm64_memory_barrier(void);

// ARM64 MMU Management
uint32 arm64_enable_mmu(uint64 ttbr0, uint64 ttbr1, uint64 tcr, uint64 mair);

// ARM64 Kernel Handoff
void arch_enter_kernel(kernel_args* kernelArgs, addr_t kernelEntry, addr_t stackTop);


// ARM64-specific constants
#define ARM64_EL0	0
#define ARM64_EL1	1
#define ARM64_EL2	2
#define ARM64_EL3	3

// ARM64 Exception Level Error Codes
#define ARM64_EL_SUCCESS		0
#define ARM64_EL_WRONG_LEVEL		1
#define ARM64_EL_TRANSITION_FAILED	2
#define ARM64_EL_FINAL_CHECK_FAILED	3


#ifdef __cplusplus
}
#endif


#endif	/* ARM64_UEFI_H */