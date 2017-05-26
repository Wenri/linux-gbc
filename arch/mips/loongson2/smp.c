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
#include <asm/mach-loongson2/2k1000.h>
#include <asm/smp.h>


DEFINE_PER_CPU(int, cpu_state);
static uint32_t core0_c0count[NR_CPUS];

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long base = IPI_BASE_OF(cpu_logical_map(cpu));
	ls64_conf_write32((u32)action, (void *)(base + IPI_OFF_SET));
}

static void loongson_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;
	for_each_cpu(i, mask){
	unsigned long base = IPI_BASE_OF(cpu_logical_map(i));
		ls64_conf_write32((u32)action, (void *)(base + IPI_OFF_SET));
	}
}

void ls64_ipi_interrupt(struct pt_regs *regs)
{
	int i, cpu = smp_processor_id();
	unsigned long base = IPI_BASE_OF(cpu_logical_map(cpu));
	unsigned int action, c0count;

	/* Load the ipi register to figure out what we're supposed to do */
	action = ls64_conf_read32((void *)(base + IPI_OFF_STATUS));

	/* Clear the ipi register to clear the interrupt */
	ls64_conf_write32((u32)action,(void *)(base + IPI_OFF_CLEAR));

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
		for (i = 1; i < num_possible_cpus(); i++)
			core0_c0count[i] = c0count;
		__sync();
	}

}

#define MAX_LOOPS 800
/*
 * SMP init and finish on secondary CPUs
 */
void __cpuinit loongson_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	for (i = 0; i < num_possible_cpus(); i++) {
		unsigned long base = IPI_BASE_OF(cpu_logical_map(i));
		ls64_conf_write32(0xffffffff,(void *)(base + IPI_OFF_ENABLE));
	}

	per_cpu(cpu_state, cpu) = CPU_ONLINE;

	i = 0;
	core0_c0count[cpu] = 0;
	loongson_send_ipi_single(0, SMP_ASK_C0COUNT);
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

void __cpuinit loongson_smp_finish(void)
{
	int cpu = smp_processor_id();
	unsigned long base = IPI_BASE_OF(cpu_logical_map(cpu));
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
	local_irq_enable();
	ls64_conf_write64(0, (void *)(base + IPI_OFF_MAILBOX0));
	if (system_state == SYSTEM_BOOTING)
		printk("CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

void __init loongson_smp_setup(void)
{
	int i = 0;

	init_cpu_possible(cpu_none_mask);

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * nr_cpus_loongson is the really present value */
	while (i < NR_CPUS) {
		__cpu_number_map[i] = i;
		__cpu_logical_map[i] = i;
		set_cpu_possible(i, true);
		i++;
	}
	printk(KERN_INFO "Detected %i available CPU(s)\n", i);

	printk("Detected %i available CPU(s)\n", i);

}

void __init loongson_prepare_cpus(unsigned int max_cpus)
{
	init_cpu_present(cpu_possible_mask);
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
void __cpuinit loongson_boot_secondary(int cpu, struct task_struct *idle)
{
	volatile unsigned long startargs[4];
	unsigned long coreid = cpu_logical_map(cpu);
	unsigned long base = IPI_BASE_OF(coreid);

	if (system_state == SYSTEM_BOOTING)
		printk("Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	if (system_state == SYSTEM_BOOTING)
		printk("CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	ls64_conf_write64(startargs[3], (void *)(base + IPI_OFF_MAILBOX3));
	ls64_conf_write64(startargs[2], (void *)(base + IPI_OFF_MAILBOX2));
	ls64_conf_write64(startargs[1], (void *)(base + IPI_OFF_MAILBOX1));
	ls64_conf_write64(startargs[0], (void *)(base + IPI_OFF_MAILBOX0));
}

/*
 * Final cleanup after all secondaries booted
 */
void __init loongson_cpus_done(void)
{
}

struct plat_smp_ops loongson_smp_ops = {
	.send_ipi_single = loongson_send_ipi_single,
	.send_ipi_mask = loongson_send_ipi_mask,
	.init_secondary = loongson_init_secondary,
	.smp_finish = loongson_smp_finish,
	.cpus_done = loongson_cpus_done,
	.boot_secondary = loongson_boot_secondary,
	.smp_setup = loongson_smp_setup,
	.prepare_cpus = loongson_prepare_cpus,
};
