/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/LS_VZ: Support for LS3A2000/LS3A3000 hardware virtualization extensions
 *
 * Copyright (C) 2017 Loongson Corp.
 * Authors: Huang Pei <huangpei@loongon.cn>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/cacheops.h>
#include <asm/cmpxchg.h>
#include <asm/fpu.h>
#include <asm/hazards.h>
#include <asm/inst.h>
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>
#include <asm/time.h>
#include <asm/tlb.h>
#include <asm/tlbex.h>

#include <linux/kvm_host.h>

#include "interrupt.h"

#include "trace.h"


/* Pointers to last VCPU loaded on each physical CPU */
//static struct kvm_vcpu *last_vcpu[NR_CPUS];
/* Pointers to last VCPU executed on each physical CPU */
/*static struct kvm_vcpu *last_exec_vcpu[NR_CPUS];*/

/*
 * Number of guest VTLB entries to use, so we can catch inconsistency between
 * CPUs.
 */
/*static unsigned int kvm_vz_guest_vtlb_size;*/

static inline long kvm_vz_read_gc0_ebase(void)
{
	if (sizeof(long) == 8 && cpu_has_ebase_wg)
		return read_gc0_ebase_64();
	else
		return read_gc0_ebase();
}

static inline void kvm_vz_write_gc0_ebase(long v)
{
	/*
	 * First write with WG=1 to write upper bits, then write again in case
	 * WG should be left at 0.
	 * write_gc0_ebase_64() is no longer UNDEFINED since R6.
	 */
        /*
         * loongson VZ on 3a2000/3a3000 is not compatible with MIPS R5/R6
         */
	if (sizeof(long) == 8) {
		write_gc0_ebase_64(MIPS_EBASE_WG);
		write_gc0_ebase_64(v);
	} else {
		write_gc0_ebase(MIPS_EBASE_WG);
		write_gc0_ebase(v);
	}
}

/*
 * These Config bits may be writable by the guest:
 * Config:	[K23, KU] (!TLB), K0
 * Config1:	(none)
 * Config2:	[TU, SU] (impl)
 * Config3:	ISAOnExc
 * Config4:	FTLBPageSize
 * Config5:	K, CV, MSAEn, UFE, FRE, SBRI, UFR
 */

static inline unsigned int kvm_vz_config_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return CONF_CM_CMASK;
}

static inline unsigned int kvm_vz_config1_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline unsigned int kvm_vz_config2_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline unsigned int kvm_vz_config3_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return MIPS_CONF3_ISA_OE;
}

static inline unsigned int kvm_vz_config4_guest_wrmask(struct kvm_vcpu *vcpu)
{
	/* no need to be exact */
	return MIPS_CONF4_VFTLBPAGESIZE;
}

static inline unsigned int kvm_vz_config5_guest_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = MIPS_CONF5_K | MIPS_CONF5_CV | MIPS_CONF5_SBRI;

	/*
	 * Permit guest FPU mode changes if FPU is enabled and the relevant
	 * feature exists according to FIR register.
	 */
	if (kvm_mips_guest_has_fpu(&vcpu->arch)) {
		if (cpu_has_fre)
			mask |= MIPS_CONF5_FRE | MIPS_CONF5_UFE;
	}

	return mask;
}

/*
 * VZ optionally allows these additional Config bits to be written by root:
 * Config:	M, [MT]
 * Config1:	M, [MMUSize-1, C2, MD, PC, WR, CA], FP
 * Config2:	M
 * Config3:	M, MSAP, [BPG], ULRI, [DSP2P, DSPP], CTXTC, [ITL, LPA, VEIC,
 *		VInt, SP, CDMM, MT, SM, TL]
 * Config4:	M, [VTLBSizeExt, MMUSizeExt]
 * Config5:	MRP
 */

static inline unsigned int kvm_vz_config_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config1_user_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = kvm_vz_config1_guest_wrmask(vcpu) | MIPS_CONF_M;

	/* Permit FPU to be present if FPU is supported */
	if (kvm_mips_guest_can_have_fpu(&vcpu->arch))
		mask |= MIPS_CONF1_FP;

	return mask;
}

static inline unsigned int kvm_vz_config2_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config2_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config3_user_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = kvm_vz_config3_guest_wrmask(vcpu) | MIPS_CONF_M |
		MIPS_CONF3_ULRI | MIPS_CONF3_CTXTC;

	return mask;
}

static inline unsigned int kvm_vz_config4_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config4_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config5_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config5_guest_wrmask(vcpu) | MIPS_CONF5_MRP;
}


static inline void save_regs_with_field_change_exception(struct kvm_vcpu *vcpu)
{
	vcpu->arch.old_cp0_status = vcpu->arch.cop0->reg[MIPS_CP0_STATUS][0];
	vcpu->arch.old_cp0_intctl = vcpu->arch.cop0->reg[MIPS_CP0_STATUS][1];
	vcpu->arch.old_cp0_cause = vcpu->arch.cop0->reg[MIPS_CP0_CAUSE][0];
	vcpu->arch.old_cp0_entryhi = vcpu->arch.cop0->reg[MIPS_CP0_TLB_HI][0];
}

static int kvm_trap_vz_handle_cop_unusable(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_FAIL;
	int ret = RESUME_GUEST;
	unsigned int sr;

	if (((cause & CAUSEF_CE) >> CAUSEB_CE) == 1) {
		/*
		 * If guest FPU not present, the FPU operation should have been
		 * treated as a reserved instruction!
		 * If FPU already in use, we shouldn't get this at all.
		 */
#if 0
		if (WARN_ON(!kvm_mips_guest_has_fpu(&vcpu->arch) ||
			    vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)) {
			preempt_enable();
			return EMULATE_FAIL;
		}

		kvm_own_fpu(vcpu);
#endif

		preempt_disable();
		sr = read_c0_status();
		write_c0_status(sr | ST0_CU1);
		preempt_enable();
		er = EMULATE_DONE;
	}
	/* other coprocessors not handled */

	switch (er) {
	case EMULATE_DONE:
		ret = RESUME_GUEST;
		break;

	case EMULATE_FAIL:
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
		break;

	default:
		BUG();
	}
	return ret;
}

