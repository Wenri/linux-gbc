/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>
#include <asm/dma-coherence.h>

#include <boot_param.h>
#include <linux/acpi.h>
#include <loongson.h>
#include <loongson-pch.h>
#include <asm/efi.h>
#include <workarounds.h>
#include <linux/dmi.h>
#include <asm/uasm.h>
#include <linux/efi.h>

#define SMBIOS_FREQLOW_MASK		0xFF
#define SMBIOS_FREQLOW_OFFSET		18
#define SMBIOS_FREQHIGH_OFFSET		19
#define SMBIOS_BOISSIZE_OFFSET		5
#define SMBIOS_CORE_PACGE_OFFSET	31
#define SMBIOS_BOISEXTERN_OFFSET        15
#define LOONGSON_EFI_ENABLE       	(1 << 3)

static char *product_name;
const char *loongson_cpuname;
static const char dmi_empty_string[] = "        ";

struct interface_info einter_smbios;
struct board_devices eboard_smbios;

extern struct plat_smp_ops loongson3_comp_smp_ops;
extern struct plat_smp_ops loongson3_smp_ops;
extern void __init prom_init_numa_memory(void);

extern u32 cpu_clock_freq;
extern char *bios_vendor;
extern char *bios_release_date;
extern void *loongson_fdt_blob;
extern char *board_manufacturer;
extern bool loongson_acpiboot_flag;
extern unsigned long loongson_max_dma32_pfn;

extern struct interface_info *einter;
extern struct board_devices *eboard;
extern struct platform_controller_hub ls2h_pch;
extern struct platform_controller_hub ls7a_pch;
extern struct platform_controller_hub rs780_pch;

extern unsigned int (*read_clear_ipi)(int);
extern unsigned int addr_read_clear_ipi(int cpu);
extern unsigned int csr_read_clear_ipi(int cpu);

