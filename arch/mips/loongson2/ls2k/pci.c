/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <linux/pci.h>

#include <pci.h>
#include <loongson.h>
#include <boot_param.h>
#include <loongson-pch.h>

int __init ls2k_pcie_init(void);

static int __init pcibios_init(void)
{
	return ls2k_pcie_init();
}

arch_initcall(pcibios_init);
