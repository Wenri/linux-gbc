#include <linux/suspend.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <asm/reboot.h>
#include <asm/mach-loongson2/2k1000.h>
#include <asm/mach-loongson2/pm.h>
#include <linux/kexec.h>
#include <asm/bootinfo.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <asm/tlbflush.h>

extern u64 suspend_addr;
extern void loongson_suspend_lowlevel(void);
extern void ls2k_suspend_irq(void);
extern void ls2k_resume_irq(void);

struct loongson2k_registers {
	u32 config5;
	u32 hwrena;
	u32 wired;
	u64 userlocal;
	u64 pagemask;
	u64 gmac;
	u64 uart;
	u64 gpu;
	u64 apbdma;
	u64 usb_phy01;
	u64 usb_phy23;
	u64 sata;
	u64 dma0;
	u64 dma1;
	u64 dma2;
	u64 dma3;
	u64 dma4;
};

static struct loongson2k_registers loongson2k_regs;

u32 loongson2k_nr_nodes;
u64 loongson2k_suspend_addr;
u32 loongson2k_pcache_ways;
u32 loongson2k_scache_ways;
u32 loongson2k_pcache_sets;
u32 loongson2k_scache_sets;
u32 loongson2k_pcache_linesz;
u32 loongson2k_scache_linesz;

enum ACPI_Sx {
	ACPI_S3 = 5,
	ACPI_S4 = 6,
	ACPI_S5 = 7,
};


static void ls2k_pm(enum ACPI_Sx sx)
{
	unsigned long base;
	unsigned int acpi_ctrl;
	base = CKSEG1ADDR(APB_BASE) + ACPI_OFF;

	acpi_ctrl = readl((void*)(base + PM1_STS));
	acpi_ctrl &= 0xffffffff;
	writel(acpi_ctrl, (void*)(base + PM1_STS));

	acpi_ctrl = readl((void*)(base + GPE0_STS));
	acpi_ctrl &= 0xffffffff;
	writel(acpi_ctrl, (void*)(base + GPE0_STS));

	/*GMAC0_EN and GMAC1_EN*/
	acpi_ctrl = readl((void*)(base + GPE0_SR));
	acpi_ctrl |= 0x00000060;
	writel(acpi_ctrl, (void*)(base + GPE0_SR));

	/*WOL_BAT_EN*/
	acpi_ctrl = readl((void*)(base + RTC_GPMCR));
	acpi_ctrl |= 0x00000080;
	writel(acpi_ctrl, (void*)(base + RTC_GPMCR));

	/*USB_GMAC_OK set 1*/
	acpi_ctrl = readl((void*)(base + R_GPMCR));
	acpi_ctrl |= 0x00000080;
	writel(acpi_ctrl, (void*)(base + R_GPMCR));

	acpi_ctrl = ((sx << 10) | (1 << 13));
	writel(acpi_ctrl, (void*)(base + PM1_CTR));

}

static void ls2k_reboot(char *cmd)
{
	unsigned long base;
	base = CKSEG1ADDR(APB_BASE) + ACPI_OFF;

	writel(1, (void*)(base + RST_CTR));

	while (1) {};

}

static void ls2k_halt(void)
{
	ls2k_pm(ACPI_S5);
	while (1) {};
}

#ifdef CONFIG_KEXEC

/* 0X80000000~0X80200000 is safe */
#define MAX_ARGS	64
#define KEXEC_CTRL_CODE	0XFFFFFFFF80100000UL
#define KEXEC_ARGV_ADDR	0XFFFFFFFF80108000UL
#define KEXEC_ARGV_SIZE	3060

void *kexec_argv;
extern const size_t relocate_new_kernel_size;

static void *initrd_buf;
static unsigned long initrd_buf_len;

static void *fdt_buf;
static unsigned long fdt_buf_len;
static int argsize;

