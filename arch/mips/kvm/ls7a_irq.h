/*
 * ls7a_irq.h: in kernel Loongson7A interrupt controller related definitions
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

#ifndef __LS7A_KVM_IRQ_H
#define __LS7A_KVM_IRQ_H

#include <linux/mm_types.h>
#include <linux/hrtimer.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>

#include "iodev.h"

#define LS7A_APIC_NUM_PINS 64


/* KVM_IRQ_LINE irq field index values */
#define KVM_LOONGSON_IRQ_TYPE_SHIFT		24
#define KVM_LOONGSON_IRQ_TYPE_MASK		0xff
#define KVM_LOONGSON_IRQ_VCPU_SHIFT		16
#define KVM_LOONGSON_IRQ_VCPU_MASK		0xff
#define KVM_LOONGSON_IRQ_NUM_SHIFT		0
#define KVM_LOONGSON_IRQ_NUM_MASK		0xffff

/* irq_type field */
#define KVM_LOONGSON_IRQ_TYPE_CPU_IP		0
#define KVM_LOONGSON_IRQ_TYPE_CPU_IO		1
#define KVM_LOONGSON_IRQ_TYPE_HT		2
#define KVM_LOONGSON_IRQ_TYPE_MSI		3
#define KVM_LOONGSON_IRQ_TYPE_IOAPIC		4
#define KVM_LOONGSON_IRQ_TYPE_ROUTE		5

/* out-of-kernel GIC cpu interrupt injection irq_number field */
#define KVM_LOONGSON_IRQ_CPU_IRQ		0
#define KVM_LOONGSON_IRQ_CPU_FIQ		1

#define KVM_LOONGSON_CPU_IP_NUM			8

#define LS7A_IOAPIC_GUEST_REG_BASE          0x000000e010000000UL
#define LS7A_IOAPIC_GUEST_REG_BASE_ALIAS    0x0000000010000000UL
#define LS3A4000_LS7A_IOAPIC_GUEST_REG_BASE_ALIAS    0x00000e0010000000UL

#define LS7A_IOAPIC_GUEST_GPA_BASE          0x900000e010000000UL

#define LS7A_IOAPIC_NUM_PINS 64

typedef struct kvm_ls7a_ioapic_state {
    u64 int_id;
    u64 int_mask; /*0x020 interrupt mask register*/
    u64 htmsi_en;/*0x040 1=msi*/
    u64 intedge; /*0x060 edge=1 level  =0*/
    u64 intclr; /*0x080 for clean edge int,set 1 clean,set 0 is noused*/
    u64 auto_crtl0; /*0x0c0*/
    u64 auto_crtl1; /*0x0e0*/
    u8 route_entry[64]; /*0x100 - 0x140*/
    u8 htmsi_vector[64]; /*0x200 - 0x240*/
    u64 intisr_chip0; /*0x300*/
    u64 intisr_chip1;/*0x320*/
    u64 last_intirr;	/* edge detection */
    u64 intirr; /* 0x380 interrupt request register */
    u64 intisr; /* 0x3a0 interrupt service register */
    u64 int_polarity; /*0x3e0 interrupt level polarity selection register 0 for high level tirgger*/
}LS7AApicState;


struct loongson_kvm_7a_ioapic {
	spinlock_t lock;
	bool wakeup_needed;
	unsigned pending_acks;
	struct kvm *kvm;
	struct kvm_ls7a_ioapic_state ls7a_ioapic;
	struct kvm_io_device dev_ls7a_ioapic;
	void (*ack_notifier)(void *opaque, int irq);
	unsigned long irq_states[LS7A_APIC_NUM_PINS];
	void * read_page_address;
	struct page * read_page;
};

struct loongson_kvm_7a_ioapic *kvm_create_pic(struct kvm *kvm);
void kvm_destroy_pic(struct loongson_kvm_7a_ioapic *vpic);
int kvm_7a_ioapic_read_irq(struct kvm *kvm);
void kvm_7a_ioapic_update_irq(struct loongson_kvm_7a_ioapic *s);

static inline struct loongson_kvm_7a_ioapic *ls7a_ioapic_irqchip(struct kvm *kvm)
{
	return kvm->arch.v_ioapic;
}

static inline int ls7a_ioapic_in_kernel(struct kvm *kvm)
{
	int ret;

	ret = (ls7a_ioapic_irqchip(kvm) != NULL);
	return ret;
}

#ifdef CONFIG_KVM_LOONGSON_IOAPIC_READ_OPT
	static inline void ls7a_update_read_page_long(struct loongson_kvm_7a_ioapic *s,unsigned int addr,unsigned long value)
	{
		*(unsigned long *)(s->read_page_address + addr) = value;
	}
	static inline void ls7a_update_read_page_char(struct loongson_kvm_7a_ioapic *s,unsigned int addr,unsigned char value)
	{
		*(unsigned char *)(s->read_page_address + addr) = value;
	}
#else
	static inline void ls7a_update_read_page_long(struct loongson_kvm_7a_ioapic *s,unsigned int addr,unsigned long value)
	{
		return;
	}
	static inline void ls7a_update_read_page_char(struct loongson_kvm_7a_ioapic *s,unsigned int addr,unsigned char value)
	{
		return;
	}
#endif

int kvm_set_ls7a_ioapic(struct kvm *kvm, struct kvm_loongson_ls7a_ioapic_state *state);
int kvm_get_ls7a_ioapic(struct kvm *kvm, struct kvm_loongson_ls7a_ioapic_state *state);
struct loongson_kvm_7a_ioapic *kvm_create_ls7a_ioapic(struct kvm *kvm);
int kvm_ls7a_ioapic_set_irq(struct kvm *kvm, int irq, int level);

void kvm_destroy_ls7a_ioapic(struct loongson_kvm_7a_ioapic *vpic);
void ls7a_ioapic_lock(struct loongson_kvm_7a_ioapic *s, unsigned long *flags);
void ls7a_ioapic_unlock(struct loongson_kvm_7a_ioapic *s, unsigned long *flags);
int kvm_ls7a_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi);
int kvm_ls7a_ioapic_set_irq(struct kvm *kvm, int irq, int level);
#endif
