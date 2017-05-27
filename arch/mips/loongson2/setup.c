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
#include <linux/kernel.h>
#include <linux/of_fdt.h>
#include <linux/bootmem.h>
#include <asm/prom.h>


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

void __init plat_mem_setup(void)
{

	/* add OF support */

	/* assuming board has minimal memory */

	if (boot_mem_map.nr_map == 0) {
		add_memory_region(0x200000, 0x9800000, BOOT_MEM_RAM);
		add_memory_region(0x90000000, 0x70000000, BOOT_MEM_RAM);
	};
}

void __init prom_free_prom_memory(void)
{
}
