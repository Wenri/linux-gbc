#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <loongson-pch.h>


void unmask_ls2h_irq(struct irq_data *d);
void mask_ls2h_irq(struct irq_data *d);
void ack_ls2h_irq(struct irq_data *d);
unsigned int startup_ls2h_board_irq(struct irq_data *d);
void shutdown_ls2h_board_irq(struct irq_data *d);

#define IRQ_LS2H_MSI_0 (LS2H_PCH_IRQ_BASE+96)
#define LS2H_NUM_MSI_IRQS 32
static DECLARE_BITMAP(msi_irq_in_use, LS2H_NUM_MSI_IRQS);


static DEFINE_SPINLOCK(lock);
static int msi_irq_mask;

#define irq2bit(irq) (irq - IRQ_LS2H_MSI_0)
#define bit2irq(bit) (IRQ_LS2H_MSI_0 + bit)

/*
 * Dynamic irq allocate and deallocation
 */
static unsigned int startup_ls2h_msi_irq(struct irq_data *d)
{
	startup_ls2h_board_irq(d);
	unmask_msi_irq(d);
	return 0;
}

static void shutdown_ls2h_msi_irq(struct irq_data *d)
{
	shutdown_ls2h_board_irq(d);
	mask_msi_irq(d);
}

static int ls2h_create_irq(void)
{
	int irq, pos;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
again:
	pos = find_first_zero_bit(&msi_irq_in_use[0], 32);
	if(pos==32)
	{
		spin_unlock_irqrestore(&lock, flags);
		return -ENOSPC;
	}

	irq = bit2irq(pos);
	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, msi_irq_in_use))
		goto again;
	spin_unlock_irqrestore(&lock, flags);

	dynamic_irq_init(irq);

	return irq;
}

static void ls2h_destroy_irq(unsigned int irq)
{
	int pos = irq2bit(irq);

	dynamic_irq_cleanup(irq);

	clear_bit(pos, msi_irq_in_use);
}

void ls2h_teardown_msi_irq(unsigned int irq)
{
	ls2h_destroy_irq(irq);
}

void disable_ls2h_msi_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	int pos = irq2bit(irq);
	msi_irq_mask &= ~(1<<pos);
	mask_ls2h_irq(d);
	mask_msi_irq(d);
}

void enable_ls2h_msi_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	int pos = irq2bit(irq);
	msi_irq_mask |= 1<<pos;
	unmask_ls2h_irq(d);
	unmask_msi_irq(d);
}

int ls2h_set_irq_affinity( struct irq_data *d, const struct cpumask *affinity, bool force);

static struct irq_chip ls2h_msi_chip = {
	.name = "PCI-MSI",
	.irq_ack	= ack_ls2h_irq,
	.irq_mask	= mask_ls2h_irq,
	.irq_unmask	= unmask_ls2h_irq,
	.irq_eoi	= unmask_ls2h_irq,
	.irq_startup	= startup_ls2h_msi_irq,
	.irq_shutdown	= shutdown_ls2h_msi_irq,
	.irq_set_affinity = plat_set_irq_affinity,
};


int ls2h_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	int pos, irq = ls2h_create_irq();
	struct msi_msg msg;


	if (irq < 0)
		return irq;

	pos = irq2bit(irq);
	irq_set_msi_desc(irq, desc);
	msg.address_hi = 0;
	msg.address_lo = 0;

	/*irq dispatch to ht vector 1,2.., 0 for leagacy devices*/
	msg.data = pos;


	printk("irq=%d\n", irq);
	write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &ls2h_msi_chip, handle_level_irq);

	return 0;
}

