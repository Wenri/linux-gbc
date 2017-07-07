/*
 * =====================================================================================
 *
 *       Filename:  mem.c
 *
 *    Description:  mem init
 *
 *        Version:  1.0
 *        Created:  03/18/2017 06:56:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/bootmem.h>
#include <asm/prom.h>
#include <asm/dma-coherence.h>

void __init mips_reboot_setup(void);

void __init device_tree_init(void)
{
	unsigned long base, size;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
	reserve_bootmem(base, size, BOOTMEM_DEFAULT);

	unflatten_device_tree();

	/* free the space reserved for the dt blob */
	free_bootmem(base, size);

}

static int __init setup_dma_ops(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "ls,nbus");

	if (!np)
		pr_err("Oops: No Loongson NBus found!\n");

	if (of_property_read_bool(np, "dma-coherent")) {

		hw_coherentio = 1;
		pr_info("Hardware support coherent IO!\n");

	} else {

		hw_coherentio = 0;
		pr_info("Hardware DOES NOT support coherent IO!\n");
	}

	return 0;
}
void __init plat_mem_setup(void)
{

	mips_reboot_setup();
}

void __init prom_free_prom_memory(void)
{

}

static struct of_device_id __initdata ls2k_ids[] = {
	{ .compatible = "simple-bus", },
	{},
};

int __init ls2k_publish_devices(void)
{

	/*return of_platform_bus_probe(NULL, ls2k_ids, NULL);*/
	return of_platform_populate(NULL, ls2k_ids, NULL, NULL);
}

arch_initcall(setup_dma_ops);
device_initcall(ls2k_publish_devices);
