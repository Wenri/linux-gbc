/*
 * CPUFreq driver for the loongson-3 processors
 *
 * All revisions of Loongson-3 processor support this feature.
 *
 * Copyright (C) 2008 - 2014 Lemote Inc.
 * Author: Yan Hua, yanh@lemote.com
 *         Chen Huacai, chenhc@lemote.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/cpufreq.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <asm/idle.h>
#include <asm/clock.h>
#include <asm/cevt-r4k.h>
#include <linux/clocksource.h>

#include <loongson.h>
#include "loongson_boost.h"

static uint nowait = 1;
static spinlock_t cpufreq_reg_lock[MAX_PACKAGES];
static struct mutex ls3a4000_mutex[MAX_PACKAGES];

static struct cpufreq_driver loongson3_cpufreq_driver;

static void (*saved_cpu_wait)(void);
extern struct clk *cpu_clk_get(int cpu);

static int loongson3_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data);

static int ls3a4000_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data);

static struct notifier_block loongson3_cpufreq_notifier_block = {
	.notifier_call = loongson3_cpu_freq_notifier
};

static struct notifier_block ls3a4000_cpufreq_notifier_block = {
	.notifier_call = ls3a4000_cpu_freq_notifier
};


DECLARE_PER_CPU(struct clock_event_device, stable_clockevent_device);

int ls_boost_freq = 0;
int ls_stable_base_freq;
int ls_boost_supported;
int ls_boost_cores;
int ls_upper_index;
const int RESERVED_FREQ = 3;

int ls_clock_scale = 0;

static int scale_resolution = 8;
static int shift = 1;

struct cpufreq_frequency_table *ls3a4000_freq_table;
EXPORT_SYMBOL_GPL(ls3a4000_freq_table);

/* 3a4000 normal mode */
struct cpufreq_frequency_table ls3a4000_normal_table[] = {
	{0, FREQ_LEV0, CPUFREQ_ENTRY_INVALID},
	{0, FREQ_LEV1, CPUFREQ_ENTRY_INVALID},
	{0, FREQ_LEV2, CPUFREQ_ENTRY_INVALID},

	{0, FREQ_LEV3, CPUFREQ_TABLE_END},
	{0, FREQ_LEV4, CPUFREQ_TABLE_END},
	{0, FREQ_LEV5, CPUFREQ_TABLE_END},
	{0, FREQ_LEV6, CPUFREQ_TABLE_END},

	{0, FREQ_LEV7, CPUFREQ_TABLE_END},
	{0, FREQ_LEV8, CPUFREQ_TABLE_END},
	{0, FREQ_LEV9, CPUFREQ_TABLE_END},
	{0, FREQ_LEV10, CPUFREQ_TABLE_END},

	{0, FREQ_LEV11, CPUFREQ_TABLE_END},
};
EXPORT_SYMBOL_GPL(ls3a4000_normal_table);

/* 3a4000 boost mode */
struct cpufreq_frequency_table ls3a4000_boost_table[] = {
	{0, FREQ_LEV0, CPUFREQ_ENTRY_INVALID},
	{0, FREQ_LEV1, CPUFREQ_ENTRY_INVALID},
	{0, FREQ_LEV2, CPUFREQ_ENTRY_INVALID},

	{0, FREQ_LEV3, CPUFREQ_TABLE_END},
	{0, FREQ_LEV4, CPUFREQ_TABLE_END},
	{0, FREQ_LEV5, CPUFREQ_TABLE_END},
	{0, FREQ_LEV6, CPUFREQ_TABLE_END},

	{0, FREQ_LEV7, CPUFREQ_TABLE_END},
	{0, FREQ_LEV8, CPUFREQ_TABLE_END},
	{0, FREQ_LEV9, CPUFREQ_TABLE_END},
	{0, FREQ_LEV10, CPUFREQ_TABLE_END},

	{0, FREQ_LEV11, CPUFREQ_TABLE_END},
};
EXPORT_SYMBOL_GPL(ls3a4000_boost_table);

