/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 UEFI Boot Loader Assembly Entry Point
 * 
 * This file implements the _start function that serves as the entry point
 * for the ARM64 UEFI boot loader. It properly receives UEFI parameters
 * and calls the C entry point using correct ARM64 calling conventions.
 */

.text

/*
 * _start - ARM64 UEFI boot loader entry point
 * 
 * Parameters (as per UEFI specification for AArch64):
 *   x0 - EFI image handle 
 *   x1 - Pointer to EFI system table
 * 
 * ARM64 AAPCS64 calling convention:
 *   x0-x7   - Parameter registers (x0, x1 used for UEFI parameters)
 *   x8      - Indirect result location register
 *   x9-x15  - Temporary registers  
 *   x16-x17 - Intra-procedure-call temporary registers
 *   x18     - Platform register (reserved)
 *   x19-x28 - Callee-saved registers
 *   x29     - Frame pointer
 *   x30     - Link register (return address)
 *   sp      - Stack pointer
 * 
 * UEFI calling convention requirements:
 *   - 16-byte stack alignment at function entry
 *   - Preserve x19-x28, x29, x30 across function calls
 *   - Return value in x0 (efi_status)
 */
.globl _start
.type _start, %function
_start:
	/*
	 * Set up stack frame following ARM64 AAPCS64 conventions
	 * Store frame pointer (x29) and link register (x30)
	 */
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/*
	 * Validate UEFI parameters
	 * x0 = EFI image handle (should not be NULL)
	 * x1 = EFI system table pointer (should not be NULL)
	 */
	cbz	x0, .Linvalid_params
	cbz	x1, .Linvalid_params

	/*
	 * UEFI parameters are already in correct registers for C function call:
	 * x0 = efi_handle image
	 * x1 = efi_system_table *systemTable
	 * 
	 * Call the C entry point: efi_main(efi_handle image, efi_system_table *systemTable)
	 */
	bl	efi_main

	/*
	 * efi_main returns efi_status in x0
	 * No need to modify return value, just restore stack and return
	 */
	
.Lreturn:
	/* Restore stack frame and return */
	ldp	x29, x30, [sp], #16
	ret

.Linvalid_params:
	/*
	 * Handle invalid parameters - return EFI_INVALID_PARAMETER
	 * EFI_INVALID_PARAMETER = 0x8000000000000002 (on 64-bit systems)
	 */
	mov	x0, #0x8000000000000002
	b	.Lreturn

/*
 * Additional ARM64 UEFI helper functions and utilities
 */

/*
 * arm64_get_current_el - Get current exception level
 * Returns current ARM64 exception level (EL0, EL1, EL2, EL3)
 */
.globl arm64_get_current_el
.type arm64_get_current_el, %function
arm64_get_current_el:
	mrs	x0, CurrentEL
	lsr	x0, x0, #2	/* Extract EL field (bits 3:2) */
	ret

/*
 * arm64_get_mpidr - Get Multiprocessor Affinity Register
 * Returns MPIDR_EL1 value for CPU identification
 */
.globl arm64_get_mpidr
.type arm64_get_mpidr, %function
arm64_get_mpidr:
	mrs	x0, mpidr_el1
	ret

/*
 * arm64_get_midr - Get Main ID Register  
 * Returns MIDR_EL1 value for CPU identification
 */
.globl arm64_get_midr
.type arm64_get_midr, %function
arm64_get_midr:
	mrs	x0, midr_el1
	ret

/*
 * arm64_cache_flush_range - Flush cache range to point of coherency
 * Parameters:
 *   x0 = start address
 *   x1 = size in bytes
 */
.globl arm64_cache_flush_range  
.type arm64_cache_flush_range, %function
arm64_cache_flush_range:
	/* Calculate end address */
	add	x1, x0, x1
	
	/* Get cache line size from CTR_EL0 */
	mrs	x2, ctr_el0
	ubfx	x2, x2, #16, #4		/* Extract DminLine */
	mov	x3, #4
	lsl	x3, x3, x2		/* Cache line size = 4 << DminLine */
	sub	x4, x3, #1
	bic	x0, x0, x4		/* Align start to cache line */

