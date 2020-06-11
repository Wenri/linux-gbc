/*
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2003 ICT CAS
 * Author: Michael Guo <guoyi@ict.ac.cn>
 *
 * Copyright (C) 2007 Lemote Inc. & Insititute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <asm/bootinfo.h>
#include <asm/dma-coherence.h>
#include <loongson.h>
#include <boot_param.h>
#include <workarounds.h>
#include <loongson-pch.h>
#include <linux/efi.h>
#include <linux/acpi.h>

struct boot_params *boot_p;
struct loongson_params *loongson_p;

bool loongson_acpiboot_flag;
struct efi_cpuinfo_loongson *ecpu;
struct efi_memory_map_loongson *emap;
struct system_loongson *esys;
struct board_devices *eboard;
struct irq_source_routing_table *eirq_source;
struct bootparamsinterface *efi_bp;

u32 cpu_guestmode;
u64 ht_control_base;
u64 pci_mem_start_addr, pci_mem_end_addr;
u64 loongson_pciio_base;
u64 ls_lpc_reg_base = LS2H_LPC_REG_BASE;
u64 vgabios_addr;
u64 poweroff_addr, restart_addr, suspend_addr;
u64 low_physmem_start, high_physmem_start;
u32 gpu_brust_type;
u32 vram_type;
u64 uma_vram_addr;
u64 uma_vram_size = 0;
u64 vuma_vram_addr;
u64 vuma_vram_size;

u64 loongson_chipcfg[MAX_PACKAGES] = {0xffffffffbfc00180};
u64 loongson_chiptemp[MAX_PACKAGES];
u64 loongson_freqctrl[MAX_PACKAGES];

unsigned long long smp_group[4];
void *loongson_fdt_blob;

enum loongson_cpu_type cputype;
u16 loongson_boot_cpu_id;
u16 loongson_reserved_cpus_mask;
u32 nr_cpus_loongson = NR_CPUS;
u32 possible_cpus_loongson = NR_CPUS;
u32 nr_nodes_loongson = MAX_NUMNODES;
int cores_per_node;
int cores_per_package;
unsigned int has_systab = 0;
unsigned long systab_addr;

u32 loongson_dma_mask_bits;
u64 loongson_workarounds;
u32 loongson_ec_sci_irq;
char loongson_ecname[32];
u32 loongson_nr_uarts;
struct uart_device loongson_uarts[MAX_UARTS];
u32 loongson_nr_sensors;
struct sensor_device loongson_sensors[MAX_SENSORS];
u32 loongson_hwmon;
int loongson3_perf_irq_mask = 1;

struct platform_controller_hub *loongson_pch;
extern struct platform_controller_hub ls2h_pch;
extern struct platform_controller_hub ls7a_pch;
extern struct platform_controller_hub rs780_pch;

struct interface_info *einter;
struct loongson_special_attribute *especial;

struct loongsonlist_vbios *pvbios;
struct loongsonlist_mem_map *loongson_mem_map;
extern char *bios_vendor;
extern char *bios_release_date;
extern char *board_manufacturer;
extern char _bios_info[];
extern char _board_info[];
extern char *loongson_cpuname;

unsigned long loongson_max_dma32_pfn;
u32 cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);
EXPORT_SYMBOL(loongson_ec_sci_irq);
EXPORT_SYMBOL(loongson_pch);

extern int mips_vint_enabled;

#define parse_even_earlier(res, option, p)				\
do {									\
	unsigned int tmp __maybe_unused;				\
									\
	if (strncmp(option, (char *)p, strlen(option)) == 0)		\
		tmp = kstrtou32((char *)p + strlen(option"="), 10, &res); \
} while (0)

static bool has_cpu_uart1(void)
{
	int i;
	for (i = 0; i < esys->nr_uarts; i++) {
		if (current_cpu_type() == CPU_LOONGSON3_COMP &&
				esys->uarts[i].uart_base == LOONGSON_REG_BASE + LOONGSON_UART1_OFFSET)
			return true;
	}
	return false;
}

bool need_cpu_uart1;
void __init no_efiboot_env(void)
{
	/* pmon passes arguments in 32bit pointers */
	unsigned int processor_id;
	char *bios_info __maybe_unused;
	char *board_info __maybe_unused;