#ifdef CONFIG_SMP
static int ls3a4000_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;
	struct clock_event_device *cd;
	unsigned long cpu = freqs->cpu;

	if (val == CPUFREQ_POSTCHANGE) {

#ifdef CONFIG_GS464V_STABLE_COUNTER
		if (stable_timer_enabled) {
			cd = &per_cpu(stable_clockevent_device, cpu);

			if (cpu != smp_processor_id())
				clockevents_config(cd, ls3a4000_freq_table[FREQ_LEV3].frequency * 1000);
			else
				clockevents_update_freq(cd, ls3a4000_freq_table[FREQ_LEV3].frequency * 1000);
		} else
#endif
		{
			cd = &per_cpu(mips_clockevent_device, cpu);

			if (cpu != smp_processor_id())
				clockevents_config(cd, freqs->new * 1000 / 2);
			else
				clockevents_update_freq(cd, freqs->new * 1000 / 2);
		}

		if (ls3a4000_freq_table[FREQ_LEV3].frequency / 1000 == ls_boost_freq) {
			cpu_data[cpu].udelay_val = (unsigned int) (((int64_t)ls_clock_scale *
				cpufreq_scale(loops_per_jiffy, ls3a4000_freq_table[FREQ_LEV3].frequency, freqs->new)) / shift);
		} else {
			cpu_data[cpu].udelay_val =
				cpufreq_scale(loops_per_jiffy, ls3a4000_freq_table[FREQ_LEV3].frequency, freqs->new);
		}
	}

	return 0;
}

static int loongson3_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;
	struct clock_event_device *cd;
	unsigned long cpu = freqs->cpu;

	cd = &per_cpu(mips_clockevent_device, cpu);

	if (val == CPUFREQ_POSTCHANGE) {
		if (cpu != smp_processor_id())
			clockevents_config(cd, freqs->new * 1000 / 2);
		else
			clockevents_update_freq(cd, freqs->new * 1000 / 2);

		cpu_data[cpu].udelay_val =
		cpufreq_scale(loops_per_jiffy, cpu_clock_freq / 1000, freqs->new);
	}

	return 0;
}
#else
static int ls3a4000_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;
	struct clock_event_device *cd;
	unsigned long cpu = freqs->cpu;

	if (val == CPUFREQ_POSTCHANGE) {

#ifdef CONFIG_GS464V_STABLE_COUNTER
		if (stable_timer_enabled) {
			cd = &per_cpu(stable_clockevent_device, cpu);
			clockevents_update_freq(cd, ls3a4000_freq_table[FREQ_LEV3].frequency * 1000);
		} else
#endif
		{
			cd = &per_cpu(mips_clockevent_device, cpu);
			clockevents_update_freq(cd, freqs->new * 1000 / 2);
		}

		if (ls3a4000_freq_table[FREQ_LEV3].frequency / 1000 == ls_boost_freq) {
			cpu_data[cpu].udelay_val = (unsigned int) (((int64_t)ls_clock_scale *
				loops_per_jiffy) / shift);
		} else {
			cpu_data[cpu].udelay_val = loops_per_jiffy;
		}
	}

	return 0;
}

static int loongson3_cpu_freq_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;
	struct clock_event_device *cd = &per_cpu(mips_clockevent_device, 0);

	if (val == CPUFREQ_POSTCHANGE) {
		clockevents_update_freq(cd, freqs->new * 1000 / 2);
		current_cpu_data.udelay_val = loops_per_jiffy;
	}

	return 0;
}
#endif

static unsigned int loongson3_cpufreq_get(unsigned int cpu)
{
	return clk_get_rate(cpu_clk_get(cpu));
}

void ls3a4000_freq_table_switch(struct cpufreq_frequency_table *table)
{
	ls3a4000_freq_table = table;
}
EXPORT_SYMBOL_GPL(ls3a4000_freq_table_switch);

