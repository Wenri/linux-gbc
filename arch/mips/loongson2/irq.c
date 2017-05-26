/*
 * =====================================================================================
 *
 *       Filename:  irq.c
 *
 *    Description:  irq handle
 *
 *        Version:  1.0
 *        Created:  03/16/2017 10:52:40 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#include <linux/irq.h>
#include <asm/irq_cpu.h>
#include <asm/mach-loongson2/2k1000.h>
#include <irq.h>

/* ip7 take perf/timer */
/* ip6 take smp */
/* ip5 take off-chip msi irq */
/* ip4 take on-chip irq */
void ls64_ipi_interrupt(struct pt_regs *regs);

static void unmask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	chip = data->chip;
	if (data->irq >= LS64_MSI_IRQ_BASE)
		index = data->irq - LS64_MSI_IRQ_BASE;
	else
		index = data->irq - LS64_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index > 32) * 0x40 ;
	ls64_conf_write32((1 << index & 0x1f), (void *)(base + INT_SET0_OFF));

}

static void mask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	chip = data->chip;
	if (data->irq >= LS64_MSI_IRQ_BASE)
		index = data->irq - LS64_MSI_IRQ_BASE;
	else
		index = data->irq - LS64_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index > 32) * 0x40 ;
	ls64_conf_write32((1 << index & 0x1f), (void *)(base + INT_CLR0_OFF));

}

static struct irq_chip ls64_irq_chip = {
	.name		= "ls64soc",
	.irq_ack	= mask_icu_irq,
	.irq_eoi	= unmask_icu_irq,
	.irq_unmask	= unmask_icu_irq,
	.irq_mask	= mask_icu_irq,
};



asmlinkage void plat_irq_dispatch(void)
{
	unsigned int cp0_cause;
	unsigned int cp0_status;
	unsigned int cp0_cause_saved;
	unsigned long hi;
	unsigned long lo;
	unsigned long irq_status;
	unsigned long irq_masked;
	unsigned long irq_po;
	int i = (read_c0_ebase() & 0x3ff);
	unsigned long addr = CKSEG1ADDR(CONF_BASE);

	hi = ls64_conf_read32((void*)(addr + INTSR1_OFF + (i << 8)));
	lo = ls64_conf_read32((void*)(addr + INTSR0_OFF + (i << 8)));
	irq_status = ((hi << 32) | lo);

	hi = ls64_conf_read32((void*)(addr + INT_EN1_OFF));
	lo = ls64_conf_read32((void*)(addr + INT_EN0_OFF));
	irq_masked = ((hi << 32) | lo);

	hi = ls64_conf_read32((void*)(addr + INT_PLE1_OFF));
	lo = ls64_conf_read32((void*)(addr + INT_PLE0_OFF));
	irq_po = ((hi << 32) | lo);


	cp0_cause_saved = read_c0_cause() & ST0_IM ;
	cp0_status = read_c0_status();
	cp0_cause = cp0_cause_saved & cp0_status;

	if (cp0_cause & STATUSF_IP7) {
		do_IRQ(MIPS_CPU_IRQ_BASE+7);
	}
#ifdef CONFIG_SMP
	else if (cp0_cause & STATUSF_IP6) {
		ls64_ipi_interrupt(NULL);
	}
#endif
	else if (cp0_cause & STATUSF_IP5) {
		/* MSI has only 32 vector */
		hi = (irq_status & irq_po & 0xffffffff);
		irq_status ^= hi;
		while ((i = __fls(hi)) != -1) {
			do_IRQ(i + LS64_MSI_IRQ_BASE);
			hi = (hi ^ (1 << i));
		}
	}
	else if (cp0_cause & STATUSF_IP4) {
		lo = irq_status & irq_masked;
		irq_status ^= lo;
		while ((i = __fls(lo)) != -1) {
			do_IRQ(i + LS64_IRQ_BASE);
			lo = (lo ^ (1 << i));
		}

	}
	else if (irq_status) {
		pr_info("irq not handled i:%lx, c:%x, s:%x\n", irq_status, cp0_cause_saved,
				cp0_status);
	}

}

void  __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	set_c0_status(STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6);
}

void ls2k1000_icu_init_percpu(void)
{
	unsigned int i;

	for (i = LS64_IRQ_BASE; i < LS64_IRQ_BASE + 60; i++)
		irq_set_chip_and_handler(i, &ls64_irq_chip, handle_level_irq);
	/* last 4 GPIO, can be safely used at edge triggered */
	for (i = LS64_IRQ_BASE + 60; i < LS64_IRQ_BASE + 64; i++)
		irq_set_chip_and_handler(i, &ls64_irq_chip, handle_edge_irq);

	set_c0_status(STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6 | STATUSF_IP7);
}
