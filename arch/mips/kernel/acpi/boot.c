/*
 *  boot.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 *  Lvjianmin <lvjianmin@loongson.cn>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/acpi_pmtmr.h>
#include <linux/efi.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/memblock.h>

#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/numa.h>
#include <loongson.h>

static unsigned int num_processors;
int acpi_disabled = 0;
EXPORT_SYMBOL(acpi_disabled);
int acpi_noirq;				/* skip ACPI IRQ initialization */
int acpi_pci_disabled;		/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);
int acpi_strict;
#define BAD_MADT_ENTRY(entry, end) (					    \
		(!entry) || (unsigned long)entry + sizeof(*entry) > end ||  \
		((struct acpi_subtable_header *)entry)->length < sizeof(*entry))

# define MAX_LOCAL_APIC 256

#define PREFIX			"ACPI: "
/*
 * The default interrupt routing model is PIC (8259).  This gets
 * overridden if IOAPICs are enumerated (below).
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_PIC;
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{

	if (!phys || !size)
		return NULL;

	return early_ioremap(phys, size);
}
void __init __acpi_unmap_table(char *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_iounmap(map, size);
}
/*
 * Following __acpi_xx functions should be implemented for sepecific cpu. */
int acpi_gsi_to_irq(u32 gsi, unsigned int *irqp)
{
	if (irqp != NULL) *irqp = gsi;
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

int acpi_isa_irq_to_gsi(unsigned isa_irq, u32 *gsi)
{
	if (gsi) *gsi = isa_irq;
	return 0;
}
int mips_acpi_suspend_lowlevel(void)
{
	return 0;
}
#ifdef CONFIG_ACPI_SLEEP
int (*acpi_suspend_lowlevel)(void) = mips_acpi_suspend_lowlevel;
#else
int (*acpi_suspend_lowlevel)(void);
#endif

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger, int polarity)
{
	return gsi;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{

}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

/*
 *  ACPI based hotplug support for CPU
 */
#ifdef CONFIG_ACPI_HOTPLUG_CPU
#include <acpi/processor.h>

/* wrapper to silence section mismatch warning */
int __ref acpi_map_lsapic(acpi_handle handle, int physid, int *pcpu)
{
	return 0;
}
EXPORT_SYMBOL(acpi_map_lsapic);

int acpi_unmap_lsapic(int cpu)
{
	return 0;
}

EXPORT_SYMBOL(acpi_unmap_lsapic);
#endif				/* CONFIG_ACPI_HOTPLUG_CPU */
void __init acpi_boot_table_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return;

	/*
	 * Initialize the ACPI boot-time table parser.
	 */
	if (acpi_table_init()) {
		disable_acpi();
		return;
	}
}

static void set_processor_mask(struct acpi_madt_local_apic *processor)
{

	int cpu;
	int cpuid = processor->id;

	if(!cpu_guestmode && (!(processor->lapic_flags & ACPI_MADT_ENABLED))) {
		return;
	}

	if (num_processors >= nr_cpu_ids) {
		pr_warning(
			"acpi: nr_cpus/possible_cpus limit of %i reached."
			"  processor 0x%x ignored.\n", nr_cpu_ids, cpuid);

	}
	if (cpuid == loongson_boot_cpu_id) {
		cpu = 0;
	} else
		cpu = cpumask_next_zero(-1, cpu_present_mask);

	__cpu_number_map[cpuid] = cpu;
	__cpu_logical_map[cpu] = cpuid;
	set_cpu_possible(cpu, true);
	if (processor->lapic_flags & ACPI_MADT_ENABLED) {
		set_cpu_present(cpu, true);
		num_processors++;
	}
	loongson_reserved_cpus_mask &= (~(1 << cpuid));
}


static int __init
acpi_parse_lapic(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_local_apic *processor = NULL;
	processor = (struct acpi_madt_local_apic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	set_processor_mask(processor);

	return 0;
}

static int __init acpi_parse_madt_lapic_entries(void)
{
	int ret;
	struct acpi_subtable_proc madt_proc[1];

	memset(madt_proc, 0, sizeof(madt_proc));
	madt_proc[0].id = ACPI_MADT_TYPE_LOCAL_APIC;
	madt_proc[0].handler = acpi_parse_lapic;
	ret = acpi_table_parse_entries_array(ACPI_SIG_MADT,
				sizeof(struct acpi_table_madt),
				madt_proc, ARRAY_SIZE(madt_proc), MAX_LOCAL_APIC);
	if (ret < 0) {
		printk(KERN_ERR PREFIX
					"Error parsing LAPIC entries\n");
		return ret;
	}

	return 0;
}

static void __init acpi_process_madt(void)
{
	int i, error;

	for (i = 0; i < NR_CPUS; i++) {
		__cpu_number_map[i] = -1;
		__cpu_logical_map[i] = -1;
	}
	loongson_reserved_cpus_mask = 0xFFFF;

	/*
	 * Parse MADT LAPIC entries
	 */
	error = acpi_parse_madt_lapic_entries();
	if (!error) {
		/*
		 * Parse MADT IO-APIC entries
		 */
	}
	if (error == -EINVAL) {
		printk(KERN_ERR PREFIX
			       "Invalid BIOS MADT, disabling ACPI\n");
		disable_acpi();
	}

	nr_cpus_loongson = num_processors;
	return;
}

int __init acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return 1;

	loongson_boot_cpu_id = get_core_id();


	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();
	return 0;
}

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0) {
		disable_acpi();
	} else {
		/* Core will printk when we return error. */
		return -EINVAL;
	}
	return 0;
}
early_param("acpi", parse_acpi);

