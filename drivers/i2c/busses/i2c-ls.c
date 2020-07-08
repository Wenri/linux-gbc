/*
 * Loongson I2C master mode driver
 *
 * Copyright (C) 2013 Loongson Technology Corporation Limited
 *
 * Originally written by JuxinGao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

#ifdef CONFIG_CPU_LOONGSON2K
#include <ls2k.h>
#else
#include <loongson-pch.h>
#endif

#define	CR_START			0x80
#define	CR_STOP				0x40
#define	CR_READ				0x20
#define	CR_WRITE			0x10
#define	CR_ACK				0x8
#define	CR_IACK				0x1

#define	SR_NOACK			0x80
#define	SR_BUSY				0x40
#define	SR_AL				0x20
#define	SR_TIP				0x2
#define	SR_IF				0x1

#define LS_I2C_PRER_LO_REG	0x0
#define LS_I2C_PRER_HI_REG	0x1
#define LS_I2C_CTR_REG    	0x2
#define LS_I2C_TXR_REG    	0x3
#define LS_I2C_RXR_REG    	0x3
#define LS_I2C_CR_REG     	0x4
#define LS_I2C_SR_REG     	0x4

#define ls_i2c_readb(addr)			readb(dev->base + addr)
#define ls_i2c_writeb(val, addr)	writeb(val, dev->base + addr)

#ifdef LS_I2C_DEBUG
#define ls_i2c_debug(fmt, args...)	printk(KERN_CRIT fmt, ##args)
#else
#define ls_i2c_debug(fmt, args...)
#endif

struct ls_i2c_dev {
	spinlock_t		lock;
	unsigned int		suspended:1;
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct completion	cmd_complete;
	struct resource		*ioarea;
	struct i2c_adapter	adapter;
};

static void ls_i2c_stop(struct ls_i2c_dev *dev)
{
again:
        ls_i2c_writeb(CR_STOP, LS_I2C_CR_REG);
        ls_i2c_readb(LS_I2C_SR_REG);
        while (ls_i2c_readb(LS_I2C_SR_REG) & SR_BUSY)
                goto again;
}

static int ls_i2c_start(struct ls_i2c_dev *dev,
		int dev_addr, int flags)
{
	int retry = 5;
	unsigned char addr = (dev_addr & 0x7f) << 1;
	addr |= (flags & I2C_M_RD)? 1:0;

start:
	mdelay(1);
	ls_i2c_writeb(addr, LS_I2C_TXR_REG);
	ls_i2c_debug("%s <line%d>: i2c device address: 0x%x\n",
			__func__, __LINE__, addr);
	ls_i2c_writeb((CR_START | CR_WRITE), LS_I2C_CR_REG);
	while (ls_i2c_readb(LS_I2C_SR_REG) & SR_TIP) ;

	if (ls_i2c_readb(LS_I2C_SR_REG) & SR_NOACK) {
		ls_i2c_stop(dev);
		while (retry--)
			goto start;
		pr_info("There is no i2c device ack\n");
		return 0;
	}
	return 1;
}

static void ls_i2c_init(struct ls_i2c_dev *dev)
{
        ls_i2c_writeb(0, LS_I2C_CTR_REG);
        ls_i2c_writeb(0x2c, LS_I2C_PRER_LO_REG);
        ls_i2c_writeb(0x1, LS_I2C_PRER_HI_REG);
        ls_i2c_writeb(0x80, LS_I2C_CTR_REG);
}

static int ls_i2c_read(struct ls_i2c_dev *dev,
		unsigned char *buf, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		ls_i2c_writeb((i == count - 1)?
				(CR_READ | CR_ACK) : CR_READ,
				LS_I2C_CR_REG);
		while (ls_i2c_readb(LS_I2C_SR_REG) & SR_TIP) ;
		buf[i] = ls_i2c_readb(LS_I2C_RXR_REG);
		ls_i2c_debug("%s <line%d>: read buf[%d] <= %02x\n",
				__func__, __LINE__, i, buf[i]);
        }

        return i;
}

static int ls_i2c_write(struct ls_i2c_dev *dev,
		unsigned char *buf, int count)
{
        int i;

        for (i = 0; i < count; i++) {
		ls_i2c_writeb(buf[i], LS_I2C_TXR_REG);
		ls_i2c_debug("%s <line%d>: write buf[%d] => %02x\n",
				__func__, __LINE__, i, buf[i]);
		ls_i2c_writeb(CR_WRITE, LS_I2C_CR_REG);
		while (ls_i2c_readb(LS_I2C_SR_REG) & SR_TIP) ;

		if (ls_i2c_readb(LS_I2C_SR_REG) & SR_NOACK) {
			ls_i2c_debug("%s <line%d>: device no ack\n",
					__func__, __LINE__);
			ls_i2c_stop(dev);
			return 0;
		}
        }

        return i;
}

static int ls_i2c_doxfer(struct ls_i2c_dev *dev,
		struct i2c_msg *msgs, int num)
{
	struct i2c_msg *m = msgs;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev->lock, flags);
	for(i = 0; i < num; i++) {
		if (!ls_i2c_start(dev, m->addr, m->flags)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return 0;
		}
		if (m->flags & I2C_M_RD)
		{
			ls_i2c_read(dev, m->buf, m->len);
		}
		else
		{
			ls_i2c_write(dev, m->buf, m->len);
		}
		++m;
	}

	ls_i2c_stop(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return i;
}

static int ls_i2c_xfer(struct i2c_adapter *adap,
                        struct i2c_msg *msgs, int num)
{
	int ret;
	int retry;
	struct ls_i2c_dev *dev;

	dev = i2c_get_adapdata(adap);
	for (retry = 0; retry < adap->retries; retry++) {
		ret = ls_i2c_doxfer(dev, msgs, num);
		if (ret != -EAGAIN)
			return ret;

		udelay(100);
	}

	return -EREMOTEIO;
}

static unsigned int ls_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ls_i2c_algo = {
	.master_xfer	= ls_i2c_xfer,
	.functionality	= ls_i2c_func,
};

static int ls_i2c_probe(struct platform_device *pdev)
{
	struct ls_i2c_dev	*dev;
	struct i2c_adapter	*adap;
	struct resource		*mem, *irq, *ioarea;
	int r;

	/* NOTE: driver uses the static register mapping */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -ENODEV;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "I2C region already claimed\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct ls_i2c_dev), GFP_KERNEL);
	if (!dev) {
		r = -ENOMEM;
		goto err_release_region;
	}

	spin_lock_init(&dev->lock);
	dev->dev = &pdev->dev;
	dev->irq = irq->start;
	dev->base = ioremap(mem->start, resource_size(mem));
	if (!dev->base) {
		r = -ENOMEM;
		goto err_free_mem;
	}

	platform_set_drvdata(pdev, dev);

	ls_i2c_init(dev);

	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	adap->retries = 5;
	strlcpy(adap->name, "LS I2C adapter", sizeof(adap->name));
	adap->algo = &ls_i2c_algo;
	adap->dev.parent = &pdev->dev;
