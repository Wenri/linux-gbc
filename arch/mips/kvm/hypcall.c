/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: Hypercall handling.
 *
 * Copyright (C) 2015  Imagination Technologies Ltd.
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/kvm_para.h>
#include <linux/random.h>
#include <asm/mmu_context.h>

#include "ls3a3000.h"

#define MAX_HYPCALL_ARGS	8

enum vmtlbexc {
	VMTLBL = 2,
	VMTLBS = 3,
	VMTLBM = 4,
	VMTLBRI = 5,
	VMTLBXI = 6
};

enum emulation_result kvm_mips_emul_hypcall(struct kvm_vcpu *vcpu,
					    union mips_instruction inst)
{
	unsigned int code = (inst.co_format.code >> 5) & 0x3ff;

	kvm_debug("[%#lx] HYPCALL %#03x\n", vcpu->arch.pc, code);

	switch (code) {
	case 0:
		return EMULATE_HYPERCALL;
	default:
		return EMULATE_FAIL;
	};
}


/* supposing virtual address XKPHYS is not used by any software */
#define KVM_UNIQUE_ENTRYHI(idx)					\
		((XKSSEG + ((idx) << (PAGE_SHIFT + 1))) |	\
		(cpu_has_tlbinv ? MIPS_ENTRYHI_EHINV : 0))

void kvm_mips_tlbw(struct kvm_mips_tlb *ptlb)
{
	unsigned long tmp_entryhi, tmp_entrylo0, tmp_entrylo1;
	unsigned long page_mask;
	unsigned int tmp_diag;
	unsigned long flags;
	int tmp_index,idx;

	local_irq_save(flags);
	//Save tmp registers
	tmp_entryhi  = read_c0_entryhi();
	tmp_entrylo0 = read_c0_entrylo0();
	tmp_entrylo1 = read_c0_entrylo1();
	page_mask = read_c0_pagemask();
	tmp_index = read_c0_index();

	//Enable diag.MID for guest
	tmp_diag = read_c0_diag();
	tmp_diag |= (1<<18);
	write_c0_diag(tmp_diag);

	write_c0_entryhi(ptlb->tlb_hi);
	mtc0_tlbw_hazard();

	write_c0_pagemask(ptlb->tlb_mask);
	write_c0_entrylo0(ptlb->tlb_lo[0]);
	write_c0_entrylo1(ptlb->tlb_lo[1]);
	mtc0_tlbw_hazard();
	tlb_probe();
	tlb_probe_hazard();

	idx = read_c0_index();
	mtc0_tlbw_hazard();
	if (idx >= 0)
		tlb_write_indexed();
	else
		tlb_write_random();
	tlbw_use_hazard();
	//Disable diag.MID
	tmp_diag = read_c0_diag();
	tmp_diag &= ~(3<<18);
	write_c0_diag(tmp_diag);

	//Restore tmp registers
	write_c0_entryhi(tmp_entryhi);
	write_c0_entrylo0(tmp_entrylo0);
	write_c0_entrylo1(tmp_entrylo1);
	write_c0_pagemask(page_mask);
	write_c0_index(tmp_index);

	local_irq_restore(flags);
}

int kvm_mips_guesttlb_lookup(struct kvm_vcpu *vcpu, unsigned long entryhi)
{
	int i;
	int index = -1;
	struct kvm_mips_tlb *tlb = vcpu->arch.guest_tlb;

	for_each_set_bit(i, vcpu->arch.tlbmap, KVM_MIPS_GUEST_TLB_SIZE) {
		if (TLB_HI_VPN2_HIT(tlb[i], entryhi) &&
				TLB_HI_ASID_HIT(tlb[i], entryhi)) {
			index = i;
			break;
		}
	}

	return index;
}

