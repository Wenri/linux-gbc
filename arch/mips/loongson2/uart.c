/*
 * =====================================================================================
 *
 *       Filename:  uart.c
 *
 *    Description:  UART for serial console
 *
 *        Version:  1.0
 *        Created:  03/21/2017 10:40:53 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <asm/mach-loongson2/2k1000.h>
#include <irq.h>

/* This file should be replaced by of_serial driver*/



static struct plat_serial8250_port uart8250_data[2] = {
	{
		.membase = (void *)CKSEG1ADDR(APB_BASE + UART0_OFF),
		.mapbase = CKSEG1ADDR(APB_BASE + UART0_OFF),
		.uartclk = 125000000,
		.irq = UART_I0,
		.regshift = 0,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype = UPIO_MEM,
	},
	{}
};

static struct platform_device uart8250_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
};

static int __init serial_init(void)
{
	uart8250_device.dev.platform_data = &uart8250_data;

	return platform_device_register(&uart8250_device);
}

arch_initcall(serial_init);