static int kvm_trap_vz_handle_tlb_ld_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	ulong badvaddr = vcpu->arch.host_cp0_badvaddr;
	union mips_instruction inst;
	enum emulation_result er = EMULATE_DONE;
	int err, ret = RESUME_GUEST;

	if (kvm_mips_handle_vz_root_tlb_fault(badvaddr, vcpu, false)) {
		/* A code fetch fault doesn't count as an MMIO */
		if (kvm_is_ifetch_fault(&vcpu->arch)) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Fetch the instruction */
		if (cause & CAUSEF_BD)
			opc += 1;
		err = kvm_get_badinstr(opc, vcpu, &inst.word);
		if (err) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Treat as MMIO */
		er = kvm_mips_emulate_load(inst, cause, run, vcpu);
		if (er == EMULATE_FAIL) {
			kvm_err("Guest Emulate Load from MMIO space failed: PC: %p, BadVaddr: %#lx\n",
				opc, badvaddr);
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		}
	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}

	return ret;
}

static int kvm_trap_vz_handle_tlb_st_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	ulong badvaddr = vcpu->arch.host_cp0_badvaddr;
	union mips_instruction inst;
	enum emulation_result er = EMULATE_DONE;
	int err, ret = RESUME_GUEST;

	if (kvm_mips_handle_vz_root_tlb_fault(badvaddr, vcpu, true)) {
		/* A code fetch fault doesn't count as an MMIO */
		if (kvm_is_ifetch_fault(&vcpu->arch)) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Fetch the instruction */
		if (cause & CAUSEF_BD)
			opc += 1;
		err = kvm_get_badinstr(opc, vcpu, &inst.word);
		if (err) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Treat as MMIO */
		er = kvm_mips_emulate_store(inst, cause, run, vcpu);
		if (er == EMULATE_FAIL) {
			kvm_err("Guest Emulate Load from MMIO space failed: PC: %p, BadVaddr: %#lx\n",
				opc, badvaddr);
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		}
	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}

	return ret;
}

static int kvm_trap_vz_no_handler(struct kvm_vcpu *vcpu)
{
	return 0;
}

static int kvm_trap_vz_handle_msa_disabled(struct kvm_vcpu *vcpu)

{
	return 0;
}

/* Write Guest TLB Entry @ Index */
#define tlbinvf_op 0x4
enum emulation_result kvm_mips_lsvz_tlbinvf(struct kvm_vcpu *vcpu)
{
	return EMULATE_DONE;
}

