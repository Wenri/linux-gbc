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

#define MAX_HYPCALL_ARGS	4

enum vmtlbexc {
	VMTLBL = 2,
	VMTLBS = 3,
	VMTLBM = 4,
	VMTLBRI = 5,
	VMTLBXI = 6
};

extern int kvm_mips_map_page(struct kvm_vcpu *vcpu, unsigned long gpa,
				    bool write_fault,
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

int guest_pte_trans(int parity, const unsigned long *args,
			      struct kvm_vcpu *vcpu,
			      bool write_fault, pte_t *pte)
{
	int ret = 0;
	unsigned long gpa;
	int idx;

	/* The badvaddr we get maybe guest unmmaped or mmapped address,
	 * but not a GPA
	 * args[0] is badvaddr
	 * args[1] is pagemask
	 * args[2] is even pte value
	 * args[3] is odd pte value
	 * we get the guest pfn according the parameters,GPA-->HPA trans here
	*/
	//PFN is the PA over 12bits
	if(!parity)
		gpa = (args[2] >> 6) << 12;
	else
		gpa = (args[3] >> 6) << 12;

	idx = (args[0] >> args[1]) & 1;
	ret = kvm_mips_map_page(vcpu, gpa, write_fault, &pte[idx], &pte[!idx]);
//	if ((args[0] & 0xf000000000000000) == XKSEG)
//		pte[!idx].pte |= _PAGE_GLOBAL;
//	else
		pte[!idx].pte = 0;

	/* Guest userspace PTE's PAGE_GLOBAL bits should not be set */
	if (args[0] < XKSSEG)
		pte[idx].pte &= ~_PAGE_GLOBAL;

	if (ret)
		ret = RESUME_HOST;

	if ((args[0] & 0xf000000000000000) < XKSSEG)
	kvm_info("2 %s gpa %lx pte[%d] %lx pte[%d] %lx\n",__func__,
			gpa, idx, pte_val(pte[idx]), !idx, pte_val(pte[!idx]));

	return ret;
}

static int kvm_mips_hypercall(struct kvm_vcpu *vcpu, unsigned long num,
			      const unsigned long *args, unsigned long *hret)
{
#ifdef CONFIG_CPU_LOONGSON3
	int write_fault = 0;
	pte_t pte_gpa[2];
	int ret = 0;
	int parity = 0;
	u32 gsexccode = (read_gc0_cause() >> CAUSEB_EXCCODE) & 0x1f;

	//Distinct TLBL/TLBS/TLBM
	switch(gsexccode) {
	case VMTLBL:
		write_fault = 0;
		break;
	case VMTLBS:
		write_fault = 1;
		break;
	case VMTLBM:
		write_fault = 1;
		break;
	case VMTLBRI:
		break;
	case VMTLBXI:
		break;
	default:
		break;
	}

	if((args[0] >> args[1]) & 1) {
		parity = 1;
	} else {
		parity = 0;
	}
	ret = guest_pte_trans(parity, args, vcpu, write_fault, pte_gpa);


	if ((args[0] & 0xf000000000000000) < XKSSEG)
	kvm_info("3 guest entryhi %lx guest pfn %lx %lx pte %lx %lx\n", (args[0] & 0xc000ffffffffe000),
					 (args[2]>> 6), (args[3] >> 6),pte_val(pte_gpa[0]),pte_val(pte_gpa[1]));
	/*update software tlb
	*/
	vcpu->arch.guest_tlb[0].tlb_hi = (args[0] & 0xc000ffffffffe000) |
					 (read_gc0_entryhi() & KVM_ENTRYHI_ASID);
	vcpu->arch.guest_tlb[0].tlb_mask = 0x7800;
	vcpu->arch.guest_tlb[0].tlb_lo[0] = pte_to_entrylo(pte_val(pte_gpa[0]));
	vcpu->arch.guest_tlb[0].tlb_lo[1] = pte_to_entrylo(pte_val(pte_gpa[1]));
#endif

	/* Report unimplemented hypercall to guest */
//	*hret = -KVM_ENOSYS;
	return RESUME_GUEST;
}

int kvm_mips_handle_hypcall(struct kvm_vcpu *vcpu)
{
	unsigned long num, args[MAX_HYPCALL_ARGS];

	/* read hypcall number and arguments */
	/* organize parameters as follow
	 * a0        a1          a2         a3
	 *badvaddr  PAGE_SHIFT  even pte  odd pte
	 *
	*/

	num = vcpu->arch.gprs[2];	/* v0 */
	args[0] = vcpu->arch.gprs[4];	/* a0 badvaddr*/
	args[1] = vcpu->arch.gprs[5];	/* a1 PAGE_SHIFT */
	args[2] = vcpu->arch.gprs[6];	/* a2 even pte value*/
	args[3] = vcpu->arch.gprs[7];	/* a3 odd pte value*/

	if ((args[0] & 0xf000000000000000) < XKSSEG)
	kvm_info("1 guest badvaddr %lx pgshift %lu a0 %lx a1 %lx\n",
				 args[0],args[1],args[2],args[3]);

	return kvm_mips_hypercall(vcpu, num,
				  args, &vcpu->arch.gprs[2] /* v0 */);
}
