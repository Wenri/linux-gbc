/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * commpage, currently used for Virtual Loongson7A IOAPIC.
 *
 * Copyright (C) 2019 Loongson Technologies, Inc.  All rights reserved.
 * Authors: Chen Zhu <zhuchen@loongson.cn>
 */

#include <linux/highmem.h>
#include <linux/mm.h>
#include "ls7a_irq.h"
#include "ls3a_ht_irq.h"
#include "ls3a3000.h"
#include "ls3a_router_irq.h"


void ls7a_ioapic_lock(struct loongson_kvm_7a_ioapic *s, unsigned long *flags)
{
	unsigned long tmp;
	spin_lock_irqsave(&s->lock, tmp);
	*flags = tmp;
}

void ls7a_ioapic_unlock(struct loongson_kvm_7a_ioapic *s, unsigned long *flags)
{
	unsigned long tmp;
	tmp = *flags;
	spin_unlock_irqrestore(&s->lock, tmp);
}

static void kvm_ls7a_ioapic_raise(struct kvm *kvm, unsigned long mask)
{
	unsigned long irqnum, val, tmp;
	struct loongson_kvm_7a_ioapic *s = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *state;
	struct kvm_mips_interrupt irq;
	int i;

	state = &s->ls7a_ioapic;
	irq.cpu = -1;
	val = mask & state->intirr & (~state->int_mask);
	if (val & (~state->htmsi_en)) {
		tmp = val & (~state->htmsi_en);
		if ((state->intisr & tmp) == 0) {
			state->intisr |= tmp;
			ls7a_update_read_page_long(s,LS7A_IOAPIC_INT_STATUS_OFFSET,state->intisr);
			route_update_reg(kvm,0,1);
		}
	}

	if (val & state->htmsi_en) {
		val &= state->htmsi_en;
		for_each_set_bit(i, &val, 64) {
			if ((state->intisr & (0x1ULL << i)) == 0) {
				state->intisr |= 0x1ULL << i;
				ls7a_update_read_page_long(s,LS7A_IOAPIC_INT_STATUS_OFFSET,state->intisr);
				irqnum = state->htmsi_vector[i];
				kvm_debug("msi_irq_handler,%ld,up\n",irqnum);
				msi_irq_handler(kvm, irqnum, 1);
			}
		}

	}
	kvm->stat.lsvz_kvm_ls7a_ioapic_update++;
}

static void kvm_ls7a_ioapic_lower(struct kvm *kvm, unsigned long mask)
{
	unsigned long irqnum, tmp, val;
	struct loongson_kvm_7a_ioapic *s = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *state;
	struct kvm_mips_interrupt irq;
	int i;

	state = &s->ls7a_ioapic;
	irq.cpu = -1;
	val = mask & state->intisr;
	if (val & (~state->htmsi_en)) {
		tmp = val & (~state->htmsi_en);
		if (state->intisr & tmp) {
			state->intisr &= ~tmp;
			ls7a_update_read_page_long(s,LS7A_IOAPIC_INT_STATUS_OFFSET,state->intisr);
			route_update_reg(kvm, 0, 0);
		}
	}

	if (val & state->htmsi_en) {
		val &= state->htmsi_en;
		for_each_set_bit(i, &val, 64) {
			if (state->intisr & (0x1ULL << i)) {
				state->intisr &= ~(0x1ULL << i);
				ls7a_update_read_page_long(s,LS7A_IOAPIC_INT_STATUS_OFFSET,state->intisr);
				irqnum = state->htmsi_vector[i];
				kvm_debug("msi_irq_handler,%ld,down\n",irqnum);
				msi_irq_handler(kvm, irqnum, 0);
			}
		}

	}
	kvm->stat.lsvz_kvm_ls7a_ioapic_update++;
}

int kvm_ls7a_set_msi(struct kvm_kernel_irq_routing_entry *e,
		struct kvm *kvm, int irq_source_id, int level, bool line_status)
{
	unsigned long flags;
	if (!level)
		return -1;

	kvm_debug("msi data is 0x%x",e->msi.data);
	ls7a_ioapic_lock(ls7a_ioapic_irqchip(kvm), &flags);
	msi_irq_handler(kvm, e->msi.data, 1);
	ls7a_ioapic_unlock(ls7a_ioapic_irqchip(kvm), &flags);

