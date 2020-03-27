/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <linux/pci.h>

#include <pci.h>
#include <loongson.h>
#include <boot_param.h>
#include <loongson-pch.h>

extern int vz_ls7a_pcibios_dev_init(struct pci_dev *dev);
extern int vz_ls7a_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);

static struct resource loongson_pci_mem_resource = {
	.name	= "pci memory space",
	.start	= LOONGSON_PCI_MEM_START,
	.end	= LOONGSON_PCI_MEM_END,
	.flags	= IORESOURCE_MEM,
};

static struct resource loongson_pci_io_resource = {
	.name	= "pci io space",
	.start	= LOONGSON_PCI_IO_START,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

static struct pci_controller  loongson_pci_controller = {
	.pci_ops	= NULL,
	.io_resource	= &loongson_pci_io_resource,
	.mem_resource	= &loongson_pci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_offset	= 0x00000000UL,
};

static void __init setup_pcimap(void)
{
	/*
	 * local to PCI mapping for CPU accessing PCI space
	 * CPU address space [256M,448M] is window for accessing pci space
	 * we set pcimap_lo[0,1,2] to map it to pci space[0M,64M], [320M,448M]
	 *
	 * pcimap: PCI_MAP2  PCI_Mem_Lo2 PCI_Mem_Lo1 PCI_Mem_Lo0
	 *	     [<2G]   [384M,448M] [320M,384M] [0M,64M]
	 */
	unsigned int dummy;

	dummy = LOONGSON_PCIMAP_PCIMAP_2 |
		LOONGSON_PCIMAP_WIN(2, LOONGSON_PCILO2_BASE) |
		LOONGSON_PCIMAP_WIN(1, LOONGSON_PCILO1_BASE) |
		LOONGSON_PCIMAP_WIN(0, 0);
	writel(dummy, LOONGSON_PCIMAP);
	/*
	 * PCI-DMA to local mapping: [2G,2G+256M] -> [0M,256M]
	 */
	writel(0x80000000, LOONGSON_PCIBASE0); /* base: 2G -> mmap: 0M */
	/* size: 256M, burst transmission, pre-fetch enable, 64bit */
	writel(0xc000000c, LOONGSON_PCI_HIT0_SEL_L);
	writel(0xffffffff, LOONGSON_PCI_HIT0_SEL_H);
	writel(0x00000006, LOONGSON_PCI_HIT1_SEL_L); /* set this BAR as invalid */
	writel(0x00000000, LOONGSON_PCI_HIT1_SEL_H);
	writel(0x00000006, LOONGSON_PCI_HIT2_SEL_L); /* set this BAR as invalid */
	writel(0x00000000, LOONGSON_PCI_HIT2_SEL_H);

	/* avoid deadlock of PCI reading/writing lock operation */
	writel(0xd2000001, LOONGSON_PCI_ISR4C);

	/* can not change gnt to break pci transfer when device's gnt not
	deassert for some broken device */
	writel(0x00fe0105, LOONGSON_PXARB_CFG);

#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG
	/*
	 * set cpu addr window2 to map CPU address space to PCI address space
	 */
	LOONGSON_ADDRWIN_CPUTOPCI(ADDRWIN_WIN2, LOONGSON_CPU_MEM_SRC,
		LOONGSON_PCI_MEM_DST, MMAP_CPUTOPCI_SIZE);
#endif
}

extern int loongson_acpi_init(void);

static int __init pcibios_init(void)
{
#ifdef CONFIG_CPU_LOONGSON3
	if (loongson_pch && loongson_pch->board_type == LS2H) {
		setup_pcimap();
		return 0;
	}

	if (loongson_pch && loongson_pch->board_type == RS780E) {
		setup_pcimap();
		loongson_pci_controller.pci_ops = &loongson_780e_pci_ops;
	} else if (loongson_pch && loongson_pch->board_type == LS7A) {
		if(cpu_guestmode){
			loongson_pch->pcibios_map_irq = vz_ls7a_pcibios_map_irq;
			loongson_pch->pcibios_dev_init = vz_ls7a_pcibios_dev_init;
			loongson_pci_controller.pci_ops = &vz_ls7a_pci_ops;
		} else {
			loongson_pci_controller.pci_ops = &loongson_ls7a_pci_ops;
		}
	} 
#else
	setup_pcimap();
	loongson_pci_controller.pci_ops = &loongson_pci_ops;
#endif

	loongson_pci_controller.io_map_base = mips_io_port_base;
#ifdef CONFIG_UEFI_FIRMWARE_INTERFACE
	loongson_pci_mem_resource.start = pci_mem_start_addr;
	loongson_pci_mem_resource.end = pci_mem_end_addr;
#endif
	register_pci_controller(&loongson_pci_controller);

#ifdef CONFIG_CPU_LOONGSON3
	loongson_acpi_init();
#endif

	return 0;
}

arch_initcall(pcibios_init);
