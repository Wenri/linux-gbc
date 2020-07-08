#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>

#define to_lm_device(d)	container_of(d, struct lm_device, dev)
#define to_lm_driver(d)	container_of(d, struct lm_driver, drv)
#include "lm.h"

static int dwc_otg_platform_driver_probe( struct platform_device *_dev);
static int dwc_otg_platform_driver_remove( struct platform_device *_dev);

static const char dwc_driver_name[] = "dwc_otg";

static struct of_device_id dwc_otg_id_table[] = {
     { .compatible = "loongson,dwc-otg", },
     { .compatible = "loongson,ls2k-otg", },
     { },
};

static struct platform_driver dwc_otg_platform_driver = {
	.driver = {
		.name = (char *)dwc_driver_name,
        .bus = &platform_bus_type,
        .of_match_table = of_match_ptr(dwc_otg_id_table),

		},
	.probe = dwc_otg_platform_driver_probe,
	.remove = dwc_otg_platform_driver_remove,
};


static int dwc_otg_platform_driver_remove(
     struct platform_device *_dev
)
{
	return 0;
}

static u64 dwc_dma_mask = 0xffffffff;
struct lm_device lm_device;
static int dwc_otg_platform_driver_probe(
struct platform_device *_dev
)
{
	int irq;
    __be32 *dma_mask_p = NULL;
    int retval;
    struct resource *res;

    if (_dev->dev.of_node) {
          dma_mask_p = (__be32 *)of_get_property(_dev->dev.of_node, "dma-mask", NULL);
          if (dma_mask_p != 0){
              dwc_dma_mask = of_read_number(dma_mask_p,2);
	          lm_device.dev.dma_mask = &dwc_dma_mask;
          }
    }

    irq = platform_get_irq(_dev, 0);
    if (irq < 0) {
        retval = irq;
        return retval;
    }

    lm_device.irq = irq;

    res = platform_get_resource(_dev, IORESOURCE_MEM, 0);
    if (!res) {
        retval = -ENODEV;
        return retval;
    }

    lm_device.resource.start = res->start;
    lm_device.resource.end   = res->end;

    lm_device.id = 0;

	return lm_device_register(&lm_device);
}



//--------------------



static int lm_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int lm_bus_probe(struct device *dev)
{
	struct lm_device *lmdev = to_lm_device(dev);
	struct lm_driver *lmdrv = to_lm_driver(dev->driver);

	return lmdrv->probe(lmdev);
}

static int lm_bus_remove(struct device *dev)
{
	struct lm_device *lmdev = to_lm_device(dev);
	struct lm_driver *lmdrv = to_lm_driver(dev->driver);

	if (lmdrv->remove)
		lmdrv->remove(lmdev);
	return 0;
}

static struct bus_type lm_bustype = {
	.name		= "logicmodule",
	.match		= lm_match,
	.probe		= lm_bus_probe,
	.remove		= lm_bus_remove,
//	.suspend	= lm_bus_suspend,
//	.resume		= lm_bus_resume,
};

static int __init lm_init(void)
{
	int retval;
	retval = bus_register(&lm_bustype);
	if(retval) {
		printk("%s:%d bus register error!\n",__func__, __LINE__);
		goto err;
	}
	retval = platform_driver_register(&dwc_otg_platform_driver);
err:
	return retval;
}

postcore_initcall(lm_init);

int lm_driver_register(struct lm_driver *drv)
{
	drv->drv.bus = &lm_bustype;
	return driver_register(&drv->drv);
}

void lm_driver_unregister(struct lm_driver *drv)
{
	driver_unregister(&drv->drv);
}

static void lm_device_release(struct device *dev)
{
	struct lm_device *d = to_lm_device(dev);

	kfree(d);
}

int lm_device_register(struct lm_device *dev)
{
	int ret;

	dev->dev.release = lm_device_release;
	dev->dev.bus = &lm_bustype;

	ret = dev_set_name(&dev->dev, "lm%d", dev->id);
	if (ret)
		return ret;
	dev->resource.name = dev_name(&dev->dev);

//	ret = request_resource(&iomem_resource, &dev->resource);
	if (ret == 0) {
		ret = device_register(&dev->dev);
		if (ret)
			release_resource(&dev->resource);
	}
	return ret;
}

EXPORT_SYMBOL(lm_driver_register);
EXPORT_SYMBOL(lm_driver_unregister);
MODULE_LICENSE("GPL");
