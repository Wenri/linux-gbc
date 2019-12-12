/*
 *  drivers/cpufreq/cpufreq_loongson_boost.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *            (C)  2019 Yi Jun <yijun@loongson.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <asm/mach-loongson/loongson.h>
#include <linux/cpufreq.h>
#include "cpufreq_governor.h"
#include "loongson_boost.h"

struct boost_cpu_dbs_info_s {
	struct cpu_dbs_common_info cdbs;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	unsigned int freq_lo_jiffies;
	unsigned int freq_hi_jiffies;
	unsigned int rate_mult;
	unsigned int sample_type:1;
};

/* Per policy Governors sysfs tunables */
struct boost_dbs_tuners {
	unsigned int ignore_nice_load;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int powersave_bias;
	unsigned int io_is_busy;
};

/* On-demand governor macros */
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_UP_THRESHOLD		(95)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

#define RECORD_LENGTH				(32)
#define BOOST_DELTA				(0xe6666665)

/* factor ** 8 = 0.5 */
#define FACTOR					(0xeac0c6e8)
#define MAX_LOAD				(1193)
#define BOOST_THRESHOLD 		(900)

#define GOV_BOOST                   (2)

static DEFINE_PER_CPU(struct od_cpu_dbs_info_s, boost_cpu_dbs_info);

static struct od_ops boost_ops;

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_LOONGSON_BOOST
static struct cpufreq_governor cpufreq_gov_loongson_boost;
#endif

static unsigned int default_powersave_bias;

static struct mutex boost_mutex[MAX_PACKAGES];

static u64 load_record[MAX_PACKAGES][NR_CPUS/MAX_PACKAGES] = {{0,}, };
static int prev_boost[MAX_PACKAGES] = {0,};

static int curr_boost_core_num[MAX_PACKAGES] = {0, };
static int real_boosted_core_num[MAX_PACKAGES] = {0, };

static int prev_boosted_cores[MAX_PACKAGES][NR_CPUS/MAX_PACKAGES] = {{0, }, };
static int real_boosted_cores[MAX_PACKAGES][NR_CPUS/MAX_PACKAGES] = {{0, }, };

extern struct clocksource csrc_hpet;
/*
 * Not all CPUs want IO time to be accounted as busy; this depends on how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (android.com) claims this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
	return 1;
}

unsigned int find_freq_index(struct cpufreq_frequency_table *freq_table, unsigned int target_freq)
{
	struct cpufreq_frequency_table *pos;
	unsigned int freq;
	unsigned int index = ls_upper_index;

	cpufreq_for_each_valid_entry(pos, freq_table) {
		freq = pos->frequency;
		index = pos - freq_table;

		if (freq == target_freq) {
			break;
		}
	}

	return index;
}

/*
 * normal mode: loads of all cores great than upper threshold
 * boost mode: load of one core great than upper threshold and loads
 * of others cores less than lower
 * boost == 0, normal mode, boost == 1, boost mode
 */

static int boost_mode(int package_id, int cpu, int *boosted_core_num, int *high_load_cores, int load)
{
	int i;
	int curr_boost = 0;
	int core_id = cpu_logical_map(cpu) % cores_per_package;

	if (ls_boost_supported) {
		load_record[package_id][core_id] = (u64) load + ((load_record[package_id][core_id] * FACTOR) >> 32);

		for (i = 0; i < cores_per_package; i++) {
			if (load_record[package_id][i] > BOOST_THRESHOLD) {
				high_load_cores[i] = 1;
				(*boosted_core_num)++;
			} else {
				high_load_cores[i] = 0;
			}
		}

		/* boost mode */
		if (*boosted_core_num > 0 && *boosted_core_num <= ls_boost_cores) {
			curr_boost = 1;
		}
	}

	return curr_boost;
}

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency. Else, we adjust the frequency
 * proportional to load.
 */
