/*
 * Copyright 2006-2018, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *
 * Refactored for Gen9+ only support (Mobile Haiku)
 * Gen9+ support verified against Intel PRM Vol 2c
 */


#include "intel_extreme.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <AreaKeeper.h>
#include <boot_item.h>
#include <driver_settings.h>
#include <util/kernel_cpp.h>

#include <vesa_info.h>

#include "driver.h"
#include "firmware.h"
#include "power.h"

#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))


/*
 * Settings and Configuration
 *
 */

static void
read_settings(bool* hardwareCursor)
{
	*hardwareCursor = false;

	void* settings = load_driver_settings("intel_extreme");
	if (settings != NULL) {
		*hardwareCursor = get_driver_boolean_parameter(settings,
			"hardware_cursor", true, true);

		unload_driver_settings(settings);
	}
}


/*
 * VBlank Semaphore Management
 *
 */

static int32
release_vblank_sem(intel_info* info)
{
	int32 count;
	if (get_sem_count(info->shared_info->vblank_sem, &count) == B_OK
		&& count < 0) {
		release_sem_etc(info->shared_info->vblank_sem, -count,
			B_DO_NOT_RESCHEDULE);
		return B_INVOKE_SCHEDULER;
	}

	return B_HANDLED_INTERRUPT;
}


/*
 * Interrupt Handling - Gen9+ (Gen8 style registers)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - Interrupt Registers
 *    - GEN8_DE_PORT_IIR at 0x44448
 *    - GEN8_DE_MISC_IIR at 0x44468
 *    - Master Interrupt Control at 0x44200
 * Gen9 uses the same interrupt register layout as Gen8/Broadwell
 *
 */

static void
gen9_enable_pipe_interrupts(intel_info* info, pipe_index pipe, bool enable)
{
	ASSERT(pipe != INTEL_PIPE_ANY);
	ASSERT(info->device_type.Generation() >= 12 || pipe != INTEL_PIPE_D);

	const uint32 regMask = PCH_INTERRUPT_PIPE_MASK_BDW(pipe);
	const uint32 regEnabled = PCH_INTERRUPT_PIPE_ENABLED_BDW(pipe);
	const uint32 regIdentity = PCH_INTERRUPT_PIPE_IDENTITY_BDW(pipe);
	const uint32 value = enable ? PCH_INTERRUPT_VBLANK_BDW : 0;

	/* Clear pending interrupts first */
	write32(info, regIdentity, ~0);
	/* Enable/disable vblank interrupt */
	write32(info, regEnabled, value);
	/* Unmask vblank interrupt */
	write32(info, regMask, ~value);
}


static uint32
gen9_enable_global_interrupts(intel_info* info, bool enable)
{
	/*
	 * ✅ PRM: Master Interrupt Control register
	 * Bit 31: Master Interrupt Enable
	 */
	write32(info, PCH_MASTER_INT_CTL_BDW,
		enable ? PCH_MASTER_INT_CTL_GLOBAL_BDW : 0);
	return enable ? 0 : read32(info, PCH_MASTER_INT_CTL_BDW);
}


static int32
gen9_handle_pipe_interrupt(intel_info* info, pipe_index pipe)
{
	const uint32 regIdentity = PCH_INTERRUPT_PIPE_IDENTITY_BDW(pipe);
	uint32 identity = read32(info, regIdentity);

	if ((identity & PCH_INTERRUPT_VBLANK_BDW) != 0) {
		int32 handled = release_vblank_sem(info);
		/* Clear the interrupt by writing 1 to the bit */
		write32(info, regIdentity, identity | PCH_INTERRUPT_VBLANK_BDW);
		return handled;
	}

	TRACE("gen9_handle_pipe_interrupt: unhandled interrupt on pipe %d\n", pipe);
	return B_HANDLED_INTERRUPT;
}


