/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Huang Pei <huangpei@loongson.cn>
 */
#include <asm/asmmacro.h>
#include <asm/errno.h>

/* preprocessor replaces the fp in ".set fp=64" with $30 otherwise */

#undef fp

	.macro	EX2 insn, reg1, reg0, src
	.set	push
	.set	arch=loongson3a
	SET_HARDFLOAT
	.set	nomacro
.ex\@:	\insn	\reg1, \reg0, \src
	.set	pop
	.section __ex_table,"a"
	PTR	.ex\@, fault
	.previous
	.endm

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
	cfc1	t0, fcr31
	.set	pop
	EX	sw	t0, SC_FPC_CSR(a0)
	EX2	gssqc1	$f1, $f0, SC_FPREGS+0(a0)
	EX2	gssqc1	$f3, $f2, SC_FPREGS+16(a0)

	EX2	gssqc1	$f5, $f4, SC_FPREGS+32(a0)
	EX2	gssqc1	$f7, $f6, SC_FPREGS+48(a0)
	EX2	gssqc1	$f9, $f8, SC_FPREGS+64(a0)
	EX2	gssqc1	$f11, $f10, SC_FPREGS+80(a0)

	EX2	gssqc1	$f13, $f12, SC_FPREGS+96(a0)
	EX2	gssqc1	$f15, $f14, SC_FPREGS+112(a0)
	EX2	gssqc1	$f17, $f16, SC_FPREGS+128(a0)
	EX2	gssqc1	$f19, $f18, SC_FPREGS+144(a0)

	EX2	gssqc1	$f21, $f20, SC_FPREGS+160(a0)
	EX2	gssqc1	$f23, $f22, SC_FPREGS+176(a0)
	EX2	gssqc1	$f25, $f24, SC_FPREGS+192(a0)
	EX2	gssqc1	$f27, $f26, SC_FPREGS+208(a0)

	EX2	gssqc1	$f29, $f28, SC_FPREGS+224(a0)
	EX2	gssqc1	$f31, $f30, SC_FPREGS+240(a0)
	jr	ra
	li	v0, 0
END(_save_fp_context)

LEAF(_restore_fp_context)
	EX	lw t0, SC_FPC_CSR(a0)
	.set	push
	SET_HARDFLOAT
	ctc1	t0, fcr31
	.set	pop
	EX2	gslqc1	$f1, $f0, SC_FPREGS+0(a0)
	EX2	gslqc1	$f3, $f2, SC_FPREGS+16(a0)

	EX2	gslqc1	$f5, $f4, SC_FPREGS+32(a0)
	EX2	gslqc1	$f7, $f6, SC_FPREGS+48(a0)
	EX2	gslqc1	$f9, $f8, SC_FPREGS+64(a0)
	EX2	gslqc1	$f11, $f10, SC_FPREGS+80(a0)

	EX2	gslqc1	$f13, $f12, SC_FPREGS+96(a0)
	EX2	gslqc1	$f15, $f14, SC_FPREGS+112(a0)
	EX2	gslqc1	$f17, $f16, SC_FPREGS+128(a0)
	EX2	gslqc1	$f19, $f18, SC_FPREGS+144(a0)

	EX2	gslqc1	$f21, $f20, SC_FPREGS+160(a0)
	EX2	gslqc1	$f23, $f22, SC_FPREGS+176(a0)
	EX2	gslqc1	$f25, $f24, SC_FPREGS+192(a0)
	EX2	gslqc1	$f27, $f26, SC_FPREGS+208(a0)

	EX2	gslqc1	$f29, $f28, SC_FPREGS+224(a0)
	EX2	gslqc1	$f31, $f30, SC_FPREGS+240(a0)
	jr	ra
	li	v0, 0
END(_restore_fp_context)

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
	EX	sw t1, SC32_FPC_CSR(a0)

	/* Store the 16 odd double precision registers */
	EX2      gssqc1 $f1, $f0, SC32_FPREGS+0(a0)
	EX2      gssqc1 $f3, $f2, SC32_FPREGS+16(a0)
	EX2      gssqc1 $f5, $f4, SC32_FPREGS+32(a0)
	EX2      gssqc1 $f7, $f6, SC32_FPREGS+48(a0)
	EX2      gssqc1 $f9, $f8, SC32_FPREGS+64(a0)
	EX2      gssqc1 $f11, $f10, SC32_FPREGS+80(a0)
	EX2      gssqc1 $f13, $f12, SC32_FPREGS+96(a0)
	EX2      gssqc1 $f15, $f14, SC32_FPREGS+112(a0)
	EX2      gssqc1 $f17, $f16, SC32_FPREGS+128(a0)
	EX2      gssqc1 $f19, $f18, SC32_FPREGS+144(a0)
	EX2      gssqc1 $f21, $f20, SC32_FPREGS+160(a0)
	EX2      gssqc1 $f23, $f22, SC32_FPREGS+176(a0)
	EX2      gssqc1 $f25, $f24, SC32_FPREGS+192(a0)
	EX2      gssqc1 $f27, $f26, SC32_FPREGS+208(a0)
	EX2      gssqc1 $f29, $f28, SC32_FPREGS+224(a0)
	EX2      gssqc1 $f31, $f30, SC32_FPREGS+240(a0)

	jr	ra
	li	v0, 0					# success

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
	.set pop

	jr	ra
	li	v0, 0					# success