void loongson3a4000_set_freq(struct cpufreq_policy* policy, uint32_t freq)
{
	uint32_t message;

	enum freq freq_level;
	uint32_t val;

	int core_id = cpu_data[policy->cpu].core;

	cpufreq_frequency_table_target(policy, policy->freq_table, freq, CPUFREQ_RELATION_C, &freq_level);

	message = (0 << 31 ) | (VOLTAGE_COMMAND << 24)
		| (freq_level << 4)
		| (core_id & CPU_ID_FIELD);

	write_csr(0x51c, message);
	/* Any_send send to another node */

	val = read_csr(0x420);
	val |= 1 << 10;
	write_csr(0x420, val);
}

/*
 * normal mode to boost mode: core_id: boost core id, freq_level: upper limit of other core
 * boost mode to normal mode: core_id and freq_level not use */
int ls3a4000_set_boost(int mode, int freq_level)
{
	struct timespec64 prev_ts;
	struct timespec64 curr_ts;
	uint64_t timeout = 300000000;
	uint32_t val;
	int ret = 0;
	uint32_t message;

	ktime_t delay = ktime_set(0, 100);

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&delay, HRTIMER_MODE_REL);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}
	}

	message = mode | (VOLTAGE_COMMAND << 24) | freq_level;

	write_csr(0x51c, message);

	val = read_csr(0x420);
	val |= 1 << 10;
	write_csr(0x420, val);

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&delay, HRTIMER_MODE_REL);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ls3a4000_set_boost);

int ls3a4000_freq_scale(struct cpufreq_policy* policy, unsigned long rate)
{
	int ret = 0;
	struct cpufreq_frequency_table *pos;

	struct timespec64 prev_ts;
	struct timespec64 curr_ts;
        ktime_t delay = ktime_set(0, 100);
	uint64_t timeout = 300000000;

	cpufreq_for_each_valid_entry(pos, ls3a4000_freq_table)
		if (rate == pos->frequency)
			break;

	if (rate != pos->frequency)
		return -ENOTSUPP;

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}

	         __set_current_state(TASK_UNINTERRUPTIBLE);
	         schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
	}

	loongson3a4000_set_freq(policy, pos->frequency);

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}

	         __set_current_state(TASK_UNINTERRUPTIBLE);
	         schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
	}

	return ret;
}

/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int loongson3_cpufreq_target(struct cpufreq_policy *policy,
				     unsigned int index)
{
	unsigned int freq;
	unsigned int cpu = policy->cpu;
	unsigned int package = cpu_data[cpu].package;
	int ret = 0;

	if (!cpu_online(cpu))
		return -ENODEV;

	if ((current_cpu_type() == CPU_LOONGSON3) &&
		((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R1)) {
		freq =
			((cpu_clock_freq / 1000) *
			loongson3_clockmod_table[index].driver_data) / 8;
	} else if ((current_cpu_type() == CPU_LOONGSON3_COMP)) {
		freq = ls3a4000_freq_table[index].frequency;
	}

	/* setting the cpu frequency */
	if ((current_cpu_type() == CPU_LOONGSON3_COMP)) {
		mutex_lock(&ls3a4000_mutex[package]);
		ret = ls3a4000_freq_scale(policy, freq);
		mutex_unlock(&ls3a4000_mutex[package]);
	} else {
		spin_lock(&cpufreq_reg_lock[package]);
		ret = clk_set_rate(policy->clk, freq);
		spin_unlock(&cpufreq_reg_lock[package]);
	}

	return ret;
}

static int loongson3_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	if (!cpu_online(policy->cpu))
		return -ENODEV;

	policy->clk = cpu_clk_get(policy->cpu);
	policy->cur = loongson3_cpufreq_get(policy->cpu);

	policy->cpuinfo.transition_latency = 1000;

	/* Loongson-3A: all cores in a package share one clock */
	if ((current_cpu_type() == CPU_LOONGSON3) &&
		((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R1)) {
		cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));
	/* 3a4000 */
	} else if ((current_cpu_type() == CPU_LOONGSON3_COMP)) {

		return cpufreq_table_validate_and_show(policy,
			ls3a4000_freq_table);
	}

	return cpufreq_table_validate_and_show(policy,
		&loongson3_clockmod_table[0]);
}