static void boost_check_cpu(int cpu, unsigned int load)
{
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(boost_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	struct cpu_dbs_common_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

	/* for boost mode */

	int curr_boost = 0;
	int boosted_core_num = 0;

	unsigned int freq_next, min_f, max_f;

	int i;
	int high_load_cores[NR_CPUS/MAX_PACKAGES] = {0, };
	int retval = -EINVAL;

	int package_id = cpu_data[cpu].package;
	int index;
	struct cpufreq_freqs freqs;
	unsigned int freq_level = 0;
	unsigned int boost_level = 0;
	unsigned int cpu_offset = cpu % cores_per_package;

	mutex_lock(&boost_mutex[package_id]);

	curr_boost = boost_mode(package_id, cpu, &boosted_core_num, high_load_cores, load);

	if (curr_boost == 1) {
		ls3a4000_freq_table_switch(ls3a4000_boost_table);

		for (i = package_id * cores_per_package;
				i < (package_id + 1) * cores_per_package; i++) {
			policy = cpufreq_cpu_get(i);
			if (policy) {
				cpufreq_table_validate_and_show(policy, &ls3a4000_boost_table[0]);
			}
			cpufreq_cpu_put(policy);
		}
	} else {
		ls3a4000_freq_table_switch(ls3a4000_normal_table);

		for (i = package_id * cores_per_package;
				i < (package_id + 1) * cores_per_package; i++) {

			policy = cpufreq_cpu_get(i);
			if (policy) {
				cpufreq_table_validate_and_show(policy, &ls3a4000_normal_table[0]);
			}
			cpufreq_cpu_put(policy);
		}
	}

	/* boost to normal */
	if (prev_boost[package_id] == 1 && curr_boost == 0) {

		/* notification begin*/
		for (i = package_id * cores_per_package;
			i < (package_id + 1) * cores_per_package; i++) {

			dbs_info = &per_cpu(boost_cpu_dbs_info, i);
			dbs_info->freq_lo = 0;
			policy = dbs_info->cdbs.cur_policy;

			freqs.old = policy->cur;
			freqs.flags = 0;

			index = find_freq_index(ls3a4000_boost_table, policy->cur);

			freqs.new = ls3a4000_normal_table[index].frequency;

			cpufreq_freq_transition_begin(policy, &freqs);
		}

		retval = ls3a4000_set_boost(NORMAL_MODE, 0);

		for (i = package_id * cores_per_package;
			i < (package_id + 1) * cores_per_package; i++) {
			dbs_info = &per_cpu(boost_cpu_dbs_info, i);
			dbs_info->freq_lo = 0;
			policy = dbs_info->cdbs.cur_policy;

			freqs.old = policy->cur;
			freqs.flags = 0;

			index = find_freq_index(ls3a4000_boost_table, policy->cur);

			freqs.new = ls3a4000_normal_table[index].frequency;

			cpufreq_freq_transition_end(policy, &freqs, retval);
		}

		real_boosted_core_num[package_id] = 0;
		for (i = 0; i < cores_per_package; i++) {
			real_boosted_cores[package_id][i] = 0;
			prev_boosted_cores[package_id][i] = 0;
		}
	}

	/* normal to boost */
	if (prev_boost[package_id] == 0 && curr_boost == 1) {
		for (i = package_id * cores_per_package;
				i < (package_id + 1) * cores_per_package; i++) {
			dbs_info = &per_cpu(boost_cpu_dbs_info, i);
			dbs_info->freq_lo = 0;
			policy = dbs_info->cdbs.cur_policy;

			min_f = policy->cpuinfo.max_freq / FREQ_DIV;
			max_f = policy->cpuinfo.max_freq;

			freqs.old = policy->cur;
			freqs.flags = 0;

			cpufreq_frequency_table_target(policy, policy->freq_table, max_f, CPUFREQ_RELATION_C, &boost_level);

			/* boost the last core first */
			if (high_load_cores[i % cores_per_package]) {
				freqs.new = max_f;
				real_boosted_cores[package_id][i]  = 1;
				real_boosted_core_num[package_id]++;

				freq_level = freq_level | (boost_level << ((i % cores_per_package) * 4));
			} else {
				freqs.new = max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV;
				freq_level = freq_level | (ls_upper_index << ((i % cores_per_package) * 4));
			}

			pr_debug("%s: cpu: %d, oldfreq: %u, new freq: %u\n",
				__func__, policy->cpu, freqs.old, freqs.new);
			cpufreq_freq_transition_begin(policy, &freqs);
		}

		retval = ls3a4000_set_boost(BOOST_MODE, freq_level);

		for (i = package_id * cores_per_package;
				i < (package_id + 1) * cores_per_package; i++) {
			dbs_info = &per_cpu(boost_cpu_dbs_info, i);
			dbs_info->freq_lo = 0;
			policy = dbs_info->cdbs.cur_policy;

			min_f = policy->cpuinfo.max_freq / FREQ_DIV;
			max_f = policy->cpuinfo.max_freq;

			freqs.old = policy->cur;
			freqs.flags = 0;

			if (high_load_cores[i % cores_per_package]) {
				freqs.new = max_f;
			} else {
				freqs.new = max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV;
			}

			pr_debug("%s: cpu: %d, oldfreq: %u, new freq: %u\n",
				__func__, policy->cpu, freqs.old, freqs.new);
			cpufreq_freq_transition_end(policy, &freqs, retval);
		}

		for (i = 0; i < cores_per_package; i++) {
			prev_boosted_cores[package_id][i] = real_boosted_cores[package_id][i];
		}
	}

	/* normal mode */
	if (prev_boost[package_id] == 0 && curr_boost == 0) {
		dbs_info = &per_cpu(boost_cpu_dbs_info, cpu);
		dbs_info->freq_lo = 0;
		policy = dbs_info->cdbs.cur_policy;
		dbs_data = policy->governor_data;
		cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

		min_f = policy->cpuinfo.max_freq / FREQ_DIV;
		max_f = policy->cpuinfo.max_freq;

		freq_next = min_f + load * (max_f - min_f) / 100;
		__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
	}

	/* boost mode */
	if (prev_boost[package_id] == 1 && curr_boost == 1) {
		dbs_info = &per_cpu(boost_cpu_dbs_info, cpu);
		dbs_info->freq_lo = 0;
		policy = dbs_info->cdbs.cur_policy;
		dbs_data = policy->governor_data;
		cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

		min_f = policy->cpuinfo.max_freq / FREQ_DIV;
		max_f = policy->cpuinfo.max_freq;

		if (real_boosted_core_num[package_id] < boosted_core_num) {
			if (prev_boosted_cores[package_id][cpu_offset] == 0
					&& high_load_cores[cpu_offset] == 1) {

				__cpufreq_driver_target(policy, max_f, CPUFREQ_RELATION_C);
				real_boosted_cores[package_id][cpu_offset] = 1;
				real_boosted_core_num[package_id]++;

			}

			if (prev_boosted_cores[package_id][cpu_offset] == 1
					&& high_load_cores[cpu_offset] == 0) {
				freq_next = min_f + load * (max_f - min_f) / 100;
				freq_next = (freq_next > max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV)
						? max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV : freq_next;
				__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);

				real_boosted_cores[package_id][cpu_offset] = 0;
				real_boosted_core_num[package_id]--;
			}

			if (prev_boosted_cores[package_id][cpu_offset] == 0
					&& high_load_cores[cpu_offset] == 0) {
				freq_next = min_f + load * (max_f - min_f) / 100;
				freq_next = (freq_next > max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV)
						? max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV : freq_next;
				__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
			}
		} else {
			if (prev_boosted_cores[package_id][cpu_offset] == 1
					&& high_load_cores[cpu_offset] == 0) {
				freq_next = min_f + load * (max_f - min_f) / 100;
				freq_next = (freq_next > max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV)
						? max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV : freq_next;
				__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);

				real_boosted_cores[package_id][cpu_offset] = 0;
				real_boosted_core_num[package_id]--;
			}

			if (prev_boosted_cores[package_id][cpu_offset] == 0
					&& high_load_cores[cpu_offset] == 0) {
				freq_next = min_f + load * (max_f - min_f) / 100;
				freq_next = (freq_next > max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV)
						? max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV : freq_next;
				__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
			}

			if (prev_boosted_cores[package_id][cpu_offset] == 0
					&& high_load_cores[cpu_offset] == 1) {
				freq_next = min_f + load * (max_f - min_f) / 100;
				freq_next = (freq_next > max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV)
						? max_f * (ls_upper_index - RESERVED_FREQ) / FREQ_DIV : freq_next;
				__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
			}
		}

		for (i = 0; i < cores_per_package; i++) {
			prev_boosted_cores[package_id][i] = real_boosted_cores[package_id][i];
		}
	}

	prev_boost[package_id] = curr_boost;
	mutex_unlock(&boost_mutex[package_id]);
}

