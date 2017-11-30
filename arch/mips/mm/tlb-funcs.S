/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Micro-assembler generated tlb handler functions.
 *
 * Copyright (C) 2013  Broadcom Corporation.
 *
 * Based on mm/page-funcs.c
 * Copyright (C) 2012  MIPS Technologies, Inc.
 * Copyright (C) 2012  Ralf Baechle <ralf@linux-mips.org>
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>

#define FASTPATH_SIZE	128
#ifdef CONFIG_KVM_GUEST_LOONGSON_VZ

#define tlbmiss_tlbwr_normal	0x0
#define tlbmiss_tlbwr_huge	0x1
#define tlbm_tlbp_and_tlbwi_normal 0x1000
#define tlbm_tlbp_and_tlbwi_huge 0x1001
#define tlbl_tlbp_and_tlbwi_normal 0x2000
#define tlbl_tlbp_and_tlbwi_huge 0x2001
#define tlbs_tlbp_and_tlbwi_normal 0x3000
#define tlbs_tlbp_and_tlbwi_huge 0x3001

#define HYPCALL		.word	0x42000028	// with code field = 0
EXPORT(tlbmiss_handler_setup_pgd_start)
LEAF(tlbmiss_handler_setup_pgd)
	.set	push
	.set	noreorder
	.set	arch=loongson3a
	lui	a2, %hi(pgd_current)
	mfc0	a1, CP0_EBASE
	andi	a1, a1, 0xff
	sll	a1, a1, 3

	daddiu  a1, a1, %lo(pgd_current)
	jr	ra
	gssdx	a0, 0(a2, a1)
	.set	reorder
	.set	pop
END(tlbmiss_handler_setup_pgd)
EXPORT(tlbmiss_handler_setup_pgd_end)

LEAF(handle_tlbm)
	.set	push
	.set	noreorder
	.set	arch=loongson3a
	lui	k1,%hi(handler_gprs_save)	// tmp_save should be per cpu
	mfc0	k0, CP0_EBASE
	daddiu	k1,%lo(handler_gprs_save)
	andi	k0, k0, 0xff

	sll	k0, k0, 6			//cache line size 2^6 = 64
	daddu	k1, k0, k1
	srl	k0, k0, 3
	gssq	a0, a1, 0(k1)

	gssq	a2, a3, 16(k1)
	sd	v0,  32(k1)
	dmfc0	a0, CP0_BADVADDR
	lui	a1, %hi(swapper_pg_dir)
	dsrl32	a3, a0, 16

	lui	a2, %hi(pgd_current)
	daddiu	k0, k0, %lo(pgd_current)
	gsldx	a2, 0(a2, k0)
	dsrl32	k0, a0, 1

	movz	a1, a2, a3	//a1 hold pgd
	andi	k0, k0, 0x7ff8	//12 bit, since pgd take up 2 16KB page;
	gsldx	a1, 0(a1, k0)
	dsrl	k0, a0, 22

	andi	k0, k0, 0x3ff8	//11 bit, pmd take 1 16KB page;
	gsldx	a1, 0(a1, k0)
	andi	k0, a1, 0x10
	bnez	k0, 3f	//check huge page
	dsrl	k0, a0, 11

	andi	k0, k0, 0x3ff8
	daddu	a3, k0, a1
1:
	sync			//for 3a2000/3a3000 sync here can remove
	lld	k0, 0(a3)
	andi	a2, k0, 0x2
	beqz	a2, 4f
	ori	a2, k0, 0x180c

	scd	a2, 0(a3)
	beqz	a2, 1b
	nop
	li	v0, tlbm_tlbp_and_tlbwi_normal
	dins	a3, zero, 0, 4

	li	a1, 14
	gslq	a3, a2, 0(a3)
2:
	HYPCALL
	gslq	a0, a1, 0(k1)
	gslq	a2, a3,16(k1)
	ld	v0, 32(k1)
	eret

3:
	sync			//for 3a2000/3a3000 sync here can remove
	lld	k0, 0(a1)
	andi	a2, k0, 0x2
	beqz	a2, 4f
	ori	a2, k0, 0x180c

	scd	a2, 0(a1)
	beqz	a2, 3b
	nop
	ori	a2, k0, 0x180c
	lui	a1, 24		// huge page need to pass page size

	b	2b
	li	v0, tlbm_tlbp_and_tlbwi_huge