#ifndef CONFIG_UEFI_FIRMWARE_INTERFACE
	int *_prom_envp;
	long l;
	extern u32 memsize, highmemsize;

	/* firmware arguments are initialized in head.S */
	_prom_envp = (int *)fw_arg2;

	l = (long)*_prom_envp;
	while (l != 0) {
		parse_even_earlier(cpu_clock_freq, "cpuclock", l);
		parse_even_earlier(memsize, "memsize", l);
		parse_even_earlier(highmemsize, "highmemsize", l);
		_prom_envp++;
		l = (long)*_prom_envp;
	}
	if (memsize == 0)
		memsize = 256;
#else
	int i;

	/* firmware arguments are initialized in head.S */
	boot_p = (struct boot_params *)fw_arg2;
	loongson_p = &(boot_p->efi.smbios.lp);

	esys	= (struct system_loongson *)((u64)loongson_p + loongson_p->system_offset);
	ecpu	= (struct efi_cpuinfo_loongson *)((u64)loongson_p + loongson_p->cpu_offset);
	emap	= (struct efi_memory_map_loongson *)((u64)loongson_p + loongson_p->memory_offset);
	eboard	= (struct board_devices *)((u64)loongson_p + loongson_p->boarddev_table_offset);
	einter	= (struct interface_info *)((u64)loongson_p + loongson_p->interface_offset);
	eirq_source = (struct irq_source_routing_table *)((u64)loongson_p + loongson_p->irq_offset);
	especial = (struct loongson_special_attribute *)((u64)loongson_p + loongson_p->special_offset);

	cputype = ecpu->cputype;
	switch (cputype) {
	case Legacy_3A:
	case Loongson_3A:
		cores_per_node = 4;
		cores_per_package = 4;
#ifdef CONFIG_KVM_GUEST_LS3A3000
		for(i = 0; i < 4; i++) {
			smp_group[i] = 0x900000003ff01000 | nid_to_addroffset(i);
			loongson_chipcfg[i] = 0x900000001fe00180 | nid_to_addroffset(i);
			loongson_chiptemp[i] = 0x900000001fe0019c | nid_to_addroffset(i);
			loongson_freqctrl[i] = 0x900000001fe001d0 | nid_to_addroffset(i);
		}
		ht_control_base = 0x900000EFFB000000;
#else

		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff01000;
		smp_group[2] = 0x900020003ff01000;
		smp_group[3] = 0x900030003ff01000;
		
		ht_control_base = 0x90000EFDFB000000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900010001fe00180;
		loongson_chipcfg[2] = 0x900020001fe00180;
		loongson_chipcfg[3] = 0x900030001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900010001fe0019c;
		loongson_chiptemp[2] = 0x900020001fe0019c;
		loongson_chiptemp[3] = 0x900030001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900010001fe001d0;
		loongson_freqctrl[2] = 0x900020001fe001d0;
		loongson_freqctrl[3] = 0x900030001fe001d0;
#endif
		loongson_workarounds = WORKAROUND_CPUFREQ;
		break;
	case Legacy_3B:
	case Loongson_3B:
		cores_per_node = 4; /* Loongson 3B has two node in one package */
		cores_per_package = 8;
		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff05000;
		smp_group[2] = 0x900020003ff09000;
		smp_group[3] = 0x900030003ff0d000;
		ht_control_base = 0x90001EFDFB000000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900020001fe00180;
		loongson_chipcfg[2] = 0x900040001fe00180;
		loongson_chipcfg[3] = 0x900060001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900020001fe0019c;
		loongson_chiptemp[2] = 0x900040001fe0019c;
		loongson_chiptemp[3] = 0x900060001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900020001fe001d0;
		loongson_freqctrl[2] = 0x900040001fe001d0;
		loongson_freqctrl[3] = 0x900060001fe001d0;
		loongson_workarounds = WORKAROUND_CPUHOTPLUG;
		break;
	default:
		cores_per_node = 1;
		cores_per_package = 1;
		loongson_chipcfg[0] = 0x900000001fe00180;
	}

	nr_cpus_loongson = ecpu->nr_cpus;
	cpu_clock_freq = ecpu->cpu_clock_freq;
	loongson_boot_cpu_id = ecpu->cpu_startup_core_id;
	loongson_reserved_cpus_mask = ecpu->reserved_cores_mask;
	nr_nodes_loongson = ecpu->total_node;
