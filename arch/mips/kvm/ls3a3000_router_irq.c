/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * currently used for Loongson3A Virtual router register.
 * Mapped into the guest kernel @ KVM_GUEST_COMMPAGE_ADDR.
 *
 * Copyright (C) 2019  Loongson Technologies, Inc.  All rights reserved.
 * Authors: Chen Zhu <zhuchen@loongson.cn>
 */

#include "ls3a3000_router_irq.h"
#include "ls3a3000.h"

void ls3a_router_irq_lock(struct loongson_kvm_ls3a_routerirq *s)
	__acquires(&s->lock)
{
	spin_lock(&s->lock);
}

void ls3a_router_irq_unlock(struct loongson_kvm_ls3a_routerirq *s)
	__releases(&s->lock)
{
	spin_unlock(&s->lock);
}

static uint32_t core_route_state[]={
INT_ROUTER_REGS_CORE0_INTISR,
INT_ROUTER_REGS_CORE1_INTISR,
INT_ROUTER_REGS_CORE2_INTISR,
INT_ROUTER_REGS_CORE3_INTISR,
};


void route_update_reg(struct kvm *kvm,int irqnum,int level)
{
        uint32_t inten,irtr,irq_nr,i,j;
	struct kvm_mips_interrupt irq;
	struct loongson_kvm_ls3a_routerirq *s = ls3a_router_irqchip(kvm);
	uint8_t *mem = s->ls3a_router_reg;

        if(irqnum < 0 || irqnum >= 32)
                return;
        inten = *(uint32_t *)(mem + INT_ROUTER_REGS_EN);
        irtr = *(uint8_t *)(mem + irqnum);
        irq_nr = ((irtr >> 4) & 0xf); //core irq pin HT route to
        if(inten & (1UL << irqnum)){
                if(level == 1){
                        *(uint32_t *)(mem + INT_ROUTER_REGS_ISR) |= (1ULL<<irqnum);
                }else{
                        *(uint32_t *)(mem + INT_ROUTER_REGS_ISR) &= (~(1ULL<<irqnum));
                }
                for(i=0;i<4;i++){
                        for(j=0;j<4;j++){
                                if((1UL << j) & irq_nr){
                                        if((s->core_irq_mask[i][j]) & (1ULL << irqnum)){
                                                if(level == 1){
                                                        *(uint32_t *)(mem + core_route_state[i]) |= (1ULL<<irqnum);
							irq.cpu = i;
							irq.irq = j+2;
                                                        //qemu_irq_raise(s->env[i]->irq[j+2]);
							kvm_vcpu_ioctl_interrupt(kvm->vcpus[i],&irq);
                                                }else{
                                                        *(uint32_t *)(mem + core_route_state[i]) &= (~(1ULL<<irqnum));
                                                        if( *(uint32_t *)(mem + core_route_state[i]) == 0){
								irq.cpu = i;
								irq.irq = -(j+2);
								kvm_vcpu_ioctl_interrupt(kvm->vcpus[i],&irq);
                                                                //qemu_irq_lower(s->env[i]->irq[j+2]);
                                                        }
                                                }
                                        }else{
                                                *(uint32_t *)(mem + core_route_state[i]) &= (~(1ULL<<irqnum));
                                        }
                                }
                        }
                }
        }
}

uint64_t ls3a_router_intctl_read(struct kvm *kvm, gpa_t addr, unsigned size,void *val)
{
	struct loongson_kvm_ls3a_routerirq *s = ls3a_router_irqchip(kvm);
        uint64_t ret,offset;
	uint8_t *mem;

	mem = s->ls3a_router_reg;
        offset =addr & 0xff;
        switch(size) {
        case 1:
                *(uint8_t *)val = (*(uint8_t *)(mem + offset));
                break;
        case 2:
                *(uint16_t *)val = (*(uint16_t *)(mem + offset));
                break;
        case 4:
                *(uint32_t *)val = (*(uint32_t *)(mem + offset));
                break;
        case 8:
                *(uint64_t *)val = (*(uint64_t *)(mem + offset));
                break;
        default:
                ret = 0;
                break;
        }
        return ret;
}

