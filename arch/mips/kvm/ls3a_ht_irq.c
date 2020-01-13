/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * currently used for Loongson3A Virtual HT interrupt.
 * Mapped into the guest kernel @ KVM_GUEST_COMMPAGE_ADDR.
 *
 * Copyright (C) 2019  Loongson Technologies, Inc.  All rights reserved.
 * Authors: Chen Zhu <zhuchen@loongson.cn>
 */

#include "ls3a_ht_irq.h"
#include "ls3a_router_irq.h"
#include "ls3a3000.h"
#include "ls3a_ext_irq.h"
#include <linux/random.h>


static uint32_t ht_irq_vector[] = {
	HT_IRQ_VECTOR_REG0,
	HT_IRQ_VECTOR_REG1,
	HT_IRQ_VECTOR_REG2,
	HT_IRQ_VECTOR_REG3,
	HT_IRQ_VECTOR_REG4,
	HT_IRQ_VECTOR_REG5,
	HT_IRQ_VECTOR_REG6,
	HT_IRQ_VECTOR_REG7
};

static uint32_t ht_irq_enable[] = {
	HT_IRQ_ENABLE_REG0,
	HT_IRQ_ENABLE_REG1,
	HT_IRQ_ENABLE_REG2,
	HT_IRQ_ENABLE_REG3,
	HT_IRQ_ENABLE_REG4,
	HT_IRQ_ENABLE_REG5,
	HT_IRQ_ENABLE_REG6,
	HT_IRQ_ENABLE_REG7
};

#define ls3a_ht_irq_lock(s, flags)	spin_lock_irqsave(&s->lock, flags)
#define ls3a_ht_irq_unlock(s, flags)	spin_unlock_irqrestore(&s->lock, flags)

static inline void ht_raise_irq (struct kvm *kvm,int regnum, unsigned int mask)
{
	uint32_t ier;
	struct loongson_kvm_ls3a_htirq *s = ls3a_ht_irqchip(kvm);

	ier = *(uint32_t *)(s->ht_irq_reg + ht_irq_enable[regnum]);
	if (mask & ier) {
		route_update_reg(kvm, regnum+24, 1);
	}
}

static inline void ht_lower_irq (struct kvm *kvm,int regnum, unsigned int mask)
{
	uint32_t isr, ier;
	struct loongson_kvm_ls3a_htirq *s = ls3a_ht_irqchip(kvm);

	isr = *(uint32_t *)(s->ht_irq_reg + ht_irq_vector[regnum]);
	ier = *(uint32_t *)(s->ht_irq_reg + ht_irq_enable[regnum]);

	if (((isr & ier) == 0) && (mask & ier)) {
		route_update_reg(kvm, regnum + 24, 0);
	}

}

void ht_irq_handler(struct kvm *kvm,int irq,int level)
{
	uint32_t reg_num,reg_bit;
	struct loongson_kvm_ls3a_htirq *s = ls3a_ht_irqchip(kvm);

	reg_num = irq/32;
	reg_bit = irq%32;
	if (level == 1) {
		*(uint32_t *)(s->ht_irq_reg + ht_irq_vector[reg_num]) |=  1 << reg_bit;
		ht_raise_irq(kvm, reg_num, 1 << reg_bit);
	} else {
		*(uint32_t *)(s->ht_irq_reg + ht_irq_vector[reg_num]) &= ~(1 << reg_bit);
		ht_lower_irq(kvm, reg_num, 1 << reg_bit);
	}
}

void msi_irq_handler(struct kvm *kvm,int irq,int level)
{
	struct loongson_kvm_ls3a_extirq *s = ls3a_ext_irqchip(kvm);
	struct kvm_ls3a_extirq_state *state = &(s->ls3a_ext_irq);
	if((current_cpu_type() == CPU_LOONGSON3_COMP)&&((state->ext_en.reg_u64)&(0x1ULL << 48))) {
		ext_irq_handler(kvm,irq,level);
	} else {
		ht_irq_handler(kvm,irq,level);
	}
}


