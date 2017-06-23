#include <linux/init.h>
#include <linux/pci.h>

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	printk("map dev:%p, slot:%d, pin:%d\n", dev, slot, pin);
	return 0;
}


int pcibios_plat_dev_init(struct pci_dev *dev)
{
	printk("pcibios_plat_dev_init called\n");
	return 0;
}