static void check_cpu(struct dbs_data *dbs_data, int cpu)
{
	struct cpu_dbs_common_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cpufreq_policy *policy;
	unsigned int sampling_rate;
	unsigned int max_load = 0;
	unsigned int ignore_nice;
	unsigned int j;

	struct od_cpu_dbs_info_s *od_dbs_info =
			dbs_data->cdata->get_cpu_dbs_info_s(cpu);

	/*
	 * Sometimes, the ondemand governor uses an additional
	 * multiplier to give long delays. So apply this multiplier to
	 * the 'sampling_rate', so as to keep the wake-up-from-idle
	 * detection logic a bit conservative.
	 */
	sampling_rate = od_tuners->sampling_rate;
	sampling_rate *= od_dbs_info->rate_mult;

	ignore_nice = od_tuners->ignore_nice_load;

	policy = cdbs->cur_policy;

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_common_info *j_cdbs;
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load;
		int io_busy = 0;

		j_cdbs = dbs_data->cdata->get_cpu_cdbs(j);

		/*
		 * For the purpose of ondemand, waiting for disk IO is
		 * an indication that you're performance critical, and
		 * not that the system is actually idle. So do not add
		 * the iowait time to the cpu idle time.
		 */
		if (dbs_data->cdata->governor == GOV_BOOST)
			io_busy = od_tuners->io_is_busy;
		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, io_busy);

		wall_time = (unsigned int)
			(cur_wall_time - j_cdbs->prev_cpu_wall);
		j_cdbs->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
			(cur_idle_time - j_cdbs->prev_cpu_idle);
		j_cdbs->prev_cpu_idle = cur_idle_time;

		if (ignore_nice) {
			u64 cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE] -
					 cdbs->prev_cpu_nice;
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			cdbs->prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		/*
		 * If the CPU had gone completely idle, and a task just woke up
		 * on this CPU now, it would be unfair to calculate 'load' the
		 * usual way for this elapsed time-window, because it will show
		 * near-zero load, irrespective of how CPU intensive that task
		 * actually is. This is undesirable for latency-sensitive bursty
		 * workloads.
		 *
		 * To avoid this, we reuse the 'load' from the previous
		 * time-window and give this task a chance to start with a
		 * reasonably high CPU frequency. (However, we shouldn't over-do
		 * this copy, lest we get stuck at a high load (high frequency)
		 * for too long, even when the current system load has actually
		 * dropped down. So we perform the copy only once, upon the
		 * first wake-up from idle.)
		 *
		 * Detecting this situation is easy: the governor's deferrable
		 * timer would not have fired during CPU-idle periods. Hence
		 * an unusually large 'wall_time' (as compared to the sampling
		 * rate) indicates this scenario.
		 *
		 * prev_load can be zero in two cases and we must recalculate it
		 * for both cases:
		 * - during long idle intervals
		 * - explicitly set to zero
		 */
		if (unlikely(wall_time > (2 * sampling_rate) &&
			     j_cdbs->prev_load)) {
			load = j_cdbs->prev_load;

			/*
			 * Perform a destructive copy, to ensure that we copy
			 * the previous load only once, upon the first wake-up
			 * from idle.
			 */
			j_cdbs->prev_load = 0;
		} else {
			load = 100 * (wall_time - idle_time) / wall_time;
			j_cdbs->prev_load = load;
		}

		if (load > max_load)
			max_load = load;
	}

	dbs_data->cdata->gov_check_cpu(cpu, max_load);
}

