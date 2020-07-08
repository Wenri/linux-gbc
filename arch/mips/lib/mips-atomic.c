/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#include <asm/irqflags.h>
#include <asm/hazards.h>
#include <linux/compiler.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/stringify.h>

#if defined(CONFIG_CPU_LOONGSON3) && defined(CONFIG_PHASE_LOCK)
unsigned int phase_lock[32];
notrace unsigned long loongson3_phase_lock_acquire(void)
{
	unsigned long flags;
	u32 node_id, tmp, cpu;

	flags = arch_local_irq_save();

	__asm__ __volatile__(
		"	.set	mips32					\n"
		"	mfc0	%0, $15, 1				\n"
		"	.set	mips0					\n"
		: "=r" (cpu));

	/*
	 * Make sure phase_lock address is cache-line seperated to avoid
	 * Fake cache-line share.
	 */
#ifdef CONFIG_KVM_GUEST_LS3A3000
	node_id = ((cpu & 0xFF) / 4) << 3;
#else
	node_id = ((cpu & 0x3FF) / 4) << 3;
#endif

	__asm__ __volatile__(
                "       .set    noreorder       # lock for phase    	\n"
		__LS3A_WAR_LLSC
                "1:							\n"
                "	ll      %1, %2					\n"
                "       bnez    %1, 1b					\n"
                "       li	%1, 1					\n"
                "       sc      %1, %0					\n"
                "       beqz    %1, 1b					\n"
                "       nop						\n"
                "       .set	reorder					\n"
                : "=m" (phase_lock[node_id]), "=&r" (tmp)
                : "m" (phase_lock[node_id])
                : "memory");

	return flags;
}
EXPORT_SYMBOL(loongson3_phase_lock_acquire);

notrace void loongson3_phase_lock_release(unsigned long flags)
{
	u32 node_id, cpu;

	__asm__ __volatile__(
		"	.set	mips32					\n"
		"	mfc0	%0, $15, 1				\n"
		"	.set	mips0					\n"
		: "=r" (cpu));

#ifdef CONFIG_KVM_GUEST_LS3A3000
	node_id = ((cpu & 0xFF) / 4) << 3;
#else
	node_id = ((cpu & 0x3FF) / 4) << 3;
#endif

	__asm__ __volatile__(
		"	.set	noreorder       # unlock for phase   	\n"
		"	sync						\n"
		"	sw	$0, %0                                  \n"
		"	sync						\n"
		"	.set\treorder                                 	\n"
		: "=m" (phase_lock[node_id])
		: "m" (phase_lock[node_id])
		: "memory");

	arch_local_irq_restore(flags);
}
EXPORT_SYMBOL(loongson3_phase_lock_release);
#endif
#if !defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_MIPS_MT_SMTC)

/*
 * For cli() we have to insert nops to make sure that the new value
 * has actually arrived in the status register before the end of this
 * macro.
 * R4000/R4400 need three nops, the R4600 two nops and the R10000 needs
 * no nops at all.
 */
/*
 * For TX49, operating only IE bit is not enough.
 *
 * If mfc0 $12 follows store and the mfc0 is last instruction of a
 * page and fetching the next instruction causes TLB miss, the result
 * of the mfc0 might wrongly contain EXL bit.
 *
 * ERT-TX49H2-027, ERT-TX49H3-012, ERT-TX49HL3-006, ERT-TX49H4-008
 *
 * Workaround: mask EXL bit of the result or place a nop before mfc0.
 */
#ifdef CONFIG_CPU_LOONGSON3
#define REDEF(name) legacy_##name
#else
#define REDEF(name) name
#endif
notrace void REDEF(arch_local_irq_disable)(void)
{
	preempt_disable();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	noat						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	$1, $2, 1					\n"
	"	ori	$1, 0x400					\n"
	"	.set	noreorder					\n"
	"	mtc0	$1, $2, 1					\n"
#elif defined(CONFIG_CPU_MIPSR2)
	/* see irqflags.h for inline function */
#else
	"	mfc0	$1,$12						\n"
	"	ori	$1,0x1f						\n"
	"	xori	$1,0x1f						\n"
	"	.set	noreorder					\n"
	"	mtc0	$1,$12						\n"
#endif
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: /* no outputs */
	: /* no inputs */
	: "memory");

	preempt_enable();
}
EXPORT_SYMBOL(REDEF(arch_local_irq_disable));


