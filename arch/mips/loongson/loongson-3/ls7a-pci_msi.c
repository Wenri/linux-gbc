#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <loongson.h>


extern int ls3a_msi_enabled;

/* LS7A MSI target address only for devices with 64bit MSI */
#define LS7A_MSI_TARGET_ADDRESS_64_HI		0xFD
#define LS7A_MSI_TARGET_ADDRESS_64_LO		0xF8000000

/* LS7A MSI target address for devices with 64bit MSI or 32bit MSI */
#define LS7A_MSI_TARGET_ADDRESS_64_32_HI	0x0
#define LS7A_MSI_TARGET_ADDRESS_64_32_LO	0x2FF00000

extern enum irq_chip_model msi_chip_model;
extern int ls3a_create_msi_irq(unsigned char need_ipi);
extern void ls3a_destroy_msi_irq(unsigned int irq, unsigned char need_ipi);

void ls7a_teardown_msi_irq(unsigned int irq)
{
	ls3a_destroy_msi_irq(irq, msi_chip_model == ICM_PCI_MSI ? 1 : 0);
}


static void ls3a_msi_nop(struct irq_data *data)
{
	return;
}


struct irq_chip ls7a_msi_chip = {
	.name = "PCI-MSI",
	.irq_ack = ls3a_msi_nop,
	.irq_enable = unmask_msi_irq,
	.irq_disable = mask_msi_irq,
	.irq_mask = mask_msi_irq,
	.irq_unmask = unmask_msi_irq,
	.irq_set_affinity = plat_set_irq_affinity,
};

int ls7a_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	int irq = ls3a_create_msi_irq(msi_chip_model == ICM_PCI_MSI ? 1 : 0);
	struct msi_msg msg;

	if(!ls3a_msi_enabled)
		return -ENOSPC;

	if (irq < 0)
		return irq;

	irq_set_msi_desc(irq, desc);

	msg.address_hi = LS7A_MSI_TARGET_ADDRESS_64_32_HI;
	msg.address_lo = LS7A_MSI_TARGET_ADDRESS_64_32_LO;

	msg.data = LS3A_IOIRQ2VECTOR(irq);

	write_msi_msg(irq, &msg);

	irq_set_chip_and_handler(irq, &ls7a_msi_chip, handle_edge_irq);

	return 0;
}
