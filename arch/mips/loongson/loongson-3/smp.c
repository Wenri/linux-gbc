/*
 * Copyright (C) 2010, 2011, 2012, Lemote, Inc.
 * Author: Chen Huacai, chenhc@lemote.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/clock.h>
#include <asm/tlbflush.h>
#include <asm/mach-loongson/mmzone.h>
#include <loongson.h>
#include <irq.h>
#include <workarounds.h>
#include <loongson-pch.h>

#include "smp.h"

DEFINE_PER_CPU(int, cpu_state);
extern int hpet_enabled;

static void *ipi_set0_regs[16];
static void *ipi_clear0_regs[16];
static void *ipi_status0_regs[16];
static void *ipi_en0_regs[16];
static void *ipi_mailbox_buf[16];
static uint32_t core0_c0count[NR_CPUS];

#ifdef CONFIG_LOONGSON3_CPUAUTOPLUG
extern int autoplug_verbose;
#define verbose autoplug_verbose
#else
#define verbose 1
#endif

void	(*loongson3_ipi)(struct pt_regs *regs);

extern void fixup_irq_route(bool online);
extern unsigned short ls3a_ipi_pos2irq[];
extern unsigned int ls2h_pos2irq[];
extern unsigned int rs780e_pos2irq[];

#define MAX_LOOPS 800

/* send ipi through IPI_Send */

static void loongson3_comp_ipi_send_csr(uint32_t action, int cpu)
{
	unsigned int irq = 0;
	uint32_t val = (1 << LOONGSON_IPI_SEND_BLOCK_SHIFT);
	while ((irq = ffs(action))) {
		val |= (irq - 1);
		val |= (cpu << LOONGSON_IPI_SEND_CPU_SHIFT);
		write_csr(LOONGSON_IPI_SEND_OFFSET, val);
		action &= ~(1 << (irq - 1));
	}
};

/* send mail through Mail_Send */
static void loongson3_comp_mail_send_csr(uint64_t mail, int cpu, int mailbox)
{
	uint64_t val = (uint32_t)(1 << LOONGSON_MAIL_SEND_BLOCK_SHIFT);
	val |= (LOONGSON_MAIL_SEND_BOX_TO_HIGHT(mailbox) << LOONGSON_MAIL_SEND_BOX_SHIFT);
	val |= (cpu << LOONGSON_MAIL_SEND_CPU_SHIFT);
	val |= (mail & LOONGSON_MAIL_SEND_H32_MASK);
	dwrite_csr(LOONGSON_MAIL_SEND_OFFSET, val);

	val = (uint32_t)(1 << LOONGSON_MAIL_SEND_BLOCK_SHIFT);
	val |= (LOONGSON_MAIL_SEND_BOX_TO_LOW(mailbox) << LOONGSON_MAIL_SEND_BOX_SHIFT);
	val |= (cpu << LOONGSON_MAIL_SEND_CPU_SHIFT);
	val |= (mail << LOONGSON_MAIL_SEND_VAL_SHIFT);
	dwrite_csr(LOONGSON_MAIL_SEND_OFFSET, val);
};

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson3_comp_send_ipi_single(int cpu, unsigned int action)
{
	loongson3_comp_ipi_send_csr(action, cpu_logical_map(cpu));
}

static void loongson3_comp_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;
	for_each_cpu(i, mask)
		loongson3_comp_ipi_send_csr(action, cpu_logical_map(i));
}

unsigned int (*read_clear_ipi)(int);
unsigned int addr_read_clear_ipi(int cpu)
{
	unsigned int action = ls64_conf_read32(ipi_status0_regs[cpu_logical_map(cpu)]);
	ls64_conf_write32((unsigned int)action, ipi_clear0_regs[cpu_logical_map(cpu)]);
	return action;
}

unsigned int csr_read_clear_ipi(int cpu)
{
	unsigned int action = read_csr(LOONGSON_IPI_STATUS_OFFSET);
	smp_mb();
	write_csr(LOONGSON_IPI_CLEAR_OFFSET, (unsigned int)action);
	return action;
}

