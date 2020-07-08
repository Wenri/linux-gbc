/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 98, 99, 2000, 01 Ralf Baechle
 *
 * Multi-arch abstraction and asm macros for easier reading:
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 * Copyright (C) 1999, 2001 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <linux/compiler.h>

/* preprocessor replaces the fp in ".set fp=64" with $30 otherwise */
#undef fp

	.macro	EX insn, reg, src
	.set	push
	SET_HARDFLOAT
	.set	nomacro
.ex\@:	\insn	\reg, \src
	.set	pop
	.section __ex_table,"a"
	PTR	.ex\@, fault
	.previous
	.endm

	.set	noreorder

LEAF(_save_fp_context)
	.set	push
	SET_HARDFLOAT
	cfc1	t1, fcr31
	.set	pop

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	.set	push
	SET_HARDFLOAT
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips32r2
	.set	fp=64
	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip storing odd if FR=0
	 nop
#endif
	/* Store the 16 odd double precision registers */
	EX	sdc1 $f1, SC_FPREGS+8(a0)
	EX	sdc1 $f3, SC_FPREGS+24(a0)
	EX	sdc1 $f5, SC_FPREGS+40(a0)
	EX	sdc1 $f7, SC_FPREGS+56(a0)
	EX	sdc1 $f9, SC_FPREGS+72(a0)
	EX	sdc1 $f11, SC_FPREGS+88(a0)
	EX	sdc1 $f13, SC_FPREGS+104(a0)
	EX	sdc1 $f15, SC_FPREGS+120(a0)
	EX	sdc1 $f17, SC_FPREGS+136(a0)
	EX	sdc1 $f19, SC_FPREGS+152(a0)
	EX	sdc1 $f21, SC_FPREGS+168(a0)
	EX	sdc1 $f23, SC_FPREGS+184(a0)
	EX	sdc1 $f25, SC_FPREGS+200(a0)
	EX	sdc1 $f27, SC_FPREGS+216(a0)
	EX	sdc1 $f29, SC_FPREGS+232(a0)
	EX	sdc1 $f31, SC_FPREGS+248(a0)
