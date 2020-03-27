#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <asm/mips-boards/bonito64.h>

#include <loongson.h>

#define VZ_HT1LO_PCICFG_BASE		0x1a000000
#define VZ_HT1LO_PCICFG_BASE_TP1	0x1b000000

static int ls7a_pci_vz_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 * val)
{
	unsigned char busnum = bus->number;
	u_int64_t addr;
	void *addrp;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);

	if (where >= 0x100) {
		*val = -1;
		printk(KERN_ERR "ls7a_pci_vz_read read bus%d,slot%d,fun%d,reg%d\n",busnum,device,function,where);
		return PCIBIOS_SUCCESSFUL;
	}

	addr = (busnum << 16) | (device << 11) | (function << 8) | where;
	addrp = (void *)(TO_UNCAC(VZ_HT1LO_PCICFG_BASE) | (addr));

	if(size == 4){
		*val = le32_to_cpu(*(volatile unsigned int *)addrp);
	}else if(size == 2){
		*val = (unsigned short)(*(volatile unsigned short *)addrp);
	}else if(size == 1){
		*val = (unsigned char)(*(volatile unsigned char *)addrp);
	}

	return PCIBIOS_SUCCESSFUL;
}

static int ls7a_pci_vz_write(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	unsigned char busnum = bus->number;
	u_int64_t addr;
	void *addrp;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);

	if (where >= 0x100) {
		printk(KERN_ERR "ls7a_pci_vz_write read bus%d,slot%d,fun%d,reg%d\n",busnum,device,function,where);
		return PCIBIOS_SUCCESSFUL;
	}

	addr = (busnum << 16) | (device << 11) | (function << 8) | where;
	addrp = (void *)(TO_UNCAC(VZ_HT1LO_PCICFG_BASE) | (addr));

	if(size == 4){
		*(volatile unsigned int *)addrp = cpu_to_le32(val);
	}else if(size == 2){
		*(volatile unsigned short *)addrp = (unsigned short )cpu_to_le32(val);
	}else if(size == 1){
		*(volatile unsigned char *)addrp = (unsigned char)cpu_to_le32(val);
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops vz_ls7a_pci_ops = {
	.read = ls7a_pci_vz_read,
	.write = ls7a_pci_vz_write
};
