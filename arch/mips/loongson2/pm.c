#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <asm/reboot.h>
#include <asm/mach-loongson2/2k1000.h>
#include <asm/mach-loongson2/pm.h>

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

void  __init mips_reboot_setup(void)
{
	_machine_restart = ls2k_reboot;
	_machine_halt = ls2k_halt;
	pm_power_off = ls2k_halt;
}
