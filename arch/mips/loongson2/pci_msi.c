/*
 *  Copyright (C) 2017, Loongson Technology Corporation Limited, Inc.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <ls2k.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <asm/mach-loongson2/2k1000.h>

u64 ls_msi_irq_mask = 0;

void set_irq_attr(int irq, unsigned int imask, unsigned int core_mask, int mode);
static int pci_msi_support;
void disable_ls2k_board_irq(struct irq_data *d);
void ack_ls2k_board_irq(struct irq_data *d);

void ls_unmask_icu_irq(struct irq_data * data);
void ls_mask_icu_irq(struct irq_data * data);

#define IRQ_LS2K_MSI_0 (LS64_MSI_IRQ_BASE)
#define LS2K_NUM_MSI_IRQS 64

/*msi use irq, other device not used*/
static DECLARE_BITMAP(msi_irq_in_use, LS2K_NUM_MSI_IRQS)={LS2K_IRQ_MASK};
static DEFINE_SPINLOCK(lock);

#define irq2bit(irq) (irq - IRQ_LS2K_MSI_0)
#define bit2irq(bit) (IRQ_LS2K_MSI_0 + bit)

/*
 * Dynamic irq allocate and deallocation
 */
static int ls2k_create_irq(void)
{
	int irq, pos, reg;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
again:
	pos = find_first_zero_bit(msi_irq_in_use, LS2K_NUM_MSI_IRQS);
	if(pos==LS2K_NUM_MSI_IRQS)
	{
		spin_unlock_irqrestore(&lock, flags);
		return -ENOSPC;
	}

	irq = bit2irq(pos);
	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, msi_irq_in_use))
		goto again;
	spin_unlock_irqrestore(&lock, flags);

	dynamic_irq_init(irq);
	reg = pos<32?LS2K_INT_MSI_TRIGGER_EN_0:LS2K_INT_MSI_TRIGGER_EN_1;
	ls2k_writel(ls2k_readl(reg)|(1<<(pos&0x1f)), reg);

	return irq;
}

static void ls2k_destroy_irq(unsigned int irq)
{
	int pos = irq2bit(irq), reg;

	dynamic_irq_cleanup(irq);

	clear_bit(pos, msi_irq_in_use);
	reg = pos<32?LS2K_INT_MSI_TRIGGER_EN_0:LS2K_INT_MSI_TRIGGER_EN_1;
	ls2k_writel(ls2k_readl(reg)&~(1<<(pos&0x1f)), reg);
}

void ls2k_teardown_msi_irq(unsigned int irq)
{
	ls2k_destroy_irq(irq);
}

void disable_ls2k_msi_irq(struct irq_data *d)
{
	mask_msi_irq(d);
}

void enable_ls2k_msi_irq(struct irq_data *d)
{
	ls_unmask_icu_irq(d);
	unmask_msi_irq(d);
}

int ls_set_affinity_icu_irq(struct irq_data *data, const struct cpumask *affinity, bool force);

static struct irq_chip ls2k_msi_chip = {
	.name = "PCI-MSI",
	.irq_ack	= ls_mask_icu_irq,
	.irq_mask	= disable_ls2k_msi_irq,
	.irq_unmask	= enable_ls2k_msi_irq,
	.irq_set_affinity	= ls_set_affinity_icu_irq,
};

int ls2k_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	int pos, irq = ls2k_create_irq();
	struct msi_msg msg;


	if (irq < 0)
		return irq;

	pos = irq2bit(irq);
	irq_set_msi_desc(irq, desc);
	msg.address_hi = 0;
	msg.address_lo = pos<32?LS2K_INT_MSI_TRIGGER_0:LS2K_INT_MSI_TRIGGER_1;

	/*irq dispatch to ht vector 1,2.., 0 for leagacy devices*/
	msg.data = pos&0x1f;

	printk("irq=%d\n", irq);
	write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &ls2k_msi_chip, handle_level_irq);
	return 0;
}

static bool nomsix=1;
core_param(nomsix, nomsix, bool, 0664);
static bool nomsi=0;
core_param(nomsi, nomsi, bool, 0664);

int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *entry;
	int ret;
	if ((type == PCI_CAP_ID_MSIX && nomsix) || (type == PCI_CAP_ID_MSI && nomsi) || !pci_msi_support)
		return -ENOSPC;
	/*
	 * If an architecture wants to support multiple MSI, it needs to
	 * override arch_setup_msi_irqs()
	 */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	list_for_each_entry(entry, &dev->msi_list, list) {
		ret = ls2k_setup_msi_irq(dev, entry);
		if (ret < 0)
			return ret;
		if (ret > 0)
			return -ENOSPC;
	}

	return 0;
}

int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	if(nomsi|| !pci_msi_support)
			return -ENOSPC;
	return ls2k_setup_msi_irq(pdev, desc);
}

void arch_teardown_msi_irq(unsigned int irq)
{
	ls2k_teardown_msi_irq(irq);
}

static int ls_pci_msi_probe(struct platform_device *dev)
{
	int i;
	unsigned long base;
	__be32 *mask_p = NULL;
	int core_id = (read_c0_ebase() & 0x3ff);
	struct device_node *np = dev->dev.of_node;

	if (np != NULL) {
		pci_msi_support = 1;
		pr_info("Support PCI MSI interrupt !\n");
	} else {
		pci_msi_support = 0;
		pr_info("NOT support PCI MSI interrupt !\n");
		return -EPERM;
	}

	mask_p = (__be32 *)of_get_property(np, "msi-mask", NULL);
	if (mask_p == NULL){
			pr_info("No msi-mask property !\n");
			return -ENXIO;
	}else{
			ls_msi_irq_mask = of_read_number(mask_p,2);
	}

	for(i = 0; i < LS2K_NUM_MSI_IRQS; i++){
			if(ls_msi_irq_mask & (1UL << i))
					clear_bit(i,msi_irq_in_use);
	}

	base = CKSEG1ADDR(CONF_BASE) + INT_LO_OFF;
	ls64_conf_write32(ls_msi_irq_mask, (void *)(base + INT_EDG_OFF ));
	ls64_conf_write32((ls_msi_irq_mask >> 32)|(0x1f << 12), (void *)(base + INT_EDG_OFF + 0x40));

	for (i = LS64_MSI_IRQ_BASE ; i < LS64_MSI_IRQ_BASE + 64; i++) {
			if((1UL << (i - LS64_MSI_IRQ_BASE)) & ls_msi_irq_mask){
					set_irq_attr(i, 1 << (STATUSB_IP5 - 10), 1 << core_id, 0);
			}
	}

	return 0;
}

static int ls_pci_msi_remove(struct platform_device *dev)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls_msi_id_table[] = {
	{ .compatible = "loongson,2k-pci-msi", },
	{},
};
#endif
static struct platform_driver ls_pci_msi_driver = {
	.probe  = ls_pci_msi_probe,
	.remove = ls_pci_msi_remove,
	.driver = {
		.name   = "ls-pci-msi",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_msi_id_table),
#endif
	},
};

static int __init ls_pci_msi_init (void)
{
	int ret;
	ret = platform_driver_register(&ls_pci_msi_driver);
	return ret;
}

arch_initcall(ls_pci_msi_init);
