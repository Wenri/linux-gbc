obj-y += cpu_hwmon.o
obj-$(CONFIG_I2C_PIIX4) += emc1412.o tmp75.o
obj-$(CONFIG_LEMOTE3A_LAPTOP) += lemote3a-laptop.o
obj-$(CONFIG_LOONGSON_LAPTOP) += loongson-czc-laptop.o
obj-$(CONFIG_LOONGSON_EA_PM_HOTKEY) += loongson-ea-pm-hotkey.o
obj-$(CONFIG_LOONGSON_BITLAND_PM_HOTKEY) += loongson-bitland-pm-hotkey.o

ifdef CONFIG_CPU_LOONGSON3
obj-y += acpi_init.o wpce_fan.o sbx00_fan.o
obj-m += ec_rom.o at24c04.o
endif

ifdef CONFIG_MTD
obj-m += pmon_flash.o
endif