static void handle_io_ipi(unsigned int irqs)
{
	int irq, irq1;
	switch (loongson_pch->board_type) {
	case RS780E:
		while ((irq = ffs(irqs))) {
			irq = irq -1;
			irqs &= ~(1<<irq);
			if(irq < 16)
				do_IRQ(irq);
			else {
				irq1 = rs780e_pos2irq[irq - 16];
				if (likely(irq1 != 0)) do_IRQ(irq1 - 1);
			}
		}
	break;
	case LS2H:
		while ((irq = ffs(irqs))) {
			irq1 = ls2h_pos2irq[irq - 1];
			if (likely(irq1 != 0)) do_IRQ(irq1 - 1);
			irqs &= ~(1<<(irq-1));
		}
	break;
	case LS7A:
		while ((irq = ffs(irqs))) {
			irq1 = ls3a_ipi_pos2irq[irq-1];
			if (likely(irq1 != (unsigned short)-1)) do_IRQ(irq1);
			irqs &= ~(1<<(irq-1));
		}
		break;
	default:
	break;
	}

}

void loongson3_ipi_interrupt(struct pt_regs *regs)
{
	int i, cpu = smp_processor_id();
	unsigned int action, c0count, irqs;

	action = read_clear_ipi(cpu);

	if (action & SMP_RESCHEDULE_YOURSELF) {
		scheduler_ipi();
	}

	if (action & SMP_CALL_FUNCTION) {
		smp_call_function_interrupt();
	}

	if (action & SMP_ASK_C0COUNT) {
		BUG_ON(cpu != 0);
		c0count = read_c0_count();
		c0count = c0count ? c0count : 1;
		for (i = 1; i < nr_cpu_ids; i++)
			core0_c0count[i] = c0count;
		__wbflush(); /* Let others see the result ASAP */
	}

	irqs = action >> IPI_IRQ_OFFSET;
	if (irqs) handle_io_ipi(irqs);
}

/*
 * SMP init and finish on secondary CPUs
 */
static void  loongson3_comp_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
#ifdef CONFIG_LOONGSON_SUPPORT_IP5
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
				STATUSF_IP5 | STATUSF_IP3 | STATUSF_IP2;
#else
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
				STATUSF_IP3 | STATUSF_IP2;
#endif

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);
	write_csr(LOONGSON_IPI_EN_OFFSET, 0xffffffff);

	per_cpu(cpu_state, cpu) = CPU_ONLINE;
	cpu_data[cpu].core = cpu_logical_map(cpu) % cores_per_package;
	cpu_data[cpu].package = cpu_logical_map(cpu) / cores_per_package;

	i = 0;
	core0_c0count[cpu] = 0;
	loongson3_comp_send_ipi_single(0, SMP_ASK_C0COUNT);
	while (!core0_c0count[cpu]) {
		i++;
		cpu_relax();
	}

	if (i > MAX_LOOPS)
		i = MAX_LOOPS;
	if (cpu_data[cpu].package)
		initcount = core0_c0count[cpu] + i;
	else /* Local access is faster for loops */
		initcount = core0_c0count[cpu] + i/2;

	write_c0_count(initcount);
}

