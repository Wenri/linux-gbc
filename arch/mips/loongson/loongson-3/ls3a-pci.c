/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/pci.h>

#include <pci.h>
#include <irq.h>
#include <loongson.h>
#include <boot_param.h>
#include <loongson-pch.h>
#include <linux/platform_device.h>

extern struct pci_ops bonito64_pci_ops;


static struct resource ioport_resource_local = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};


static struct resource iomem_resource_local = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};



static struct resource loongson_pci_mem_resource = {
	.name   = "pci memory space",
	.start  = 0x20000000,
	.end    = 0x30000000,
	.flags  = IORESOURCE_MEM,
	.parent = &iomem_resource_local,
};

static struct resource loongson_pci_io_resource = {
	.name   = "pci io space",
	.start  = 0x00004000UL,
	.end    = IO_SPACE_LIMIT,
	.flags  = IORESOURCE_IO,
	.parent = &ioport_resource_local,
};

static struct pci_controller  loongson_pci_controller = {
	.pci_ops        = &loongson_pci_ops,
	.io_resource    = &loongson_pci_io_resource,
	.mem_resource   = &loongson_pci_mem_resource,
	.mem_offset     = 0x00000000UL,
	.io_offset      = 0x00000000UL,
};


static void __init setup_pcimap(struct resource *res)
{
	int idx;
	/*
	 * local to PCI mapping for CPU accessing PCI space
	 * CPU address space [256M,448M] is window for accessing pci space
	 * we set pcimap_lo[0,1,2] to map it to pci space[0M,64M], [320M,448M]
	 *
	 * pcimap: PCI_MAP2  PCI_Mem_Lo2 PCI_Mem_Lo1 PCI_Mem_Lo0
	 * 	     [<2G]   [384M,448M] [320M,384M] [0M,64M]
	 */
	LOONGSON_PCICMD = PCI_COMMAND_IO|PCI_COMMAND_MEMORY|PCI_COMMAND_MASTER;
	LOONGSON_PCIMAP = LOONGSON_PCIMAP_PCIMAP_2 |
		LOONGSON_PCIMAP_WIN(2, LOONGSON_PCILO2_BASE) |
		LOONGSON_PCIMAP_WIN(1, LOONGSON_PCILO1_BASE) |
		LOONGSON_PCIMAP_WIN(0, 0);

	/*
	 * PCI-DMA to local mapping: [2G,2G+256M] -> [0M,256M]
	 */
	LOONGSON_PCIBASE0 = 0x80000000ul;   /* base: 2G -> mmap: 0M */
	/* size: 256M, burst transmission, pre-fetch enable, 64bit */
	LOONGSON_PCI_HIT0_SEL_L = 0x8000000cul;
	LOONGSON_PCI_HIT0_SEL_H = 0xfffffffful;
	LOONGSON_PCI_HIT1_SEL_L = 0x00000006ul; /* set this BAR as invalid */
	LOONGSON_PCI_HIT1_SEL_H = 0x00000000ul;
	LOONGSON_PCI_HIT2_SEL_L = 0x00000006ul; /* set this BAR as invalid */
	LOONGSON_PCI_HIT2_SEL_H = 0x00000000ul;

	/* avoid deadlock of PCI reading/writing lock operation */
	LOONGSON_PCI_ISR4C = 0xd2000001ul;

	/* can not change gnt to break pci transfer when device's gnt not
	deassert for some broken device */
	//LOONGSON_PXARB_CFG = 0x00fe0105ul;

	idx = res->name[0] - '0';
	*(volatile long *)(0x900000003ff00000 + idx*8) = res->start;;
	*(volatile long *)(0x900000003ff00040 + idx*8) = ~(long long)(res->end - res->start);
	*(volatile long *)(0x900000003ff00080 + idx*8) = res->start|0x82;

}


DEFINE_SPINLOCK(ls3_pci_lock);