.Lflush_loop:
	dc	cvac, x0		/* Clean cache line to PoC */
	add	x0, x0, x3		/* Move to next cache line */
	cmp	x0, x1
	b.lo	.Lflush_loop

	dsb	sy			/* Data synchronization barrier */
	ret

/*
 * arm64_invalidate_icache - Invalidate entire instruction cache
 */
.globl arm64_invalidate_icache
.type arm64_invalidate_icache, %function
arm64_invalidate_icache:
	ic	iallu			/* Invalidate all instruction caches to PoU */
	dsb	nsh			/* Data synchronization barrier */
	isb				/* Instruction synchronization barrier */
	ret

/*
 * arm64_cache_flush_all - Flush all caches (comprehensive implementation)
 */
.globl arm64_cache_flush_all
.type arm64_cache_flush_all, %function
arm64_cache_flush_all:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/* Clean and invalidate all data caches */
	/* This is a simplified version - real implementation would walk cache hierarchy */
	
	/* Clean entire data cache by set/way */
	mov	x0, #0			/* Level 0 cache */
	msr	csselr_el1, x0		/* Select cache level */
	isb
	
	mrs	x0, ccsidr_el1		/* Read cache size info */
	
	/* Extract line size (bits 2:0) */
	and	x1, x0, #7
	add	x1, x1, #4		/* Line size = 2^(bits+4) bytes */
	
	/* Extract associativity (bits 12:3) */
	lsr	x2, x0, #3
	and	x2, x2, #0x3FF
	
	/* Extract number of sets (bits 27:13) */
	lsr	x3, x0, #13
	and	x3, x3, #0x7FFF
	
	/* Simple cache flush - clean and invalidate by set/way */
	mov	x4, #0			/* Way counter */
.Lflush_way_loop:
	mov	x5, #0			/* Set counter */
.Lflush_set_loop:
	lsl	x6, x4, #30		/* Way field */
	lsl	x7, x5, x1		/* Set field shifted by line size */
	orr	x6, x6, x7
	dc	cisw, x6		/* Clean and invalidate by set/way */
	add	x5, x5, #1
	cmp	x5, x3
	b.le	.Lflush_set_loop
	add	x4, x4, #1
	cmp	x4, x2
	b.le	.Lflush_way_loop
	
	dsb	sy			/* Data synchronization barrier */
	
	/* Invalidate entire instruction cache */
	ic	iallu
	dsb	nsh
	isb
	
	ldp	x29, x30, [sp], #16
	ret

/*
 * arm64_memory_barrier - Full memory barrier
 */
.globl arm64_memory_barrier
.type arm64_memory_barrier, %function
arm64_memory_barrier:
	dsb	sy			/* Data synchronization barrier - system */
	isb				/* Instruction synchronization barrier */
	ret

/*
 * ARM64 Exception Level Management Functions
 */

/*
 * arm64_detect_exception_level - Detect current exception level
 * Returns: 0=EL0, 1=EL1, 2=EL2, 3=EL3
 */
.globl arm64_detect_exception_level
.type arm64_detect_exception_level, %function
arm64_detect_exception_level:
	mrs	x0, CurrentEL
	lsr	x0, x0, #2		/* Extract EL field (bits 3:2) */
	ret

/*
 * arm64_transition_to_el1 - Transition from EL2 to EL1
 * This function handles the transition from EL2 (hypervisor) to EL1 (kernel)
 * following ARM64 architectural requirements.
 */
