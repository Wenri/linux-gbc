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
#include <asm/io.h>
#include <boot_param.h>
#include <loongson-pch.h>
#include <linux/of_platform.h>

extern void ls7a_init_irq(void);
extern void ls7a_irq_dispatch(void);

extern int ls7a_pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin);
extern int ls7a_pcibios_dev_init(struct pci_dev *dev);
extern int ls7a_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc);
extern void ls7a_teardown_msi_irq(unsigned int irq);
unsigned long ls7a_dc_writeflags;
DEFINE_SPINLOCK(ls7a_dc_writelock);
unsigned long ls7a_rwflags;
DEFINE_RWLOCK(ls7a_rwlock);

static void ls7a_early_config(void)
{
}

static void __init ls7a_arch_initcall(void)
{
}

static void __init ls7a_device_initcall(void)
{
}

const struct platform_controller_hub ls7a_pch = {
	.board_type		= LS7A,
	.pcidev_max_funcs 	= 7,
	.early_config		= ls7a_early_config,
	.init_irq		= ls7a_init_irq,
	.irq_dispatch		= ls7a_irq_dispatch,
	.pcibios_map_irq	= ls7a_pcibios_map_irq,
	.pcibios_dev_init	= ls7a_pcibios_dev_init,
	.pch_arch_initcall	= ls7a_arch_initcall,
	.pch_device_initcall	= ls7a_device_initcall,
#ifdef CONFIG_PCI_MSI
	.pch_setup_msi_irq	= ls7a_setup_msi_irq,
	.pch_teardown_msi_irq	= ls7a_teardown_msi_irq,
#endif
};

static struct of_device_id __initdata ls7a_ids[] = {
       { .compatible = "simple-bus", },
       {},
};

int __init ls7a_publish_devices(void)
{
       return of_platform_populate(NULL, ls7a_ids, NULL, NULL);
}

device_initcall(ls7a_publish_devices);