END(_save_fp_context32)

LEAF(_restore_fp_context32)
	/* Restore an o32 sigcontext.  */
	.set push
	SET_HARDFLOAT

	EX	lw t0, SC32_FPC_CSR(a0)

	mfc0	t1, CP0_STATUS
	sll	t1, t1, 5
	bgez	t1, 1f			# skip loading odd if FR=0
	ctc1	t0, fcr31

	EX2      gslqc1 $f1, $f0, SC32_FPREGS+0(a0)
	EX2      gslqc1 $f3, $f2, SC32_FPREGS+16(a0)
	EX2      gslqc1 $f5, $f4, SC32_FPREGS+32(a0)
	EX2      gslqc1 $f7, $f6, SC32_FPREGS+48(a0)
	EX2      gslqc1 $f9, $f8, SC32_FPREGS+64(a0)
	EX2      gslqc1 $f11, $f10, SC32_FPREGS+80(a0)
	EX2      gslqc1 $f13, $f12, SC32_FPREGS+96(a0)
	EX2      gslqc1 $f15, $f14, SC32_FPREGS+112(a0)
	EX2      gslqc1 $f17, $f16, SC32_FPREGS+128(a0)
	EX2      gslqc1 $f19, $f18, SC32_FPREGS+144(a0)
	EX2      gslqc1 $f21, $f20, SC32_FPREGS+160(a0)
	EX2      gslqc1 $f23, $f22, SC32_FPREGS+176(a0)
	EX2      gslqc1 $f25, $f24, SC32_FPREGS+192(a0)
	EX2      gslqc1 $f27, $f26, SC32_FPREGS+208(a0)
	EX2      gslqc1 $f29, $f28, SC32_FPREGS+224(a0)
	EX2      gslqc1 $f31, $f30, SC32_FPREGS+240(a0)
	jr	ra
	li	v0, 0					# success

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
	jr	ra
	li	v0, 0					# success
	.set pop
	END(_restore_fp_context32)
#endif
	.set	reorder

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT				# failure
	jr	ra
	.end	fault

LEAF(_save_fp)
	.set	push
	.set	noreorder
	.set	arch=loongson3a
	SET_HARDFLOAT

	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f
	cfc1	t0, fcr31
#ifndef CONFIG_CPU_HAS_MSA 	
	gssqc1	$f1, $f0, THREAD_FPR0(a0)
	gssqc1	$f3, $f2, THREAD_FPR2(a0)

	gssqc1	$f5, $f4, THREAD_FPR4(a0)
	gssqc1	$f7, $f6, THREAD_FPR6(a0)
	gssqc1	$f9, $f8, THREAD_FPR8(a0)
	gssqc1	$f11, $f10, THREAD_FPR10(a0)

	gssqc1	$f13, $f12, THREAD_FPR12(a0)
	gssqc1	$f15, $f14, THREAD_FPR14(a0)
	gssqc1	$f17, $f16, THREAD_FPR16(a0)
	gssqc1	$f19, $f18, THREAD_FPR18(a0)

	gssqc1	$f21, $f20, THREAD_FPR20(a0)
	gssqc1	$f23, $f22, THREAD_FPR22(a0)
	gssqc1	$f25, $f24, THREAD_FPR24(a0)
	gssqc1	$f27, $f26, THREAD_FPR26(a0)

	gssqc1	$f29, $f28, THREAD_FPR28(a0)
	gssqc1	$f31, $f30, THREAD_FPR30(a0)
	jr	ra
	sw	t0, THREAD_FCR31(a0)
#else
	sdc1	$f1,  THREAD_FPR1(a0)
	sdc1	$f3,  THREAD_FPR3(a0)
	sdc1	$f5,  THREAD_FPR5(a0)
	sdc1	$f7,  THREAD_FPR7(a0)
	sdc1	$f9,  THREAD_FPR9(a0)
	sdc1	$f11, THREAD_FPR11(a0)
	sdc1	$f13, THREAD_FPR13(a0)
	sdc1	$f15, THREAD_FPR15(a0)
	sdc1	$f17, THREAD_FPR17(a0)
	sdc1	$f19, THREAD_FPR19(a0)
	sdc1	$f21, THREAD_FPR21(a0)
	sdc1	$f23, THREAD_FPR23(a0)
	sdc1	$f25, THREAD_FPR25(a0)
	sdc1	$f27, THREAD_FPR27(a0)
	sdc1	$f29, THREAD_FPR29(a0)
	sdc1	$f31, THREAD_FPR31(a0)
