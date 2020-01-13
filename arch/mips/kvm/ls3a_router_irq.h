/*
 * ls3a3000_router_irq.h: in kernel Loongson7A interrupt controller related definitions
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

#ifndef __LS3A_KVM_ROUTER_IRQ_H
#define __LS3A_KVM_ROUTER_IRQ_H

#include <linux/mm_types.h>
#include <linux/hrtimer.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>

#include "iodev.h"



#define INT_ROUTER_REGS_BASE            0x3ff01400UL
#define INT_ROUTER_REGS_SIZE            0x100
#define INT_ROUTER_REGS_SYS_INT0        0x00
#define INT_ROUTER_REGS_SYS_INT1        0x01
#define INT_ROUTER_REGS_SYS_INT2        0x02
#define INT_ROUTER_REGS_SYS_INT3        0x03
#define INT_ROUTER_REGS_PCI_INT0        0x04
#define INT_ROUTER_REGS_PCI_INT1        0x05
#define INT_ROUTER_REGS_PCI_INT2        0x06
#define INT_ROUTER_REGS_PCI_INT3        0x07
#define INT_ROUTER_REGS_MATRIX_INT0     0x08
#define INT_ROUTER_REGS_MATRIX_INT1     0x09
#define INT_ROUTER_REGS_LPC_INT         0x0a
#define INT_ROUTER_REGS_MC0             0x0b
#define INT_ROUTER_REGS_MC1             0x0c
#define INT_ROUTER_REGS_BARRIER         0x0d
#define INT_ROUTER_REGS_THSENS_INT      0x0e
#define INT_ROUTER_REGS_PCI_PERR        0x0f
#define INT_ROUTER_REGS_HT0_INT0        0x10
#define INT_ROUTER_REGS_HT0_INT1        0x11
#define INT_ROUTER_REGS_HT0_INT2        0x12
#define INT_ROUTER_REGS_HT0_INT3        0x13
#define INT_ROUTER_REGS_HT0_INT4        0x14
#define INT_ROUTER_REGS_HT0_INT5        0x15
#define INT_ROUTER_REGS_HT0_INT6        0x16
#define INT_ROUTER_REGS_HT0_INT7        0x17
#define INT_ROUTER_REGS_HT1_INT0        0x18
#define INT_ROUTER_REGS_HT1_INT1        0x19
#define INT_ROUTER_REGS_HT1_INT2        0x1a
#define INT_ROUTER_REGS_HT1_INT3        0x1b
#define INT_ROUTER_REGS_HT1_INT4        0x1c
#define INT_ROUTER_REGS_HT1_INT5        0x1d
#define INT_ROUTER_REGS_HT1_INT6        0x1e
#define INT_ROUTER_REGS_HT1_INT7        0x1f
#define INT_ROUTER_REGS_ISR             0x20
#define INT_ROUTER_REGS_EN              0x24
#define INT_ROUTER_REGS_EN_SET          0x28
#define INT_ROUTER_REGS_EN_CLR          0x2c
#define INT_ROUTER_REGS_EDGE            0x38
#define INT_ROUTER_REGS_CORE0_INTISR    0x40
#define INT_ROUTER_REGS_CORE1_INTISR    0x48
#define INT_ROUTER_REGS_CORE2_INTISR    0x50
#define INT_ROUTER_REGS_CORE3_INTISR    0x58


struct loongson_kvm_ls3a_routerirq{
	spinlock_t lock;
	struct kvm *kvm;
	struct kvm_io_device dev_ls3a_router_irq;
	uint8_t ls3a_router_reg[0x100];
};

static inline struct loongson_kvm_ls3a_routerirq *ls3a_router_irqchip(struct kvm *kvm)
{
	return kvm->arch.v_routerirq;
}

static inline int ls3a_router_in_kernel(struct kvm *kvm)
{
	int ret;

	ret = (ls3a_router_irqchip(kvm) != NULL);
	return ret;
}


void route_update_reg(struct kvm *kvm,int irqnum,int level);
struct loongson_kvm_ls3a_routerirq *kvm_create_ls3a_router_irq(struct kvm *kvm);
int kvm_get_ls3a_router_irq(struct kvm *kvm, uint8_t *state);
int kvm_set_ls3a_router_irq(struct kvm *kvm, uint8_t *state);
#endif