static void boost_dbs_timer(struct work_struct *work)
{
	struct od_cpu_dbs_info_s *dbs_info =
		container_of(work, struct od_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cur_policy->cpu;
	struct od_cpu_dbs_info_s *core_dbs_info = &per_cpu(boost_cpu_dbs_info,
			cpu);
	struct dbs_data *dbs_data = dbs_info->cdbs.cur_policy->governor_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	int delay = 0;
	bool modify_all = true;

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (!need_load_eval(&core_dbs_info->cdbs, od_tuners->sampling_rate)) {
		modify_all = false;
		goto max_delay;
	}

	/* Common NORMAL_SAMPLE setup */
	core_dbs_info->sample_type = OD_NORMAL_SAMPLE;

	check_cpu(dbs_data, cpu);
	if (core_dbs_info->freq_lo) {
		/* Setup timer for SUB_SAMPLE */
		core_dbs_info->sample_type = OD_SUB_SAMPLE;
		delay = core_dbs_info->freq_hi_jiffies;
	}

max_delay:
	if (!delay)
		delay = delay_for_sampling_rate(od_tuners->sampling_rate
				* core_dbs_info->rate_mult);

	gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, modify_all);
	mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
}

/************************** sysfs interface ************************/
static struct common_dbs_data boost_dbs_cdata;