static void  loongson3_comp_smp_finish(void)
{
#ifndef CONFIG_LOONGSON3_ENHANCEMENT
#ifndef CONFIG_GS464V_STABLE_COUNTER
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
#endif
#endif
	fixup_irq_route(true);
	local_irq_enable();
	dwrite_csr(LOONGSON_MAIL_BUF_OFFSET, 0);
	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_INFO "CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

static void __init loongson3_comp_smp_setup(void)
{
	int i = 0, num = 0; /* i: physical id, num: logical id */

	init_cpu_possible(cpu_none_mask);

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * nr_cpus_loongson is the really present value */
	while (i < possible_cpus_loongson) {
		if (loongson_reserved_cpus_mask & (1<<i)) {
			/* Reserved physical CPU cores */
			__cpu_number_map[i] = -1;
		} else {
			__cpu_number_map[i] = num;
			__cpu_logical_map[num] = i;
			set_cpu_possible(num, true);
			num++;
		}
		i++;
	}
	printk(KERN_INFO "Detected %i available CPU(s)\n", num);

	while (num < possible_cpus_loongson) {
		__cpu_logical_map[num] = -1;
		num++;
	}

	write_csr(LOONGSON_IPI_EN_OFFSET, 0xffffffff);
	for (i = 0; i < possible_cpus_loongson; i++)
		loongson3_comp_mail_send_csr(0, i, 0);

	cpu_data[0].core = cpu_logical_map(0) % cores_per_package;
	cpu_data[0].package = cpu_logical_map(0) / cores_per_package;
}
/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
static void  loongson3_comp_boot_secondary(int cpu, struct task_struct *idle)
{
	volatile unsigned long startargs[4];

	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_INFO "Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_DEBUG "CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	loongson3_comp_mail_send_csr(startargs[3], cpu_logical_map(cpu), 3);
	loongson3_comp_mail_send_csr(startargs[2], cpu_logical_map(cpu), 2);
	loongson3_comp_mail_send_csr(startargs[1], cpu_logical_map(cpu), 1);
	loongson3_comp_mail_send_csr(startargs[0], cpu_logical_map(cpu), 0);
}

static void ipi_set0_regs_init(void)
{
	ipi_set0_regs[0] = (void *)(smp_core_group0_base + smp_core0_offset + SET0);
	ipi_set0_regs[1] = (void *)(smp_core_group0_base + smp_core1_offset + SET0);
	ipi_set0_regs[2] = (void *)(smp_core_group0_base + smp_core2_offset + SET0);
	ipi_set0_regs[3] = (void *)(smp_core_group0_base + smp_core3_offset + SET0);
	ipi_set0_regs[4] = (void *)(smp_core_group1_base + smp_core0_offset + SET0);
	ipi_set0_regs[5] = (void *)(smp_core_group1_base + smp_core1_offset + SET0);
	ipi_set0_regs[6] = (void *)(smp_core_group1_base + smp_core2_offset + SET0);
	ipi_set0_regs[7] = (void *)(smp_core_group1_base + smp_core3_offset + SET0);
	ipi_set0_regs[8] = (void *)(smp_core_group2_base + smp_core0_offset + SET0);
	ipi_set0_regs[9] = (void *)(smp_core_group2_base + smp_core1_offset + SET0);
	ipi_set0_regs[10] = (void *)(smp_core_group2_base + smp_core2_offset + SET0);
	ipi_set0_regs[11] = (void *)(smp_core_group2_base + smp_core3_offset + SET0);
	ipi_set0_regs[12] = (void *)(smp_core_group3_base + smp_core0_offset + SET0);
	ipi_set0_regs[13] = (void *)(smp_core_group3_base + smp_core1_offset + SET0);
	ipi_set0_regs[14] = (void *)(smp_core_group3_base + smp_core2_offset + SET0);
	ipi_set0_regs[15] = (void *)(smp_core_group3_base + smp_core3_offset + SET0);
}

static void ipi_clear0_regs_init(void)
{
	ipi_clear0_regs[0] = (void *)(smp_core_group0_base + smp_core0_offset + CLEAR0);
	ipi_clear0_regs[1] = (void *)(smp_core_group0_base + smp_core1_offset + CLEAR0);
	ipi_clear0_regs[2] = (void *)(smp_core_group0_base + smp_core2_offset + CLEAR0);
	ipi_clear0_regs[3] = (void *)(smp_core_group0_base + smp_core3_offset + CLEAR0);
	ipi_clear0_regs[4] = (void *)(smp_core_group1_base + smp_core0_offset + CLEAR0);
	ipi_clear0_regs[5] = (void *)(smp_core_group1_base + smp_core1_offset + CLEAR0);
	ipi_clear0_regs[6] = (void *)(smp_core_group1_base + smp_core2_offset + CLEAR0);
	ipi_clear0_regs[7] = (void *)(smp_core_group1_base + smp_core3_offset + CLEAR0);
	ipi_clear0_regs[8] = (void *)(smp_core_group2_base + smp_core0_offset + CLEAR0);
	ipi_clear0_regs[9] = (void *)(smp_core_group2_base + smp_core1_offset + CLEAR0);
	ipi_clear0_regs[10] = (void *)(smp_core_group2_base + smp_core2_offset + CLEAR0);
	ipi_clear0_regs[11] = (void *)(smp_core_group2_base + smp_core3_offset + CLEAR0);
	ipi_clear0_regs[12] = (void *)(smp_core_group3_base + smp_core0_offset + CLEAR0);
	ipi_clear0_regs[13] = (void *)(smp_core_group3_base + smp_core1_offset + CLEAR0);
	ipi_clear0_regs[14] = (void *)(smp_core_group3_base + smp_core2_offset + CLEAR0);
	ipi_clear0_regs[15] = (void *)(smp_core_group3_base + smp_core3_offset + CLEAR0);
}

static void ipi_status0_regs_init(void)
{
	ipi_status0_regs[0] = (void *)(smp_core_group0_base + smp_core0_offset + STATUS0);
	ipi_status0_regs[1] = (void *)(smp_core_group0_base + smp_core1_offset + STATUS0);
	ipi_status0_regs[2] = (void *)(smp_core_group0_base + smp_core2_offset + STATUS0);
	ipi_status0_regs[3] = (void *)(smp_core_group0_base + smp_core3_offset + STATUS0);
	ipi_status0_regs[4] = (void *)(smp_core_group1_base + smp_core0_offset + STATUS0);
	ipi_status0_regs[5] = (void *)(smp_core_group1_base + smp_core1_offset + STATUS0);
	ipi_status0_regs[6] = (void *)(smp_core_group1_base + smp_core2_offset + STATUS0);
	ipi_status0_regs[7] = (void *)(smp_core_group1_base + smp_core3_offset + STATUS0);
	ipi_status0_regs[8] = (void *)(smp_core_group2_base + smp_core0_offset + STATUS0);
	ipi_status0_regs[9] = (void *)(smp_core_group2_base + smp_core1_offset + STATUS0);
	ipi_status0_regs[10] = (void *)(smp_core_group2_base + smp_core2_offset + STATUS0);
	ipi_status0_regs[11] = (void *)(smp_core_group2_base + smp_core3_offset + STATUS0);
	ipi_status0_regs[12] = (void *)(smp_core_group3_base + smp_core0_offset + STATUS0);
	ipi_status0_regs[13] = (void *)(smp_core_group3_base + smp_core1_offset + STATUS0);
	ipi_status0_regs[14] = (void *)(smp_core_group3_base + smp_core2_offset + STATUS0);
	ipi_status0_regs[15] = (void *)(smp_core_group3_base + smp_core3_offset + STATUS0);
}

static void ipi_en0_regs_init(void)
{
	ipi_en0_regs[0] = (void *)(smp_core_group0_base + smp_core0_offset + EN0);
	ipi_en0_regs[1] = (void *)(smp_core_group0_base + smp_core1_offset + EN0);
	ipi_en0_regs[2] = (void *)(smp_core_group0_base + smp_core2_offset + EN0);
	ipi_en0_regs[3] = (void *)(smp_core_group0_base + smp_core3_offset + EN0);
	ipi_en0_regs[4] = (void *)(smp_core_group1_base + smp_core0_offset + EN0);
	ipi_en0_regs[5] = (void *)(smp_core_group1_base + smp_core1_offset + EN0);
	ipi_en0_regs[6] = (void *)(smp_core_group1_base + smp_core2_offset + EN0);
	ipi_en0_regs[7] = (void *)(smp_core_group1_base + smp_core3_offset + EN0);
	ipi_en0_regs[8] = (void *)(smp_core_group2_base + smp_core0_offset + EN0);
	ipi_en0_regs[9] = (void *)(smp_core_group2_base + smp_core1_offset + EN0);
	ipi_en0_regs[10] = (void *)(smp_core_group2_base + smp_core2_offset + EN0);
	ipi_en0_regs[11] = (void *)(smp_core_group2_base + smp_core3_offset + EN0);
	ipi_en0_regs[12] = (void *)(smp_core_group3_base + smp_core0_offset + EN0);
	ipi_en0_regs[13] = (void *)(smp_core_group3_base + smp_core1_offset + EN0);
	ipi_en0_regs[14] = (void *)(smp_core_group3_base + smp_core2_offset + EN0);
	ipi_en0_regs[15] = (void *)(smp_core_group3_base + smp_core3_offset + EN0);
}

static void ipi_mailbox_buf_init(void)
{
	ipi_mailbox_buf[0] = (void *)(smp_core_group0_base + smp_core0_offset + BUF);
	ipi_mailbox_buf[1] = (void *)(smp_core_group0_base + smp_core1_offset + BUF);
	ipi_mailbox_buf[2] = (void *)(smp_core_group0_base + smp_core2_offset + BUF);
	ipi_mailbox_buf[3] = (void *)(smp_core_group0_base + smp_core3_offset + BUF);
	ipi_mailbox_buf[4] = (void *)(smp_core_group1_base + smp_core0_offset + BUF);
	ipi_mailbox_buf[5] = (void *)(smp_core_group1_base + smp_core1_offset + BUF);
	ipi_mailbox_buf[6] = (void *)(smp_core_group1_base + smp_core2_offset + BUF);
	ipi_mailbox_buf[7] = (void *)(smp_core_group1_base + smp_core3_offset + BUF);
	ipi_mailbox_buf[8] = (void *)(smp_core_group2_base + smp_core0_offset + BUF);
	ipi_mailbox_buf[9] = (void *)(smp_core_group2_base + smp_core1_offset + BUF);
	ipi_mailbox_buf[10] = (void *)(smp_core_group2_base + smp_core2_offset + BUF);
	ipi_mailbox_buf[11] = (void *)(smp_core_group2_base + smp_core3_offset + BUF);
	ipi_mailbox_buf[12] = (void *)(smp_core_group3_base + smp_core0_offset + BUF);
	ipi_mailbox_buf[13] = (void *)(smp_core_group3_base + smp_core1_offset + BUF);
	ipi_mailbox_buf[14] = (void *)(smp_core_group3_base + smp_core2_offset + BUF);
	ipi_mailbox_buf[15] = (void *)(smp_core_group3_base + smp_core3_offset + BUF);
}

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson3_send_ipi_single(int cpu, unsigned int action)
{
	ls64_conf_write32((u32)action, ipi_set0_regs[cpu_logical_map(cpu)]);
}

static void loongson3_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;
	for_each_cpu(i, mask)
		ls64_conf_write32((u32)action, ipi_set0_regs[cpu_logical_map(i)]);
}