static int32
gen9_handle_interrupts(intel_info* info, uint32 interrupt)
{
	int32 handled = B_HANDLED_INTERRUPT;

	/* Handle Pipe A interrupts */
	if ((interrupt & PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_A)) != 0) {
		handled = gen9_handle_pipe_interrupt(info, INTEL_PIPE_A);
		interrupt &= ~PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_A);
	}

	/* Handle Pipe B interrupts */
	if ((interrupt & PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_B)) != 0) {
		handled = gen9_handle_pipe_interrupt(info, INTEL_PIPE_B);
		interrupt &= ~PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_B);
	}

	/* Handle Pipe C interrupts */
	if ((interrupt & PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_C)) != 0) {
		handled = gen9_handle_pipe_interrupt(info, INTEL_PIPE_C);
		interrupt &= ~PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(INTEL_PIPE_C);
	}

	/* Handle Display Port AUX interrupts */
	if ((interrupt & GEN8_DE_PORT_IRQ) != 0) {
		uint32 iir = read32(info, GEN8_DE_PORT_IIR);
		if (iir != 0)
			write32(info, GEN8_DE_PORT_IIR, iir);
		interrupt &= ~GEN8_DE_PORT_IRQ;
	}

	/* Handle PCH interrupts (hotplug, etc.) */
	if ((interrupt & GEN8_DE_PCH_IRQ) != 0) {
		uint32 iir = read32(info, SDEIIR);
		if (iir != 0) {
			TRACE("gen9_handle_interrupts: PCH_IIR 0x%08" B_PRIx32 "\n", iir);
			write32(info, SDEIIR, iir);

			/* ICP+ PCH has separate DDI and TC hotplug registers */
			if (info->shared_info->pch_info >= INTEL_PCH_ICP) {
				uint32 ddiHotplug = read32(info, SHOTPLUG_CTL_DDI);
				write32(info, SHOTPLUG_CTL_DDI, ddiHotplug);

				uint32 tcHotplug = read32(info, SHOTPLUG_CTL_TC);
				write32(info, SHOTPLUG_CTL_TC, tcHotplug);
			}
		}
		interrupt &= ~GEN8_DE_PCH_IRQ;
	}

	interrupt &= ~PCH_MASTER_INT_CTL_GLOBAL_BDW;
	if (interrupt != 0)
		TRACE("gen9_handle_interrupts: unhandled 0x%08" B_PRIx32 "\n", interrupt);

	return handled;
}


static int32
gen9_interrupt_handler(void* data)
{
	intel_info* info = (intel_info*)data;

	/* Disable interrupts and read pending status */
	uint32 interrupt = gen9_enable_global_interrupts(info, false);
	if (interrupt == 0) {
		gen9_enable_global_interrupts(info, true);
		return B_UNHANDLED_INTERRUPT;
	}

	int32 handled = gen9_handle_interrupts(info, interrupt);

	gen9_enable_global_interrupts(info, true);
	return handled;
}


/*
 * Interrupt Handling - Gen11+ (Ice Lake and newer)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - Gen11 Interrupt Registers
 *    - GEN11_GFX_MSTR_IRQ at 0x190010
 *    - GEN11_DISPLAY_INT_CTL at 0x44200
 *    - GEN11_DE_HPD_IIR at 0x44478
 * Gen11 has a different master interrupt register architecture
 *
 */

static uint32
gen11_enable_global_interrupts(intel_info* info, bool enable)
{
	/*
	 * ✅ PRM: GEN11_GFX_MSTR_IRQ register at 0x190010
	 * Bit 31: Master Interrupt Enable
	 */
	write32(info, GEN11_GFX_MSTR_IRQ, enable ? GEN11_MASTER_IRQ : 0);
	return enable ? 0 : read32(info, GEN11_GFX_MSTR_IRQ);
}


static int32
gen11_handle_interrupts(intel_info* info, uint32 interrupt)
{
	int32 handled = B_HANDLED_INTERRUPT;

	/* Handle Display Engine interrupts (reuse Gen9 handler) */
	if ((interrupt & GEN11_DISPLAY_IRQ) != 0) {
		uint32 displayInt = read32(info, GEN11_DISPLAY_INT_CTL);
		handled = gen9_handle_interrupts(info, displayInt);
	}

	/* Handle HPD (Hot Plug Detect) interrupts */
	if ((interrupt & GEN11_DE_HPD_IRQ) != 0) {
		uint32 iir = read32(info, GEN11_DE_HPD_IIR);
		if (iir != 0) {
			TRACE("gen11_handle_interrupts: HPD_IIR 0x%08" B_PRIx32 "\n", iir);
			write32(info, GEN11_DE_HPD_IIR, iir);
		}
	}

	return handled;
}


