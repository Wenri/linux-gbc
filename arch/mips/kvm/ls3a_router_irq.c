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

#include "ls3a_router_irq.h"
#include "ls3a3000.h"

#define ls3a_router_irq_lock(s, flags)		spin_lock_irqsave(&s->lock, flags)
#define ls3a_router_irq_unlock(s, flags)	spin_unlock_irqrestore(&s->lock, flags)

static uint32_t core_route_state[]= {
	INT_ROUTER_REGS_CORE0_INTISR,
	INT_ROUTER_REGS_CORE1_INTISR,
	INT_ROUTER_REGS_CORE2_INTISR,
	INT_ROUTER_REGS_CORE3_INTISR,
};


void route_update_reg(struct kvm *kvm,int irqnum,int level)
{
	uint32_t inten,irtr,i;
	struct kvm_mips_interrupt irq;
	struct loongson_kvm_ls3a_routerirq *s = ls3a_router_irqchip(kvm);
	uint8_t core_mask,irq_nr,ip_num=2;
	uint8_t *mem = s->ls3a_router_reg;
	int nrcpus;
	unsigned long flags;

	if(irqnum < 0 || irqnum >= 32)
		return;

	ls3a_router_irq_lock(ls3a_router_irqchip(kvm), flags);

	inten = *(uint32_t *)(mem + INT_ROUTER_REGS_EN);
	irtr = *(uint8_t *)(mem + irqnum);
	/* core irq pin HT route to */
	irq_nr = ((irtr >> 4) & 0xf);
	core_mask = irtr & 0xf;
	if ((inten & (1UL << irqnum)) == 0)
		goto route_update_fail;

	ip_num = __ffs(irq_nr) + 2;
	if (ip_num > 5) {
		printk("%s(%d): ipp num %d larger than 5\n", __FUNCTION__, __LINE__, ip_num);
		goto route_update_fail;
	}

	i = __ffs(core_mask);
	nrcpus = atomic_read(&kvm->online_vcpus);
	if (i > (nrcpus - 1)) {
		i = 0;
	}

	if (level == 1) {
		*(uint32_t *)(mem + INT_ROUTER_REGS_ISR) |= (1ULL<<irqnum);
		*(uint32_t *)(mem + core_route_state[i]) |= (1ULL<<irqnum);
		irq.cpu = i;
		irq.irq = ip_num;
		kvm->arch.core_ip_mask [i][ip_num - 2] |= 0x1;
		kvm_vcpu_ioctl_interrupt(kvm->vcpus[i],&irq);

	} else {
		*(uint32_t *)(mem + INT_ROUTER_REGS_ISR) &= (~(1ULL<<irqnum));
		*(uint32_t *)(mem + core_route_state[i]) &= (~(1ULL<<irqnum));
		if (*(uint32_t *)(mem + core_route_state[i]) == 0) {
			kvm->arch.core_ip_mask[i][ip_num -2] &= ~0x1;
			if (kvm->arch.core_ip_mask[i][ip_num -2] == 0) {
				irq.cpu = i;
				irq.irq = -ip_num;
				kvm_vcpu_ioctl_interrupt(kvm->vcpus[i],&irq);\
			}
		}
	}
	ls3a_router_irq_unlock(ls3a_router_irqchip(kvm), flags);

	return;

route_update_fail:
	ls3a_router_irq_unlock(ls3a_router_irqchip(kvm), flags);

	return;
}

static uint64_t ls3a_router_intctl_read(struct kvm *kvm, gpa_t addr, unsigned size,void *val)
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
	kvm->stat.lsvz_kvm_ls3a_router_read_exits++;
	return ret;
}

static int ls3a_router_intctl_write(struct kvm *kvm , gpa_t addr, unsigned size, const void *val)
{
	uint64_t val_data,offset;
	uint8_t *mem;
	struct loongson_kvm_ls3a_routerirq *s = ls3a_router_irqchip(kvm);

	mem = s->ls3a_router_reg;
	offset = addr & 0xff;
	val_data = *(uint64_t *)val;
	if (offset >= 0 && (offset < 0x20)) {
		while ((size > 0) && (offset < 0x20)) {
			*(uint8_t *)(mem + offset) = (uint8_t)(val_data);
			size --;
			offset ++;
			val_data = val_data >> 8;
		}
	}

	/* offset == 0x28 */
	if (offset == INT_ROUTER_REGS_EN_SET) {
		if (size >= 4) {
			*(uint32_t *)(mem + INT_ROUTER_REGS_EN) |= (uint32_t)(val_data);
			offset += 4;
			size -= 4;
			val_data = val_data >> 32;
		}
	}


	/* offset == 0x2c */
	if (offset == INT_ROUTER_REGS_EN_CLR) {
		if(size >= 4) {
			*(uint32_t *)(mem + INT_ROUTER_REGS_EN) &= ~((uint32_t)(val_data));
			*(uint32_t *)(mem + INT_ROUTER_REGS_ISR) &= ~((~((uint32_t)(val_data))) & (*(uint32_t *)(mem + INT_ROUTER_REGS_EDGE)));
			offset += 4;
			size -= 4;
			val_data = val_data >> 32;
		}
	}

	/* offset == 0x38 */
	if (offset == INT_ROUTER_REGS_EDGE) {
		if (size >=4 ) {
			*(uint32_t *)(mem + offset) = (uint32_t)(val_data);
			offset += 4;
			size -= 4;
			val_data = val_data >> 32;
		}
	}

	if (size) {
		printk("%s remaining size %d offset %llx\n", __FUNCTION__, size, offset);
	}
	kvm->stat.lsvz_kvm_ls3a_router_write_exits++;

	return 0;
}

