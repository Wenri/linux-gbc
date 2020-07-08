#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/syscore_ops.h>
#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

#include <loongson-pch.h>

#define USE_7A_INT0 0x1
#define USE_7A_INT1 0x2

static volatile unsigned long long *irq_status = (volatile unsigned long long *)((LS7A_IOAPIC_INT_STATUS));
static volatile unsigned long long *irq_mask   = (volatile unsigned long long *)((LS7A_IOAPIC_INT_MASK  ));
static volatile unsigned long long *irq_edge   = (volatile unsigned long long *)((LS7A_IOAPIC_INT_EDGE  ));
static volatile unsigned long long *irq_clear  = (volatile unsigned long long *)((LS7A_IOAPIC_INT_CLEAR ));
static volatile unsigned long long *irq_pol  = (volatile unsigned long long *)((LS7A_IOAPIC_INT_POL ));
static volatile unsigned long long *irq_msi_en = (volatile unsigned long long *)((LS7A_IOAPIC_HTMSI_EN  ));
static volatile unsigned char *irq_msi_vec = (volatile unsigned char *)((LS7A_IOAPIC_HTMSI_VEC ));
enum irq_chip_model msi_chip_model = ICM_PCI_MSI;
extern unsigned long long smp_group[4];
static irqreturn_t lpc_irq_handler(int irq, void *data);
extern struct plat_smp_ops *mp_ops;
extern int ext_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity, bool force);
extern struct irq_chip ls7a_msi_chip;
extern int ls3a_msi_enabled;
extern unsigned int ext_ini_en[MAX_32ARRAY_SIZE];
extern int pch_create_dirq(unsigned int irq);
extern void pch_destroy_dirq(unsigned int irq);
extern unsigned char ls3a_ipi_irq2pos[LS3A_MAX_IO_IRQ];
extern unsigned short ls3a_ipi_pos2irq[64];

static void mask_pch_irq(struct irq_data *d);
static void unmask_pch_irq(struct irq_data *d);

static DEFINE_SPINLOCK(irqchip_lock);
unsigned int startup_pch_irq_comp(struct irq_data *d)
{
	unmask_pch_irq(d);
	return 0;
}

unsigned int startup_pch_irq(struct irq_data *d)
{
	pch_create_dirq(d->irq);
	unmask_pch_irq(d);
	return 0;
}

void shutdown_pch_irq(struct irq_data *d)
{
	mask_pch_irq(d);
	pch_destroy_dirq(d->irq);
}

static void mask_pch_irq(struct irq_data *d)
{
	unsigned long flags;

	unsigned long irq_nr = d->irq;

	spin_lock_irqsave(&irqchip_lock, flags);

	*irq_mask |= (1ULL << LS3A_IOIRQ2VECTOR(irq_nr));

	spin_unlock_irqrestore(&irqchip_lock, flags);
}

static void unmask_pch_irq(struct irq_data *d)
{
	unsigned long flags;

	unsigned long irq_nr = d->irq;

	spin_lock_irqsave(&irqchip_lock, flags);

	if(ls3a_msi_enabled)
		*irq_clear = 1ULL << LS3A_IOIRQ2VECTOR(irq_nr);
	*irq_mask &= ~(1ULL << LS3A_IOIRQ2VECTOR(irq_nr));

	spin_unlock_irqrestore(&irqchip_lock, flags);
}

static struct irq_chip pch_irq_chip = {
	.name        = "LS7A-IOAPIC",
	.irq_mask    = mask_pch_irq,
	.irq_unmask    = unmask_pch_irq,
	.irq_startup	= startup_pch_irq,
	.irq_shutdown	= shutdown_pch_irq,
	.irq_set_affinity = plat_set_irq_affinity,
};

static struct irq_chip ext_irq_chip = {
	.name		= "LS7A-IOAPIC-EXT",
	.irq_mask	= mask_pch_irq,
	.irq_unmask	= unmask_pch_irq,
	.irq_startup	= startup_pch_irq_comp,
	.irq_shutdown	= mask_pch_irq,
	.irq_set_affinity = ext_set_irq_affinity,
};