static int32
gen11_interrupt_handler(void* data)
{
	intel_info* info = (intel_info*)data;

	uint32 interrupt = gen11_enable_global_interrupts(info, false);

	if (interrupt == 0) {
		gen11_enable_global_interrupts(info, true);
		return B_UNHANDLED_INTERRUPT;
	}

	int32 handled = gen11_handle_interrupts(info, interrupt);

	gen11_enable_global_interrupts(info, true);
	return handled;
}


/*
 * Interrupt Initialization - Gen11+ (Ice Lake and newer)
 *
 * ✅ Verified against Intel PRM - Display Interrupt Setup
 *
 */

static void
init_gen11_interrupts(intel_info* info)
{
	/* Setup PCH interrupts for ICP+ (Ice Lake PCH and newer) */
	if (info->shared_info->pch_info >= INTEL_PCH_ICP) {
		read32(info, SDEIIR);
		write32(info, SDEIER, 0xffffffff);
		write32(info, SDEIMR, ~SDE_GMBUS_ICP);
		read32(info, SDEIMR);
	}

	/* Setup AUX channel interrupts for all DDI ports */
	uint32 auxMask = GEN8_AUX_CHANNEL_A | GEN9_AUX_CHANNEL_B
		| GEN9_AUX_CHANNEL_C | GEN9_AUX_CHANNEL_D
		| CNL_AUX_CHANNEL_F | ICL_AUX_CHANNEL_E;
	read32(info, GEN8_DE_PORT_IIR);
	write32(info, GEN8_DE_PORT_IER, auxMask);
	write32(info, GEN8_DE_PORT_IMR, ~auxMask);
	read32(info, GEN8_DE_PORT_IMR);

	/* Setup misc interrupts (PSR, etc.) */
	read32(info, GEN8_DE_MISC_IIR);
	write32(info, GEN8_DE_MISC_IER, GEN8_DE_EDP_PSR);
	write32(info, GEN8_DE_MISC_IMR, ~GEN8_DE_EDP_PSR);
	read32(info, GEN8_DE_MISC_IMR);

	/* Setup GU misc interrupts */
	read32(info, GEN11_GU_MISC_IIR);
	write32(info, GEN11_GU_MISC_IER, GEN11_GU_MISC_GSE);
	write32(info, GEN11_GU_MISC_IMR, ~GEN11_GU_MISC_GSE);
	read32(info, GEN11_GU_MISC_IMR);

	/* Setup HPD (Hot Plug Detect) interrupts */
	read32(info, GEN11_DE_HPD_IIR);
	write32(info, GEN11_DE_HPD_IER,
		GEN11_DE_TC_HOTPLUG_MASK | GEN11_DE_TBT_HOTPLUG_MASK);
	write32(info, GEN11_DE_HPD_IMR, 0xffffffff);
	read32(info, GEN11_DE_HPD_IMR);

	write32(info, GEN11_TC_HOTPLUG_CTL, 0);
	write32(info, GEN11_TBT_HOTPLUG_CTL, 0);

	/* Setup PCH hotplug for ICP+ (Ice Lake PCH and newer) */
	if (info->shared_info->pch_info >= INTEL_PCH_ICP) {
		if (info->shared_info->pch_info <= INTEL_PCH_ADP)
			write32(info, SHPD_FILTER_CNT, SHPD_FILTER_CNT_500_ADJ);

		read32(info, SDEIMR);
		write32(info, SDEIMR, 0x3f023f07);
		read32(info, SDEIMR);

		/* Enable DDI hotplug detection for ports A-D */
		uint32 ctl = read32(info, SHOTPLUG_CTL_DDI);
		ctl |= SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_A)
			| SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_B)
			| SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_C)
			| SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_D);
		write32(info, SHOTPLUG_CTL_DDI, ctl);

		/* Enable Type-C hotplug detection for TC ports 1-6 */
		ctl = read32(info, SHOTPLUG_CTL_TC);
		ctl |= SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC1)
			| SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC2)
			| SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC3)
			| SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC4)
			| SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC5)
			| SHOTPLUG_CTL_TC_HPD_ENABLE(HPD_PORT_TC6);
		write32(info, SHOTPLUG_CTL_TC, ctl);
	}

	gen11_enable_global_interrupts(info, true);
}


