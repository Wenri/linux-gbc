#
# Makefile for Loongson-3 family machines
#
obj-y			+= irq.o cop2-ex.o ec_wpce775l.o ec_npce985x.o ec_it8528.o platform.o clock.o workarounds.o \
			ls2h-irq.o ls2h-platform.o rs780-irq.o rs780-platform.o ls7a-irq.o ls7a-platform.o \
			loongson3-memset.o loongson3-memcpy.o


obj-$(CONFIG_SMP)	+= smp.o

obj-$(CONFIG_NUMA)	+= numa.o

obj-$(CONFIG_SUSPEND)	+= pm.o sleep.o

obj-$(CONFIG_GS464E_NODE_COUNTER) += node_counter.o

obj-$(CONFIG_GS464V_STABLE_COUNTER) += stable_counter.o

obj-$(CONFIG_LOONGSON3_HPET_SUPPORT) += hpet.o

obj-$(CONFIG_LOONGSON3_CPUAUTOPLUG) += loongson3_cpuautoplug.o

obj-$(CONFIG_PCI_MSI)  += pci_msi.o ls7a-pci_msi.o rs780-pci_msi.o ls2h-pci_msi.o

obj-$(CONFIG_PCI)  += ls3a-pci.o

obj-$(CONFIG_USE_OF)  += dts/

obj-$(CONFIG_MEMORY_HOTPLUG) += rtasd.o