extern unsigned int cpu_for_irq[LS3A_MAX_IO_IRQ];

void ext_handle_irqs(unsigned long long irqs, int i) {
	unsigned int  irq;
	if (i == 0) {
		if(irqs & (1 << LS7A_IOAPIC_LPC_OFFSET))
		{
			lpc_irq_handler(0, 0);
			irqs &= ~(1 << LS7A_IOAPIC_LPC_OFFSET);
			*irq_clear = (1 << LS7A_IOAPIC_LPC_OFFSET);
		}
	}

	while(irqs){
		irq = __ffs(irqs);
		irqs &= ~(1ULL<<irq);

		do_IRQ(LS3A_VECTOR2IOIRQ(irq + (i << 6)));
	}
}

void handle_irqs(unsigned long long irqs, int i) {
	unsigned int  irq;
	struct irq_data *irqd;
	struct cpumask affinity;
	int cpu = smp_processor_id();

	if((i == 0) && irqs & (1 << LS7A_IOAPIC_LPC_OFFSET))
	{
		lpc_irq_handler(0, 0);
		irqs &= ~(1 << LS7A_IOAPIC_LPC_OFFSET);
		*irq_clear = (1 << LS7A_IOAPIC_LPC_OFFSET);
	}

	while(irqs){
		irq = __ffs(irqs);
		irqs &= ~(1ULL<<irq);
		irq += (i << 6);

		/* handled by local core */
		if (ls3a_ipi_irq2pos[irq] == (unsigned char)-1) {
			do_IRQ(LS3A_VECTOR2IOIRQ(irq));
			continue;
		}

		irqd = irq_get_irq_data(LS3A_VECTOR2IOIRQ(irq));
		cpumask_and(&affinity, irqd->affinity, cpu_active_mask);
		if (cpumask_empty(&affinity)) {
			do_IRQ(LS3A_VECTOR2IOIRQ(irq));
			continue;
		}

		cpu_for_irq[irq] = cpumask_next(cpu_for_irq[irq], &affinity);
		if (cpu_for_irq[irq] >= nr_cpu_ids)
			cpu_for_irq[irq] = cpumask_first(&affinity);

		if (cpu_for_irq[irq] == cpu) {
			do_IRQ(LS3A_VECTOR2IOIRQ(irq));
			continue;
		}

		/* balanced by other cores */
		mp_ops->send_ipi_single(cpu_for_irq[irq], (0x1 << (ls3a_ipi_irq2pos[irq])) << IPI_IRQ_OFFSET);
	}
}

asmlinkage void ls7a_irq_dispatch(void)
{
	unsigned long flags;
	volatile unsigned long long intmask;

	/* read irq status register */
	unsigned long long intstatus = *irq_status;

	spin_lock_irqsave(&irqchip_lock, flags);
	*irq_mask |= intstatus;
	intmask = *irq_mask;
	spin_unlock_irqrestore(&irqchip_lock, flags);

	handle_irqs(intstatus, 0);
}

void ls7a_msi_irq_dispatch(void)
{
	unsigned int i;
	unsigned long long irqs;
	int cpuid = get_core_id();
	unsigned int ls3a_irq_cpu = ls64_conf_read32(LS_IRC_ISR(cpuid));

	for(i = 0; i < 4; i++) {
		if(ls3a_irq_cpu & (0x3 << (24 + (i << 1)))) {
			irqs = LOONGSON_HT1_INT_VECTOR64(i);
			LOONGSON_HT1_INT_VECTOR64(i) = irqs;

			handle_irqs(irqs, i);
		}
	}
}

