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
#include <linux/types.h>
#include <linux/interrupt.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <loongson.h>
#include <ls2k.h>
#include <ls2k_int.h>

static struct ls2k_int_ctrl_regs volatile *int_ctrl_regs
	= (struct ls2k_int_ctrl_regs volatile *)
	(CKSEG1ADDR(LS2K_INT_REG_BASE));

DEFINE_RAW_SPINLOCK(ls2k_irq_lock);

void ack_ls2k_board_irq(struct irq_data *d)
{
	unsigned long flags;
	
	unsigned long irq_nr = d->irq;

	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);

	irq_nr -= LS2K_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_clr = (1 << (irq_nr & 0x1f));

	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

void disable_ls2k_board_irq(struct irq_data *d)
{
	unsigned long flags;

	unsigned long irq_nr = d->irq;
	
	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);

	irq_nr -= LS2K_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_clr = (1 << (irq_nr & 0x1f));

	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

void enable_ls2k_board_irq(struct irq_data *d)
{
	unsigned long flags;

	unsigned long irq_nr = d->irq;
	
	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);

	irq_nr -= LS2K_IRQ_BASE;
	(int_ctrl_regs + (irq_nr >> 5))->int_set = (1 << (irq_nr & 0x1f));

	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

static struct irq_chip ls2k_board_irq_chip = {
	.name		= "LS2K BOARD",
	.irq_ack		= ack_ls2k_board_irq,
	.irq_mask		= disable_ls2k_board_irq,
	.irq_mask_ack	= disable_ls2k_board_irq,
	.irq_unmask		= enable_ls2k_board_irq,
	.irq_eoi		= enable_ls2k_board_irq,
};

static void __ls2k_irq_dispatch(int n, int intstatus)
{
	int irq;

	if(!intstatus) return;
	irq = ffs(intstatus);
	do_IRQ(n * 32 + LS2K_IRQ_BASE + irq - 1);
}

void _ls2k_irq_dispatch(void)
{
#if 0
	int intstatus, i;

	for (i = 0; i < 2; i++)
		if ((intstatus = (int_ctrl_regs + i)->int_isr) != 0) {
			__ls2k_irq_dispatch(i, intstatus);
		}
#else
	 int cpu = smp_processor_id();
	 if(!cpu)
	 {
		 __ls2k_irq_dispatch(0, IO_control_regs_CORE0_INTISR);
		 __ls2k_irq_dispatch(1, IO_control_regs_CORE0_INTISR_HI);
	 }
	 else
	 {
		 __ls2k_irq_dispatch(0, IO_control_regs_CORE1_INTISR);
		 __ls2k_irq_dispatch(1, IO_control_regs_CORE1_INTISR_HI);
	 }
#endif
}

void _ls2k_init_irq(u32 irq_base)
{
	u32 i;

	/* uart, keyboard, and mouse are active high */
	(int_ctrl_regs + 0)->int_clr	= -1;
	(int_ctrl_regs + 0)->int_auto	= 0;
	(int_ctrl_regs + 0)->int_bounce	= 0;

	(int_ctrl_regs + 1)->int_clr	= -1;
	(int_ctrl_regs + 1)->int_auto	= 0;
	(int_ctrl_regs + 1)->int_bounce	= 0;


	for (i = irq_base; i <= LS2K_LAST_IRQ; i++)
		irq_set_chip_and_handler(i, &ls2k_board_irq_chip,
					 handle_level_irq);
}