static int loongson_kexec_prepare(struct kimage *image)
{
	int i, argc = 0;
	unsigned int *argv;
	char *str, *ptr, *bootloader = "kexec";
	char *dtb_args = kmalloc(1024, GFP_KERNEL);
	*dtb_args = 0;
	/* argv at offset 0, argv[] at offset KEXEC_ARGV_SIZE/2 */
	argv = (unsigned int *)kexec_argv;
	argv[argc++] = (unsigned int)(KEXEC_ARGV_ADDR + KEXEC_ARGV_SIZE/2);

	for (i = 0; i < image->nr_segments; i++) {
		if (!strncmp(bootloader, (char *)image->segment[i].buf,
				strlen(bootloader))) {
			/*
			 * convert command line string to array
			 * of parameters (as bootloader does).
			 */
			int offt;
			argsize = image->segment[i].bufsz;
			strncpy(dtb_args, (char *)image->segment[i].buf + 6, 1024);
			memcpy(kexec_argv + KEXEC_ARGV_SIZE/2, image->segment[i].buf, KEXEC_ARGV_SIZE/2);
			str = (char *)kexec_argv + KEXEC_ARGV_SIZE/2;
			ptr = strchr(str, ' ');

			while (ptr && (argc < MAX_ARGS)) {
				*ptr = '\0';
				if (ptr[1] != ' ') {
					offt = (int)(ptr - str + 1);
					argv[argc] = KEXEC_ARGV_ADDR + KEXEC_ARGV_SIZE/2 + offt;
					argc++;
				}
				ptr = strchr(ptr + 1, ' ');
			}
		}
		else if (!memcmp("\x1f\x8b\x08", (char *)image->segment[i].buf, 3)) {
			initrd_buf = phys_to_virt(image->segment[i].mem);
			initrd_buf_len = image->segment[i].bufsz;
		}
		else if (!memcmp("\xd0\x0d\xfe\xed", (char *)image->segment[i].buf, 4)) {
			fdt_buf = image->segment[i].buf;
			fdt_buf_len = image->segment[i].bufsz;
			memcpy((void *)fw_arg2, fdt_buf, fdt_buf_len);
		}

	}

	if (initrd_buf_len) {
		int nodeoffset, len;
		void *fdt = (void *)fw_arg2;
		char *p;
		p = kexec_argv + KEXEC_ARGV_SIZE/2 + argsize;
		argv[argc] = KEXEC_ARGV_ADDR + KEXEC_ARGV_SIZE/2 + argsize;
		argc++;
		sprintf(p, "rd_start=0x%lx rd_size=0x%lx", (long)initrd_buf, initrd_buf_len);
		nodeoffset = fdt_path_offset (fdt, "/chosen");
		if (nodeoffset >= 0) {
			if (!fdt_getprop(fdt, nodeoffset, "linux,initrd-start", &len)) {
				strncat(dtb_args, " ", 1024);
				strncat(dtb_args, p, 1024);
				len = strlen(dtb_args) + 1;
				if (fdt_setprop(fdt, nodeoffset, "bootargs", dtb_args, len)) {
					fdt_delprop(fdt, nodeoffset, "bootargs");
					fdt_setprop(fdt, nodeoffset, "bootargs", dtb_args, len);
				}
			}
		}
	}
	kfree(dtb_args);

	kexec_args[0] = argc;
	kexec_args[1] = fw_arg1;
	kexec_args[2] = fw_arg2;
	image->control_code_page = virt_to_page((void *)KEXEC_CTRL_CODE);

	return 0;
}

#ifdef CONFIG_SMP
static void kexec_smp_down(void *ignored)
{
	int cpu = smp_processor_id();

	local_irq_disable();
	set_cpu_online(cpu, false);
	while (!atomic_read(&kexec_ready_to_reboot))
		cpu_relax();

	asm volatile (
	"	sync					\n"
	"	synci	($0)				\n");

	relocated_kexec_smp_wait(NULL);
}
#endif

static void loongson_kexec_shutdown(void)
{
#ifdef CONFIG_SMP
	int cpu;

	cpu_hotplug_enable();
	for_each_possible_cpu(cpu)
		if (!cpu_online(cpu))
			cpu_up(cpu); /* Everyone should go to reboot_code_buffer */

	smp_call_function(kexec_smp_down, NULL, 0);
	smp_wmb();
	while (num_online_cpus() > 1) {
		mdelay(1);
		cpu_relax();
	}
#endif
	memcpy((void *)fw_arg1, kexec_argv, KEXEC_ARGV_SIZE);
}

static void loongson_crash_shutdown(struct pt_regs *regs)
{
	default_machine_crash_shutdown(regs);
	memcpy((void *)fw_arg1, kexec_argv, KEXEC_ARGV_SIZE);
}

#endif
void  __init mips_reboot_setup(void)
{
	_machine_restart = ls2k_reboot;
	_machine_halt = ls2k_halt;
	pm_power_off = ls2k_halt;
}

int  __init ls2k_kexec_setup(void)
{
#ifdef CONFIG_KEXEC
	kexec_argv = kmalloc(KEXEC_ARGV_SIZE, GFP_KERNEL);
	fw_arg1 = KEXEC_ARGV_ADDR;

	_machine_kexec_prepare = loongson_kexec_prepare;
	_machine_kexec_shutdown = loongson_kexec_shutdown;
	_machine_crash_shutdown = loongson_crash_shutdown;
#endif
	return 0;
}

arch_initcall(ls2k_kexec_setup);

static int ls2k_pm_valid_state(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_ON:
		return 1;
	case PM_SUSPEND_MEM:
		return !!suspend_addr;
	default:
		return 0;
	}
}

static int ls2k_pm_begin(suspend_state_t state)
{
	return 0;
}