void ls7a_ext_irq_dispatch(void)
{
	unsigned int i;
	unsigned long long  irqs;
	for(i = 0; i < 4; i++) {
		irqs = dread_csr(LOONGSON_EXT_IOI_COREISR64_OFFSET + (i << 3));
		dwrite_csr(LOONGSON_EXT_IOI_COREISR64_OFFSET + (i << 3), irqs);

		ext_handle_irqs(irqs, i);
	}
}

static void init_irq_route(int dev, int irq)
{
	*(volatile unsigned char *)(LS7A_IOAPIC_ROUTE_ENTRY + dev) = USE_7A_INT0;
	if (ls3a_msi_enabled) {
		*irq_msi_en |= 1ULL << dev;
		*(volatile unsigned char *)(irq_msi_vec + dev) = LS3A_IOIRQ2VECTOR(irq);
	}
}

static void init_7a_irq(int dev, int irq, struct irq_chip *pirq_chip) {
	init_irq_route(dev, irq);
	irq_set_chip_and_handler(irq, pirq_chip, handle_level_irq);
}

static void ext_ioi_init(void)
{
	int i;
	unsigned int data = 0;
	unsigned int tmp, node;

	tmp = (unsigned int)(read_csr(LS_ANYSEND_OTHER_FUNC_OFFSET) | LS_ANYSEND_OTHER_FUNC_EXT_IOI);

	/* init irq en bitmap */
	for (i = 0; i < MAX_32ARRAY_SIZE; i++) {
		ext_ini_en[i] = LS_ANYSEND_IOI_EN32_DATA;
	}

	for_each_online_node(node) {
		any_send(LS_ANYSEND_OTHER_FUNC_OFFSET, tmp, 0, node);

		for(i = 0; i < LS_ANYSEND_IOI_NODEMAP_ITEMS; i++) {
			data = ((((i << 1) + 1) << LS_IOI_NODEMAP_BITS_PER_ENTRY) | (i << 1));
			any_send(LOONGSON_EXT_IOI_NODEMAP_OFFSET + i*4, data, 0, node);
		}

		EXT_IOI_REGS_INIT(LS_ANYSEND_IOI_EN_ITEMS, LOONGSON_EXT_IOI_EN64_OFFSET, LS_ANYSEND_IOI_EN32_DATA, node);
		EXT_IOI_REGS_INIT(LS_ANYSEND_IOI_IPMAP_ITEMS, LOONGSON_EXT_IOI_MAP_OFFSET, LS_ANYSEND_IOI_IPMAP_DATA, node);
		EXT_IOI_REGS_INIT(LS_ANYSEND_IOI_ROUTE_ITEMS, LOONGSON_EXT_IOI_ROUTE_OFFSET, LS_ANYSEND_IOI_ROUTE_DATA, node);
		EXT_IOI_REGS_INIT(LS_ANYSEND_IOI_BOUNCE_ITEMS, LOONGSON_EXT_IOI_BOUNCE64_OFFSET, LS_ANYSEND_IOI_BOUNCE_DATA, node);
	}
	loongson_pch->irq_dispatch = ls7a_ext_irq_dispatch;

}