#endif
1:
	sdc1	$f0, THREAD_FPR0(a0)
	sdc1	$f2, THREAD_FPR2(a0)
	sdc1	$f4, THREAD_FPR4(a0)
	sdc1	$f6, THREAD_FPR6(a0)

	sdc1	$f8, THREAD_FPR8(a0)
	sdc1	$f10, THREAD_FPR10(a0)
	sdc1	$f12, THREAD_FPR12(a0)
	sdc1	$f14, THREAD_FPR14(a0)

	sdc1	$f16, THREAD_FPR16(a0)
	sdc1	$f18, THREAD_FPR18(a0)
	sdc1	$f20, THREAD_FPR20(a0)
	sdc1	$f22, THREAD_FPR22(a0)

	sdc1	$f24, THREAD_FPR24(a0)
	sdc1	$f26, THREAD_FPR26(a0)
	sdc1	$f28, THREAD_FPR28(a0)
	sdc1	$f30, THREAD_FPR30(a0)

	jr	ra
	sw	t0, THREAD_FCR31(a0)
	.set	pop
END(_save_fp)
/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
	.set	push
	.set	noreorder
	SET_HARDFLOAT
	.set	arch=loongson3a

	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f
	lw	t0, THREAD_FCR31(a0)

#ifndef CONFIG_CPU_HAS_MSA 	
	gslqc1	$f1, $f0, THREAD_FPR0(a0)
	gslqc1	$f3, $f2, THREAD_FPR2(a0)

	gslqc1	$f5, $f4, THREAD_FPR4(a0)
	gslqc1	$f7, $f6, THREAD_FPR6(a0)
	gslqc1	$f9, $f8, THREAD_FPR8(a0)
	gslqc1	$f11, $f10, THREAD_FPR10(a0)

	gslqc1	$f13, $f12, THREAD_FPR12(a0)
	gslqc1	$f15, $f14, THREAD_FPR14(a0)
	gslqc1	$f17, $f16, THREAD_FPR16(a0)
	gslqc1	$f19, $f18, THREAD_FPR18(a0)

	gslqc1	$f21, $f20, THREAD_FPR20(a0)
	gslqc1	$f23, $f22, THREAD_FPR22(a0)
	gslqc1	$f25, $f24, THREAD_FPR24(a0)
	gslqc1	$f27, $f26, THREAD_FPR26(a0)

	gslqc1	$f29, $f28, THREAD_FPR28(a0)
	gslqc1	$f31, $f30, THREAD_FPR30(a0)
	jr	ra
	ctc1	t0, fcr31
#else
	ldc1	$f1,  THREAD_FPR1(a0)
	ldc1	$f3,  THREAD_FPR3(a0)
	ldc1	$f5,  THREAD_FPR5(a0)
	ldc1	$f7,  THREAD_FPR7(a0)
	ldc1	$f9,  THREAD_FPR9(a0)
	ldc1	$f11, THREAD_FPR11(a0)
	ldc1	$f13, THREAD_FPR13(a0)
	ldc1	$f15, THREAD_FPR15(a0)
	ldc1	$f17, THREAD_FPR17(a0)
	ldc1	$f19, THREAD_FPR19(a0)
	ldc1	$f21, THREAD_FPR21(a0)
	ldc1	$f23, THREAD_FPR23(a0)
	ldc1	$f25, THREAD_FPR25(a0)
	ldc1	$f27, THREAD_FPR27(a0)
	ldc1	$f29, THREAD_FPR29(a0)
	ldc1	$f31, THREAD_FPR31(a0)
#endif

1:
	ldc1	$f0, THREAD_FPR0(a0)
	ldc1	$f2, THREAD_FPR2(a0)
	ldc1	$f4, THREAD_FPR4(a0)
	ldc1	$f6, THREAD_FPR6(a0)

	ldc1	$f8, THREAD_FPR8(a0)
	ldc1	$f10, THREAD_FPR10(a0)
	ldc1	$f12, THREAD_FPR12(a0)
	ldc1	$f14, THREAD_FPR14(a0)

	ldc1	$f16, THREAD_FPR16(a0)
	ldc1	$f18, THREAD_FPR18(a0)
	ldc1	$f20, THREAD_FPR20(a0)
	ldc1	$f22, THREAD_FPR22(a0)

	ldc1	$f24, THREAD_FPR24(a0)
	ldc1	$f26, THREAD_FPR26(a0)
	ldc1	$f28, THREAD_FPR28(a0)
	ldc1	$f30, THREAD_FPR30(a0)

	jr	ra
	ctc1	t0, fcr31

	.set	pop
END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * The value to initialize fcr31 to comes in $a0.
 */

