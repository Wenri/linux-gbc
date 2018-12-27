#
# Makefile for Loongson-3 family machines
#
obj-y			+= irq.o cop2-ex.o ec_wpce775l.o ec_npce985x.o platform.o clock.o workarounds.o \
			   ls2h-irq.o ls2h-platform.o rs780-irq.o rs780-platform.o loongson3-memset.o loongson3-memcpy.o

obj-$(CONFIG_SMP)	+= smp.o

obj-$(CONFIG_NUMA)	+= numa.o

obj-$(CONFIG_SUSPEND)	+= pm.o sleep.o

obj-$(CONFIG_GS464E_NODE_COUNTER) += node_counter.o

obj-$(CONFIG_RS780_HPET) += hpet.o

obj-$(CONFIG_LOONGSON3_CPUAUTOPLUG) += loongson3_cpuautoplug.o

obj-$(CONFIG_KVM_GUEST_LOONGSON_VZ) += rtasd.o