int kvm_mips_gva_to_hpa(struct kvm_vcpu *vcpu, unsigned long gva, unsigned long  *phpa) {
	unsigned int s_index, s_asid;
	soft_tlb *ptlb;
	int i, idx;
	unsigned int vatag;
	unsigned long hpa;

	/* user space use soft TLB*/
	s_index = ((gva >> STLB_ENTRYHI_SHIFT) & STLB_WAY_MASK) * STLB_SET;
	s_asid = read_gc0_entryhi() & MIPS_ENTRYHI_ASID;
	ptlb = &vcpu->arch.stlb[s_index];
	vatag = (gva >> STLB_VATAG_SHIFT) & 0xffffffff;
	for (i=0; i<STLB_SET; i++) {
		if ((ptlb->asid == s_asid) && (ptlb->vatag == vatag)) {
			/* found it */
			idx = (gva >> PAGE_SHIFT) & 1;
			if (idx == 0)
				hpa = ptlb->lo0;
			else
				hpa = ptlb->lo1;
			hpa = ((hpa >> 6) << 12) | (gva & ~PAGE_MASK);
			*phpa = hpa;
			return 1;
		}
		ptlb++;
	}

	return 0;
}

static void kvm_flush_stlb_one(struct kvm_vcpu *vcpu, unsigned long gva)
{
	unsigned int s_index;
	unsigned int s_asid, vatag;
	soft_tlb *ptlb;
	int i;

	s_index = ((gva >> STLB_ENTRYHI_SHIFT) & STLB_WAY_MASK) * STLB_SET;
	s_asid = (0xff) & read_gc0_entryhi();
	vatag = (gva >> STLB_VATAG_SHIFT) & 0xffffffff;
	ptlb = &vcpu->arch.stlb[s_index];
	for (i=0; i<STLB_SET; i++) {
		if ((ptlb->asid == s_asid) && (ptlb->vatag == vatag)) {
			memset(ptlb, 0, sizeof(soft_tlb));
			break;
		}
		ptlb++;
	}

	return;
}

static void kvm_flush_stlb_range(struct kvm_vcpu *vcpu, unsigned long gva_start, unsigned long gva_end)
{
        unsigned int s_index, start, end;
        unsigned int s_asid, vatag;
        soft_tlb *ptlb;
        int i;

	i = (gva_end - gva_start) >> (PAGE_SHIFT + 1);
	if (i > 1024) {
		/* flush all stlb items */
		memset(vcpu->arch.stlb, 0, STLB_BUF_SIZE * sizeof(soft_tlb));
		memset(vcpu->arch.asid_we, 0, STLB_ASID_SIZE * sizeof(unsigned long));
		return;
	}

	start = (gva_start >> STLB_ENTRYHI_SHIFT) & STLB_WAY_MASK;
	end   = (gva_end >> STLB_ENTRYHI_SHIFT) & STLB_WAY_MASK;
	s_asid = (0xff) & read_gc0_entryhi();

	if (start <= end) {
		s_index = end;
	} else {
		s_index = STLB_WAY;
	}

	/* flush start to min(end, bottom) stlb items */
	vatag = (gva_start >> STLB_VATAG_SHIFT) & 0xffffffff;
	while (start < s_index) {
		ptlb = &vcpu->arch.stlb[start * STLB_SET];
		for (i=0; i<STLB_SET; i++) {
			if ((ptlb->asid == s_asid) && (ptlb->vatag == vatag)) {
				memset(ptlb, 0, sizeof(soft_tlb));
				break;
			}
			ptlb++;
		}
		start++;
	}

	if (start == STLB_WAY) {
		/* flush 0 to end stlb items */
		start = 0;
		s_index = end;
		vatag = (gva_end >> STLB_VATAG_SHIFT) & 0xffffffff;
		while (start < s_index) {
			ptlb = &vcpu->arch.stlb[start * STLB_SET];
			for (i=0; i<STLB_SET; i++) {
				if ((ptlb->asid == s_asid) && (ptlb->vatag == vatag)) {
					memset(ptlb, 0, sizeof(soft_tlb));
					break;
				}
				ptlb++;
			}
			start++;
		}
	}
	return;
}