static int loongson3_cpufreq_exit(struct cpufreq_policy *policy)
{
	return 0;
}

static struct cpufreq_driver loongson3_cpufreq_driver = {
	.name = "loongson3",
	.init = loongson3_cpufreq_cpu_init,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = loongson3_cpufreq_target,
	.get = loongson3_cpufreq_get,
	.exit = loongson3_cpufreq_exit,
	.attr = cpufreq_generic_attr,
};

static struct platform_device_id platform_device_ids[] = {
	{
		.name = "loongson3_cpufreq",
	},
	{}
};

MODULE_DEVICE_TABLE(platform, platform_device_ids);

static struct platform_driver platform_driver = {
	.driver = {
		.name = "loongson3_cpufreq",
		.owner = THIS_MODULE,
	},
	.id_table = platform_device_ids,
};

/*
 * This is the simple version of Loongson-3 wait, Maybe we need do this in
 * interrupt disabled content
 */

void loongson3_cpu_wait(void)
{
	u32 cpu_freq, shared_cpus = 0;
	int i, cpu = smp_processor_id();
	uint64_t core_id = cpu_data[cpu].core;
	uint64_t package_id = cpu_data[cpu].package;

	for_each_online_cpu(i)
		if (cpu_data[i].package == package_id)
			shared_cpus++;

	if (shared_cpus > 1)
		goto out;

	if ((current_cpu_type() == CPU_LOONGSON3) &&
		((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R1)) {
		cpu_freq = LOONGSON_CHIPCFG(package_id);
		LOONGSON_CHIPCFG(package_id) &= ~0x7;    /* Put CPU into wait mode */
		LOONGSON_CHIPCFG(package_id) = cpu_freq; /* Restore CPU state */
	} else {
		cpu_freq = LOONGSON_FREQCTRL(package_id);
		LOONGSON_FREQCTRL(package_id) &= ~(0x7 << (core_id*4)); /* Put CPU into wait mode */
		LOONGSON_FREQCTRL(package_id) = cpu_freq;               /* Restore CPU state */
	}

out:
	local_irq_enable();
}
EXPORT_SYMBOL_GPL(loongson3_cpu_wait);

static int compute_scale(int dividor, int dividee)
{
	int i;
	int result = 0;
	int remainder = 0;

	result = dividor / dividee;
	remainder = (dividor % dividee) * 10;

	for (i = 0; i < scale_resolution; i++) {
		result =  result * 10 +  remainder  / dividee;
		remainder = (remainder % dividee) * 10;
		shift = shift * 10;
	}

	return result;
}

static int get_cpufreq_info(void)
{
	struct timespec64 prev_ts;
	struct timespec64 curr_ts;
        ktime_t delay = ktime_set(0, 100);
	uint64_t timeout = 300000000;
	int ret;
	int message;
	int val;
	int i;
	enum freq min_freq_level;
	enum freq max_freq_level;

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}

	         __set_current_state(TASK_INTERRUPTIBLE);
	         schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
	}

	if (ret != 0) {
		pr_err("file %s, line %d, not support dvfs\n", __FILE__, __LINE__);
		return ret;
	}

	message = DVFS_INFO << 24;
	write_csr(0x51c, message);

	val = read_csr(0x420);
	val |= 1 << 10;
	write_csr(0x420, val);

	ktime_get_ts64(&prev_ts);
	ktime_get_ts64(&curr_ts);

	ret = -EPERM;
	while (((curr_ts.tv_sec - prev_ts.tv_sec) * 1000000000 + (curr_ts.tv_nsec - prev_ts.tv_nsec)) < timeout) {
		ktime_get_ts64(&curr_ts);

		if (read_csr(0x51c) & COMPLETE_STATUS) {
			ret = 0;
			break;
		}

	         __set_current_state(TASK_INTERRUPTIBLE);
	         schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
	}

	if (ret != 0) {
		pr_err("file %s, line %d, not support dvfs\n", __FILE__, __LINE__);
		return ret;
	}

	val = read_csr(0x51c);

	if (val & DVFS_INFO_BOOST_CORE_FREQ) {
		ls_boost_supported = 1;
		ls_boost_freq = ((val & DVFS_INFO_BOOST_CORE_FREQ) >> 8) * 25;
	}

	min_freq_level = val & DVFS_INFO_MIN_FREQ;
	max_freq_level = (val & DVFS_INFO_MAX_FREQ) >> 4;

	ls_upper_index = (val & DVFS_INFO_NORMAL_CORE_UPPER_LIMIT) >> 16;
	ls_boost_cores = (val & DVFS_INFO_BOOST_CORES) >> 20;

	ls_stable_base_freq = calc_const_freq() / 1000000;
	ls_clock_scale = compute_scale(ls_boost_freq, ls_stable_base_freq);

	for (i = min_freq_level; i <= max_freq_level; i++) {
		ls3a4000_normal_table[i].frequency = ((cpu_clock_freq / 1000) * (8 + RESERVED_FREQ - i)) / 8;
		ls3a4000_boost_table[i].frequency = ((cpu_clock_freq / 1000) * (8 + RESERVED_FREQ - i)) / 8;
	}

	if (ls_boost_supported) {
		for (i = min_freq_level; i <= max_freq_level; i++)
		ls3a4000_boost_table[i].frequency = ((ls_boost_freq) * (8 + RESERVED_FREQ - i)) * 1000 / 8;
	}

	ls3a4000_freq_table = ls3a4000_normal_table;

	return ret;
}