/*
 * SMP init and finish on secondary CPUs
 */
static void  loongson3_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);
	ls64_conf_write32(0xffffffff, ipi_en0_regs[cpu_logical_map(cpu)]);

	per_cpu(cpu_state, cpu) = CPU_ONLINE;
	cpu_data[cpu].core = cpu_logical_map(cpu) % cores_per_package;
	cpu_data[cpu].package = cpu_logical_map(cpu) / cores_per_package;

	i = 0;
	core0_c0count[cpu] = 0;
	loongson3_send_ipi_single(0, SMP_ASK_C0COUNT);
	while (!core0_c0count[cpu]) {
		i++;
		cpu_relax();
	}

	if (i > MAX_LOOPS)
		i = MAX_LOOPS;
	if (cpu_data[cpu].package)
		initcount = core0_c0count[cpu] + i;
	else /* Local access is faster for loops */
		initcount = core0_c0count[cpu] + i/2;

	write_c0_count(initcount);
}

static void  loongson3_smp_finish(void)
{
	int cpu = smp_processor_id();

#ifndef CONFIG_LOONGSON3_ENHANCEMENT
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
#endif
	fixup_irq_route(true);
	local_irq_enable();
	ls64_conf_write64(0, (void *)(ipi_mailbox_buf[cpu_logical_map(cpu)]+0x0));
	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_INFO "CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

static void __init loongson3_smp_setup(void)
{
	int i = 0, num = 0; /* i: physical id, num: logical id */

	init_cpu_possible(cpu_none_mask);

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * nr_cpus_loongson is the really present value */
	while (i < possible_cpus_loongson) {
		if (loongson_reserved_cpus_mask & (1<<i)) {
			/* Reserved physical CPU cores */
			__cpu_number_map[i] = -1;
		} else {
			__cpu_number_map[i] = num;
			__cpu_logical_map[num] = i;
			set_cpu_possible(num, true);
			num++;
		}
		i++;
	}
	printk(KERN_INFO "Detected %i available CPU(s)\n", num);

	while (num < possible_cpus_loongson) {
		__cpu_logical_map[num] = -1;
		num++;
	}
	ipi_set0_regs_init();
	ipi_clear0_regs_init();
	ipi_status0_regs_init();
	ipi_en0_regs_init();
	ipi_mailbox_buf_init();
		
	/* Enable ipi interrupt on boot cpu */
	ls64_conf_write32(0xffffffff, ipi_en0_regs[cpu_logical_map(0)]);
	for (i = 0; i < possible_cpus_loongson; i++)
		ls64_conf_write64(0, (void *)(ipi_mailbox_buf[i]+0x0));

	cpu_data[0].core = cpu_logical_map(0) % cores_per_package;
	cpu_data[0].package = cpu_logical_map(0) / cores_per_package;
}

static void __init loongson3_prepare_cpus(unsigned int max_cpus)
{
	int i = 0;

	while(i < nr_cpus_loongson){
		set_cpu_present(i,true);
		i++;
	}

	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
static void  loongson3_boot_secondary(int cpu, struct task_struct *idle)
{
	volatile unsigned long startargs[4];

	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_INFO "Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	if (verbose || system_state == SYSTEM_BOOTING)
		printk(KERN_DEBUG "CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	ls64_conf_write64(startargs[3], (void *)(ipi_mailbox_buf[cpu_logical_map(cpu)]+0x18));
	ls64_conf_write64(startargs[2], (void *)(ipi_mailbox_buf[cpu_logical_map(cpu)]+0x10));
	ls64_conf_write64(startargs[1], (void *)(ipi_mailbox_buf[cpu_logical_map(cpu)]+0x8));
	ls64_conf_write64(startargs[0], (void *)(ipi_mailbox_buf[cpu_logical_map(cpu)]+0x0));
}

/*
 * Final cleanup after all secondaries booted
 */
static void __init loongson3_cpus_done(void)
{
	disable_unused_cpus();
}

#ifdef CONFIG_HOTPLUG_CPU

extern void fixup_irqs(void);
extern void (*flush_cache_all)(void);

static int loongson3_cpu_disable(void)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EBUSY;

	set_cpu_online(cpu, false);
	cpu_clear(cpu, cpu_callin_map);
	local_irq_save(flags);
	fixup_irqs();
	local_irq_restore(flags);
	flush_cache_all();
	local_flush_tlb_all();

	return 0;
}


static void loongson3_cpu_die(unsigned int cpu)
{
	while (per_cpu(cpu_state, cpu) != CPU_DEAD)
		cpu_relax();

	mb();
}

/* To shutdown a core in Loongson 3, the target core should go to CKSEG1 and
 * flush all L1 entries at first. Then, another core (usually Core 0) can
 * safely disable the clock of the target core. loongson3_play_dead() is
 * called via CKSEG1 (uncached and unmmaped) */
static void loongson3a_r1_play_dead(int *state_addr)
{
	register int val;
	register long cpuid, core, node, count;
	register void *addr, *base, *initfunc;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x20  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets));

	__asm__ __volatile__(
		"   .set push                         \n"
		"   .set noreorder                    \n"
		"   .set mips64                       \n"
		"   mfc0  %[cpuid], $15, 1            \n"
		"   andi  %[cpuid], 0x3ff             \n"
		"   dli   %[base], 0x900000003ff01000 \n"
		"   andi  %[core], %[cpuid], 0x3      \n"
		"   sll   %[core], 8                  \n" /* get core id */
		"   or    %[base], %[base], %[core]   \n"
		"   andi  %[node], %[cpuid], 0xc      \n"
		"   dsll  %[node], 42                 \n" /* get node id */
		"   or    %[base], %[base], %[node]   \n"
		"1: li    %[count], 0x100             \n" /* wait for init loop */
		"2: bnez  %[count], 2b                \n" /* limit mailbox access */
		"   addiu %[count], -1                \n"
		"   ld    %[initfunc], 0x20(%[base])  \n" /* get PC via mailbox */
		"   beqz  %[initfunc], 1b             \n"
		"   nop                               \n"
		"   ld    $sp, 0x28(%[base])          \n" /* get SP via mailbox */
		"   ld    $gp, 0x30(%[base])          \n" /* get GP via mailbox */
		"   ld    $a1, 0x38(%[base])          \n"
		"   jr    %[initfunc]                 \n" /* jump to initial PC */
		"   nop                               \n"
		"   .set pop                          \n"
		: [core] "=&r" (core), [node] "=&r" (node),
		  [base] "=&r" (base), [cpuid] "=&r" (cpuid),
		  [count] "=&r" (count), [initfunc] "=&r" (initfunc)
		: /* No Input */
		: "a1");
}