static enum emulation_result kvm_vz_gpsi_cop0(union mips_instruction inst,
					      u32 *opc, u32 cause,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;
	u32 rt, rd, sel;
	unsigned long curr_pc;
	unsigned long val;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	if (inst.co_format.co) {
		switch (inst.co_format.func) {
		case wait_op:
			er = kvm_mips_emul_wait(vcpu);
			break;
		case tlbr_op:
			/*NEED TO BE FIXED!!!*/
//			er = kvm_mips_lsvz_tlbr(vcpu);
			break;
		case tlbwi_op:
			/*NEED TO BE FIXED!!!*/
//			er = kvm_mips_lsvz_tlbwi(vcpu);
			break;
		case tlbwr_op:
			/*NEED TO BE FIXED!!!*/
//			er = kvm_mips_lsvz_tlbwr(vcpu);
			break;
		case tlbp_op:
			/*NEED TO BE FIXED!!!*/
//			er = kvm_mips_lsvz_tlbp(vcpu);
			break;
		case tlbinvf_op:
			er = kvm_mips_lsvz_tlbinvf(vcpu);
			break;
		default:
			er = EMULATE_FAIL;
		}
	} else {
		rt = inst.c0r_format.rt;
		rd = inst.c0r_format.rd;
		sel = inst.c0r_format.sel;

//printk("$$$$ inst.word is 0x%x, rt is %d, rd is %d, sel is %d\n", inst.word, rt, rd, sel);
		switch (inst.c0r_format.rs) {
		case dmfc_op:
		case mfc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			if (rd == MIPS_CP0_TLB_PGGRAIN &&
			    sel == 1) {			/* PageGrain */
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_CONFIG) &&
			    (sel == 6)) {               /* GSConfig*/
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_TLB_CONTEXT) &&
			    (sel == 0)) {               /* Context */
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_TLB_XCONTEXT) &&
			    (sel == 0)) {               /* XContext */
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_DIAG) &&
			    (sel == 0)) {               /* Diag */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_HI) &&
			    (sel == 0)) {               /* EntryHI */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_PG_MASK) &&
			    ((sel == 0) ||              /* Pagemask */
			     (sel == 5) ||              /* PWBase */
			     (sel == 6) ||              /* PWField */
			     (sel == 7))) {             /* PWSize */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_WIRED) &&
			    ((sel == 0) ||              /* Wired */
			     (sel == 6))) {             /* PWCtl */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_LO0) &&
			    (sel == 0)) {               /* Entrylo0*/
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_LO1) &&
			    (sel == 0)) {               /* Entrylo1*/
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_TLB_INDEX) &&
			    (sel == 0)) {               /* Index */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_DESAVE) &&
			    ((sel == 0) ||              /* Desave */
			    (sel == 2) ||               /* Kscracth1 */
			    (sel == 3) ||               /* Kscracth2 */
			    (sel == 4) ||               /* Kscracth3 */
			    (sel == 5) ||               /* Kscracth4 */
			    (sel == 6) ||               /* Kscracth5 */
			    (sel == 7))) {              /* Kscracth6 */
				val = cop0->reg[rd][sel];

			} else if ((rd == MIPS_CP0_COUNT) &&
			    ((sel == 0) ||              /* Count */
			    (sel == 7))) {               /* PGD */
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_COMPARE) &&
			    (sel == 0)) {               /* Compare */
				val = cop0->reg[rd][sel];
			} else if ((rd == MIPS_CP0_PRID &&
				    (sel == 0 ||	/* PRid */
				     sel == 2 ||	/* CDMMBase */
				     sel == 3)) ||	/* CMGCRBase */
				   (rd == MIPS_CP0_STATUS &&
				    (sel == 2 ||	/* SRSCtl */
				     sel == 3)) ||	/* SRSMap */
				   (rd == MIPS_CP0_CONFIG &&
				    (sel == 7)) ||	/* Config7 */
				   (rd == MIPS_CP0_ERRCTL &&
				    (sel == 0))) {	/* ErrCtl */
				val = cop0->reg[rd][sel];
			} else {
				val = 0;
				er = EMULATE_FAIL;
			}

			if (er != EMULATE_FAIL) {
				/* Sign extend */
				if (inst.c0r_format.rs == mfc_op)
					val = (int)val;
				vcpu->arch.gprs[rt] = val;
			}

			trace_kvm_hwr(vcpu, (inst.c0r_format.rs == mfc_op) ?
					KVM_TRACE_MFC0 : KVM_TRACE_DMFC0,
				      KVM_TRACE_COP0(rd, sel), val);
			break;

		case dmtc_op:
		case mtc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			val = vcpu->arch.gprs[rt];
			trace_kvm_hwr(vcpu, (inst.c0r_format.rs == mtc_op) ?
					KVM_TRACE_MTC0 : KVM_TRACE_DMTC0,
				      KVM_TRACE_COP0(rd, sel), val);
			if (rd == MIPS_CP0_TLB_PGGRAIN &&
			    sel == 1) {			/* PageGrain */
				/* Sign extend */
				if (inst.c0r_format.rs == mtc_op)
					val = (int)val;
				cop0->reg[rd][sel] = val;
			} else if ((rd == MIPS_CP0_CONFIG) &&
			    (sel == 6)) {               /* GSConfig*/
				/* Sign extend */
				if (inst.c0r_format.rs == mtc_op)
					val = (int)val;
				cop0->reg[rd][sel] = val;
			} else if ((rd == MIPS_CP0_TLB_CONTEXT) &&
			    (sel == 0)) {               /* Context */
				cop0->reg[rd][sel] = val;
			} else if ((rd == MIPS_CP0_TLB_XCONTEXT) &&
			    (sel == 0)) {               /* XContext */
				cop0->reg[rd][sel] = val;

			} else if ((rd == MIPS_CP0_DIAG) &&
			    (sel == 0)) {               /* Diag */
				/* Sign extend */
				if (inst.c0r_format.rs == mtc_op)
					val = (int)val;
				cop0->reg[rd][sel] = val;

			} else if ((rd == MIPS_CP0_TLB_HI) &&
			    (sel == 0)) {               /* EntryHI*/
				/* Sign extend */
#define ENTRYHI_WRITE_MASK 0xC00000FFFFFFFFFF
				if (inst.c0r_format.rs == mtc_op)
					val = (int)val;
				cop0->reg[rd][sel] = val & ENTRYHI_WRITE_MASK;

#define PAGEMASK_WRITE_MASK0 0x00000000FFFFF800
#define PAGEMASK_WRITE_MASK1 0x0000000000003000
#define PWFIELD_WRITE_MASK 0x0000003F3FFFFFFF
#define PWSIZE_WRITE_MASK 0x0000003F7FFFFFFF
			} else if ((rd == MIPS_CP0_TLB_PG_MASK)) {
				if (sel == 0)               /* Pagemask */
					cop0->reg[rd][sel] = (val & PAGEMASK_WRITE_MASK0)
							| PAGEMASK_WRITE_MASK1;
				else if (sel == 5)          /* PWBase */
					cop0->reg[rd][sel] = val;
				else if (sel == 6)          /* PWField */
					cop0->reg[rd][sel] = val & PWFIELD_WRITE_MASK;
				else if (sel == 7)          /* PWSize */
					cop0->reg[rd][sel] = val & PWSIZE_WRITE_MASK;
				else
					er = EMULATE_FAIL;

#define WIRED_WRITE_MASK 0x0000003F
#define PWCTL_WRITE_MASK 0x4000003F
			} else if (rd == MIPS_CP0_TLB_WIRED) {
				if (sel == 0)               /* Wired */
					cop0->reg[rd][sel] = val & WIRED_WRITE_MASK;
				else if (sel == 6)          /* PWCtl */
					cop0->reg[rd][sel] = val & PWCTL_WRITE_MASK;

#define ENTRYLO_WRITE_MASK 0xE00003FFFFFFFFFF
			} else if ((rd == MIPS_CP0_TLB_LO0) &&
			    (sel == 0)) {               /* Entrylo0*/
				cop0->reg[rd][sel] = val & ENTRYLO_WRITE_MASK;

			} else if ((rd == MIPS_CP0_TLB_LO1) &&
			    (sel == 0)) {               /* Entrylo1*/
				cop0->reg[rd][sel] = val & ENTRYLO_WRITE_MASK;

#define INDEX_WRITE_MASK 0x7FF
			} else if ((rd == MIPS_CP0_TLB_INDEX) &&
			    (sel == 0)) {               /* Index */
				cop0->reg[rd][sel] = (int)val & INDEX_WRITE_MASK;

			} else if ((rd == MIPS_CP0_COUNT) &&
			    (sel == 0)) {               /* Count */
				kvm_vz_lose_htimer(vcpu);
				kvm_mips_write_count(vcpu, vcpu->arch.gprs[rt]);
//				cop0->reg[rd][sel] = val;
			} else if (rd == MIPS_CP0_COMPARE &&
				   sel == 0) {		/* Compare */
				kvm_mips_write_compare(vcpu,
						       vcpu->arch.gprs[rt],
						       true);
			} else if ((rd == MIPS_CP0_COUNT) &&
			    (sel == 7)) {               /* PGD */
				cop0->reg[rd][sel] = val;

			} else if ((rd == MIPS_CP0_DESAVE) &&
			    ((sel == 0) ||              /* Desave */
			    (sel == 2) ||               /* Kscracth1 */
			    (sel == 3) ||               /* Kscracth2 */
			    (sel == 4) ||               /* Kscracth3 */
			    (sel == 5) ||               /* Kscracth4 */
			    (sel == 6) ||               /* Kscracth5 */
			    (sel == 7))) {              /* Kscracth6 */
				if (inst.c0r_format.rs == mtc_op)
					val = (int)val;
				cop0->reg[rd][sel] = val;

			} else {
				er = EMULATE_FAIL;
			}
			break;
		case wrpgpr_op:
			er = EMULATE_FAIL;
			break;
//		case rdpgpr_op:
//			er = EMULATE_FAIL;
//			break;
		default:
			er = EMULATE_FAIL;
			break;
		}
	}
	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL) {
		kvm_err("[%#lx]%s: unsupported cop0 instruction 0x%08x\n",
			curr_pc, __func__, inst.word);

		vcpu->arch.pc = curr_pc;
	}

	return er;
}

/*
 *  * Most cache ops are split into a 2 bit field identifying the cache, and a 3
 *   * bit field identifying the cache operation.
 *    */
#define CacheOp_Cache                   0x03
#define CacheOp_Op                      0x1c

#define Cache_I                         0x00
#define Cache_D                         0x01
#define Cache_T                         0x02
#define Cache_V                         0x02 /* Loongson-3 */
#define Cache_S                         0x03

#define Index_Writeback_Inv             0x00
#define Index_Load_Tag                  0x04
#define Index_Store_Tag                 0x08
#define Hit_Invalidate                  0x10
#define Hit_Writeback_Inv               0x14    /* not with Cache_I though */
#define Hit_Writeback                   0x18