static void disable_pci_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int irq_nr = d->irq;
	int i = (irq_nr - SYS_IRQ_BASE);

	spin_lock_irqsave(&ls3_pci_lock, flags);
	LOONGSON_INT_ROUTER_INTENCLR = (1<<i);
	spin_unlock_irqrestore(&ls3_pci_lock, flags);
}

static void enable_pci_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int irq_nr = d->irq;
	int i = (irq_nr - SYS_IRQ_BASE);

	spin_lock_irqsave(&ls3_pci_lock, flags);

	LOONGSON_INT_ROUTER_INTENSET = (1<<i);

	spin_unlock_irqrestore(&ls3_pci_lock, flags);
}

static struct irq_chip loongson_pci_irq_chip = {
	.name		= "LOONGSON PCI",
	.irq_mask	= disable_pci_irq,
	.irq_unmask	= enable_pci_irq,
};

static void ls3_pci_irq_init(void)
{
	int i;
	for(i=PCI_IRQ_BASE; i<PCI_IRQ_BASE+4;i++)
		irq_set_chip_and_handler(i, &loongson_pci_irq_chip, handle_level_irq);
	LOONGSON_INT_ROUTER_PCI(0) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
	LOONGSON_INT_ROUTER_PCI(1) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
	LOONGSON_INT_ROUTER_PCI(2) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
	LOONGSON_INT_ROUTER_PCI(3) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
}


static int ls3a_pci_probe(struct platform_device *dev)
{
	struct resource *res;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	loongson_pci_mem_resource.start = res->start;
	loongson_pci_mem_resource.end = res->end;
	loongson_pci_controller.io_map_base = CKSEG1ADDR(0x1fd00000);

	setup_pcimap(res);

#if 0
	if (request_resource(&iomem_resource_local, &loongson_pci_mem_resource) < 0)
		goto out;

	if (request_resource(&ioport_resource_local, &loongson_pci_io_resource) < 0)
		goto out;
#endif

	ls3_pci_irq_init();

	register_pci_controller(&loongson_pci_controller);

	if(loongson_pci_controller.bus)
		pci_fixup_irqs(pci_common_swizzle, pcibios_map_irq);

	return 0;
}

int plat_device_is_ls3a_pci(struct device *dev)
{
	struct pci_dev *pcidev;
	if(!dev || !dev->bus || strcmp(dev->bus->name,"pci"))
		return 0;

	pcidev = to_pci_dev(dev);
	if(pcidev->bus->sysdata == &loongson_pci_controller)
		return 1;
	else
		return 0;
}

static dma_addr_t pci_phys_to_dma_ls3a(struct device *dev, phys_addr_t paddr)
{
	        return paddr|0x80000000;
}

static phys_addr_t pci_dma_to_phys_ls3a(struct device *dev, dma_addr_t daddr)
{
	if(daddr > 0x8fffffff)
		return daddr;
	else
		return daddr & 0x0fffffff;
}

int ls3a_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	switch(slot)
	{
	case 9:
	 return PCI_IRQ_BASE;
	case 10:
	 return PCI_IRQ_BASE+1;
	case 11:
	 return PCI_IRQ_BASE+2;
	break;
	default:
	return PCI_IRQ_BASE;
	}
}

//-----------------------------------------------------------------

static inline void dma_sync_virtual(struct device *dev, void *addr, size_t size,
	enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
		dma_cache_wback((unsigned long)addr, size);
		break;

	case DMA_FROM_DEVICE:
		if (((unsigned long)addr | size) & L1_CACHE_MASK)
			dma_cache_wback_inv((unsigned long)addr, size);
		else
			dma_cache_inv((unsigned long)addr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv((unsigned long)addr, size);
		break;

	default:
		BUG();
	}
}

static inline void *dma_to_virt(struct device *dev, dma_addr_t dma_addr)
{
	return phys_to_virt(pci_dma_to_phys_ls3a(dev, dma_addr));
}

