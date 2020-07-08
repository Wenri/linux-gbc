/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *    written by Carsten Langgaard, carstenl@mips.com
 */
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable-bits.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#include <asm/asmmacro.h>

/* preprocessor replaces the fp in ".set fp=64" with $30 otherwise */
#undef fp

/*
 * Offset to the current process status flags, the first 32 bytes of the
 * stack are not used.
 */
#define ST_OFF (_THREAD_SIZE - 32 - PT_SIZE + PT_STATUS)

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *		       struct thread_info *next_ti, s32 fp_save)
 */
	.align	5
	LEAF(resume)
	mfc0	t1, CP0_STATUS
	LONG_S	t1, THREAD_STATUS(a0)
	cpu_save_nonscratch a0
	LONG_S	ra, THREAD_REG31(a0)

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	cpu_restore_nonscratch a1

	PTR_ADDU	t0, $28, _THREAD_SIZE - 32
	set_saved_sp	t0, t1, t2
#ifdef CONFIG_MIPS_MT_SMTC
	/* Read-modify-writes of Status must be atomic on a VPE */
	mfc0	t2, CP0_TCSTATUS
	ori	t1, t2, TCSTATUS_IXMT
	mtc0	t1, CP0_TCSTATUS
	andi	t2, t2, TCSTATUS_IXMT
	_ehb
	DMT	8				# dmt	t0
	move	t1,ra
	jal	mips_ihb
	move	ra,t1
#endif /* CONFIG_MIPS_MT_SMTC */
	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff01
	and	t1, a3
	LONG_L	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
#ifdef CONFIG_MIPS_MT_SMTC
	_ehb
	andi	t0, t0, VPECONTROL_TE
	beqz	t0, 1f
	emt
1:
	mfc0	t1, CP0_TCSTATUS
	xori	t1, t1, TCSTATUS_IXMT
	or	t1, t1, t2
	mtc0	t1, CP0_TCSTATUS
	_ehb
#endif /* CONFIG_MIPS_MT_SMTC */
	move	v0, a0
	jr	ra
	END(resume)
