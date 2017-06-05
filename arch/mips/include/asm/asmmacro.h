/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Ralf Baechle
 */
#ifndef _ASM_ASMMACRO_H
#define _ASM_ASMMACRO_H

#include <asm/hazards.h>

#ifdef CONFIG_32BIT
#include <asm/asmmacro-32.h>
#endif
#ifdef CONFIG_64BIT
#include <asm/asmmacro-64.h>
#endif
#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#endif

#ifdef CONFIG_MIPS_MT_SMTC
	.macro	local_irq_enable reg=t0
	mfc0	\reg, CP0_TCSTATUS
	ori	\reg, \reg, TCSTATUS_IXMT
	xori	\reg, \reg, TCSTATUS_IXMT
	mtc0	\reg, CP0_TCSTATUS
	_ehb
	.endm

	.macro	local_irq_disable reg=t0
	mfc0	\reg, CP0_TCSTATUS
	ori	\reg, \reg, TCSTATUS_IXMT
	mtc0	\reg, CP0_TCSTATUS
	_ehb
	.endm
#elif defined(CONFIG_CPU_MIPSR2)
	.macro	local_irq_enable reg=t0
	ei
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
	di
	irq_disable_hazard
	.endm
#else
	.macro	local_irq_enable reg=t0
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	xori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
	irq_disable_hazard
	.endm
#endif /* CONFIG_MIPS_MT_SMTC */

/*
 * Temporary until all gas have MT ASE support
 */
	.macro	DMT	reg=0
	.word	0x41600bc1 | (\reg << 16)
	.endm

	.macro	EMT	reg=0
	.word	0x41600be1 | (\reg << 16)
	.endm

	.macro	DVPE	reg=0
	.word	0x41600001 | (\reg << 16)
	.endm

	.macro	EVPE	reg=0
	.word	0x41600021 | (\reg << 16)
	.endm

	.macro	MFTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41000000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

	.macro	MTTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41800000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

#ifdef TOOLCHAIN_SUPPORTS_MSA
	.macro	ld_d	wd, off, base
	.set	push
	.set	mips32r2
	.set	msa
	ld.d	$w\wd, \off(\base)
	.set	pop
	.endm

	.macro	st_d	wd, off, base
	.set	push
	.set	mips32r2
	.set	msa
	st.d	$w\wd, \off(\base)
	.set	pop
	.endm

	.macro	copy_u_w	rd, ws, n
	.set	push
	.set	mips32r2
	.set	msa
	copy_u.w \rd, $w\ws[\n]
	.set	pop
	.endm

	.macro	copy_u_d	rd, ws, n
	.set	push
	.set	mips64r2
	.set	msa
	copy_u.d \rd, $w\ws[\n]
	.set	pop
	.endm

	.macro	insert_w	wd, n, rs
	.set	push
	.set	mips32r2
	.set	msa
	insert.w $w\wd[\n], \rs
	.set	pop
	.endm

	.macro	insert_d	wd, n, rs
	.set	push
	.set	mips64r2
	.set	msa
	insert.d $w\wd[\n], \rs
	.set	pop
	.endm
#else
	/*
	 * Temporary until all toolchains in use include MSA support.
	 */
	.macro	cfcmsa	rd, cs
	.set	push
	.set	noat
	.word	0x787e0059 | (\cs << 11)
	move	\rd, $1
	.set	pop
	.endm

	.macro	ctcmsa	cd, rs
	.set	push
	.set	noat
	move	$1, \rs
	.word	0x783e0819 | (\cd << 6)
	.set	pop
	.endm

	.macro	ld_d	wd, off, base
	.set	push
	.set	noat
	daddiu	$1, \base, \off
	.word	0x78000823 | (\wd << 6)
	.set	pop
	.endm

	.macro	st_d	wd, off, base
	.set	push
	.set	noat
	daddiu	$1, \base, \off
	.word	0x78000827 | (\wd << 6)
	.set	pop
	.endm

	.macro	copy_u_w	rd, ws, n
	.set	push
	.set	noat
	.word	0x78f00059 | (\n << 16) | (\ws << 11)
	/* move triggers an assembler bug... */
	or	\rd, $1, zero
	.set	pop
	.endm

	.macro	copy_u_d	rd, ws, n
	.set	push
	.set	noat
	.word	0x78f80059 | (\n << 16) | (\ws << 11)
	/* move triggers an assembler bug... */
	or	\rd, $1, zero
	.set	pop
	.endm

	.macro	insert_w	wd, n, rs
	.set	push
	.set	noat
	/* move triggers an assembler bug... */
	or	$1, \rs, zero
	.word	0x79300819 | (\n << 16) | (\wd << 6)
	.set	pop
	.endm

	.macro	insert_d	wd, n, rs
	.set	push
	.set	noat
	/* move triggers an assembler bug... */
	or	$1, \rs, zero
	.word	0x79380819 | (\n << 16) | (\wd << 6)
	.set	pop
	.endm
#endif


#endif /* _ASM_ASMMACRO_H */
