#
# Makefile for Loongson-3 family machines
#
obj-y			+= irq.o cpu.o

obj-$(CONFIG_SMP)	+= smp.o

obj-$(CONFIG_NUMA)	+= numa.o

obj-$(CONFIG_RS780_HPET) += hpet.o

obj-$(CONFIG_HOTPLUG_CPU)	+= cpuplug_war.o
