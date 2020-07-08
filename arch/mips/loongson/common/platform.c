/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/err.h>
#include <linux/smp.h>
#include <linux/platform_device.h>
#include <loongson.h>
#include <loongson-pch.h>

static struct platform_device loongson2_cpufreq_device = {
	.name = "loongson2_cpufreq",
	.id = -1,
};

static struct platform_device loongson_sci_event_device = {
	.name = "ls_pm_hotkey",
	.id = -1,
};

#if defined(CONFIG_LOONGSON_EA_PM_HOTKEY)
static struct platform_device loongson_ea_sci_event_device = {
	.name = "ea_pm_hotkey",
	.id = -1,
};
#endif

static struct platform_device loongson3_cpufreq_device = {
	.name = "loongson3_cpufreq",
	.id = -1,
};

/*
 * GPIO
 */

static struct resource loongson_gpio_resources[] = {
	[0] = {
		.start  = LOONGSON_REG_GPIO_BASE,
		.end    = LOONGSON_REG_GPIO_BASE + 4,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_gpio_data loongson3_gpio_data = {
	.gpio_conf = 4,
	.gpio_out  = 0,
	.gpio_in   = 0,
	.gpio_base = 0,
	.ngpio     = 16,
};
static struct platform_device loongson_gpio_device = {
	.name   = "loongson-gpio",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(loongson_gpio_resources),
	.resource       = loongson_gpio_resources,
	.dev	= {
			.platform_data = &loongson3_gpio_data,
	},
};

static int __init loongson_cpufreq_init(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	switch(c->cputype){
	case CPU_LOONGSON3:
		/* Only 2F revision and it's successors support CPUFreq */
		if ((c->processor_id & PRID_REV_MASK) == PRID_REV_LOONGSON2F)
			return platform_device_register(&loongson2_cpufreq_device);
		if ((c->processor_id & PRID_REV_MASK) < PRID_REV_LOONGSON3A_R1)
			break;
	case CPU_LOONGSON3_COMP:
		return platform_device_register(&loongson3_cpufreq_device);
	default:
		return -ENODEV;
	}
	return -ENODEV;
}

arch_initcall(loongson_cpufreq_init);

static int __init loongson3_platform_init(void)
{
	if(cpu_guestmode)
		return 0;

	platform_device_register(&loongson_sci_event_device);
#if defined(CONFIG_LOONGSON_EA_PM_HOTKEY)
	platform_device_register(&loongson_ea_sci_event_device);
#endif
	platform_device_register(&loongson_gpio_device);
	return 0;
}
arch_initcall(loongson3_platform_init);
