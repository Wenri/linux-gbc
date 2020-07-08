/*
 * =====================================================================================
 *
 *       Filename:  irq.h
 *
 *    Description:  irq config
 *
 *        Version:  1.0
 *        Created:  03/21/2017 03:13:52 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */

#ifndef __ASM_MACH_LS64SOC_IRQ_H_
#define __ASM_MACH_LS64SOC_IRQ_H_



#define MIPS_CPU_IRQ_BASE 0
#define NR_IRQS 192

enum LS2K_ICU {
	LS2K_IRQ_BASE = 8,

	/* 8 */
	UART_I0 = LS2K_IRQ_BASE,
	UART_I1,
	UART_I2,
	E1_I,
	HDA_I,
	I2S_I,
	AC97_I,
	THEN_I,

	/* 16 */
	TOY_TICK_I,
	RTC_TICK_I,
	RESERVED_I0,
	RESERVED_I1,
	GMAC0_I0,
	GMAC0_I1,
	GMAC1_I0,
	GMAC1_I1,

	/* 24 */
	CAN0_I,
	CAN1_I,
	BOOT_I,
	SATA_I,
	NAND_I,
	HPET_I,
	I2C0_I,
	I2C1_I,

	/* 32 */
	PWM0_I,
	PWM1_I,
	PWM2_I,
	PWM3_I,
	DC_I,
	GPU_I,
	RESERVED_I3,
	SDIO_I,

	/* 40 */
	PCI0_I0,
	PCI0_I1,
	PCI0_I2,
	PCI0_I3,
	PCI1_I0,
	PCI1_I1,
	PCI1_I2,
	PCI1_I3,

	/* 48 */
	TOY_I0,
	TOY_I1,
	TOY_I2,
	TOY_I3,
	DMA_I0,
	DMA_I1,
	DMA_I2,
	DMA_I3,

	/* 56 */
	DMA_I4,
	OTG_I,
	EHCI_I,
	OHCI_I,
	RTC_I0,
	RTC_I1,
	RTC_I2,
	RSA_I,

	/* 64 */
	AES_I,
	DES_I,
	GPIO_INT_LO_I,
	GPIO_INT_HI_I,
	GPIO_INT0,
	GPIO_INT1,
	GPIO_INT2,
	GPIO_INT3,

	LS64_MSI_IRQ_BASE = 128,
	LS64_IRQ_LAST = NR_IRQS - 1,
};

#include_next <irq.h>
#endif
