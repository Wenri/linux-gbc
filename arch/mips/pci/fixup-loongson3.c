/*
 * fixup-loongson3.c
 *
 * Copyright (C) 2012 Lemote, Inc.
 * Author: Xiang Yu, xiangy@lemote.com
 *         Chen Huacai, chenhc@lemote.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/pci.h>
#include <irq.h>
#include <boot_param.h>
#include <workarounds.h>
#include <loongson-pch.h>
#include <loongson.h>

int plat_device_is_ls3a_pci(const struct device *dev);
int ls3a_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);
int ls3a_pcibios_dev_init(struct pci_dev *pdev);

static void print_fixup_info(const struct pci_dev * pdev)
{
	dev_info(&pdev->dev, "Device %x:%x, irq %d\n",
			pdev->vendor, pdev->device, pdev->irq);
}

int ls7a_get_irq_by_devfn(u8 slot, int fn)
{
	int irq;
	switch(slot)
	{
		default:
		case 2:
		/*APB 2*/
		irq = 0;
		break;

		case 3:
		/*GMAC0 3 0*/
		/*GMAC1 3 1*/
		irq = (fn == 0) ? 12 : 14;
		break;

		case 4:
		/* ohci:4 0 */
		/* ehci:4 1 */
		irq = (fn == 0) ? 49 : 48;
		break;

		case 5:
		/* ohci:5 0 */
		/* ehci:5 1 */
		irq = (fn == 0) ? 51 : 50;
		break;

		case 6:
		/* DC: 6 1 28 */
		/* GPU:6 0 29 */
		irq = (fn == 0) ? 29 : 28;
		break;

		case 7:
		/*HDA: 7 0 58 */
		irq = 58;
		break;

		case 8:
		/* sata */
		if (fn == 0)
			irq = 16;
		if (fn == 1)
			irq = 17;
		if (fn == 2)
			irq = 18;
		break;

		case 9:
		/* pcie_f0 port0 */
		irq = 32;
		break;

		case 10:
		/* pcie_f0 port1 */
		irq = 33;
		break;

		case 11:
		/* pcie_f0 port2 */
		irq = 34;
		break;

		case 12:
		/* pcie_f0 port3 */
		irq = 35;
		break;

		case 13:
		/* pcie_f1 port0 */
		irq = 36;
		break;

		case 14:
		/* pcie_f1 port1 */
		irq = 37;
		break;

		case 15:
		/* pcie_g0 port0 */
		irq = 40;
		break;

		case 16:
		/* pcie_g0 port1 */
		irq = 41;
		break;

		case 17:
		/* pcie_g1 port0 */
		irq = 42;
		break;

		case 18:
		/* pcie_g1 port1 */
		irq = 43;
		break;

		case 19:
		/* pcie_h port0 */
		irq = 38;
		break;

		case 20:
		/* pcie_h port1 */
		irq = 39;
		break;
	}
	return irq;
}

int __init ls7a_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_bus  *bus = dev->bus;
	unsigned char busnum = dev->bus->number;
	unsigned char pci_line = 0;
	int fn = dev->devfn & 7;
	int irq = 0;

	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &pci_line);
	if (pci_line == 0 || pci_line == 0xff) {
		if(busnum != 0)
		{
			while(bus->parent->parent)
				bus = bus->parent;
			slot = bus->self->devfn >> 3;
			fn = bus->self->devfn & 7;
		}

		irq = ls7a_get_irq_by_devfn(slot, fn);

		return LS7A_IOAPIC_IRQ_BASE + irq;
	} else {
		return pci_line;
	}
}

int __init rs780_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = 0;

	/* Only 3A3000 and successor guest with fixed irq */
	if(cpu_guestmode) {
		switch (dev->vendor) {
	        case 0x1af4:
	            if (dev->device == 0x1000)
	                irq = VIRTDEV_NET_VIRTIO_IRQ;
	            else if (dev->device == 0x1001)
	                irq = VIRTDEV_BLK_VIRTIO_IRQ;
	            else if (dev->device == 0x1002)
	                irq = VIRTDEV_BALLOON_VIRTIO_IRQ;
	            else if (dev->device == 0x1003)
	                irq = VIRTDEV_SERIAL_VIRTIO_IRQ;
	            else if (dev->device == 0x1004)
	                irq = VIRTDEV_SCSI_VIRTIO_IRQ;
	            else if (dev->device == 0x1050)
	                irq = VIRTDEV_GPU_VIRTIO_IRQ;
	        break;
	        case 0x1b36:
	            if (dev->device == 0x100)
	                irq = VIRTDEV_QXL_IRQ;
	        break;
	        default:
			irq = VIRTDEV_IRQ_DEFAULT;
	        break;
	    }
	} else {
		print_fixup_info(dev);
		return dev->irq;
	}
	return irq;
}

static void pci_fixup_radeon(struct pci_dev *pdev)
{
	if (pdev->resource[PCI_ROM_RESOURCE].start)
		return;

	if (!vgabios_addr)
		return;

	pdev->resource[PCI_ROM_RESOURCE].start  = vgabios_addr;
	pdev->resource[PCI_ROM_RESOURCE].end    = vgabios_addr + 256*1024 - 1;
	pdev->resource[PCI_ROM_RESOURCE].flags |= IORESOURCE_ROM_COPY;

	dev_info(&pdev->dev, "BAR %d: assigned %pR for Radeon ROM\n",
			PCI_ROM_RESOURCE, &pdev->resource[PCI_ROM_RESOURCE]);
}

DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
				PCI_CLASS_DISPLAY_VGA, 8, pci_fixup_radeon);

int __init ls2h_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return LS2H_PCH_PCIE_PORT0_IRQ + LS2H_PCIE_GET_PORTNUM(dev->sysdata);;
}

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if(plat_device_is_ls3a_pci(&dev->dev))
		return ls3a_pci_map_irq(dev, slot, pin);

	return loongson_pch->pcibios_map_irq(dev, slot, pin);
}

int ls7a_pcibios_dev_init(struct pci_dev *dev)
{
	return 0;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	int ret;
	init_dma_attrs(&dev->dev.archdata.dma_attrs);
	if (loongson_workarounds & WORKAROUND_PCIE_DMA)
		dma_set_attr(DMA_ATTR_FORCE_SWIOTLB, &dev->dev.archdata.dma_attrs);

	if(plat_device_is_ls3a_pci(&dev->dev))
		return ls3a_pcibios_dev_init(dev);

	if (loongson_pch->pcibios_dev_init)
		ret = loongson_pch->pcibios_dev_init(dev);
	else
		ret = 0;

	return ret;
}