static void __init mips_nmi_setup(void)
{
	void *base;
	extern char except_vec_nmi;

	base = (void *)(CAC_BASE + 0x380);
	memcpy(base, &except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

const char *dmi_string_parse(const struct dmi_header *dm, u8 s)
{
	const u8 *bp = ((u8 *) dm) + dm->length;

	if (s) {
		s--;
		while (s > 0 && *bp) {
			bp += strlen(bp) + 1;
			s--;
		}

		if (*bp != 0) {
			size_t len = strlen(bp)+1;
			size_t cmp_len = len > 8 ? 8 : len;

			if (!memcmp(bp, dmi_empty_string, cmp_len))
				return dmi_empty_string;
		return bp;
		}
	}

	return "";

}

static void __init parse_cpu_table(const struct dmi_header *dm)
{
	char *dmi_data = (char *)(dm + 1);
	int freq_temp = 0;
	int bios_extern;

	freq_temp = ((*(dmi_data + SMBIOS_FREQHIGH_OFFSET) << 8) + \
			((*(dmi_data + SMBIOS_FREQLOW_OFFSET)) & SMBIOS_FREQLOW_MASK));
	cpu_clock_freq = freq_temp * 1000000;
	cores_per_package = *(dmi_data + SMBIOS_CORE_PACGE_OFFSET);
	loongson_cpuname =  dmi_string_parse(dm, dmi_data[12]);

	bios_extern = *(dmi_data + SMBIOS_BOISEXTERN_OFFSET);
	if (bios_extern & LOONGSON_EFI_ENABLE)
		set_bit(EFI_BOOT, &loongson_efi_facility);
	else
		clear_bit(EFI_BOOT, &loongson_efi_facility);

	pr_info("CpuClock = %u\n", cpu_clock_freq);
}

static void __init parse_bios_table(const struct dmi_header *dm)
{
	char *dmi_data = (char *)(dm + 1);

	einter = &einter_smbios;
	einter_smbios.size = *(dmi_data + SMBIOS_BOISSIZE_OFFSET);
}

#ifdef CONFIG_EFI_PARTITION
static void __init parse_bios_extern(const struct dmi_header *dm)
{
	char *dmi_data = (char *)(dm + 1);
	int bios_extern;

	bios_extern = *(dmi_data + SMBIOS_BOISEXTERN_OFFSET);

	if (bios_extern & LOONGSON_EFI_ENABLE)
		set_bit(EFI_BOOT, &loongson_efi_facility);
	else
		clear_bit(EFI_BOOT, &loongson_efi_facility);
}

static void __init find_token_pmon(const struct dmi_header *dm, void *dummy)
{
	if (dm->type == 0)
		parse_bios_extern(dm);
}
#endif


static void __init find_tokens(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0x0: /* Extern BIOS */
		parse_bios_table(dm);
		break;
	case 0x4: /* Calling interface */
		parse_cpu_table(dm);
		break;
	}
}

static void __init smbios_parse(void)
{
	eboard = &eboard_smbios;
	bios_vendor = (void *)dmi_get_system_info(DMI_BIOS_VENDOR);
	strcpy(einter_smbios.description,dmi_get_system_info(DMI_BIOS_VERSION));
	bios_release_date = (void *)dmi_get_system_info(DMI_BIOS_DATE);
	board_manufacturer = (void *)dmi_get_system_info(DMI_BOARD_VENDOR);
	strcpy(eboard_smbios.name, dmi_get_system_info(DMI_BOARD_NAME));
	dmi_walk(find_tokens, NULL);
	product_name = (void *)dmi_get_system_info(DMI_PRODUCT_NAME);
	strsep(&product_name,"-");
	strsep(&product_name,"-");
	product_name = strsep(&product_name,"-");
	if (strstr(product_name, "7A1000")) {
		loongson_fdt_blob = __dtb_loongson3_ls7a_begin;
		loongson_pch = &ls7a_pch;
		loongson_ec_sci_irq = 0x07;
		loongson_max_dma32_pfn = 0x100000000ULL>> PAGE_SHIFT;
		ls_lpc_reg_base = 0x10002000;
	}else if (strstr(product_name, "RS780E")) {
		loongson_pch = &rs780_pch;
		loongson_ec_sci_irq = 0x07;
		loongson_max_dma32_pfn = 0x100000000ULL>> PAGE_SHIFT;
	}else {
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


}

static inline int loongson3_ccnuma_platform(void)
{
	if (nr_cpus_loongson <= 4)
		return 0;

	return 1;
}

void __init prom_init(void)
{
#ifdef CONFIG_ACPI
	acpi_gbl_use_default_register_widths = false;
#endif
	prom_init_cmdline();
	prom_init_env();

	/* init base address of io space */
	set_io_port_base((unsigned long)
		ioremap(LOONGSON_PCIIO_BASE, LOONGSON_PCIIO_SIZE));

	if (loongson_acpiboot_flag) {
#ifdef CONFIG_EFI
		efi_init();
#endif
		acpi_boot_table_init();
		acpi_boot_init();
		
	}

	if (!loongson3_ccnuma_platform()) {
#ifdef CONFIG_PHASE_LOCK
#define LRA	31
		unsigned int *p;
		/* optimize loongson3_phase_lock_acquire for SMP */
		p = (unsigned int *)&loongson3_phase_lock_acquire;
		uasm_i_jr(&p, LRA);
		uasm_i_nop(&p);

		/* optimize loongson3_phase_lock_release for SMP */
		p = (unsigned int *)&loongson3_phase_lock_release;
		uasm_i_jr(&p, LRA);
		uasm_i_nop(&p);
#endif
	}

#ifdef CONFIG_NUMA
	prom_init_numa_memory();
#else
	if (loongson_acpiboot_flag) {
		prom_init_memory_new();
	} else {
		prom_init_memory();
	}
#endif
#ifdef CONFIG_ACPI
	acpi_disabled = 1;
#endif
	dmi_scan_machine();
	dmi_set_dump_stack_arch_desc();
	if (loongson_acpiboot_flag)
		smbios_parse();

#ifdef CONFIG_EFI_PARTITION
	if (strstr(einter->description,"uefi") || strstr(einter->description,"UEFI") ||
		strstr(einter->description,"udk") || strstr(einter->description,"UDK"))
		set_bit(EFI_BOOT, &loongson_efi_facility);
	else if ((strstr(einter->description,"pmon")) || (strstr(einter->description,"PMON")))
		clear_bit(EFI_BOOT, &loongson_efi_facility);
	else
		dmi_walk(find_token_pmon, NULL);

#endif
	pr_info("The BIOS Version: %s\n",einter->description);

	if (loongson_pch)
		loongson_pch->early_config();
	/*init the uart base address */
	prom_init_uart_base();
#if defined(CONFIG_SMP)
	if ((current_cpu_type() == CPU_LOONGSON3_COMP) &&
		(read_csr(LOONGSON_CPU_FEATURE_OFFSET) & LOONGSON_CPU_FEATURE_IPI_PERCORE)) {
		read_clear_ipi = csr_read_clear_ipi;
		register_smp_ops(&loongson3_comp_smp_ops);
	} else {
		read_clear_ipi = addr_read_clear_ipi;
		register_smp_ops(&loongson3_smp_ops);
	}
#endif
	board_nmi_handler_setup = mips_nmi_setup;
#ifdef CONFIG_CPU_LOONGSON3
	if (!hw_coherentio) {
		/* set HT-access uncache */
		switch (cputype) {
		case Legacy_3A:
		case Loongson_3A:
			if (loongson_pch->board_type == LS7A) {
				HT_uncache_enable_reg0	= HT_cache_enable_reg1; //for 7a gpu
				HT_uncache_base_reg0	= HT_cache_base_reg1;
			} else {
				HT_uncache_enable_reg0	= 0xc0000000; //Low 256M
				HT_uncache_base_reg0	= 0x0080fff0;
			}
			HT_uncache_enable_reg1	= 0xc0000000; //Node 0
			HT_uncache_base_reg1	= 0x0000e000;
			HT_uncache_enable_reg2	= 0xc0100000; //Node 1
			HT_uncache_base_reg2	= 0x2000e000;
			HT_uncache_enable_reg3	= 0xc0200000; //Node 2/3
			HT_uncache_base_reg3	= 0x4000c000;
			writeq(0x0000202000000000, (void *)0x900000003ff02708);
			writeq(0xffffffe000000000, (void *)0x900000003ff02748);
			writeq(0x0000300000000086, (void *)0x900000003ff02788);
			break;
		case Legacy_3B:
		case Loongson_3B:
			HT_uncache_enable_reg0	= 0xc0000000;
			HT_uncache_base_reg0	= 0x0080fff0;
			HT_uncache_enable_reg1	= 0xc0000000;
			HT_uncache_base_reg1	= 0x00008000;
			break;
		default:
			break;
		}
	} else {
		/* set HT-access cache */
		switch (cputype) {
		case Legacy_3A:
		case Loongson_3A:
			HT_uncache_enable_reg0	= 0x0;
			HT_uncache_enable_reg1	= 0x0;
			HT_uncache_enable_reg2	= 0x0;
			HT_uncache_enable_reg3	= 0x0;
			break;
		case Legacy_3B:
		case Loongson_3B:
			HT_uncache_enable_reg0	= 0x0;
			HT_uncache_enable_reg1	= 0x0;
			break;
		default:
			break;
		}
	}
	__sync();
#endif /* CONFIG_CPU_LOONGSON3 */
}

void __init prom_free_prom_memory(void)
{
}