static int __init cpufreq_init(void)
{
	int i, ret;
	extern int hpet_enabled;

	if (!hpet_enabled)
		return -ENODEV;

	if ((current_cpu_type() == CPU_LOONGSON3_COMP))  { /* 3a4000 */
		ret = get_cpufreq_info();
		if (ret) {
			pr_err("3A4000 not support dvfs\n");
			return ret;
		}

		for (i = 0; i < MAX_PACKAGES; i++) {
			mutex_init(&ls3a4000_mutex[i]);
		}
	} else
		return -ENODEV;

	for (i = 0; i < MAX_PACKAGES; i++) {
		spin_lock_init(&cpufreq_reg_lock[i]);
	}

	/* Register platform stuff */
	ret = platform_driver_register(&platform_driver);
	if (ret)
		return ret;

	pr_info("cpufreq: Loongson-3 CPU frequency driver.\n");

	if ((current_cpu_type() == CPU_LOONGSON3_COMP))  {
		cpufreq_register_notifier(&ls3a4000_cpufreq_notifier_block,
			  CPUFREQ_TRANSITION_NOTIFIER);
	} else {
		cpufreq_register_notifier(&loongson3_cpufreq_notifier_block,
			  CPUFREQ_TRANSITION_NOTIFIER);
	}

	ret = cpufreq_register_driver(&loongson3_cpufreq_driver);

	if (!ret && !nowait) {
		saved_cpu_wait = cpu_wait;
		cpu_wait = loongson3_cpu_wait;
	}

	return ret;
}

static void __exit cpufreq_exit(void)
{
	if (!nowait && saved_cpu_wait)
		cpu_wait = saved_cpu_wait;
	cpufreq_unregister_driver(&loongson3_cpufreq_driver);
	if ((current_cpu_type() == CPU_LOONGSON3_COMP))  {
		cpufreq_unregister_notifier(&ls3a4000_cpufreq_notifier_block,
		  CPUFREQ_TRANSITION_NOTIFIER);
	} else {
		cpufreq_unregister_notifier(&loongson3_cpufreq_notifier_block,
		    CPUFREQ_TRANSITION_NOTIFIER);
	}

	platform_driver_unregister(&platform_driver);
}

module_init(cpufreq_init);
module_exit(cpufreq_exit);

module_param(nowait, uint, 0644);
MODULE_PARM_DESC(nowait, "Disable Loongson-3A/3B specific wait");

MODULE_AUTHOR("Huacai Chen <chenhc@lemote.com>");
MODULE_DESCRIPTION("CPUFreq driver for Loongson-3A/3B");
MODULE_LICENSE("GPL");
