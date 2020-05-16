#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

#include <loongson-pch.h>
#include <linux/reboot.h>

/*
 * Tracking total msi num of system.
 * */
unsigned int ls3a_num_msi_irqs = LS3A_NUM_MSI_IRQS;

/* Maximum 26 IPI irqs */
#define PCH_DIRQS 26

/*
 * ls3a_ipi_irq2pos:
 *
 * Array for mapping from io vector(0-255) in HT or EXT of cpu to
 * bit position(0-25) in ls3a_ipi_in_use. The system io irqs
 * are mapped into io vector as following:
 *
 * io vector	irq
 * ------------------------------------------
 * 000-063	064-127(7A IOPIC)
 * 064-127	128-191(MSI 3A3000/4000)
 * 128-255	192-319(MSI 3A4000)
 *
 * other irqs:
 * 000-015:	lpc
 * 016-063:	cpu/core
 * */
unsigned char ls3a_ipi_irq2pos[LS3A_MAX_IO_IRQ] = { [0 ... LS3A_MAX_IO_IRQ-1] = -1 };

/*
 * ls3a_ipi_pos2irq:
 *
 * Array for mapping from position in ls3a_ipi_in_use to io vector.
 * */
unsigned short ls3a_ipi_pos2irq[64] = { [0 ... 63] = -1 };

/*
 * ls3a_ipi_in_use:
 *
 * Bitmap for tracking io ipi using.
 * */
DECLARE_BITMAP(ls3a_ipi_in_use, PCH_DIRQS);

/*
 * ls3a_msi_irq_in_use:
 *
 * Bitmap for tracking msi using.
 * */
DECLARE_BITMAP(ls3a_msi_irq_in_use, LS3A_MAX_MSI_IRQ);

/*
 * convertion between msi and position in ls3a_msi_irq_in_use
 *
 * msi - pos = LS3A_MSI_SUB_VECTOR
 * */
#define LOCAL_MSI2POS(irq)	(irq - LS3A_MSI_SUB_VECTOR)
#define LOCAL_POS2MSI(pos)	(pos + LS3A_MSI_SUB_VECTOR)

/*
 * cpu_for_irq:
 *
 * Array for tracking affinitive cpu handling the irq.
 *
 * */
unsigned int cpu_for_irq[LS3A_MAX_IO_IRQ] = {[0 ... LS3A_MAX_IO_IRQ-1] = -1};

extern struct platform_controller_hub ls2h_pch;
extern struct platform_controller_hub ls7a_pch;
extern struct platform_controller_hub rs780_pch;

extern unsigned long long smp_group[4];
extern enum irq_chip_model msi_chip_model;
extern bool need_cpu_uart1;
int ls3a_msi_enabled = 0;
EXPORT_SYMBOL(ls3a_msi_enabled);
extern unsigned int ls2h_irq2pos[];
extern void loongson3_ipi_interrupt(struct pt_regs *regs);

unsigned int ext_ini_en[MAX_32ARRAY_SIZE];

static DEFINE_SPINLOCK(affinity_lock);

static DEFINE_SPINLOCK(pch_irq_lock);

int pch_create_dirq(unsigned int irq)
{
	unsigned long flags;
	int pos;
	spin_lock_irqsave(&pch_irq_lock, flags);
again:
	pos = find_first_zero_bit(ls3a_ipi_in_use, PCH_DIRQS);
	if(pos == PCH_DIRQS)
	{
		spin_unlock_irqrestore(&pch_irq_lock, flags);
		return -ENOSPC;
	}
	if (test_and_set_bit(pos, ls3a_ipi_in_use))
		goto again;
	ls3a_ipi_pos2irq[pos] = irq;
	ls3a_ipi_irq2pos[LS3A_IOIRQ2VECTOR(irq)] = pos;
	spin_unlock_irqrestore(&pch_irq_lock, flags);
	return 0;
}

void pch_destroy_dirq(unsigned int irq)
{
	unsigned long flags;
	int pos;
	spin_lock_irqsave(&pch_irq_lock, flags);
	pos = ls3a_ipi_irq2pos[LS3A_IOIRQ2VECTOR(irq)];

	if(pos >= 0)
	{
		clear_bit(pos, ls3a_ipi_in_use);
		ls3a_ipi_irq2pos[LS3A_IOIRQ2VECTOR(irq)] = -1;
		ls3a_ipi_pos2irq[pos] = -1;
	}
	spin_unlock_irqrestore(&pch_irq_lock, flags);
}

