/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Generic Interrupt Controller (GIC) Header
 * 
 * This header provides the interface for the ARM64 GIC driver, including
 * comprehensive Inter-Processor Interrupt (IPI) support for SMP systems.
 */

#ifndef _KERNEL_ARCH_ARM64_ARCH_GIC_H
#define _KERNEL_ARCH_ARM64_ARCH_GIC_H

#include <SupportDefs.h>

// IPI Types
#define IPI_RESCHEDULE          0    // Request reschedule
#define IPI_CALL_FUNCTION       1    // Execute function on target CPU
#define IPI_CALL_FUNCTION_SYNC  2    // Execute function with synchronization
#define IPI_TLB_FLUSH           3    // TLB flush request
#define IPI_CACHE_FLUSH         4    // Cache flush request
#define IPI_TIMER_SYNC          5    // Timer synchronization
#define IPI_SHUTDOWN            6    // CPU shutdown request
#define IPI_DEBUG_BREAK         7    // Debug break request

// IPI handler function type
typedef void (*ipi_handler_func)(uint32 cpu, void* data);

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GIC Initialization and Management
 */

// Initialize GIC with memory-mapped register bases
status_t gic_init(addr_t distributor_base, addr_t cpu_interface_base, addr_t redistributor_base);

// Initialize GIC for secondary CPU
status_t gic_init_secondary_cpu(uint32 cpu);

// Cleanup GIC driver
void gic_cleanup(void);

/*
 * Basic Interrupt Management
 */

// Enable/disable interrupts
status_t gic_enable_interrupt(uint32 irq);
status_t gic_disable_interrupt(uint32 irq);

// Set interrupt priority (0 = highest, 255 = lowest)
status_t gic_set_interrupt_priority(uint32 irq, uint8 priority);

// Set interrupt target CPU(s)
status_t gic_set_interrupt_target(uint32 irq, uint32 cpu_mask);

// Acknowledge and end interrupts
int32 gic_acknowledge_interrupt(void);
status_t gic_end_interrupt(uint32 irq);

/*
 * Inter-Processor Interrupt (IPI) Management
 */

// Register/unregister IPI handlers
status_t gic_register_ipi_handler(uint32 ipi_type, ipi_handler_func handler, void* data);
status_t gic_unregister_ipi_handler(uint32 ipi_type);

// Send IPIs to specific targets
status_t gic_send_ipi(uint32 target_cpu, uint32 ipi_type);
status_t gic_broadcast_ipi(uint32 ipi_type);
status_t gic_send_ipi_mask(uint32 cpu_mask, uint32 ipi_type);

// Handle incoming IPI (called from interrupt handler)
void gic_handle_ipi(uint32 cpu, uint32 ipi_id);

/*
 * Cross-CPU Function Call Support
 */

// Execute function on specific CPUs
status_t gic_call_function_on_cpus(uint32 cpu_mask, void (*function)(void*), void* data, bool wait);
status_t gic_call_function_on_all_cpus(void (*function)(void*), void* data, bool wait);
status_t gic_call_function_on_cpu(uint32 target_cpu, void (*function)(void*), void* data, bool wait);

/*
 * Common SMP Operations
 */

// Request reschedule on CPUs
status_t gic_request_reschedule(uint32 cpu_mask);
status_t gic_request_reschedule_all(void);

// Request TLB flush on CPUs
status_t gic_request_tlb_flush(uint32 cpu_mask, bool wait);

// Request cache flush on CPUs
status_t gic_request_cache_flush(uint32 cpu_mask, bool wait);

/*
 * IPI Status and Debugging
 */

// Check for pending IPIs
bool gic_has_pending_ipi(uint32 cpu);
uint32 gic_get_pending_ipi_mask(uint32 cpu);
void gic_clear_pending_ipis(uint32 cpu);

/*
 * GIC Information and Debugging
 */

// Get GIC information
uint32 gic_get_version(void);
uint32 gic_get_max_interrupts(void);
bool gic_is_initialized(void);

// Debug functions
void gic_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_ARM64_ARCH_GIC_H */