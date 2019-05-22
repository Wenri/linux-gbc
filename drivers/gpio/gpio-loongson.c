/*
 *  Loongson-3A/3B/7A GPIO Support
 *
 *  Copyright (c) 2018 Juxin Gao <gaojuxin@loongson.cn>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <asm/types.h>

#ifdef CONFIG_CPU_LOONGSON3
#include <loongson-pch.h>
#endif/* CONFIG_CPU_LOONGSON3 */

#define LOONGSON_GPIO_IN_OFFSET	16
#define GPIO_IO_CONF(x)	(x->base + x->conf_offset)
#define GPIO_OUT(x)	(x->base + x->out_offset)
#define GPIO_IN(x)	(x->base + x->in_offset)

struct loongson_gpio_chip {
	struct gpio_chip	chip;
	spinlock_t		lock;
	void __iomem		*base;
	int conf_offset;
	int out_offset;
	int in_offset;
};

/*
 * GPIO primitives.
 */
static int loongson_gpio_request(struct gpio_chip *chip, unsigned pin)
{
	if (pin >= (chip->ngpio + chip->base))
		return -EINVAL;
	else
		return 0;
}

static inline void
__set_direction(struct loongson_gpio_chip *lgpio, unsigned pin, int input)
{
	u64 u;
	u32 temp;
        unsigned long flags;

	if (!strcmp(lgpio->chip.label,"loongson-gpio")){
		temp = readl(GPIO_IO_CONF(lgpio));
		if (input)
			temp |= 1 << pin;
		else
			temp &= ~(1 << pin);
		writel(temp, GPIO_IO_CONF(lgpio));
		return ;
	}

	ls7a_read(u, (unsigned long)GPIO_IO_CONF(lgpio));
	if (input)
		u |= 1UL << pin;
	else
		u &= ~(1UL << pin);
	ls7a_write(u, (unsigned long)GPIO_IO_CONF(lgpio));
}

static void __set_level(struct loongson_gpio_chip *lgpio, unsigned pin, int high)
{
	u64 u;
	u32 temp;
  	unsigned long flags;

	/* If GPIO controller is on 3A,then... */
	if (!strcmp(lgpio->chip.label,"loongson-gpio")){
		temp = readl(GPIO_OUT(lgpio));
		if (high)
			temp |= 1 << pin;
		else
			temp &= ~(1 << pin);
		writel(temp, GPIO_OUT(lgpio));
		return;
	}

	ls7a_read(u, (unsigned long)GPIO_OUT(lgpio));
	if (high)
		u |= 1UL << pin;
	else
		u &= ~(1UL << pin);
	ls7a_write(u, (unsigned long)GPIO_OUT(lgpio));
}

static int loongson_gpio_direction_input(struct gpio_chip *chip, unsigned pin)
{
	unsigned long flags;
	struct loongson_gpio_chip *lgpio =
		container_of(chip, struct loongson_gpio_chip, chip);

	spin_lock_irqsave(&lgpio->lock, flags);
	__set_direction(lgpio, pin, 1);
	spin_unlock_irqrestore(&lgpio->lock, flags);

	return 0;
}

static int loongson_gpio_direction_output(struct gpio_chip *chip,
		unsigned pin, int value)
{
	struct loongson_gpio_chip *lgpio =
		container_of(chip, struct loongson_gpio_chip, chip);
	unsigned long flags;

	spin_lock_irqsave(&lgpio->lock, flags);
	__set_level(lgpio, pin, value);
	__set_direction(lgpio, pin, 0);
	spin_unlock_irqrestore(&lgpio->lock, flags);

	return 0;
}

static int loongson_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct loongson_gpio_chip *lgpio =
		container_of(chip, struct loongson_gpio_chip, chip);
	u64 val, u;
	u32 temp;
  	unsigned long flags;

	/* GPIO controller in 3A is different for 7A */
	if (!strcmp(lgpio->chip.label,"loongson-gpio")){
		temp = readl(GPIO_IN(lgpio));
		return ((temp & (1 << (pin + LOONGSON_GPIO_IN_OFFSET))) != 0);
	}

	ls7a_read(u, (unsigned long)GPIO_IO_CONF(lgpio));
	if (u & (1UL << pin))
		ls7a_read(val, (unsigned long)GPIO_IN(lgpio));
	else
		ls7a_read(val, (unsigned long)GPIO_OUT(lgpio));

	return (val >> pin) & 1;
}

static void loongson_gpio_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct loongson_gpio_chip *lgpio =
		container_of(chip, struct loongson_gpio_chip, chip);
	unsigned long flags;

	spin_lock_irqsave(&lgpio->lock, flags);
	__set_level(lgpio, pin, value);
	spin_unlock_irqrestore(&lgpio->lock, flags);
}