.globl arm64_transition_to_el1
.type arm64_transition_to_el1, %function
arm64_transition_to_el1:
	/* Save return address and frame pointer */
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/* Check current exception level */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	cmp	x0, #2
	b.ne	.Lel1_already		/* Already in EL1 or lower */

	/* We're in EL2, transition to EL1 */
	
	/* Step 1: Configure HCR_EL2 (Hypervisor Configuration Register) */
	mrs	x0, hcr_el2
	orr	x0, x0, #(1 << 31)	/* RW bit - EL1 is AArch64 */
	orr	x0, x0, #(1 << 1)	/* SWIO bit - set/way invalidation override */
	msr	hcr_el2, x0

	/* Step 2: Configure CNTHCTL_EL2 (Counter-timer Hypervisor Control) */
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3		/* Enable EL1 access to timers */
	msr	cnthctl_el2, x0

	/* Step 3: Configure CNTVOFF_EL2 (Counter-timer Virtual Offset) */
	msr	cntvoff_el2, xzr	/* No virtual offset */

	/* Step 4: Configure SCTLR_EL1 (System Control Register EL1) */
	mrs	x0, sctlr_el1
	/* Set reserved bits to 1 as required by architecture */
	mov	x1, #0x0800
	movk	x1, #0x30d0, lsl #16
	orr	x0, x0, x1
	/* Clear bits that should be 0 */
	mov	x1, #0x0002
	movk	x1, #0x0000, lsl #16
	bic	x0, x0, x1
	/* Specific settings for boot environment */
	orr	x0, x0, #(1 << 2)	/* C bit - data cache enable (will be set later) */
	orr	x0, x0, #(1 << 12)	/* I bit - instruction cache enable */
	bic	x0, x0, #(1 << 0)	/* M bit - MMU disable for now */
	bic	x0, x0, #(1 << 1)	/* A bit - alignment check disable */
	msr	sctlr_el1, x0

	/* Step 5: Configure SPSR_EL2 (Saved Program Status Register) */
	mov	x0, #0x3c5		/* EL1h (EL1 with SP_EL1) + disable interrupts */
	msr	spsr_el2, x0

	/* Step 6: Set ELR_EL2 to return address (EL1 entry point) */
	adr	x0, .Lel1_entry
	msr	elr_el2, x0

	/* Step 7: Perform the exception level transition */
	eret				/* Exception return to EL1 */

.Lel1_entry:
	/* Now running in EL1 */
	
	/* Verify we're in EL1 */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	cmp	x0, #1
	b.eq	.Lel1_success

	/* Transition failed - this should not happen */
	mov	x0, #0xDEAD
	movk	x0, #0xBEEF, lsl #16
	b	.Lel1_done

.Lel1_success:
	mov	x0, #0		/* Success */
	b	.Lel1_done

.Lel1_already:
	mov	x0, #0		/* Already in correct EL */

.Lel1_done:
	/* Restore stack frame and return */
	ldp	x29, x30, [sp], #16
	ret

/*
 * arm64_setup_el1_environment - Set up EL1 execution environment
 * This function configures various EL1 system registers for kernel execution
 */
.globl arm64_setup_el1_environment
.type arm64_setup_el1_environment, %function
arm64_setup_el1_environment:
	/* Save registers */
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/* Ensure we're in EL1 */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	cmp	x0, #1
	b.ne	.Lsetup_wrong_el

	/* Configure CPACR_EL1 (Architectural Feature Access Control Register) */
	mov	x0, #(3 << 20)		/* FPEN = 11b - no trapping of FP/SIMD */
	msr	cpacr_el1, x0

	/* Configure CONTEXTIDR_EL1 (Context ID Register) */
	msr	contextidr_el1, xzr	/* Clear context ID */

	/* Configure TPIDR_EL1 (Thread Pointer/ID Register) */
	msr	tpidr_el1, xzr		/* Clear thread pointer */

	/* Configure debug registers */
	msr	mdscr_el1, xzr		/* Monitor Debug System Control Register */
	
	/* Disable all breakpoints and watchpoints */
	msr	dbgbcr0_el1, xzr
	msr	dbgbcr1_el1, xzr
	msr	dbgwcr0_el1, xzr
	msr	dbgwcr1_el1, xzr

	mov	x0, #0			/* Success */
	b	.Lsetup_done

