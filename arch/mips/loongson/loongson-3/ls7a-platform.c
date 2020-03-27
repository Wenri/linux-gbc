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

extern int ls7a_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);
extern int ls7a_pcibios_dev_init(struct pci_dev *dev);
extern int ls7a_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc);
extern void ls7a_teardown_msi_irq(unsigned int irq);
#ifdef CONFIG_PM
extern int ls7a_init_ops(void);
#endif
unsigned long ls7a_dc_writeflags;
DEFINE_SPINLOCK(ls7a_dc_writelock);
unsigned long ls7a_rwflags;
DEFINE_RWLOCK(ls7a_rwlock);

u32 node_id_offset;
#ifdef CONFIG_KVM_GUEST_LS3A3000
#define NODE_ID_OFFSET_ADDR	0x900000E01001041CULL
#else
#define NODE_ID_OFFSET_ADDR	0x90000E001001041CULL
#endif

#ifndef loongson_ls7a_decode_year
#define loongson_ls7a_decode_year(year) ((year) + 1900)
#endif

#define RTC_TOYREAD0    0x2C
#define RTC_YEAR        0x30

static void ls7a_early_config(void)
{
	node_id_offset = (*(volatile u32 *)NODE_ID_OFFSET_ADDR >> 8) & 0x1F;
}

static void __init ls7a_arch_initcall(void)
{
}

static void __init ls7a_device_initcall(void)
{
#ifdef CONFIG_PM
	ls7a_init_ops();
#endif
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

unsigned long loongson_ls7a_get_rtc_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned int value;

	value = ls7a_readl(LS7A_RTC_REG_BASE + RTC_TOYREAD0);
	sec = (value >> 4) & 0x3f;
	min = (value >> 10) & 0x3f;
	hour = (value >> 16) & 0x1f;
	day = (value >> 21) & 0x1f;
	mon = (value >> 26) & 0x3f;
	year = ls7a_readl(LS7A_RTC_REG_BASE + RTC_YEAR);

	year = loongson_ls7a_decode_year(year);

	return mktime(year, mon, day, hour, min, sec);
}


device_initcall(ls7a_publish_devices);
