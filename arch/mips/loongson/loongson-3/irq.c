#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

#include <loongson-pch.h>
#include <linux/reboot.h>

extern struct platform_controller_hub ls2h_pch;
extern struct platform_controller_hub ls7a_pch;
extern struct platform_controller_hub rs780_pch;

extern unsigned long long smp_group[4];
extern void loongson3_ipi_interrupt(struct pt_regs *regs);

int ls3a_msi_enabled = 0;
EXPORT_SYMBOL(ls3a_msi_enabled);
extern unsigned char ls7a_ipi_irq2pos[];
extern unsigned int ls2h_irq2pos[];

int plat_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	if ((loongson_pch == &ls7a_pch && ls7a_ipi_irq2pos[d->irq] < 0) || (loongson_pch == &ls2h_pch && ls2h_irq2pos[d->irq - LS2H_PCH_IRQ_BASE] < 0))
		return -EINVAL;

	if (cpumask_empty(affinity))
		return -EINVAL;

	cpumask_copy(d->affinity, affinity);

	return IRQ_SET_MASK_OK_NOCOPY;
}

#ifdef CONFIG_KVM_GUEST_LS3A3000
#define UNUSED_IPS (CAUSEF_IP1 | CAUSEF_IP0)
#else
#define UNUSED_IPS (CAUSEF_IP5 | CAUSEF_IP4 | CAUSEF_IP1 | CAUSEF_IP0)
#endif

void mach_irq_dispatch(unsigned int pending)
{
	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
#ifdef CONFIG_KVM_GUEST_LS3A3000
	pending = read_c0_cause() & read_c0_status() & ST0_IM;
	if (pending & CAUSEF_IP5)
		loongson_nodecounter_adjust();
	if (pending & CAUSEF_IP4) {
		lsvirt_button_poweroff();
	}
#endif
	if (pending & CAUSEF_IP3)
		loongson_pch->irq_dispatch();
	if (pending & CAUSEF_IP2)
	{
#ifndef CONFIG_KVM_GUEST_LS3A3000
		int cpu = smp_processor_id();
		int irqs, irq, irqs_pci, irq_lpc;

		if(cpu == 0)
		{

			irqs_pci = LOONGSON_INT_ROUTER_ISR(0) & 0xf0;
			irq_lpc = LOONGSON_INT_ROUTER_ISR(0) & 0x400;
			if(irqs_pci)
			{
				while ((irq = ffs(irqs_pci)) != 0) {
					do_IRQ(irq - 1 + SYS_IRQ_BASE);
					irqs_pci &= ~(1 << (irq-1));
				}
			}
			else if(irq_lpc)
			{
				if(ls_lpc_reg_base == LS3_LPC_REG_BASE)
				{
					irqs = ls2h_readl(LS_LPC_INT_ENA) & ls2h_readl(LS_LPC_INT_STS) & 0xfeff;
					if (irqs) {
						while ((irq = ffs(irqs)) != 0) {
							do_IRQ(irq - 1);
							irqs &= ~(1 << (irq-1));
						}
					}
				}

				do_IRQ(LOONGSON_UART_IRQ);
			}
		}
		else
#endif
			do_IRQ(LOONGSON_UART_IRQ);
	}
	if (pending & UNUSED_IPS) {
		printk(KERN_ERR "%s : spurious interrupt\n", __func__);
		spurious_interrupt();
	}
}

static struct irqaction cascade_irqaction = {
	.handler = no_action,
	.flags = IRQF_NO_SUSPEND,
	.name = "cascade",
};

static inline void mask_loongson_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	if(!desc->action)
		return;

	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ) {
		int cpu = smp_processor_id();
		int node_id = cpu_logical_map(cpu) / cores_per_node;
		u64 intenclr_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_INTENCLR);

		*(volatile u32 *)intenclr_addr = 1 << 10;
	}
}

static inline void unmask_loongson_irq(struct irq_data *d)
{
	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ) {
		int cpu = smp_processor_id();
		int node_id = cpu_logical_map(cpu) / cores_per_node;
		u64 intenset_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_INTENSET);

		*(volatile u32 *)intenset_addr = 1 << 10;
	}
}

static inline unsigned int startup_loongson_irq(struct irq_data *d)
{
	return 0;
}

static inline void shutdown_loongson_irq(struct irq_data *d)
{

}

 /* For MIPS IRQs which shared by all cores */
static struct irq_chip loongson_irq_chip = {
	.name		= "Loongson",
	.irq_ack	= mask_loongson_irq,
	.irq_mask	= mask_loongson_irq,
	.irq_mask_ack	= mask_loongson_irq,
	.irq_unmask	= unmask_loongson_irq,
	.irq_eoi	= unmask_loongson_irq,
	.irq_startup	= startup_loongson_irq,
	.irq_shutdown	= shutdown_loongson_irq,
};

void __init mach_init_irq(void)
{
	int i;
	u64 intenset_addr;
	u64 introuter_lpc_addr;

	clear_c0_status(ST0_IM | ST0_BEV);

	mips_cpu_irq_init();
	if (loongson_pch)
		loongson_pch->init_irq();

	/* setup CASCADE irq */
	setup_irq(LOONGSON_BRIDGE_IRQ, &cascade_irqaction);

	irq_set_chip_and_handler(LOONGSON_UART_IRQ,
			&loongson_irq_chip, handle_level_irq);

	for (i = 0; i < nr_nodes_loongson; i++) {
		intenset_addr = smp_group[i] | (u64)(&LOONGSON_INT_ROUTER_INTENSET);
		introuter_lpc_addr = smp_group[i] | (u64)(&LOONGSON_INT_ROUTER_LPC);
		if (i == 0) {
			*(volatile u8 *)introuter_lpc_addr = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
                } else {
			*(volatile u8 *)introuter_lpc_addr = LOONGSON_INT_COREx_INTy(0, 0);
		}
		*(volatile u32 *)intenset_addr = 1 << 10;
	}

#ifndef CONFIG_KVM_GUEST_LS3A3000
	set_c0_status(STATUSF_IP2 | STATUSF_IP6);
#else
	set_c0_status(STATUSF_IP2  | STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6);
#endif
}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
