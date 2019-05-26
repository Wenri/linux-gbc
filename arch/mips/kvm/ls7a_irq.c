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

#include "ls7a_irq.h"
#include "ls3a3000_ht_irq.h"
#include "ls3a3000_router_irq.h"

void ls7a_ioapic_lock(struct loongson_kvm_7a_ioapic *s)
	__acquires(&s->lock)
{
	spin_lock(&s->lock);
}

void ls7a_ioapic_unlock(struct loongson_kvm_7a_ioapic *s)
	__releases(&s->lock)
{
	spin_unlock(&s->lock);
}

void kvm_ls7a_ioapic_update(struct kvm *kvm)
{
	uint64_t irqnum,i;
	struct loongson_kvm_7a_ioapic *s = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *state;
	struct kvm_mips_interrupt irq;

	state = &s->ls7a_ioapic;
	irq.cpu = -1;
        if(state->intirr & (~state->int_mask) & (~state->htmsi_en)){
#if 0
		irq.irq = 3;
		kvm_vcpu_ioctl_interrupt(vcpu,&irq);
#else
		state->intisr = (state->intirr & (~state->int_mask) &(~state->htmsi_en));
		route_update_reg(kvm,0,1);
#endif
	}else{
#if 0
		irq.irq = -3;
		kvm_vcpu_ioctl_interrupt(vcpu,&irq);
#else
		state->intisr &= (~state->htmsi_en);
		route_update_reg(kvm,0,0);
#endif
	}
       if(state->htmsi_en){
               for(i = 0;i < 64;i++){
                       if((((~state->intisr) & state->intirr) & state->htmsi_en)&(1ULL << i)){
				state->intisr |= 1ULL << i;
				irqnum = state->htmsi_vector[i];
				ht_irq_handler(kvm,irqnum,1);
                               //qemu_set_irq(s->parent_irq[s->htmsi_vector[i]],1);
                       }else if(((~(state->intisr |state->intirr))& state->htmsi_en) & (1ULL << i)){
				irqnum = state->htmsi_vector[i];
				ht_irq_handler(kvm,irqnum,0);
                               //qemu_set_irq(s->parent_irq[s->htmsi_vector[i]],0);
                       }
               }
        }
	kvm->stat.lsvz_kvm_ls7a_ioapic_update++;
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
	    /*TODO*/
	} else {
	    /* level triggered */
	    if (!!level) {
	        state->intirr |= mask;
	    } else {
	        state->intirr &= ~mask;
	    }
	}
	kvm_ls7a_ioapic_update(kvm);
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
	uint64_t data;

        offset = addr&0xfff;
	kvm = s->kvm;
	state = &(s->ls7a_ioapic);
        if(8 == len){
		data = *(uint64_t *)val;
                switch(offset){
                        case LS7A_IOAPIC_INT_MASK_OFFSET:
                                state->int_mask = data;
                                kvm_ls7a_ioapic_update(kvm);
                                break;
                        case LS7A_IOAPIC_INT_STATUS_OFFSET:
                                state->intisr = data;
                                break;
                        case LS7A_IOAPIC_INT_EDGE_OFFSET:
                                state->intedge = data;
                                break;
                        case LS7A_IOAPIC_INT_CLEAR_OFFSET:
				state->intisr &= (~data);
				kvm_ls7a_ioapic_update(kvm);
                                break;
                        case LS7A_IOAPIC_INT_POL_OFFSET:
                                state->int_polarity = data;
                                break;
                        case LS7A_IOAPIC_HTMSI_EN_OFFSET:
                                state->htmsi_en = data;
                                break;
                        default:
                             break;
                }
        }else if(1 == len){
		data = *(unsigned char *)val;
                if(offset >= LS7A_IOAPIC_HTMSI_VEC_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_HTMSI_VEC_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                state->htmsi_vector[offset_tmp] = (uint8_t)(data & 0xff);
                        }
                }else if(offset >=  LS7A_IOAPIC_ROUTE_ENTRY_OFFSET){
                        offset_tmp = offset - LS7A_IOAPIC_ROUTE_ENTRY_OFFSET;
                        if(offset_tmp >= 0 && offset_tmp < 64){
                                state->route_entry[offset_tmp] = (uint8_t)(data & 0xff);
                        }
                }
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
                }
		if(val != NULL)
			*(unsigned char *)val = result;
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

	s = kzalloc(sizeof(struct loongson_kvm_7a_ioapic), GFP_KERNEL);
	if (!s)
		return NULL;
	spin_lock_init(&s->lock);
	s->kvm = kvm;

	/*
	 * Initialize PIO device
	 */
	kvm_iodevice_init(&s->dev_ls7a_ioapic, &kvm_ls7a_ioapic_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, LS7A_IOAPIC_GUSET_REG_BASE, 0x1000,
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


int kvm_get_ls7a_ioapic(struct kvm *kvm, struct kvm_ls7a_ioapic_state *state)
{
	struct loongson_kvm_7a_ioapic *ls7a_ioapic = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *ioapic_state =  &(ls7a_ioapic->ls7a_ioapic);
	ls7a_ioapic_lock(ls7a_ioapic);
	memcpy(state, ioapic_state, sizeof(struct kvm_ls7a_ioapic_state));
	ls7a_ioapic_unlock(ls7a_ioapic);
	kvm->stat.lsvz_kvm_get_ls7a_ioapic++;
	return 0;
}

int kvm_set_ls7a_ioapic(struct kvm *kvm, struct kvm_ls7a_ioapic_state *state)
{
	struct loongson_kvm_7a_ioapic *ls7a_ioapic = ls7a_ioapic_irqchip(kvm);
	struct kvm_ls7a_ioapic_state *ioapic_state =  &(ls7a_ioapic->ls7a_ioapic);
	if (!ls7a_ioapic)
		return -EINVAL;

	ls7a_ioapic_lock(ls7a_ioapic);
	memcpy(ioapic_state, state, sizeof(struct kvm_ls7a_ioapic_state));
	kvm_ls7a_ioapic_update(kvm);
	ls7a_ioapic_unlock(ls7a_ioapic);
	kvm->stat.lsvz_kvm_set_ls7a_ioapic++;
	return 0;
}

void kvm_destroy_ls7a_ioapic(struct loongson_kvm_7a_ioapic *vpic)
{
	kvm_io_bus_unregister_dev(vpic->kvm, KVM_PIO_BUS, &vpic->dev_ls7a_ioapic);
	kfree(vpic);
}
