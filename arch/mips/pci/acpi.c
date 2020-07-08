#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <asm/pci.h>
void __init pci_acpi_crs_quirks(void)
{
}



struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct pci_bus *bus = NULL;
	return bus;
}