static void kvm_add_stlb(struct kvm_vcpu *vcpu, unsigned long gva, struct kvm_mips_tlb *hosttlb)
{
        unsigned int s_index;
        unsigned int s_asid, vatag;
	unsigned long min_weight;
        soft_tlb *ptlb;
        int i, min_set, priority;

	/* not commpage */
	if ((gva & (PAGE_MASK << 1)) == 0)
		return;

	/* user space use soft TLB*/
	s_index = ((gva >> STLB_ENTRYHI_SHIFT) & STLB_WAY_MASK) * STLB_SET;
	s_asid = (0xff) & read_gc0_entryhi();
	vatag = (gva >> STLB_VATAG_SHIFT) & 0xffffffff;
	ptlb = &vcpu->arch.stlb[s_index];
	min_weight = -1;
	min_set = 0;
	priority = 0;

	for (i=0; i<STLB_SET; i++) {
		if ((ptlb->asid == s_asid) && (ptlb->vatag == vatag)) {
			min_set = i;
			priority = 2;
			break;
		} else if ((ptlb->flag & STLB_FLAG_VALID) == 0) {
			min_set = i;
			priority = 1;
		} else if ((min_weight < vcpu->arch.asid_we[ptlb->asid]) && (priority == 0)) {
			min_weight = vcpu->arch.asid_we[ptlb->asid];
			min_set = i;
		}
		ptlb++;
	}

	ptlb = &vcpu->arch.stlb[s_index + min_set];
	ptlb->vatag = 0xffffffff & (gva >> STLB_VATAG_SHIFT);
	ptlb->lo0 = hosttlb->tlb_lo[0];
	ptlb->lo1 = hosttlb->tlb_lo[1];
	ptlb->asid = s_asid;
	ptlb->flag = ptlb->flag | STLB_FLAG_VALID;

	return;
}