static void *ls3a_pci_dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp, struct dma_attrs *attrs)
{
	void *ret;

	if (dma_alloc_from_coherent(dev, size, dma_handle, &ret))
		return ret;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);

#ifdef CONFIG_ZONE_DMA
	if (dev == NULL)
		gfp |= __GFP_DMA;
	else if (dev->coherent_dma_mask <= DMA_BIT_MASK(24))
		gfp |= __GFP_DMA;
	else
#endif
#ifdef CONFIG_ZONE_DMA32
	if (dev == NULL)
		gfp |= __GFP_DMA32;
	else if (dev->coherent_dma_mask <= DMA_BIT_MASK(32))
		gfp |= __GFP_DMA32;
	else
#endif
	;
	gfp |= __GFP_NORETRY|__GFP_NOWARN;

	ret = swiotlb_alloc_coherent(dev, size, dma_handle, gfp);
	dma_cache_wback_inv((unsigned long)dma_to_virt(dev, *dma_handle), size);
	ret = UNCAC_ADDR(ret);
	mb();

	return ret;
}

static void ls3a_pci_dma_free_coherent(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	int order = get_order(size);

	if (dma_release_from_coherent(dev, order, vaddr))
		return;

	vaddr = CAC_ADDR(vaddr);
	dma_cache_wback_inv((unsigned long)dma_to_virt(dev, dma_handle), size);

	swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

#define PCIE_DMA_ALIGN 16

static dma_addr_t ls3a_pci_dma_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				struct dma_attrs *attrs)
{
	dma_addr_t daddr;

	if (offset % PCIE_DMA_ALIGN)
		daddr = swiotlb_map_page(dev, page, offset, size, dir, &dev->archdata.dma_attrs);
	else
		daddr = swiotlb_map_page(dev, page, offset, size, dir, NULL);

	dma_sync_virtual(dev, dma_to_virt(dev, daddr), size, dir);
	mb();

	return daddr;
}

static void ls3a_pci_dma_unmap_page(struct device *dev, dma_addr_t dev_addr,
			size_t size, enum dma_data_direction dir,
			struct dma_attrs *attrs)
{
	dma_sync_virtual(dev, dma_to_virt(dev, dev_addr), size, dir);
	swiotlb_unmap_page(dev, dev_addr, size, dir, attrs);
}

static int ls3a_pci_dma_map_sg(struct device *dev, struct scatterlist *sgl,
				int nents, enum dma_data_direction dir,
				struct dma_attrs *attrs)
{
	int i, r;
	struct scatterlist *sg;

	r = swiotlb_map_sg_attrs(dev, sgl, nents, dir,
					&dev->archdata.dma_attrs);
	for_each_sg(sgl, sg, nents, i)
		dma_sync_virtual(dev, dma_to_virt(dev, sg->dma_address), sg->length, dir);
	mb();

	return r;
}

static void ls3a_pci_dma_unmap_sg(struct device *dev, struct scatterlist *sgl,
			int nelems, enum dma_data_direction dir,
			struct dma_attrs *attrs)
{
	int i;
	struct scatterlist *sg;

	if (dir != DMA_TO_DEVICE) {
		for_each_sg(sgl, sg, nelems, i)
			dma_sync_virtual(dev, dma_to_virt(dev, sg->dma_address), sg->length, dir);
	}

	swiotlb_unmap_sg_attrs(dev, sgl, nelems, dir, attrs);
}

static void ls3a_pci_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dev_addr,
			size_t size, enum dma_data_direction dir)
{
	dma_sync_virtual(dev, dma_to_virt(dev, dev_addr), size, dir);
	swiotlb_sync_single_for_cpu(dev, dev_addr, size, dir);
}

