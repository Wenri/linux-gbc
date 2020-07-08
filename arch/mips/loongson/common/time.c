/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <asm/mc146818-time.h>
#include <asm/time.h>
#include <asm/hpet.h>

#include <loongson.h>
#include <cs5536/cs5536_mfgpt.h>
#include <loongson-pch.h>

static cycle_t read_const_cntr64(struct clocksource *clk)
{
	cycle_t count;
	__asm__ __volatile__(
		"       .set push\n"
		"       .set mips32r2\n"
		"       rdhwr   %0, $30\n"
		"       .set pop\n"
		: "=r" (count));

	return count;
}

static struct clocksource clocksource_loongson = {
       .name           = "LOONGSON_CONST",
       .read           = read_const_cntr64,
       .mask           = CLOCKSOURCE_MASK(64),
       .flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};
extern unsigned long long read_node_counter(void);
unsigned long long read_stable_counter(void)
{
	/* 64-bit arithmatic can overflow, so use 128-bit.  */
	u64 t1, t2, t3;
	unsigned long long rv;
	u64 mult = clocksource_loongson.mult;
	u64 shift = clocksource_loongson.shift;
	u64 cnt = read_const_cntr64(NULL);

	asm (
		"dmultu\t%[cnt],%[mult]\n\t"
		"nor\t%[t1],$0,%[shift]\n\t"
		"mfhi\t%[t2]\n\t"
		"mflo\t%[t3]\n\t"
		"dsll\t%[t2],%[t2],1\n\t"
		"dsrlv\t%[rv],%[t3],%[shift]\n\t"
		"dsllv\t%[t1],%[t2],%[t1]\n\t"
		"or\t%[rv],%[t1],%[rv]\n\t"
		: [rv] "=&r" (rv), [t1] "=&r" (t1), [t2] "=&r" (t2), [t3] "=&r" (t3)
		: [cnt] "r" (cnt), [mult] "r" (mult), [shift] "r" (shift)
		: "hi", "lo");
	return rv;
}

unsigned long long (* read_sched_clock)(void);
static unsigned long long legacy_sched_clock(void)
{
	return (unsigned long long)(jiffies - INITIAL_JIFFIES)
					* (NSEC_PER_SEC / HZ);
}

static void loongson3_init_clock(void)
{
	unsigned long freq;

	int res = (read_c0_prid() & 0xffffff);
	if (res >= 0x146308 && res < 0x14630f) {
		/* for 3a2000/3a3000 const-freq counter freq equal cpu_clock_freq */
		clocksource_loongson.rating = 320;
		freq = cpu_clock_freq;
		read_sched_clock = read_node_counter;
	} else if (current_cpu_type() == CPU_LOONGSON3_COMP) {
		clocksource_loongson.rating = 380;
		freq = calc_const_freq();
		read_sched_clock = read_stable_counter;
	} else {
		read_sched_clock = legacy_sched_clock;
		return;
	}

	clocksource_register_hz(&clocksource_loongson, freq);
	pr_info("loongson const clocksource register @%ldMhz!\n", freq/1000000);
}

unsigned long long notrace sched_clock(void)
{
	if (likely(read_sched_clock))
		return read_sched_clock();
	else
		return jiffies;
}

void __init plat_time_init(void)
{
	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;

	loongson3_init_clock();

	if(cpu_guestmode)
		preset_lpj = cpu_clock_freq / HZ;

#if defined(CONFIG_RS780_HPET) || defined(CONFIG_LS7A_HPET)
	if(!cpu_guestmode)
		setup_hpet_timer();
#else
	setup_mfgpt0_timer();
#endif
}

void read_persistent_clock(struct timespec *ts)
{
	if (loongson_pch->board_type == LS7A)
		ts->tv_sec = loongson_ls7a_get_rtc_time();
	else if (loongson_pch->board_type == RS780E)
		ts->tv_sec = mc146818_get_cmos_time();
	ts->tv_nsec = 0;
}