static int kvm_mips_hcall_tlb(struct kvm_vcpu *vcpu, unsigned long num,
			      const unsigned long *args, unsigned long *hret)
{

	/* organize parameters as follow
	 * a0        a1          a2         a3
	 *badvaddr  PAGE_SHIFT  even pte  odd pte
	 *
	*/
	if(((args[0] & 0xf000000000000000) > XKSSEG) &&
			((args[0] & 0xf000000000000000) != XKSEG) &&
			((args[0] & CKSEG3) != CKSSEG))
		kvm_err("should not guest badvaddr %lx with type %lx\n",
				 args[0], args[4]);

	if ((args[0] & 0xf000000000000000) < XKSSEG)
		kvm_debug("1 guest badvaddr %lx pgshift %lu a2 %lx a3 %lx\n",
				 args[0],args[1],args[2],args[3]);

	if((args[4] & 0xf000) == 0)
	{
		++vcpu->stat.lsvz_hc_tlbmiss_exits;
		if(((args[0] & 0x4000) && (args[3] & 0x1000)) ||
			(!(args[0] & 0x4000) && (args[2] & 0x1000)))
				++vcpu->stat.lsvz_hc_missvalid_exits;
	} else if((args[4] & 0xf000) == 0x1000)
		++vcpu->stat.lsvz_hc_tlbm_exits;
	else if((args[4] & 0xf000) == 0x2000)
		++vcpu->stat.lsvz_hc_tlbl_exits;
	else if((args[4] & 0xf000) == 0x3000)
		++vcpu->stat.lsvz_hc_tlbs_exits;
	else if((args[4] & 0xf000) == 0x4000)
		++vcpu->stat.lsvz_hc_emulate_exits;

	vcpu->arch.host_cp0_badvaddr = args[0];

	if ((args[4] == 0x5001) || (args[4] == 0x5005)) {
#if 1
		/*If guest hypcall to flush_tlb_page (0x5001)
		 *or flush_tlb_one (0x5005)
		 * TLB probe and then clear the TLB Line
		*/
		unsigned long tmp_entryhi, tmp_entrylo0, tmp_entrylo1;
		unsigned long page_mask;
		unsigned int tmp_diag;
		unsigned long flags;
		int tmp_index, idx;
		unsigned long badvaddr;

		local_irq_save(flags);
		//Save tmp registers
		tmp_entryhi  = read_c0_entryhi();
		tmp_entrylo0 = read_c0_entrylo0();
		tmp_entrylo1 = read_c0_entrylo1();
		page_mask = read_c0_pagemask();
		tmp_index = read_c0_index();

		//Enable diag.MID for guest
		tmp_diag = read_c0_diag();
		tmp_diag |= (1<<18);
		write_c0_diag(tmp_diag);

		badvaddr = (args[0] & 0xc000ffffffffe000) & (PAGE_MASK << 1);
		if(args[4] == 0x5001)
			badvaddr |=  read_gc0_entryhi() & MIPS_ENTRYHI_ASID;
		write_c0_entryhi(badvaddr);

		mtc0_tlbw_hazard();
		tlb_probe();
		tlb_probe_hazard();

		idx = read_c0_index();
		if (idx >= 0) {
			/* Make sure all entries differ. */
			write_c0_entryhi(KVM_UNIQUE_ENTRYHI(idx));
			write_c0_entrylo0(0);
			write_c0_entrylo1(0);
			mtc0_tlbw_hazard();
			tlb_write_indexed();
			tlbw_use_hazard();
		}
		//Disable diag.MID
		tmp_diag = read_c0_diag();
		tmp_diag &= ~(3<<18);
		write_c0_diag(tmp_diag);

		//Restore tmp registers
		write_c0_entryhi(tmp_entryhi);
		write_c0_entrylo0(tmp_entrylo0);
		write_c0_entrylo1(tmp_entrylo1);
		write_c0_pagemask(page_mask);
		write_c0_index(tmp_index);

		local_irq_restore(flags);

#else
		local_flush_tlb_all();
#endif

		if (args[4] == 0x5001) {
			kvm_flush_stlb_one(vcpu, args[0]);
		}
	} else if ((args[4] == 0x5003) || (args[4] == 0x5004)) {
#if 1
		/*flush_tlb_range (0x5003) of guest XUSEG address
		 * or flush_tlb_kernel_range (0x5004)
		*/
		unsigned long flags;

		local_irq_save(flags);
		//range size larger than TLB lines
		if(args[2] > 1024)
			local_flush_tlb_all();
		else {
			unsigned long tmp_entryhi, tmp_entrylo0, tmp_entrylo1;
			unsigned long page_mask;
			unsigned int tmp_diag;
			unsigned long address;
			int tmp_index, idx;
			unsigned long gc0_entryhi;

			address = (args[0] & 0xc000ffffffffe000) & (PAGE_MASK << 1); 
			//Save tmp registers
			tmp_entryhi  = read_c0_entryhi();
			tmp_entrylo0 = read_c0_entrylo0();
			tmp_entrylo1 = read_c0_entrylo1();
			page_mask = read_c0_pagemask();
			tmp_index = read_c0_index();
			gc0_entryhi = (read_gc0_entryhi() & MIPS_ENTRYHI_ASID);
			if(args[4] == 0x5003)
				address |= gc0_entryhi;

			//Enable diag.MID for guest
			tmp_diag = read_c0_diag();
			tmp_diag |= (1<<18);
			write_c0_diag(tmp_diag);

			while(address < args[1]) {

				write_c0_entryhi(address);
				mtc0_tlbw_hazard();
				address += (PAGE_SIZE << 1);
				tlb_probe();
				tlb_probe_hazard();

				idx = read_c0_index();
				if (idx >= 0) {
					/* Make sure all entries differ. */
					write_c0_entryhi(KVM_UNIQUE_ENTRYHI(idx));
					write_c0_entrylo0(0);
					write_c0_entrylo1(0);
					mtc0_tlbw_hazard();
					tlb_write_indexed();
					tlbw_use_hazard();
				}
			}
			//Disable diag.MID
			tmp_diag = read_c0_diag();
			tmp_diag &= ~(3<<18);
			write_c0_diag(tmp_diag);

			//Restore tmp registers
			write_c0_entryhi(tmp_entryhi);
			write_c0_entrylo0(tmp_entrylo0);
			write_c0_entrylo1(tmp_entrylo1);
			write_c0_pagemask(page_mask);
			write_c0_index(tmp_index);


		}
		local_irq_restore(flags);
#else
		local_flush_tlb_all();
#endif
		if (args[4] == 0x5003) {
			kvm_flush_stlb_range(vcpu, args[0], args[1]);
		}
	} else if (args[4] == 0x5002) {
		/*flush tlb all */
		local_flush_tlb_all();
		memset(vcpu->arch.stlb, 0, STLB_BUF_SIZE * sizeof(soft_tlb));
		memset(vcpu->arch.asid_we, 0, STLB_ASID_SIZE * sizeof(unsigned long));

		memset(vcpu->arch.guest_tlb, 0, sizeof(struct kvm_mips_tlb) * KVM_MIPS_GUEST_TLB_SIZE);
		memset(vcpu->arch.tlbmap, 0, KVM_MIPS_GUEST_TLB_SIZE/sizeof(unsigned char));
	} else if ((args[4] >> 12) < 5) {
		unsigned long prot_bits = 0;
		unsigned long gpa;
		int write_fault = 0;
		pte_t pte_gpa;
		pte_t pte_gpa1;
		int ret = 0, mmio = 0, index;

		unsigned long cksseg_gva;
		int offset, cksseg_odd = 0;
		struct kvm_mips_tlb tlb;

		/* Now the prot bits scatter as this
		CCA D V G RI XI SP PROT S H M A W P
		so set all CCA=3 as cached*/

		pte_val(pte_gpa) = 0;
		pte_val(pte_gpa1) = 0;
		if (args[2] & _PAGE_VALID) {
			write_fault = args[2] & _PAGE_DIRTY;
			gpa = ((pte_to_entrylo(args[2]) & 0x3ffffffffff) >> 6) << 12;
			ret = kvm_lsvz_map_page(vcpu, gpa, write_fault, _PAGE_GLOBAL, &pte_gpa, NULL);
			if (ret == 0) {
				prot_bits = args[2] & 0xffff;
				prot_bits = (prot_bits & ~_CACHE_MASK) | _page_cachable_default;
				pte_val(pte_gpa) = (pte_val(pte_gpa) & _PFN_MASK) | (prot_bits & pte_val(pte_gpa) & ~_PFN_MASK);
				/* NI/RI attribute does not support now */
				//pte_val(pte_gpa) = (pte_val(pte_gpa) & _PFN_MASK) | ((_PAGE_NO_EXEC | _PAGE_NO_READ) & prot_bits & ~_PFN_MASK);
			} else
				mmio = 1;
		}

		if (args[3] & _PAGE_VALID) {
			write_fault = args[3] & _PAGE_DIRTY;
			gpa = ((pte_to_entrylo(args[3]) & 0x3ffffffffff) >> 6) << 12;
			ret = kvm_lsvz_map_page(vcpu, gpa, write_fault, _PAGE_GLOBAL, &pte_gpa1, NULL);
			if (ret == 0) {
				prot_bits = args[3] & 0xffff; //Get all the sw/hw prot bits of even pte
				prot_bits = (prot_bits & ~_CACHE_MASK) | _page_cachable_default;
				pte_val(pte_gpa1) = (pte_val(pte_gpa1) & _PFN_MASK) | (prot_bits & pte_val(pte_gpa1) & ~_PFN_MASK);
				/* NI/RI attribute does not support now */
				//pte_val(pte_gpa1) = (pte_val(pte_gpa1) & _PFN_MASK) | ((_PAGE_NO_EXEC|_PAGE_NO_READ) & prot_bits & ~_PFN_MASK);
			} else
				mmio = 1;
		}

		/*update software tlb
		*/
		tlb.tlb_hi = (args[0] & 0xc000ffffffffe000) & (PAGE_MASK << 1);
		tlb.tlb_hi = tlb.tlb_hi | (read_gc0_entryhi() & MIPS_ENTRYHI_ASID);
		/* only normal pagesize is supported now */
		tlb.tlb_mask = 0x7800; //normal pagesize 16KB
		tlb.tlb_lo[0] = pte_to_entrylo(pte_val(pte_gpa));
		tlb.tlb_lo[1] = pte_to_entrylo(pte_val(pte_gpa1));
		kvm_mips_tlbw(&tlb);

		if ((args[0] & 0xf000000000000000) == XKUSEG) {
			if (mmio == 1) {
				index = kvm_mips_guesttlb_lookup(vcpu, tlb.tlb_hi);
				if (index < 0) {
					index = find_first_zero_bit(vcpu->arch.tlbmap, KVM_MIPS_GUEST_TLB_SIZE);
					if (index < 0) {
						get_random_bytes(&index, sizeof(index));
						index &= (KVM_MIPS_GUEST_TLB_SIZE - 1);
					}
				}
				vcpu->arch.guest_tlb[index].tlb_hi = tlb.tlb_hi;
				vcpu->arch.guest_tlb[index].tlb_mask = 0x7800;
				vcpu->arch.guest_tlb[index].tlb_lo[0] = pte_to_entrylo(args[2]);
				vcpu->arch.guest_tlb[index].tlb_lo[1] = pte_to_entrylo(args[3]);
				set_bit(index, vcpu->arch.tlbmap);
			}

			kvm_add_stlb(vcpu, args[0], &tlb);
		}

		/*Save CKSSEG address GVA-->GPA mapping*/
		if (((args[0] & CKSEG3) == CKSSEG)) {
			cksseg_gva = args[0] & (PAGE_MASK);
			cksseg_odd = (cksseg_gva >> 14) & 1;
			offset = ((cksseg_gva - CKSSEG) & 0x3fffffff ) >> 14;
			/*If the cksseg address is odd */
			if(cksseg_odd) {
				vcpu->kvm->arch.cksseg_map[offset - 1][0] = cksseg_gva - PAGE_SIZE;
				vcpu->kvm->arch.cksseg_map[offset - 1][1] = ((pte_to_entrylo(args[2]) & 0x3ffffffffff) >> 6) << 12;
				vcpu->kvm->arch.cksseg_map[offset][0] = cksseg_gva;
				vcpu->kvm->arch.cksseg_map[offset][1] = ((pte_to_entrylo(args[3]) & 0x3ffffffffff) >> 6) << 12;
			} else {
				vcpu->kvm->arch.cksseg_map[offset][0] = cksseg_gva;
				vcpu->kvm->arch.cksseg_map[offset][1] = ((pte_to_entrylo(args[2]) & 0x3ffffffffff) >> 6) << 12;
				vcpu->kvm->arch.cksseg_map[offset + 1][0] = cksseg_gva + PAGE_SIZE;
				vcpu->kvm->arch.cksseg_map[offset + 1][1] = ((pte_to_entrylo(args[3]) & 0x3ffffffffff) >> 6) << 12;
			}
		}

	} else {
		/* Report unimplemented hypercall to guest */
		*hret = -KVM_ENOSYS;
		kvm_err("unsupported hypcall operation from guest %lx\n", vcpu->arch.pc);
	}

	/* Report unimplemented hypercall to guest */
//	*hret = -KVM_ENOSYS;
	return RESUME_GUEST;
}