static DEFINE_SPINLOCK(lock);
/*
 * Dynamic irq allocate and deallocation
 */
int ls3a_create_msi_irq(unsigned char need_ipi)
{
	int irq, pos;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
again:
	pos = find_first_zero_bit(ls3a_msi_irq_in_use, LS3A_NUM_MSI_IRQ);
	if(pos == LS3A_NUM_MSI_IRQ) {
		spin_unlock_irqrestore(&lock, flags);
		return -ENOSPC;
	}

	irq = LOCAL_POS2MSI(pos);
	if(need_ipi) pch_create_dirq(irq);
	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, ls3a_msi_irq_in_use))
		goto again;
	spin_unlock_irqrestore(&lock, flags);

	dynamic_irq_init(irq);

	return irq;
}

void ls3a_destroy_msi_irq(unsigned int irq, unsigned char need_ipi)
{
	int pos = LOCAL_MSI2POS(irq);

	if(need_ipi) pch_destroy_dirq(irq);
	dynamic_irq_cleanup(irq);

	clear_bit(pos, ls3a_msi_irq_in_use);
}

void any_send(unsigned int off, unsigned int data, unsigned int data_mask, unsigned int node)
{
	unsigned long data64 = 0;
	unsigned int blk_bit_set = (unsigned int)(1 << LS_ANYSEND_BLOCK_SHIFT);

	data64 = blk_bit_set | off;
	data64 |= (node << LS_ANYSEND_NODE_SHIFT);
	data64 |= (data_mask << LS_ANYSEND_MASK_SHIFT);
	data64 |= ((unsigned long)data << LS_ANYSEND_DATA_SHIFT);
	dwrite_csr(LOONGSON_ANY_SEND_OFFSET, data64);
}

static void set_irq_route(int pos, const struct cpumask *affinity)
{
	int pos_off;
	unsigned int nodemap, route_node, data_byte, data_mask;
	unsigned char coremap[MAX_NUMNODES];
	unsigned long data = 0;
	int cpu, node;

	pos_off = pos & (~3);
	data_byte = pos & (3);
	data_mask = ~BIT_MASK(data_byte) & 0xf;
	nodemap = 0;
	memset(coremap, 0, sizeof(unsigned char) * MAX_NUMNODES);

	/* calculate nodemap and coremap of target irq */
	for_each_cpu(cpu, affinity) {
		node = __cpu_logical_map[cpu] / cores_per_node;
		nodemap |= (1 << node);
		coremap[node] |= (1 << (__cpu_logical_map[cpu] % cores_per_node));
	}


	for_each_online_node(node) {

		data = 0;
		/* Node 0 is in charge of inter-node interrupt dispatch */
		route_node = (node == 0) ? nodemap : (1 << node);
		data |= ((coremap[node] | (route_node << LS_IOI_CPUNODE_SHIFT_IN_ROUTE))\
			 << (LS_ANYSEND_ROUTE_DATA_POS(data_byte)));
		any_send(LOONGSON_EXT_IOI_ROUTE_OFFSET + pos_off, data, data_mask, node);
	}

}


