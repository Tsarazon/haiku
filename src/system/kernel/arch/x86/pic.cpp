/*
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*!	\file pic.cpp
	\brief Intel 8259A Programmable Interrupt Controller (PIC) driver
	
	This driver manages the legacy 8259A PIC chips in a cascaded configuration.
	Two PICs are connected in master-slave topology, providing 15 usable IRQ lines
	(IRQ 2 is used for cascading).
	
	References:
	- Intel 8259A Datasheet
	- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A
	- AMD64 Architecture Programmer's Manual, Volume 2: System Programming
*/

#include <arch/x86/pic.h>

#include <arch/cpu.h>
#include <arch/int.h>
#include <arch/x86/arch_int.h>
#include <interrupts.h>


//#define TRACE_PIC
#ifdef TRACE_PIC
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// PIC 8259A I/O Port Definitions
#define PIC_MASTER_CONTROL		0x20	// Master PIC command port
#define PIC_MASTER_MASK			0x21	// Master PIC data/mask port
#define PIC_SLAVE_CONTROL		0xa0	// Slave PIC command port
#define PIC_SLAVE_MASK			0xa1	// Slave PIC data/mask port

// Convenience aliases for initialization sequence
#define PIC_MASTER_INIT1		PIC_MASTER_CONTROL
#define PIC_MASTER_INIT2		PIC_MASTER_MASK
#define PIC_MASTER_INIT3		PIC_MASTER_MASK
#define PIC_MASTER_INIT4		PIC_MASTER_MASK
#define PIC_SLAVE_INIT1			PIC_SLAVE_CONTROL
#define PIC_SLAVE_INIT2			PIC_SLAVE_MASK
#define PIC_SLAVE_INIT3			PIC_SLAVE_MASK
#define PIC_SLAVE_INIT4			PIC_SLAVE_MASK

// Edge/Level Trigger Control Registers (ELCR)
// Note: Not available on original 8259A, but present on most modern chipsets
#define PIC_MASTER_TRIGGER_MODE	0x4d0	// Master ELCR
#define PIC_SLAVE_TRIGGER_MODE	0x4d1	// Slave ELCR

// Initialization Command Word 1 (ICW1)
#define PIC_INIT1				0x10
#define PIC_INIT1_SEND_INIT4	0x01	// ICW4 needed

// Initialization Command Word 3 (ICW3)
#define PIC_INIT3_IR2_IS_SLAVE	0x04	// Master: IRQ2 has slave attached
#define PIC_INIT3_SLAVE_ID2		0x02	// Slave: cascade identity (IRQ2)

// Initialization Command Word 4 (ICW4)
#define PIC_INIT4_x86_MODE		0x01	// 8086/8088 mode

// Operation Command Word 3 (OCW3) - for reading ISR/IRR
#define PIC_CONTROL3			0x08
#define PIC_CONTROL3_READ_ISR	0x03	// Read In-Service Register
#define PIC_CONTROL3_READ_IRR	0x02	// Read Interrupt Request Register

// End of Interrupt command
#define PIC_NON_SPECIFIC_EOI	0x20

// Interrupt configuration
#define PIC_SLAVE_INT_BASE		8		// Slave IRQs start at 8
#define PIC_NUM_INTS			15		// Last valid IRQ number (0-15 = 16 IRQs)
#define PIC_CASCADE_IRQ			2		// IRQ used for cascading

// Interrupt mask values
#define PIC_MASK_ALL			0xff	// Mask all interrupts (bits 0-7)
#define PIC_MASK_ALL_EXCEPT_SLAVE 0xfb	// Mask all except cascade (bit 2 = 0)
#define PIC_SPURIOUS_IRQ7		0x80	// ISR bit for IRQ7 (1 << 7)


// Global state
static uint16 sLevelTriggeredInterrupts = 0;
	// Bitmap: 1 = level triggered, 0 = edge triggered


// Internal helper functions