1:	.set	pop
#endif

	.set push
	SET_HARDFLOAT
	/* Store the 16 even double precision registers */
	EX	sdc1 $f0, SC_FPREGS+0(a0)
	EX	sdc1 $f2, SC_FPREGS+16(a0)
	EX	sdc1 $f4, SC_FPREGS+32(a0)
	EX	sdc1 $f6, SC_FPREGS+48(a0)
	EX	sdc1 $f8, SC_FPREGS+64(a0)
	EX	sdc1 $f10, SC_FPREGS+80(a0)
	EX	sdc1 $f12, SC_FPREGS+96(a0)
	EX	sdc1 $f14, SC_FPREGS+112(a0)
	EX	sdc1 $f16, SC_FPREGS+128(a0)
	EX	sdc1 $f18, SC_FPREGS+144(a0)
	EX	sdc1 $f20, SC_FPREGS+160(a0)
	EX	sdc1 $f22, SC_FPREGS+176(a0)
	EX	sdc1 $f24, SC_FPREGS+192(a0)
	EX	sdc1 $f26, SC_FPREGS+208(a0)
	EX	sdc1 $f28, SC_FPREGS+224(a0)
	EX	sdc1 $f30, SC_FPREGS+240(a0)
	EX	sw t1, SC_FPC_CSR(a0)
	jr	ra
	 li	v0, 0					# success
	.set pop
	END(_save_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
	/* Save 32-bit process floating point context */
LEAF(_save_fp_context32)
	.set push
	.set mips64r2
	SET_HARDFLOAT
	cfc1	t1, fcr31

	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip storing odd if FR=0
	 nop

	/* Store the 16 odd double precision registers */
	EX      sdc1 $f1, SC32_FPREGS+8(a0)
	EX      sdc1 $f3, SC32_FPREGS+24(a0)
	EX      sdc1 $f5, SC32_FPREGS+40(a0)
	EX      sdc1 $f7, SC32_FPREGS+56(a0)
	EX      sdc1 $f9, SC32_FPREGS+72(a0)
	EX      sdc1 $f11, SC32_FPREGS+88(a0)
	EX      sdc1 $f13, SC32_FPREGS+104(a0)
	EX      sdc1 $f15, SC32_FPREGS+120(a0)
	EX      sdc1 $f17, SC32_FPREGS+136(a0)
	EX      sdc1 $f19, SC32_FPREGS+152(a0)
	EX      sdc1 $f21, SC32_FPREGS+168(a0)
	EX      sdc1 $f23, SC32_FPREGS+184(a0)
	EX      sdc1 $f25, SC32_FPREGS+200(a0)
	EX      sdc1 $f27, SC32_FPREGS+216(a0)
	EX      sdc1 $f29, SC32_FPREGS+232(a0)
	EX      sdc1 $f31, SC32_FPREGS+248(a0)

	/* Store the 16 even double precision registers */
1:	EX	sdc1 $f0, SC32_FPREGS+0(a0)
	EX	sdc1 $f2, SC32_FPREGS+16(a0)
	EX	sdc1 $f4, SC32_FPREGS+32(a0)
	EX	sdc1 $f6, SC32_FPREGS+48(a0)
	EX	sdc1 $f8, SC32_FPREGS+64(a0)
	EX	sdc1 $f10, SC32_FPREGS+80(a0)
	EX	sdc1 $f12, SC32_FPREGS+96(a0)
	EX	sdc1 $f14, SC32_FPREGS+112(a0)
	EX	sdc1 $f16, SC32_FPREGS+128(a0)
	EX	sdc1 $f18, SC32_FPREGS+144(a0)
	EX	sdc1 $f20, SC32_FPREGS+160(a0)
	EX	sdc1 $f22, SC32_FPREGS+176(a0)
	EX	sdc1 $f24, SC32_FPREGS+192(a0)
	EX	sdc1 $f26, SC32_FPREGS+208(a0)
	EX	sdc1 $f28, SC32_FPREGS+224(a0)
	EX	sdc1 $f30, SC32_FPREGS+240(a0)
	EX	sw t1, SC32_FPC_CSR(a0)
	cfc1	t0, $0				# implementation/version
	EX	sw t0, SC32_FPC_EIR(a0)
	.set pop

	jr	ra
	 li	v0, 0					# success
	END(_save_fp_context32)
#endif

/*
 * Restore FPU state:
 *  - fp gp registers
 *  - cp1 status/control register
 */
LEAF(_restore_fp_context)
	.set	push
	SET_HARDFLOAT
	EX	lw t0, SC_FPC_CSR(a0)

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	.set	push
	SET_HARDFLOAT
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips32r2
	.set	fp=64
	mfc0	t1, CP0_STATUS
	sll	t1, t1, 5
	bgez	t1, 1f			# skip loading odd if FR=0
	 nop
#endif
	EX	ldc1 $f1, SC_FPREGS+8(a0)
	EX	ldc1 $f3, SC_FPREGS+24(a0)
	EX	ldc1 $f5, SC_FPREGS+40(a0)
	EX	ldc1 $f7, SC_FPREGS+56(a0)
	EX	ldc1 $f9, SC_FPREGS+72(a0)
	EX	ldc1 $f11, SC_FPREGS+88(a0)
	EX	ldc1 $f13, SC_FPREGS+104(a0)
	EX	ldc1 $f15, SC_FPREGS+120(a0)
	EX	ldc1 $f17, SC_FPREGS+136(a0)
	EX	ldc1 $f19, SC_FPREGS+152(a0)
	EX	ldc1 $f21, SC_FPREGS+168(a0)
	EX	ldc1 $f23, SC_FPREGS+184(a0)
	EX	ldc1 $f25, SC_FPREGS+200(a0)
	EX	ldc1 $f27, SC_FPREGS+216(a0)
	EX	ldc1 $f29, SC_FPREGS+232(a0)
	EX	ldc1 $f31, SC_FPREGS+248(a0)
1:	.set pop
#endif
	.set push
	SET_HARDFLOAT
	EX	ldc1 $f0, SC_FPREGS+0(a0)
	EX	ldc1 $f2, SC_FPREGS+16(a0)
	EX	ldc1 $f4, SC_FPREGS+32(a0)
	EX	ldc1 $f6, SC_FPREGS+48(a0)
	EX	ldc1 $f8, SC_FPREGS+64(a0)
	EX	ldc1 $f10, SC_FPREGS+80(a0)
	EX	ldc1 $f12, SC_FPREGS+96(a0)
	EX	ldc1 $f14, SC_FPREGS+112(a0)
	EX	ldc1 $f16, SC_FPREGS+128(a0)
	EX	ldc1 $f18, SC_FPREGS+144(a0)
	EX	ldc1 $f20, SC_FPREGS+160(a0)
	EX	ldc1 $f22, SC_FPREGS+176(a0)
	EX	ldc1 $f24, SC_FPREGS+192(a0)
	EX	ldc1 $f26, SC_FPREGS+208(a0)
	EX	ldc1 $f28, SC_FPREGS+224(a0)
	EX	ldc1 $f30, SC_FPREGS+240(a0)
	ctc1	t0, fcr31
	.set pop
	jr	ra
	 li	v0, 0					# success
	END(_restore_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
LEAF(_restore_fp_context32)
	/* Restore an o32 sigcontext.  */
	.set push
	SET_HARDFLOAT
	EX	lw t0, SC32_FPC_CSR(a0)

	mfc0	t1, CP0_STATUS
	sll	t1, t1, 5
	bgez	t1, 1f			# skip loading odd if FR=0
	 nop

	EX      ldc1 $f1, SC32_FPREGS+8(a0)
	EX      ldc1 $f3, SC32_FPREGS+24(a0)
	EX      ldc1 $f5, SC32_FPREGS+40(a0)
	EX      ldc1 $f7, SC32_FPREGS+56(a0)
	EX      ldc1 $f9, SC32_FPREGS+72(a0)
	EX      ldc1 $f11, SC32_FPREGS+88(a0)
	EX      ldc1 $f13, SC32_FPREGS+104(a0)
	EX      ldc1 $f15, SC32_FPREGS+120(a0)
	EX      ldc1 $f17, SC32_FPREGS+136(a0)
	EX      ldc1 $f19, SC32_FPREGS+152(a0)
	EX      ldc1 $f21, SC32_FPREGS+168(a0)
	EX      ldc1 $f23, SC32_FPREGS+184(a0)
	EX      ldc1 $f25, SC32_FPREGS+200(a0)
	EX      ldc1 $f27, SC32_FPREGS+216(a0)
	EX      ldc1 $f29, SC32_FPREGS+232(a0)
	EX      ldc1 $f31, SC32_FPREGS+248(a0)

1:	EX	ldc1 $f0, SC32_FPREGS+0(a0)
	EX	ldc1 $f2, SC32_FPREGS+16(a0)
	EX	ldc1 $f4, SC32_FPREGS+32(a0)
	EX	ldc1 $f6, SC32_FPREGS+48(a0)
	EX	ldc1 $f8, SC32_FPREGS+64(a0)
	EX	ldc1 $f10, SC32_FPREGS+80(a0)
	EX	ldc1 $f12, SC32_FPREGS+96(a0)
	EX	ldc1 $f14, SC32_FPREGS+112(a0)
	EX	ldc1 $f16, SC32_FPREGS+128(a0)
	EX	ldc1 $f18, SC32_FPREGS+144(a0)
	EX	ldc1 $f20, SC32_FPREGS+160(a0)
	EX	ldc1 $f22, SC32_FPREGS+176(a0)
	EX	ldc1 $f24, SC32_FPREGS+192(a0)
	EX	ldc1 $f26, SC32_FPREGS+208(a0)
	EX	ldc1 $f28, SC32_FPREGS+224(a0)
	EX	ldc1 $f30, SC32_FPREGS+240(a0)
	ctc1	t0, fcr31
	jr	ra
	 li	v0, 0					# success
	.set pop
	END(_restore_fp_context32)
#endif

#ifdef CONFIG_CPU_HAS_MSA

	.macro	op_one_wr	op, idx, base
	.align	4
\idx:	\op	\idx, 0, \base
	jr	ra
	 nop
	.endm

	.macro	op_msa_wr	name, op
LEAF(\name)
	.set		push
	.set		noreorder
	sll		t0, a0, 4
	PTR_LA		t1, 0f
	PTR_ADDU	t0, t0, t1
	jr		t0
	  nop
	op_one_wr	\op, 0, a1
	op_one_wr	\op, 1, a1
	op_one_wr	\op, 2, a1
	op_one_wr	\op, 3, a1
	op_one_wr	\op, 4, a1
	op_one_wr	\op, 5, a1
	op_one_wr	\op, 6, a1
	op_one_wr	\op, 7, a1
	op_one_wr	\op, 8, a1
	op_one_wr	\op, 9, a1
	op_one_wr	\op, 10, a1
	op_one_wr	\op, 11, a1
	op_one_wr	\op, 12, a1
	op_one_wr	\op, 13, a1
	op_one_wr	\op, 14, a1
	op_one_wr	\op, 15, a1
	op_one_wr	\op, 16, a1
	op_one_wr	\op, 17, a1
	op_one_wr	\op, 18, a1
	op_one_wr	\op, 19, a1
	op_one_wr	\op, 20, a1
	op_one_wr	\op, 21, a1
	op_one_wr	\op, 22, a1
	op_one_wr	\op, 23, a1
	op_one_wr	\op, 24, a1
	op_one_wr	\op, 25, a1
	op_one_wr	\op, 26, a1
	op_one_wr	\op, 27, a1
	op_one_wr	\op, 28, a1
	op_one_wr	\op, 29, a1
	op_one_wr	\op, 30, a1
	op_one_wr	\op, 31, a1
	.set		pop
	END(\name)
	.endm

	op_msa_wr	read_msa_wr_b, st_b
	op_msa_wr	read_msa_wr_h, st_h
	op_msa_wr	read_msa_wr_w, st_w
	op_msa_wr	read_msa_wr_d, st_d

	op_msa_wr	write_msa_wr_b, ld_b
	op_msa_wr	write_msa_wr_h, ld_h
	op_msa_wr	write_msa_wr_w, ld_w
	op_msa_wr	write_msa_wr_d, ld_d

#endif /* CONFIG_CPU_HAS_MSA */

	.set	reorder

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT				# failure
	jr	ra
	.end	fault


/*
 * Save a thread's fp context.
 */
LEAF(_save_fp)
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	mfc0	t0, CP0_STATUS
#endif
	fpu_save_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_save_fp)

/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	mfc0	t0, CP0_STATUS
#endif
	fpu_restore_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_restore_fp)