#ifdef CONFIG_OF
	adap->dev.of_node = of_node_get(pdev->dev.of_node);
#endif
	/* i2c device drivers may be active on return from add_adapter() */
	adap->nr = pdev->id;
	r = i2c_add_numbered_adapter(adap);
	if (r) {
		dev_err(dev->dev, "failure adding adapter\n");
		goto err_iounmap;
	}
	return 0;

err_iounmap:
	iounmap(dev->base);
err_free_mem:
	platform_set_drvdata(pdev, NULL);
	kfree(dev);
err_release_region:
	release_mem_region(mem->start, resource_size(mem));

	return r;
}

static int ls_i2c_remove(struct platform_device *pdev)
{
	struct ls_i2c_dev	*dev = platform_get_drvdata(pdev);
	struct resource		*mem;

	platform_set_drvdata(pdev, NULL);
	i2c_del_adapter(&dev->adapter);
	iounmap(dev->base);
	kfree(dev);
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	return 0;
}

#ifdef CONFIG_PM
static int ls_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ls_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	i2c_dev->suspended = 1;

	return 0;
}

static int ls_i2c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ls_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	i2c_dev->suspended = 0;
	ls_i2c_init(i2c_dev);

	return 0;
}

static const struct dev_pm_ops ls_i2c_dev_pm_ops = {
	.suspend_noirq	= ls_i2c_suspend_noirq,
	.resume		= ls_i2c_resume,
};

#define LS_DEV_PM_OPS (&ls_i2c_dev_pm_ops)
#else
#define LS_DEV_PM_OPS NULL
#endif
#ifdef CONFIG_OF
static struct of_device_id ls_i2c_id_table[] = {
	{.compatible = "loongson,ls-i2c"},
	{},
};
#endif
static struct platform_driver ls_i2c_driver = {
	.probe		= ls_i2c_probe,
	.remove		= ls_i2c_remove,
	.driver		= {
		.name = "ls-i2c",
		.owner = THIS_MODULE,
		.pm = LS_DEV_PM_OPS,
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_i2c_id_table),
#endif
	},
};

static int __init ls_i2c_init_driver(void)
{
	return platform_driver_register(&ls_i2c_driver);
}
subsys_initcall(ls_i2c_init_driver);

static void __exit ls_i2c_exit_driver(void)
{
	platform_driver_unregister(&ls_i2c_driver);
}

module_exit(ls_i2c_exit_driver);

MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_DESCRIPTION("LOONGSON I2C bus adapter");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ls-i2c");