/**
 * update_sampling_rate - update sampling rate effective immediately if needed.
 * @new_rate: new sampling rate
 *
 * If new rate is smaller than the old, simply updating
 * dbs_tuners_int.sampling_rate might not be appropriate. For example, if the
 * original sampling_rate was 1 second and the requested new sampling rate is 10
 * ms because the user needs immediate reaction from ondemand governor, but not
 * sure if higher frequency will be required or not, then, the governor may
 * change the sampling rate too late; up to 1 second later. Thus, if we are
 * reducing the sampling rate, we need to make the new value effective
 * immediately.
 */
static void update_sampling_rate(struct dbs_data *dbs_data,
		unsigned int new_rate)
{
	struct od_dbs_tuners *boost_tuners = dbs_data->tuners;
	int cpu;

	boost_tuners->sampling_rate = new_rate = max(new_rate,
			dbs_data->min_sampling_rate);

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct od_cpu_dbs_info_s *dbs_info;
		unsigned long next_sampling, appointed_at;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (policy->governor != &cpufreq_gov_loongson_boost) {
			cpufreq_cpu_put(policy);
			continue;
		}
		dbs_info = &per_cpu(boost_cpu_dbs_info, cpu);
		cpufreq_cpu_put(policy);

		mutex_lock(&dbs_info->cdbs.timer_mutex);

		if (!delayed_work_pending(&dbs_info->cdbs.work)) {
			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			continue;
		}

		next_sampling = jiffies + usecs_to_jiffies(new_rate);
		appointed_at = dbs_info->cdbs.work.timer.expires;

		if (time_before(next_sampling, appointed_at)) {

			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			cancel_delayed_work_sync(&dbs_info->cdbs.work);
			mutex_lock(&dbs_info->cdbs.timer_mutex);

			gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy,
					usecs_to_jiffies(new_rate), true);

		}
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
	}
}

static ssize_t store_sampling_rate(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	update_sampling_rate(dbs_data, input);
	return count;
}

static ssize_t store_io_is_busy(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *boost_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	boost_tuners->io_is_busy = !!input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct od_cpu_dbs_info_s *dbs_info = &per_cpu(boost_cpu_dbs_info,
									j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
			&dbs_info->cdbs.prev_cpu_wall, boost_tuners->io_is_busy);
	}
	return count;
}

static ssize_t store_up_threshold(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}

	od_tuners->up_threshold = input;
	return count;
}

static ssize_t store_sampling_down_factor(struct dbs_data *dbs_data,
		const char *buf, size_t count)
{
	struct od_dbs_tuners *boost_tuners = dbs_data->tuners;
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	boost_tuners->sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct od_cpu_dbs_info_s *dbs_info = &per_cpu(boost_cpu_dbs_info,
				j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

static ssize_t store_ignore_nice_load(struct dbs_data *dbs_data,
		const char *buf, size_t count)
{
	struct od_dbs_tuners *boost_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == boost_tuners->ignore_nice_load) { /* nothing to do */
		return count;
	}
	boost_tuners->ignore_nice_load = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct od_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(boost_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
			&dbs_info->cdbs.prev_cpu_wall, boost_tuners->io_is_busy);
		if (boost_tuners->ignore_nice_load)
			dbs_info->cdbs.prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];

	}
	return count;
}