/*!	Read the 8-bit Interrupt Mask Register from specified PIC
	\param isMaster true for master PIC, false for slave PIC
	\return Current interrupt mask (1 = masked, 0 = enabled)
*/
static inline uint8
pic_read_mask(bool isMaster)
{
	return in8(isMaster ? PIC_MASTER_MASK : PIC_SLAVE_MASK);
}


/*!	Write the 8-bit Interrupt Mask Register to specified PIC
	\param isMaster true for master PIC, false for slave PIC
	\param mask Interrupt mask to write (1 = mask, 0 = enable)
*/
static inline void
pic_write_mask(bool isMaster, uint8 mask)
{
	out8(mask, isMaster ? PIC_MASTER_MASK : PIC_SLAVE_MASK);
}


/*!	Read the Edge/Level Control Register (ELCR) if available
	\param isMaster true for master PIC, false for slave PIC
	\return Current trigger mode configuration (1 = level, 0 = edge)
*/
static inline uint8
pic_read_trigger_mode(bool isMaster)
{
	return in8(isMaster ? PIC_MASTER_TRIGGER_MODE : PIC_SLAVE_TRIGGER_MODE);
}


/*!	Write the Edge/Level Control Register (ELCR) if available
	\param isMaster true for master PIC, false for slave PIC
	\param mode Trigger mode configuration (1 = level, 0 = edge)
*/
static inline void
pic_write_trigger_mode(bool isMaster, uint8 mode)
{
	out8(mode, isMaster ? PIC_MASTER_TRIGGER_MODE : PIC_SLAVE_TRIGGER_MODE);
}


// Public API implementation

/*!	Detects spurious interrupts on IRQ7 (and potentially IRQ15)
	
	Spurious interrupts occur when the PIC starts to signal an interrupt
	but the IRQ line is deasserted before the CPU acknowledges it.
	This commonly happens on IRQ7 (printer port) due to electrical noise.
	
	Detection method: Read the In-Service Register (ISR). If the corresponding
	bit is not set, the interrupt is spurious and should be ignored.
	
	\param num Normalized interrupt number (0-15)
	\return true if interrupt is spurious, false if legitimate
*/
static bool
pic_is_spurious_interrupt(int32 num)
{
	if (num != 7)
		return false;

	// Note: Detecting spurious interrupts on line 15 (slave's IRQ7) is more
	// complex and requires checking both PICs. Since spurious IRQ15 is
	// extremely rare, we currently don't handle it specially.

	// Read the In-Service Register (ISR) to check if IRQ7 is really active
	out8(PIC_CONTROL3 | PIC_CONTROL3_READ_ISR, PIC_MASTER_CONTROL);
	uint8 isr = in8(PIC_MASTER_CONTROL);
	
	// Restore normal operation (read IRR by default)
	out8(PIC_CONTROL3 | PIC_CONTROL3_READ_IRR, PIC_MASTER_CONTROL);

	// If bit 7 is not set, this is a spurious interrupt
	return (isr & PIC_SPURIOUS_IRQ7) == 0;
}


/*!	Determines if an interrupt is configured as level-triggered
	\param num Normalized interrupt number (0-15)
	\return true if level-triggered, false if edge-triggered or invalid
*/
static bool
pic_is_level_triggered_interrupt(int32 num)
{
	if (num < 0 || num > PIC_NUM_INTS)
		return false;

	return (sLevelTriggeredInterrupts & (1 << num)) != 0;
}


/*!	Sends End-Of-Interrupt (EOI) signal to PIC
	
	For edge-triggered interrupts, this clears the In-Service Register (ISR) bit.
	For level-triggered interrupts, the ISR bit remains set until the hardware
	deasserts the IRQ line.
	
	The non-specific EOI clears the highest priority ISR bit. For slave interrupts,
	both PICs must receive EOI (slave first, then master).
	
	\param num Normalized interrupt number (0-15)
	\return true if EOI was sent, false if invalid interrupt number
*/
static bool
pic_end_of_interrupt(int32 num)
{
	if (num < 0 || num > PIC_NUM_INTS)
		return false;

	// For slave PIC interrupts (8-15), send EOI to slave first
	if (num >= PIC_SLAVE_INT_BASE)
		out8(PIC_NON_SPECIFIC_EOI, PIC_SLAVE_CONTROL);

	// Always send EOI to master PIC (it handles cascading)
	out8(PIC_NON_SPECIFIC_EOI, PIC_MASTER_CONTROL);
	
	return true;
}