static int kvm_mips_hypercall(struct kvm_vcpu *vcpu, unsigned long num,
			      const unsigned long *args, unsigned long *hret)
{
	if(current_cpu_type() == CPU_LOONGSON3) {
		struct kvm_run *run = vcpu->run;
		int ret;

		/* Here is existing tlb hypercall
		   #define tlbmiss_tlbwr_normal    0x0
		   #define tlbmiss_tlbwr_huge      0x1
		   #define tlbm_tlbp_and_tlbwi_normal 0x1000
		   #define tlbm_tlbp_and_tlbwi_huge 0x1001
		   #define tlbl_tlbp_and_tlbwi_normal 0x2000
		   #define tlbl_tlbp_and_tlbwi_huge 0x2001
		   #define tlbs_tlbp_and_tlbwi_normal 0x3000
		   #define tlbs_tlbp_and_tlbwi_huge 0x3001
		*/
		if (num != KVM_MIPS_GET_RTAS_INFO)
			return kvm_mips_hcall_tlb(vcpu, num, args, hret);

		run->hypercall.nr = num;
		run->hypercall.args[0] = args[0];
		run->hypercall.args[1] = args[1];
		run->hypercall.args[2] = args[2];
		run->hypercall.args[3] = args[3];
		run->hypercall.args[4] = args[4];
		run->hypercall.args[5] = args[5];
		run->exit_reason = KVM_EXIT_HYPERCALL;
		ret = RESUME_HOST;
		return ret;
	}
	/* Report unimplemented hypercall to guest */
	*hret = -KVM_ENOSYS;
	return RESUME_GUEST;
}

int kvm_mips_handle_hypcall(struct kvm_vcpu *vcpu)
{
	unsigned long num, args[MAX_HYPCALL_ARGS];

	/* read hypcall number and arguments */
	num = vcpu->arch.gprs[2];	/* v0 */
	args[0] = vcpu->arch.gprs[4];	/* a0 */
	args[1] = vcpu->arch.gprs[5];	/* a1 */
	args[2] = vcpu->arch.gprs[6];	/* a2 */
	args[3] = vcpu->arch.gprs[7];	/* a3 */
	args[4] = vcpu->arch.gprs[2];	/* tlb_miss/tlbl/tlbs/tlbm */
	args[5] = vcpu->arch.gprs[3];	/* EXCCODE/_TLBL/_TLBS/_MOD */

	return kvm_mips_hypercall(vcpu, num,
				  args, &vcpu->arch.gprs[2] /* v0 */);
}
