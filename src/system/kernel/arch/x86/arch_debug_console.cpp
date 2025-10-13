/*
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de
 * Copyright 2001, Rob Judd <judd@r2d2.stcloudstate.edu>
 * Copyright 2002, Marcus Overhagen <marcus@overhagen.de>
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*! x86 Debug Console Driver
 *
 * Provides serial port output and PS/2 keyboard input for kernel debugging.
 *
 * Serial output uses 16550-compatible UART at configurable port (default COM1).
 * Keyboard input polls PS/2 controller for emergency debugger entry and reboot.
 */

#include "debugger_keymaps.h"
#include "ps2_defs.h"

#include <KernelExport.h>
#include <driver_settings.h>
#include <interrupts.h>

#include <arch/cpu.h>
#include <arch/debug_console.h>
#include <boot/stage2.h>
#include <debug.h>

#include <string.h>
#include <stdlib.h>


// 16550 UART Register Offsets
enum serial_register_offsets {
	SERIAL_TRANSMIT_BUFFER		= 0,
	SERIAL_RECEIVE_BUFFER		= 0,
	SERIAL_DIVISOR_LATCH_LOW	= 0,
	SERIAL_DIVISOR_LATCH_HIGH	= 1,
	SERIAL_FIFO_CONTROL			= 2,
	SERIAL_LINE_CONTROL			= 3,
	SERIAL_MODEM_CONTROL		= 4,
	SERIAL_LINE_STATUS			= 5,
	SERIAL_MODEM_STATUS			= 6,
};

// Line Status Register bits (LSR)
#define SERIAL_LSR_DATA_READY			0x01
#define SERIAL_LSR_TRANSMIT_EMPTY		0x20

// Line Control Register bits (LCR)
#define SERIAL_LCR_DLAB					0x80  // Divisor Latch Access Bit
#define SERIAL_LCR_8N1					0x03  // 8 data, no parity, 1 stop

// PS/2 Keyboard Scancodes (Set 1)
enum keycodes {
	LEFT_SHIFT		= 42,
	RIGHT_SHIFT		= 54,
	LEFT_CONTROL	= 29,
	LEFT_ALT		= 56,
	RIGHT_ALT		= 58,
	CURSOR_LEFT		= 75,
	CURSOR_RIGHT	= 77,
	CURSOR_UP		= 72,
	CURSOR_DOWN		= 80,
	CURSOR_HOME		= 71,
	CURSOR_END		= 79,
	PAGE_UP			= 73,
	PAGE_DOWN		= 81,
	DELETE			= 83,
	SYS_REQ			= 84,
	F12				= 88,
};

static const uint16 kDefaultSerialPort = 0x3f8;  // COM1
static const uint32 kDefaultBaudRate = 115200;
static const int32 kSerialTimeout = 256 * 1024;  // Loop iterations

static uint32 sSerialBaudRate = kDefaultBaudRate;
static uint16 sSerialBasePort = kDefaultSerialPort;

static spinlock sSerialOutputSpinlock = B_SPINLOCK_INITIALIZER;
static int32 sEarlyBootMessageLock = 0;  // Atomic lock for pre-threading phase
static bool sSerialTimedOut = false;     // Permanent failure flag

static bool sKeyboardHandlerInstalled = false;


static void
init_serial_port(uint16 basePort, uint32 baudRate)
{
	sSerialBasePort = basePort;
	sSerialBaudRate = baudRate;

	uint16 divisor = (uint16)(115200 / baudRate);

	// Set DLAB to access divisor latches
	out8(SERIAL_LCR_DLAB, sSerialBasePort + SERIAL_LINE_CONTROL);
	out8(divisor & 0xf, sSerialBasePort + SERIAL_DIVISOR_LATCH_LOW);
	out8(divisor >> 8, sSerialBasePort + SERIAL_DIVISOR_LATCH_HIGH);
	// Clear DLAB, configure 8N1
	out8(SERIAL_LCR_8N1, sSerialBasePort + SERIAL_LINE_CONTROL);
}


static void
put_char(const char c)
{
	int32 timeout = kSerialTimeout;

	while ((in8(sSerialBasePort + SERIAL_LINE_STATUS) & SERIAL_LSR_TRANSMIT_EMPTY) == 0) {
		if (--timeout == 0) {
			// Timeout is permanent - don't hang kernel on broken/missing UART
			sSerialTimedOut = true;
			return;
		}
		arch_cpu_pause();
	}

	out8(c, sSerialBasePort + SERIAL_TRANSMIT_BUFFER);
}


/*!	Minimal keyboard interrupt handler for debugger entry.
 *
 * Active only before input_server starts. Handles:
 * - Ctrl+Alt+Del emergency reboot
 * - Alt+SysRq+key debug commands
 *
 * State machine can desync on missed key releases (inherent PS/2 limitation).
 * This is acceptable since it's only for emergency access.
 */