int ext_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	unsigned long flags;
	unsigned short pos_off;
	unsigned int vector;
	struct cpumask tmp;
	unsigned int node;

	if (!config_enabled(CONFIG_SMP))
		return -EPERM;

	if (d->irq < LS7A_IOAPIC_IRQ_BASE || d->irq >= LS3A_MSI_SUB_VECTOR + LS3A_NUM_MSI_IRQ)
		return -EINVAL;

	spin_lock_irqsave(&affinity_lock, flags);

	if (cpumask_empty(affinity)) {
		spin_unlock_irqrestore(&affinity_lock, flags);
		return -EINVAL;
	}

	if (!cpumask_intersects(affinity, cpu_online_mask)) {
		spin_unlock_irqrestore(&affinity_lock, flags);
		return -EINVAL;
	}

	cpumask_and(&tmp, affinity, cpu_online_mask);
	/*
	 * control interrupt enable or disalbe through boot cpu
	 * which is reponsible for dispatching interrupts.
	 * */
	vector = LS3A_IOIRQ2VECTOR(d->irq);
	pos_off = vector >> 5;

	node = loongson_boot_cpu_id / cores_per_node;
	any_send(LOONGSON_EXT_IOI_EN64_OFFSET + (pos_off << 2), ext_ini_en[pos_off] & (~((1 << (vector & 0x1F)))), 0, node);
	set_irq_route(vector, &tmp);
	any_send(LOONGSON_EXT_IOI_EN64_OFFSET + (pos_off << 2), ext_ini_en[pos_off], 0, node);

	cpumask_copy(d->affinity, &tmp);
	spin_unlock_irqrestore(&affinity_lock, flags);

	return IRQ_SET_MASK_OK_NOCOPY;

}

int plat_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	if ((loongson_pch == &ls7a_pch && ls3a_ipi_irq2pos[LS3A_IOIRQ2VECTOR(d->irq)] == 255) || (loongson_pch == &ls2h_pch && ls2h_irq2pos[d->irq - LS2H_PCH_IRQ_BASE] == 0))
		return -EINVAL;

	if (!config_enabled(CONFIG_SMP))
		return -EPERM;

	if (d->irq < LS7A_IOAPIC_IRQ_BASE || d->irq >= LS3A_MSI_SUB_VECTOR + LS3A_NUM_MSI_IRQ)
		return -EINVAL;

	if (cpumask_empty(affinity)) {
		return -EINVAL;
	}

	if (!cpumask_intersects(affinity, cpu_online_mask)) {
		return -EINVAL;
	}

	cpumask_and(d->affinity, affinity, cpu_online_mask);

	return IRQ_SET_MASK_OK_NOCOPY;
}

#define UNUSED_IPS_GUEST (CAUSEF_IP0)
#ifdef CONFIG_LOONGSON_SUPPORT_IP5
#define UNUSED_IPS (CAUSEF_IP4 | CAUSEF_IP0)
#define LOONGSON_INT_ROUTER_INTSR	LOONGSON3_REG32(LOONGSON3_REG_BASE, LOONGSON_INT_ROUTER_OFFSET + 0x20)

void loongson_ip5_dispatch(void)
{
	int irq, irqs;

	irqs = LOONGSON_INT_ROUTER_INTSR & 0xff0000;

	while((irq = ffs(irqs)) != 0){
		irq = irq - 1;
		irqs &= ~(1 << irq);
		do_IRQ(LOONGSON_SUPPORT_IP5_BASE_IRQ + irq - 16);
	}
}

#else
#define UNUSED_IPS (CAUSEF_IP5 | CAUSEF_IP4 | CAUSEF_IP0)
#endif
static void mach_guest_irq_dispatch(unsigned int pending)
{
	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
	pending = read_c0_cause() & read_c0_status() & ST0_IM;
	if (pending & CAUSEF_IP1){
		#ifdef CONFIG_GS464V_STABLE_COUNTER
		if((current_cpu_type() == CPU_LOONGSON3_COMP) && stable_timer_enabled)
			loongson_stablecounter_adjust();
		else
		#endif
			loongson_nodecounter_adjust();
	}
	if (pending & CAUSEF_IP3)
		loongson_pch->irq_dispatch();
	if (pending & CAUSEF_IP2)
		do_IRQ(LOONGSON_UART_IRQ);

	if (pending & UNUSED_IPS_GUEST) {
		printk(KERN_ERR "%s : spurious interrupt\n", __func__);
		spurious_interrupt();
	}
}

asmlinkage void ip7_dispatch(void)
{
	do_IRQ(LOONGSON_TIMER_IRQ);
}

#ifdef CONFIG_SMP
asmlinkage void ip6_dispatch(void)
{
	loongson3_ipi_interrupt(NULL);
}
#endif

