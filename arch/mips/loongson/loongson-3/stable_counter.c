#include <linux/init.h>
#include <asm/time.h>
#include <loongson.h>
#include <asm/mipsregs.h>
#include <asm/cevt-r4k.h>

#define ls_stable_freq cpu_clock_freq

#define STABLE_TIMER_PERIODIC_EN    (_ULCAST_(1) << 62)
#define STABLE_TIMER_EN             (_ULCAST_(1) << 61)
#define STABLE_TIMER_INITVAL_RST    (_ULCAST_(0xffff) << 48)
#define GSCFG_FLT_EN                (_ULCAST_(1) << 7)
#define GSCFG_VLT_EN                (_ULCAST_(1) << 6)
#define STABLE_TIMER_CFG            0x1060

#define stabel_irq    7

int stable_timer_irq_installed;
static unsigned long long GSconfigFlag;
int stable_timer_enabled = 0;

static DEFINE_SPINLOCK(stable_lock);
DEFINE_PER_CPU(struct clock_event_device, stable_clockevent_device);

irqreturn_t stable_irq_handler(int irq, void *data)
{
	const int r2 = cpu_has_mips_r2;
	struct clock_event_device *cd;
	int cpu = smp_processor_id();

	/*
	 * Suckage alert:
	 * Before R2 of the architecture there was no way to see if a
	 * performance counter interrupt was pending, so we have to run
	 * the performance counter interrupt handler anyway.
	 */
	if (handle_perf_irq(r2))
		goto out;

	/*
	 * The same applies to performance counter interrupts.	But with the
	 * above we now know that the reason we got here must be a timer
	 * interrupt.  Being the paranoiacs we are we check anyway.
	 */
	if (!r2 || (read_c0_cause() & (1 << 30))) {
		/* Clear Count/Compare Interrupt */
		write_c0_compare(read_c0_compare());
		cd = &per_cpu(stable_clockevent_device, cpu);
		cd->event_handler(cd);
	}

out:
	return IRQ_HANDLED;
}

static struct irqaction stable_irq_irqaction = {
	.handler = stable_irq_handler,
	.flags = IRQF_PERCPU | IRQF_SHARED | IRQF_TIMER,
	.name = "stable timer",
};

void stable_event_handler(struct clock_event_device *dev)
{
}

static void stable_set_mode(enum clock_event_mode mode,
		struct clock_event_device *evt)
{
	unsigned long  cfg;
	unsigned int period_init;

	spin_lock(&stable_lock);

	cfg = dread_csr(STABLE_TIMER_CFG);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		printk(KERN_INFO "set stable clock event to periodic mode!\n");
		cfg |= (STABLE_TIMER_PERIODIC_EN | STABLE_TIMER_EN);
		cfg &= STABLE_TIMER_INITVAL_RST;
		period_init = calc_const_freq() / HZ;
		dwrite_csr(STABLE_TIMER_CFG, cfg | period_init);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		printk(KERN_INFO "set stable clock event to oneshot mode!\n");
		/* set timer0 type
		 * 1 : periodic interrupt
		 * 0 : non-periodic(oneshot) interrupt
		 */
		cfg &= ~STABLE_TIMER_PERIODIC_EN;
		cfg |= STABLE_TIMER_EN;
		cfg |= ~STABLE_TIMER_INITVAL_RST;
		dwrite_csr(STABLE_TIMER_CFG, cfg);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
	spin_unlock(&stable_lock);
}


static int stable_next_event(unsigned long delta,
		struct clock_event_device *evt)
{
	unsigned long cfg;

	cfg = dread_csr(STABLE_TIMER_CFG);
	cfg &= ~STABLE_TIMER_PERIODIC_EN;
	cfg |= STABLE_TIMER_EN;
	cfg &= STABLE_TIMER_INITVAL_RST;
	dwrite_csr(STABLE_TIMER_CFG, delta | cfg);
	return 0;
}

static void stable_event_enable(void)
{
	GSconfigFlag = read_c0_config6();
	GSconfigFlag |= GSCFG_FLT_EN;
	GSconfigFlag &= ~GSCFG_VLT_EN;
	write_c0_config6(GSconfigFlag);
}

