/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Embedded Alley Solutions, Inc
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2009 Jiajie Chen (chenjiajie@cse.buaa.edu.cn)
 */
#ifndef __ASM_MACH_LOONGSON2_KERNEL_ENTRY_H
#define __ASM_MACH_LOONGSON2_KERNEL_ENTRY_H

/*
 * Override macros used in arch/mips/kernel/head.S.
 */

	.macro	kernel_entry_setup
	.set	push
	.set	mips64
	/* Enable STFill Buffer */
	mfc0	t0, $16, 6
	or	t0, 0x100
	mtc0	t0, $16, 6
	_ehb
	.set	pop
	.endm

/*
 * Do SMP slave processor setup.
 */
	.macro	smp_slave_setup
	.set	push
	.set	mips64
	/* Enable STFill Buffer */
	mfc0	t0, $16, 6
	or	t0, 0x100
	mtc0	t0, $16, 6
	_ehb
	.set	pop
	.endm

#endif /* __ASM_MACH_LOONGSON2_KERNEL_ENTRY_H */
