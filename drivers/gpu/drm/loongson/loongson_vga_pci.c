/****************************************************************************
*
*    Copyright (C) 2018 by Loongson Technology Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
/*#include "platform_driver.h"*/
#include "loongson_drv.h"

#include <loongson-pch.h>
#define DEVICE_NAME "loongson-vga"

#if 1

static int loongson_vga_platform_suspend(struct platform_device *pDevice, pm_message_t state)
{
	DBG("");
	return 0;
}

static int loongson_vga_platform_resume(struct platform_device *device)
{
	DBG("");
	return 0;
}

static void loongson_vga_platform_shutdown(struct platform_device *device)
{
	DBG("");
}

static int loongson_vga_platform_probe(struct platform_device *device)
{
	DBG("%s", device->name);
	return drm_platform_init(&loongson_vga_drm_driver, device);
}

static int loongson_vga_platform_remove(struct platform_device *device)
{
	DBG("");
	drm_platform_exit(&loongson_vga_drm_driver, device);
	return 0;
}



static struct platform_driver loongson_vga_platform_drv = {
		.driver = {
			.name = "loongson-vga",
			.owner = THIS_MODULE,
		},
		.probe = loongson_vga_platform_probe,
		.remove = loongson_vga_platform_remove,
		.suspend = loongson_vga_platform_suspend,
		.resume = loongson_vga_platform_resume,
		.shutdown = loongson_vga_platform_shutdown,
};

#endif

static struct pci_device_id loongson_vga_pci_devices[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC)},
	{0, 0, 0, 0, 0, 0, 0}
};



static struct resource loongson_vga_resources[] = {
	[0] = {
		.name	= "vga_base",
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "vga_irq",
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= "vga_mem",
		.start	= 0x0000a000000,
		.end	= 0x0000effffff,
		.flags	= IORESOURCE_MEM,
	},
};


static int loongson_vga_pci_register(struct pci_dev *pdev,
				 const struct pci_device_id *ent)

{
	int ret;
	struct pci_dev *gpu_pdev;

	pr_debug("loongson_vga_pci_register BEGIN\n");

	/* Enable device in PCI config */
	ret = pci_enable_device(pdev);
	if (ret < 0) {
		printk(KERN_ERR "loongson vga devices (%s): Cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	/* request the mem regions */
	ret = pci_request_region(pdev, 0, "loongson vga io");
	if (ret < 0) {
		printk( KERN_ERR "loongson fb (%s): cannot request region 0.\n",
			pci_name(pdev));
		goto err_out;
	}

	loongson_vga_resources[0].start = pci_resource_start (pdev, 0);
	loongson_vga_resources[0].end = pci_resource_end(pdev, 0);
	loongson_vga_resources[1].start = pdev->irq;
	loongson_vga_resources[1].end = pdev->irq;
#ifdef CONFIG_CPU_LOONGSON3
	gpu_pdev= pci_get_device(PCI_VENDOR_ID_LOONGSON,PCI_DEVICE_ID_LOONGSON_GPU ,NULL);
	loongson_vga_resources[2].start = pci_resource_start (gpu_pdev,2);
	loongson_vga_resources[2].end = pci_resource_end(gpu_pdev, 2);
	pci_enable_device_mem(gpu_pdev);
#endif
	platform_device_register(&loongson_vga_device);

	return 0;
err_out:
	return ret;
}

static void loongson_vga_pci_unregister(struct pci_dev *pdev)
{

	platform_device_unregister(&loongson_vga_device);
	pci_release_region(pdev, 0);
}

int loongson_vga_pci_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	pci_save_state(pdev);
	return 0;
}

int loongson_vga_pci_resume(struct pci_dev *pdev)
{
	return 0;
}


static struct pci_driver loongson_vga_pci_driver = {
	.name		= "loongson-vga-pci",
	.id_table	= loongson_vga_pci_devices,
	.probe		= loongson_vga_pci_register,
	.remove		= loongson_vga_pci_unregister,
#ifdef	CONFIG_SUSPEND
	.suspend = loongson_vga_pci_suspend,
	.resume	 = loongson_vga_pci_resume,
#endif
};

static int __init loongson_vga_pci_init(void)
{
	int ret;
	struct pci_dev *pdev = NULL;
	/*if PCIE Graphics card exist,use it as default*/
	pdev = pci_get_device(PCI_VENDOR_ID_ATI, PCI_ANY_ID, NULL);
	if(pdev)
			return 0;
	ret = platform_driver_register(&loongson_vga_platform_driver);
	if(ret) return ret;
	ret = pci_register_driver (&loongson_vga_pci_driver);
	return ret;

}

static void __exit loongson_vga_pci_exit(void)
{
	platform_driver_unregister(&loongson_vga_platform_driver);
	pci_unregister_driver (&loongson_vga_pci_driver);
}

module_init(loongson_vga_pci_init);
module_exit(loongson_vga_pci_exit);
MODULE_DESCRIPTION("Loongson Graphics VGA PCI Driver");
MODULE_LICENSE("GPL");
