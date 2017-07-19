#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <ls2k_int.h>
#include <ls2k.h>

extern unsigned long long smp_group[4];
extern void loongson2k_ipi_interrupt(struct pt_regs *regs);

int plat_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	unsigned int cpu;
	struct cpumask new_affinity;

	/* I/O devices are connected on package-0 */
	cpumask_copy(&new_affinity, affinity);
	for_each_cpu(cpu, affinity)
		if (cpu_data[cpu].package > 0)
			cpumask_clear_cpu(cpu, &new_affinity);

	if (cpumask_empty(&new_affinity))
		return -EINVAL;

	cpumask_copy(d->affinity, &new_affinity);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct ls2k_int_ctrl_regs volatile *int_ctrl_regs
	= (struct ls2k_int_ctrl_regs volatile *)
	(CKSEG1ADDR(LS2K_INT_REG_BASE));

DEFINE_RAW_SPINLOCK(ls2k_irq_lock);

static void __ls2k_irq_dispatch(int n, int intstatus)
{
	int irq;

	if(!intstatus) return;
	irq = ffs(intstatus);
	do_IRQ(n * 32 + LS2K_IRQ_BASE + irq - 1);
}

void _ls2k_irq_dispatch(void)
{
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
}

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

void _ls2k_init_irq(u32 irq_base)
{
	u32 i;

	/* uart, keyboard, and mouse are active high */
	(int_ctrl_regs + 0)->int_clr	= -1;
	(int_ctrl_regs + 0)->int_auto	= 0;
	(int_ctrl_regs + 0)->int_bounce	= 0;
    (int_ctrl_regs + 0)->int_edge   = (~LS2K_IRQ_MASK) & 0xffffffff;

	(int_ctrl_regs + 1)->int_clr	= -1;
	(int_ctrl_regs + 1)->int_auto	= 0;
	(int_ctrl_regs + 1)->int_bounce	= 0;
    (int_ctrl_regs + 1)->int_edge   = ((~LS2K_IRQ_MASK)>>32) & 0xffffffff;


	for (i = irq_base; i <= LS2K_LAST_IRQ; i++){   
        if((1<<(i - irq_base)) & LS2K_IRQ_MASK)
		irq_set_chip_and_handler(i, &ls2k_board_irq_chip,
					 handle_level_irq);
    }

    /*config msi window*/
    writeq(0x000000001fe10000ULL, (void *)CKSEG1ADDR(0x1fe12500));
    writeq(0xffffffffffff0000ULL, (void *)CKSEG1ADDR(0x1fe12540));
    writeq(0x000000001fe10081ULL, (void *)CKSEG1ADDR(0x1fe12580));
}

void __init ls2k_init_irq(void)
{
	clear_c0_status(ST0_IM | ST0_BEV);
	local_irq_disable();

	_ls2k_init_irq(LS2K_IRQ_BASE);

#if 1
	{
		volatile unsigned char *p = (unsigned char *)(IO_base_regs_addr+0x1400);
		int i;
		for(i=0;i<0x20;i++)
		p[i] = 0x11;

		for(i=0x40;i<0x60;i++)
		p[i] = 0x11;
	}
#endif

	INT_router_regs_uart0 = 0x11;    //uart0 IP2

	INT_router_regs_gmac0 = 0x81;     //IP5

	INT_router_regs_gpu = 0x81;    //IP5

	INT_router_regs_ahci = 0x81;     //IP5

	INT_router_regs_ohci = 0x21;     //IP3

}

#define UNUSED_IPS (CAUSEF_IP5 | CAUSEF_IP4 | CAUSEF_IP1 | CAUSEF_IP0)

void mach_irq_dispatch(unsigned int pending)
{
	if (!pending)
		return;

	if (pending & CAUSEF_IP7) {
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	}
#ifdef CONFIG_SMP
	if (pending & CAUSEF_IP6) {
		loongson2k_ipi_interrupt(NULL);
	}
#endif
	if (pending & CAUSEF_IP2) {
		_ls2k_irq_dispatch();
	}
	if (pending & CAUSEF_IP3) {
		_ls2k_irq_dispatch();
	}
	if (pending & CAUSEF_IP5) {
		_ls2k_irq_dispatch();
	}
	if (pending & (~(CAUSEF_IP7 | CAUSEF_IP6 | CAUSEF_IP2 | CAUSEF_IP3 | CAUSEF_IP5))) {
		spurious_interrupt();
	}
}

static struct irqaction cascade_irqaction = {
	.handler = no_action,
	.flags = IRQF_NO_SUSPEND,
	.name = "cascade",
};

void __init mach_init_irq(void)
{
	clear_c0_status(ST0_IM | ST0_BEV);

	mips_cpu_irq_init();
	
	/* machine specific irq init */
	ls2k_init_irq();

	set_c0_status(STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP6);

	setup_irq(MIPS_CPU_IRQ_BASE + 2, &cascade_irqaction);
	setup_irq(MIPS_CPU_IRQ_BASE + 3, &cascade_irqaction);
	setup_irq(MIPS_CPU_IRQ_BASE + 5, &cascade_irqaction);

}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