4:
	sync
	gslq	a0, a1, 0(k1)
	gslq	a2, a3,16(k1)
	j	tlb_do_page_fault_1
	ld	v0, 32(k1)
	.set	reorder
	.set	pop
END(handle_tlbm)
EXPORT(handle_tlbm_end)

LEAF(handle_tlbl)
	.set	push
	.set	noreorder
	.set	arch=loongson3a

	lui	k1,%hi(handler_gprs_save)	// tmp_save should be per cpu
	mfc0	k0, CP0_EBASE
	daddiu	k1,%lo(handler_gprs_save)
	andi	k0, k0, 0xff

	sll	k0, k0, 6
	daddu	k1, k0, k1
	srl	k0, k0, 3
	gssq	a0, a1, 0(k1)

	gssq	a2, a3, 16(k1)
	sd	v0,  32(k1)
	dmfc0	a0, CP0_BADVADDR
	lui	a1, %hi(swapper_pg_dir)
	dsrl32	a3, a0, 16

	lui	a2, %hi(pgd_current)
	daddiu	k0, k0, %lo(pgd_current)
	gsldx	a2, 0(a2,k0)
	dsrl32	k0, a0, 1

	movz	a1, a2, a3	//a1 hold pgd
	andi	k0, k0, 0x7ff8	//12 bit, since pgd take up 2 16KB page;
	gsldx	a1, 0(a1, k0)
	dsrl	k0, a0, 22

	andi	k0, k0, 0x3ff8	//11 bit, pmd take 1 16KB page;
	gsldx	a1, 0(a1, k0)
	andi	k0, a1, 0x10
	bnez	k0, 3f	//check huge page
	dsrl	k0, a0, 11

	andi	k0, k0, 0x3ff8
	daddu	a3, k0, a1

1:
	sync
	lld	k0, 0(a3)
	andi	a2, k0, 0x1
	beqz	a2, 4f
	ori	a2, k0, 0x804

	scd	a2, 0(a3)
	beqz	a2, 1b
	nop
	li	v0, tlbl_tlbp_and_tlbwi_normal
	dins	a3, zero, 0, 4

	li	a1, 14		//setup a1 for hypercall
	gslq	a3, a2, 0(a3)	//setup a2, a3 for hypercall
2:
	HYPCALL
	gslq	a0, a1, 0(k1)
	gslq	a2, a3,16(k1)
	ld	v0, 32(k1)
	eret

3:
	sync			//for 3a2000/3a3000 sync here can remove
	lld	k0, 0(a1)
	andi	a2, k0, 0x1
	beqz	a2, 4f
	ori	a2, k0, 0x804

	scd	a2, 0(a1)
	beqz	a2, 3b
	move	a2, k0
	li	a1, 24

	b	2b
	li	v0, tlbl_tlbp_and_tlbwi_huge
4:
	sync
	gslq	a0, a1, 0(k1)
	gslq	a2, a3, 16(k1)
	j	tlb_do_page_fault_0  //do_page_fault with write = 0
	ld	v0, 32(k1)
	.set	reorder
	.set	pop
END(handle_tlbl)
EXPORT(handle_tlbl_end)

LEAF(handle_tlbs)
	.set	push
	.set	noreorder
	.set	arch=loongson3a

	lui	k1,%hi(handler_gprs_save)	// tmp_save should be per cpu
	mfc0	k0, CP0_EBASE
	daddiu	k1,%lo(handler_gprs_save)
	andi	k0, k0, 0xff

	sll	k0, k0, 6
	daddu	k1, k0, k1
	srl	k0, k0, 3
	gssq	a0, a1, 0(k1)

	gssq	a2, a3, 16(k1)
	sd	v0, 32(k1)
	dmfc0	a0, CP0_BADVADDR
	lui	a1, %hi(swapper_pg_dir)
	dsrl32	a3, a0, 16

	lui	a2, %hi(pgd_current)
	daddiu	k0, k0, %lo(pgd_current)
	gsldx	a2, 0(a2, k0)
	dsrl32	k0, a0, 1

	movz	a1, a2, a3	//a1 hold pgd
	andi	k0, k0, 0x7ff8	//12 bit, since pgd take up 2 16KB page;
	gsldx	a1, 0(a1, k0)
	dsrl	k0, a0, 22

	andi	k0, k0, 0x3ff8	//11 bit, pmd take 1 16KB page;
	gsldx	a1, 0(a1, k0)
	andi	k0, a1, 0x10
	bnez	k0, 3f	//check huge page
	dsrl	k0, a0, 11

	andi	k0, k0, 0x3ff8
	daddu	a3, k0, a1
