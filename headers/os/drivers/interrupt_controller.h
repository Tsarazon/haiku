/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _INTERRUPT_CONTROLLER_H
#define _INTERRUPT_CONTROLLER_H

#include <device_keeper.h>

enum {
	IRQ_TYPE_LEVEL	= 0,
	IRQ_TYPE_EDGE	= 1,
};

typedef struct interrupt_controller_info {
	int	cpu_count;		// number of supported CPUs
	int	irq_count;		// number of supported IRQs
} interrupt_controller_info;


// Bus interface name for publish_interface/get_interface. An interrupt
// controller driver publishes this on its node in attach().
// Children retrieve it via
//   gDeviceKeeper->get_interface(node, INTERRUPT_CONTROLLER_INTERFACE_NAME,
//       (const void**)&ops, &cookie);
#define INTERRUPT_CONTROLLER_INTERFACE_NAME \
	"interface/interrupt_controller/v1"

// interrupt controller drivers
typedef struct interrupt_controller_module_info {
	status_t	(*get_controller_info)(void *cookie,
					interrupt_controller_info *info);

	status_t	(*enable_io_interrupt)(void *cookie, int irq, int type);
	status_t	(*disable_io_interrupt)(void *cookie, int irq);

	// Returns the IRQ number or a negative value, if the interrupt shall be
	// ignore (spurious interrupts). Since more than one interrupt can be
	// pending, the function should be called in a loop until it returns a
	// negative value.
	// Must be called with CPU interrupts disabled.
	int			(*acknowledge_io_interrupt)(void *cookie);

} interrupt_controller_module_info;

#endif	// _INTERRUPT_CONTROLLER_H