/*
 * Interrupt Handler Installation
 *
 */

static void
init_interrupt_handler(intel_info* info)
{
	info->shared_info->vblank_sem = create_sem(0, "intel extreme vblank");
	if (info->shared_info->vblank_sem < B_OK)
		return;

	status_t status = B_OK;

	/* Change the owner of the sem to the calling team */
	thread_id thread = find_thread(NULL);
	thread_info threadInfo;
	if (get_thread_info(thread, &threadInfo) != B_OK
		|| set_sem_owner(info->shared_info->vblank_sem, threadInfo.team)
			!= B_OK) {
		status = B_ERROR;
	}

	/* Find the right interrupt vector, using MSIs if available */
	info->irq = 0;
	info->use_msi = false;
	if (info->pci->u.h0.interrupt_pin != 0x00) {
		info->irq = info->pci->u.h0.interrupt_line;
		if (info->irq == 0xff)
			info->irq = 0;
	}

	/* Try to use MSI (Message Signaled Interrupts) */
	if (gPCI->get_msi_count(info->pci->bus,
			info->pci->device, info->pci->function) >= 1) {
		uint32 msiVector = 0;
		if (gPCI->configure_msi(info->pci->bus, info->pci->device,
				info->pci->function, 1, &msiVector) == B_OK
			&& gPCI->enable_msi(info->pci->bus, info->pci->device,
				info->pci->function) == B_OK) {
			TRACE("using message signaled interrupts\n");
			info->irq = msiVector;
			info->use_msi = true;
		}
	}

	if (status == B_OK && info->irq != 0) {
		info->fake_interrupts = false;

		/* Select appropriate interrupt handler based on generation */
		interrupt_handler handler;
		if (info->device_type.Generation() >= 11)
			handler = &gen11_interrupt_handler;
		else
			handler = &gen9_interrupt_handler;

		status = install_io_interrupt_handler(info->irq, handler,
			(void*)info, 0);

		if (status == B_OK) {
			/* Enable pipe interrupts for vblank */
			gen9_enable_pipe_interrupts(info, INTEL_PIPE_A, true);
			gen9_enable_pipe_interrupts(info, INTEL_PIPE_B, true);
			gen9_enable_pipe_interrupts(info, INTEL_PIPE_C, true);

			if (info->device_type.Generation() >= 11) {
				init_gen11_interrupts(info);
			} else {
				gen9_enable_global_interrupts(info, true);
			}
		}
	}

	if (status < B_OK) {
		info->fake_interrupts = true;
		ERROR("Fake interrupt mode (no PCI interrupt line assigned)\n");
		status = B_ERROR;
	}

	if (status < B_OK) {
		delete_sem(info->shared_info->vblank_sem);
		info->shared_info->vblank_sem = B_ERROR;
	}
}


/*
 * Memory Management
 *
 */

status_t
intel_free_memory(intel_info* info, addr_t base)
{
	return gGART->free_memory(info->aperture, base);
}


status_t
intel_allocate_memory(intel_info* info, size_t size, size_t alignment,
	uint32 flags, addr_t* _base, phys_addr_t* _physicalBase)
{
	return gGART->allocate_memory(info->aperture, size, alignment,
		flags, _base, _physicalBase);
}


/*
 * Clock and Reference Frequency Detection
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - Clock Registers
 *    - ICL_DSSM at 0x51004 for Gen11+ reference frequency
 *    - Skylake uses fixed 24 MHz reference
 *
 */