void ls2k_suspend_reg(void)
{
	unsigned long base;

	base = CKSEG1ADDR(CONF_BASE);

	loongson2k_regs.gmac = ls64_conf_read64((void*)(base + GMAC_OFF));
	loongson2k_regs.uart = ls64_conf_read64((void*)(base + UART_OFF));
	loongson2k_regs.gpu = ls64_conf_read64((void*)(base + GPU_OFF));
	loongson2k_regs.apbdma = ls64_conf_read64((void*)(base + APBDMA_OFF));
	loongson2k_regs.usb_phy01 = ls64_conf_read64((void*)(base + USB_PHY01_OFF));
	loongson2k_regs.usb_phy23 = ls64_conf_read64((void*)(base + USB_PHY23_OFF));
	loongson2k_regs.sata = ls64_conf_read64((void*)(base + SATA_OFF));
	loongson2k_regs.dma0 = ls64_conf_read64((void*)(base + CONF_DMA0_OFF));
	loongson2k_regs.dma1 = ls64_conf_read64((void*)(base + CONF_DMA1_OFF));
	loongson2k_regs.dma2 = ls64_conf_read64((void*)(base + CONF_DMA2_OFF));
	loongson2k_regs.dma3 = ls64_conf_read64((void*)(base + CONF_DMA3_OFF));
	loongson2k_regs.dma4 = ls64_conf_read64((void*)(base + CONF_DMA4_OFF));
}
void ls2k_resume_reg(void)
{
	unsigned long base;

	base = CKSEG1ADDR(CONF_BASE);
	ls64_conf_write64(loongson2k_regs.gmac, (void *)(base + GMAC_OFF));
	ls64_conf_write64(loongson2k_regs.uart, (void *)(base + UART_OFF));
	ls64_conf_write64(loongson2k_regs.gpu, (void *)(base + GPU_OFF));
	ls64_conf_write64(loongson2k_regs.apbdma, (void *)(base + APBDMA_OFF));
	ls64_conf_write64(loongson2k_regs.usb_phy01, (void *)(base + USB_PHY01_OFF));
	ls64_conf_write64(loongson2k_regs.usb_phy23, (void *)(base + USB_PHY23_OFF));
	ls64_conf_write64(loongson2k_regs.sata, (void *)(base + SATA_OFF));
	ls64_conf_write64(loongson2k_regs.dma0, (void *)(base + CONF_DMA0_OFF));
	ls64_conf_write64(loongson2k_regs.dma1, (void *)(base + CONF_DMA1_OFF));
	ls64_conf_write64(loongson2k_regs.dma2, (void *)(base + CONF_DMA2_OFF));
	ls64_conf_write64(loongson2k_regs.dma3, (void *)(base + CONF_DMA3_OFF));
	ls64_conf_write64(loongson2k_regs.dma4, (void *)(base + CONF_DMA4_OFF));
}

void mach_suspend(suspend_state_t state)
{
	if (state == PM_SUSPEND_MEM) {
		loongson2k_regs.config5 = read_c0_config5();
		loongson2k_regs.hwrena = read_c0_hwrena();
		loongson2k_regs.userlocal = read_c0_userlocal();
		loongson2k_regs.wired = read_c0_wired();
		ls2k_suspend_irq();
		ls2k_suspend_reg();
	}
}

void mach_resume(suspend_state_t state)
{
	if (state == PM_SUSPEND_MEM) {
		write_c0_config5(loongson2k_regs.config5);
		write_c0_hwrena(loongson2k_regs.hwrena);
		write_c0_userlocal(loongson2k_regs.userlocal);
		write_c0_wired(loongson2k_regs.wired);
		local_flush_tlb_all();
		ls2k_resume_irq();
		ls2k_resume_reg();
	}
}

static int ls2k_pm_enter(suspend_state_t state)
{
	mach_suspend(state);

	/* processor specific suspend */
	switch(state){
	case PM_SUSPEND_MEM:
		loongson2k_nr_nodes = MAX_NUMNODES;
		loongson2k_suspend_addr = CKSEG1ADDR(suspend_addr);
		loongson2k_pcache_ways = cpu_data[0].dcache.ways;
		loongson2k_scache_ways = cpu_data[0].scache.ways;
		loongson2k_pcache_sets = cpu_data[0].dcache.sets;
		loongson2k_scache_sets = cpu_data[0].scache.sets;
		loongson2k_pcache_linesz = cpu_data[0].dcache.linesz;
		loongson2k_scache_linesz = cpu_data[0].scache.linesz;
		loongson_suspend_lowlevel();
		break;
	default:
		break;
	}

	mach_resume(state);

	return 0;
}

static void ls2k_pm_wake(void)
{

}

static void ls2k_pm_end(void)
{

}

static const struct platform_suspend_ops ls2k_pm_ops = {
	.valid	= ls2k_pm_valid_state,
	.begin	= ls2k_pm_begin,
	.enter	= ls2k_pm_enter,
	.wake	= ls2k_pm_wake,
	.end	= ls2k_pm_end,
};

static int __init ls2k_pm_init(void)
{
	suspend_set_ops(&ls2k_pm_ops);
	return 0;
}
arch_initcall(ls2k_pm_init);
