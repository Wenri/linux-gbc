/*
 * =====================================================================================
 *
 *       Filename:  init.c
 *
 *    Description:  loongson 2 soc entry
 *
 *        Version:  1.0
 *        Created:  03/18/2017 09:44:47 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/io.h>
#include <asm/bootinfo.h>
#include <linux/of_fdt.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
extern struct plat_smp_ops loongson_smp_ops;

#endif

extern struct boot_param_header  * __dtb_start;
void __init early_init_devtree(void *params);

void prom_init_env(void);

void __init prom_init(void)
{
	void * fdtp;
	int prom_argc;
	/* pmon passes arguments in 32bit pointers */
	int *_prom_argv;
	int i;
	long l;

	/* firmware arguments are initialized in head.S */
	prom_argc = fw_arg0;
	_prom_argv = (int *)fw_arg1;

	/* arg[0] is "g", the rest is boot parameters */
	arcs_cmdline[0] = '\0';
	for (i = 1; i < prom_argc; i++) {
		l = (long)_prom_argv[i];
		if (strlen(arcs_cmdline) + strlen(((char *)l) + 1)
		    >= sizeof(arcs_cmdline))
			break;
		strcat(arcs_cmdline, ((char *)l));
		strcat(arcs_cmdline, " ");
	}

	/* firmware arguments are initialized in head.S */
	fdtp = __dtb_start;

	if (!fdtp)
		fdtp = (void *)fw_arg2;

	pr_info("FDT point@%p\n", fdtp);

	/*early_init_devtree(fdtp);*/
#if defined(CONFIG_SMP)
	register_smp_ops(&loongson_smp_ops);
#endif
}


const char *get_system_type(void)
{
	return "Loongson2K-SBC";
}