static enum emulation_result kvm_vz_gpsi_cache(union mips_instruction inst,
					       u32 *opc, u32 cause,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	u32 cache, op_inst, op, base;
	s16 offset;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long va, curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */

	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	base = inst.i_format.rs;
	op_inst = inst.i_format.rt;
	offset = inst.i_format.simmediate;
	cache = op_inst & CacheOp_Cache;
	op = op_inst & CacheOp_Op;

	va = arch->gprs[base] + offset;

	kvm_debug("CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		  cache, op, base, arch->gprs[base], offset);

	/* Secondary or tirtiary cache ops ignored */
	if (cache != Cache_I && cache != Cache_D)
		return EMULATE_DONE;

	switch (op_inst) {
	case Index_Invalidate_I:
		flush_icache_line_indexed(va);
		return EMULATE_DONE;
	case Index_Writeback_Inv_D:
		flush_dcache_line_indexed(va);
		return EMULATE_DONE;
	case Hit_Invalidate_I:
	case Hit_Invalidate_D:
	case Hit_Writeback_Inv_D:
		if (boot_cpu_type() == CPU_LOONGSON3) {
			/* We can just flush entire icache */
//			local_flush_icache_range(0, 0);
			return EMULATE_DONE;
		}

		/* So far, other platforms support guest hit cache ops */
		break;
	default:
		break;
	};

	kvm_err("@ %#lx/%#lx CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		curr_pc, vcpu->arch.gprs[31], cache, op, base, arch->gprs[base],
		offset);
	/* Rollback PC */
	vcpu->arch.pc = curr_pc;

	return EMULATE_FAIL;
}

static enum emulation_result kvm_trap_vz_handle_gpsi(u32 cause, u32 *opc,
						     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	struct kvm_run *run = vcpu->run;
	union mips_instruction inst;
	int rd, rt, sel;
	int err;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;

	err = kvm_get_badinstr(opc, vcpu, &inst.word);
//	printk("#### badinst is 0x%x\n", inst.word);
	if (err)
		return EMULATE_FAIL;

	switch (inst.r_format.opcode) {
	case cop0_op:
		er = kvm_vz_gpsi_cop0(inst, opc, cause, run, vcpu);
		break;
	case cache_op:
		trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
		er = kvm_vz_gpsi_cache(inst, opc, cause, run, vcpu);
		break;
	case spec3_op:
		switch (inst.spec3_format.func) {
		case rdhwr_op:
			if (inst.r_format.rs || (inst.r_format.re >> 3))
				goto unknown;

			rd = inst.r_format.rd;
			rt = inst.r_format.rt;
			sel = inst.r_format.re & 0x7;

			switch (rd) {
			case MIPS_HWR_CC:	/* Read count register */
				arch->gprs[rt] =
					(long)(int)kvm_mips_read_count(vcpu);
				break;
			default:
				trace_kvm_hwr(vcpu, KVM_TRACE_RDHWR,
					      KVM_TRACE_HWR(rd, sel), 0);
				goto unknown;
			};

			trace_kvm_hwr(vcpu, KVM_TRACE_RDHWR,
				      KVM_TRACE_HWR(rd, sel), arch->gprs[rt]);

			er = update_pc(vcpu, cause);
			break;
		default:
			goto unknown;
		};
		break;
unknown:
	default:
		kvm_err("GPSI exception not supported (%p/%#x)\n",
				opc, inst.word);
		kvm_arch_vcpu_dump_regs(vcpu);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

static enum emulation_result kvm_trap_vz_handle_gsfc(u32 cause, u32 *opc,
						     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	union mips_instruction inst;
	int err;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err)
		return EMULATE_FAIL;

	/* complete MTC0 on behalf of guest and advance EPC */
	if (inst.c0r_format.opcode == cop0_op &&
	    ((inst.c0r_format.rs == mtc_op) ||
	    (inst.c0r_format.rs == dmtc_op)) &&
	    inst.c0r_format.z == 0) {
		int rt = inst.c0r_format.rt;
		int rd = inst.c0r_format.rd;
		int sel = inst.c0r_format.sel;
		unsigned int val = arch->gprs[rt];
		unsigned int old_val, change;

		trace_kvm_hwr(vcpu, KVM_TRACE_MTC0, KVM_TRACE_COP0(rd, sel),
			      val);

		if ((rd == MIPS_CP0_STATUS) && (sel == 0)) {
			/* FR bit should read as zero if no FPU */
			if (!kvm_mips_guest_has_fpu(&vcpu->arch))
				val &= ~(ST0_CU1 | ST0_FR);

			/*
			 * Also don't allow FR to be set if host doesn't support
			 * it.
			 */
			if (!(boot_cpu_data.fpu_id & MIPS_FPIR_F64))
				val &= ~ST0_FR;

			old_val = arch->old_cp0_status;
			change = val ^ old_val;

			if (change & ST0_KX) {
				/*
				 * indicate access 64 bit kernel seg
				 * and use XTLB REFILL exception
				 */
					old_val ^= ST0_KX;
			}

			//Update old value
			arch->old_cp0_status = old_val;
		} else if ((rd == MIPS_CP0_CAUSE) && (sel == 0)) {
			u32 old_cause = arch->old_cp0_cause;
			u32 change = old_cause ^ val;

			/* DC bit enabling/disabling timer? */
			if (change & CAUSEF_DC) {
				if (val & CAUSEF_DC) {
//					kvm_vz_lose_htimer(vcpu);
					kvm_mips_count_disable_cause(vcpu);
				} else {
					kvm_mips_count_enable_cause(vcpu);
				}
			}

			/* Only certain bits are RW to the guest */
			change &= (CAUSEF_DC | CAUSEF_IV | CAUSEF_WP |
				   CAUSEF_IP0 | CAUSEF_IP1);

			/* WP can only be cleared */
			change &= ~CAUSEF_WP | old_cause;

			arch->old_cp0_cause = val;
		} else if ((rd == MIPS_CP0_STATUS) && (sel == 1)) { /* IntCtl */
			arch->old_cp0_intctl = val;
		} else if ((rd == MIPS_CP0_TLB_HI) && (sel == 0)) {
			unsigned long old_val = arch->old_cp0_entryhi;
			unsigned long val = arch->gprs[rt];
			change = val ^ old_val;
			/* If change R VPN2 EHINV area
			   Still set ZERO to these area
			*/

		} else {
			kvm_err("Handle GSFC, unsupported field change @ %p: %#x\n",
			    opc, inst.word);
			er = EMULATE_FAIL;
		}

		if (er != EMULATE_FAIL)
			er = update_pc(vcpu, cause);
	} else {
		kvm_err("Handle GSFC, unrecognized instruction @ %p: %#x\n",
			opc, inst.word);
		er = EMULATE_FAIL;
	}

	return er;
}

static int kvm_trap_vz_handle_guest_exit(struct kvm_vcpu *vcpu)
{
	u32 cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	u32 __user *opc = (u32 __user *) vcpu->arch.pc;
	u32 gexccode = (vcpu->arch.host_cp0_guestctl0 &
			MIPS_GCTL0_GEXC) >> MIPS_GCTL0_GEXC_SHIFT;
	int ret = RESUME_GUEST;

#if 0
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;

	printk("$$$$ %s:%s:%d\n", __FILE__,__func__,__LINE__);
	printk("$$$$ VZ Guest Exception: cause %#x, PC: %p, BadVaddr: %#lx\n",
			  cause, opc, badvaddr);
	printk("$$$$ excode %#x, gsexccode: %#x\n",
			  exccode, gexccode);
#endif

	trace_kvm_exit(vcpu, KVM_TRACE_EXIT_GEXCCODE_BASE + gexccode);
	switch (gexccode) {
	case MIPS_GCTL0_GEXC_GPSI:
		++vcpu->stat.vz_gpsi_exits;
		er = kvm_trap_vz_handle_gpsi(cause, opc, vcpu);
		break;
	case MIPS_GCTL0_GEXC_GSFC:
	/*only GFC in loongson, the same code as GSFC*/
		++vcpu->stat.vz_gsfc_exits;
		er = kvm_trap_vz_handle_gsfc(cause, opc, vcpu);
		break;
//	case MIPS_GCTL0_GEXC_HC:
//		++vcpu->stat.vz_hc_exits;
//		er = kvm_trap_vz_handle_hc(cause, opc, vcpu);
//		break;
//	case MIPS_GCTL0_GEXC_GRR:
//		++vcpu->stat.vz_grr_exits;
//		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
//						       vcpu);
//		break;
	default:
//		++vcpu->stat.vz_resvd_exits;
//		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
//						       vcpu);
		break;

	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_HYPERCALL) {
		ret = kvm_mips_handle_hypcall(vcpu);
	} else {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}

	return ret;
}

static void kvm_vz_hardware_disable(void)
{

}

static int kvm_vz_hardware_enable(void)
{
	return 0;
}

static int kvm_vz_check_extension(struct kvm *kvm, long ext)
{

	return 0;
}

static int kvm_vz_vcpu_init(struct kvm_vcpu *vcpu)
{

	return 0;
}

static void kvm_vz_vcpu_uninit(struct kvm_vcpu *vcpu)
{
}

static int kvm_vz_vcpu_setup(struct kvm_vcpu *vcpu)
{
	unsigned long count_hz = 100*1000*1000; /* default to 100 MHz */

	vcpu->arch.cop0->reg[MIPS_CP0_TLB_PG_MASK][0] = 0xF000ULL;

	/*
	 * Start off the timer at the same frequency as the host timer, but the
	 * soft timer doesn't handle frequencies greater than 1GHz yet.
	 */
	if (mips_hpt_frequency && mips_hpt_frequency <= NSEC_PER_SEC)
		count_hz = mips_hpt_frequency;
	kvm_mips_init_count(vcpu, count_hz);

	return 0;
}

static void kvm_vz_flush_shadow_all(struct kvm *kvm)
{
}

static void kvm_vz_flush_shadow_memslot(struct kvm *kvm,
					const struct kvm_memory_slot *slot)
{
	/*kvm_vz_flush_shadow_all(kvm);*/
}

static gpa_t kvm_vz_gva_to_gpa_cb(gva_t gva)
{
	/* VZ guest has already converted gva to gpa */
	if((gva & CKSEG3) == CKSEG1)
		return CPHYSADDR(gva);
	else {
		return gva;
	}
}

static void kvm_vz_queue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	set_bit(priority, &vcpu->arch.pending_exceptions);
	clear_bit(priority, &vcpu->arch.pending_exceptions_clr);
}

static void kvm_vz_dequeue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	clear_bit(priority, &vcpu->arch.pending_exceptions);
	set_bit(priority, &vcpu->arch.pending_exceptions_clr);
}

