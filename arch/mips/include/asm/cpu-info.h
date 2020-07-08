/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 Waldorf GMBH
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_INFO_H
#define __ASM_CPU_INFO_H

#include <linux/types.h>

#include <asm/cache.h>

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned int waysize;	/* Bytes per way */
	unsigned short sets;	/* Number of lines per set */
	unsigned char ways;	/* Number of ways */
	unsigned char linesz;	/* Size of line in bytes */
	unsigned char waybit;	/* Bits to select in a cache set */
	unsigned char flags;	/* Flags describing cache properties */
};

struct guest_info {
	unsigned long		ases;
	unsigned long		ases_dyn;
	unsigned long long	options;
	unsigned long long	options_dyn;
	int			tlbsize;
	u8			conf;
	u8			kscratch_mask;
};

/*
 * Flag definitions
 */
#define MIPS_CACHE_NOT_PRESENT	0x00000001
#define MIPS_CACHE_VTAG		0x00000002	/* Virtually tagged cache */
#define MIPS_CACHE_ALIASES	0x00000004	/* Cache could have aliases */
#define MIPS_CACHE_IC_F_DC	0x00000008	/* Ic can refill from D-cache */
#define MIPS_IC_SNOOPS_REMOTE	0x00000010	/* Ic snoops remote stores */
#define MIPS_CACHE_PINDEX	0x00000020	/* Physically indexed cache */

struct cpuinfo_mips {
	unsigned long		asid_cache;
#ifdef CONFIG_MIPS_ASID_BITS_VARIABLE
	unsigned long           asid_mask;
#endif
	unsigned int		udelay_val;
	/*
	 * Capability and feature descriptor structure for MIPS CPU
	 */
	unsigned int		cputype;
	unsigned long long	options;
	unsigned short		tlbsize;
	unsigned short		tlbsizevtlb;
	unsigned short		tlbsizeftlbsets;
	unsigned char		tlbsizeftlbways;
	unsigned char		srsets; /* Shadow register sets */

	unsigned long		ases;
	unsigned int		processor_id;
	unsigned int		fpu_csr31;
	unsigned int		fpu_msk31;
	unsigned int		fpu_id;
	unsigned int		msa_id;
	int			isa_level;

	struct cache_desc	icache; /* Primary I-cache */
	unsigned short		watch_reg_count;   /* Number that exist */
	struct cache_desc	dcache; /* Primary D or combined I/D cache */
	unsigned short		watch_reg_use_cnt; /* Usable by ptrace */

	struct cache_desc	tcache; /* Tertiary/split secondary cache */
	short			dummy1;
	struct cache_desc	scache; /* Secondary cache */
	short			dummy2;

#define NUM_WATCH_REGS 4
	void			*data;	/* Additional data */
	u16			watch_reg_masks[NUM_WATCH_REGS];
	unsigned int		kscratch_mask; /* Usable KScratch mask. */
	int			package;/* physical package number */
	int			core;	/* physical core number */
#if defined(CONFIG_CPU_LOONGSON3) || defined(CONFIG_CPU_LOONGSON2K)
	unsigned long long      lses;   /* loongson specific extensions */
#if !defined(CONFIG_CPU_LOONGSON2K)
	struct cache_desc	vcache; /* Victim cache, between pcache and scache */
#endif
#endif
#ifdef CONFIG_64BIT
	short			vmbits; /* Virtual memory size in bits */
#endif
#if defined(CONFIG_MIPS_MT_SMP) || defined(CONFIG_MIPS_MT_SMTC)
	/*
	 * In the MIPS MT "SMTC" model, each TC is considered
	 * to be a "CPU" for the purposes of scheduling, but
	 * exception resources, ASID spaces, etc, are common
	 * to all TCs within the same VPE.
	 */
	int			vpe_id;	 /* Virtual Processor number */
#endif
#ifdef CONFIG_MIPS_MT_SMTC
	int			tc_id;	 /* Thread Context number */
#endif
	/* VZ & Guest features */
	struct guest_info	guest;
	unsigned int		gtoffset_mask;
	unsigned int		guestid_mask;
	unsigned long		guestid_cache;
	/*3A3000 feature*/
	unsigned long		vpid_cache;
} __attribute__((aligned(SMP_CACHE_BYTES)));

extern struct cpuinfo_mips cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#define raw_current_cpu_data cpu_data[raw_smp_processor_id()]
#define boot_cpu_data cpu_data[0]

extern void cpu_probe(void);
extern void cpu_report(void);

extern const char *__cpu_name[];
extern const char *__cpu_full_name[];
#define cpu_name_string()	__cpu_name[raw_smp_processor_id()]
#define cpu_full_name_string()	__cpu_full_name[raw_smp_processor_id()]

static inline unsigned long cpu_asid_inc(void)
{
	return 1 << CONFIG_MIPS_ASID_SHIFT;
}

static inline unsigned long cpu_asid_mask(struct cpuinfo_mips *cpuinfo)
{
#ifdef CONFIG_MIPS_ASID_BITS_VARIABLE
	return cpuinfo->asid_mask;
#endif
	return ((1 << CONFIG_MIPS_ASID_BITS) - 1) << CONFIG_MIPS_ASID_SHIFT;
}

static inline void set_cpu_asid_mask(struct cpuinfo_mips *cpuinfo,
                                     unsigned long asid_mask)
{
#ifdef CONFIG_MIPS_ASID_BITS_VARIABLE
	cpuinfo->asid_mask = asid_mask;
#endif
}

#endif /* __ASM_CPU_INFO_H */
