/*
 * Copyright 2011-2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM PrimeCell PL011 UART Header for ARM64
 * 
 * Authors:
 *   Alexander von Gluck IV, kallisti5@unixzen.com
 *   ARM64 adaptations by Haiku ARM64 Team
 */
#ifndef _KERNEL_ARCH_ARM64_UART_PL011_H
#define _KERNEL_ARCH_ARM64_UART_PL011_H

#include <arch/generic/debug_uart.h>
#include <SupportDefs.h>

class ArchUARTPL011 : public DebugUART {
public:
					ArchUARTPL011(addr_t base, int64 clock);
					~ArchUARTPL011();

	virtual void	InitEarly();
	virtual void	InitPort(uint32 baud);

	virtual void	Enable();
	virtual void	Disable();

	virtual int		PutChar(char c);
	virtual int		GetChar(bool wait);

	virtual void	FlushTx();
	virtual void	FlushRx();

private:
	void			Out32(int reg, uint32 data);
	uint32			In32(int reg);
	void			Barrier();
};

ArchUARTPL011* arch_get_uart_pl011(addr_t base, int64 clock);

#endif /* _KERNEL_ARCH_ARM64_UART_PL011_H */