static int32
debug_keyboard_interrupt(void *data)
{
	// State persists across calls (single-CPU interrupt handler)
	static bool controlPressed = false;
	static bool altPressed = false;
	static bool sysReqPressed = false;

	uint8 key = in8(PS2_PORT_DATA);

	if (key & 0x80) {
		// Key release (high bit set)
		switch (key & ~0x80) {
			case LEFT_CONTROL:
				controlPressed = false;
				break;
			case LEFT_ALT:
			case RIGHT_ALT:
				altPressed = false;
				break;
			case SYS_REQ:
				sysReqPressed = false;
				break;
		}
		return B_HANDLED_INTERRUPT;
	}

	// Key press
	switch (key) {
		case LEFT_CONTROL:
			controlPressed = true;
			break;

		case LEFT_ALT:
		case RIGHT_ALT:
			altPressed = true;
			break;

		case SYS_REQ:
			sysReqPressed = true;
			break;

		case DELETE:
			if (controlPressed && altPressed)
				arch_cpu_shutdown(true);
		break;

		default:
			if (altPressed && sysReqPressed) {
				if (debug_emergency_key_pressed(kUnshiftedKeymap[key])) {
					// Command executed - reset state to avoid repeated triggers
					controlPressed = false;
					sysReqPressed = false;
					altPressed = false;
				}
			}
			break;
	}

	return B_HANDLED_INTERRUPT;
}


//	#pragma mark - Public API


void
arch_debug_remove_interrupt_handler(uint32 line)
{
	if (line != INT_PS2_KEYBOARD || !sKeyboardHandlerInstalled)
		return;

	remove_io_interrupt_handler(INT_PS2_KEYBOARD, &debug_keyboard_interrupt,
								NULL);
	sKeyboardHandlerInstalled = false;
}


void
arch_debug_install_interrupt_handlers(void)
{
	install_io_interrupt_handler(INT_PS2_KEYBOARD, &debug_keyboard_interrupt,
								 NULL, 0);
	sKeyboardHandlerInstalled = true;
}