void init_7a_irqs(struct irq_chip *pirq_chip)
{
	/*lpc irq is level trigged*/
	*irq_edge   = 0x00000ULL;
	*irq_pol    = 0x00000000ULL;
	*irq_pol   |= (1ULL << 59);
	*irq_status = 0ULL;
	*irq_mask   = 0xffffffffffffffffULL;
	*irq_clear  = -1ULL;

	init_7a_irq(LS7A_IOAPIC_UART0_OFFSET	, LS7A_IOAPIC_UART0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_I2C0_OFFSET	, LS7A_IOAPIC_I2C0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GMAC0_OFFSET	, LS7A_IOAPIC_GMAC0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GMAC0_PMT_OFFSET, LS7A_IOAPIC_GMAC0_PMT_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GMAC1_OFFSET	, LS7A_IOAPIC_GMAC1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GMAC1_PMT_OFFSET, LS7A_IOAPIC_GMAC1_PMT_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_SATA0_OFFSET	, LS7A_IOAPIC_SATA0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_SATA1_OFFSET	, LS7A_IOAPIC_SATA1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_SATA2_OFFSET	, LS7A_IOAPIC_SATA2_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_DC_OFFSET	, LS7A_IOAPIC_DC_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GPU_OFFSET	, LS7A_IOAPIC_GPU_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_EHCI0_OFFSET	, LS7A_IOAPIC_EHCI0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_OHCI0_OFFSET	, LS7A_IOAPIC_OHCI0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_EHCI1_OFFSET	, LS7A_IOAPIC_EHCI1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_OHCI1_OFFSET	, LS7A_IOAPIC_OHCI1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F0_PORT0_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F0_PORT1_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F0_PORT2_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT2_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F0_PORT3_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT3_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F1_PORT0_OFFSET, LS7A_IOAPIC_PCIE_F1_PORT0_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_F1_PORT1_OFFSET, LS7A_IOAPIC_PCIE_F1_PORT1_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_H_LO_OFFSET, LS7A_IOAPIC_PCIE_H_LO_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_H_HI_OFFSET, LS7A_IOAPIC_PCIE_H_HI_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_G0_LO_OFFSET, LS7A_IOAPIC_PCIE_G0_LO_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_G0_HI_OFFSET, LS7A_IOAPIC_PCIE_G0_HI_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_G1_LO_OFFSET, LS7A_IOAPIC_PCIE_G1_LO_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_PCIE_G1_HI_OFFSET, LS7A_IOAPIC_PCIE_G1_HI_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_ACPI_INT_OFFSET, LS7A_IOAPIC_ACPI_INT_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_HPET_INT_OFFSET, LS7A_IOAPIC_HPET_INT_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_AC97_HDA_OFFSET, LS7A_IOAPIC_AC97_HDA_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_LPC_OFFSET, LS7A_IOAPIC_LPC_IRQ, pirq_chip);
	init_7a_irq(LS7A_IOAPIC_GPIO_HI_OFFSET, LS7A_IOAPIC_GPIO_HI_IRQ, pirq_chip);
}

void ls7a_irq_router_init(void)
{
	int i;
	unsigned int dummy;

	if(ls3a_msi_enabled) {

		int loop = (current_cpu_type() == CPU_LOONGSON3_COMP) ? 8 : 4;
		*(volatile int *)(LOONGSON_HT1_CFG_BASE+0x58) = 0x400;//strip1, ht_int 8bit

		/* route HT1 intx */
		for (i = 0; i < loop; i++) {
			dummy = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);
			ls64_conf_write8(dummy, LS_IRC_ENT_HT1(i));
			LOONGSON_HT1_INTN_EN(i) = 0xffffffff;

			dummy =  ls64_conf_read32(LS_IRC_EN);
			dummy |=  (1 << (i + 24));
			ls64_conf_write32(dummy, LS_IRC_ENSET);

			if (i >= 2) {
 				dummy =  ls64_conf_read32(LS_IRC_BCE);
 				dummy |=  (1 << (i + 24));
 				ls64_conf_write32(dummy, LS_IRC_BCE);
			}
		}
	} else {
		/* route 3A CPU0 INT0 to node0 core0 INT1(IP3) */
		dummy = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);
		ls64_conf_write8(dummy, LS_IRC_ENT(0));

		dummy = ls64_conf_read32(LS_IRC_EN);
		dummy |= 0x1;
		ls64_conf_write32(dummy, LS_IRC_ENSET);
	}
}

static DEFINE_SPINLOCK(lpc_irq_lock);

static void ack_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);
	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CLR) = (0x1 << (d->irq));
	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

static void mask_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);
	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA) &= (~(0x1 << (d->irq)));
	spin_unlock_irqrestore(&lpc_irq_lock, flags);
}

static void mask_ack_lpc_irq(struct irq_data *d)
{
}

static void unmask_lpc_irq(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&lpc_irq_lock, flags);
	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA) |= (0x1 << (d->irq));
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