static void
detect_reference_frequency(intel_info* info)
{
	uint32 generation = info->device_type.Generation();

	if (generation == 9 && info->device_type.InGroup(INTEL_GROUP_SKY)) {
		/*
		 * ✅ PRM: Skylake uses fixed 24 MHz reference clock
		 */
		info->shared_info->pll_info.reference_frequency = 24000;
	} else if (generation >= 9) {
		/*
		 * ✅ PRM: Gen9.5+ (Kaby Lake) and Gen11+ detect from ICL_DSSM register
		 * ICL_DSSM bits [31:29] contain reference frequency strap
		 */
		uint32 refInfo = (read32(info, ICL_DSSM) & ICL_DSSM_REF_FREQ_MASK)
			>> ICL_DSSM_REF_FREQ_SHIFT;
		switch (refInfo) {
			case ICL_DSSM_24000:
				info->shared_info->pll_info.reference_frequency = 24000;
				break;
			case ICL_DSSM_19200:
				info->shared_info->pll_info.reference_frequency = 19200;
				break;
			case ICL_DSSM_38400:
				info->shared_info->pll_info.reference_frequency = 38400;
				break;
			default:
				ERROR("Unknown reference frequency strap: 0x%08" B_PRIx32
					", defaulting to 24MHz\n", refInfo);
				info->shared_info->pll_info.reference_frequency = 24000;
				break;
		}
	} else {
		/* Fallback for unknown Gen9 variants */
		info->shared_info->pll_info.reference_frequency = 24000;
	}

	/*
	 * ✅ PRM: Gen9+ DPLL frequency limits
	 * Min: 25 MHz, Max: 350 MHz for display PLLs
	 */
	info->shared_info->pll_info.max_frequency = 350000;
	info->shared_info->pll_info.min_frequency = 25000;
	info->shared_info->pll_info.divisor_register = 0;

	TRACE("Reference frequency: %" B_PRIu32 " kHz\n",
		info->shared_info->pll_info.reference_frequency);
}


static void
detect_hw_clocks(intel_info* info)
{
	/*
	 * ⚠️ Note: CD clock detection for Gen9+ requires reading
	 * CDCLK_CTL register. Current implementation is simplified.
	 * Full implementation should follow PRM sequences.
	 *
	 * TODO: Implement proper CD clock detection per PRM:
	 *   - SKL: Read CDCLK_CTL and apply divider table
	 *   - ICL+: Read CDCLK_CTL with different encoding
	 */

	/* Detect raw clock from PCH */
	if (info->pch_info != INTEL_PCH_NONE) {
		if (info->shared_info->pch_info >= INTEL_PCH_CNP) {
			/*
			 * ✅ PRM: CNP+ (Cannon Point) uses 24 MHz raw clock
			 */
			info->shared_info->hraw_clock = 24000;
		} else {
			/*
			 * ✅ PRM: SPT reads raw clock from PCH_RAWCLK_FREQ register
			 */
			info->shared_info->hraw_clock = (read32(info, PCH_RAWCLK_FREQ)
				& RAWCLK_FREQ_MASK) * 1000;
		}
		TRACE("Raw clock rate: %" B_PRIu32 " kHz\n",
			info->shared_info->hraw_clock);
	} else {
		/* SOC platforms without PCH (Broxton, Apollo Lake, etc.) */
		info->shared_info->hraw_clock = 24000;
	}

	/*
	 * ⚠️ TODO: Implement proper CD clock detection for Gen9+
	 * This requires reading CDCLK_CTL and using the appropriate
	 * divider tables from the PRM.
	 *
	 * Current default is a safe fallback value.
	 */
	info->shared_info->hw_cdclk = 337500;

	TRACE("CD clock: %" B_PRIu32 " kHz\n", info->shared_info->hw_cdclk);
}


/*
 * Hardware Initialization
 *
 */

