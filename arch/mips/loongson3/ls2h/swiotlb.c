#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>
#include <linux/scatterlist.h>

#include <asm/bootinfo.h>

#include <dma-coherence.h>

#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/gfp.h>

#include <asm/cache.h>
#include <asm/dma-mapping.h>
#include <asm/io.h>

extern void __dma_sync(unsigned long addr, size_t size,
	enum dma_data_direction direction);

extern unsigned long dma_addr_to_virt(struct device *dev,
	dma_addr_t dma_addr);

static dma_addr_t pcie_dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction,
	struct dma_attrs *attrs)
{
	dma_addr_t daddr;

	BUG_ON(direction == DMA_NONE);

	daddr = swiotlb_map_page(dev, page, offset, size,
						direction, attrs);

	if (!plat_device_is_coherent(dev)) {
		__dma_sync(dma_addr_to_virt(dev, daddr), size, direction);
	}

	mb();
	return daddr;
}

static int pcie_dma_map_sg(struct device *dev, struct scatterlist *sg,
	int nents, enum dma_data_direction direction, struct dma_attrs *attrs)
{
	int r = swiotlb_map_sg_attrs(dev, sg, nents, direction, NULL);
	mb();
	return r;
}

static void pcie_dma_sync_single_for_device(struct device *dev,
	dma_addr_t dma_handle, size_t size, enum dma_data_direction direction)
{
	swiotlb_sync_single_for_device(dev, dma_handle, size, direction);
	mb();
}

static void pcie_dma_sync_sg_for_device(struct device *dev,
	struct scatterlist *sg, int nelems, enum dma_data_direction direction)
{
	swiotlb_sync_sg_for_device(dev, sg, nelems, direction);
	mb();
}

static void *pcie_dma_alloc_coherent(struct device *dev, size_t size,
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
	/* Loongson3 doesn't support DMA above 40-bit */
	else if (dev->coherent_dma_mask <= DMA_BIT_MASK(40))
		gfp |= __GFP_DMA32;
	else
#endif
		;

	/* Don't invoke OOM killer */
	gfp |= __GFP_NORETRY;

	ret = swiotlb_alloc_coherent(dev, size, dma_handle, gfp);

	mb();
	return ret;
}

static void pcie_dma_free_coherent(struct device *dev, size_t size,
	void *vaddr, dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	int order = get_order(size);

	if (dma_release_from_coherent(dev, order, vaddr))
		return;

	swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

static int pcie_dma_set_mask(struct device *dev, u64 mask)
{
	/* Loongson3 doesn't support DMA above 40-bit */
	if (mask > DMA_BIT_MASK(32)) {
		*dev->dma_mask = DMA_BIT_MASK(32);
		return -EIO;
	}

	*dev->dma_mask = mask;

	return 0;
}
static dma_addr_t pcie_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr;
}

static phys_addr_t pcie_dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return daddr;
}

struct mips_dma_map_ops ls2h_pcie_dma_map_ops = {
	.dma_map_ops = {
		.alloc		= pcie_dma_alloc_coherent,
		.free		= pcie_dma_free_coherent,
		.map_page		= pcie_dma_map_page,
		.unmap_page		= swiotlb_unmap_page,
		.map_sg			= pcie_dma_map_sg,
		.unmap_sg		= swiotlb_unmap_sg_attrs,
		.sync_single_for_cpu	= swiotlb_sync_single_for_cpu,
		.sync_single_for_device	= pcie_dma_sync_single_for_device,
		.sync_sg_for_cpu	= swiotlb_sync_sg_for_cpu,
		.sync_sg_for_device	= pcie_dma_sync_sg_for_device,
		.mapping_error		= swiotlb_dma_mapping_error,
		.dma_supported		= swiotlb_dma_supported,
		.set_dma_mask 		= pcie_dma_set_mask
	},
	.phys_to_dma	= pcie_phys_to_dma,
	.dma_to_phys	= pcie_dma_to_phys
};

void __init ls2h_init_swiotlb(void)
{
	extern int swiotlb_force;
	swiotlb_force = 1;
	swiotlb_init(1);
}