static irqreturn_t lpc_irq_handler(int irq, void *data)
{
	int irqs;
	int lpc_irq;

	irqs = *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA);
	irqs &= *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_STS);
	if (irqs)
		while ((lpc_irq = ffs(irqs))) {
			do_IRQ(lpc_irq - 1);
			irqs &= ~(1 << (lpc_irq - 1));
		}
	return IRQ_HANDLED;
}

static struct irqaction lpc_irq = {
	.handler = lpc_irq_handler,
	.flags = IRQF_NO_THREAD,
	.name = "lpc",
};

extern struct system_loongson *esys;
static void register_lpc_irqs(void)
{
	int i = 0;
	setup_irq(LS7A_IOAPIC_LPC_IRQ, &lpc_irq);
	/* added for KBC attached on LPC controler */
	for(i = 0; i < 16; i++)
		irq_set_chip_and_handler(i, &lpc_irq_chip, handle_level_irq);

	/* Enable the LPC interrupt, bit31: en  bit30: edge */
	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CTL) = 0x80000000;

	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA) = 0;

	/* clear all 18-bit interrpt bit */
	*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CLR) = 0x3ffff;
}

static bool fw_support_fdt(void)
{
	return !!(esys && esys->vers >= 2 && esys->of_dtb_addr);
}

static void ls7a_lpc_init(void)
{
	struct device_node *np;

	if (fw_support_fdt()) {
		np = of_find_compatible_node(NULL, NULL, "simple-bus");
		if (np) {
			if (of_property_read_bool(np, "enable-lpc-irq")) {
				if (of_property_read_bool(np, "lpc-irq-low")) {
					*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_POL) = 0;
				} else {
					*(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_POL) = -1;
				}

				register_lpc_irqs();
			}
		}
		
	} else {
		register_lpc_irqs();
	}
}

void __init ls7a_init_irq(void)
{
	switch (current_cpu_type()) {
		case CPU_LOONGSON3:
			if(((read_c0_prid() & 0xff) > PRID_REV_LOONGSON3A_R1) && ((read_c0_prid() & 0xff) < PRID_REV_LOONGSON3A_R3_1)) {
				pr_info("Do not supports HT MSI interrupt, disabling LS7A MSI Interrupt.\n");
				ls3a_msi_enabled = 0;
		} else {
#ifdef CONFIG_LS7A_MSI_SUPPORT
			pr_info("Supports HT MSI interrupt, enabling LS7A MSI Interrupt.\n");
			ls3a_msi_enabled = 1;
			pch_irq_chip.name = "LS7A-IOAPIC-MSI";
			loongson_pch->irq_dispatch = ls7a_msi_irq_dispatch;
#endif
		}
		ls7a_irq_router_init();
		init_7a_irqs(&pch_irq_chip);
	break;
	case CPU_LOONGSON3_COMP:
		if(read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_CPU_FEATURE_MSI) {
#ifdef CONFIG_LS7A_MSI_SUPPORT
			pr_info("Supports HT MSI interrupt, enabling LS7A MSI Interrupt.\n");
			ls3a_msi_enabled = 1;
			pch_irq_chip.name = "LS7A-IOAPIC-MSI";
			loongson_pch->irq_dispatch = ls7a_msi_irq_dispatch;
#endif
			if(ls3a_msi_enabled && read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_CPU_FEATURE_EXT_IOI) {
				ext_ioi_init();
				init_7a_irqs(&ext_irq_chip);
				ls7a_msi_chip.name = "PCI-MSI-EXT";
				ls7a_msi_chip.irq_set_affinity = ext_set_irq_affinity;
				msi_chip_model = ICM_PCI_MSI_EXT;
			} else {
				ls7a_irq_router_init();
				init_7a_irqs(&pch_irq_chip);
			}

		} else {
			pr_info("Do not supports HT MSI interrupt, disabling LS7A MSI Interrupt.\n");
			ls3a_msi_enabled = 0;
			ls7a_irq_router_init();
			init_7a_irqs(&pch_irq_chip);
		}

	break;

	default:
	break;
	}

	ls7a_lpc_init();
	if(cpu_has_vint)
		set_vi_handler(3, ls7a_irq_dispatch);
}

