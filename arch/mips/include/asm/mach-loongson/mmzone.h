/*
 * Copyright (C) 2010 Loongson Inc. & Insititute of Computing Technology
 * Author:  Gao Xiang, gaoxiang@ict.ac.cn
 *          Meng Xiaofu, Shuangshuang Zhang
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_MACH_MMZONE_H
#define _ASM_MACH_MMZONE_H

#include <boot_param.h>

#ifdef CONFIG_NEED_MULTIPLE_NODES

#ifdef CONFIG_KVM_GUEST_LS3A3000
#define NODE_ADDRSPACE_SHIFT 36
#else
#define NODE_ADDRSPACE_SHIFT 44
#endif

#define pa_to_nid(addr)	  ((addr >> NODE_ADDRSPACE_SHIFT) & 0xf)
#define nid_to_addroffset(node)	((unsigned long)node << NODE_ADDRSPACE_SHIFT)

struct node_data {
	struct pglist_data pglist;
	cpumask_t cpumask;
};

extern struct node_data *__node_data[];

#define NODE_DATA(n)		(&__node_data[(n)]->pglist)

#endif

#endif /* _ASM_MACH_MMZONE_H */