#define QUAUX(X) #X
#define QU(X) QUAUX(X)

static void loongson3a_r2r3_play_dead(int *state_addr)
{
	register int val;
	register long cpuid, core, node, count;
	register void *addr, *base, *initfunc;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x40  \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"2: cache 2, 0(%[addr])           \n" /* flush L1 VCache */
		"   cache 2, 1(%[addr])           \n"
		"   cache 2, 2(%[addr])           \n"
		"   cache 2, 3(%[addr])           \n"
		"   cache 2, 4(%[addr])           \n"
		"   cache 2, 5(%[addr])           \n"
		"   cache 2, 6(%[addr])           \n"
		"   cache 2, 7(%[addr])           \n"
		"   cache 2, 8(%[addr])           \n"
		"   cache 2, 9(%[addr])           \n"
		"   cache 2, 10(%[addr])          \n"
		"   cache 2, 11(%[addr])          \n"
		"   cache 2, 12(%[addr])          \n"
		"   cache 2, 13(%[addr])          \n"
		"   cache 2, 14(%[addr])          \n"
		"   cache 2, 15(%[addr])          \n"
		"   addiu %[vsets], %[vsets], -1  \n"
		"   bnez  %[vsets], 2b            \n"
		"   addiu %[addr], %[addr], 0x40  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets),
		  [vsets] "r" (cpu_data[smp_processor_id()].vcache.sets));

	__asm__ __volatile__(
		"   .set push                         \n"
		"   .set noreorder                    \n"
		"   .set mips64                       \n"
		"   mfc0  %[cpuid], $15, 1            \n"
		"   andi  %[cpuid], 0x3ff             \n"
		"   dli   %[base], 0x900000003ff01000 \n"
		"   andi  %[core], %[cpuid], 0x3      \n"
		"   sll   %[core], 8                  \n" /* get core id */
		"   or    %[base], %[base], %[core]   \n"
		"   andi  %[node], %[cpuid], 0xc      \n"
		"   dsll  %[node], " QU(NODE_ADDRSPACE_SHIFT-2)" \n" /* get node id */
		"   or    %[base], %[base], %[node]   \n"
		"1: li    %[count], 0x100             \n" /* wait for init loop */
		"2: bnez  %[count], 2b                \n" /* limit mailbox access */
		"   addiu %[count], -1                \n"
		"   lw    %[initfunc], 0x20(%[base])  \n" /* get PC via mailbox */
		"   beqz  %[initfunc], 1b             \n"
		"   nop                               \n"
		"   ld    %[initfunc], 0x20(%[base])  \n" /* get PC via mailbox */
		"   ld    $sp, 0x28(%[base])          \n" /* get SP via mailbox */
		"   ld    $gp, 0x30(%[base])          \n" /* get GP via mailbox */
		"   ld    $a1, 0x38(%[base])          \n"
		"   jr    %[initfunc]                 \n" /* jump to initial PC */
		"   nop                               \n"
		"   .set pop                          \n"
		: [core] "=&r" (core), [node] "=&r" (node),
		  [base] "=&r" (base), [cpuid] "=&r" (cpuid),
		  [count] "=&r" (count), [initfunc] "=&r" (initfunc)
		: /* No Input */
		: "a1");
}

