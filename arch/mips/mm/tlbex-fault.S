/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.macro tlb_do_page_fault, write
	NESTED(tlb_do_page_fault_\write, PT_SIZE, sp)
	SAVE_ALL

	/* Only Check for 3A4000 and later guest */
	PTR_LA	t0, guest_fixup
	ld	t0, 0(t0)
	beqz	t0, 1f
	/* Invalid the refilled line */
	tlbp
	mfc0	t0, CP0_INDEX
	bltz	t0, 1f
	dmfc0	t0, CP0_ENTRYHI
	ori	t0, t0, MIPS_ENTRYHI_EHINV
	dmtc0	t0, CP0_ENTRYHI
	___ehb
	tlbwi
1:
	MFC0	a2, CP0_BADVADDR
	KMODE
	move	a0, sp
	REG_S	a2, PT_BVADDR(sp)
	li	a1, \write
	PTR_LA	ra, ret_from_exception
	j	do_page_fault
	END(tlb_do_page_fault_\write)
	.endm

	tlb_do_page_fault 0
	tlb_do_page_fault 1