	return 0;
}

int kvm_ls7a_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	struct kvm_kernel_irq_routing_entry route;

	if (!ls3a_htirq_in_kernel(kvm) || msi->flags != 0)
		return -EINVAL;

	kvm->stat.lsvz_kvm_ls7a_msi_irq++;
	route.msi.address_lo = msi->address_lo;
	route.msi.address_hi = msi->address_hi;
	route.msi.data = msi->data;

	kvm_debug("msi data is 0x%x",route.msi.data);
	return kvm_ls7a_set_msi(&route, kvm, KVM_USERSPACE_IRQ_SOURCE_ID, 1, false);

}

int kvm_ls7a_ioapic_set_irq(struct kvm *kvm, int irq, int level)
{
	struct loongson_kvm_7a_ioapic *s;
	struct kvm_ls7a_ioapic_state *state;
	uint64_t mask = 1ULL << irq;
	s = ls7a_ioapic_irqchip(kvm);
	state = &s->ls7a_ioapic;
	BUG_ON(irq < 0 || irq >= LS7A_IOAPIC_NUM_PINS);

	if (state->intedge & mask) {
		/* edge triggered */
		/* TODO */
	} else {
		/* level triggered */
		if (!!level) {
			if ((state->intirr & mask) == 0) {
				state->intirr |= mask;
				ls7a_update_read_page_long(s,0x380,state->intirr);
				kvm_ls7a_ioapic_raise(kvm, mask);
			}
		} else {
			if (state->intirr & mask) {
				state->intirr &= ~mask;
				ls7a_update_read_page_long(s,0x380,state->intirr);
				kvm_ls7a_ioapic_lower(kvm, mask);
			}
		}
	}
	kvm->stat.lsvz_kvm_ls7a_ioapic_set_irq++;
	return 0;
}

int ls7a_ioapic_reg_write(struct loongson_kvm_7a_ioapic *s,
		       gpa_t addr, int len,const void *val)
{
	struct kvm *kvm;
	struct kvm_ls7a_ioapic_state *state;

        int64_t offset_tmp;
        uint64_t offset;
	uint64_t data, old;

        offset = addr&0xfff;
	kvm = s->kvm;
	state = &(s->ls7a_ioapic);

	if (offset & (len -1 )) {
		printk("%s(%d):unaligned address access %llx size %d \n",
				__FUNCTION__, __LINE__, addr, len);
		return 0;
	}

        if(8 == len){
		data = *(uint64_t *)val;
                switch(offset){
                        case LS7A_IOAPIC_INT_MASK_OFFSET:
				old = state->int_mask;
                                state->int_mask = data;
				ls7a_update_read_page_long(s,offset,state->int_mask);
				if (old & ~data)
					kvm_ls7a_ioapic_raise(kvm, old & ~data);
				else if (~old & data)
					kvm_ls7a_ioapic_lower(kvm, ~old & data);
                                break;
                        case LS7A_IOAPIC_INT_STATUS_OFFSET:
                                state->intisr = data;
				ls7a_update_read_page_long(s,offset,state->intisr);
                                break;
                        case LS7A_IOAPIC_INT_EDGE_OFFSET:
                                state->intedge = data;
				ls7a_update_read_page_long(s,offset,state->intedge);
                                break;
                        case LS7A_IOAPIC_INT_CLEAR_OFFSET:
				kvm_ls7a_ioapic_lower(kvm, data);
				state->intisr &= (~data);
				ls7a_update_read_page_long(s,LS7A_IOAPIC_INT_STATUS_OFFSET,state->intisr);
                                break;
                        case LS7A_IOAPIC_INT_POL_OFFSET:
                                state->int_polarity = data;
				ls7a_update_read_page_long(s,offset,state->int_polarity);
                                break;
                        case LS7A_IOAPIC_HTMSI_EN_OFFSET:
                                state->htmsi_en = data;
				ls7a_update_read_page_long(s,offset,state->htmsi_en);
                                break;
                        default:
				WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
				break;
                }
        }else if(1 == len){
		data = *(unsigned char *)val;
                if(offset >= LS7A_IOAPIC_HTMSI_VEC_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_HTMSI_VEC_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                state->htmsi_vector[offset_tmp] = (uint8_t)(data & 0xff);
				ls7a_update_read_page_char(s,offset,state->htmsi_vector[offset_tmp]);
                        }
                }else if(offset >=  LS7A_IOAPIC_ROUTE_ENTRY_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_ROUTE_ENTRY_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                state->route_entry[offset_tmp] = (uint8_t)(data & 0xff);
				ls7a_update_read_page_char(s,offset,state->route_entry[offset_tmp]);
                        }
                } else {
			WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
		}
        } else {
		WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
	}
	kvm->stat.lsvz_ls7a_ioapic_reg_write++;
	return 0;

}