static void loongson3b_play_dead(int *state_addr)
{
	register int val;
	register long cpuid, core, node, count;
	register void *addr, *base, *initfunc;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x20  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets));

	__asm__ __volatile__(
		"   .set push                         \n"
		"   .set noreorder                    \n"
		"   .set mips64                       \n"
		"   mfc0  %[cpuid], $15, 1            \n"
		"   andi  %[cpuid], 0x3ff             \n"
		"   dli   %[base], 0x900000003ff01000 \n"
		"   andi  %[core], %[cpuid], 0x3      \n"
		"   sll   %[core], 8                  \n" /* get core id */
		"   or    %[base], %[base], %[core]   \n"
		"   andi  %[node], %[cpuid], 0xc      \n"
		"   dsll  %[node], 42                 \n" /* get node id */
		"   or    %[base], %[base], %[node]   \n"
		"   dsrl  %[node], 30                 \n" /* 15:14 */
		"   or    %[base], %[base], %[node]   \n"
		"1: li    %[count], 0x100             \n" /* wait for init loop */
		"2: bnez  %[count], 2b                \n" /* limit mailbox access */
		"   addiu %[count], -1                \n"
		"   ld    %[initfunc], 0x20(%[base])  \n" /* get PC via mailbox */
		"   beqz  %[initfunc], 1b             \n"
		"   nop                               \n"
		"   ld    $sp, 0x28(%[base])          \n" /* get SP via mailbox */
		"   ld    $gp, 0x30(%[base])          \n" /* get GP via mailbox */
		"   ld    $a1, 0x38(%[base])          \n"
		"   jr    %[initfunc]                 \n" /* jump to initial PC */
		"   nop                               \n"
		"   .set pop                          \n"
		: [core] "=&r" (core), [node] "=&r" (node),
		  [base] "=&r" (base), [cpuid] "=&r" (cpuid),
		  [count] "=&r" (count), [initfunc] "=&r" (initfunc)
		: /* No Input */
		: "a1");
}