static int loongson_gpio_init(struct loongson_gpio_chip *lgpio, struct device_node *np,
			    int gpio_base, int ngpio,
			    void __iomem *base, int conf_offset, int out_offset, int in_offset, const char* name)
{
	lgpio->chip.label = kstrdup(name, GFP_KERNEL);
	lgpio->chip.request = loongson_gpio_request;
	lgpio->chip.direction_input = loongson_gpio_direction_input;
	lgpio->chip.get = loongson_gpio_get;
	lgpio->chip.direction_output = loongson_gpio_direction_output;
	lgpio->chip.set = loongson_gpio_set;
	lgpio->chip.base = gpio_base;
	lgpio->chip.ngpio = ngpio;
	lgpio->chip.can_sleep = 0;
	lgpio->chip.of_node = np;

	spin_lock_init(&lgpio->lock);
	lgpio->base = (void __iomem *)base;
	lgpio->conf_offset = conf_offset;
	lgpio->out_offset = out_offset;
	lgpio->in_offset = in_offset;

	gpiochip_add(&lgpio->chip);

	return 0;
}

static int loongson_gpio_probe(struct platform_device *pdev)
{
	struct resource *iores;
	const char *name;
	void __iomem *base;
	int ret = 0;
	int gpio_base;
	int ngpio;
	u32 conf_offset;
	u32 out_offset;
	u32 in_offset;
	struct loongson_gpio_chip *lgpio;
	struct device_node *np = pdev->dev.of_node;
	struct platform_gpio_data *gpio_data = (struct platform_gpio_data *)pdev->dev.platform_data;

	if (np){
		of_property_read_u32(np, "ngpios", &ngpio);
		of_property_read_u32(np, "gpio_base", &gpio_base);
		of_property_read_u32(np, "conf_offset", &conf_offset);
		of_property_read_u32(np, "out_offset", &out_offset);
		of_property_read_u32(np, "in_offset", &in_offset);
		of_property_read_string(np, "compatible", &name);
		if (!strcmp(name, "loongson,ls7a-dc-gpio")){
#ifdef CONFIG_CPU_LOONGSON3
			base = (void *)TO_UNCAC(LS7A_DC_CNT_REG_BASE);
#endif
		}else{
			iores = platform_get_resource(pdev,IORESOURCE_MEM, 0);
			if (!iores) {
				ret = -ENODEV;
				goto out;
			}
			if (!request_mem_region(iores->start, resource_size(iores),
						pdev->name)) {
				ret = -EBUSY;
				goto out;
			}
			base = ioremap(iores->start, resource_size(iores));
			if (!base) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}else{
		gpio_base 	= gpio_data->gpio_base;
		ngpio		= gpio_data->ngpio;
		conf_offset = gpio_data->gpio_conf;
		out_offset  = gpio_data->gpio_out;
		in_offset   = gpio_data->gpio_in;
		name		= pdev->name;
		iores = platform_get_resource(pdev,IORESOURCE_MEM, 0);
		if (!iores) {
			ret = -ENODEV;
			goto out;
		}
		if (!request_mem_region(iores->start, resource_size(iores),
					pdev->name)) {
			ret = -EBUSY;
			goto out;
		}
		base = ioremap(iores->start, resource_size(iores));
		if (!base) {
			ret = -ENOMEM;
			goto out;
		}

	}

	lgpio = kzalloc(sizeof(struct loongson_gpio_chip), GFP_KERNEL);
	if (!lgpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, lgpio);
	loongson_gpio_init(lgpio, np, gpio_base, ngpio, base, conf_offset, out_offset, in_offset, name);

	return 0;
out:
	pr_err("%s: %s: missing mandatory property\n", __func__, np->name);
	return ret;
}

static int loongson_gpio_remove(struct platform_device *pdev)
{
	struct loongson_gpio_chip *lgpio = platform_get_drvdata(pdev);
	struct resource		*mem;

	platform_set_drvdata(pdev, NULL);
	gpiochip_remove(&lgpio->chip);
	iounmap(lgpio->base);
	kfree(lgpio);
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	return 0;
}

static const struct of_device_id loongson_gpio_dt_ids[] = {
	{ .compatible = "loongson,ls7a-gpio"},
	{ .compatible = "loongson,ls7a-dc-gpio"},
	{}
};

static struct platform_driver ls_gpio_driver = {
	.driver = {
		.name = "loongson-gpio",
		.owner	= THIS_MODULE,
		.of_match_table = loongson_gpio_dt_ids,
	},
	.probe = loongson_gpio_probe,
	.remove = loongson_gpio_remove,
};

static int __init loongson_gpio_setup(void)
{
	return platform_driver_register(&ls_gpio_driver);
}
subsys_initcall(loongson_gpio_setup);

static void __exit loongson_gpio_driver(void)
{
	platform_driver_unregister(&ls_gpio_driver);
}
module_exit(loongson_gpio_driver);
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_DESCRIPTION("LOONGSON GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:loongson_gpio");