LEAF(_init_fpu)
	.set push
	SET_HARDFLOAT
	.set	noreorder

	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t0, t1
	mtc0	t0, CP0_STATUS

	li	t1, -1				# SNaN
	ctc1	a0, fcr31
	sll	t0, t0, 5
	bgez	t0, 1f				# 16 / 32 register mode?
	dmtc1	t1, $f0

	mov.d	$f1, $f0
	mov.d	$f3, $f0
	mov.d	$f5, $f0
	mov.d	$f7, $f0

	mov.d	$f9, $f0
	mov.d	$f11, $f0
	mov.d	$f13, $f0
	mov.d	$f15, $f0

	mov.d	$f17, $f0
	mov.d	$f19, $f0
	mov.d	$f21, $f0
	mov.d	$f23, $f0

	mov.d	$f25, $f0
	mov.d	$f27, $f0
	mov.d	$f29, $f0
	mov.d	$f31, $f0

1:
	mov.d	$f2, $f0
	mov.d	$f4, $f0
	mov.d	$f6, $f0
	mov.d	$f8, $f0

	mov.d	$f10, $f0
	mov.d	$f12, $f0
	mov.d	$f14, $f0
	mov.d	$f16, $f0

	mov.d	$f18, $f0
	mov.d	$f20, $f0
	mov.d	$f22, $f0
	mov.d	$f24, $f0

	mov.d	$f26, $f0
	mov.d	$f28, $f0
	jr	ra
	mov.d	$f30, $f0

	.set pop	/* SET_HARDFLOAT */
END(_init_fpu)

#ifdef CONFIG_CPU_HAS_MSA
	.macro	cfmsa1
	.word	0x787e0059 | (1 << 11)
	.endm

	.macro	ctmsa1
	.word	0x783e0819 | (1 << 6)
	.endm

	.macro	st_b_m	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	PTR_ADDU $1, \base, \off
	.word	0x78000824 | (\wd << 6)
	.set	pop
	.endm

	.macro	ld_b_m	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	PTR_ADDU $1, \base, \off
	.word	0x78000820 | (\wd << 6)
	.set	pop
	.endm

	.macro	st_b_off	wd, off
	.set	push
	.set	noat
	.word	0x78000824 | (\wd << 6) | (\off << 16)
	.set	pop
	.endm

	.macro	ld_b_off	wd, off
	.set	push
	.set	noat
	.word	0x78000820 | (\wd << 6) | (\off << 16)
	.set	pop
	.endm

	.macro	vld_b	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	PTR_ADDU $1, \base, \off
	.word	0x78000820 | (\wd << 6)
	.set	pop
	.endm

	.macro	vst_b	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	PTR_ADDU $1, \base, \off
	.word	0x78000824 | (\wd << 6)
	.set	pop
	.endm

	.macro	vsel_d		patt, ws, wd
	.set	push
	.set	noat
	SET_HARDFLOAT
	.word	0xef000003 | (\wd << 6) | (\ws << 11) | (\patt << 16)
	.set	pop
	.endm


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
	.set		msa
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

/*
 * Save a thread's MSA vector context.
 */
LEAF(_save_msa)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	/* when LASX enabled, the s10 offset in st_b/ld_b is not enough
	 * for saving/restoring all the vector regs with a0 pointing to
	 * start of the fpu_union.
	 * let a0 point to the middle of fpu_union
	 */
#ifdef CONFIG_CPU_HAS_LASX
	vst_b	16, THREAD_FPR16, a0
	st_b_off	17, (THREAD_FPR1-THREAD_FPR0)
	st_b_off	18, (THREAD_FPR2-THREAD_FPR0)
	st_b_off	19, (THREAD_FPR3-THREAD_FPR0)
	st_b_off	20, (THREAD_FPR4-THREAD_FPR0)
	st_b_off	21, (THREAD_FPR5-THREAD_FPR0)
	st_b_off	22, (THREAD_FPR6-THREAD_FPR0)
	st_b_off	23, (THREAD_FPR7-THREAD_FPR0)
	st_b_off	24, (THREAD_FPR8-THREAD_FPR0)
	st_b_off	25, (THREAD_FPR9-THREAD_FPR0)
	st_b_off	26, (THREAD_FPR10-THREAD_FPR0)
	st_b_off	27, (THREAD_FPR11-THREAD_FPR0)
	st_b_off	28, (THREAD_FPR12-THREAD_FPR0)
	st_b_off	29, (THREAD_FPR13-THREAD_FPR0)
	st_b_off	30, (THREAD_FPR14-THREAD_FPR0)
	st_b_off	31, (THREAD_FPR15-THREAD_FPR0)
	st_b_off	0, (THREAD_FPR16-THREAD_FPR0)
	st_b_off	1, (THREAD_FPR17-THREAD_FPR0)
	st_b_off	2, (THREAD_FPR18-THREAD_FPR0)
	st_b_off	3, (THREAD_FPR19-THREAD_FPR0)
	st_b_off	4, (THREAD_FPR20-THREAD_FPR0)
	st_b_off	5, (THREAD_FPR21-THREAD_FPR0)
	st_b_off	6, (THREAD_FPR22-THREAD_FPR0)
	st_b_off	7, (THREAD_FPR23-THREAD_FPR0)
	st_b_off	8, (THREAD_FPR24-THREAD_FPR0)
	st_b_off	9, (THREAD_FPR25-THREAD_FPR0)
	st_b_off	10, (THREAD_FPR26-THREAD_FPR0)
	st_b_off	11, (THREAD_FPR27-THREAD_FPR0)
	st_b_off	12, (THREAD_FPR28-THREAD_FPR0)
	st_b_off	13, (THREAD_FPR29-THREAD_FPR0)
	st_b_off	14, (THREAD_FPR30-THREAD_FPR0)
	st_b_off	15, (THREAD_FPR31-THREAD_FPR0)