void play_dead(void)
{
	int *state_addr;
	unsigned int cpu = smp_processor_id();
	void (*play_dead_at_ckseg1)(int *);

	idle_task_exit();
	switch (current_cpu_type()) {
	case CPU_LOONGSON3:
		switch (read_c0_prid() & 0xf) {
		case PRID_REV_LOONGSON3A_R1:
		default:
			play_dead_at_ckseg1 = (void *)CKSEG1ADDR((unsigned long)loongson3a_r1_play_dead);
			break;
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			play_dead_at_ckseg1 = (void *)CKSEG1ADDR((unsigned long)loongson3a_r2r3_play_dead);
		break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			play_dead_at_ckseg1 = (void *)CKSEG1ADDR((unsigned long)loongson3b_play_dead);
			break;
	}
	case CPU_LOONGSON3_COMP:
	default:
		play_dead_at_ckseg1 = (void *)CKSEG1ADDR((unsigned long)loongson3a_r2r3_play_dead);
		break;
	}

	state_addr = &per_cpu(cpu_state, cpu);
	mb();
	play_dead_at_ckseg1(state_addr);
}

static void loongson3_disable_clock(int cpu)
{
	uint64_t core_id = cpu_data[cpu].core;
	uint64_t package_id = cpu_data[cpu].package;

	if ((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R1) {
		LOONGSON_CHIPCFG(package_id) &= ~(1 << (12 + core_id));
	} else {
		if (!(loongson_workarounds & WORKAROUND_CPUHOTPLUG))
			LOONGSON_FREQCTRL(package_id) &= ~(1 << (core_id * 4 + 3));
	}
}

static void loongson3_enable_clock(int cpu)
{
	uint64_t core_id = cpu_data[cpu].core;
	uint64_t package_id = cpu_data[cpu].package;

	if ((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R1) {
		LOONGSON_CHIPCFG(package_id) |= 1 << (12 + core_id);
	} else {
		if (!(loongson_workarounds & WORKAROUND_CPUHOTPLUG))
			LOONGSON_FREQCTRL(package_id) |= 1 << (core_id * 4 + 3);
	}
}

#define CPU_POST_DEAD_FROZEN	(CPU_POST_DEAD | CPU_TASKS_FROZEN)
static int  loongson3_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_POST_DEAD:
	case CPU_POST_DEAD_FROZEN:
		if (verbose || system_state == SYSTEM_BOOTING)
			printk(KERN_INFO "Disable clock for CPU#%d\n", cpu);
		loongson3_disable_clock(cpu);
		break;
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		if (verbose || system_state == SYSTEM_BOOTING)
			printk(KERN_INFO "Enable clock for CPU#%d\n", cpu);
		loongson3_enable_clock(cpu);
		break;
	}

	return NOTIFY_OK;
}

