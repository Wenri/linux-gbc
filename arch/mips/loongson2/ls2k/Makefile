#
# Makefile for LST ls2k evaluate board.
#

obj-y += setup.o init.o cmdline.o env.o time.o reset.o irq.o \
		pci.o bonito-irq.o mem.o machtype.o platform.o efi.o \
		ls2k-irq.o cop2-ex.o platform.o clock.o ls2k-be.o

obj-$(CONFIG_GPIOLIB) += gpio.o

#
# Serial port support
#
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o
obj-$(CONFIG_LOONGSON_UART_BASE) += uart_base.o

#
# Suspend Support
#


obj-$(CONFIG_SMP)	+= smp.o

obj-$(CONFIG_LS2K_PCIE) += ls2k-pci.o

obj-$(CONFIG_SWIOTLB) += dma-swiotlb.o

obj-$(CONFIG_PCI_MSI) += pci_msi.o

#
#Hpet Support
#
obj-$(CONFIG_LS2K_HPET) += hpet.o