static void kvm_vz_queue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	/*
	 * timer expiry is asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

static void kvm_vz_dequeue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	/*
	 * timer expiry is asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_dequeue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

static void kvm_vz_queue_io_int_cb(struct kvm_vcpu *vcpu,
				   struct kvm_mips_interrupt *irq)
{
}

static void kvm_vz_dequeue_io_int_cb(struct kvm_vcpu *vcpu,
				     struct kvm_mips_interrupt *irq)
{
}


static int kvm_vz_irq_clear_cb(struct kvm_vcpu *vcpu, unsigned int priority,
			       u32 cause)
{
	return 0;
}

static int kvm_vz_irq_deliver_cb(struct kvm_vcpu *vcpu, unsigned int priority,
				 u32 cause)
{
	return 0;
}

/*
 * VZ guest timer handling.
 */

/**
 * kvm_vz_should_use_htimer() - Find whether to use the VZ hard guest timer.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	true if the VZ GTOffset & real guest CP0_Count should be used
 *		instead of software emulation of guest timer.
 *		false otherwise.
 */
static bool kvm_vz_should_use_htimer(struct kvm_vcpu *vcpu)
{
	if (kvm_mips_count_disabled(vcpu))
		return false;

	/* Chosen frequency must match real frequency */
	if (mips_hpt_frequency != vcpu->arch.count_hz)
		return false;

	/* We don't support a CP0_GTOffset with fewer bits than CP0_Count,
	 * because we don't test gtoffset in cpu-probe.c, so ignore this
	 */
//	if (current_cpu_data.gtoffset_mask != 0xffffffff)
//		return false;

//	printk("---user htimer\n");
	return true;
}

/**
 * _kvm_vz_restore_stimer() - Restore soft timer state.
 * @vcpu:	Virtual CPU.
 * @compare:	CP0_Compare register value, restored by caller.
 * @cause:	CP0_Cause register to restore.
 *
 * Restore VZ state relating to the soft timer. The hard timer can be enabled
 * later.
 */
static void _kvm_vz_restore_stimer(struct kvm_vcpu *vcpu, u32 compare,
				   u32 cause)
{
	/*
	 * Avoid spurious counter interrupts by setting Guest CP0_Count to just
	 * after Guest CP0_Compare.
	 */
	write_c0_gtoffset(compare - read_c0_count());

	back_to_back_c0_hazard();
	write_gc0_cause(cause);
}

/**
 * _kvm_vz_restore_htimer() - Restore hard timer state.
 * @vcpu:	Virtual CPU.
 * @compare:	CP0_Compare register value, restored by caller.
 * @cause:	CP0_Cause register to restore.
 *
 * Restore hard timer Guest.Count & Guest.Cause taking care to preserve the
 * value of Guest.CP0_Cause.TI while restoring Guest.CP0_Cause.
 */