static uint64_t ls3a_ht_intctl_read(struct kvm *kvm, gpa_t addr, unsigned size,void* val)
{
	uint64_t offset;
	uint64_t ret = 0;
	static int linkcfg = 0;
	uint8_t *mem;
	struct loongson_kvm_ls3a_htirq *s = ls3a_ht_irqchip(kvm);

	mem = s->ht_irq_reg;
	offset = addr&0xff;

	if (offset & (size -1 )) {
		printk("%s(%d):unaligned address access %llx size %d \n",
				__FUNCTION__, __LINE__, addr, size);
		return 0;

	}

	switch(size) {
	case 8:
		*(uint64_t *)val = *(uint64_t *)(mem + offset);
		break;
	case 4:
		if(addr == HT_LINK_CONFIG_REG) {
			linkcfg = get_random_int();
			//linkcfg = 0;
			*(uint32_t *)val = linkcfg;
		} else {
			*(uint32_t *)val = (uint32_t)(*(uint32_t *)(mem + offset));
		}
		break;
	case 2:
		*(uint16_t *)val = (uint16_t)(*(uint16_t *)(mem + offset));
		break;
	case 1:
		*(uint8_t *)val = (uint8_t)(*(uint8_t *)(mem + offset));
		break;
	default:
		WARN_ONCE(1,"Abnormal address access:addr 0x%llx,size %d\n",addr,size);
		ret = 0;
	}
	kvm->stat.lsvz_kvm_ls3a_ht_read_exits++;
	return ret;
}

static void ls3a_ht_intctl_write(struct kvm *kvm, gpa_t addr,unsigned size, const void *val)
{
	uint32_t regnum, old, new, isr;
	uint64_t offset,data;
	uint8_t *mem;
	struct loongson_kvm_ls3a_htirq *s = ls3a_ht_irqchip(kvm);

	offset = addr & 0xff;
	if (offset & (size -1 )) {
		printk("%s(%d):unaligned address access %llx size %d \n",
				__FUNCTION__, __LINE__, addr, size);
		return;

	}
	mem = s->ht_irq_reg;

	data = *(uint64_t *)val;

	switch(offset) {
	case HT_IRQ_VECTOR_REG0:
	case HT_IRQ_VECTOR_REG1:
	case HT_IRQ_VECTOR_REG2:
	case HT_IRQ_VECTOR_REG3:
	case HT_IRQ_VECTOR_REG4:
	case HT_IRQ_VECTOR_REG5:
	case HT_IRQ_VECTOR_REG6:
	case HT_IRQ_VECTOR_REG7:
		while (size >= 4) {
			regnum = (offset - HT_IRQ_VECTOR_REG0) >> 2;
			*(uint32_t *)(mem + offset) &= ~((uint32_t)data);
			ht_lower_irq(kvm, regnum, (uint32_t)data);
			offset += 4;
			size -= 4;
			data = data >> 32;
		}
		break;
	case HT_IRQ_ENABLE_REG0:
	case HT_IRQ_ENABLE_REG1:
	case HT_IRQ_ENABLE_REG2:
	case HT_IRQ_ENABLE_REG3:
	case HT_IRQ_ENABLE_REG4:
	case HT_IRQ_ENABLE_REG5:
	case HT_IRQ_ENABLE_REG6:
	case HT_IRQ_ENABLE_REG7:
		while(size >= 4) {
			regnum = (offset - HT_IRQ_ENABLE_REG0) >> 2;
			old = *(uint32_t *)(mem + offset);
			new = (uint32_t)data;
			*(uint32_t *)(mem + offset) = new;
			isr = *(uint32_t *)(mem + ht_irq_vector[regnum]);
			if ((new & ~old) & isr) {
				if ((isr & old) == 0)
					route_update_reg(kvm, regnum+24, 1);
			} else if ((old & ~new) & isr) {
				if ((isr & new) == 0)
					route_update_reg(kvm, regnum+24, 0);
			}
			offset += 4;
			size -= 4;
			data = data >> 32;
		}
		break;
	default:
		if(8 == size) {
			*(uint64_t *)(mem + offset) = data;
			size -= 8;
			offset += 8;
		} else if (4 == size) {
			*(uint32_t *)(mem + offset) = (uint32_t)data;
			size -= 4;
			offset += 4;
		} else if (2 == size) {
			*(uint16_t *)(mem + offset) = (uint16_t)data;
			size -= 2;
			offset += 2;
		} else if (1 == size) {
			*(uint8_t *)(mem + offset) = (uint8_t)data;
			size --;
			offset ++;
		}
		break;
	}
	if (size != 0) {
		printk("%s(%d):Abnormal register write %llx size %d \n",
				__FUNCTION__, __LINE__, offset, size);
	}
	kvm->stat.lsvz_kvm_ls3a_ht_write_exits++;
}