#ifdef CONFIG_PM

static struct lpc_saved_regs
{
	unsigned int ctl_reg0;
	unsigned int ctl_reg1;
	unsigned int inter_sts;
	unsigned int inter_clr;
	unsigned int inter_pol;
}lpc_saved_reg;

static struct saved_registers
{
	unsigned long irq_edge;
	unsigned long irq_pol;
	unsigned long irq_mask;
}pic_saved_registers;

static void init_irqs_route(void)
{
	init_irq_route(LS7A_IOAPIC_UART0_OFFSET    , LS7A_IOAPIC_UART0_IRQ);
	init_irq_route(LS7A_IOAPIC_I2C0_OFFSET     , LS7A_IOAPIC_I2C0_IRQ);
	init_irq_route(LS7A_IOAPIC_GMAC0_OFFSET    , LS7A_IOAPIC_GMAC0_IRQ);
	init_irq_route(LS7A_IOAPIC_GMAC0_PMT_OFFSET, LS7A_IOAPIC_GMAC0_PMT_IRQ);
	init_irq_route(LS7A_IOAPIC_GMAC1_OFFSET    , LS7A_IOAPIC_GMAC1_IRQ);
	init_irq_route(LS7A_IOAPIC_GMAC1_PMT_OFFSET, LS7A_IOAPIC_GMAC1_PMT_IRQ);
	init_irq_route(LS7A_IOAPIC_SATA0_OFFSET    , LS7A_IOAPIC_SATA0_IRQ);
	init_irq_route(LS7A_IOAPIC_SATA1_OFFSET    , LS7A_IOAPIC_SATA1_IRQ);
	init_irq_route(LS7A_IOAPIC_SATA2_OFFSET    , LS7A_IOAPIC_SATA2_IRQ);
	init_irq_route(LS7A_IOAPIC_DC_OFFSET       , LS7A_IOAPIC_DC_IRQ);
	init_irq_route(LS7A_IOAPIC_GPU_OFFSET      , LS7A_IOAPIC_GPU_IRQ);
	init_irq_route(LS7A_IOAPIC_EHCI0_OFFSET    , LS7A_IOAPIC_EHCI0_IRQ);
	init_irq_route(LS7A_IOAPIC_OHCI0_OFFSET    , LS7A_IOAPIC_OHCI0_IRQ);
	init_irq_route(LS7A_IOAPIC_EHCI1_OFFSET    , LS7A_IOAPIC_EHCI1_IRQ);
	init_irq_route(LS7A_IOAPIC_OHCI1_OFFSET    , LS7A_IOAPIC_OHCI1_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F0_PORT0_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT0_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F0_PORT1_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT1_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F0_PORT2_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT2_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F0_PORT3_OFFSET, LS7A_IOAPIC_PCIE_F0_PORT3_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F1_PORT0_OFFSET, LS7A_IOAPIC_PCIE_F1_PORT0_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_F1_PORT1_OFFSET, LS7A_IOAPIC_PCIE_F1_PORT1_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_H_LO_OFFSET, LS7A_IOAPIC_PCIE_H_LO_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_H_HI_OFFSET, LS7A_IOAPIC_PCIE_H_HI_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_G0_LO_OFFSET, LS7A_IOAPIC_PCIE_G0_LO_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_G0_HI_OFFSET, LS7A_IOAPIC_PCIE_G0_HI_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_G1_LO_OFFSET, LS7A_IOAPIC_PCIE_G1_LO_IRQ);
	init_irq_route(LS7A_IOAPIC_PCIE_G1_HI_OFFSET, LS7A_IOAPIC_PCIE_G1_HI_IRQ);
	init_irq_route(LS7A_IOAPIC_ACPI_INT_OFFSET, LS7A_IOAPIC_ACPI_INT_IRQ);
	init_irq_route(LS7A_IOAPIC_HPET_INT_OFFSET, LS7A_IOAPIC_HPET_INT_IRQ);
	init_irq_route(LS7A_IOAPIC_AC97_HDA_OFFSET, LS7A_IOAPIC_AC97_HDA_IRQ);
	init_irq_route(LS7A_IOAPIC_LPC_OFFSET, LS7A_IOAPIC_LPC_IRQ);
	init_irq_route(LS7A_IOAPIC_GPIO_HI_OFFSET, LS7A_IOAPIC_GPIO_HI_IRQ);
}

