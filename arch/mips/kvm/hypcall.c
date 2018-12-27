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

#define MAX_HYPCALL_ARGS	8

enum vmtlbexc {
	VMTLBL = 2,
	VMTLBS = 3,
	VMTLBM = 4,
	VMTLBRI = 5,
	VMTLBXI = 6
};

extern int kvm_lsvz_map_page(struct kvm_vcpu *vcpu, unsigned long gpa,
				    bool write_fault,unsigned long prot_bits,
				    pte_t *out_entry, pte_t *out_buddy);

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

int guest_pte_trans(const unsigned long *args,
			      struct kvm_vcpu *vcpu,
			      bool write_fault, pte_t *pte, pte_t *pte1)
{
	int ret = 0;
	unsigned long gpa = 0;
	int idx= 0;
	unsigned long entrylo;
	unsigned long prot_bits = 0;
	gfn_t gfn;
	unsigned long hva;
	struct kvm_memory_slot* slot;

	/* The badvaddr we get maybe guest unmmaped or mmapped address,
	 * but not a GPA
	 * args[0] is badvaddr
	 * args[1] is pagemask
	 * args[2] is even pte value
	 * args[3] is odd pte value
	 * we get the guest pfn according the parameters,GPA-->HPA trans here
	*/
	//PFN is the PA over 12bits
		entrylo = pte_to_entrylo(args[2]);
		prot_bits = args[2] & 0xffff; //Get all the sw/hw prot bits

		gpa = ((entrylo & 0x3ffffffffff) >> 6) << 12;

		//we need to get the HVA????
		gfn = gpa >> PAGE_SHIFT;
		slot = gfn_to_memslot(vcpu->kvm, gfn);
		if(slot) {
			hva = slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE;

			if((hva >> args[1]) & 1)
				idx = 1;
			else
				idx = 0;

			ret = kvm_lsvz_map_page(vcpu, gpa, write_fault, _PAGE_GLOBAL, &pte[idx], &pte[!idx]);
			if(ret)
				kvm_info("entrylo0 map page error\n");

			if (args[0] < XKSSEG)
				pte[idx].pte &= ~_PAGE_GLOBAL;
		}

		entrylo = pte_to_entrylo(args[3]);
		prot_bits = args[3] & 0xffff; //Get all the sw/hw prot bits

		gpa = ((entrylo & 0x3ffffffffff) >> 6) << 12;

		//we need to get the HVA????
		gfn = gpa >> PAGE_SHIFT;
		slot = gfn_to_memslot(vcpu->kvm, gfn);
		if(slot){
			hva = slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE;
		
			if((hva >> args[1]) & 1)
				idx = 1;
			else
				idx = 0;
			ret = kvm_lsvz_map_page(vcpu, gpa, write_fault, _PAGE_GLOBAL, &pte1[idx], &pte1[!idx]);
			if(ret)
				kvm_info("entrylo1 map page error\n");

			if (args[0] < XKSSEG)
				pte1[idx].pte &= ~_PAGE_GLOBAL;
		}

	if (ret)
		ret = RESUME_HOST;

	if ((args[0] & 0xf000000000000000) < XKSSEG)
		kvm_debug("2 %s gpa %lx pte[%d] %lx pte[%d] %lx\n",__func__,
			gpa, idx, pte_val(pte[idx]), !idx, pte_val(pte[!idx]));

	return ret;
}

