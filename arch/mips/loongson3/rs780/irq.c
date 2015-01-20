#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

extern void loongson3_ipi_interrupt(struct pt_regs *regs);

static void ht_irqdispatch(void)
{
	unsigned int i, irq;
	unsigned int ht_irq[] = {0, 1, 3, 4, 5, 6, 7, 8, 12, 14, 15};

	irq = LOONGSON_HT1_INT_VECTOR(0);
	LOONGSON_HT1_INT_VECTOR(0) = irq;

	for (i = 0; i < (sizeof(ht_irq) / sizeof(*ht_irq)); i++) {
		if (irq & (0x1 << ht_irq[i]))
			do_IRQ(ht_irq[i]);
	}
}

void rs780_irq_dispatch(unsigned int pending)
{
	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	else if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
	else if (pending & CAUSEF_IP3)
		ht_irqdispatch();
	else if (pending & CAUSEF_IP2)
		do_IRQ(LOONGSON_UART_IRQ);
	else {
		printk(KERN_ERR "%s : spurious interrupt\n", __func__);
		spurious_interrupt();
	}
}

void irq_router_init(void)
{
	int i;

	/* route LPC int to cpu core0 int 0 */
	LOONGSON_INT_ROUTER_LPC = LOONGSON_INT_COREx_INTy(boot_cpu_id, 0);
	/* route HT1 int0 ~ int7 to cpu core0 INT1*/
	for (i = 0; i < 8; i++)
		LOONGSON_INT_ROUTER_HT1(i) = LOONGSON_INT_COREx_INTy(boot_cpu_id, 1);
	/* enable HT1 interrupt */
	LOONGSON_HT1_INTN_EN(0) = 0xffffffff;
	/* enable router interrupt intenset */
	LOONGSON_INT_ROUTER_INTENSET = LOONGSON_INT_ROUTER_INTEN | (0xffff << 16) | 0x1 << 10;
}

void __init rs780_init_irq(void)
{
	clear_c0_status(ST0_IM | ST0_BEV);

	irq_router_init();
	init_i8259_irqs();

}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