notrace unsigned long REDEF(arch_local_irq_save)(void)
{
	unsigned long flags;

	preempt_disable();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	reorder						\n"
	"	.set	noat						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	%[flags], $2, 1				\n"
	"	ori	$1, %[flags], 0x400				\n"
	"	.set	noreorder					\n"
	"	mtc0	$1, $2, 1					\n"
	"	andi	%[flags], %[flags], 0x400			\n"
#elif defined(CONFIG_CPU_MIPSR2)
	/* see irqflags.h for inline function */
#else
	"	mfc0	%[flags], $12					\n"
	"	ori	$1, %[flags], 0x1f				\n"
	"	xori	$1, 0x1f					\n"
	"	.set	noreorder					\n"
	"	mtc0	$1, $12						\n"
#endif
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: [flags] "=r" (flags)
	: /* no inputs */
	: "memory");

	preempt_enable();

	return flags;
}
EXPORT_SYMBOL(REDEF(arch_local_irq_save));

notrace void REDEF(arch_local_irq_restore)(unsigned long flags)
{
	unsigned long __tmp1;

#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * SMTC kernel needs to do a software replay of queued
	 * IPIs, at the cost of branch and call overhead on each
	 * local_irq_restore()
	 */
	if (unlikely(!(flags & 0x0400)))
		smtc_ipi_replay();
#endif
	preempt_disable();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	noreorder					\n"
	"	.set	noat						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	$1, $2, 1					\n"
	"	andi	%[flags], 0x400					\n"
	"	ori	$1, 0x400					\n"
	"	xori	$1, 0x400					\n"
	"	or	%[flags], $1					\n"
	"	mtc0	%[flags], $2, 1					\n"
#elif defined(CONFIG_CPU_MIPSR2) && defined(CONFIG_IRQ_CPU)
	/* see irqflags.h for inline function */
#elif defined(CONFIG_CPU_MIPSR2)
	/* see irqflags.h for inline function */
#else
	"	mfc0	$1, $12						\n"
	"	andi	%[flags], 1					\n"
	"	ori	$1, 0x1f					\n"
	"	xori	$1, 0x1f					\n"
	"	or	%[flags], $1					\n"
	"	mtc0	%[flags], $12					\n"
#endif
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: [flags] "=r" (__tmp1)
	: "0" (flags)
	: "memory");

	preempt_enable();
}
EXPORT_SYMBOL(REDEF(arch_local_irq_restore));

notrace void __arch_local_irq_restore(unsigned long flags)
{
	unsigned long __tmp1;

	preempt_disable();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	noreorder					\n"
	"	.set	noat						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	$1, $2, 1					\n"
	"	andi	%[flags], 0x400					\n"
	"	ori	$1, 0x400					\n"
	"	xori	$1, 0x400					\n"
	"	or	%[flags], $1					\n"
	"	mtc0	%[flags], $2, 1					\n"
#elif defined(CONFIG_CPU_MIPSR2) && defined(CONFIG_IRQ_CPU)
	/* see irqflags.h for inline function */
#elif defined(CONFIG_CPU_MIPSR2)
	/* see irqflags.h for inline function */
#else
	"	mfc0	$1, $12						\n"
	"	andi	%[flags], 1					\n"
	"	ori	$1, 0x1f					\n"
	"	xori	$1, 0x1f					\n"
	"	or	%[flags], $1					\n"
	"	mtc0	%[flags], $12					\n"
#endif
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: [flags] "=r" (__tmp1)
	: "0" (flags)
	: "memory");

	preempt_enable();
}
EXPORT_SYMBOL(__arch_local_irq_restore);

#endif /* !defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_MIPS_MT_SMTC) */
