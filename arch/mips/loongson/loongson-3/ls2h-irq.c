/*
 *  Copyright (C) 2013, Loongson Technology Corporation Limited, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/smp.h>
#include <asm/delay.h>
#include <irq.h>
#include <loongson.h>
#include <boot_param.h>
#include <loongson-pch.h>
#include <linux/cpumask.h>

static struct ls2h_intctl_regs volatile *int_ctrl_regs
	= (struct ls2h_intctl_regs volatile *)(CKSEG1ADDR(LS2H_INT_REG_BASE));

static DEFINE_SPINLOCK(pch_irq_lock);

#define LS2H_IRQS	(LS2H_PCH_LAST_IRQ - LS2H_PCH_IRQ_BASE)
#define LS2H_DIRQS 	(32-IPI_IRQ_OFFSET)
unsigned int ls2h_irq2pos[LS2H_IRQS];
unsigned int ls2h_pos2irq[LS2H_DIRQS];
static DECLARE_BITMAP(ls2h_irq_in_use, LS2H_DIRQS);
static unsigned char irq_cpu[LS2H_IRQS] =  {[0 ... LS2H_IRQS-1] = -1};;

void unmask_ls2h_irq(struct irq_data *d);
void mask_ls2h_irq(struct irq_data *d);
extern struct plat_smp_ops *mp_ops;

static int ls2h_create_dirq(unsigned int irq)
{
	unsigned long flags;
	int pos;
	spin_lock_irqsave(&pch_irq_lock, flags);
again:
	pos = find_first_zero_bit(ls2h_irq_in_use, LS2H_DIRQS);
	if(pos == LS2H_DIRQS)
	{
		spin_unlock_irqrestore(&pch_irq_lock, flags);
		return -ENOSPC;
	}
	if (test_and_set_bit(pos, ls2h_irq_in_use))
		goto again;
	ls2h_pos2irq[pos] = irq+1;
	ls2h_irq2pos[irq-LS2H_PCH_IRQ_BASE] = pos+1;
	spin_unlock_irqrestore(&pch_irq_lock, flags);
	return 0;
}

static void ls2h_destroy_dirq(unsigned int irq)
{
	unsigned long flags;
	int pos;
	spin_lock_irqsave(&pch_irq_lock, flags);
	pos = ls2h_irq2pos[irq-LS2H_PCH_IRQ_BASE];

	if(pos)
	{
		clear_bit(pos-1, ls2h_irq_in_use);
		ls2h_irq2pos[irq-LS2H_PCH_IRQ_BASE] = 0;
		ls2h_pos2irq[pos-1] = 0;
	}
	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

unsigned int startup_ls2h_board_irq(struct irq_data *d)
{
	unsigned long irq_nr = d->irq;
	ls2h_create_dirq(irq_nr);
	unmask_ls2h_irq(d);
	return 0;
}

void shutdown_ls2h_board_irq(struct irq_data *d)
{
	unsigned long irq_nr = d->irq;
	mask_ls2h_irq(d);
	ls2h_destroy_dirq(irq_nr);
}

void ack_ls2h_irq(struct irq_data *d)
{
	int irq_nr;
	unsigned long flags;

	spin_lock_irqsave(&pch_irq_lock, flags);

	irq_nr = d->irq - LS2H_PCH_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_clr = (1 << (irq_nr & 0x1f));

	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

void mask_ls2h_irq(struct irq_data *d)
{
	int irq_nr;
	unsigned long flags;

	spin_lock_irqsave(&pch_irq_lock, flags);

	irq_nr = d->irq - LS2H_PCH_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_en &= ~(1 << (irq_nr & 0x1f));

	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

static void mask_ack_ls2h_irq(struct irq_data *d)
{
	int irq_nr;
	unsigned long flags;

	spin_lock_irqsave(&pch_irq_lock, flags);

	irq_nr = d->irq - LS2H_PCH_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_clr = (1 << (irq_nr & 0x1f));
	(int_ctrl_regs + (irq_nr >> 5))->int_en &= ~(1 << (irq_nr & 0x1f));

	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

void unmask_ls2h_irq(struct irq_data *d)
{
	int irq_nr;
	unsigned long flags;

	spin_lock_irqsave(&pch_irq_lock, flags);

	irq_nr = d->irq - LS2H_PCH_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_en |= (1 << (irq_nr & 0x1f));

	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

#define eoi_pch_irq unmask_ls2h_irq

static struct irq_chip pch_irq_chip = {
	.name		= "Loongson",
	.irq_ack	= ack_ls2h_irq,
	.irq_mask	= mask_ls2h_irq,
	.irq_mask_ack	= mask_ack_ls2h_irq,
	.irq_unmask	= unmask_ls2h_irq,
	.irq_eoi	= eoi_pch_irq,
	.irq_startup	= startup_ls2h_board_irq,
	.irq_shutdown	= shutdown_ls2h_board_irq,
	.irq_set_affinity	= plat_set_irq_affinity,
};

static DEFINE_SPINLOCK(lpc_irq_lock);

static void ack_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);

	ls2h_writel(0x1 << (d->irq), LS_LPC_INT_CLR);

	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

static void mask_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);

	ls2h_writel(ls2h_readl(LS_LPC_INT_ENA) & ~(0x1 << (d->irq)), LS_LPC_INT_ENA);

	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

static void mask_ack_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);

	ls2h_writel(0x1 << (d->irq), LS_LPC_INT_CLR);
	ls2h_writel(ls2h_readl(LS_LPC_INT_ENA) & ~(0x1 << (d->irq)), LS_LPC_INT_ENA);

	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

static void unmask_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);

	ls2h_writel(ls2h_readl(LS_LPC_INT_ENA) | (0x1 << (d->irq)), LS_LPC_INT_ENA);

	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

#define eoi_lpc_irq unmask_lpc_irq

static struct irq_chip lpc_irq_chip = {
	.name		= "Loongson",
	.irq_ack	= ack_lpc_irq,
	.irq_mask	= mask_lpc_irq,
	.irq_mask_ack	= mask_ack_lpc_irq,
	.irq_unmask	= unmask_lpc_irq,
	.irq_eoi	= eoi_lpc_irq,
};


static void __ls2h_irq_dispatch(int n, int intstatus)
{
	int irq, irq1;
	static unsigned int core_num = 0;
	struct irq_data *irqd;
	int cpu = smp_processor_id();

	irq = ffs(intstatus);
	if (!irq) {
		pr_info("Unknow n: %d intstatus %x \n", n, intstatus);
		spurious_interrupt();
	} else {
		core_num = (core_num + 1) % cores_per_package;
		irq = n * 32 + LS2H_PCH_IRQ_BASE + irq - 1;
		irq1 = irq - LS2H_PCH_IRQ_BASE;
		irqd = irq_get_irq_data(irq);
		irq_cpu[irq1] = cpumask_next(irq_cpu[irq1], irqd->affinity);

		if (irq_cpu[irq1] >= nr_cpu_ids)
			irq_cpu[irq1] = cpumask_first(irqd->affinity);
		core_num = irq_cpu[irq1];
		if (!ls2h_irq2pos[irq-LS2H_PCH_IRQ_BASE] || core_num == cpu || !cpu_online(core_num))
			do_IRQ(irq);
		else
		{
			mask_ls2h_irq(irqd);
			mp_ops->send_ipi_single(core_num, (1 << (ls2h_irq2pos[irq1]-1)) << IPI_IRQ_OFFSET);
		}
	}
}


asmlinkage void ls2h_irq_dispatch(void)
{
	int i, intstatus, irqs, lpc_irq;

	for (i = 0; i < 5; i++) {
		if ((intstatus = (int_ctrl_regs + i)->int_isr) == 0)
			continue;

		if ((i == 0) && (intstatus & (1 << 13)) && ls_lpc_reg_base == LS2H_LPC_REG_BASE) {
			irqs = ls2h_readl(LS_LPC_INT_ENA) & ls2h_readl(LS_LPC_INT_STS) & 0xfeff;
			if (irqs)
				while ((lpc_irq = ffs(irqs))) {
					do_IRQ(lpc_irq - 1);
					irqs &= ~(1 << (lpc_irq-1));
				}
		} else
			__ls2h_irq_dispatch(i, intstatus);

	}
}



static void ls2h_irq_router_init(void)
{
	/* Route INTn0 to Core0 INT1 */
	LOONGSON_INT_ROUTER_ENTRY(0) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);

	/* Route the LPC interrupt to Core0 INT0 */
	LOONGSON_INT_ROUTER_LPC = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);

	/* Enable UART and INT0 interrupts */
	LOONGSON_INT_ROUTER_INTENSET = (0x1 << 10) | (1 << 0);

	/* uart, keyboard, and mouse are active high */
	(int_ctrl_regs + 0)->int_edge	= 0x00000000;
	(int_ctrl_regs + 0)->int_pol	= 0xff7fffff;
	(int_ctrl_regs + 0)->int_clr	= 0x00000000;
	(int_ctrl_regs + 0)->int_en	= 0x00ffffff;

	(int_ctrl_regs + 1)->int_edge	= 0x00000000;
	(int_ctrl_regs + 1)->int_pol	= 0xfeffffff;
	(int_ctrl_regs + 1)->int_clr	= 0x00000000;
	(int_ctrl_regs + 1)->int_en	= 0x03ffffff;

	(int_ctrl_regs + 2)->int_edge	= 0x00000000;
	(int_ctrl_regs + 2)->int_pol	= 0xfffffffe;
	(int_ctrl_regs + 2)->int_clr	= 0x00000000;
	(int_ctrl_regs + 2)->int_en	= 0x00000001;

	(int_ctrl_regs + 3)->int_edge = 0xffffffff;
	(int_ctrl_regs + 3)->int_pol = 0xffffffff;
	(int_ctrl_regs + 3)->int_clr = 0x00000000;
	(int_ctrl_regs + 3)->int_en = 0x00000000;

	/* Enable the LPC interrupt */
	ls2h_writel(0x80000000, LS_LPC_INT_CTL);

	/* set the 18-bit interrpt enable bit for keyboard and mouse */
	ls2h_writel(0x1 << 0x1 | 0x1 << 12, LS_LPC_INT_ENA);

	/* clear all 18-bit interrpt bit */
	ls2h_writel(0x3ffff, LS_LPC_INT_CLR);
}

void __init ls2h_init_irq(void)
{
	u32 i;

	local_irq_disable();
	ls2h_irq_router_init();

	for (i = LS2H_PCH_IRQ_BASE; i <= LS2H_PCH_LAST_IRQ; i++)
	{
		if((i-LS2H_PCH_IRQ_BASE)/32==3) continue;
		irq_set_chip_and_handler(i, &pch_irq_chip,
					 handle_level_irq);
	}

	/* added for KBC attached on LPC controler */
	irq_set_chip_and_handler(1, &lpc_irq_chip, handle_level_irq);
	irq_set_chip_and_handler(12, &lpc_irq_chip, handle_level_irq);

	if(cpu_has_vint)
		set_vi_handler(3, ls2h_irq_dispatch);
}

#ifdef CONFIG_PM

static int loongson3_ls2h_suspend(void)
{
	return 0;
}

static void loongson3_ls2h_resume(void)
{
	ls2h_irq_router_init();
}

static struct syscore_ops ls2h_syscore_ops = {
	.suspend = loongson3_ls2h_suspend,
	.resume = loongson3_ls2h_resume,
};

int __init ls2h_init_ops(void)
{
	register_syscore_ops(&ls2h_syscore_ops);
	return 0;
}

#endif