static void _kvm_vz_restore_htimer(struct kvm_vcpu *vcpu,
				   u32 compare, u32 cause)
{
	u32 start_count, after_count;
	ktime_t freeze_time;
	unsigned long flags;

	/*
	 * Freeze the soft-timer and sync the guest CP0_Count with it. We do
	 * this with interrupts disabled to avoid latency.
	 */
	local_irq_save(flags);
	freeze_time = kvm_mips_freeze_hrtimer(vcpu, &start_count);
	write_c0_gtoffset(start_count - read_c0_count());
	local_irq_restore(flags);

	/* restore guest CP0_Cause, as TI may already be set */
	back_to_back_c0_hazard();
	write_gc0_cause(cause);

	/*
	 * The above sequence isn't atomic and would result in lost timer
	 * interrupts if we're not careful. Detect if a timer interrupt is due
	 * and assert it.
	 */
	back_to_back_c0_hazard();
	after_count = read_gc0_count();
	if (after_count - start_count > compare - start_count - 1)
		kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

/**
 * kvm_vz_restore_timer() - Restore timer state.
 * @vcpu:	Virtual CPU.
 *
 * Restore soft timer state from saved context.
 */
static void kvm_vz_restore_timer(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 cause, compare;

	compare = kvm_read_sw_gc0_compare(cop0);
	cause = kvm_read_sw_gc0_cause(cop0);

	write_gc0_compare(compare);
	_kvm_vz_restore_stimer(vcpu, compare, cause);
}

/**
 * kvm_vz_acquire_htimer() - Switch to hard timer state.
 * @vcpu:	Virtual CPU.
 *
 * Restore hard timer state on top of existing soft timer state if possible.
 *
 * Since hard timer won't remain active over preemption, preemption should be
 * disabled by the caller.
 */
void kvm_vz_acquire_htimer(struct kvm_vcpu *vcpu)
{
	u32 gctl0;

	gctl0 = read_c0_guestctl0();
	if (!(gctl0 & MIPS_GCTL0_GT) && kvm_vz_should_use_htimer(vcpu)) {
		/* enable guest access to hard timer */
		write_c0_guestctl0(gctl0 | MIPS_GCTL0_GT);

		_kvm_vz_restore_htimer(vcpu, read_gc0_compare(),
				       read_gc0_cause());
	}
}

/**
 * _kvm_vz_save_htimer() - Switch to software emulation of guest timer.
 * @vcpu:	Virtual CPU.
 * @compare:	Pointer to write compare value to.
 * @cause:	Pointer to write cause value to.
 *
 * Save VZ guest timer state and switch to software emulation of guest CP0
 * timer. The hard timer must already be in use, so preemption should be
 * disabled.
 */
static void _kvm_vz_save_htimer(struct kvm_vcpu *vcpu,
				u32 *out_compare, u32 *out_cause)
{
	u32 cause, compare, before_count, end_count;
	ktime_t before_time;

	compare = read_gc0_compare();
	*out_compare = compare;

	before_time = ktime_get();

	/*
	 * Record the CP0_Count *prior* to saving CP0_Cause, so we have a time
	 * at which no pending timer interrupt is missing.
	 */
	before_count = read_gc0_count();
	back_to_back_c0_hazard();
	cause = read_gc0_cause();
	*out_cause = cause;

	/*
	 * Record a final CP0_Count which we will transfer to the soft-timer.
	 * This is recorded *after* saving CP0_Cause, so we don't get any timer
	 * interrupts from just after the final CP0_Count point.
	 */
	back_to_back_c0_hazard();
	end_count = read_gc0_count();

	/*
	 * The above sequence isn't atomic, so we could miss a timer interrupt
	 * between reading CP0_Cause and end_count. Detect and record any timer
	 * interrupt due between before_count and end_count.
	 */
	if (end_count - before_count > compare - before_count - 1)
		kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);

	/*
	 * Restore soft-timer, ignoring a small amount of negative drift due to
	 * delay between freeze_hrtimer and setting CP0_GTOffset.
	 */
	kvm_mips_restore_hrtimer(vcpu, before_time, end_count, -0x10000);
}

/**
 * kvm_vz_save_timer() - Save guest timer state.
 * @vcpu:	Virtual CPU.
 *
 * Save VZ guest timer state and switch to soft guest timer if hard timer was in
 * use.
 */
static void kvm_vz_save_timer(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 gctl0, compare, cause;

	gctl0 = read_c0_guestctl0();
	if (gctl0 & MIPS_GCTL0_GT) {
		/* disable guest use of hard timer */
		write_c0_guestctl0(gctl0 & ~MIPS_GCTL0_GT);

		/* save hard timer state */
		_kvm_vz_save_htimer(vcpu, &compare, &cause);
	} else {
		compare = read_gc0_compare();
		cause = read_gc0_cause();
	}

	/* save timer-related state to VCPU context */
	kvm_write_sw_gc0_cause(cop0, cause);
	kvm_write_sw_gc0_compare(cop0, compare);
}

/**
 * kvm_vz_lose_htimer() - Ensure hard guest timer is not in use.
 * @vcpu:	Virtual CPU.
 *
 * Transfers the state of the hard guest timer to the soft guest timer, leaving
 * guest state intact so it can continue to be used with the soft timer.
 */
void kvm_vz_lose_htimer(struct kvm_vcpu *vcpu)
{
	u32 gctl0, compare, cause;

	preempt_disable();
	gctl0 = read_c0_guestctl0();
	if (gctl0 & MIPS_GCTL0_GT) {
		/* disable guest use of timer */
		write_c0_guestctl0(gctl0 & ~MIPS_GCTL0_GT);

		/* switch to soft timer */
		_kvm_vz_save_htimer(vcpu, &compare, &cause);

		/* leave soft timer in usable state */
		_kvm_vz_restore_stimer(vcpu, compare, cause);
	}
	preempt_enable();
}

static unsigned long kvm_vz_num_regs(struct kvm_vcpu *vcpu)
{
	return 0;
}

static int kvm_vz_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices)
{
	return 0;
}
#if 0
static inline s64 entrylo_kvm_to_user(unsigned long v)
{
	s64 mask, ret = v;

	if (BITS_PER_LONG == 32) {
		/*
		 * KVM API exposes 64-bit version of the register, so move the
		 * RI/XI bits up into place.
		 */
		mask = MIPS_ENTRYLO_RI | MIPS_ENTRYLO_XI;
		ret &= ~mask;
		ret |= ((s64)v & mask) << 32;
	}
	return ret;
}

