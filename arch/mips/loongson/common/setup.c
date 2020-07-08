/*
 * Copyright (C) 2007 Lemote Inc. & Insititute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <asm/wbflush.h>
#include <linux/of_fdt.h>
#include <asm/prom.h>
#include <linux/bootmem.h>

#include <loongson.h>

#ifdef CONFIG_VT
#include <linux/console.h>
#include <linux/screen_info.h>
#endif

#include <linux/libfdt.h>

static void wbflush_loongson(void)
{
	asm(".set\tpush\n\t"
	    ".set\tnoreorder\n\t"
	    ".set mips3\n\t"
	    "sync\n\t"
	    "nop\n\t"
	    ".set\tpop\n\t"
	    ".set mips0\n\t");
}

void (*__wbflush)(void) = wbflush_loongson;
EXPORT_SYMBOL(__wbflush);

void __init plat_mem_setup(void)
{
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;

	screen_info = (struct screen_info) {
		.orig_x			= 0,
		.orig_y			= 25,
		.orig_video_cols	= 80,
		.orig_video_lines	= 25,
		.orig_video_isVGA	= VIDEO_TYPE_VGAC,
		.orig_video_points	= 16,
	};
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
#ifdef CONFIG_OF
	if (loongson_fdt_blob)
		__dt_setup_arch(loongson_fdt_blob);
#endif
}

#define NR_CELLS 6

#ifdef CONFIG_OF
void __init device_tree_init(void)
{
	unsigned long base, size;
	void *dt;

	if (!initial_boot_params) {
		pr_warn("No valid device tree found, continuing without\n");
		return;
	}
	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
	dt = memblock_virt_alloc(size,roundup_pow_of_two(FDT_V17_SIZE));
	if (dt) {
		memcpy(dt, initial_boot_params, size);
		initial_boot_params = dt;
	}

	unflatten_device_tree();

}
#endif
