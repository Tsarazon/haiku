/*
 * Copyright 2024-2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * BCM2712 (Raspberry Pi 5) Hardware Initialization
 * 
 * This module provides hardware detection and initialization for the
 * Broadcom BCM2712 SoC found in Raspberry Pi 5.
 *
 * Authors:
 *   Haiku ARM64 Development Team
 */

#include <KernelExport.h>
#include <kernel.h>
#include <boot/kernel_args.h>

#include <arch/arm64/arch_bcm2712.h>
#include <arch/arm64/arch_uart_pl011.h>
#include <arch/generic/debug_uart.h>

#include <string.h>

//#define TRACE_BCM2712
#ifdef TRACE_BCM2712
#	define TRACE(x...) dprintf("BCM2712: " x)
#else
#	define TRACE(x...) do {} while (0)
#endif

// BCM2712 Detection State
static bool sBCM2712Detected = false;
static bool sRaspberryPi5 = false;

// UART state
static DebugUART* sDebugUART = NULL;


/*
 * Detect BCM2712 hardware from device tree compatible strings
 */
static bool
bcm2712_detect_hardware(kernel_args* args)
{
	// Check if FDT is available
	if (args->arch_args.fdt.Get() == NULL) {
		TRACE("No device tree available\n");
		return false;
	}

	// For now, we'll use simple detection based on interrupt controller
	// In real implementation, this should parse FDT compatible strings
	
	// BCM2712 uses GIC-400 (GICv2) at specific address
	if (strcmp(args->arch_args.interrupt_controller.kind, INTC_KIND_GICV2) == 0) {
		phys_addr_t gicd_base = args->arch_args.interrupt_controller.regs1.start;
		
		// BCM2712 GIC distributor is at 0x107FFF9000
		if (gicd_base == BCM2712_GICD_BASE) {
			TRACE("Detected GIC-400 at BCM2712 address\n");
			sBCM2712Detected = true;
			sRaspberryPi5 = true;
			return true;
		}
	}
	
	return false;
}


/*
 * Initialize BCM2712 UART for early console output
 */
static status_t
bcm2712_init_uart(void)
{
	TRACE("Initializing BCM2712 UART0 (PL011)\n");
	
	// Create PL011 UART instance for BCM2712 UART0
	// Base address: 0x107D001000, Clock: 48MHz
	sDebugUART = arch_get_uart_pl011(BCM2712_UART0_BASE, BCM2712_UART_CLOCK);
	
	if (sDebugUART == NULL) {
		dprintf("BCM2712: Failed to create UART instance\n");
		return B_NO_MEMORY;
	}
	
	// Initialize UART at 115200 baud
	sDebugUART->InitPort(BCM2712_UART_BAUD);
	sDebugUART->Enable();
	
	TRACE("UART0 initialized successfully\n");
	
	return B_OK;
}


/*
 * Initialize BCM2712-specific hardware
 */
status_t
bcm2712_init(kernel_args* args)
{
	TRACE("Checking for BCM2712 hardware\n");
	
	// Detect BCM2712 hardware
	if (!bcm2712_detect_hardware(args)) {
		TRACE("BCM2712 not detected\n");
		return B_ERROR;
	}
	
	dprintf("BCM2712: Raspberry Pi 5 detected!\n");
	dprintf("BCM2712: Cortex-A76 quad-core @ 2.4GHz\n");
	dprintf("BCM2712: GIC-400 at 0x%016lx\n", BCM2712_GICD_BASE);
	dprintf("BCM2712: System Timer at 0x%016lx (54MHz)\n", BCM2712_SYSTIMER_BASE);
	
	// Initialize UART for debug output
	status_t status = bcm2712_init_uart();
	if (status != B_OK) {
		dprintf("BCM2712: Warning: Failed to initialize UART: %s\n", 
			strerror(status));
		// Non-fatal error, continue
	}
	
	return B_OK;
}


/*
 * Check if BCM2712 was detected
 */
bool
bcm2712_is_detected(void)
{
	return sBCM2712Detected;
}


/*
 * Check if this is a Raspberry Pi 5
 */
bool
bcm2712_is_raspberry_pi5(void)
{
	return sRaspberryPi5;
}


/*
 * Get BCM2712 UART instance
 */
DebugUART*
bcm2712_get_debug_uart(void)
{
	return sDebugUART;
}
