#include <linux/init.h>
#include <asm/time.h>
#include <loongson.h>
#include <linux/timekeeper_internal.h>
#include <asm/cevt-r4k.h>

#define NODE_COUNTER_FREQ cpu_clock_freq
#define NODE_COUNTER_PTR 0x900000003FF00408UL

static struct clocksource csrc_node_counter;

#ifndef CONFIG_KVM_GUEST_LS3A3000
static cycle_t node_counter_read(struct clocksource *cs)
{
	cycle_t count;
	unsigned long mask, delta, tmp;
	volatile unsigned long *counter = (unsigned long *)NODE_COUNTER_PTR;

	asm volatile (
		"ld	%[count], %[counter] \n\t"
		"andi	%[tmp], %[count], 0xff \n\t"
		"sltiu	%[delta], %[tmp], 0xf9 \n\t"
		"li	%[mask], -1 \n\t"
		"bnez	%[delta], 1f \n\t"
		"addiu	%[tmp], -0xf8 \n\t"
		"sll	%[tmp], 3 \n\t"
		"dsrl	%[mask], %[tmp] \n\t"
		"daddiu %[delta], %[mask], 1 \n\t"
		"dins	%[mask], $0, 0, 8 \n\t"
		"dsubu	%[delta], %[count], %[delta] \n\t"
		"and	%[tmp], %[count], %[mask] \n\t"
		"dsubu	%[tmp], %[mask] \n\t"
		"movz	%[count], %[delta], %[tmp] \n\t"
		"1:	\n\t"
		:[count]"=&r"(count), [mask]"=&r"(mask),
		 [delta]"=&r"(delta), [tmp]"=&r"(tmp)
		:[counter]"m"(*counter)
	);

	return count;
}
#else
static cycle_t node_counter_read(struct clocksource *cs)
{
	cycle_t count;
	volatile unsigned long *counter = (unsigned long *)NODE_COUNTER_PTR;

	count = *counter;

	return count;
}
#endif

static unsigned long long read_ns_counter(void)
{
	/* 64-bit arithmatic can overflow, so use 128-bit.  */
	u64 t1, t2, t3;
	unsigned long long rv;
	u64 mult = csrc_node_counter.mult;
	u64 shift = csrc_node_counter.shift;
	u64 cnt = node_counter_read(NULL);

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
unsigned long long read_node_counter(void)
{
	return read_ns_counter();
}

static cycle_t node_counter_csr_read(struct clocksource *cs)
{
	return dread_csr(LOONGSON_CSR_NODE_CONTER);
}

static void node_counter_suspend(struct clocksource *cs)
{
}

static void node_counter_resume(struct clocksource *cs)
{
}

static struct clocksource csrc_node_counter = {
	.name = "node_counter",
	/* mips clocksource rating is less than 300, so node_counter is better. */
	.rating = 360,
	.read = node_counter_read,
	.mask = CLOCKSOURCE_MASK(64),
	/* oneshot mode work normal with this flag */
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.suspend = node_counter_suspend,
	.resume = node_counter_resume,
	.mult = 0,
	.shift = 10,
};

extern void update_clocksource_for_loongson(struct clocksource *cs);
extern unsigned long loops_per_jiffy;

/* used for adjust clocksource and clockevent for guest when
 *  migrate to a diffrent cpu freq.
 */
void loongson_nodecounter_adjust(void)
{
	unsigned int cpu;
	struct clock_event_device *cd;
	struct clocksource *cs = &csrc_node_counter;
	u32 cpu_freq;

	cpu_freq = LOONGSON_FREQCTRL(0);

	for_each_online_cpu(cpu){
		cd = &per_cpu(mips_clockevent_device, cpu);
		clockevents_update_freq(cd, cpu_freq / 2);
		cpu_data[cpu].udelay_val = cpufreq_scale(loops_per_jiffy, cpu_clock_freq / 1000, cpu_freq / 1000);
	}

	__clocksource_updatefreq_scale(cs, 1, cpu_freq);
	update_clocksource_for_loongson(cs);
}

int __init init_node_counter_clocksource(void)
{
	int res;

	if (!NODE_COUNTER_FREQ)
		return -ENXIO;

	/* Loongson3A2000 and Loongson3A3000 has node counter */
	switch(current_cpu_type()){
	case CPU_LOONGSON3:	
		switch (read_c0_prid() & 0xF) {
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			break;
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
		default:
			return 0;
		}
		break;
	case CPU_LOONGSON3_COMP:
	if(read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_NODE_COUNTER_EN) {
		csrc_node_counter.read = node_counter_csr_read;
		break;
	} else
		return 0;
	default:
		break;
	}
	csrc_node_counter.mult =
		clocksource_hz2mult(NODE_COUNTER_FREQ, csrc_node_counter.shift);
	res = clocksource_register_hz(&csrc_node_counter, NODE_COUNTER_FREQ);
	printk(KERN_INFO "node counter clock source device register\n");

	return res;
}

arch_initcall(init_node_counter_clocksource);