static inline unsigned long entrylo_user_to_kvm(s64 v)
{
	unsigned long mask, ret = v;

	if (BITS_PER_LONG == 32) {
		/*
		 * KVM API exposes 64-bit versiono of the register, so move the
		 * RI/XI bits down into place.
		 */
		mask = MIPS_ENTRYLO_RI | MIPS_ENTRYLO_XI;
		ret &= ~mask;
		ret |= (v >> 32) & mask;
	}
	return ret;
}
#endif

static int kvm_vz_get_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 *v)
{
	int ret = 0;

	switch (reg->id) {
	case KVM_REG_MIPS_CP0_INDEX:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO0:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO1:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_CONTEXT:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_CONTEXTCONFIG:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_USERLOCAL:
		*v = read_gc0_userlocal();
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXTCONFIG:
		ret = -EINVAL;
		break;
#endif
	case KVM_REG_MIPS_CP0_PAGEMASK:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PAGEGRAIN:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL0:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL1:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL2:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWBASE:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWFIELD:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWSIZE:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_WIRED:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWCTL:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_HWRENA:
		*v = (long)read_gc0_hwrena();
		break;
	case KVM_REG_MIPS_CP0_BADVADDR:
		*v = (long)read_gc0_badvaddr();
		break;
	case KVM_REG_MIPS_CP0_BADINSTR:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_BADINSTRP:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_COUNT:
		*v = kvm_mips_read_count(vcpu);
		break;
	case KVM_REG_MIPS_CP0_ENTRYHI:
		*v = (long)read_gc0_entryhi();
		break;
	case KVM_REG_MIPS_CP0_COMPARE:
		*v = (long)read_gc0_compare();
		break;
	case KVM_REG_MIPS_CP0_STATUS:
		*v = (long)read_gc0_status();
		break;
	case KVM_REG_MIPS_CP0_INTCTL:
		*v = read_gc0_intctl();
		break;
	case KVM_REG_MIPS_CP0_CAUSE:
		*v = (long)read_gc0_cause();
		break;
	case KVM_REG_MIPS_CP0_EPC:
		*v = (long)read_gc0_epc();
		break;
	case KVM_REG_MIPS_CP0_PRID:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_EBASE:
		*v = kvm_vz_read_gc0_ebase();
		break;
	case KVM_REG_MIPS_CP0_CONFIG:
		*v = read_gc0_config();
		break;
	case KVM_REG_MIPS_CP0_CONFIG1:
		*v = read_gc0_config1();
		break;
	case KVM_REG_MIPS_CP0_CONFIG2:
		*v = read_gc0_config2();
		break;
	case KVM_REG_MIPS_CP0_CONFIG3:
		*v = read_gc0_config3();
		break;
	case KVM_REG_MIPS_CP0_CONFIG4:
		*v = read_gc0_config4();
		break;
	case KVM_REG_MIPS_CP0_CONFIG5:
		*v = read_gc0_config5();
		break;
#if 0
	case KVM_REG_MIPS_CP0_MAAR(0) ... KVM_REG_MIPS_CP0_MAAR(0x3f):
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_CP0_MAAR(0);
		if (idx >= ARRAY_SIZE(vcpu->arch.maar))
			return -EINVAL;
		*v = vcpu->arch.maar[idx];
		break;
	case KVM_REG_MIPS_CP0_MAARI:
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		*v = kvm_read_sw_gc0_maari(vcpu->arch.cop0);
		break;
#endif
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXT:
		ret = -EINVAL;
		break;
#endif
	case KVM_REG_MIPS_CP0_ERROREPC:
		*v = (long)read_gc0_errorepc();
		break;
	case KVM_REG_MIPS_CP0_KSCRATCH1 ... KVM_REG_MIPS_CP0_KSCRATCH6:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_COUNT_CTL:
		*v = vcpu->arch.count_ctl;
		break;
	case KVM_REG_MIPS_COUNT_RESUME:
		*v = ktime_to_ns(vcpu->arch.count_resume);
		break;
	case KVM_REG_MIPS_COUNT_HZ:
		*v = vcpu->arch.count_hz;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int kvm_vz_set_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 v)
{
	int ret = 0;
	unsigned int cur, change;

	switch (reg->id) {
	case KVM_REG_MIPS_CP0_INDEX:
		write_gc0_index(v);
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO0:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO1:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_CONTEXT:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_CONTEXTCONFIG:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_USERLOCAL:
		write_gc0_userlocal(v);
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXTCONFIG:
		ret = -EINVAL;
		break;
#endif
	case KVM_REG_MIPS_CP0_PAGEMASK:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PAGEGRAIN:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL0:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL1:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_SEGCTL2:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWBASE:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWFIELD:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWSIZE:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_WIRED:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_PWCTL:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_HWRENA:
		write_gc0_hwrena(v);
		break;
	case KVM_REG_MIPS_CP0_BADVADDR:
		write_gc0_badvaddr(v);
		break;
	case KVM_REG_MIPS_CP0_BADINSTR:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_BADINSTRP:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_COUNT:
		kvm_mips_write_count(vcpu, v);
		break;
	case KVM_REG_MIPS_CP0_ENTRYHI:
		write_gc0_entryhi(v);
		break;
	case KVM_REG_MIPS_CP0_COMPARE:
		kvm_mips_write_compare(vcpu, v, false);
		break;
	case KVM_REG_MIPS_CP0_STATUS:
		write_gc0_status(v);
		break;
	case KVM_REG_MIPS_CP0_INTCTL:
		write_gc0_intctl(v);
		break;
	case KVM_REG_MIPS_CP0_CAUSE:
		/*
		 * If the timer is stopped or started (DC bit) it must look
		 * atomic with changes to the timer interrupt pending bit (TI).
		 * A timer interrupt should not happen in between.
		 */
		if ((read_gc0_cause() ^ v) & CAUSEF_DC) {
			if (v & CAUSEF_DC) {
				/* disable timer first */
				kvm_mips_count_disable_cause(vcpu);
				change_gc0_cause((u32)~CAUSEF_DC, v);
			} else {
				/* enable timer last */
				change_gc0_cause((u32)~CAUSEF_DC, v);
				kvm_mips_count_enable_cause(vcpu);
			}
		} else {
			write_gc0_cause(v);
		}
		break;
	case KVM_REG_MIPS_CP0_EPC:
		write_gc0_epc(v);
		break;
	case KVM_REG_MIPS_CP0_PRID:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_CP0_EBASE:
		kvm_vz_write_gc0_ebase(v);
		break;
	case KVM_REG_MIPS_CP0_CONFIG:
		cur = read_gc0_config();
		change = (cur ^ v) & kvm_vz_config_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG1:
		cur = read_gc0_config1();
		change = (cur ^ v) & kvm_vz_config1_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config1(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG2:
		cur = read_gc0_config2();
		change = (cur ^ v) & kvm_vz_config2_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config2(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG3:
		cur = read_gc0_config3();
		change = (cur ^ v) & kvm_vz_config3_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config3(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG4:
		cur = read_gc0_config4();
		change = (cur ^ v) & kvm_vz_config4_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config4(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG5:
		cur = read_gc0_config5();
		change = (cur ^ v) & kvm_vz_config5_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config5(v);
		}
		break;
#if 0
	case KVM_REG_MIPS_CP0_MAAR(0) ... KVM_REG_MIPS_CP0_MAAR(0x3f):
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_CP0_MAAR(0);
		if (idx >= ARRAY_SIZE(vcpu->arch.maar))
			return -EINVAL;
		vcpu->arch.maar[idx] = mips_process_maar(dmtc_op, v);
		break;
	case KVM_REG_MIPS_CP0_MAARI:
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		kvm_write_maari(vcpu, v);
		break;
#endif
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXT:
		ret = -EINVAL;
		break;
#endif
	case KVM_REG_MIPS_CP0_ERROREPC:
		write_gc0_errorepc(v);
		break;
	case KVM_REG_MIPS_CP0_KSCRATCH1 ... KVM_REG_MIPS_CP0_KSCRATCH6:
		ret = -EINVAL;
		break;
	case KVM_REG_MIPS_COUNT_CTL:
		ret = kvm_mips_set_count_ctl(vcpu, v);
		break;
	case KVM_REG_MIPS_COUNT_RESUME:
		ret = kvm_mips_set_count_resume(vcpu, v);
		break;
	case KVM_REG_MIPS_COUNT_HZ:
		ret = kvm_mips_set_count_hz(vcpu, v);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int kvm_vz_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	if (current->flags & PF_VCPU) {
		tlbw_use_hazard();
		kvm_ls_vz_load_guesttlb(vcpu);
	}
	/*Should we load the guest cp0s here??? FIX ME */

	/*
	 * Restore timer state regardless, as e.g. Cause.TI can change over time
	 * if left unmaintained.
	 */
	kvm_vz_restore_timer(vcpu);

	return 0;
}

static int kvm_vz_vcpu_put(struct kvm_vcpu *vcpu, int cpu)
{
	if (current->flags & PF_VCPU)
		kvm_ls_vz_save_guesttlb(vcpu);
	/*Should we save the guest cp0s here??? FIX ME */

	kvm_vz_save_timer(vcpu);

	return 0;
}

static void kvm_vz_vcpu_reenter(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	save_regs_with_field_change_exception(vcpu);

	kvm_ls_vz_load_guesttlb(vcpu);
}

static int kvm_vz_vcpu_run(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
//	int cpu = smp_processor_id();
	int r;

	kvm_vz_acquire_htimer(vcpu);
	/* Check if we have any exceptions/interrupts pending */
	kvm_mips_deliver_interrupts(vcpu, read_gc0_cause());
//
//	kvm_vz_check_requests(vcpu, cpu);
//	kvm_vz_vcpu_load_tlb(vcpu, cpu);
//	kvm_vz_vcpu_load_wired(vcpu);
	kvm_ls_vz_load_guesttlb(vcpu);
	save_regs_with_field_change_exception(vcpu);

	r = vcpu->arch.vcpu_run(run, vcpu);

	kvm_ls_vz_save_guesttlb(vcpu);
//	kvm_vz_vcpu_save_wired(vcpu);

	return r;
}

static struct kvm_mips_callbacks kvm_vz_callbacks = {
	.handle_cop_unusable = kvm_trap_vz_handle_cop_unusable,
	.handle_tlb_mod = kvm_trap_vz_handle_tlb_st_miss,
	.handle_tlb_ld_miss = kvm_trap_vz_handle_tlb_ld_miss,
	.handle_tlb_st_miss = kvm_trap_vz_handle_tlb_st_miss,
	.handle_addr_err_st = kvm_trap_vz_no_handler,
	.handle_addr_err_ld = kvm_trap_vz_no_handler,
	.handle_syscall = kvm_trap_vz_no_handler,
	.handle_res_inst = kvm_trap_vz_no_handler,
	.handle_break = kvm_trap_vz_no_handler,
	.handle_msa_disabled = kvm_trap_vz_handle_msa_disabled,
	.handle_guest_exit = kvm_trap_vz_handle_guest_exit,

	.hardware_enable = kvm_vz_hardware_enable,
	.hardware_disable = kvm_vz_hardware_disable,
	.check_extension = kvm_vz_check_extension,
	.vcpu_init = kvm_vz_vcpu_init,
	.vcpu_uninit = kvm_vz_vcpu_uninit,
	.vcpu_setup = kvm_vz_vcpu_setup,
	.flush_shadow_all = kvm_vz_flush_shadow_all,
	.flush_shadow_memslot = kvm_vz_flush_shadow_memslot,
	.gva_to_gpa = kvm_vz_gva_to_gpa_cb,
	.queue_timer_int = kvm_vz_queue_timer_int_cb,
	.dequeue_timer_int = kvm_vz_dequeue_timer_int_cb,
	.queue_io_int = kvm_vz_queue_io_int_cb,
	.dequeue_io_int = kvm_vz_dequeue_io_int_cb,
	.irq_deliver = kvm_vz_irq_deliver_cb,
	.irq_clear = kvm_vz_irq_clear_cb,
	.num_regs = kvm_vz_num_regs,
	.copy_reg_indices = kvm_vz_copy_reg_indices,
	.get_one_reg = kvm_vz_get_one_reg,
	.set_one_reg = kvm_vz_set_one_reg,
	.vcpu_load = kvm_vz_vcpu_load,
	.vcpu_put = kvm_vz_vcpu_put,
	.vcpu_run = kvm_vz_vcpu_run,
	.vcpu_reenter = kvm_vz_vcpu_reenter,
};

int kvm_mips_emulation_init(struct kvm_mips_callbacks **install_callbacks)
{
	if (!cpu_has_vz)
		return -ENODEV;

	/*
	 * VZ requires at least 2 KScratch registers, so it should have been
	 * possible to allocate pgd_reg.
	 */
	if (WARN(pgd_reg == -1,
		 "pgd_reg not allocated even though cpu_has_vz\n"))
		return -ENODEV;

	pr_info("Starting KVM with MIPS VZ extensions\n");

	*install_callbacks = &kvm_vz_callbacks;
	return 0;
}