#else
	st_b_m		0, THREAD_FPR0, a0
	st_b_off	1, (THREAD_FPR1-THREAD_FPR0)
	st_b_off	2, (THREAD_FPR2-THREAD_FPR0)
	st_b_off	3, (THREAD_FPR3-THREAD_FPR0)
	st_b_off	4, (THREAD_FPR4-THREAD_FPR0)
	st_b_off	5, (THREAD_FPR5-THREAD_FPR0)
	st_b_off	6, (THREAD_FPR6-THREAD_FPR0)
	st_b_off	7, (THREAD_FPR7-THREAD_FPR0)
	st_b_off	8, (THREAD_FPR8-THREAD_FPR0)
	st_b_off	9, (THREAD_FPR9-THREAD_FPR0)
	st_b_off	10, (THREAD_FPR10-THREAD_FPR0)
	st_b_off	11, (THREAD_FPR11-THREAD_FPR0)
	st_b_off	12, (THREAD_FPR12-THREAD_FPR0)
	st_b_off	13, (THREAD_FPR13-THREAD_FPR0)
	st_b_off	14, (THREAD_FPR14-THREAD_FPR0)
	st_b_off	15, (THREAD_FPR15-THREAD_FPR0)
	st_b_off	16, (THREAD_FPR16-THREAD_FPR0)
	st_b_off	17, (THREAD_FPR17-THREAD_FPR0)
	st_b_off	18, (THREAD_FPR18-THREAD_FPR0)
	st_b_off	19, (THREAD_FPR19-THREAD_FPR0)
	st_b_off	20, (THREAD_FPR20-THREAD_FPR0)
	st_b_off	21, (THREAD_FPR21-THREAD_FPR0)
	st_b_off	22, (THREAD_FPR22-THREAD_FPR0)
	st_b_off	23, (THREAD_FPR23-THREAD_FPR0)
	st_b_off	24, (THREAD_FPR24-THREAD_FPR0)
	st_b_off	25, (THREAD_FPR25-THREAD_FPR0)
	st_b_off	26, (THREAD_FPR26-THREAD_FPR0)
	st_b_off	27, (THREAD_FPR27-THREAD_FPR0)
	st_b_off	28, (THREAD_FPR28-THREAD_FPR0)
	st_b_off	29, (THREAD_FPR29-THREAD_FPR0)
	st_b_off	30, (THREAD_FPR30-THREAD_FPR0)
	st_b_off	31, (THREAD_FPR31-THREAD_FPR0)
#endif
	SET_HARDFLOAT
	cfmsa1
	jr	ra
	sw	$1, THREAD_MSA_CSR(a0)
	.set	pop
END(_save_msa)

/*
 * Restore a thread's MSA vector context.
 */
LEAF(_restore_msa)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	SET_HARDFLOAT
	lw	$1, THREAD_MSA_CSR(a0)
	ctmsa1
#ifdef CONFIG_CPU_HAS_LASX
	vld_b	16, THREAD_FPR16, a0
	ld_b_off	17, (THREAD_FPR1-THREAD_FPR0)
	ld_b_off	18, (THREAD_FPR2-THREAD_FPR0)
	ld_b_off	19, (THREAD_FPR3-THREAD_FPR0)
	ld_b_off	20, (THREAD_FPR4-THREAD_FPR0)
	ld_b_off	21, (THREAD_FPR5-THREAD_FPR0)
	ld_b_off	22, (THREAD_FPR6-THREAD_FPR0)
	ld_b_off	23, (THREAD_FPR7-THREAD_FPR0)
	ld_b_off	24, (THREAD_FPR8-THREAD_FPR0)
	ld_b_off	25, (THREAD_FPR9-THREAD_FPR0)
	ld_b_off	26, (THREAD_FPR10-THREAD_FPR0)
	ld_b_off	27, (THREAD_FPR11-THREAD_FPR0)
	ld_b_off	28, (THREAD_FPR12-THREAD_FPR0)
	ld_b_off	29, (THREAD_FPR13-THREAD_FPR0)
	ld_b_off	30, (THREAD_FPR14-THREAD_FPR0)
	ld_b_off	31, (THREAD_FPR15-THREAD_FPR0)
	ld_b_off	0, (THREAD_FPR16-THREAD_FPR0)
	ld_b_off	1, (THREAD_FPR17-THREAD_FPR0)
	ld_b_off	2, (THREAD_FPR18-THREAD_FPR0)
	ld_b_off	3, (THREAD_FPR19-THREAD_FPR0)
	ld_b_off	4, (THREAD_FPR20-THREAD_FPR0)
	ld_b_off	5, (THREAD_FPR21-THREAD_FPR0)
	ld_b_off	6, (THREAD_FPR22-THREAD_FPR0)
	ld_b_off	7, (THREAD_FPR23-THREAD_FPR0)
	ld_b_off	8, (THREAD_FPR24-THREAD_FPR0)
	ld_b_off	9, (THREAD_FPR25-THREAD_FPR0)
	ld_b_off	10, (THREAD_FPR26-THREAD_FPR0)
	ld_b_off	11, (THREAD_FPR27-THREAD_FPR0)
	ld_b_off	12, (THREAD_FPR28-THREAD_FPR0)
	ld_b_off	13, (THREAD_FPR29-THREAD_FPR0)
	ld_b_off	14, (THREAD_FPR30-THREAD_FPR0)
	jr	ra
	ld_b_off	15, (THREAD_FPR31-THREAD_FPR0)