/*!	Enables (unmasks) an interrupt line
	\param num Normalized interrupt number (0-15)
*/
static void
pic_enable_io_interrupt(int32 num)
{
	if (num < 0 || num > PIC_NUM_INTS)
		return;

	TRACE(("pic_enable_io_interrupt: irq %ld\n", num));

	if (num < PIC_SLAVE_INT_BASE) {
		// Master PIC interrupt (IRQ 0-7)
		uint8 mask = pic_read_mask(true);
		pic_write_mask(true, mask & ~(1 << num));
	} else {
		// Slave PIC interrupt (IRQ 8-15)
		uint8 mask = pic_read_mask(false);
		pic_write_mask(false, mask & ~(1 << (num - PIC_SLAVE_INT_BASE)));
	}
}


/*!	Disables (masks) an interrupt line
	
	IRQ 2 (cascade line) is never disabled to maintain communication
	between master and slave PICs.
	
	\param num Normalized interrupt number (0-15)
*/
static void
pic_disable_io_interrupt(int32 num)
{
	// Never disable cascade line (IRQ 2)
	if (num < 0 || num > PIC_NUM_INTS || num == PIC_CASCADE_IRQ)
		return;

	TRACE(("pic_disable_io_interrupt: irq %ld\n", num));

	if (num < PIC_SLAVE_INT_BASE) {
		// Master PIC interrupt (IRQ 0-7)
		uint8 mask = pic_read_mask(true);
		pic_write_mask(true, mask | (1 << num));
	} else {
		// Slave PIC interrupt (IRQ 8-15)
		uint8 mask = pic_read_mask(false);
		pic_write_mask(false, mask | (1 << (num - PIC_SLAVE_INT_BASE)));
	}
}


/*!	Configures interrupt trigger mode (edge vs. level)
	
	This uses the ELCR (Edge/Level Control Register), which is not part of
	the original 8259A specification but is present on most modern chipsets.
	
	Edge-triggered: Interrupt fires on rising/falling edge of signal
	Level-triggered: Interrupt fires while signal is at specified level
	
	\param num Normalized interrupt number (0-15)
	\param config Configuration flags (B_LEVEL_TRIGGERED or B_EDGE_TRIGGERED)
*/
static void
pic_configure_io_interrupt(int32 num, uint32 config)
{
	// Never reconfigure cascade line (IRQ 2)
	if (num < 0 || num > PIC_NUM_INTS || num == PIC_CASCADE_IRQ)
		return;

	TRACE(("pic_configure_io_interrupt: irq %ld; config 0x%08lx\n", num, config));

	bool isMaster = (num < PIC_SLAVE_INT_BASE);
	uint8 value = pic_read_trigger_mode(isMaster);
	int32 localBit = isMaster ? num : (num - PIC_SLAVE_INT_BASE);

	// Set or clear the trigger mode bit
	if (config & B_LEVEL_TRIGGERED)
		value |= (1 << localBit);
	else
		value &= ~(1 << localBit);

	pic_write_trigger_mode(isMaster, value);

	// Update our cached trigger mode configuration
	sLevelTriggeredInterrupts = pic_read_trigger_mode(true)
		| (pic_read_trigger_mode(false) << 8);
}


// Initialization

