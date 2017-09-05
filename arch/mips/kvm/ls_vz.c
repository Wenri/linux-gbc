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
		default:
			er = EMULATE_FAIL;
		}
	} else {
		rt = inst.c0r_format.rt;
		rd = inst.c0r_format.rd;
		sel = inst.c0r_format.sel;

printk("$$$$ inst.word is 0x%x, rt is %d, rd is %d, sel is %d\n", inst.word, rt, rd, sel);
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
	printk("#### badinst is 0x%x\n", inst.word);
	if (err)
		return EMULATE_FAIL;

	switch (inst.r_format.opcode) {
	case cop0_op:
		er = kvm_vz_gpsi_cop0(inst, opc, cause, run, vcpu);
		break;
#ifndef CONFIG_CPU_MIPSR6
	case cache_op:
		trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
//		er = kvm_vz_gpsi_cache(inst, opc, cause, run, vcpu);
		break;
#endif
	case spec3_op:
		switch (inst.spec3_format.func) {
#ifdef CONFIG_CPU_MIPSR6
		case cache6_op:
			trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
			er = kvm_vz_gpsi_cache(inst, opc, cause, run, vcpu);
			break;
#endif
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

static int kvm_trap_vz_handle_guest_exit(struct kvm_vcpu *vcpu)
{
	u32 cause = vcpu->arch.host_cp0_cause;
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	enum emulation_result er = EMULATE_DONE;
	u32 __user *opc = (u32 __user *) vcpu->arch.pc;
	u32 gexccode = (vcpu->arch.host_cp0_guestctl0 &
			MIPS_GCTL0_GEXC) >> MIPS_GCTL0_GEXC_SHIFT;
	int ret = RESUME_GUEST;

	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;

	printk("$$$$ %s:%s:%d\n", __FILE__,__func__,__LINE__);
	printk("$$$$ VZ Guest Exception: cause %#x, PC: %p, BadVaddr: %#lx\n",
			  cause, opc, badvaddr);
	printk("$$$$ excode %#x, gsexccode: %#x\n",
			  exccode, gexccode);

	trace_kvm_exit(vcpu, KVM_TRACE_EXIT_GEXCCODE_BASE + gexccode);
	switch (gexccode) {
	case MIPS_GCTL0_GEXC_GPSI:
		++vcpu->stat.vz_gpsi_exits;
		er = kvm_trap_vz_handle_gpsi(cause, opc, vcpu);
		break;
//	case MIPS_GCTL0_GEXC_GSFC:
//	/*only GFC in loongson, the same code as GSFC*/
//		++vcpu->stat.vz_gsfc_exits;
//		er = kvm_trap_vz_handle_gsfc(cause, opc, vcpu);
//		break;
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
	return gva;
}

static void kvm_vz_queue_timer_int_cb(struct kvm_vcpu *vcpu)
{

}

static void kvm_vz_dequeue_timer_int_cb(struct kvm_vcpu *vcpu)
{

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
static unsigned long kvm_vz_num_regs(struct kvm_vcpu *vcpu)
{
	return 0;
}

static int kvm_vz_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices)
{
	return 0;
}

static int kvm_vz_get_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 *v)
{
	return 0;
}

static int kvm_vz_set_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 v)
{
	return 0;
}

static int kvm_vz_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	return 0;
}

static int kvm_vz_vcpu_put(struct kvm_vcpu *vcpu, int cpu)
{
	return 0;
}

static void kvm_vz_vcpu_reenter(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	kvm_ls_vz_load_guesttlb(vcpu);
}

static int kvm_vz_vcpu_run(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
//	int cpu = smp_processor_id();
	int r;
//
//	kvm_vz_acquire_htimer(vcpu);
//	/* Check if we have any exceptions/interrupts pending */
//	kvm_mips_deliver_interrupts(vcpu, read_gc0_cause());
//
//	kvm_vz_check_requests(vcpu, cpu);
//	kvm_vz_vcpu_load_tlb(vcpu, cpu);
//	kvm_vz_vcpu_load_wired(vcpu);
	kvm_ls_vz_load_guesttlb(vcpu);

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