#else
	ld_b_m		0, THREAD_FPR0, a0
	ld_b_off	1, (THREAD_FPR1-THREAD_FPR0)
	ld_b_off	2, (THREAD_FPR2-THREAD_FPR0)
	ld_b_off	3, (THREAD_FPR3-THREAD_FPR0)
	ld_b_off	4, (THREAD_FPR4-THREAD_FPR0)
	ld_b_off	5, (THREAD_FPR5-THREAD_FPR0)
	ld_b_off	6, (THREAD_FPR6-THREAD_FPR0)
	ld_b_off	7, (THREAD_FPR7-THREAD_FPR0)
	ld_b_off	8, (THREAD_FPR8-THREAD_FPR0)
	ld_b_off	9, (THREAD_FPR9-THREAD_FPR0)
	ld_b_off	10, (THREAD_FPR10-THREAD_FPR0)
	ld_b_off	11, (THREAD_FPR11-THREAD_FPR0)
	ld_b_off	12, (THREAD_FPR12-THREAD_FPR0)
	ld_b_off	13, (THREAD_FPR13-THREAD_FPR0)
	ld_b_off	14, (THREAD_FPR14-THREAD_FPR0)
	ld_b_off	15, (THREAD_FPR15-THREAD_FPR0)
	ld_b_off	16, (THREAD_FPR16-THREAD_FPR0)
	ld_b_off	17, (THREAD_FPR17-THREAD_FPR0)
	ld_b_off	18, (THREAD_FPR18-THREAD_FPR0)
	ld_b_off	19, (THREAD_FPR19-THREAD_FPR0)
	ld_b_off	20, (THREAD_FPR20-THREAD_FPR0)
	ld_b_off	21, (THREAD_FPR21-THREAD_FPR0)
	ld_b_off	22, (THREAD_FPR22-THREAD_FPR0)
	ld_b_off	23, (THREAD_FPR23-THREAD_FPR0)
	ld_b_off	24, (THREAD_FPR24-THREAD_FPR0)
	ld_b_off	25, (THREAD_FPR25-THREAD_FPR0)
	ld_b_off	26, (THREAD_FPR26-THREAD_FPR0)
	ld_b_off	27, (THREAD_FPR27-THREAD_FPR0)
	ld_b_off	28, (THREAD_FPR28-THREAD_FPR0)
	ld_b_off	29, (THREAD_FPR29-THREAD_FPR0)
	ld_b_off	30, (THREAD_FPR30-THREAD_FPR0)
	jr	ra
	ld_b_off	31, (THREAD_FPR31-THREAD_FPR0)
#endif
	.set	pop
END(_restore_msa)

LEAF(_init_msa_upper)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	not	$1, zero
	insert_d 0, 1
	vsel_d	0x2, 0, 1
	vsel_d	0x2, 0, 2
	vsel_d	0x2, 0, 3
	vsel_d	0x2, 0, 4
	vsel_d	0x2, 0, 5
	vsel_d	0x2, 0, 6
	vsel_d	0x2, 0, 7
	vsel_d	0x2, 0, 8
	vsel_d	0x2, 0, 9
	vsel_d	0x2, 0, 10
	vsel_d	0x2, 0, 11
	vsel_d	0x2, 0, 12
	vsel_d	0x2, 0, 13
	vsel_d	0x2, 0, 14
	vsel_d	0x2, 0, 15
	vsel_d	0x2, 0, 16
	vsel_d	0x2, 0, 17
	vsel_d	0x2, 0, 18
	vsel_d	0x2, 0, 19
	vsel_d	0x2, 0, 20
	vsel_d	0x2, 0, 21
	vsel_d	0x2, 0, 22
	vsel_d	0x2, 0, 23
	vsel_d	0x2, 0, 24
	vsel_d	0x2, 0, 25
	vsel_d	0x2, 0, 26
	vsel_d	0x2, 0, 27
	vsel_d	0x2, 0, 28
	vsel_d	0x2, 0, 29
	vsel_d	0x2, 0, 30
	jr	ra
	vsel_d	0x2, 0, 31
	.set	pop
