/*  early printk support
 *
 *  Copyright (c) 2009 Philippe Vachon <philippe@cowpig.ca>
 *  Copyright (c) 2009 Lemote Inc.
 *  Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <linux/serial_reg.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/mach-loongson2/2k1000.h>

#define PORT(base, offset) (u8 *)(base + offset)

static inline unsigned int serial_in(unsigned char *base, int offset)
{
	return readb(PORT(base, offset));
}

static inline void serial_out(unsigned char *base, int offset, int value)
{
	writeb(value, PORT(base, offset));
}

void prom_putchar(char c)
{
	int timeout;
	unsigned char *uart_base;

	uart_base = (unsigned char *)CKSEG1ADDR(APB_BASE + UART0_OFF);
	timeout = 1024;

	while (((serial_in(uart_base, UART_LSR) & UART_LSR_THRE) == 0) &&
			(timeout-- > 0))
		;

	serial_out(uart_base, UART_TX, c);
}

void prom_printf(char *fmt, ...)
{
	va_list args;
	static char ppbuf[1024];
	char *bptr;

	va_start(args, fmt);
	vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;
	while (*bptr != 0) {
		if (*bptr == '\n')
			prom_putchar('\r');

		prom_putchar(*bptr++);
	}

	va_end(args);
}