static int kvm_ls3a_router_write(struct kvm_io_device * dev,
		gpa_t addr, int len, const void *val)
{
	struct loongson_kvm_ls3a_routerirq *s;
	uint64_t result = 0;
	unsigned long flags;

	s = container_of(dev, struct loongson_kvm_ls3a_routerirq, dev_ls3a_router_irq);
	ls3a_router_irq_lock(s, flags);
	result = ls3a_router_intctl_write(s->kvm,addr,len,val);
	ls3a_router_irq_unlock(s, flags);

	return result;
}


static int kvm_ls3a_router_read(struct kvm_io_device *dev,
		gpa_t addr, int len, void *val)
{
	struct loongson_kvm_ls3a_routerirq *s;
	uint64_t result=0;
	unsigned long flags;

	s = container_of(dev, struct loongson_kvm_ls3a_routerirq, dev_ls3a_router_irq);
	ls3a_router_irq_lock(s, flags);
	result = ls3a_router_intctl_read(s->kvm,addr,len,val);
	ls3a_router_irq_unlock(s, flags);

	return 0;
}

static const struct kvm_io_device_ops kvm_ls3a_router_irq_ops = {
	.read     = kvm_ls3a_router_read,
	.write    = kvm_ls3a_router_write,
};

struct loongson_kvm_ls3a_routerirq *kvm_create_ls3a_router_irq(struct kvm *kvm)
{
	struct loongson_kvm_ls3a_routerirq *s;
	int ret,i,j;

	s = kzalloc(sizeof(struct loongson_kvm_ls3a_routerirq), GFP_KERNEL);
	if (!s)
		return NULL;
	spin_lock_init(&s->lock);
	s->kvm = kvm;


	kvm_iodevice_init(&s->dev_ls3a_router_irq, &kvm_ls3a_router_irq_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, INT_ROUTER_REGS_BASE, 0x100,
			&s->dev_ls3a_router_irq);
	if (ret < 0)
		goto fail_unlock;

	mutex_unlock(&kvm->slots_lock);
	for (i=0;i<4;i++) {
		for (j=0;j<4;j++) {
			kvm->arch.core_ip_mask[i][j]=0;
		}
	}
	return s;

fail_unlock:
	mutex_unlock(&kvm->slots_lock);
	kfree(s);

	return NULL;
}

int kvm_get_ls3a_router_irq(struct kvm *kvm, uint8_t *state)
{
	struct loongson_kvm_ls3a_routerirq *ls3a_router_irq = ls3a_router_irqchip(kvm);
	uint8_t *router_irq_reg =  ls3a_router_irq->ls3a_router_reg;
	unsigned long flags;

	ls3a_router_irq_lock(ls3a_router_irq, flags);
	memcpy(state, router_irq_reg, 0x100);
	ls3a_router_irq_unlock(ls3a_router_irq, flags);
	kvm->stat.lsvz_kvm_get_ls3a_router_irq++;
	return 0;
}

int kvm_set_ls3a_router_irq(struct kvm *kvm, uint8_t *state)
{
	struct loongson_kvm_ls3a_routerirq *ls3a_router_irq = ls3a_router_irqchip(kvm);
	uint8_t *router_irq_reg =  ls3a_router_irq->ls3a_router_reg;
	unsigned long flags;
	if (!router_irq_reg)
		return -EINVAL;

	ls3a_router_irq_lock(ls3a_router_irq, flags);
	memcpy(router_irq_reg, state, 0x100);
	ls3a_router_irq_unlock(ls3a_router_irq, flags);
	kvm->stat.lsvz_kvm_set_ls3a_router_irq++;
	return 0;
}

