#include <linux/kvm_host.h>
#include <trace/events/kvm.h>
#include "ls7a_irq.h"
#include "ls3a_ht_irq.h"
/**
 * kvm_set_routing_entry: populate a kvm routing entry
 * from a user routing entry
 *
 * @kvm: the VM this entry is applied to
 * @e: kvm kernel routing entry handle
 * @ue: user api routing entry handle
 * return 0 on success, -EINVAL on errors.
 */
int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	int r = -EINVAL;

	switch (ue->type) {
	case KVM_IRQ_ROUTING_IRQCHIP:
		printk(KERN_ERR "%s:%d not support KVM_IRQ_ROUTING_IRQCHIP \n",__func__,__LINE__);
		goto out;
	case KVM_IRQ_ROUTING_MSI:
		e->set = kvm_set_msi;
		e->msi.address_lo = ue->u.msi.address_lo;
		e->msi.address_hi = ue->u.msi.address_hi;
		e->msi.data = ue->u.msi.data;
		/**
 		*linux4.19 kernel have flags and devid
		*e->msi.flags = ue->flags;
		*e->msi.devid = ue->u.msi.devid;
		*/
		break;
	default:
		goto out;
	}
	r = 0;
out:
	return r;
}

/**
 * kvm_set_msi: inject the MSI corresponding to the
 * MSI routing entry
 *
 * This is the entry point for irqfd MSI injection
 * and userspace MSI injection.
 */
int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e,
		struct kvm *kvm, int irq_source_id,
		int level, bool line_status)
{
	struct kvm_kernel_irq_routing_entry route;
	unsigned int ret;

	if (!irqchip_in_kernel(kvm))
		return -EINVAL;

	route.msi.address_lo = e->msi.address_lo;
	route.msi.address_hi = e->msi.address_hi;
	route.msi.data = e->msi.data;
	if (!level)
		return -1;
	ret = kvm_ls7a_set_msi(&route, kvm, KVM_USERSPACE_IRQ_SOURCE_ID, 1, false);
	return ret;
}

int kvm_ls7a_setup_default_irq_routing(struct kvm *kvm)
{
	struct kvm_irq_routing_entry *entries;

	u32 nr = LS7A_APIC_NUM_PINS;
	int i, ret;

	entries = kcalloc(nr, sizeof(*entries), GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < nr; i++) {
		entries[i].gsi = i;
		entries[i].type = KVM_IRQ_ROUTING_IRQCHIP;
		entries[i].u.irqchip.irqchip = 0;
		entries[i].u.irqchip.pin = i;
	}
	ret = kvm_set_irq_routing(kvm, entries, nr, 0);
	kfree(entries);

	return 0;
}