static void ls3a_pci_dma_sync_single_for_device(struct device *dev,
				dma_addr_t dma_handle, size_t size,
				enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(dev, dma_handle, size, dir);
	dma_sync_virtual(dev, dma_to_virt(dev, dma_handle), size, dir);
	mb();
}

static void ls3a_pci_dma_sync_sg_for_cpu(struct device *dev,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		dma_sync_virtual(dev, dma_to_virt(dev,
					sg->dma_address), sg->length, dir);
	}
	swiotlb_sync_sg_for_cpu(dev, sgl, nents, dir);
}

static void ls3a_pci_dma_sync_sg_for_device(struct device *dev,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	swiotlb_sync_sg_for_device(dev, sgl, nents, dir);
	for_each_sg(sgl, sg, nents, i) {
		dma_sync_virtual(dev, dma_to_virt(dev,
					sg->dma_address), sg->length, dir);
	}
	mb();
}

struct loongson_dma_map_ops {
	struct dma_map_ops dma_map_ops;
	dma_addr_t (*phys_to_dma)(struct device *dev, phys_addr_t paddr);
	phys_addr_t (*dma_to_phys)(struct device *dev, dma_addr_t daddr);
};

static int ls3a_pci_dma_set_mask(struct device *dev, u64 mask)
{
	/* Loongson3 doesn't support DMA above 40-bit */
	if (mask > DMA_BIT_MASK(32)) {
		*dev->dma_mask = DMA_BIT_MASK(32);
		return -EIO;
	}

	*dev->dma_mask = mask;

	return 0;
}

static struct loongson_dma_map_ops ls3a_pci_dma_map_ops = {
	.dma_map_ops = {
		.alloc = ls3a_pci_dma_alloc_coherent,
		.free = ls3a_pci_dma_free_coherent,
		.map_page = ls3a_pci_dma_map_page,
		.unmap_page = ls3a_pci_dma_unmap_page,
		.map_sg = ls3a_pci_dma_map_sg,
		.unmap_sg = ls3a_pci_dma_unmap_sg,
		.sync_single_for_cpu = ls3a_pci_dma_sync_single_for_cpu,
		.sync_single_for_device = ls3a_pci_dma_sync_single_for_device,
		.sync_sg_for_cpu = ls3a_pci_dma_sync_sg_for_cpu,
		.sync_sg_for_device = ls3a_pci_dma_sync_sg_for_device,
		.mapping_error = swiotlb_dma_mapping_error,
		.dma_supported = swiotlb_dma_supported,
		.set_dma_mask 	= ls3a_pci_dma_set_mask
	},
	.phys_to_dma	= pci_phys_to_dma_ls3a,
	.dma_to_phys	= pci_dma_to_phys_ls3a
};


int ls3a_pcibios_dev_init(struct pci_dev *pdev)
{
	pdev->dev.archdata.dma_ops = &ls3a_pci_dma_map_ops.dma_map_ops;
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls3a_pci_id_table[] = {
	{ .compatible = "loongson,ls3a-pci", },
};
#endif

static struct platform_driver ls3a_pci_driver = {
	.probe	= ls3a_pci_probe,
	.driver = {
		.name	= "ls3a-pci",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls3a_pci_id_table),
#endif
	},
};

#ifdef LS3A_PCI_ENABLE_IN_KERNEL
static struct resource ls3a_pci_resources[] = {
	[0] = {
		.name = "3",
		.start = 0x20000000,
		.end   = 0x2fffffff,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device ls3a_pci_device = {
	.name			= "ls3a-pci",
	.id   			= 0,
	.num_resources  = ARRAY_SIZE(ls3a_pci_resources),
	.resource 		= ls3a_pci_resources,
};
#endif

int __init ls3a_pci_init (void)
{

#ifdef LS3A_PCI_ENABLE_IN_KERNEL
	platform_device_register(&ls3a_pci_device);
#endif
	platform_driver_register(&ls3a_pci_driver);
	return 0;
}

arch_initcall(ls3a_pci_init);
