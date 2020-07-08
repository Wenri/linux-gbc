/*
 * EFI partition
 *
 * Just for ACPI here, complete it when implementing EFI runtime.
 *
 * lvjianmin: <lvjianmin@loongson.cn>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/bcd.h>

#include <linux/acpi.h>
#include <asm/efi.h>
#include <boot_param.h>
extern struct bootparamsinterface *efi_bp;
unsigned long mips_efi_facility;
extern unsigned int has_systab;
extern unsigned long systab_addr;
extern unsigned long loongson_efi_facility;

static efi_config_table_type_t arch_tables[] __initdata = {
    	{NULL_GUID, NULL, NULL},
};

/*
 * Returns 1 if 'facility' is enabled, 0 otherwise.
 */
int efi_enabled(int facility)
{
	return test_bit(facility, &mips_efi_facility) != 0;
}
EXPORT_SYMBOL(efi_enabled);

static int __init efi_systab_init(void)
{
	efi.systab = (efi_system_table_t *)efi_bp->systemtable;
	if (efi.systab == NULL) {
		panic("Whoa! Can't find EFI system table.\n");
		return 1;
	}

	set_bit(EFI_64BIT, &mips_efi_facility);
	efi.config_table = (unsigned long)efi.systab->tables;
	efi.runtime  = (unsigned long)efi.systab->runtime;

	if (efi_config_init(arch_tables))
		return 1;
	if (efi.smbios3 != EFI_INVALID_TABLE_ADDR)
		systab_addr =  efi.smbios3;
	else if (efi.smbios != EFI_INVALID_TABLE_ADDR)
		systab_addr =  efi.smbios;
	else {
		pr_err("%s : ERROR: smbios addr invaild\n",__func__);
		return 1;
	}
	has_systab = 1;

	set_bit(EFI_BOOT, &loongson_efi_facility);
	set_bit(EFI_CONFIG_TABLES, &mips_efi_facility);
	return 0;
}

void __init efi_init(void)
{
	if (efi_systab_init())
		return;
}
