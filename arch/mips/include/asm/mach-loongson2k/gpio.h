/*
 * STLS2F GPIO Support
 *
 * Copyright (c) 2008  Richard Liu, STMicroelectronics <richard.liu@st.com>
 * Copyright (c) 2008-2010  Arnaud Patard <apatard@mandriva.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __STLS2F_GPIO_H
#define __STLS2F_GPIO_H
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pinctrl/pinctrl.h>
#define ARCH_NR_GPIOS 60
static inline bool gpio_is_valid(int number)
{
   return false;
}

static inline void
gpiochip_remove_pin_ranges(struct gpio_chip *chip)
{
   WARN_ON(1);
}
#endif				/* __STLS2F_GPIO_H */