#ifdef CONFIG_CPU_HAS_MSA

/*
 * Save a thread's MSA vector context.
 */
LEAF(_save_msa)
	msa_save_all	a0
	jr	ra
	END(_save_msa)

/*
 * Restore a thread's MSA vector context.
 */
LEAF(_restore_msa)
	msa_restore_all	a0
	jr	ra
	END(_restore_msa)

LEAF(_init_msa_upper)
	msa_init_all_upper
	jr	ra
	END(_init_msa_upper)

#endif

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * The value to initialize fcr31 to comes in $a0.
 */

	.set push
	SET_HARDFLOAT

LEAF(_init_fpu)
#ifdef CONFIG_MIPS_MT_SMTC
	/* Rather than manipulate per-VPE Status, set per-TC bit in TCStatus */
	mfc0	t0, CP0_TCSTATUS
	/* Bit position is the same for Status, TCStatus */
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_TCSTATUS
#else /* Normal MIPS CU1 enable */
	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_STATUS
#endif /* CONFIG_MIPS_MT_SMTC */
	enable_fpu_hazard

	ctc1	a0, fcr31

	li	t1, -1				# SNaN

#ifdef CONFIG_64BIT
	sll	t0, t0, 5
	bgez	t0, 1f				# 16 / 32 register mode?

	dmtc1	t1, $f1
	dmtc1	t1, $f3
	dmtc1	t1, $f5
	dmtc1	t1, $f7
	dmtc1	t1, $f9
	dmtc1	t1, $f11
	dmtc1	t1, $f13
	dmtc1	t1, $f15
	dmtc1	t1, $f17
	dmtc1	t1, $f19
	dmtc1	t1, $f21
	dmtc1	t1, $f23
	dmtc1	t1, $f25
	dmtc1	t1, $f27
	dmtc1	t1, $f29
	dmtc1	t1, $f31