show_store_one(boost, sampling_rate);
show_store_one(boost, io_is_busy);
show_store_one(boost, up_threshold);
show_store_one(boost, sampling_down_factor);
show_store_one(boost, ignore_nice_load);
declare_show_sampling_rate_min(boost);

gov_sys_pol_attr_rw(sampling_rate);
gov_sys_pol_attr_rw(io_is_busy);
gov_sys_pol_attr_rw(up_threshold);
gov_sys_pol_attr_rw(sampling_down_factor);
gov_sys_pol_attr_rw(ignore_nice_load);
gov_sys_pol_attr_ro(sampling_rate_min);

static struct attribute *dbs_attributes_gov_sys[] = {
	&sampling_rate_min_gov_sys.attr,
	&sampling_rate_gov_sys.attr,
	&up_threshold_gov_sys.attr,
	&sampling_down_factor_gov_sys.attr,
	&ignore_nice_load_gov_sys.attr,
	&io_is_busy_gov_sys.attr,
	NULL
};

static struct attribute_group boost_attr_group_gov_sys = {
	.attrs = dbs_attributes_gov_sys,
	.name = "loongson_boost",
};

static struct attribute *dbs_attributes_gov_pol[] = {
	&sampling_rate_min_gov_pol.attr,
	&sampling_rate_gov_pol.attr,
	&up_threshold_gov_pol.attr,
	&sampling_down_factor_gov_pol.attr,
	&ignore_nice_load_gov_pol.attr,
	&io_is_busy_gov_pol.attr,
	NULL
};

static struct attribute_group boost_attr_group_gov_pol = {
	.attrs = dbs_attributes_gov_pol,
	.name = "loongson_boost",
};

/************************** sysfs end ************************/

static int boost_init(struct dbs_data *dbs_data)
{
	struct od_dbs_tuners *tuners;
	u64 idle_time;
	int cpu;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	cpu = get_cpu();
	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		tuners->up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		dbs_data->min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		tuners->up_threshold = DEF_FREQUENCY_UP_THRESHOLD;

		/* For correct statistics, we need 10 ticks for each measure */
		dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
			jiffies_to_usecs(10);
	}

	tuners->sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR;
	tuners->ignore_nice_load = 0;
	tuners->powersave_bias = default_powersave_bias;
	tuners->io_is_busy = should_io_be_busy();

	dbs_data->tuners = tuners;
	mutex_init(&dbs_data->mutex);
	return 0;
}