static int kvm_ls7a_ioapic_write(struct kvm_io_device * dev,
			 gpa_t addr, int len, const void *val)
{
	struct loongson_kvm_7a_ioapic *s;
	s = container_of(dev, struct loongson_kvm_7a_ioapic, dev_ls7a_ioapic);
	return ls7a_ioapic_reg_write(s,addr,len,val);
}

int ls7a_ioapic_reg_read(struct loongson_kvm_7a_ioapic *s,
		       gpa_t addr, int len, void *val)
{
        uint64_t offset,offset_tmp;
	struct kvm *kvm;
	struct kvm_ls7a_ioapic_state *state;
	uint64_t result=0;

	state = &(s->ls7a_ioapic);
	kvm = s->kvm;
        offset = addr&0xfff;

	if (offset & (len -1 )) {
		printk("%s(%d):unaligned address access %llx size %d \n",
				__FUNCTION__, __LINE__, addr, len);
		return 0;
	}

	if(8 == len){
                switch(offset){
                        case LS7A_IOAPIC_INT_MASK_OFFSET:
                                result = state->int_mask;
                                break;
                        case LS7A_IOAPIC_INT_STATUS_OFFSET:
                                result = state->intisr & (~state->int_mask);
                                break;
                        case LS7A_IOAPIC_INT_EDGE_OFFSET:
                                result = state->intedge;
                                break;
                        case LS7A_IOAPIC_INT_POL_OFFSET:
                                result = state->int_polarity;
                                break;
                        case LS7A_IOAPIC_HTMSI_EN_OFFSET:
                                result = state->htmsi_en;
                                break;
                        default:
				WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
                                break;
                }
		if(val != NULL)
			*(uint64_t *)val = result;

        }else if(1 == len){
                if(offset >= LS7A_IOAPIC_HTMSI_VEC_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_HTMSI_VEC_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                result = state->htmsi_vector[offset_tmp];
                        }
                }else if(offset >=  LS7A_IOAPIC_ROUTE_ENTRY_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_ROUTE_ENTRY_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                result = state->route_entry[offset_tmp];
                        }
                } else {
			WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
		}                
		if(val != NULL)
			*(unsigned char *)val = result;
        }else{
		WARN_ONCE(1,"Abnormal address access:addr 0x%llx,len %d\n",addr,len);
	}
	kvm->stat.lsvz_ls7a_ioapic_reg_read++;
	return result;
}

static int kvm_ls7a_ioapic_read(struct kvm_io_device *dev,
		       gpa_t addr, int len, void *val)
{
	struct loongson_kvm_7a_ioapic *s;
	uint64_t result=0;

	s = container_of(dev, struct loongson_kvm_7a_ioapic, dev_ls7a_ioapic);
	result = ls7a_ioapic_reg_read(s,addr,len,val);
	return 0;
}


static const struct kvm_io_device_ops kvm_ls7a_ioapic_ops = {
	.read     = kvm_ls7a_ioapic_read,
	.write    = kvm_ls7a_ioapic_write,
};

struct loongson_kvm_7a_ioapic *kvm_create_ls7a_ioapic(struct kvm *kvm)
{
	struct loongson_kvm_7a_ioapic *s;
	int ret;
	unsigned long ls7a_ioapic_reg_base;