1:
#endif

#ifdef CONFIG_CPU_MIPS32
	mtc1	t1, $f0
	mtc1	t1, $f1
	mtc1	t1, $f2
	mtc1	t1, $f3
	mtc1	t1, $f4
	mtc1	t1, $f5
	mtc1	t1, $f6
	mtc1	t1, $f7
	mtc1	t1, $f8
	mtc1	t1, $f9
	mtc1	t1, $f10
	mtc1	t1, $f11
	mtc1	t1, $f12
	mtc1	t1, $f13
	mtc1	t1, $f14
	mtc1	t1, $f15
	mtc1	t1, $f16
	mtc1	t1, $f17
	mtc1	t1, $f18
	mtc1	t1, $f19
	mtc1	t1, $f20
	mtc1	t1, $f21
	mtc1	t1, $f22
	mtc1	t1, $f23
	mtc1	t1, $f24
	mtc1	t1, $f25
	mtc1	t1, $f26
	mtc1	t1, $f27
	mtc1	t1, $f28
	mtc1	t1, $f29
	mtc1	t1, $f30
	mtc1	t1, $f31

#ifdef CONFIG_CPU_MIPS32_R2
	.set    push
	.set    mips32r2
	.set	fp=64
	sll     t0, t0, 5			# is Status.FR set?
	bgez    t0, 1f				# no: skip setting upper 32b

	mthc1   t1, $f0
	mthc1   t1, $f1
	mthc1   t1, $f2
	mthc1   t1, $f3
	mthc1   t1, $f4
	mthc1   t1, $f5
	mthc1   t1, $f6
	mthc1   t1, $f7
	mthc1   t1, $f8
	mthc1   t1, $f9
	mthc1   t1, $f10
	mthc1   t1, $f11
	mthc1   t1, $f12
	mthc1   t1, $f13
	mthc1   t1, $f14
	mthc1   t1, $f15
	mthc1   t1, $f16
	mthc1   t1, $f17
	mthc1   t1, $f18
	mthc1   t1, $f19
	mthc1   t1, $f20
	mthc1   t1, $f21
	mthc1   t1, $f22
	mthc1   t1, $f23
	mthc1   t1, $f24
	mthc1   t1, $f25
	mthc1   t1, $f26
	mthc1   t1, $f27
	mthc1   t1, $f28
	mthc1   t1, $f29
	mthc1   t1, $f30
	mthc1   t1, $f31
1:	.set    pop
#endif /* CONFIG_CPU_MIPS32_R2 */
#else
	.set	mips3
	dmtc1	t1, $f0
	dmtc1	t1, $f2
	dmtc1	t1, $f4
	dmtc1	t1, $f6
	dmtc1	t1, $f8
	dmtc1	t1, $f10
	dmtc1	t1, $f12
	dmtc1	t1, $f14
	dmtc1	t1, $f16
	dmtc1	t1, $f18
	dmtc1	t1, $f20
	dmtc1	t1, $f22
	dmtc1	t1, $f24
	dmtc1	t1, $f26
	dmtc1	t1, $f28
	dmtc1	t1, $f30
#endif
	jr	ra
	END(_init_fpu)

	.set pop	/* SET_HARDFLOAT */
