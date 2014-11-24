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
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/clock.h>
#include <asm/tlbflush.h>
#include <loongson.h>

#include "smp.h"

/* read a 64bit value from ipi register */
uint64_t loongson3_ipi_read64(void * addr)
{
	return *((volatile uint64_t *)addr);
};

/* write a 64bit value to ipi register */
void loongson3_ipi_write64(uint64_t action, void * addr)
{
	*((volatile uint64_t *)addr) = action;
	__wbflush();
};

/* read a 32bit value from ipi register */
uint32_t loongson3_ipi_read32(void * addr)
{
	return *((volatile uint32_t *)addr);
};

/* write a 32bit value to ipi register */
void loongson3_ipi_write32(uint32_t action, void * addr)
{
	*((volatile uint32_t *)addr) = action;
	__wbflush();
};

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson3_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	loongson3_ipi_write32((u32)action, (void *)(base + IPI_OFF_SET));
}

static void loongson3_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;
	for_each_cpu(i, mask){
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(i));
		loongson3_ipi_write32((u32)action, (void *)(base + IPI_OFF_SET));
	}
}

void loongson3_ipi_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	unsigned int action;

	/* Load the ipi register to figure out what we're supposed to do */
	action = loongson3_ipi_read32((void *)(base + IPI_OFF_STATUS));

	/* Clear the ipi register to clear the interrupt */
	loongson3_ipi_write32((u32)action,(void *)(base + IPI_OFF_CLEAR));

	if (action & SMP_RESCHEDULE_YOURSELF) {
		scheduler_ipi();
	}

	if (action & SMP_CALL_FUNCTION) {
		smp_call_function_interrupt();
	}
}

/*
 * SMP init and finish on secondary CPUs
 */
void loongson3_init_secondary(void)
{
	int i;
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	for (i = 0; i < nr_cpus_loongson; i++) {
		unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(i));
		loongson3_ipi_write32(0xffffffff,(void *)(base + IPI_OFF_ENABLE));
	}

}

void loongson3_smp_finish(void)
{
	int cpu = smp_processor_id();
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
	local_irq_enable();
	loongson3_ipi_write64(0, (void *)(base + IPI_OFF_MAILBOX0));
	printk(KERN_INFO "CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

void __init loongson3_smp_setup(void)
{
	int i, num;

	init_cpu_possible(cpu_none_mask);
	set_cpu_possible(0, true);

	__cpu_number_map[0] = 0;
	__cpu_logical_map[0] = 0;

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * nr_cpus_loongson is the really present value */
	for (i = 1, num = 0; i < nr_cpus_loongson; i++) {
		set_cpu_possible(i, true);
		__cpu_number_map[i] = ++num;
		__cpu_logical_map[num] = i;
	}
	printk(KERN_INFO "Detected %i available secondary CPU(s)\n", num);
}

void __init loongson3_prepare_cpus(unsigned int max_cpus)
{
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
void loongson3_boot_secondary(int cpu, struct task_struct *idle)
{
	volatile unsigned long startargs[4];
	unsigned long coreid = cpu_logical_map(cpu);
	unsigned long base = LOONGSON3_TO_BASE(coreid);

	printk(KERN_INFO "Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	printk(KERN_DEBUG "CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	loongson3_ipi_write64(startargs[3], (void *)(base + IPI_OFF_MAILBOX3));
	loongson3_ipi_write64(startargs[2], (void *)(base + IPI_OFF_MAILBOX2));
	loongson3_ipi_write64(startargs[1], (void *)(base + IPI_OFF_MAILBOX1));
	loongson3_ipi_write64(startargs[0], (void *)(base + IPI_OFF_MAILBOX0));
}

/*
 * Final cleanup after all secondaries booted
 */
void __init loongson3_cpus_done(void)
{
}

struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = loongson3_send_ipi_single,
	.send_ipi_mask = loongson3_send_ipi_mask,
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
	.cpus_done = loongson3_cpus_done,
	.boot_secondary = loongson3_boot_secondary,
	.smp_setup = loongson3_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
};