static void boost_exit(struct dbs_data *dbs_data)
{
	int i;

	ls3a4000_freq_table_switch(ls3a4000_normal_table);

	for (i = 0; i < nr_cpu_ids; i++) {
		load_record[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
		prev_boosted_cores[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
		real_boosted_cores[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
	}


	for (i = 0; i < MAX_PACKAGES; i++) {
		curr_boost_core_num[i] = 0;
		real_boosted_core_num[i] = 0;
	}

	kfree(dbs_data->tuners);
}

define_get_cpu_dbs_routines(boost_cpu_dbs_info);

static struct common_dbs_data boost_dbs_cdata = {
	.governor = GOV_BOOST,
	.attr_group_gov_sys = &boost_attr_group_gov_sys,
	.attr_group_gov_pol = &boost_attr_group_gov_pol,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = boost_dbs_timer,
	.gov_check_cpu = boost_check_cpu,
	.gov_ops = &boost_ops,
	.init = boost_init,
	.exit = boost_exit,
};

static struct attribute_group *get_sysfs_attr(struct dbs_data *dbs_data)
{
	if (have_governor_per_policy())
		return dbs_data->cdata->attr_group_gov_pol;
	else
		return dbs_data->cdata->attr_group_gov_sys;
}

static void set_sampling_rate(struct dbs_data *dbs_data,
		unsigned int sampling_rate)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	od_tuners->sampling_rate = sampling_rate;
}

static inline void gov_cancel_work(struct dbs_data *dbs_data,
		struct cpufreq_policy *policy)
{
	struct cpu_dbs_common_info *cdbs;
	int i;

	for_each_cpu(i, policy->cpus) {
		cdbs = dbs_data->cdata->get_cpu_cdbs(i);
		cancel_delayed_work_sync(&cdbs->work);
	}
}

static int boost_cpufreq_governor_dbs(struct cpufreq_policy *policy,
		unsigned int event)
{
	struct dbs_data *dbs_data;
	struct od_cpu_dbs_info_s *od_dbs_info = NULL;
	struct od_ops *od_ops = NULL;
	struct od_dbs_tuners *od_tuners = NULL;
	struct cpu_dbs_common_info *cpu_cdbs;

	struct common_dbs_data *cdata = &boost_dbs_cdata;

	unsigned int sampling_rate, latency, ignore_nice, j, cpu = policy->cpu;
	int io_busy = 0;
	int rc;

	if (have_governor_per_policy())
		dbs_data = policy->governor_data;
	else
		dbs_data = cdata->gdbs_data;

	WARN_ON(!dbs_data && (event != CPUFREQ_GOV_POLICY_INIT));

	switch (event) {
	case CPUFREQ_GOV_POLICY_INIT:
		if (have_governor_per_policy()) {
			WARN_ON(dbs_data);
		} else if (dbs_data) {
			dbs_data->usage_count++;
			policy->governor_data = dbs_data;
			return 0;
		}

		dbs_data = kzalloc(sizeof(*dbs_data), GFP_KERNEL);
		if (!dbs_data) {
			pr_err("%s: POLICY_INIT: kzalloc failed\n", __func__);
			return -ENOMEM;
		}

		dbs_data->cdata = cdata;
		dbs_data->usage_count = 1;
		rc = cdata->init(dbs_data);
		if (rc) {
			pr_err("%s: POLICY_INIT: init() failed\n", __func__);
			kfree(dbs_data);
			return rc;
		}

		if (!have_governor_per_policy())
			WARN_ON(cpufreq_get_global_kobject());

		rc = sysfs_create_group(get_governor_parent_kobj(policy),
				get_sysfs_attr(dbs_data));
		if (rc) {
			cdata->exit(dbs_data);
			kfree(dbs_data);
			return rc;
		}

		policy->governor_data = dbs_data;

		/* policy latency is in ns. Convert it to us first */
		latency = policy->cpuinfo.transition_latency / 1000;
		if (latency == 0)
			latency = 1;

		/* Bring kernel and HW constraints together */
		dbs_data->min_sampling_rate = max(dbs_data->min_sampling_rate,
				MIN_LATENCY_MULTIPLIER * latency);
		set_sampling_rate(dbs_data, max(dbs_data->min_sampling_rate,
					latency * LATENCY_MULTIPLIER));

		if (!have_governor_per_policy())
			cdata->gdbs_data = dbs_data;

		return 0;
	case CPUFREQ_GOV_POLICY_EXIT:
		if (!--dbs_data->usage_count) {
			sysfs_remove_group(get_governor_parent_kobj(policy),
					get_sysfs_attr(dbs_data));

			if (!have_governor_per_policy())
				cpufreq_put_global_kobject();

			cdata->exit(dbs_data);
			kfree(dbs_data);
			cdata->gdbs_data = NULL;
		}

		policy->governor_data = NULL;
		return 0;
	}

	cpu_cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

	od_tuners = dbs_data->tuners;
	od_dbs_info = dbs_data->cdata->get_cpu_dbs_info_s(cpu);
	sampling_rate = od_tuners->sampling_rate;
	ignore_nice = od_tuners->ignore_nice_load;
	od_ops = dbs_data->cdata->gov_ops;
	io_busy = od_tuners->io_is_busy;

	switch (event) {
	case CPUFREQ_GOV_START:
		if (!policy->cur)
			return -EINVAL;

		mutex_lock(&dbs_data->mutex);

		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_common_info *j_cdbs =
				dbs_data->cdata->get_cpu_cdbs(j);
			unsigned int prev_load;

			j_cdbs->cpu = j;
			j_cdbs->cur_policy = policy;
			j_cdbs->prev_cpu_idle = get_cpu_idle_time(j,
					       &j_cdbs->prev_cpu_wall, io_busy);

			prev_load = (unsigned int)
				(j_cdbs->prev_cpu_wall - j_cdbs->prev_cpu_idle);
			j_cdbs->prev_load = 100 * prev_load /
					(unsigned int) j_cdbs->prev_cpu_wall;

			if (ignore_nice)
				j_cdbs->prev_cpu_nice =
					kcpustat_cpu(j).cpustat[CPUTIME_NICE];

			mutex_init(&j_cdbs->timer_mutex);
			INIT_DEFERRABLE_WORK(&j_cdbs->work,
					     dbs_data->cdata->gov_dbs_timer);
		}

		od_dbs_info->rate_mult = 1;
		od_dbs_info->sample_type = OD_NORMAL_SAMPLE;

		mutex_unlock(&dbs_data->mutex);

		/* Initiate timer time stamp */
		cpu_cdbs->time_stamp = ktime_get();

		gov_queue_work(dbs_data, policy,
				delay_for_sampling_rate(sampling_rate), true);

		clocksource_change_rating(&csrc_hpet, 490);

		break;

	case CPUFREQ_GOV_STOP:
		cpufreq_frequency_table_cpuinfo(policy, ls3a4000_normal_table);
		gov_cancel_work(dbs_data, policy);

		clocksource_change_rating(&csrc_hpet, 10);

		mutex_lock(&dbs_data->mutex);
		mutex_destroy(&cpu_cdbs->timer_mutex);
		cpu_cdbs->cur_policy = NULL;

		mutex_unlock(&dbs_data->mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&dbs_data->mutex);
		if (!cpu_cdbs->cur_policy) {
			mutex_unlock(&dbs_data->mutex);
			break;
		}
		mutex_lock(&cpu_cdbs->timer_mutex);
		if (policy->max < cpu_cdbs->cur_policy->cur)
			__cpufreq_driver_target(cpu_cdbs->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > cpu_cdbs->cur_policy->cur)
			__cpufreq_driver_target(cpu_cdbs->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		check_cpu(dbs_data, cpu);
		mutex_unlock(&cpu_cdbs->timer_mutex);
		mutex_unlock(&dbs_data->mutex);
		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_LOONGSON_BOOST
static
#endif
struct cpufreq_governor cpufreq_gov_loongson_boost = {
	.name			= "loongson_boost",
	.governor		= boost_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	int i;

	if (current_cpu_type() == CPU_LOONGSON3_COMP) {
		for (i = 0; i < MAX_PACKAGES; i++) {
			mutex_init(&boost_mutex[i]);
		}

		return cpufreq_register_governor(&cpufreq_gov_loongson_boost);
	} else {
		return -EINVAL;
	}
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	struct od_cpu_dbs_info_s *dbs_info;
	struct cpufreq_policy *policy;
	int i;

	ls3a4000_freq_table_switch(ls3a4000_normal_table);

	for (i = 0; i < nr_cpu_ids; i++) {
		dbs_info = &per_cpu(boost_cpu_dbs_info, i);
		policy = dbs_info->cdbs.cur_policy;

		if (policy) {
			cpufreq_frequency_table_cpuinfo(policy, ls3a4000_normal_table);
		}

		load_record[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
		prev_boosted_cores[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
		real_boosted_cores[i / MAX_PACKAGES][i % MAX_PACKAGES] = 0;
	}

	for (i = 0; i < MAX_PACKAGES; i++) {
		curr_boost_core_num[i] = 0;
		real_boosted_core_num[i] = 0;
	}

	cpufreq_unregister_governor(&cpufreq_gov_loongson_boost);
}

MODULE_AUTHOR(" Yijun <yijun@loongson.cn>");
MODULE_DESCRIPTION("'cpufreq_loongson_boost' - A dynamic cpufreq governor for "
	"Loongson boost mode governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_LOONGSON_BOOST
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