int
arch_debug_blue_screen_try_getchar(void)
{
	/*!	Poll PS/2 keyboard without interrupts (debugger mode).
	 *
	 *	Generates ANSI escape sequences for cursor/editing keys.
	 *	State machine handles multi-byte sequences across calls.
	 */
	static bool shiftPressed = false;
	static bool controlPressed = false;
	static bool altPressed = false;
	static uint8 special = 0;   // First escape byte
	static uint8 special2 = 0;  // Second escape byte

	// Multi-byte escape sequence state machine
	if (special & 0x80) {
		special &= ~0x80;
		return '[';
	}
	if (special != 0) {
		uint8 key = special;
		special = 0;
		return key;
	}
	if (special2 != 0) {
		uint8 key = special2;
		special2 = 0;
		return key;
	}

	uint8 status = in8(PS2_PORT_CTRL);
	if ((status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0)
		return -1;

	uint8 key = in8(PS2_PORT_DATA);

	// Ignore mouse data on auxiliary port
	if (status & PS2_STATUS_AUX_DATA)
		return -1;

	if (key & 0x80) {
		// Key release
		switch (key & ~0x80) {
			case LEFT_SHIFT:
			case RIGHT_SHIFT:
				shiftPressed = false;
				return -1;
			case LEFT_CONTROL:
				controlPressed = false;
				return -1;
			case LEFT_ALT:
				altPressed = false;
				return -1;
		}
		return -1;
	}

	// Key press
	switch (key) {
		case LEFT_SHIFT:
		case RIGHT_SHIFT:
			shiftPressed = true;
			return -1;

		case LEFT_CONTROL:
			controlPressed = true;
			return -1;

		case LEFT_ALT:
			altPressed = true;
			return -1;

			// Cursor keys generate ANSI escape sequences: ESC [ X
		case CURSOR_UP:
			special = 0x80 | 'A';
			return '\x1b';
		case CURSOR_DOWN:
			special = 0x80 | 'B';
			return '\x1b';
		case CURSOR_RIGHT:
			special = 0x80 | 'C';
			return '\x1b';
		case CURSOR_LEFT:
			special = 0x80 | 'D';
			return '\x1b';
		case CURSOR_HOME:
			special = 0x80 | 'H';
			return '\x1b';
		case CURSOR_END:
			special = 0x80 | 'F';
			return '\x1b';

			// Page/Delete keys generate: ESC [ N ~
		case PAGE_UP:
			special = 0x80 | '5';
			special2 = '~';
			return '\x1b';
		case PAGE_DOWN:
			special = 0x80 | '6';
			special2 = '~';
			return '\x1b';

		case DELETE:
			if (controlPressed && altPressed)
				arch_cpu_shutdown(true);
		special = 0x80 | '3';
		special2 = '~';
		return '\x1b';

		default:
			// Convert scancode to ASCII
			if (controlPressed) {
				char c = kShiftedKeymap[key];
				if (c >= 'A' && c <= 'Z')
					return 0x1f & c;  // Ctrl+letter = ASCII control char
			}
			if (altPressed)
				return kAltedKeymap[key];
		return shiftPressed ? kShiftedKeymap[key] : kUnshiftedKeymap[key];
	}
}


char
arch_debug_blue_screen_getchar(void)
{
	while (true) {
		int c = arch_debug_blue_screen_try_getchar();
		if (c >= 0)
			return (char)c;
		arch_cpu_pause();
	}
}


int
arch_debug_serial_try_getchar(void)
{
	uint8 lineStatus = in8(sSerialBasePort + SERIAL_LINE_STATUS);

	// LSR 0xff indicates no UART present at this address
	if (lineStatus == 0xff)
		return -1;

	if ((lineStatus & SERIAL_LSR_DATA_READY) == 0)
		return -1;

	return in8(sSerialBasePort + SERIAL_RECEIVE_BUFFER);
}


char
arch_debug_serial_getchar(void)
{
	while (true) {
		uint8 lineStatus = in8(sSerialBasePort + SERIAL_LINE_STATUS);

		// No UART present
		if (lineStatus == 0xff)
			return 0;

		if (lineStatus & SERIAL_LSR_DATA_READY)
			break;

		arch_cpu_pause();
	}

	return in8(sSerialBasePort + SERIAL_RECEIVE_BUFFER);
}


static void
_arch_debug_serial_putchar(const char c)
{
	if (c == '\n') {
		put_char('\r');
		put_char('\n');
	} else if (c != '\r') {
		put_char(c);
	}
}


void
arch_debug_serial_putchar(const char c)
{
	if (sSerialTimedOut)
		return;

	cpu_status state = 0;
	if (!debug_debugger_running()) {
		state = disable_interrupts();
		acquire_spinlock(&sSerialOutputSpinlock);
	}

	_arch_debug_serial_putchar(c);

	if (!debug_debugger_running()) {
		release_spinlock(&sSerialOutputSpinlock);
		restore_interrupts(state);
	}
}


static void
arch_debug_serial_puts_locked(const char *string)
{
	while (*string != '\0') {
		_arch_debug_serial_putchar(*string);
		string++;
	}
}


void
arch_debug_serial_puts(const char *s)
{
	if (sSerialTimedOut)
		return;

	cpu_status state = 0;
	if (!debug_debugger_running()) {
		state = disable_interrupts();
		acquire_spinlock(&sSerialOutputSpinlock);
	}

	arch_debug_serial_puts_locked(s);

	if (!debug_debugger_running()) {
		release_spinlock(&sSerialOutputSpinlock);
		restore_interrupts(state);
	}
}


void
arch_debug_serial_early_boot_message(const char *string)
{
	/*!	Output critical early boot messages before threading available.
	 *
	 *	Uses atomic lock instead of spinlock because current_thread()
	 *	returns NULL during early boot phase.
	 */
	if (sSerialTimedOut)
		return;

	// Spin on atomic lock (no scheduler to yield to)
	while (atomic_test_and_set(&sEarlyBootMessageLock, 1, 0) != 0)
		arch_cpu_pause();

	arch_debug_console_init(NULL);
	arch_debug_serial_puts_locked(string);

	atomic_set(&sEarlyBootMessageLock, 0);
}


status_t
arch_debug_console_init(kernel_args *args)
{
	// Prefer boot loader discovered serial port
	if (args != NULL && args->platform_args.serial_base_ports[0] != 0)
		sSerialBasePort = args->platform_args.serial_base_ports[0];

	init_serial_port(sSerialBasePort, sSerialBaudRate);

	return B_OK;
}


status_t
arch_debug_console_init_settings(kernel_args *args)
{
	uint32 baudRate = sSerialBaudRate;
	uint16 basePort = sSerialBasePort;

	void* handle = load_driver_settings("kernel");
	if (handle != NULL) {
		const char *value = get_driver_parameter(handle, "serial_debug_port",
												 NULL, NULL);
		if (value != NULL) {
			int32 number = strtol(value, NULL, 0);
			if (number >= MAX_SERIAL_PORTS) {
				// Direct I/O port address
				basePort = number;
			} else if (number >= 0) {
				// Index into boot loader discovered ports
				if (args->platform_args.serial_base_ports[number] != 0)
					basePort = args->platform_args.serial_base_ports[number];
			}
		}

		value = get_driver_parameter(handle, "serial_debug_speed", NULL, NULL);
		if (value != NULL) {
			int32 number = strtol(value, NULL, 0);
			switch (number) {
				case 9600:
				case 19200:
				case 38400:
				case 57600:
				case 115200:
					baudRate = number;
					break;
			}
		}

		unload_driver_settings(handle);
	}

	// Reinitialize if configuration changed
	if (sSerialBasePort != basePort || baudRate != sSerialBaudRate) {
		init_serial_port(basePort, baudRate);
		sSerialTimedOut = false;  // Reset timeout on reconfiguration
	}

	return B_OK;
}