#ifdef CONFIG_KEXEC
	loongson_boot_cpu_id = get_core_id();
	for (i = 0; i < loongson_boot_cpu_id; i++)
		loongson_reserved_cpus_mask |= (1<<i);
	pr_info("Boot CPU ID is being fixed from %d to %d\n",
		ecpu->cpu_startup_core_id, loongson_boot_cpu_id);
#endif
	if (nr_cpus_loongson > NR_CPUS || nr_cpus_loongson == 0)
		nr_cpus_loongson = NR_CPUS;
	if ((nr_nodes_loongson*cores_per_node) < nr_cpus_loongson) {
		nr_nodes_loongson = (nr_cpus_loongson + cores_per_node - 1) / cores_per_node;
		pr_err("node num %d does not match nrcpus %d \n", 
			nr_nodes_loongson, nr_cpus_loongson);
	}
	possible_cpus_loongson = nr_nodes_loongson * cores_per_node;

	pci_mem_start_addr = eirq_source->pci_mem_start_addr;
	pci_mem_end_addr = eirq_source->pci_mem_end_addr;
	loongson_pciio_base = eirq_source->pci_io_start_addr;
	loongson_dma_mask_bits = eirq_source->dma_mask_bits;

	if (loongson_dma_mask_bits < 32 || loongson_dma_mask_bits > 64)
		loongson_dma_mask_bits = 32;

	if (((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R2_0)
		|| ((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R3_0)) {
		eirq_source->dma_noncoherent = 1;
		loongson3_perf_irq_mask = 0;
	}
	if (strstr(arcs_cmdline, "cached"))
		eirq_source->dma_noncoherent = 0;
	if (strstr(arcs_cmdline, "uncached"))
		eirq_source->dma_noncoherent = 1;

	if (strstr(arcs_cmdline, "hwmon"))
		loongson_hwmon = 1;
	else
		loongson_hwmon = 0;

	hw_coherentio = !eirq_source->dma_noncoherent;
	pr_info("BIOS configured I/O coherency: %s\n", hw_coherentio?"ON":"OFF");

	if (strstr(eboard->name,"2H")) {
		int ls2h_board_ver;

		ls2h_board_ver = ls2h_readl(LS2H_GPIO_IN_REG);
		ls2h_board_ver = (ls2h_board_ver >> 8) & 0xf;

		if (ls2h_board_ver == LS3A2H_BOARD_VER_2_2) {
			loongson_pciio_base = 0x1bf00000;
			ls_lpc_reg_base = LS2H_LPC_REG_BASE;
		} else {
			loongson_pciio_base = 0x1ff00000;
			ls_lpc_reg_base = LS3_LPC_REG_BASE;
		}

		loongson_pch = &ls2h_pch;
		loongson_ec_sci_irq = 0x80;
		loongson_max_dma32_pfn = 0x180000000ULL>> PAGE_SHIFT;
		loongson_workarounds |= WORKAROUND_PCIE_DMA;
	}
	else if (strstr(eboard->name,"7A")) {
		loongson_pch = &ls7a_pch;
		loongson_ec_sci_irq = 0x07;
		loongson_fdt_blob = __dtb_loongson3_ls7a_begin;
		if (strstr(eboard->name,"2way")) {
			eirq_source->dma_noncoherent = 0;
			hw_coherentio = 1;
			pr_info("Board [%s] detected, **coherent dma** is unconditionally used!\n", eboard->name);
		}
		if (esys->vers >= 2 && esys->of_dtb_addr)
			loongson_fdt_blob = (void *)(esys->of_dtb_addr);
		loongson_max_dma32_pfn = 0x100000000ULL>> PAGE_SHIFT;
		ls_lpc_reg_base = 0x10002000;
	}
	else {
		loongson_pch = &rs780_pch;
		loongson_ec_sci_irq = 0x07;
		loongson_max_dma32_pfn = 0x100000000ULL>> PAGE_SHIFT;
	}

	/* parse bios info */
	strcpy(_bios_info, einter->description);
	bios_info = _bios_info;
	bios_vendor = strsep(&bios_info, "-");
	strsep(&bios_info, "-");
	strsep(&bios_info, "-");
	bios_release_date = strsep(&bios_info, "-");
	if (!bios_release_date)
	bios_release_date = especial->special_name;

	/* parse board info */
	strcpy(_board_info, eboard->name);
	board_info = _board_info;
	board_manufacturer = strsep(&board_info, "-");

	poweroff_addr = boot_p->reset_system.Shutdown;
	restart_addr = boot_p->reset_system.ResetWarm;
	suspend_addr = boot_p->reset_system.DoSuspend;
	pr_info("Shutdown Addr: %llx Reset Addr: %llx\n", poweroff_addr, restart_addr);

	vgabios_addr = boot_p->efi.smbios.vga_bios;

	memset(loongson_ecname, 0, 32);
	if (esys->has_ec)
		memcpy(loongson_ecname, esys->ec_name, 32);
	loongson_workarounds |= esys->workarounds;

#if defined(CONFIG_LOONGSON_EA_PM_HOTKEY)
	if (strstr(eboard->name,"L39") || strstr(eboard->name,"L41"))
		loongson_workarounds |= WORKAROUND_LVDS_EA_EC;
#endif

	need_cpu_uart1 = has_cpu_uart1();
	loongson_nr_uarts = esys->nr_uarts ? esys->nr_uarts : 1;
	if (loongson_nr_uarts > MAX_UARTS)
		loongson_nr_uarts = 1;
	memcpy(loongson_uarts, esys->uarts,
		sizeof(struct uart_device) * loongson_nr_uarts);

	loongson_nr_sensors = esys->nr_sensors;
	if (loongson_nr_sensors > MAX_SENSORS)
		loongson_nr_sensors = 0;
	if (loongson_nr_sensors)
		memcpy(loongson_sensors, esys->sensors,
			sizeof(struct sensor_device) * loongson_nr_sensors);
#endif
	if (cpu_clock_freq == 0) {
		processor_id = (&current_cpu_data)->processor_id;
		switch (processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			cpu_clock_freq = 533080000;
			break;
		case PRID_REV_LOONGSON2F:
			cpu_clock_freq = 797000000;
			break;
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			cpu_clock_freq = 900000000;
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			cpu_clock_freq = 1000000000;
			break;
		default:
			cpu_clock_freq = 100000000;
			break;
		}
	}
	pr_info("CpuClock = %u\n", cpu_clock_freq);

	cpu_guestmode = (!cpu_has_vz && (((read_c0_prid() & 0xffff) >=
		(PRID_IMP_LOONGSON2 | PRID_REV_LOONGSON3A_R2_0)) ||
		((read_c0_prid() & 0xffff) == 0)));

#if defined( CONFIG_VIRTUALIZATION) || defined(CONFIG_KVM_GUEST_LS3A3000)
	if ((read_c0_prid() & 0xffff) >= 0x6308) {
		mips_vint_enabled = 0;
		pr_info("In Guest mode, disable mips vint\n");
	}
#endif
}

u8 ext_listhdr_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end) {
		sum = (u8)(sum + *(buffer++));
	}

	return (sum);
}
int parse_mem(struct _extention_list_hdr *head)
{
	loongson_mem_map = (struct loongsonlist_mem_map *)head;
	if (ext_listhdr_checksum((u8 *)loongson_mem_map, head->length)) {
		prom_printf("mem checksum error\n");
		return -EPERM;
	}
	return 0;
}