extern void local_flush_tlb_all(void);
extern void flush_tlb_all(void);
static int kvm_mips_hcall_tlb(struct kvm_vcpu *vcpu, unsigned long num,
			      const unsigned long *args, unsigned long *hret)
{
	/* organize parameters as follow
	 * a0        a1          a2         a3
	 *badvaddr  PAGE_SHIFT  even pte  odd pte
	 *
	*/
	if(((args[0] & 0xf000000000000000) > XKSSEG) &&
			((args[0] & 0xf000000000000000) != XKSEG))
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
#ifdef CONFIG_CPU_LOONGSON3
	if ((args[4] == 0x5001) || (args[4] == 0x5005)) {
#if 0	
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

		badvaddr = args[0] & PAGE_MASK;
		if(args[4] == 0x5001)
			write_c0_entryhi(badvaddr | vcpu->arch.cop0->reg[MIPS_CP0_TLB_HI][0]);
		else if (args[4] == 0x5005)
			write_c0_entryhi(badvaddr);

		mtc0_tlbw_hazard();
		tlb_probe();
		tlb_probe_hazard();

		idx = read_c0_index();
		if (idx >= 0) {
			/* Make sure all entries differ. */
			write_c0_entryhi(MIPS_ENTRYHI_EHINV);
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
		write_c0_entrylo0(tmp_entrylo1);
		write_c0_pagemask(page_mask);
		write_c0_index(tmp_index);

		//flush ITLB/DTLB
		tmp_diag = read_c0_diag();
		tmp_diag |= 0xc;
		write_c0_diag(tmp_diag);

		local_irq_restore(flags);

		if ((args[0] & 0xf000000000000000) < XKSSEG)
			kvm_debug("%lx guest badvaddr %lx  %lx ASID %lx idx %x\n",args[4], args[0],badvaddr, read_gc0_entryhi(),idx);
#else
		flush_tlb_all();
#endif
	} else if ((args[4] == 0x5003) || (args[4] == 0x5004)) {
#if 0
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

			address = args[0];
			//Save tmp registers
			tmp_entryhi  = read_c0_entryhi();
			tmp_entrylo0 = read_c0_entrylo0();
			tmp_entrylo1 = read_c0_entrylo1();
			page_mask = read_c0_pagemask();
			tmp_index = read_c0_index();
			gc0_entryhi = vcpu->arch.cop0->reg[MIPS_CP0_TLB_HI][0];

			//Enable diag.MID for guest
			tmp_diag = read_c0_diag();
			tmp_diag |= (1<<18);
			write_c0_diag(tmp_diag);

			while(address < args[1]) {

				if(args[4] == 0x5003)
					write_c0_entryhi(address | gc0_entryhi);
				else if (args[4] == 0x5004)
					write_c0_entryhi(address);

				mtc0_tlbw_hazard();
				address += PAGE_SIZE;
				tlb_probe();
				tlb_probe_hazard();

				idx = read_c0_index();
				if (idx >= 0) {
					/* Make sure all entries differ. */
					write_c0_entryhi(MIPS_ENTRYHI_EHINV);
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
			write_c0_entrylo0(tmp_entrylo1);
			write_c0_pagemask(page_mask);
			write_c0_index(tmp_index);

			//flush ITLB/DTLB
			tmp_diag = read_c0_diag();
			tmp_diag |= 0xc;
			write_c0_diag(tmp_diag);

		}
		local_irq_restore(flags);
#else		
		flush_tlb_all();
#endif		
	} else if (args[4] == 0x5002) {
		/*flush tlb all */
		flush_tlb_all();
	} else {
		unsigned long prot_bits = 0;
		unsigned long prot_bits1 = 0;
		unsigned long gpa = 0;
		int write_fault = 0;
		pte_t pte_gpa[2];
		pte_t pte_gpa1[2];
		int ret = 0;
		u32 gsexccode = args[5];

		gfn_t gfn;
		struct kvm_memory_slot* slot;
		unsigned long hva = 0, hva1 = 0;

		//Distinct TLBL/TLBS/TLBM
		switch(gsexccode) {
		case EXCCODE_TLBL:
			write_fault = 0;
			break;
		case EXCCODE_TLBS:
			write_fault = 1;
			break;
		case EXCCODE_MOD:
			write_fault = 1;
			break;
		case EXCCODE_TLBRI:
			break;
		case EXCCODE_TLBXI:
			break;
		default:
			kvm_info("illegal guest cause value %lx type %lx\n",args[5],args[4]);
			break;
		}
		prot_bits = args[3] & 0xffff; //Get all the sw/hw prot bits of odd pte
		prot_bits1 = args[2] & 0xffff; //Get all the sw/hw prot bits of even pte

		/* Now the prot bits scatter as this
		CCA D V G RI XI SP PROT S H M A W P
		so set all CCA=3 as cached*/
		prot_bits |= 0x6000;
		prot_bits1 |= 0x6000;

		//Process GUEST even pte
		gpa = ((pte_to_entrylo(args[3]) & 0x3ffffffffff) >> 6) << 12;
		gfn = gpa >> PAGE_SHIFT;
		slot = gfn_to_memslot(vcpu->kvm, gfn);
		if(slot)
			hva1 = slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE;

		//Process GUEST odd pte
		gpa = ((pte_to_entrylo(args[2]) & 0x3ffffffffff) >> 6) << 12;
		gfn = gpa >> PAGE_SHIFT;
		slot = gfn_to_memslot(vcpu->kvm, gfn);
		if(slot)
			hva = slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE;

		ret = guest_pte_trans(args, vcpu, write_fault, pte_gpa, pte_gpa1);
		if(ret)
			kvm_info("translate gpa error\n");

		/*update software tlb
		*/
		vcpu->arch.guest_tlb[1].tlb_hi = (args[0] & 0xc000ffffffffe000);
		if (args[1] == 14)
			vcpu->arch.guest_tlb[1].tlb_mask = 0x7800; //normal pagesize 16KB
		else if (args[1] == 24)
			vcpu->arch.guest_tlb[1].tlb_mask = 0x1fff800; //huge pagesize 16MB

		if((hva >> args[1]) & 1)
			vcpu->arch.guest_tlb[1].tlb_lo[0] = pte_to_entrylo((pte_val(pte_gpa[1]) & 0xffffffffffff0000) | prot_bits1);
		else
			vcpu->arch.guest_tlb[1].tlb_lo[0] = pte_to_entrylo((pte_val(pte_gpa[0]) & 0xffffffffffff0000) | prot_bits1);

		if((hva1 >> args[1]) & 1)
			vcpu->arch.guest_tlb[1].tlb_lo[1] = pte_to_entrylo((pte_val(pte_gpa1[1]) & 0xffffffffffff0000) | prot_bits);
		else
			vcpu->arch.guest_tlb[1].tlb_lo[1] = pte_to_entrylo((pte_val(pte_gpa1[0]) & 0xffffffffffff0000) | prot_bits);

		if ((args[0] & 0xf000000000000000) == XKSEG) {
			vcpu->arch.guest_tlb[1].tlb_lo[0] |= 1;
			vcpu->arch.guest_tlb[1].tlb_lo[1] |= 1;
		}

		if ((args[0] & 0xf000000000000000) < XKSSEG)
			kvm_debug("%lx guest badvaddr %lx entryhi %lx guest pte %lx %lx pte %lx %lx tlb0 %lx tlb1 %lx\n",args[4], args[0],
					vcpu->arch.guest_tlb[1].tlb_hi, args[2], args[3],
					pte_val(pte_gpa[0]),pte_val(pte_gpa[1]),
					(unsigned long)pte_to_entrylo((pte_val(pte_gpa[0]) & 0xffffffffffff0000) | prot_bits1),
					(unsigned long)pte_to_entrylo((pte_val(pte_gpa[1]) & 0xffffffffffff0000) | prot_bits));
		if((args[4] != 0) && ((args[0] & 0xf000000000000000) < XKSSEG))
			kvm_debug("%lx guest badvaddr %lx entryhi %lx guest pte %lx %lx pte %lx %lx tlb0 %lx tlb1 %lx\n",args[4], args[0],
					vcpu->arch.guest_tlb[1].tlb_hi, args[2], args[3],
					pte_val(pte_gpa[0]),pte_val(pte_gpa[1]),
					(unsigned long)pte_to_entrylo((pte_val(pte_gpa[0]) & 0xffffffffffff0000) | prot_bits1),
					(unsigned long)pte_to_entrylo((pte_val(pte_gpa[1]) & 0xffffffffffff0000) | prot_bits));
	}
#endif

	/* Report unimplemented hypercall to guest */
//	*hret = -KVM_ENOSYS;
	return RESUME_GUEST;
}

static int kvm_mips_hypercall(struct kvm_vcpu *vcpu, unsigned long num,
		const unsigned long *args, unsigned long *hret)
{
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

int kvm_mips_handle_hypcall(struct kvm_vcpu *vcpu)
{
	unsigned long num, args[MAX_HYPCALL_ARGS];

	/* read hypcall number and arguments */
	num = vcpu->arch.gprs[2];	/* v0 */
	args[0] = vcpu->arch.gprs[4];	/* a0 badvaddr*/
	args[1] = vcpu->arch.gprs[5];	/* a1 PAGE_SHIFT */
	args[2] = vcpu->arch.gprs[6];	/* a2 even pte value*/
	args[3] = vcpu->arch.gprs[7];	/* a3 odd pte value*/
	args[4] = vcpu->arch.gprs[2];	/* tlb_miss/tlbl/tlbs/tlbm */
	args[5] = vcpu->arch.gprs[3];	/* EXCCODE/_TLBL/_TLBS/_MOD */

	return kvm_mips_hypercall(vcpu, num,
				  args, &vcpu->arch.gprs[2] /* v0 */);
}