static int  register_loongson3_notifier(void)
{
	hotcpu_notifier(loongson3_cpu_callback, 0);
	return 0;
}
early_initcall(register_loongson3_notifier);

void  disable_unused_cpus(void)
{
	int cpu;
	struct cpumask tmp;

	cpumask_complement(&tmp, cpu_online_mask);
	cpumask_and(&tmp, &tmp, cpu_possible_mask);

	for_each_cpu(cpu, &tmp)
		cpu_up(cpu);

	for_each_cpu(cpu, &tmp)
		cpu_down(cpu);
}

#endif

struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = loongson3_send_ipi_single,
	.send_ipi_mask = loongson3_send_ipi_mask,
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
	.cpus_done = loongson3_cpus_done,
	.boot_secondary = loongson3_boot_secondary,
	.smp_setup = loongson3_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable = loongson3_cpu_disable,
	.cpu_die = loongson3_cpu_die,
#endif
};

struct plat_smp_ops loongson3_comp_smp_ops = {
	.send_ipi_single = loongson3_comp_send_ipi_single,
	.send_ipi_mask = loongson3_comp_send_ipi_mask,
	.init_secondary = loongson3_comp_init_secondary,
	.smp_finish = loongson3_comp_smp_finish,
	.cpus_done = loongson3_cpus_done,
	.boot_secondary = loongson3_comp_boot_secondary,
	.smp_setup = loongson3_comp_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable = loongson3_cpu_disable,
	.cpu_die = loongson3_cpu_die,
#endif
};

/*
 * Power management
 */
#ifdef CONFIG_PM

static int loongson3_ipi_suspend(void)
{
	return 0;
}

static void loongson3_ipi_resume(void)
{
	ls64_conf_write32(0xffffffff, ipi_en0_regs[cpu_logical_map(0)]);
}

static struct syscore_ops loongson3_ipi_syscore_ops = {
	.resume		= loongson3_ipi_resume,
	.suspend	= loongson3_ipi_suspend,
};

static int loongson3_comp_ipi_suspend(void)
{
	return 0;
}

static void loongson3_comp_ipi_resume(void)
{
	write_csr(LOONGSON_IPI_EN_OFFSET, 0xffffffff);
}

static struct syscore_ops loongson3_comp_ipi_syscore_ops = {
	.resume		= loongson3_comp_ipi_resume,
	.suspend	= loongson3_comp_ipi_suspend,
};

/*
 * Enable boot cpu ipi before enabling nonboot cpus
 * during syscore_resume.
 * */
static int __init init_ipi_sysfs(void)
{
	if ((current_cpu_type() == CPU_LOONGSON3_COMP) &&
		(read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_CPU_FEATURE_IPI_PERCORE)) {
		register_syscore_ops(&loongson3_comp_ipi_syscore_ops);
	} else {
		register_syscore_ops(&loongson3_ipi_syscore_ops);
	}

	return 0;
}

core_initcall(init_ipi_sysfs);
#endif
