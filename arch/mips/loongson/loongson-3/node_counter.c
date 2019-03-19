#include <linux/init.h>
#include <asm/time.h>
#include <loongson.h>

#define NODE_COUNTER_FREQ cpu_clock_freq
#define NODE_COUNTER_PTR 0x900000003FF00408UL
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

int __init init_node_counter_clocksource(void)
{
	int res;

	if (!NODE_COUNTER_FREQ)
		return -ENXIO;

	/* Loongson3A2000 and Loongson3A3000 has node counter */
	switch(current_cpu_type()){
	case CPU_LOONGSON3:	
		switch (read_c0_prid() & 0xF) {
		case PRID_REV_LOONGSON3A_R2:
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