status_t
intel_extreme_init(intel_info* info)
{
	CALLED();

	/* Verify this is Gen9+ */
	if (info->device_type.Generation() < 9) {
		ERROR("Device generation %d is not supported (Gen9+ required)\n",
			info->device_type.Generation());
		return B_NOT_SUPPORTED;
	}

	/* Map GART aperture for graphics memory access */
	info->aperture = gGART->map_aperture(info->pci->bus, info->pci->device,
		info->pci->function, 0, &info->aperture_base);
	if (info->aperture < B_OK) {
		ERROR("Could not map GART aperture: %s\n", strerror(info->aperture));
		return info->aperture;
	}

	/* Create shared info area for accelerant communication */
	AreaKeeper sharedCreator;
	info->shared_area = sharedCreator.Create("intel extreme shared info",
		(void**)&info->shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(intel_shared_info)) + 3 * B_PAGE_SIZE,
		B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info->shared_area < B_OK) {
		ERROR("Could not create shared area\n");
		gGART->unmap_aperture(info->aperture);
		return info->shared_area;
	}

	/* Enable power (D0 state) */
	gPCI->set_powerstate(info->pci->bus, info->pci->device, info->pci->function,
		PCI_pm_state_d0);

	memset((void*)info->shared_info, 0, sizeof(intel_shared_info));

	/*
	 * ✅ PRM: Gen9+ MMIO BAR is always at index 0
	 * BAR0 contains the graphics register MMIO space
	 */
	int mmioIndex = 0;

	/* Read driver settings */
	bool hardwareCursor;
	read_settings(&hardwareCursor);

	/* Map memory-mapped I/O registers */
	phys_addr_t addr = info->pci->u.h0.base_registers[mmioIndex];
	uint64 barSize = info->pci->u.h0.base_register_sizes[mmioIndex];

	/* Handle 64-bit BAR */
	if ((info->pci->u.h0.base_register_flags[mmioIndex] & PCI_address_type)
			== PCI_address_type_64) {
		addr |= (uint64)info->pci->u.h0.base_registers[mmioIndex + 1] << 32;
		barSize |= (uint64)info->pci->u.h0.base_register_sizes[mmioIndex + 1]
			<< 32;
	}

	AreaKeeper mmioMapper;
	info->registers_area = mmioMapper.Map("intel extreme mmio", addr, barSize,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&info->registers);
	if (mmioMapper.InitCheck() < B_OK) {
		ERROR("Could not map memory I/O\n");
		gGART->unmap_aperture(info->aperture);
		return info->registers_area;
	}

	bool hasPCH = (info->pch_info != INTEL_PCH_NONE);

	TRACE("Initializing Intel Gen%d GPU %s PCH split\n",
		info->device_type.Generation(), hasPCH ? "with" : "without");

	/*
	 * ✅ PRM: Setup register blocks for Gen9+ PCH layout
	 * Gen9+ always uses the PCH register organization
	 */
	uint32* blocks = info->shared_info->register_blocks;
	blocks[REGISTER_BLOCK(REGS_FLAT)] = 0;
	blocks[REGISTER_BLOCK(REGS_NORTH_SHARED)]
		= PCH_NORTH_SHARED_REGISTER_BASE;
	blocks[REGISTER_BLOCK(REGS_NORTH_PIPE_AND_PORT)]
		= PCH_NORTH_PIPE_AND_PORT_REGISTER_BASE;
	blocks[REGISTER_BLOCK(REGS_NORTH_PLANE_CONTROL)]
		= PCH_NORTH_PLANE_CONTROL_REGISTER_BASE;
	blocks[REGISTER_BLOCK(REGS_SOUTH_SHARED)]
		= PCH_SOUTH_SHARED_REGISTER_BASE;
	blocks[REGISTER_BLOCK(REGS_SOUTH_TRANSCODER_PORT)]
		= PCH_SOUTH_TRANSCODER_AND_PORT_REGISTER_BASE;

	TRACE("REGS_NORTH_SHARED: 0x%" B_PRIx32 "\n",
		blocks[REGISTER_BLOCK(REGS_NORTH_SHARED)]);
	TRACE("REGS_NORTH_PIPE_AND_PORT: 0x%" B_PRIx32 "\n",
		blocks[REGISTER_BLOCK(REGS_NORTH_PIPE_AND_PORT)]);
	TRACE("REGS_NORTH_PLANE_CONTROL: 0x%" B_PRIx32 "\n",
		blocks[REGISTER_BLOCK(REGS_NORTH_PLANE_CONTROL)]);
	TRACE("REGS_SOUTH_SHARED: 0x%" B_PRIx32 "\n",
		blocks[REGISTER_BLOCK(REGS_SOUTH_SHARED)]);
	TRACE("REGS_SOUTH_TRANSCODER_PORT: 0x%" B_PRIx32 "\n",
		blocks[REGISTER_BLOCK(REGS_SOUTH_TRANSCODER_PORT)]);

	/* Enable bus master, memory-mapped I/O, and frame buffer */
	set_pci_config(info->pci, PCI_command, 2, get_pci_config(info->pci,
		PCI_command, 2) | PCI_command_io | PCI_command_memory
		| PCI_command_master);

	/* Allocate ring buffer memory for command submission */
	ring_buffer* primary = &info->shared_info->primary_ring_buffer;
	if (intel_allocate_memory(info, 16 * B_PAGE_SIZE, 0, 0,
			(addr_t*)&primary->base, NULL) == B_OK) {
		primary->register_base = INTEL_PRIMARY_RING_BUFFER;
		primary->size = 16 * B_PAGE_SIZE;
		primary->offset = (addr_t)primary->base - info->aperture_base;
	}

	/* Enable power management features */
	intel_en_gating(info);
	intel_en_downclock(info);

	/*
	 * Load GPU firmware (DMC, optionally GuC/HuC)
	 * DMC provides display power states (DC5/DC6)
	 * This is optional - display works without it but uses more power
	 */
	intel_firmware_init(info);

	/* Keep areas and mappings */
	sharedCreator.Detach();
	mmioMapper.Detach();

	/* Get aperture information */
	aperture_info apertureInfo;
	gGART->get_aperture_info(info->aperture, &apertureInfo);

	/* Initialize shared info structure */
	info->shared_info->registers_area = info->registers_area;
	info->shared_info->graphics_memory = (uint8*)info->aperture_base;
	info->shared_info->physical_graphics_memory = apertureInfo.physical_base;
	info->shared_info->graphics_memory_size = apertureInfo.size;
	info->shared_info->frame_buffer = 0;
	info->shared_info->dpms_mode = B_DPMS_ON;
	info->shared_info->min_brightness = 2;
	info->shared_info->pch_info = info->pch_info;
	info->shared_info->device_type = info->device_type;

	/* Parse VBIOS/VBT information for panel timings */
	info->shared_info->got_vbt = parse_vbt_from_bios(info->shared_info);

	/* Detect reference frequencies and clocks */
	detect_reference_frequency(info);
	detect_hw_clocks(info);