asmlinkage void ip2_dispatch(void)
{
	int cpu = cpu_logical_map(smp_processor_id());
	int irqs, irq, irqs_pci, irq_lpc, irq_uart1;

	if(cpu == loongson_boot_cpu_id)
	{
		int core_index = loongson_boot_cpu_id % cores_per_node;
		int isr = ls64_conf_read32(LS_IRC_ISR(core_index));
		irqs_pci = isr & 0xf0;
		if(irqs_pci)
		{
			while ((irq = ffs(irqs_pci)) != 0) {
				do_IRQ(irq - 1 + SYS_IRQ_BASE);
				irqs_pci &= ~(1 << (irq-1));
			}
		}

		irq_lpc = isr & 0x400;
		if(irq_lpc)
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

		irq_uart1 = isr & 0x8000;
		if(irq_uart1) {
			do_IRQ(LOONGSON_UART1_IRQ);
		}
	}
	else
		do_IRQ(LOONGSON_UART_IRQ);
}

void mach_irq_dispatch(unsigned int pending)
{
	if(cpu_guestmode)
		return mach_guest_irq_dispatch(pending);

	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
#ifdef CONFIG_LOONGSON_SUPPORT_IP5
	if (pending & CAUSEF_IP5)
		loongson_ip5_dispatch();
#endif
	if (pending & CAUSEF_IP3)
		loongson_pch->irq_dispatch();
	if (pending & CAUSEF_IP2)
	{
		int cpu = cpu_logical_map(smp_processor_id());
		int irqs, irq, irqs_pci, irq_lpc, irq_uart1;

		if(cpu == loongson_boot_cpu_id)
		{
			int core_index = loongson_boot_cpu_id % cores_per_node;
			int isr = ls64_conf_read32(LS_IRC_ISR(core_index));
			irqs_pci = isr & 0xf0;
			if(irqs_pci)
			{
				while ((irq = ffs(irqs_pci)) != 0) {
					do_IRQ(irq - 1 + SYS_IRQ_BASE);
					irqs_pci &= ~(1 << (irq-1));
				}
			}

			irq_lpc = isr & 0x400;
			if(irq_lpc)
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

			irq_uart1 = isr & 0x8000;
			if(irq_uart1) {
				do_IRQ(LOONGSON_UART1_IRQ);
			}
		}
		else
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

#ifdef CONFIG_LOONGSON_SUPPORT_IP5
	if(!desc->action)
		return;

	if ((d->irq != LOONGSON_UART_IRQ) &&
	    (d->irq != LOONGSON_UART1_IRQ) &&
	    (d->irq < LOONGSON_SUPPORT_IP5_BASE_IRQ))
		return;
	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ)
		ls64_conf_write32((1 << 10), LS_IRC_ENCLR);
	else if (d->irq == LOONGSON_UART1_IRQ)
		ls64_conf_write32((1 << 15), LS_IRC_ENCLR);
	else
		ls64_conf_write32(1 << (16 + d->irq - LOONGSON_SUPPORT_IP5_BASE_IRQ), LS_IRC_ENCLR);
#else
	if(!desc->action)
		return;

	if (d->irq == LOONGSON_UART_IRQ)
		ls64_conf_write32((1 << 10), LS_IRC_ENCLR);
	else if (d->irq == LOONGSON_UART1_IRQ)
		ls64_conf_write32((1 << 15), LS_IRC_ENCLR);

#endif
}

static inline void unmask_loongson_irq(struct irq_data *d)
{
#ifdef CONFIG_LOONGSON_SUPPORT_IP5
	if ((d->irq != LOONGSON_UART_IRQ) &&
	    (d->irq != LOONGSON_UART1_IRQ) &&
	    (d->irq < LOONGSON_SUPPORT_IP5_BASE_IRQ))
		return;
	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ)
		ls64_conf_write32((1 << 10), LS_IRC_ENSET);
	else if (d->irq == LOONGSON_UART1_IRQ)
		ls64_conf_write32((1 << 15), LS_IRC_ENSET);
	else
		ls64_conf_write32(1 << (16 + d->irq - LOONGSON_SUPPORT_IP5_BASE_IRQ), LS_IRC_ENSET);
#else
	if (d->irq == LOONGSON_UART_IRQ)
		ls64_conf_write32((1 << 10), LS_IRC_ENSET);
	else if (d->irq == LOONGSON_UART1_IRQ)
		ls64_conf_write32((1 << 15), LS_IRC_ENSET);

