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
#include <asm/mipsregs.h>
#include <irq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>

static int irqbalance = 1;
module_param(irqbalance, int, 0664);
static unsigned int int_auto[2], int_bounce[2];

#ifdef CONFIG_HOTPLUG_CPU
void handle_irq_affinity(void)
{
	struct irq_desc *desc;
	struct irq_chip *chip;
	unsigned int irq;
	unsigned long flags;

	for_each_active_irq(irq) {
		const struct cpumask *affinity;
		desc = irq_to_desc(irq);

		raw_spin_lock_irqsave(&desc->lock, flags);
		affinity = desc->irq_data.affinity;
		if (cpumask_any_and(affinity, cpu_online_mask) >= nr_cpu_ids) {
			affinity = cpu_online_mask;
		}

		chip = irq_data_get_irq_chip(&desc->irq_data);
		if (chip && chip->irq_set_affinity)
			chip->irq_set_affinity(&desc->irq_data, affinity, true);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

void fixup_irqs(void)
{
	handle_irq_affinity();
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}
#endif

struct ls2k_irq_registers {
	u32 Inten_0;
	u32 Intpol_0;
	u32 Intedge_0;
	u32 Intbounce_0;
	u32 Intauto_0;
	u32 Inten_1;
	u32 Intpol_1;
	u32 Intedge_1;
	u32 Intbounce_1;
	u32 Intauto_1;
	u8  Entry[64];
};
static struct ls2k_irq_registers ls2k_irq_regs;

void ls2k_suspend_irq(void)
{
	unsigned long base;
	int index;

	base = CKSEG1ADDR(CONF_BASE) + INT_LO_OFF;

	ls2k_irq_regs.Inten_0 = ls64_conf_read32((void*)(base + INT_EN_OFF));
	ls2k_irq_regs.Intpol_0 = ls64_conf_read32((void*)(base + INT_PLE_OFF));
	ls2k_irq_regs.Intedge_0 = ls64_conf_read32((void*)(base + INT_EDG_OFF));
	ls2k_irq_regs.Intbounce_0 = ls64_conf_read32((void*)(base + INT_BCE_OFF));
	ls2k_irq_regs.Intauto_0 = ls64_conf_read32((void*)(base + INT_AUTO_OFF));

	ls2k_irq_regs.Inten_1 = ls64_conf_read32((void*)(base + 0x40 + INT_EN_OFF));
	ls2k_irq_regs.Intpol_1 = ls64_conf_read32((void*)(base + 0x40 + INT_PLE_OFF));
	ls2k_irq_regs.Intedge_1 = ls64_conf_read32((void*)(base + 0x40 + INT_EDG_OFF));
	ls2k_irq_regs.Intbounce_1 = ls64_conf_read32((void*)(base + 0x40 + INT_BCE_OFF));
	ls2k_irq_regs.Intauto_1 = ls64_conf_read32((void*)(base + 0x40 + INT_AUTO_OFF));

	for (index = 0; index < 32; index++)
		ls2k_irq_regs.Entry[index] = ls64_conf_read8((void *)(base + index));
	for (index = 0; index < 32; index++)
		ls2k_irq_regs.Entry[index + 32] = ls64_conf_read8((void *)(base + 0x40 + index));
}

void ls2k_resume_irq(void)
{
	unsigned long base;
	int index;

	base = CKSEG1ADDR(CONF_BASE) + INT_LO_OFF;

	ls64_conf_write32(ls2k_irq_regs.Inten_0, (void *)(base + INT_SET_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intpol_0,   (void *)(base + INT_PLE_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intedge_0,  (void *)(base + INT_EDG_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intbounce_0, (void *)(base + INT_BCE_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intauto_0,   (void *)(base + INT_AUTO_OFF));

	ls64_conf_write32(ls2k_irq_regs.Inten_1, (void *)(base + 0x40 + INT_SET_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intpol_1,   (void *)(base + 0x40 + INT_PLE_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intedge_1,  (void *)(base + 0x40 + INT_EDG_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intbounce_1, (void *)(base + 0x40 + INT_BCE_OFF));
	ls64_conf_write32(ls2k_irq_regs.Intauto_1,   (void *)(base + 0x40 + INT_AUTO_OFF));

	for (index = 0; index < 32; index++)
		ls64_conf_write8(ls2k_irq_regs.Entry[index], (void *)(base + index));
	for (index = 0; index < 32; index++)
		ls64_conf_write8(ls2k_irq_regs.Entry[index + 32], (void *)(base + 0x40 + index));
}

static int param_set_intparam(const char *val, struct kernel_param *kp)
{
	unsigned long base;
	int i;
	*(volatile int *)kp->arg = simple_strtoul(val,0,0);
	for(i=0;i<2;i++)
	{
		base = CKSEG1ADDR(CONF_BASE) + i * 0x40 + INT_LO_OFF ;

		ls64_conf_write32(int_auto[i], (void *)(base + INT_AUTO_OFF));
		ls64_conf_write32(int_bounce[i], (void *)(base + INT_BCE_OFF));
	}

	return 0;
}


int param_get_intparam(char *buffer, const struct kernel_param *kp)
{
        /* Y and N chosen as being relatively non-coder friendly */
        return sprintf(buffer, "0x%x", *(unsigned int *)kp->arg);
}
module_param_call(int_auto0, param_set_intparam, param_get_intparam, &int_auto[0], 0644);
module_param_call(int_auto1, param_set_intparam, param_get_intparam, &int_auto[1], 0644);
module_param_call(int_bounce0, param_set_intparam, param_get_intparam, &int_bounce[0], 0644);
module_param_call(int_bounce1, param_set_intparam, param_get_intparam, &int_bounce[1], 0644);



/* ip7 take perf/timer */
/* ip6 take smp */
/* ip5 take off-chip msi irq */
/* ip4 take on-chip irq */
DEFINE_RAW_SPINLOCK(ls2k_irq_lock);
void ls64_ipi_interrupt(struct pt_regs *regs);

void ls_unmask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	unsigned long flags;

	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	chip = data->chip;
	if (data->irq >= LS64_MSI_IRQ_BASE)
		index = data->irq - LS64_MSI_IRQ_BASE;
	else
		index = data->irq - LS2K_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index >= 32) * 0x40 + INT_LO_OFF ;
	ls64_conf_write32(1 << (index & 0x1f), (void *)(base + INT_SET_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

void ls_mask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	unsigned long flags;

	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	chip = data->chip;
	if (data->irq >= LS64_MSI_IRQ_BASE)
		index = data->irq - LS64_MSI_IRQ_BASE;
	else
		index = data->irq - LS2K_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index >= 32) * 0x40 + INT_LO_OFF;
	ls64_conf_write32(1 << (index & 0x1f), (void *)(base + INT_CLR_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

int ls_set_affinity_icu_irq(struct irq_data *data, const struct cpumask *affinity,
		bool force)
{
	cpumask_t tmask;
	unsigned int cpu;
	volatile unsigned char *entry;
	unsigned long *mask;
	unsigned long base;
	unsigned int index;
	int off, sel;
	unsigned long flags;
	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	if (data->irq >= LS64_MSI_IRQ_BASE)
		index = data->irq - LS64_MSI_IRQ_BASE;
	else
		index = data->irq - LS2K_IRQ_BASE;
	off = (index & 0x1f);
	sel = (index >> 5);
	base = CKSEG1ADDR(CONF_BASE) + sel * 0x40 + INT_LO_OFF ;

	cpumask_copy(&tmask, affinity);

	for_each_cpu(cpu, affinity) {
		if (!cpu_online(cpu))
			cpu_clear(cpu, tmask);
	}

	if (cpus_empty(tmask))
		cpu_set(0, tmask);

	cpumask_copy(data->affinity, &tmask);

	mask = cpumask_bits(&tmask);
	*mask = ((*mask&1)?(1<<cpu_logical_map(0)):0)|((*mask&2)?(1<<cpu_logical_map(1)):0);
	entry = (void *)(base  + off);
	switch(*mask&3)
	{
		case 1:
			int_auto[sel] &= ~(1 << off);
			int_bounce[sel] &= ~(1 << off);
			writeb((readb(entry) & 0xf0)|0x01, entry);
			break;
		case 2:
			int_auto[sel] &= ~(1 << off);
			int_bounce[sel] &= ~(1 << off);
			writeb((readb(entry) & 0xf0)|0x02, entry);
			break;
		case 3:
			if(irqbalance&1)
				int_auto[sel] |= (1 << off);
			else
				int_auto[sel] &= ~(1 << off);

			if(irqbalance&2)
				int_bounce[sel] |= (1 << off);
			else
				int_bounce[sel] &= ~(1 << off);

			if(irqbalance == 0)
				int_bounce[sel] |= (1 << off);

			writeb((readb(entry) & 0xf0)|0x03, entry);
			break;
	}

	ls64_conf_write32(int_auto[sel], (void *)(base + INT_AUTO_OFF));
	ls64_conf_write32(int_bounce[sel], (void *)(base + INT_BCE_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);

	return IRQ_SET_MASK_OK_NOCOPY;
}


static struct irq_chip ls64_irq_chip = {
	.name		= "ls64soc",
	/*.irq_ack	= mask_icu_irq,*/
	/*.irq_eoi	= unmask_icu_irq,*/
	.irq_unmask	= ls_unmask_icu_irq,
	.irq_mask	= ls_mask_icu_irq,
	.irq_set_affinity	= ls_set_affinity_icu_irq,
};

extern u64 ls_msi_irq_mask;

asmlinkage void ip7_dispatch(void)
{
	do_IRQ(MIPS_CPU_IRQ_BASE + 7);
}

#ifdef CONFIG_SMP
asmlinkage void ip6_dispatch(void)
{
	ls64_ipi_interrupt(NULL);
}
#endif

asmlinkage void ip5_dispatch(void)
{
	unsigned long irq_status;
	unsigned long hi, lo;
	int i = (read_c0_ebase() & 0x3ff);
	unsigned long addr = CKSEG1ADDR(CONF_BASE);


	hi = ls64_conf_read32((void*)(addr + INTSR1_OFF + (i << 8)));
	lo = ls64_conf_read32((void*)(addr + INTSR0_OFF + (i << 8)));
	irq_status = ((hi << 32) | lo) & ls_msi_irq_mask;

	if ((i = __fls(irq_status)) != -1)
		do_IRQ(i + LS64_MSI_IRQ_BASE);
	else
		spurious_interrupt();

}

asmlinkage void ip4_dispatch(void)
{
	unsigned long irq_status, irq_masked;
	unsigned long hi, lo;
	int i = (read_c0_ebase() & 0x3ff);
	unsigned long addr = CKSEG1ADDR(CONF_BASE);

	hi = ls64_conf_read32((void*)(addr + INTSR1_OFF + (i << 8)));
	lo = ls64_conf_read32((void*)(addr + INTSR0_OFF + (i << 8)));
	irq_status = ((hi << 32) | lo);

	hi = ls64_conf_read32((void*)(addr + INT_HI_OFF + INT_EN_OFF));
	lo = ls64_conf_read32((void*)(addr + INT_LO_OFF + INT_EN_OFF));
	irq_masked = ((hi << 32) | lo);

	lo = (irq_status & irq_masked & ~ls_msi_irq_mask);

	if ((i = __fls(lo)) != -1)
		do_IRQ(i + LS2K_IRQ_BASE);
	else
		spurious_interrupt();
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int cp0_cause;
	unsigned int cp0_status;
	unsigned int cp0_cause_saved;
	cp0_cause_saved = read_c0_cause() & ST0_IM ;
	cp0_status = read_c0_status();
	cp0_cause = cp0_cause_saved & cp0_status;


	if (cp0_cause & STATUSF_IP7)
		ip7_dispatch();

#ifdef CONFIG_SMP
	else if (cp0_cause & STATUSF_IP6)
		ip6_dispatch();

#endif
	else if (cp0_cause & STATUSF_IP5)
		ip5_dispatch();
	else
		ip4_dispatch();

}

void set_irq_attr(int irq, unsigned int imask, unsigned int core_mask, int mode)
{
	unsigned int index;
	unsigned long base;
	unsigned int ret;
	int hi;
	int au;
	int bounce;

	if (irq >= LS64_MSI_IRQ_BASE)
		index = irq - LS64_MSI_IRQ_BASE;
	else
		index = irq - LS2K_IRQ_BASE;

	hi = (index >= 32);
	index = index & 0x1f;
	au = (mode & 0x1);
	bounce = ((mode >> 1) & 0x1);
	base = CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + hi * 0x40;

	ls64_conf_write8(imask << 4 | core_mask, (void *)(base + index));

	ret = ls64_conf_read32((void*)(base + INT_BCE_OFF)) & ~(1 << index);
	ret |= (bounce << index);
	ls64_conf_write32(ret, (void *)(base + INT_BCE_OFF));

	ret = ls64_conf_read32((void*)(base + INT_AUTO_OFF)) & ~(1 << index);
	ret |= (au << index);
	ls64_conf_write32(ret, (void *)(base + INT_AUTO_OFF));
}

void __init setup_irq_default(void)
{
	unsigned int i;
	int core_id = (read_c0_ebase() & 0x3ff);

	for (i = LS2K_IRQ_BASE; i < LS2K_IRQ_BASE + 64; i++) {
		irq_set_chip_and_handler(i, &ls64_irq_chip, handle_level_irq);
		set_irq_attr(i, 1 << (STATUSB_IP4 - 10), 1 << core_id, 0);
	}

	ls64_conf_write32(ls_msi_irq_mask, (void *)(CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + INT_EDG_OFF ));
	ls64_conf_write32((ls_msi_irq_mask >> 32)|(0x1f << 12), (void *)(CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + INT_EDG_OFF + 0x40));
}

static void __init setup_irq_mode(void)
{
	if (cpu_has_vint) {
		set_vi_handler(7, ip7_dispatch);
#ifdef CONFIG_SMP
		set_vi_handler(6, ip6_dispatch);
#endif
		set_vi_handler(5, ip5_dispatch);
		set_vi_handler(4, ip4_dispatch);

	}
}

void  __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	set_c0_status(STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6);
	setup_irq_default();
	setup_irq_mode();
}
