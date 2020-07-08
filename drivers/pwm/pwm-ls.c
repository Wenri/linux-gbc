/*
 * Copyright (C) 2017 Loongson Technology Corporation Limited
 *
 * Author: Juxin Gao <gaojuxin@loongson.cn>
 * License terms: GNU General Public License (GPL)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/atmel_tc.h>
#include <linux/pwm.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#ifdef CONFIG_CPU_LOONGSON2K
#include <ls2k.h>
#endif

/* counter offest */
#define LOW_BUFFER  0x004
#define FULL_BUFFER 0x008
#define CTRL		0x00c

/* CTRL counter each bit */
#define CTRL_EN		BIT(0)
#define CTRL_OE		BIT(3)
#define CTRL_SINGLE	BIT(4)
#define CTRL_INTE	BIT(5)
#define CTRL_INT	BIT(6)
#define CTRL_RST	BIT(7)
#define CTRL_CAPTE	BIT(8)
#define CTRL_INVERT	BIT(9)
#define CTRL_DZONE	BIT(10)

#define to_ls_pwm_chip(_chip)		container_of(_chip, struct ls_pwm_chip, chip)
#define NS_IN_HZ (1000000000UL)

#ifdef CONFIG_CPU_LOONGSON2K
#define CPU_FRQ_PWM (125000000UL)
#else
#define CPU_FRQ_PWM (50000000UL)
#endif

struct ls_pwm_chip{
	struct pwm_chip chip;
	void __iomem		*mmio_base;
};

static int ls_pwm_set_polarity(struct pwm_chip *chip,
				      struct pwm_device *pwm,
				      enum pwm_polarity polarity)
{
	struct ls_pwm_chip *ls_pwm = to_ls_pwm_chip(chip);
	u16 val;

	val = readl(ls_pwm->mmio_base + CTRL);

	switch (polarity) {
		case PWM_POLARITY_NORMAL:
			val &= ~CTRL_INVERT;
			break;
		case PWM_POLARITY_INVERSED:
			val |= CTRL_INVERT;
			break;
		default:
			break;
	}

	writel(val, ls_pwm->mmio_base + CTRL);

	return 0;
}

static void ls_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls_pwm_chip *ls_pwm = to_ls_pwm_chip(chip);
	u32 ret;

	ret = readl(ls_pwm->mmio_base + CTRL);
	ret &= ~CTRL_EN;
	writel(ret, ls_pwm->mmio_base + CTRL);
}

static int ls_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls_pwm_chip *ls_pwm = to_ls_pwm_chip(chip);
	int ret;

	ret = readl(ls_pwm->mmio_base + CTRL);
	ret |= CTRL_EN;
	writel(ret, ls_pwm->mmio_base + CTRL);
	return 0;
}

static int ls_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				int duty_ns, int period_ns)
{
	struct ls_pwm_chip *ls_pwm = to_ls_pwm_chip(chip);
	unsigned int period, duty;
	unsigned long long val0,val1;

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;

	val0 = CPU_FRQ_PWM * period_ns;
	do_div(val0, NSEC_PER_SEC);
	if (val0 < 1)
		val0 = 1;
	period = val0;

	val1 = CPU_FRQ_PWM * duty_ns;
	do_div(val1, NSEC_PER_SEC);
	if (val1 < 1)
		val1 = 1;
	duty = val1;

	writel(duty,ls_pwm->mmio_base + LOW_BUFFER);
	writel(period,ls_pwm->mmio_base + FULL_BUFFER);

	return 0;
}

static const struct pwm_ops ls_pwm_ops = {
	.config = ls_pwm_config,
	.set_polarity = ls_pwm_set_polarity,
	.enable = ls_pwm_enable,
	.disable = ls_pwm_disable,
	.owner = THIS_MODULE,
};

static int ls_pwm_probe(struct platform_device *pdev)
{
	struct ls_pwm_chip *pwm;
	struct resource *mem;
	int err;
	if (pdev->id > 3)
		dev_err(&pdev->dev, "Currently only four PWM controller is implemented,namely, 0,1,2,3.\n");
	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if(!pwm){
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &ls_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 1;

	mem = platform_get_resource(pdev,IORESOURCE_MEM, 0);
	if(!mem){
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	pwm->mmio_base = devm_ioremap_resource(&pdev->dev, mem);
	if(!pwm->mmio_base){
		dev_err(&pdev->dev, "mmio_base is null\n");
		return -ENOMEM;
	}
	err = pwmchip_add(&pwm->chip);
	if(err < 0){
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n",err);
		return err;
	}

	platform_set_drvdata(pdev, pwm);

	dev_dbg(&pdev->dev, "pwm probe successful\n");
	return 0;
}

static int ls_pwm_remove(struct platform_device *pdev)
{
	struct ls_pwm_chip *pwm = platform_get_drvdata(pdev);
	int err;
	if (!pwm)
		return -ENODEV;
	err = pwmchip_remove(&pwm->chip);
	if(err < 0)
		return err;

	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id ls_pwm_id_table[] = {
	{.compatible = "loongson,ls2k-pwm"},
	{.compatible = "loongson,ls7a-pwm"},
	{},
};
#endif
static struct platform_driver ls_pwm_driver = {
	.driver = {
		.name = "ls-pwm",
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_pwm_id_table),
#endif
	},
	.probe = ls_pwm_probe,
	.remove = ls_pwm_remove,
};
module_platform_driver(ls_pwm_driver);

MODULE_AUTHOR("Juxin Gao <gaojuxin@loongson.com>");
MODULE_DESCRIPTION("Loongson Pwm Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ls-pwm");