static int kvm_ls3a_ht_read(struct kvm_io_device *dev,
		gpa_t addr, int len, void *val)
{
	struct loongson_kvm_ls3a_htirq *s;
	uint64_t result=0;
	unsigned long flags;

	s = container_of(dev, struct loongson_kvm_ls3a_htirq, dev_ls3a_ht_irq);
	
	ls7a_ioapic_lock(s->kvm->arch.v_ioapic, &flags);
	result = ls3a_ht_intctl_read(s->kvm,addr,len,val);
	ls7a_ioapic_unlock(s->kvm->arch.v_ioapic, &flags);

	return 0;
}


static int kvm_ls3a_ht_write(struct kvm_io_device * dev,
		gpa_t addr, int len, const void *val)
{
	struct loongson_kvm_ls3a_htirq *s;
	unsigned long flags;

	s = container_of(dev, struct loongson_kvm_ls3a_htirq, dev_ls3a_ht_irq);

	ls7a_ioapic_lock(s->kvm->arch.v_ioapic, &flags);
	ls3a_ht_intctl_write(s->kvm,addr,len,val);
	ls7a_ioapic_unlock(s->kvm->arch.v_ioapic, &flags);

	return 0;
}

static const struct kvm_io_device_ops kvm_ls3a_ht_irq_ops = {
	.read     = kvm_ls3a_ht_read,
	.write    = kvm_ls3a_ht_write,
};

struct loongson_kvm_ls3a_htirq *kvm_create_ls3a_ht_irq(struct kvm *kvm)
{
	struct loongson_kvm_ls3a_htirq *s;
	int ret;
	unsigned long ht_control_reg_base;

	s = kzalloc(sizeof(struct loongson_kvm_ls3a_htirq), GFP_KERNEL);
	if (!s)
		return NULL;
	spin_lock_init(&s->lock);
	s->kvm = kvm;

	if (current_cpu_type() == CPU_LOONGSON3_COMP) {
		ht_control_reg_base = LS3A4000_HT_CONTROL_REGS_BASE;
	} else {
		ht_control_reg_base = HT_CONTROL_REGS_BASE;
	}

	kvm_iodevice_init(&s->dev_ls3a_ht_irq, &kvm_ls3a_ht_irq_ops);
	mutex_lock(&kvm->slots_lock);

	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, ht_control_reg_base, 0x100,
			&s->dev_ls3a_ht_irq);
	if (ret < 0)
		goto fail_unlock;

	mutex_unlock(&kvm->slots_lock);
	return s;

fail_unlock:
	mutex_unlock(&kvm->slots_lock);
	kfree(s);

	return NULL;
}

int kvm_get_ls3a_ht_irq(struct kvm *kvm, uint8_t *state)
{
	struct loongson_kvm_ls3a_htirq *ls3a_ht_irq = ls3a_ht_irqchip(kvm);
	uint8_t *ht_irq_reg =  ls3a_ht_irq->ht_irq_reg;
	unsigned long flags;

	ls3a_ht_irq_lock(ls3a_ht_irq, flags);
	memcpy(state, ht_irq_reg, 0x100);
	ls3a_ht_irq_unlock(ls3a_ht_irq, flags);
	kvm->stat.lsvz_kvm_get_ls3a_ht_irq++;
	return 0;
}

int kvm_set_ls3a_ht_irq(struct kvm *kvm, uint8_t *state)
{
	struct loongson_kvm_ls3a_htirq *ls3a_ht_irq = ls3a_ht_irqchip(kvm);
	uint8_t *ht_irq_reg =  ls3a_ht_irq->ht_irq_reg;
	unsigned long flags;
	if (!ht_irq_reg)
		return -EINVAL;

	ls3a_ht_irq_lock(ls3a_ht_irq, flags);
	memcpy(ht_irq_reg, state, 0x100);
	ls3a_ht_irq_unlock(ls3a_ht_irq, flags);
	kvm->stat.lsvz_kvm_set_ls3a_ht_irq++;
	return 0;
}


void kvm_destroy_ls3a_htirq(struct loongson_kvm_ls3a_htirq *v_htirq)
{
	kvm_io_bus_unregister_dev(v_htirq->kvm, KVM_MMIO_BUS, &v_htirq->dev_ls3a_ht_irq);
	kfree(v_htirq);
}