END(_init_msa_upper)

	.macro	lasx_init_upper	wd
	.set	push
	.set	noat
	xinsert_d \wd, 2
	xinsert_d \wd, 3
	.endm

	.macro	lasx_init_uppest	wd
	.set	push
	.set	noat
	insert_d \wd, 1
	xinsert_d \wd, 2
	xinsert_d \wd, 3
	.endm

/*
 * Save a thread's LASX vector context.
 */
LEAF(_save_lasx)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	SET_HARDFLOAT
	cfmsa1
	sw	$1, THREAD_MSA_CSR(a0)
	xvst_b	16, THREAD_FPR16, a0
	xvst_b_off	17, (THREAD_FPR1-THREAD_FPR0)
	xvst_b_off	18, (THREAD_FPR2-THREAD_FPR0)
	xvst_b_off	19, (THREAD_FPR3-THREAD_FPR0)
	xvst_b_off	20, (THREAD_FPR4-THREAD_FPR0)
	xvst_b_off	21, (THREAD_FPR5-THREAD_FPR0)
	xvst_b_off	22, (THREAD_FPR6-THREAD_FPR0)
	xvst_b_off	23, (THREAD_FPR7-THREAD_FPR0)
	xvst_b_off	24, (THREAD_FPR8-THREAD_FPR0)
	xvst_b_off	25, (THREAD_FPR9-THREAD_FPR0)
	xvst_b_off	26, (THREAD_FPR10-THREAD_FPR0)
	xvst_b_off	27, (THREAD_FPR11-THREAD_FPR0)
	xvst_b_off	28, (THREAD_FPR12-THREAD_FPR0)
	xvst_b_off	29, (THREAD_FPR13-THREAD_FPR0)
	xvst_b_off	30, (THREAD_FPR14-THREAD_FPR0)
	xvst_b_off	31, (THREAD_FPR15-THREAD_FPR0)
	xvst_b_off	0, (THREAD_FPR16-THREAD_FPR0)
	xvst_b_off	1, (THREAD_FPR17-THREAD_FPR0)
	xvst_b_off	2, (THREAD_FPR18-THREAD_FPR0)
	xvst_b_off	3, (THREAD_FPR19-THREAD_FPR0)
	xvst_b_off	4, (THREAD_FPR20-THREAD_FPR0)
	xvst_b_off	5, (THREAD_FPR21-THREAD_FPR0)
	xvst_b_off	6, (THREAD_FPR22-THREAD_FPR0)
	xvst_b_off	7, (THREAD_FPR23-THREAD_FPR0)
	xvst_b_off	8, (THREAD_FPR24-THREAD_FPR0)
	xvst_b_off	9, (THREAD_FPR25-THREAD_FPR0)
	xvst_b_off	10, (THREAD_FPR26-THREAD_FPR0)
	xvst_b_off	11, (THREAD_FPR27-THREAD_FPR0)
	xvst_b_off	12, (THREAD_FPR28-THREAD_FPR0)
	xvst_b_off	13, (THREAD_FPR29-THREAD_FPR0)
	xvst_b_off	14, (THREAD_FPR30-THREAD_FPR0)
	jr	ra
	xvst_b_off	15, (THREAD_FPR31-THREAD_FPR0)
END(_save_lasx)

/*
 * Restore a thread's LASX vector context.
 */