#endif
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

static void set_irq_mode(void)
{
	if(cpu_has_vint) {
		set_vi_handler(7, ip7_dispatch);
		set_vi_handler(6, ip6_dispatch);
		set_vi_handler(2, ip2_dispatch);
	}
}

void __init mach_init_irq(void)
{
	unsigned data;

	clear_c0_status(ST0_IM | ST0_BEV);

	if ((current_cpu_type() == CPU_LOONGSON3_COMP)) {
		ls3a_num_msi_irqs = LS3A_MAX_MSI_IRQ;
	}

	mips_cpu_irq_init();
	if (loongson_pch)
		loongson_pch->init_irq();

	/* setup CASCADE irq */
	setup_irq(LOONGSON_BRIDGE_IRQ, &cascade_irqaction);

	irq_set_chip_and_handler(LOONGSON_UART_IRQ,
			&loongson_irq_chip, handle_level_irq);
	data = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
	ls64_conf_write8(data, LS_IRC_ENT_LPC);
	data = ls64_conf_read32(LS_IRC_EN);
	data |= (1 << 10);
	ls64_conf_write32(data, LS_IRC_ENSET);

	if (need_cpu_uart1) {
		irq_set_chip_and_handler(LOONGSON_UART1_IRQ,
				&loongson_irq_chip, handle_level_irq);
		data = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
		ls64_conf_write8(data, LS_IRC_ENT_UART1);
		data = ls64_conf_read32(LS_IRC_EN);
		data |= (1 << 15);
		ls64_conf_write32(data, LS_IRC_ENSET);
	}

#ifdef CONFIG_LOONGSON_SUPPORT_IP5
	if (current_cpu_type() == CPU_LOONGSON3_COMP) {
		int i;
		if (((*(volatile unsigned int *)0x900000003ff00404) & 0xf00000) != 0xf00000)
			(*(volatile unsigned int *)0x900000003ff00404) |= 0xf00000;

		for (i = 0; i < LOONGSON_SUPPORT_IP5_IRQ_NUM; i++) {
			irq_set_chip_and_handler(LOONGSON_SUPPORT_IP5_BASE_IRQ + i,
				&loongson_irq_chip, handle_level_irq);

			data = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 3);
			ls64_conf_write8(data, LS_IRC_ENT_HT0(i));
			data = ls64_conf_read32(LS_IRC_EN);
			data |= (1 << (i + 16));
			ls64_conf_write32(data, LS_IRC_ENSET);
		}
	}
#endif

	if(cpu_guestmode)
		set_c0_status(STATUSF_IP1 | STATUSF_IP2 |
					    STATUSF_IP5 | STATUSF_IP6);
	else
#ifdef CONFIG_LOONGSON_SUPPORT_IP5
		set_c0_status(STATUSF_IP2 | STATUSF_IP5 | STATUSF_IP6);
#else
		set_c0_status(STATUSF_IP2 | STATUSF_IP6);
#endif

	set_irq_mode();
}

void fixup_irq_route(bool online)
{
	unsigned char val;
	unsigned int phy_cpu = __cpu_logical_map[smp_processor_id()];

	if (msi_chip_model != ICM_PCI_MSI_EXT && \
			loongson_pch->board_type == LS7A) {
		if (phy_cpu < cores_per_node) {
			if (online) {
				val = ls64_conf_read8(LS_IRC_ENT_HT1(2));
				val |= (1 << phy_cpu);
				ls64_conf_write8(val, LS_IRC_ENT_HT1(2));

				val = ls64_conf_read8(LS_IRC_ENT_HT1(3));
				val |= (1 << phy_cpu);
				ls64_conf_write8(val, LS_IRC_ENT_HT1(3));
			} else {
				val = ls64_conf_read8(LS_IRC_ENT_HT1(2));
				val &= (~(1 << phy_cpu));
				ls64_conf_write8(val, LS_IRC_ENT_HT1(2));

				val = ls64_conf_read8(LS_IRC_ENT_HT1(3));
				val &= (~(1 << phy_cpu));
				ls64_conf_write8(val, LS_IRC_ENT_HT1(3));
			}
		}
	}
}

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
	fixup_irq_route(false);
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