	s = kzalloc(sizeof(struct loongson_kvm_7a_ioapic), GFP_KERNEL);
	if (!s)
		return NULL;
	spin_lock_init(&s->lock);
	s->kvm = kvm;

#ifdef CONFIG_KVM_LOONGSON_IOAPIC_READ_OPT
	s->read_page = alloc_pages(GFP_KERNEL | __GFP_REPEAT,0);
	s->read_page_address = page_address(s->read_page);
	memset(s->read_page_address,0,PAGE_SIZE);
#endif

	if(current_cpu_type() == CPU_LOONGSON3_COMP) {
		ls7a_ioapic_reg_base = LS3A4000_LS7A_IOAPIC_GUEST_REG_BASE;
	} else {
		ls7a_ioapic_reg_base = LS7A_IOAPIC_GUEST_REG_BASE;
	}

	/*
	 * Initialize PIO device
	 */
	kvm_iodevice_init(&s->dev_ls7a_ioapic, &kvm_ls7a_ioapic_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, ls7a_ioapic_reg_base, 0x1000,
				      &s->dev_ls7a_ioapic);
	if (ret < 0)
		goto fail_unlock;

	mutex_unlock(&kvm->slots_lock);
	return s;

fail_unlock:
	mutex_unlock(&kvm->slots_lock);
	kfree(s);

	return NULL;
}


int kvm_get_ls7a_ioapic(struct kvm *kvm, struct kvm_loongson_ls7a_ioapic_state *state)
{
	struct loongson_kvm_7a_ioapic *ls7a_ioapic = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *ioapic_state =  &(ls7a_ioapic->ls7a_ioapic);
	unsigned long flags;

	ls7a_ioapic_lock(ls7a_ioapic, &flags);
	memcpy(state, ioapic_state, sizeof(struct kvm_ls7a_ioapic_state));
	ls7a_ioapic_unlock(ls7a_ioapic, &flags);
	kvm->stat.lsvz_kvm_get_ls7a_ioapic++;
	return 0;
}

int kvm_set_ls7a_ioapic(struct kvm *kvm, struct kvm_loongson_ls7a_ioapic_state *state)
{
	struct loongson_kvm_7a_ioapic *ls7a_ioapic = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *ioapic_state =  &(ls7a_ioapic->ls7a_ioapic);
	unsigned long flags;
	int i;
	if (!ls7a_ioapic)
		return -EINVAL;

	ls7a_update_read_page_long(ls7a_ioapic,LS7A_IOAPIC_INT_MASK_OFFSET,ioapic_state->int_mask);
	ls7a_update_read_page_long(ls7a_ioapic,LS7A_IOAPIC_INT_EDGE_OFFSET,ioapic_state->intedge);
	ls7a_update_read_page_long(ls7a_ioapic,LS7A_IOAPIC_INT_STATUS_OFFSET,ioapic_state->intisr);
	ls7a_update_read_page_long(ls7a_ioapic,LS7A_IOAPIC_INT_POL_OFFSET,ioapic_state->int_polarity);
	ls7a_update_read_page_long(ls7a_ioapic,LS7A_IOAPIC_HTMSI_EN_OFFSET,ioapic_state->htmsi_en);
	for(i=0;i<64;i++){
		ls7a_update_read_page_char(ls7a_ioapic,LS7A_IOAPIC_ROUTE_ENTRY_OFFSET + i,ioapic_state->route_entry[i]);
		ls7a_update_read_page_char(ls7a_ioapic,LS7A_IOAPIC_HTMSI_VEC_OFFSET + i,ioapic_state->htmsi_vector[i]);
	}
	ls7a_ioapic_lock(ls7a_ioapic, &flags);
	memcpy(ioapic_state, state, sizeof(struct kvm_ls7a_ioapic_state));
	ls7a_ioapic_unlock(ls7a_ioapic, &flags);
	kvm->stat.lsvz_kvm_set_ls7a_ioapic++;
	return 0;
}

void kvm_destroy_ls7a_ioapic(struct loongson_kvm_7a_ioapic *vpic)
{
	kvm_io_bus_unregister_dev(vpic->kvm, KVM_PIO_BUS, &vpic->dev_ls7a_ioapic);
	kfree(vpic->read_page_address);
	kfree(vpic);
}
