/*
 * Copyright 2019-2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *   Alexander von Gluck IV <kallisti5@unixzen.com>
 */

#include <arch_cpu_defs.h>
#include <arch_dtb.h>
#include <arch_smp.h>
#include <boot/platform.h>
#include <boot/stage2.h>

extern "C" {
#include <libfdt.h>
}

#include "dtb.h"

/* The potential interrupt controller would be present in the dts as:
 * compatible = "arm,gic-v3";
 */
const struct supported_interrupt_controllers {
	const char*	dtb_compat;
	const char*	kind;
} kSupportedInterruptControllers[] = {
	{ "arm,cortex-a9-gic", INTC_KIND_GICV1 },
	{ "arm,cortex-a15-gic", INTC_KIND_GICV2 },
	{ "arm,gic-v3", INTC_KIND_GICV3 },
	{ "arm,gic-400", INTC_KIND_GICV2 },
	{ "ti,omap3-intc", INTC_KIND_OMAP3 },
	{ "marvell,pxa-intc", INTC_KIND_PXA },
};


static void
parse_psci_node(const void* fdt, int node)
{
	// Parse PSCI method from /psci node
	int methodLen;
	const char* method = (const char*)fdt_getprop(fdt, node, "method", &methodLen);
	
	if (method != NULL) {
		if (strcmp(method, "smc") == 0) {
			gKernelArgs.arch_args.psci_method = 1; // SMC
			dprintf("PSCI: method=SMC\n");
		} else if (strcmp(method, "hvc") == 0) {
			gKernelArgs.arch_args.psci_method = 2; // HVC
			dprintf("PSCI: method=HVC\n");
		} else {
			dprintf("PSCI: WARNING: unknown method '%s', defaulting to SMC\n", method);
			gKernelArgs.arch_args.psci_method = 1; // Default to SMC
		}
	} else {
		// No method specified, default to SMC
		gKernelArgs.arch_args.psci_method = 1;
		dprintf("PSCI: no method specified, defaulting to SMC\n");
	}
}


// BCM2712 (Raspberry Pi 5) detection
static bool sRaspberryPi5Detected = false;

void
arch_handle_fdt(const void* fdt, int node)
{
	const char* deviceType = (const char*)fdt_getprop(fdt, node,
		"device_type", NULL);

	if (deviceType != NULL) {
		if (strcmp(deviceType, "cpu") == 0) {
			platform_cpu_info* info = NULL;
			arch_smp_register_cpu(&info);
			if (info == NULL)
				return;

			// Read CPU "reg" property which contains MPIDR
			int regLen;
			const uint32* reg = (const uint32*)fdt_getprop(fdt, node, "reg", &regLen);
			if (reg != NULL && regLen >= 4) {
				// FDT stores MPIDR in "reg" property
				// Can be 32-bit or 64-bit depending on #address-cells
				if (regLen == 4) {
					// 32-bit MPIDR
					info->id = fdt32_to_cpu(*reg);
				} else if (regLen >= 8) {
					// 64-bit MPIDR
					uint64 mpidr = ((uint64)fdt32_to_cpu(reg[0]) << 32) | fdt32_to_cpu(reg[1]);
					info->id = mpidr;
				}

				// Store MPIDR in kernel_args
				if (gKernelArgs.num_cpus < SMP_MAX_CPUS) {
					gKernelArgs.arch_args.cpu_mpidr[gKernelArgs.num_cpus] = info->id;
				}

				dprintf("cpu: id=0x%lx (MPIDR=0x%lx)\n", info->id, info->id);
			} else {
				dprintf("cpu: WARNING: no 'reg' property found\n");
			}
		}
	}

	int compatibleLen;
	const char* compatible = (const char*)fdt_getprop(fdt, node,
		"compatible", &compatibleLen);

	if (compatible == NULL)
		return;

	// Check for PSCI node
	if (dtb_has_fdt_string(compatible, compatibleLen, "arm,psci") ||
		dtb_has_fdt_string(compatible, compatibleLen, "arm,psci-0.2") ||
		dtb_has_fdt_string(compatible, compatibleLen, "arm,psci-1.0")) {
		parse_psci_node(fdt, node);
		return;
	}

	// Check for Raspberry Pi 5
	if (dtb_has_fdt_string(compatible, compatibleLen, "raspberrypi,5-model-b") ||
		dtb_has_fdt_string(compatible, compatibleLen, "raspberrypi,5-compute-module")) {
		sRaspberryPi5Detected = true;
		dprintf("Raspberry Pi 5 detected!\n");
	}

	// Check for BCM2712 SoC
	if (dtb_has_fdt_string(compatible, compatibleLen, "brcm,bcm2712")) {
		dprintf("BCM2712 SoC detected\n");
	}

	intc_info &interrupt_controller = gKernelArgs.arch_args.interrupt_controller;
	if (interrupt_controller.kind[0] == 0) {
		for (uint32 i = 0; i < B_COUNT_OF(kSupportedInterruptControllers); i++) {
			if (dtb_has_fdt_string(compatible, compatibleLen,
				kSupportedInterruptControllers[i].dtb_compat)) {

				memcpy(interrupt_controller.kind, kSupportedInterruptControllers[i].kind,
					sizeof(interrupt_controller.kind));

				dtb_get_reg(fdt, node, 0, interrupt_controller.regs1);
				dtb_get_reg(fdt, node, 1, interrupt_controller.regs2);

				dprintf("Found interrupt controller: %s\n", interrupt_controller.kind);
				
				// Special handling for BCM2712/RPi5
				if (sRaspberryPi5Detected) {
					dprintf("  BCM2712 GIC-400 at 0x%016lx\n", interrupt_controller.regs1.start);
					dprintf("  CPU Interface at 0x%016lx\n", interrupt_controller.regs2.start);
				}
			}
		}
	}
}


void
arch_dtb_set_kernel_args(void)
{
	intc_info &interrupt_controller = gKernelArgs.arch_args.interrupt_controller;
	dprintf("Chosen interrupt controller:\n");
	if (interrupt_controller.kind[0] == 0) {
		dprintf("kind: None!\n");
	} else {
		dprintf("  kind: %s\n", interrupt_controller.kind);
		dprintf("  regs: %#" B_PRIx64 ", %#" B_PRIx64 "\n",
			interrupt_controller.regs1.start,
			interrupt_controller.regs1.size);
		dprintf("        %#" B_PRIx64 ", %#" B_PRIx64 "\n",
			interrupt_controller.regs2.start,
			interrupt_controller.regs2.size);
	}

	dprintf("Registered CPUs: %lu\n", gKernelArgs.num_cpus);
	for (uint32 i = 0; i < gKernelArgs.num_cpus; i++) {
		dprintf("  CPU %lu: MPIDR=0x%lx\n", i, gKernelArgs.arch_args.cpu_mpidr[i]);
	}
}
