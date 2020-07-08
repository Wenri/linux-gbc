/* Board-specific reboot/shutdown routines
 * Copyright (c) 2009 Philippe Vachon <philippe@cowpig.ca>
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <loongson.h>

void mach_prepare_reboot(void)
{
	unsigned int dummy;

	dummy = readl(LOONGSON_GENCFG);
	dummy &= ~(1 << 2);
	writel(dummy, LOONGSON_GENCFG);

	dummy |= (1 << 2);
	writel(dummy, LOONGSON_GENCFG);

}

void mach_prepare_shutdown(void)
{
}