int ls3a_router_intctl_write(struct kvm *kvm , gpa_t addr, unsigned size, const void *val)
{
        uint64_t val_data,offset;
        int i,j,k,change;
        uint8_t old;
	uint8_t *mem;
        struct loongson_kvm_ls3a_routerirq *s = ls3a_router_irqchip(kvm);

	mem = s->ls3a_router_reg;
        offset = addr & 0xff;
        change = 0;
	val_data = *(uint64_t *)val;
        if(offset >= 0 && (offset < 0x20)){
                for(i=0;i < size;i++){
                        if(offset + i <= 0x20){
                                old = *(uint8_t *)(mem + offset + i);
                                if(old != (uint8_t)(val_data >> (i*8))){
                                        *(uint8_t *)(mem + offset) = (uint8_t)(val_data >> (i*8));
                                        for(j=0;j<4;j++){
                                                for(k=0;k<4;k++){
                                                        if((((val_data >>4)&0xfULL)&(1<<k))&&((val_data&0xfULL)&(1<<j))){
                                                                route_update_reg(kvm,offset+i,0);
                                                                s->core_irq_mask[j][k] |= (1UL << (offset+i));
                                                                route_update_reg(kvm,offset+i,1);
                                                        }else{
                                                                s->core_irq_mask[j][k] &= (~(1UL << (offset+i)));
                                                                //DPRINTF("set core %d,int %d,disable route %d\n",j,k,offset+i);
                                                        }
                                                }
                                        }
                                }
                        }
                }
        }
        if(offset == INT_ROUTER_REGS_EN_SET){
                if(4 == size){
                        *(uint32_t *)(mem + INT_ROUTER_REGS_EN) |= (uint32_t)(val_data);
                }
        }
        if(offset == INT_ROUTER_REGS_EN_CLR){
                if(4 == size){
                        *(uint32_t *)(mem + INT_ROUTER_REGS_EN) &= ~((uint32_t)(val_data));
                        *(uint32_t *)(mem + INT_ROUTER_REGS_ISR) &= ~((~((uint32_t)(val_data))) & (*(uint32_t *)(mem + INT_ROUTER_REGS_EDGE)));
                }
        }
        if(offset == INT_ROUTER_REGS_EDGE){
                if(4 == size){
                        *(uint32_t *)(mem + offset) = (uint32_t)(val_data);
                }
        }
	return 0;
}

static int kvm_ls3a_router_write(struct kvm_io_device * dev,
                         gpa_t addr, int len, const void *val)
{
        struct loongson_kvm_ls3a_routerirq *s;
        s = container_of(dev, struct loongson_kvm_ls3a_routerirq, dev_ls3a_router_irq);

        return ls3a_router_intctl_write(s->kvm,addr,len,val);
}


static int kvm_ls3a_router_read(struct kvm_io_device *dev,
                       gpa_t addr, int len, void *val)
{
        struct loongson_kvm_ls3a_routerirq *s;
        uint64_t result=0;

        s = container_of(dev, struct loongson_kvm_ls3a_routerirq, dev_ls3a_router_irq);
        result = ls3a_router_intctl_read(s->kvm,addr,len,val);
        return 0;
}

static const struct kvm_io_device_ops kvm_ls3a_router_irq_ops = {
        .read     = kvm_ls3a_router_read,
        .write    = kvm_ls3a_router_write,
};

struct loongson_kvm_ls3a_routerirq *kvm_create_ls3a_router_irq(struct kvm *kvm)
{
        struct loongson_kvm_ls3a_routerirq *s;
        int ret;

        s = kzalloc(sizeof(struct loongson_kvm_ls3a_routerirq), GFP_KERNEL);
        if (!s)
                return NULL;
        spin_lock_init(&s->lock);
        s->kvm = kvm;


        kvm_iodevice_init(&s->dev_ls3a_router_irq, &kvm_ls3a_router_irq_ops);
        mutex_lock(&kvm->slots_lock);
        ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, INT_ROUTER_REGS_BASE, 0x100,
                                      &s->dev_ls3a_router_irq);
        if (ret < 0)
                goto fail_unlock;

        mutex_unlock(&kvm->slots_lock);
        return s;

fail_unlock:
        mutex_unlock(&kvm->slots_lock);
        kfree(s);

        return NULL;
}

int kvm_get_ls3a_router_irq(struct kvm *kvm, struct routerirq_state *state)
{
        struct loongson_kvm_ls3a_routerirq *ls3a_router_irq = ls3a_router_irqchip(kvm);
        uint8_t *router_irq_reg =  ls3a_router_irq->ls3a_router_reg;
	uint32_t *core_irq_mask =  (uint32_t *)(ls3a_router_irq->core_irq_mask);
        ls3a_router_irq_lock(ls3a_router_irq);
        memcpy(state->ls3a_router_reg, router_irq_reg, 0x100);
        memcpy(state->core_irq_mask, core_irq_mask, 64);
        ls3a_router_irq_unlock(ls3a_router_irq);
        kvm->stat.lsvz_kvm_get_ls3a_router_irq++;
        return 0;
}

int kvm_set_ls3a_router_irq(struct kvm *kvm, struct routerirq_state *state)
{
        struct loongson_kvm_ls3a_routerirq *ls3a_router_irq = ls3a_router_irqchip(kvm);
        uint8_t *router_irq_reg =  ls3a_router_irq->ls3a_router_reg;
	uint32_t *core_irq_mask = (uint32_t *)(ls3a_router_irq->core_irq_mask);
        if ((!router_irq_reg) || (!core_irq_mask))
                return -EINVAL;

        ls3a_router_irq_lock(ls3a_router_irq);
        memcpy(router_irq_reg, state->ls3a_router_reg, 0x100);
        memcpy(core_irq_mask, state->core_irq_mask, 64);
        ls3a_router_irq_unlock(ls3a_router_irq);
        kvm->stat.lsvz_kvm_set_ls3a_router_irq++;
        return 0;
}