static void restore_registers(void)
{
	*irq_edge = pic_saved_registers.irq_edge;
	*irq_pol = pic_saved_registers.irq_pol;
	*irq_mask = pic_saved_registers.irq_mask;
}

static void save_registers(void)
{
	pic_saved_registers.irq_edge = *irq_edge;
	pic_saved_registers.irq_pol = *irq_pol;
	pic_saved_registers.irq_mask = *irq_mask;
}

static int loongson3_comp_iopic_suspend(void)
{
	save_registers();
	return 0;
}

static void loongson3_comp_iopic_resume(void)
{
	int i;
	struct irq_desc *desc;

	ext_ioi_init();
	init_irqs_route();
	restore_registers();

	for (i = 0; i < NR_IRQS; i++) {
		desc = irq_to_desc(i);
		if (desc && desc->handle_irq != NULL &&
			desc->handle_irq != handle_bad_irq) {
			ext_set_irq_affinity(&desc->irq_data, desc->irq_data.affinity, 0);
		}
	}
}

static int loongson3_lpc_suspend(void)
{
	lpc_saved_reg.ctl_reg0 = *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CTL);
	lpc_saved_reg.ctl_reg1 =  *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA);
	lpc_saved_reg.inter_sts = *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_STS);
	lpc_saved_reg.inter_clr = *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CLR);
	lpc_saved_reg.inter_pol = *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_POL);

	return 0;
}

static void loongson3_lpc_resume(void)
{
	 *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CTL) = lpc_saved_reg.ctl_reg0;
	 *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_ENA) = lpc_saved_reg.ctl_reg1;
	 *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_STS) = lpc_saved_reg.inter_sts;
	 *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_CLR) = lpc_saved_reg.inter_clr;
	 *(volatile unsigned int *)TO_UNCAC(LS_LPC_INT_POL) = lpc_saved_reg.inter_pol;
}

static struct syscore_ops ls7a_comp_syscore_ops = {
	.suspend = loongson3_comp_iopic_suspend,
	.resume = loongson3_comp_iopic_resume,
};


static int loongson3_iopic_suspend(void)
{
	save_registers();
	return 0;
}

static void loongson3_iopic_resume(void)
{
	ls7a_irq_router_init();
	init_irqs_route();
	restore_registers();
}

static struct syscore_ops ls7a_syscore_ops = {
	.suspend = loongson3_iopic_suspend,
	.resume = loongson3_iopic_resume,
};

static struct syscore_ops ls7a_lpc_syscore_ops = {
	.suspend = loongson3_lpc_suspend,
	.resume = loongson3_lpc_resume,
};

int __init ls7a_init_ops(void)
{
	if ((current_cpu_type() == CPU_LOONGSON3_COMP)) {
		if (ls3a_msi_enabled && read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_CPU_FEATURE_EXT_IOI) { /* ext ioi */
			register_syscore_ops(&ls7a_comp_syscore_ops);
		} else {
			register_syscore_ops(&ls7a_syscore_ops);
		}
	} else {
		register_syscore_ops(&ls7a_syscore_ops);
	}
	register_syscore_ops(&ls7a_lpc_syscore_ops);

	return 0;
}

#endif
