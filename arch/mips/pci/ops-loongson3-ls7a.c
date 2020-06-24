#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <asm/mips-boards/bonito64.h>

#include <loongson.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#define HT1LO_PCICFG_BASE      0x1a000000
#define HT1LO_PCICFG_BASE_TP1  0x1b000000
#ifdef CONFIG_KVM_GUEST_LS3A3000
#define HT1LO_EXT_PCICFG_BASE  0xef00000000UL
#define HT1LO_EXT_PCICFG_BASE_TP1  0xef10000000UL
#else
#define HT1LO_EXT_PCICFG_BASE  0xefe00000000UL
#define HT1LO_EXT_PCICFG_BASE_TP1  0xefe10000000UL
#endif
#define HT_CONFIG_REVISION	0xefdfe000108UL

#define PCI_byte_BAD 0
#define PCI_word_BAD (pos & 1)
#define PCI_dword_BAD (pos & 3)
#define PCI_FIND_CAP_TTL        48

#define PCI_OP_READ(size,type,len) \
static int pci_bus_read_config_##size##_nolock \
	(struct pci_bus *bus, unsigned int devfn, int pos, type *value) \
{                                                                       \
	int res;                                                        \
	u32 data = 0;                                                   \
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;       \
	res = bus->ops->read(bus, devfn, pos, len, &data);              \
	*value = (type)data;                                            \
	return res;                                                     \
}

PCI_OP_READ(byte, u8, 1)
PCI_OP_READ(word, u16, 2)
PCI_OP_READ(dword, u32, 4)

static int __pci_find_next_cap_ttl_nolock(struct pci_bus *bus, unsigned int devfn,
					  u8 pos, int cap, int *ttl)
{
	u8 id;
	u16 ent;

	pci_bus_read_config_byte_nolock(bus, devfn, pos, &pos);

	while ((*ttl)--) {
		if (pos < 0x40)
			break;
		pos &= ~3;
		pci_bus_read_config_word_nolock(bus, devfn, pos, &ent);

		id = ent & 0xff;
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = (ent >> 8);
	}
	return 0;
}

static int __pci_find_next_cap_nolock(struct pci_bus *bus, unsigned int devfn,
					u8 pos, int cap)
{
	int ttl = PCI_FIND_CAP_TTL;

	return __pci_find_next_cap_ttl_nolock(bus, devfn, pos, cap, &ttl);
}

static int __pci_bus_find_cap_start_nolock(struct pci_bus *bus,
					   unsigned int devfn, u8 hdr_type)
{
	u16 status;

	pci_bus_read_config_word_nolock(bus, devfn, PCI_STATUS, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	switch (hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
	case PCI_HEADER_TYPE_BRIDGE:
		return PCI_CAPABILITY_LIST;
	case PCI_HEADER_TYPE_CARDBUS:
		return PCI_CB_CAPABILITY_LIST;
	default:
		return 0;
	}

	return 0;
}

static int pci_bus_find_capability_nolock(struct pci_bus *bus, unsigned int devfn, int cap)
{
	int pos;
	u8 hdr_type;

	pci_bus_read_config_byte_nolock(bus, devfn, PCI_HEADER_TYPE, &hdr_type);

	pos = __pci_bus_find_cap_start_nolock(bus, devfn, hdr_type & 0x7f);
	if (pos)
		pos = __pci_find_next_cap_nolock(bus, devfn, pos, cap);

	return pos;
}

static int ls7a_pci_config_access(unsigned char access_type,
		struct pci_bus *bus, unsigned int devfn,
		int where, u32 *data)
{
	unsigned char busnum = bus->number;
	unsigned int ht_rev;
	u32 cur_mrrs, real_mrrs, min_mrrs;
	u_int64_t addr, type;
	void *addrp;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);
	int reg = where & ~3;
	int pos;

	if (busnum == 0 && device == 6 && function == 0 && reg == 0x20)
		return 0;
	if (busnum == 0) {

		/** Filter out non-supported devices.
		 *  device 2:misc  device 21:confbus
		 */
		if (device > 23)
			return PCIBIOS_DEVICE_NOT_FOUND;
		if (reg < 0x100) {
			addr = (device << 11) | (function << 8) | reg;
			addrp = (void *)(TO_UNCAC(HT1LO_PCICFG_BASE) | (addr & 0xffff));
		} else {
			addr = (device << 11) | (function << 8) | (reg & 0xff) | ((reg & 0xf00) << 16);
			addrp = (void *)(TO_UNCAC(HT1LO_EXT_PCICFG_BASE) | addr);
		}
		type = 0;

	} else {
		if (reg < 0x100) {
			addr = (busnum << 16) | (device << 11) | (function << 8) | reg;
			addrp = (void *)(TO_UNCAC(HT1LO_PCICFG_BASE_TP1) | (addr));
		} else {
			addr = (busnum << 16) | (device << 11) | (function << 8) | (reg & 0xff) | ((reg & 0xf00) << 16);
			addrp = (void *)(TO_UNCAC(HT1LO_EXT_PCICFG_BASE_TP1) | addr);
		}
		type = 0x10000;
	}

	if (access_type == PCI_ACCESS_WRITE) {
		pos = pci_bus_find_capability_nolock(bus, devfn, PCI_CAP_ID_EXP);

		if (pos != 0 && (pos + PCI_EXP_DEVCTL) == reg)
		{
			min_mrrs = cur_mrrs = *data & PCI_EXP_DEVCTL_READRQ;
			real_mrrs = (*(volatile unsigned int *)addrp) & PCI_EXP_DEVCTL_READRQ;

			if (cur_mrrs > real_mrrs) {
				min_mrrs = real_mrrs;
				*data = *data & ~PCI_EXP_DEVCTL_READRQ;
				*data = *data | real_mrrs;
			}

			if (min_mrrs > (1 << 12)) {
				ht_rev = *(volatile unsigned int *)TO_UNCAC(HT_CONFIG_REVISION) & 0xff;
				if (ht_rev == 0) {
					*data = *data & ~PCI_EXP_DEVCTL_READRQ;
					*data = *data | (1 << 12);
				}
			}
		}

		*(volatile unsigned int *)addrp = cpu_to_le32(*data);
	} else {
		*data = le32_to_cpu(*(volatile unsigned int *)addrp);
		if (busnum == 0 && reg == PCI_CLASS_REVISION && *data == 0x06000001)
			*data = (PCI_CLASS_BRIDGE_PCI << 16) | (*data & 0xffff);
		if (busnum == 0 && reg == 0x3c &&  (*data &0xff00) == 0)
			*data |= 0x100;

		if (*data == 0xffffffff) {
			*data = -1;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}
	return PCIBIOS_SUCCESSFUL;
}

static int ls7a_pci_pcibios_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 * val)
{
	u32 data = 0;
	int ret = ls7a_pci_config_access(PCI_ACCESS_READ,
			bus, devfn, where, &data);

	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int ls7a_pci_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	u32 data = 0;
	int ret;

	if (size == 4)
		data = val;
	else {
		ret = ls7a_pci_config_access(PCI_ACCESS_READ,
				bus, devfn, where, &data);
		if (ret != PCIBIOS_SUCCESSFUL)
			return ret;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
	}

	ret = ls7a_pci_config_access(PCI_ACCESS_WRITE,
			bus, devfn, where, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops loongson_ls7a_pci_ops = {
	.read = ls7a_pci_pcibios_read,
	.write = ls7a_pci_pcibios_write
};
