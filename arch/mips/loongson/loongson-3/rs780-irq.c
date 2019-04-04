#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

extern int ls3a_msi_enabled;
extern unsigned long long smp_group[4];
extern void loongson3_send_irq_by_ipi(int cpu, int irqs);
extern void loongson3_ipi_interrupt(struct pt_regs *regs);

unsigned int irq_cpu[16] = {[0 ... 15] = -1};
#ifdef CONFIG_KVM_GUEST_LS3A3000
unsigned int ht_irq[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
#else
unsigned int ht_irq[] = {0, 1, 3, 4, 5, 6, 7, 8, 12, 14, 15};
#endif
unsigned int local_irq = 1<<0 | 1<<1 | 1<<2 | 1<<7 | 1<<8 | 1<<12;

static int bootcore_int_mask2 = 1;
extern unsigned int rs780e_irq2pos[];
extern unsigned int rs780e_pos2irq[];
static unsigned int irq_msi[LS3A_NUM_MSI_IRQS] = {[0 ... LS3A_NUM_MSI_IRQS-1] = -1};

static void dispatch_msi_irq(int cpu, int htid)
{
	/*dispatch ht irqs for smi*/
	int i, irq, irq1;
	uint64_t irqs, irq0;
	int start, end;
	int cpumask;
	struct irq_data *irqd;

	start = htid;
	end = (htid == 0)?3:htid;
	cpumask=(htid == 0)?bootcore_int_mask2:(1<<htid);


	for(i=start;i<=end && cpumask;i++)
	{
                if(!(cpumask&(1<<i))) continue;
		cpumask &= ~(1<<i);
		irqs = LOONGSON_HT1_INT_VECTOR64(i);
		if(i == 0)
			irqs &= ~0xffffULL;
		if(!irqs) continue;
		LOONGSON_HT1_INT_VECTOR64(i) = irqs;
		__sync();
		irq0 = 0;
		while(irqs){
			irq = __ffs(irqs);
			irqs &= ~(1ULL<<irq);

			irq1 = i*64+irq;

			irqd = irq_get_irq_data(IRQ_LS3A_MSI_0 + irq1);

			irq_msi[irq1] = cpumask_next(irq_msi[irq1], irqd->affinity);

			if (irq_msi[irq1] >= nr_cpu_ids)
				irq_msi[irq1] = cpumask_first(irqd->affinity);

			if (irq_msi[irq1] == cpu || !rs780e_irq2pos[irq1] || !cpu_online(irq_msi[irq1])) {
				irq0 |= 1ULL<<irq;
			}
			else
				loongson3_send_irq_by_ipi(irq_msi[irq1], (0x1 << (rs780e_irq2pos[irq1]-1+16)));
		}

		while(irq0){
			irq = __ffs(irq0);
			do_IRQ(IRQ_LS3A_MSI_0+i*64+irq);
			irq0 &= ~(1ULL<<irq);
		}
	}
}

void ht0_irq_dispatch(void)
{
	unsigned int i, irq;
	struct irq_data *irqd;
	struct cpumask affinity;

	irq = LOONGSON_HT1_INT_VECTOR64(0) & 0xffff;
	LOONGSON_HT1_INT_VECTOR64(0) = irq;

	for (i = 0; i < (sizeof(ht_irq) / sizeof(*ht_irq)); i++) {
		if (!(irq & (0x1 << ht_irq[i])))
			continue;

		/* handled by local core */
		if (local_irq & (0x1 << ht_irq[i])) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		irqd = irq_get_irq_data(ht_irq[i]);
		cpumask_and(&affinity, irqd->affinity, cpu_active_mask);
		if (cpumask_empty(&affinity)) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		irq_cpu[ht_irq[i]] = cpumask_next(irq_cpu[ht_irq[i]], &affinity);
		if (irq_cpu[ht_irq[i]] >= nr_cpu_ids)
			irq_cpu[ht_irq[i]] = cpumask_first(&affinity);

		if (irq_cpu[ht_irq[i]] == 0) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		/* balanced by other cores */
		loongson3_send_irq_by_ipi(irq_cpu[ht_irq[i]], (0x1 << ht_irq[i]));
	}

}

void rs780_irq_dispatch(void)
{
	int cpu = smp_processor_id();
	int rawcpu = cpu_logical_map(cpu);
	int htid =(rawcpu == loongson_boot_cpu_id)?0:(rawcpu == 0)?loongson_boot_cpu_id:rawcpu;

	if(htid == 0)
		ht0_irq_dispatch();
	dispatch_msi_irq(cpu, htid);
}

void rs780_irq_router_init(void)
{
	int i, rawcpu;

	*(volatile int *)(LOONGSON_HT1_CFG_BASE+0x58) &= ~0x700;
	/* route LPC int to cpu core0 int 0 */
	LOONGSON_INT_ROUTER_LPC = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 0);
	/* route HT1 int0 ~ int7 to cpu core0 INT1*/
	LOONGSON_INT_ROUTER_HT1(0) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);
	for (i = 1; i <= 3; i++)
	{
		rawcpu = (i == loongson_boot_cpu_id)?0:i; 

		if(cpu_number_map(rawcpu)<setup_max_cpus && cpu_number_map(rawcpu)<nr_cpu_ids)
			LOONGSON_INT_ROUTER_HT1(i) = LOONGSON_INT_COREx_INTy(rawcpu, 1);
		else
		{
			LOONGSON_INT_ROUTER_HT1(i) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);
			bootcore_int_mask2 |= 1<<i;
		}
	}

	for (i = 4; i < 8; i++)
		LOONGSON_INT_ROUTER_HT1(i) = LOONGSON_INT_COREx_INTy(loongson_boot_cpu_id, 1);
	/* enable HT1 interrupt */
	LOONGSON_HT1_INTN_EN(0) = 0xffffffff;
	LOONGSON_HT1_INTN_EN(1) = 0xffffffff;
	LOONGSON_HT1_INTN_EN(2) = 0x00000000;
	LOONGSON_HT1_INTN_EN(3) = 0x00000000;
	/* enable router interrupt intenset */
	LOONGSON_INT_ROUTER_INTENSET = LOONGSON_INT_ROUTER_INTEN | (0xffff << 16) | 0x1 << 10;
}

void __init rs780_init_irq(void)
{
	int prid = read_c0_prid();
	if(((prid & 0xff00) == PRID_IMP_LOONGSON2) && ((prid & 0xff) <= PRID_REV_LOONGSON3A_R3_1)) {
		pr_info("Do not supports HT MSI interrupt, disabling RS780E MSI Interrupt.\n");
		ls3a_msi_enabled = 0;
	} else {
		pr_info("Supports HT MSI interrupt, enabling RS780E MSI Interrupt.\n");
		ls3a_msi_enabled = 1;
	}
	rs780_irq_router_init();
	init_i8259_irqs();
}
