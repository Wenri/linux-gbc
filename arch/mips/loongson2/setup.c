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
#include <linux/libfdt.h>
#include <linux/pci.h>

#include <ls2k.h>
#include <linux/spinlock.h>
DEFINE_SPINLOCK(ls2k_io_lock);
EXPORT_SYMBOL(ls2k_io_lock);
u64 loongson_chiptemp[MAX_PACKAGES];
u32 nr_cpus_loongson;
u32 cores_per_package;
u32 loongson_hwmon;

void __init mips_reboot_setup(void);

void __init device_tree_init(void)
{
	unsigned long base, size;
    void *dt;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
    dt = memblock_virt_alloc(size,roundup_pow_of_two(FDT_V17_SIZE));
    if (dt) {
        memcpy(dt, initial_boot_params, size);
        initial_boot_params = dt;
    }

	unflatten_device_tree();

}

static int __init setup_package(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "ls,nbus");
	if (!np) {
		pr_err("Oops: No Loongson NBus found!\n");
		hw_coherentio = 0;
		pr_info("Assume Hardware DOES NOT support coherent IO!\n");
		goto no_np;
	}
	if (of_property_read_u32(np, "nr_cpus_loongson" , &nr_cpus_loongson)){
		pr_err("nr_cpus_loongson not found!\n");
		goto no_found;
	}
	if (of_property_read_u32(np, "cores_per_package", &cores_per_package)){
		pr_err("cores_per_package not found!\n");
		goto no_found;
	}

	if (strstr(arcs_cmdline, "hwmon"))
		loongson_hwmon = 1;
	else
		loongson_hwmon = 0;

	of_node_put(np);
no_np:
	return 0;
no_found:
	return 0;
}

/*static void __init set_io_noncoherent(void)*/
/*{*/

       /*unsigned long val;*/
       /*unsigned long addr = CKSEG1ADDR(0x1fe10000);*/

       /*val = readq((void*)(addr + 0x0420));*/
       /*val &= 0xffffff8fffffffe; //pcie, usb, hda, gmac*/
       /*writeq(val, (void *)(addr + 0x0420));*/

       /*val = readq((void*)(addr + 0x0430));*/
       /*val &= 0xffffffffffffff9; //dc, gpu*/
       /*writeq(val, (void *)(addr + 0x0430));*/

       /*val = readq((void *)(addr + 0x0450));*/
       /*val &= 0xffffffffffffbff; //sata*/
       /*writeq(val, (void *)(addr + 0x0450));*/
/*}*/

static int __init setup_dma_ops(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "ls,nbus");

	if (!np) {
		pr_err("Oops: No Loongson NBus found!\n");
		hw_coherentio = 0;
		pr_info("Assume Hardware DOES NOT support coherent IO!\n");
		goto no_np;
	}

	if (of_property_read_bool(np, "dma-coherent")) {

		hw_coherentio = 1;
		pr_info("Hardware support coherent IO!\n");

	} else {

		hw_coherentio = 0;
		pr_info("Hardware DOES NOT support coherent IO!\n");

		 /*it should not be called from any formal release kernel,*/
		 /*since it should be called from bootloader*/
		/*set_io_noncoherent();*/
	}

	if (of_property_read_bool(np, "pci-probe-only")) {
		pci_add_flags(PCI_PROBE_ONLY);
	}

	of_node_put(np);
no_np:
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

arch_initcall(setup_package);
arch_initcall(setup_dma_ops);
device_initcall(ls2k_publish_devices);