/*!	Initialize the 8259A PICs in cascaded configuration
	
	This performs the standard ICW (Initialization Command Word) sequence:
	1. ICW1: Start initialization, specify if ICW4 is needed
	2. ICW2: Set interrupt vector offset (where IRQs map in IDT)
	3. ICW3: Configure cascading (master knows about slave, slave knows its ID)
	4. ICW4: Set operation mode (8086 mode)
	
	After initialization, all interrupts are masked except IRQ 2 (cascade line).
*/
void
pic_init()
{
	static const interrupt_controller picController = {
		"8259 PIC",
		&pic_enable_io_interrupt,
		&pic_disable_io_interrupt,
		&pic_configure_io_interrupt,
		&pic_is_spurious_interrupt,
		&pic_is_level_triggered_interrupt,
		&pic_end_of_interrupt,
		NULL	// No CPU affinity support in PIC mode
	};

	// ICW1: Begin initialization sequence
	// Bit 4 = 1 (ICW1), Bit 0 = 1 (ICW4 needed)
	out8(PIC_INIT1 | PIC_INIT1_SEND_INIT4, PIC_MASTER_INIT1);
	out8(PIC_INIT1 | PIC_INIT1_SEND_INIT4, PIC_SLAVE_INIT1);

	// ICW2: Set interrupt vector offset
	// Master: IRQ 0-7 map to vectors ARCH_INTERRUPT_BASE + 0-7
	// Slave: IRQ 8-15 map to vectors ARCH_INTERRUPT_BASE + 8-15
	out8(ARCH_INTERRUPT_BASE, PIC_MASTER_INIT2);
	out8(ARCH_INTERRUPT_BASE + PIC_SLAVE_INT_BASE, PIC_SLAVE_INIT2);

	// ICW3: Configure cascading
	// Master: bit 2 set = IRQ 2 has slave attached
	// Slave: value 2 = slave connected to master's IRQ 2
	out8(PIC_INIT3_IR2_IS_SLAVE, PIC_MASTER_INIT3);
	out8(PIC_INIT3_SLAVE_ID2, PIC_SLAVE_INIT3);

	// ICW4: Set 8086/8088 mode
	out8(PIC_INIT4_x86_MODE, PIC_MASTER_INIT4);
	out8(PIC_INIT4_x86_MODE, PIC_SLAVE_INIT4);

	// Mask all interrupts except cascade line (IRQ 2)
	pic_write_mask(true, PIC_MASK_ALL_EXCEPT_SLAVE);
	pic_write_mask(false, PIC_MASK_ALL);

	// Read and cache the trigger mode configuration from ELCR
	// This preserves any configuration set by the BIOS
	sLevelTriggeredInterrupts = pic_read_trigger_mode(true)
		| (pic_read_trigger_mode(false) << 8);

	TRACE(("PIC level trigger mode: 0x%04x\n", sLevelTriggeredInterrupts));

	// Reserve the 16 legacy ISA interrupt vectors
	reserve_io_interrupt_vectors(16, 0, INTERRUPT_TYPE_EXCEPTION);

	// Register this PIC controller with the interrupt subsystem
	arch_int_set_interrupt_controller(picController);
}


/*!	Disable the PIC and return currently enabled interrupts
	
	This is typically called when transitioning to APIC mode.
	All interrupts are masked to prevent spurious interrupts during transition.
	
	\param enabledInterrupts Output parameter receiving bitmap of enabled IRQs
		Bit N set = IRQ N was enabled before disabling
*/
void
pic_disable(uint16& enabledInterrupts)
{
	// Read current interrupt masks and invert to get enabled interrupts
	// (in PIC mask registers, 1 = disabled, 0 = enabled)
	uint16 masks = pic_read_mask(true) | (pic_read_mask(false) << 8);
	enabledInterrupts = ~masks;
	
	// Remove cascade IRQ from the enabled set (it's always "enabled")
	enabledInterrupts &= ~(1 << PIC_CASCADE_IRQ);

	// Mask all interrupts on both PICs
	pic_write_mask(true, PIC_MASK_ALL);
	pic_write_mask(false, PIC_MASK_ALL);

	// Free the reserved interrupt vectors
	free_io_interrupt_vectors(16, 0);
}