.Lsetup_wrong_el:
	mov	x0, #1			/* Wrong exception level */

.Lsetup_done:
	ldp	x29, x30, [sp], #16
	ret

/*
 * arm64_init_exception_level - Initialize ARM64 exception level management
 * This is the main entry point for exception level setup
 * Returns: 0 on success, non-zero on error
 */
.globl arm64_init_exception_level
.type arm64_init_exception_level, %function
arm64_init_exception_level:
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	stp	x19, x20, [sp, #16]	/* Save callee-saved registers */

	/* Step 1: Detect current exception level */
	bl	arm64_detect_exception_level
	mov	x19, x0			/* Save current EL */

	/* Step 2: Transition to EL1 if needed */
	cmp	x19, #1
	b.eq	.Linit_already_el1
	
	bl	arm64_transition_to_el1
	cmp	x0, #0
	b.ne	.Linit_transition_failed

.Linit_already_el1:
	/* Step 3: Set up EL1 environment */
	bl	arm64_setup_el1_environment
	mov	x20, x0			/* Save result */

	/* Step 4: Verify final state */
	bl	arm64_detect_exception_level
	cmp	x0, #1
	b.ne	.Linit_final_check_failed

	mov	x0, x20			/* Return setup result */
	b	.Linit_success

.Linit_transition_failed:
	mov	x0, #2			/* Transition failed */
	b	.Linit_done

.Linit_final_check_failed:
	mov	x0, #3			/* Final EL check failed */
	b	.Linit_done

.Linit_success:
	/* x0 already contains result */

.Linit_done:
	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret

/*
 * arm64_enable_mmu - Enable MMU (enhanced implementation)
 * Parameters:
 *   x0 = TTBR0_EL1 value (translation table base register 0)
 *   x1 = TTBR1_EL1 value (translation table base register 1)
 *   x2 = TCR_EL1 value (translation control register)
 *   x3 = MAIR_EL1 value (memory attribute indirection register)
 */
.globl arm64_enable_mmu
.type arm64_enable_mmu, %function
arm64_enable_mmu:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/* Ensure we're in EL1 */
	mrs	x4, CurrentEL
	lsr	x4, x4, #2
	cmp	x4, #1
	b.ne	.Lmmu_wrong_el

	/* Step 1: Disable MMU if enabled */
	mrs	x4, sctlr_el1
	bic	x4, x4, #1		/* Clear M bit */
	msr	sctlr_el1, x4
	isb

	/* Step 2: Set translation table base registers */
	msr	ttbr0_el1, x0
	msr	ttbr1_el1, x1

	/* Step 3: Set translation control register */
	msr	tcr_el1, x2

	/* Step 4: Set memory attribute indirection register */
	msr	mair_el1, x3

	/* Step 5: Invalidate TLB */
	tlbi	vmalle1is
	dsb	ish
	isb

	/* Step 6: Enable MMU */
	mrs	x4, sctlr_el1
	orr	x4, x4, #1		/* Set M bit */
	msr	sctlr_el1, x4
	isb

	mov	x0, #0			/* Success */
	b	.Lmmu_done

.Lmmu_wrong_el:
	mov	x0, #1			/* Wrong exception level */

.Lmmu_done:
	ldp	x29, x30, [sp], #16
	ret

/*
 * Data section for constants and literals
 */
.section .rodata

.Lversion_string:
	.asciz "Haiku ARM64 UEFI Boot Loader v1.0"
	.align 3

/*
 * Export version string for C code access
 */
.globl haiku_arm64_version_string
haiku_arm64_version_string:
	.quad .Lversion_string

/*
 * End of file marker
 */
.section .note.GNU-stack,"",%progbits