LEAF(_restore_lasx)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	SET_HARDFLOAT
	lw	$1, THREAD_MSA_CSR(a0)
	ctmsa1
	.set	pop
	xvld_b	16, THREAD_FPR16, a0
	xvld_b_off	17, (THREAD_FPR1-THREAD_FPR0)
	xvld_b_off	18, (THREAD_FPR2-THREAD_FPR0)
	xvld_b_off	19, (THREAD_FPR3-THREAD_FPR0)
	xvld_b_off	20, (THREAD_FPR4-THREAD_FPR0)
	xvld_b_off	21, (THREAD_FPR5-THREAD_FPR0)
	xvld_b_off	22, (THREAD_FPR6-THREAD_FPR0)
	xvld_b_off	23, (THREAD_FPR7-THREAD_FPR0)
	xvld_b_off	24, (THREAD_FPR8-THREAD_FPR0)
	xvld_b_off	25, (THREAD_FPR9-THREAD_FPR0)
	xvld_b_off	26, (THREAD_FPR10-THREAD_FPR0)
	xvld_b_off	27, (THREAD_FPR11-THREAD_FPR0)
	xvld_b_off	28, (THREAD_FPR12-THREAD_FPR0)
	xvld_b_off	29, (THREAD_FPR13-THREAD_FPR0)
	xvld_b_off	30, (THREAD_FPR14-THREAD_FPR0)
	xvld_b_off	31, (THREAD_FPR15-THREAD_FPR0)
	xvld_b_off	0, (THREAD_FPR16-THREAD_FPR0)
	xvld_b_off	1, (THREAD_FPR17-THREAD_FPR0)
	xvld_b_off	2, (THREAD_FPR18-THREAD_FPR0)
	xvld_b_off	3, (THREAD_FPR19-THREAD_FPR0)
	xvld_b_off	4, (THREAD_FPR20-THREAD_FPR0)
	xvld_b_off	5, (THREAD_FPR21-THREAD_FPR0)
	xvld_b_off	6, (THREAD_FPR22-THREAD_FPR0)
	xvld_b_off	7, (THREAD_FPR23-THREAD_FPR0)
	xvld_b_off	8, (THREAD_FPR24-THREAD_FPR0)
	xvld_b_off	9, (THREAD_FPR25-THREAD_FPR0)
	xvld_b_off	10, (THREAD_FPR26-THREAD_FPR0)
	xvld_b_off	11, (THREAD_FPR27-THREAD_FPR0)
	xvld_b_off	12, (THREAD_FPR28-THREAD_FPR0)
	xvld_b_off	13, (THREAD_FPR29-THREAD_FPR0)
	xvld_b_off	14, (THREAD_FPR30-THREAD_FPR0)
	jr	ra
	xvld_b_off	15, (THREAD_FPR31-THREAD_FPR0)
END(_restore_lasx)

LEAF(_init_lasx_upper)
	.set	push
	.set	noat
	.set	noreorder
	SET_HARDFLOAT
	not	$1, zero
	lasx_init_upper	0
	xvseli_d 0xc, 0, 1
	xvseli_d 0xc, 0, 2
	xvseli_d 0xc, 0, 3
	xvseli_d 0xc, 0, 4
	xvseli_d 0xc, 0, 5
	xvseli_d 0xc, 0, 6
	xvseli_d 0xc, 0, 7
	xvseli_d 0xc, 0, 8
	xvseli_d 0xc, 0, 9
	xvseli_d 0xc, 0, 10
	xvseli_d 0xc, 0, 11
	xvseli_d 0xc, 0, 12
	xvseli_d 0xc, 0, 13
	xvseli_d 0xc, 0, 14
	xvseli_d 0xc, 0, 15
	xvseli_d 0xc, 0, 16
	xvseli_d 0xc, 0, 17
	xvseli_d 0xc, 0, 18
	xvseli_d 0xc, 0, 19
	xvseli_d 0xc, 0, 20
	xvseli_d 0xc, 0, 21
	xvseli_d 0xc, 0, 22
	xvseli_d 0xc, 0, 23
	xvseli_d 0xc, 0, 24
	xvseli_d 0xc, 0, 25
	xvseli_d 0xc, 0, 26
	xvseli_d 0xc, 0, 27
	xvseli_d 0xc, 0, 28
	xvseli_d 0xc, 0, 29
	xvseli_d 0xc, 0, 30
	jr	ra
	xvseli_d 0xc, 0, 31
	.set	pop
END(_init_lasx_upper)

LEAF(_init_lasx_uppest)
	.set	push
	.set	noat
	.set	noreorder
	.set	msa
	SET_HARDFLOAT
	not	$1, zero
	lasx_init_uppest	0
	xvseli_d 0xe, 0, 1
	xvseli_d 0xe, 0, 2
	xvseli_d 0xe, 0, 3
	xvseli_d 0xe, 0, 4
	xvseli_d 0xe, 0, 5
	xvseli_d 0xe, 0, 6
	xvseli_d 0xe, 0, 7
	xvseli_d 0xe, 0, 8
	xvseli_d 0xe, 0, 9
	xvseli_d 0xe, 0, 10
	xvseli_d 0xe, 0, 11
	xvseli_d 0xe, 0, 12
	xvseli_d 0xe, 0, 13
	xvseli_d 0xe, 0, 14
	xvseli_d 0xe, 0, 15
	xvseli_d 0xe, 0, 16
	xvseli_d 0xe, 0, 17
	xvseli_d 0xe, 0, 18
	xvseli_d 0xe, 0, 19
	xvseli_d 0xe, 0, 20
	xvseli_d 0xe, 0, 21
	xvseli_d 0xe, 0, 22
	xvseli_d 0xe, 0, 23
	xvseli_d 0xe, 0, 24
	xvseli_d 0xe, 0, 25
	xvseli_d 0xe, 0, 26
	xvseli_d 0xe, 0, 27
	xvseli_d 0xe, 0, 28
	xvseli_d 0xe, 0, 29
	xvseli_d 0xe, 0, 30
	jr	ra
	xvseli_d 0xe, 0, 31
	.set	pop
END(_init_lasx_uppest)
#endif
