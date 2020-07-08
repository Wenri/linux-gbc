#include <linux/kernel.h>
#include <asm/time.h>
#include <asm/mach-loongson2/2k1000.h>
#include <hpet.h>
#include <asm/sched_clock.h>

unsigned long get_cpu_clock(void)
{
	unsigned long node_pll_hi;
	unsigned long node_pll_lo;
	unsigned long ret;
	int div_loopc;
	int div_ref;
	int div_out;
	int l2_out;

	node_pll_lo =  readq((void *)CKSEG1ADDR(CONF_BASE + PLL_SYS0_OFF));
	node_pll_hi =  readq((void *)CKSEG1ADDR(CONF_BASE + PLL_SYS1_OFF));

	div_ref = (node_pll_lo >> 26) & 0x3f;
	div_loopc = (node_pll_lo >> 32) & 0x3ff;
	div_out = (node_pll_lo >> 42) & 0x3f;
	l2_out = node_pll_hi & 0x3f;

	ret = (unsigned long)100000000 * div_loopc;
	l2_out = l2_out * div_out * div_ref;

	do_div(ret, l2_out);

	return ret;
}

static u32 notrace mips_cp0_read_sched_clock(void)
{
	return read_c0_count();
}

void __init plat_time_init(void)
{
	mips_hpt_frequency = get_cpu_clock() / 2;
	pr_info("MIPS Counter Frequency is: %d Mhz\n",
			mips_hpt_frequency / 1000000);
	setup_sched_clock(mips_cp0_read_sched_clock, 32, mips_hpt_frequency);
	sched_clock_postinit();
#ifdef CONFIG_LS2K_HPET
	setup_hpet_timer();
#endif
}