int stable_clockevent_init(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	unsigned int irq;

	if (!ls_stable_freq)
		return -ENXIO;

	switch (c->cputype) {
	case CPU_LOONGSON3:
		/* Loongson3A2000 and Loongson3A3000 has no stable timer */
		return -ENXIO;
	case CPU_LOONGSON3_COMP:
		ls_stable_freq = calc_const_freq();
		break;
	default:
		return -ENXIO;
	}

	if (!(read_cfg(LOONGSON_CPUCFG_CONFIG_FIELD2) & MIPS_LSE_LLFTP)) {
		printk(KERN_INFO "Don't support Stable timer\n");
		return -EPERM;
	}

	stable_event_enable();

	if (!(read_c0_config6() & GSconfigFlag)) {
		printk(KERN_INFO "Stable timer GSCFG_FLT and GSCFG_VLT incorrect\n");
		return -EPERM;
	}

	irq = MIPS_CPU_IRQ_BASE + stabel_irq;

	cd = &per_cpu(stable_clockevent_device, cpu);

	cd->name = "stable timer";
	if(cpu_guestmode)
		cd->features = CLOCK_EVT_FEAT_ONESHOT;
	else
		cd->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;

	cd->rating = 320;
	cd->irq = irq;
	cd->cpumask = cpumask_of(cpu);
	cd->set_mode = stable_set_mode;
	cd->set_next_event = stable_next_event;
	cd->event_handler   = stable_event_handler;

	clockevents_config_and_register(cd, ls_stable_freq, 0x300, ((1UL << 48) - 1));

	if (stable_timer_irq_installed)
		return 0;

	stable_timer_irq_installed = 1;
	setup_irq(irq, &stable_irq_irqaction);
	stable_timer_enabled = 1;

	printk(KERN_INFO "stable clock event device register\n");

	return 0;
}

static void stable_counter_suspend(struct clocksource *cs)
{
}

static void stable_counter_resume(struct clocksource *cs)
{
}

static cycle_t stable_counter_csr_read(struct clocksource *cs)
{
	return (cycle_t)drdtime();
}

static struct clocksource csrc_stable_counter = {
	.name = "stable counter",
	/* mips clocksource rating is less than 300, so stable_counter is better. */
	.rating = 380,
	.read = stable_counter_csr_read,
	.mask = CLOCKSOURCE_MASK(64),
	/* oneshot mode work normal with this flag */
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.suspend = stable_counter_suspend,
	.resume = stable_counter_resume,
	.mult = 0,
	.shift = 10,
};

static int stable_enable(void)
{
	/*check stable clock feature*/
	if (!(read_cfg(LOONGSON_CPUCFG_CONFIG_FIELD2) & MIPS_LSE_LLFTP)) {
		printk(KERN_INFO "Don't support STABLE counter\n");
		return -ENXIO;
	}

	/*check stable clock enable*/
	if (!(dread_csr(LOONGSON_OTHER_FUNC_OFFSET) & LOONGSON_STABLE_COUNT_EN)) {
		printk(KERN_INFO "stable counter not enable\n");
		return -EBUSY;
	}

	return 0;
}

extern void update_clocksource_for_loongson(struct clocksource *cs);
extern unsigned long loops_per_jiffy;

//used for adjust clocksource and clockevent for guest when migrate to a diffrent cpu freq
void loongson_stablecounter_adjust(void)
{
	unsigned int cpu;
	struct clock_event_device *cd;
	struct clocksource *cs = &csrc_stable_counter;
	u32 new_stable_freq;

	new_stable_freq = LOONGSON_FREQCTRL(0);

	printk("======src_freq 0x%x,new_freq 0x%x\n",ls_stable_freq,new_stable_freq);

	for_each_online_cpu(cpu){
		cd = &per_cpu(stable_clockevent_device, cpu);
		clockevents_update_freq(cd, new_stable_freq);
		cpu_data[cpu].udelay_val = cpufreq_scale(loops_per_jiffy, ls_stable_freq / 1000, new_stable_freq / 1000);
	}

	loops_per_jiffy = cpu_data[0].udelay_val;
	ls_stable_freq = new_stable_freq;
	mips_hpt_frequency = new_stable_freq / 2;
	__clocksource_updatefreq_scale(cs, 1, new_stable_freq);
	update_clocksource_for_loongson(cs);
}


int __init init_stable_clocksource(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	int res;

	if (!ls_stable_freq)
		return -ENXIO;

	switch (c->cputype) {
	case CPU_LOONGSON3:
		/* Loongson3A2000 and Loongson3A3000 has no stable counter */
		return -ENXIO;
	case CPU_LOONGSON3_COMP:
		if (stable_enable() == 0) {
			csrc_stable_counter.read = stable_counter_csr_read;
			break;
		} else
			return -ENXIO;
	default:
		return -ENXIO;
	}

	csrc_stable_counter.mult =
		clocksource_hz2mult(ls_stable_freq, csrc_stable_counter.shift);
	res = clocksource_register_hz(&csrc_stable_counter, ls_stable_freq);

	printk(KERN_INFO "stable counter clock source device register\n");

	return res;
}
arch_initcall(init_stable_clocksource);
