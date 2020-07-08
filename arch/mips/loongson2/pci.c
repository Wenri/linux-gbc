#include <asm/inst.h>
#include <linux/pci.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/branch.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define LS2K_PCI_IO_MASK 0xffffff

static int bus_no;
static DEFINE_MUTEX(ls_pci_scan_mutex);

extern struct pci_ops ls2k_pcie_ops_port0;
int of_pci_parse_bus_range(struct device_node *node, struct resource *res);
struct platform_device *of_find_device_by_node(struct device_node *np);
int platform_get_irq(struct platform_device *dev, unsigned int num);

static int ls2k_get_busno(void)
{
	return bus_no;
}

unsigned long pci_address_to_pio(phys_addr_t address)
{
	return (unsigned long) (LS2K_PCI_IO_MASK & address);
}

static int ls_of_pci_irq_map(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;
	struct pci_bus *bus;
	struct platform_device *pdev;
	struct pci_controller *controller;

	bus = dev->bus;
	while(bus->parent != NULL)
			bus = bus->parent;

	controller =(struct pci_controller *) bus->sysdata;
	pdev = of_find_device_by_node(controller->of_node);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
			return -1;
	}

	return irq;
}

void register_ls_pcie_controller(struct pci_controller *hose)
{
	static int next_busno;
	static int need_domain_info;
	LIST_HEAD(resources);
	struct pci_bus *bus;

	if (request_resource(&iomem_resource, hose->mem_resource) < 0)
		goto out;


	if (request_resource(&ioport_resource, hose->io_resource) < 0) {
		release_resource(hose->mem_resource);
		goto out;
	}

	/*
	 * Do not panic here but later - this might happen before console init.
	 */
	if (!hose->io_map_base) {
		printk(KERN_WARNING
		       "registering PCI controller with io_map_base unset\n");
	}

	mutex_lock(&ls_pci_scan_mutex);
	if (!hose->iommu)
		PCI_DMA_BUS_IS_PHYS = 1;

	if (hose->get_busno)
		next_busno = (*hose->get_busno)();

	pci_add_resource_offset(&resources,
				hose->mem_resource, hose->mem_offset);
	pci_add_resource_offset(&resources,
				hose->io_resource, hose->io_offset);
	pci_add_resource_offset(&resources,
				hose->busn_resource, hose->busn_offset);
	bus = pci_scan_root_bus(NULL, next_busno, hose->pci_ops, hose,
				&resources);
	hose->bus = bus;

	need_domain_info = need_domain_info || hose->index;
	hose->need_domain_info = need_domain_info;

	if (!bus) {
		pci_free_resource_list(&resources);
		return;
	}

	next_busno = bus->busn_res.end + 1;
	/* Don't allow 8-bit bus number overflow inside the hose -
	   reserve some space for bridges. */
	if (next_busno > 224) {
		next_busno = 0;
		need_domain_info = 1;
	}

	pci_fixup_irqs(pci_common_swizzle, ls_of_pci_irq_map);

	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		struct pci_bus *child;

		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}
	pci_bus_add_devices(bus);
	mutex_unlock(&ls_pci_scan_mutex);

	return;

out:
	printk(KERN_WARNING
	       "Skipping PCI bus scan due to resource conflict\n");
}

static int ls_pcie_probe(struct platform_device *dev)
{
	int ret;
	struct device_node *np;
	struct resource *mem_res,*bus_res,*io_res;
	struct pci_controller  *loongson_pci_controller;

	loongson_pci_controller = kzalloc(sizeof(*loongson_pci_controller), GFP_KERNEL);
	if (!loongson_pci_controller){
			ret = -ENOMEM;
			goto err_controller;
	}

	np = dev->dev.of_node;
	loongson_pci_controller->of_node = np;

	mem_res = kzalloc(sizeof(*mem_res), GFP_KERNEL);
	if (!mem_res){
			ret = -ENOMEM;
			goto err_mem_res;
	}

	io_res = kzalloc(sizeof(*io_res), GFP_KERNEL);
	if (!io_res){
			ret = -ENOMEM;
			goto err_io_res;
	}
	loongson_pci_controller->mem_resource = mem_res;
	loongson_pci_controller->io_resource = io_res;

	pci_load_of_ranges(loongson_pci_controller,np);
	bus_res = kzalloc(sizeof(*bus_res), GFP_KERNEL);
	if (!bus_res){
			ret = -ENOMEM;
			goto err_bus_res;
	}
	if(of_pci_parse_bus_range(np, bus_res)){
			ret = -EINVAL;
			goto err_bus_res;
	}

	loongson_pci_controller->get_busno = ls2k_get_busno;
	loongson_pci_controller->busn_resource = bus_res;
	bus_no = bus_res->start;
	loongson_pci_controller->pci_ops = &ls2k_pcie_ops_port0;
	register_ls_pcie_controller(loongson_pci_controller);

	return 0;

err_bus_res:
	kfree(bus_res);
err_io_res:
	kfree(io_res);
err_mem_res:
	kfree(mem_res);
err_controller:
	kfree(loongson_pci_controller);

	return ret;
}

static int ls_pcie_remove(struct platform_device *dev)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls_pcie_id_table[] = {
	{ .compatible = "loongson,ls-pcie", },
	{},
};
#endif
static struct platform_driver ls_pcie_driver = {
	.probe	= ls_pcie_probe,
	.remove = ls_pcie_remove,
	.driver = {
		.name	= "ls-pcie",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_pcie_id_table),
#endif
	},
};

/*
 * fix loongson 2k pcie bus error
 */
static int ls2k_be_handler(struct pt_regs *regs, int is_fixup)
{
	int action = MIPS_BE_FATAL;
	union mips_instruction insn;
	unsigned int __user *epc;

	if (is_fixup)
			return MIPS_BE_FIXUP;

	epc = (unsigned int __user *)exception_epc(regs);
	if (likely(__get_user(insn.word, epc) >= 0)) {
			switch (insn.i_format.opcode) {
					case lw_op:
							compute_return_epc(regs);
							regs->regs[insn.i_format.rt] = -1;
							action = MIPS_BE_DISCARD;
							break;
					case sw_op:
							compute_return_epc(regs);
							action = MIPS_BE_DISCARD;
							break;
			}
	}

	return action;
}

static int __init ls2k_pcie_init (void)
{
	ioport_resource.end = 0xffffffff;
	board_be_handler = ls2k_be_handler;
	platform_driver_register(&ls_pcie_driver);
	return 0;
}
arch_initcall(ls2k_pcie_init);
