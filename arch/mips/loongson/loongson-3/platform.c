/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *         Xiang Yu, xiangy@lemote.com
 *         Chen Huacai, chenhc@lemote.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <loongson-pch.h>
#include <loongson_hwmon.h>
#include <asm/bootinfo.h>
#include <linux/i2c.h>
#include <boot_param.h>
#include <workarounds.h>
#include <linux/of.h>
#include <loongson.h>
#include <linux/acpi.h>

int hpet_enabled = 0;

static struct ls_temp_id temp_data= {
	.max_id = 3,
};

/*
 *  temperature device
 */
static struct resource ls_temp0_resources[] = {
	[0] = {
		.start = LS_CHIPTEMP0,
		.end   = LS_CHIPTEMP0 + 0x4,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource ls_temp1_resources[] = {
	[0] = {
		.start = LS_CHIPTEMP1,
		.end   = LS_CHIPTEMP1 + 0x4,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource ls_temp2_resources[] = {
	[0] = {
		.start = LS_CHIPTEMP2,
		.end   = LS_CHIPTEMP2 + 0x4,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource ls_temp3_resources[] = {
	[0] = {
		.start = LS_CHIPTEMP3,
		.end   = LS_CHIPTEMP3 + 0x4,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device ls_temp0_device = {
	.name			= "ls-hwmon",
	.id   			= 0,
	.num_resources  = ARRAY_SIZE(ls_temp0_resources),
	.resource 		= ls_temp0_resources,
	.dev			= {
		.platform_data = &temp_data,
	},
};

static struct platform_device ls_temp1_device = {
	.name			= "ls-hwmon",
	.id   			= 1,
	.num_resources  = ARRAY_SIZE(ls_temp1_resources),
	.resource 		= ls_temp1_resources,
	.dev			= {
		.platform_data = &temp_data,
	},
};

static struct platform_device ls_temp2_device = {
	.name			= "ls-hwmon",
	.id   			= 2,
	.num_resources  = ARRAY_SIZE(ls_temp2_resources),
	.resource 		= ls_temp2_resources,
	.dev			= {
		.platform_data = &temp_data,
	},
};
static struct platform_device ls_temp3_device = {
	.name			= "ls-hwmon",
	.id   			= 3,
	.num_resources  = ARRAY_SIZE(ls_temp3_resources),
	.resource 		= ls_temp3_resources,
	.dev			= {
		.platform_data = &temp_data,
	},
};

static struct platform_device *ls_temp_devices[] = {
	&ls_temp0_device,
	&ls_temp1_device,
	&ls_temp2_device,
	&ls_temp3_device,
};

/*
 * Kernel helper policy
 *
 * Fan is controlled by EC in laptop pruducts, but EC can not get the current
 * cpu temperature which used for adjusting the current fan speed.
 *
 * So, kernel read the CPU temperature and notify it to EC per second,
 * that's all!
 */
struct loongson_fan_policy kernel_helper_policy = {
	.type = KERNEL_HELPER_POLICY,
	.adjust_period = 1,
	.depend_temp = loongson_cpu_temp,
};

/*
 * Policy at step mode
 *
 * up_step array    |   down_step array
 *                  |
 * [min, 50),  50%  |   (min, 45),  50%
 * [50,  60),  60%  |   [45,  55),  60%
 * [60,  70),  70%  |   [55,  65),  70%
 * [70,  80),  80%  |   [65,  75),  80%
 * [80,  max), 100% |   [75,  max), 100%
 *
 */
struct loongson_fan_policy step_speed_policy = {
	.type = STEP_SPEED_POLICY,
	.adjust_period = 1,
	.depend_temp = loongson_cpu_temp,
	.up_step_num = 5,
	.down_step_num = 5,
	.up_step = {
			{MIN_TEMP, 50,    50},
			{   50,    60,    60},
			{   60,    70,    70},
			{   70,    80,    80},
			{   80, MAX_TEMP, 100},
		   },
	.down_step = {
			{MIN_TEMP, 45,    50},
			{   45,    55,    60},
			{   55,    65,    70},
			{   65,    75,    80},
			{   75, MAX_TEMP, 100},
		     },
};

/*
 * Constant speed policy
 *
 */
struct loongson_fan_policy constant_speed_policy = {
	.type = CONSTANT_SPEED_POLICY,
};

#define GPIO_LCD_CNTL		5
#define GPIO_BACKLIGHIT_CNTL	7

static int __init loongson3_platform_init(void)
{
	int i;
	struct platform_device * pdev;

	if (loongson_pch)
		loongson_pch->pch_arch_initcall();

	if (loongson_ecname[0] != '\0')
		platform_device_register_simple(loongson_ecname, -1, NULL, 0);

	for (i=0; i<loongson_nr_sensors; i++) {
		if (loongson_sensors[i].type > SENSOR_FAN)
			continue;

		pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
		pdev->name = loongson_sensors[i].name;
		pdev->id = loongson_sensors[i].id;
		pdev->dev.platform_data = &loongson_sensors[i];
		platform_device_register(pdev);
	}

	if (loongson_workarounds & WORKAROUND_LVDS_GPIO) {
		gpio_request(GPIO_LCD_CNTL,  "gpio_lcd_cntl");
		gpio_request(GPIO_BACKLIGHIT_CNTL, "gpio_bl_cntl");
	}
#ifdef CONFIG_OF
	if(!of_find_compatible_node(NULL, NULL, "loongson,ls-hwmon"))
#endif
		platform_add_devices(ls_temp_devices, ARRAY_SIZE(ls_temp_devices));

	return 0;
}

extern void power_button_init(void);
static int __init loongson3_device_init(void)
{
	if (loongson_pch)
		loongson_pch->pch_device_initcall();

	if (acpi_disabled)
		power_button_init();
	return 0;
}

arch_initcall(loongson3_platform_init);
device_initcall(loongson3_device_init);