/*
 * MIPS has no SMM, and bios will not acquire
 * the global lock to do something when kernel running, so
 * don't have to implement global lock.
 * __acpi_acquire_global_lock
 * will always return -1 indicating owning the lock.
 *
 * __acpi_release_global_lock will always return 0 indicating
 * no acquring request pending.
 *
 * */
int __acpi_acquire_global_lock(unsigned int *lock)
{
	return -1;
}

int __acpi_release_global_lock(unsigned int *lock)
{
	return 0;
}

#ifdef CONFIG_ACPI_NUMA
int acpi_numa __initdata;

static __init int setup_node(int pxm)
{
	return acpi_map_pxm_to_node(pxm);
}

static __init void bad_srat(void)
{
	printk(KERN_ERR "SRAT: SRAT not used.\n");
	acpi_numa = -1;
}

static __init inline int srat_disabled(void)
{
	return acpi_numa < 0;
}

/*
 * Callback for SLIT parsing.  pxm_to_node() returns NUMA_NO_NODE for
 * I/O localities since SRAT does not list them.  I/O localities are
 * not supported at this point.
 */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	for (i = 0; i < slit->locality_count; i++) {
		if (pxm_to_node(i) == NUMA_NO_NODE)
			continue;
		for (j = 0; j < slit->locality_count; j++) {
			if (pxm_to_node(j) == NUMA_NO_NODE)
				continue;
				/* Fix me when use slit table */
		}
	}
}
extern cpumask_t possible_cpu_per_node;
/* Callback for Proximity Domain -> CPUID mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm, node;

	if (srat_disabled())
		return;
	if (pa->header.length != sizeof(struct acpi_srat_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain_lo;
	if (acpi_srat_revision >= 2) {
		pxm |= (pa->proximity_domain_hi[0] << 8);
		pxm |= (pa->proximity_domain_hi[1] << 16);
		pxm |= (pa->proximity_domain_hi[2] << 24);
	}
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	if (pa->apic_id >= CONFIG_NR_CPUS) {
		printk(KERN_INFO "SRAT: PXM %u -> CPU 0x%02x -> Node %u skipped apicid that is too big\n", pxm, pa->apic_id, node);
		return;
	}
	
	if(!cpu_guestmode)
		numa_add_cpu(__cpu_number_map[pa->apic_id], node);
	else
		numa_add_cpu(pa->apic_id, node);

	set_cpuid_to_node(pa->apic_id, node);
	node_set(node, numa_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> CPU 0x%02x -> Node %u\n",
		pxm, pa->apic_id, node);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline int save_add_info(void) {return 1;}
#else
static inline int save_add_info(void) {return 0;}
#endif

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
int __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	u64 start, end;
	u32 hotpluggable;
	int node, pxm;

	if (srat_disabled())
		goto out_err;
	if (ma->header.length != sizeof(struct acpi_srat_mem_affinity))
		goto out_err_bad_srat;
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		goto out_err;
	hotpluggable = ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE;
	if (hotpluggable && !save_add_info())
		goto out_err;

	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains.\n");
		goto out_err_bad_srat;
	}
	if (numa_add_memblk(node, start, end) < 0)
		goto out_err_bad_srat;

	node_set(node, numa_nodes_parsed);

	pr_info("SRAT: Node %u PXM %u [mem %#010Lx-%#010Lx]%s%s\n",
		node, pxm,
		(unsigned long long) start, (unsigned long long) end - 1,
		hotpluggable ? " hotplug" : "",
		ma->flags & ACPI_SRAT_MEM_NON_VOLATILE ? " non-volatile" : "");

	/* Mark hotplug range in memblock. */
	if (hotpluggable && memblock_mark_hotplug(start, ma->length))
		pr_warn("SRAT: Failed to mark hotplug range [mem %#010Lx-%#010Lx] in memblock\n",
			(unsigned long long)start, (unsigned long long)end - 1);

	max_possible_pfn = max(max_possible_pfn, PFN_UP(end - 1));

	return 0;
out_err_bad_srat:
	bad_srat();
out_err:
	return -1;
}

void __init acpi_numa_arch_fixup(void) {}
#endif
