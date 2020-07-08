/*
 * ls3a3a000_ht_irq.h: in kernel Loongson3A HT interrupt controller related definitions
 * Copyright (c) 2019, Loongson Technology.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Authors:
 *   Chen Zhu <zhuchen@loongson.cn>
 *
 */

#ifndef __LS3A_KVM_HT_IRQ_H
#define __LS3A_KVM_HT_IRQ_H


#include <linux/mm_types.h>
#include <linux/hrtimer.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>

#include "iodev.h"

#define HT_CONTROL_REGS_BASE            0x0effb000000UL
#define LS3A4000_HT_CONTROL_REGS_BASE            0x0efdfb000000UL
#define HT_CONTROL_REGS_SIZE            0x100
#define HT_LINK_CONFIG_REG              0x44
#define HT_IRQ_VECTOR_REG0              0x80
#define HT_IRQ_VECTOR_REG1              0x84
#define HT_IRQ_VECTOR_REG2              0x88
#define HT_IRQ_VECTOR_REG3              0x8C
#define HT_IRQ_VECTOR_REG4              0x90
#define HT_IRQ_VECTOR_REG5              0x94
#define HT_IRQ_VECTOR_REG6              0x98
#define HT_IRQ_VECTOR_REG7              0x9C
#define HT_IRQ_ENABLE_REG0              0xA0
#define HT_IRQ_ENABLE_REG1              0xA4
#define HT_IRQ_ENABLE_REG2              0xA8
#define HT_IRQ_ENABLE_REG3              0xAC
#define HT_IRQ_ENABLE_REG4              0xB0
#define HT_IRQ_ENABLE_REG5              0xB4
#define HT_IRQ_ENABLE_REG6              0xB8
#define HT_IRQ_ENABLE_REG7              0xBC


struct loongson_kvm_ls3a_htirq {
	spinlock_t lock;
	struct kvm *kvm;
	struct kvm_io_device dev_ls3a_ht_irq;
	uint8_t ht_irq_reg[0x100];
};

static inline struct loongson_kvm_ls3a_htirq *ls3a_ht_irqchip(struct kvm *kvm)
{
	return kvm->arch.v_htirq;
}

static inline int ls3a_htirq_in_kernel(struct kvm *kvm)
{
	int ret;

	ret = (ls3a_ht_irqchip(kvm) != NULL);
	return ret;
}


void ht_update_irqreg(struct kvm *kvm,int regnum ,int disable);
void ht_irq_handler(struct kvm *kvm,int irq,int level);
void msi_irq_handler(struct kvm *kvm,int irq,int level);
struct loongson_kvm_ls3a_htirq *kvm_create_ls3a_ht_irq(struct kvm *kvm);
void kvm_destroy_ls3a_htirq(struct loongson_kvm_ls3a_htirq *v_htirq);
int kvm_get_ls3a_ht_irq(struct kvm *kvm, uint8_t *state);
int kvm_set_ls3a_ht_irq(struct kvm *kvm, uint8_t *state);
#endif