int parse_vbios(struct _extention_list_hdr *head)
{
	pvbios = (struct loongsonlist_vbios *)head;

	if (ext_listhdr_checksum((u8 *)pvbios, head->length)) {
		prom_printf("vbios_addr checksum error\n");
		return -EPERM;
	} else {
		vgabios_addr = pvbios->vbios_addr;
	}

	return 0;
}

static int list_find(struct _extention_list_hdr *head)
{
	struct _extention_list_hdr *fhead = head;

	if (fhead == NULL) {
		prom_printf("the link is empty!\n");
		return -1;
	}

	while(fhead != NULL) {
		if (memcmp(&(fhead->signature), LOONGSON_MEM_LINKLIST, 3) == 0) {
			if (parse_mem(fhead) !=0) {
				prom_printf("parse mem failed\n");
				return -EPERM;
			}
		} else if (memcmp(&(fhead->signature), LOONGSON_VBIOS_LINKLIST, 5) == 0) {
			if (parse_vbios(fhead) != 0) {
				prom_printf("parse vbios failed\n");
				return -EPERM;
			}
		}
		fhead = fhead->next;
	}
	return 0;

}
extern void init_suspend_addr(void);
void __init prom_init_env(void)
{
#ifdef CONFIG_KVM_GUEST_LS3A3000
	int i;
#endif
	efi_bp = (struct bootparamsinterface *)fw_arg2;

	if (memcmp(&(efi_bp->signature), LOONGSON_EFIBOOT_SIGNATURE, 3) != 0) {
		no_efiboot_env();
	}else {
#ifdef CONFIG_KVM_GUEST_LS3A3000
		for(i = 0; i < 4; i++) {
			smp_group[i] = 0x900000003ff01000 | nid_to_addroffset(i);
			loongson_chipcfg[i] = 0x900000001fe00180 | nid_to_addroffset(i);
			loongson_chiptemp[i] = 0x900000001fe0019c | nid_to_addroffset(i);
			loongson_freqctrl[i] = 0x900000001fe001d0 | nid_to_addroffset(i);
		}
		ht_control_base = 0x900000EFFB000000;
#else
		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff01000;
		smp_group[2] = 0x900020003ff01000;
		smp_group[3] = 0x900030003ff01000;

		ht_control_base = 0x90000EFDFB000000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900010001fe00180;
		loongson_chipcfg[2] = 0x900020001fe00180;
		loongson_chipcfg[3] = 0x900030001fe00180;

		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900010001fe0019c;
		loongson_chiptemp[2] = 0x900020001fe0019c;
		loongson_chiptemp[3] = 0x900030001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900010001fe001d0;
		loongson_freqctrl[2] = 0x900020001fe001d0;
		loongson_freqctrl[3] = 0x900030001fe001d0;
#endif

		cpu_guestmode = (!cpu_has_vz && (((read_c0_prid() & 0xffff) >=
			(PRID_IMP_LOONGSON2 | PRID_REV_LOONGSON3A_R2_0)) ||
			((read_c0_prid() & 0xffff) == 0)));

		loongson_workarounds = WORKAROUND_CPUFREQ;
		hw_coherentio = 1;
		loongson_nr_uarts = 1;
		loongson_acpiboot_flag = 1;
#ifdef CONFIG_ACPI
		acpi_disabled = 0;
#endif
		pci_mem_start_addr = PCI_MEM_START_ADDR;
		pci_mem_end_addr = PCI_MEM_END_ADDR;
		loongson_pciio_base = LOONGSON_PCI_IOBASE;
		loongson_dma_mask_bits = LOONGSON_DMA_MASK_BIT;
		init_suspend_addr();

		if (((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R2_0)
		|| ((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A_R3_0))
			loongson3_perf_irq_mask = 0;

		if (strstr(arcs_cmdline, "hwmon"))
			loongson_hwmon = 1;
		else
			loongson_hwmon = 0;

		if (list_find(efi_bp->extlist))
			prom_printf("Scan bootparm failed\n");

	}


}

static int __init init_cpu_fullname(void)
{
	int cpu;

	/* get the __cpu_full_name from bios */
	if (ecpu) {
		if((ecpu->vers > 1) && (ecpu->cpuname[0] != 0))
			for(cpu = 0; cpu < NR_CPUS; cpu++)
				__cpu_full_name[cpu] = ecpu->cpuname;
	} else {
		if (loongson_acpiboot_flag == 1) {
			for(cpu = 0; cpu < NR_CPUS; cpu++)
				__cpu_full_name[cpu] = loongson_cpuname;
		}
	}
	return 0;
}
arch_initcall(init_cpu_fullname);