1:
	sync
	lld	k0, 0(a3)
	andi	a2, k0, 0x3
	xori	a2, a2, 0x3
	bnez	a2, 4f
	ori	a2, k0, 0x180c
	scd	a2, 0(a3)
	beqz	a2, 1b
	nop
	li	v0, tlbs_tlbp_and_tlbwi_normal
	dins	a3, zero, 0, 4
	li	a1, 14
	gslq	a3, a2, 0(a3)	//setup a2, a3 for hypercall
2:
	HYPCALL
	gslq	a0, a1, 0(k1)
	gslq	a2, a3,16(k1)
	ld	v0, 32(k1)
	eret

3:
	sync			//for 3a2000/3a3000 sync here can remove
	lld	k0, 0(a1)
	andi	a2, k0, 0x3
	xori	a2, a2, 0x3
	bnez	a2, 4f
	ori	a2, k0, 0x180c

	scd	a2, 0(a1)
	beqz	a2, 3b
	ori	a2, k0, 0x180c
	li	a1, 24

	b	2b
	li	v0, tlbs_tlbp_and_tlbwi_huge

4:
	sync
	gslq	a0, a1, 0(k1)
	gslq	a2, a3,16(k1)
	j	tlb_do_page_fault_1  //do_page_fault with write = 1
	ld	v0, 32(k1)
	.set	reorder
	.set	pop
END(handle_tlbs)
EXPORT(handle_tlbs_end)

LEAF(tlb_miss)
	.set	push
	.set	noreorder
	.set	arch=loongson3a

	lui	k1,%hi(handler_gprs_save)	// tmp_save should be per cpu
	mfc0	k0, CP0_EBASE
	daddiu	k1,%lo(handler_gprs_save)
	andi	k0, k0, 0xff

	sll	k0, k0, 6
	daddu	k1, k0, k1
	srl	k0, k0, 3
	gssq	a0, a1, 0(k1)

	gssq	a2, a3, 16(k1)
	sd	v0, 32(k1)
	dmfc0	a0, CP0_BADVADDR
	lui	a1, %hi(swapper_pg_dir)
	dsrl32	a3, a0, 16

	lui	a2, %hi(pgd_current)
	daddiu	k0, k0, %lo(pgd_current)
	gsldx	a2, 0(a2, k0)
	dsrl32	k0, a0, 1

	movz	a1, a2, a3	//a1 hold pgd
	andi	k0, k0, 0x7ff8	//12 bit, since pgd take up 2 16KB page;
	gsldx	a1, 0(a1, k0)
	dsrl	k0, a0, 22

	andi	k0, k0, 0x3ff8	//11 bit, pmd take 1 16KB page;
	gsldx	a1, 0(a1, k0)
	andi	k0, a1, 0x10
	bnez	k0, 2f	//check huge page
	dsrl	k0, a0, 11

	andi	k0, k0, 0x3ff0
	daddu	a1, k0, a1
	gslq	a3, a2, 0(a1)
	li	v0, tlbmiss_tlbwr_normal
	li	a1, 14
1:
	HYPCALL
	gslq	a0, a1, 0(k1)
	gslq	a2, a3, 16(k1)
	ld	v0, 32(k1)
	eret

2:
	ld	a2, 0(a1)
	li	a1, 24
	b	1b
	li	v0, tlbmiss_tlbwr_huge
	.set	reorder
	.set	pop
END(tlb_miss)
EXPORT(handle_tlb_miss)

#else

EXPORT(tlbmiss_handler_setup_pgd_start)
LEAF(tlbmiss_handler_setup_pgd)
1:	j	1b		/* Dummy, will be replaced. */
	.space	64
END(tlbmiss_handler_setup_pgd)
EXPORT(tlbmiss_handler_setup_pgd_end)

LEAF(handle_tlbm)
	.space		FASTPATH_SIZE * 4
END(handle_tlbm)
EXPORT(handle_tlbm_end)

LEAF(handle_tlbs)
	.space		FASTPATH_SIZE * 4
END(handle_tlbs)
EXPORT(handle_tlbs_end)

LEAF(handle_tlbl)
	.space		FASTPATH_SIZE * 4
END(handle_tlbl)
EXPORT(handle_tlbl_end)
#endif