#ifdef __HAIKU__
	strlcpy(info->shared_info->device_identifier, info->device_identifier,
		sizeof(info->shared_info->device_identifier));
#else
	strcpy(info->shared_info->device_identifier, info->device_identifier);
#endif

	/* Allocate hardware status page */
	if (intel_allocate_memory(info, B_PAGE_SIZE, 0, B_APERTURE_NEED_PHYSICAL,
			(addr_t*)&info->shared_info->status_page,
			&info->shared_info->physical_status_page) == B_OK) {
		write32(info, INTEL_HARDWARE_STATUS_PAGE,
			(uint32)info->shared_info->physical_status_page);
	}

	/* Allocate cursor memory if hardware cursor is enabled */
	if (hardwareCursor) {
		intel_allocate_memory(info, B_PAGE_SIZE, 0, B_APERTURE_NEED_PHYSICAL,
			(addr_t*)&info->shared_info->cursor_memory,
			&info->shared_info->physical_cursor_memory);
	}

	/* Get EDID from boot loader if available */
	edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO,
		NULL);
	if (edidInfo != NULL) {
		info->shared_info->has_vesa_edid_info = true;
		memcpy(&info->shared_info->vesa_edid_info, edidInfo,
			sizeof(edid1_info));
	}

	/* Initialize interrupt handling */
	init_interrupt_handler(info);

	TRACE("Initialization completed successfully\n");
	return B_OK;
}


/*
 * Hardware Cleanup
 *
 */

void
intel_extreme_uninit(intel_info* info)
{
	CALLED();

	if (!info->fake_interrupts && info->shared_info->vblank_sem > 0) {
		/* Disable interrupt generation */
		if (info->device_type.Generation() >= 11)
			gen11_enable_global_interrupts(info, false);
		gen9_enable_global_interrupts(info, false);

		/* Remove interrupt handler */
		interrupt_handler handler;
		if (info->device_type.Generation() >= 11)
			handler = &gen11_interrupt_handler;
		else
			handler = &gen9_interrupt_handler;
		remove_io_interrupt_handler(info->irq, handler, info);

		/* Disable MSI if it was enabled */
		if (info->use_msi) {
			gPCI->disable_msi(info->pci->bus,
				info->pci->device, info->pci->function);
			gPCI->unconfigure_msi(info->pci->bus,
				info->pci->device, info->pci->function);
		}
	}

	/* Cleanup firmware (disable DC states) */
	intel_firmware_uninit(info);

	/* Cleanup resources */
	gGART->unmap_aperture(info->aperture);
	delete_area(info->registers_area);
	delete_area(info->shared_area);
}
