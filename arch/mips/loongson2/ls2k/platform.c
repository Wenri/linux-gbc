/*
 *  Copyright (C) 2013, Loongson Technology Corporation Limited, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial_8250.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/delay.h>
#include <linux/stmmac.h>
#include <linux/i2c.h>
#include <linux/phy.h>

#include <ls2k.h>
#include <ls2k_int.h>
#include <linux/i2c-gpio.h>

int hpet_enabled = 1;
/*
 * UART
 */
struct plat_serial8250_port ls2k_uart8250_data[] = {
	[0] = {
		.mapbase = CKSEG1ADDR(LS2K_UART0_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART0_REG_BASE),	.irq = LS2K_UART0_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[1] = {
		.mapbase = CKSEG1ADDR(LS2K_UART1_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART1_REG_BASE),	.irq = LS2K_UART0_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[2] = {
		.mapbase = CKSEG1ADDR(LS2K_UART2_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART2_REG_BASE),	.irq = LS2K_UART0_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[3] = {
		.mapbase = CKSEG1ADDR(LS2K_UART3_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART3_REG_BASE),	.irq = LS2K_UART0_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[4] = {
		.mapbase = CKSEG1ADDR(LS2K_UART4_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART4_REG_BASE),	.irq = LS2K_UART4_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[5] = {
		.mapbase = CKSEG1ADDR(LS2K_UART5_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART5_REG_BASE),	.irq = LS2K_UART4_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[6] = {
		.mapbase = CKSEG1ADDR(LS2K_UART6_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART6_REG_BASE),	.irq = LS2K_UART4_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[7] = {
		.mapbase = CKSEG1ADDR(LS2K_UART7_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART7_REG_BASE),	.irq = LS2K_UART4_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[8] = {
		.mapbase = CKSEG1ADDR(LS2K_UART8_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART8_REG_BASE),	.irq = LS2K_UART8_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[9] = {
		.mapbase = CKSEG1ADDR(LS2K_UART9_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART9_REG_BASE),	.irq = LS2K_UART8_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[10] = {
		.mapbase = CKSEG1ADDR(LS2K_UART10_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART10_REG_BASE),	.irq = LS2K_UART8_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	[11] = {
		.mapbase = CKSEG1ADDR(LS2K_UART11_REG_BASE),	.uartclk = 125000000,
		.membase = (void *)CKSEG1ADDR(LS2K_UART11_REG_BASE),	.irq = LS2K_UART8_IRQ,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST, 	.iotype = UPIO_MEM,
		.regshift = 0,
	},
	{}
};

static struct platform_device uart8250_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM1,
	.dev = {
		.platform_data = ls2k_uart8250_data,
	}
};
/*
 * SPI
 */
#ifdef CONFIG_SPI_LS2K
static struct resource ls2k_spi0_resources[]={
	[0]={
		.start      =   LS2K_SPI_REG_BASE,
		.end        =   LS2K_SPI_REG_BASE+ 0x6 ,
		.flags      =   IORESOURCE_MEM,
	},
};

static struct platform_device ls2k_spi0_device={
	.name   =   "ls2k-spi",
	.id     =       0,
	.num_resources  =ARRAY_SIZE(ls2k_spi0_resources),
	.resource       =ls2k_spi0_resources,
};

static struct spi_board_info ls2k_spi_device[] = {
	{
		.modalias = "spidev",
		.bus_num = 0,
		.chip_select = 0,
		.max_speed_hz = 100000000,
	},
};
#endif
/*
 * NAND
 */
#ifdef CONFIG_MTD_NAND_LS2K
static struct mtd_partition ls2k_nand_partitions[]={
	[0] = {
		.name   = "kernel",
		.offset = 0,
		.size   = 0x01400000,
	},
	[1] = {
		.name   = "os",
		.offset = 0x01400000,
		.size   = 0x0,

	},
};

static struct ls2k_nand_plat_data ls2k_nand_parts = {
        .enable_arbiter = 1,
        .parts          = ls2k_nand_partitions,
        .nr_parts       = ARRAY_SIZE(ls2k_nand_partitions),
	.chip_ver	= LS2K_VER3,
	.cs = 2,
	.csrdy = 0x11<<(2*9),
};

static struct resource ls2k_nand_resources[] = {
	[0] = {
		.start      = LS2K_NAND_DMA_ACC_REG,
		.end        = LS2K_NAND_DMA_ACC_REG,
		.flags      = IORESOURCE_MEM,
	},
	[1] = {
		.start      = LS2K_NAND_REG_BASE,
		.end        = LS2K_NAND_REG_BASE + 0x20,
		.flags      = IORESOURCE_MEM,
	},
	[2] = {
		.start      = LS2K_DMA0_REG,
		.end        = LS2K_DMA0_REG,
		.flags      = IORESOURCE_DMA,
	},
	[3] = {
		.start      = LS2K_DMA0_IRQ,
		.end        = LS2K_DMA0_IRQ,
		.flags      = IORESOURCE_IRQ,
	},
};

struct platform_device ls2k_nand_device = {
	.name       = "ls2k-nand",
	.id         = 0,
	.dev        = {
		.platform_data = &ls2k_nand_parts,
	},
	.num_resources  = ARRAY_SIZE(ls2k_nand_resources),
	.resource       = ls2k_nand_resources,
};
#endif

/*
 * I2C
 */
static struct resource ls2k_i2c0_resources[] = {
	[0] = {
		.start = LS2K_I2C0_REG_BASE,
		.end   = LS2K_I2C0_REG_BASE + 0x8,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = LS2K_I2C0_IRQ,
		.end   = LS2K_I2C0_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ls2k_i2c0_device = {
	.name           = "ls2k-i2c",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(ls2k_i2c0_resources),
	.resource       = ls2k_i2c0_resources,
};

static struct resource ls2k_i2c1_resources[] = {
	[0] = {
		.start = LS2K_I2C1_REG_BASE,
		.end   = LS2K_I2C1_REG_BASE + 0x8,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = LS2K_I2C1_IRQ,
		.end   = LS2K_I2C1_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ls2k_i2c1_device = {
	.name           = "ls2k-i2c",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(ls2k_i2c1_resources),
	.resource       = ls2k_i2c1_resources,
};

/*
 * RTC
 */
static struct resource ls2k_rtc_resources[] = {
       [0] = {
               .start  = LS2K_RTC_REG_BASE,
               .end    = (LS2K_RTC_REG_BASE + 0xff),
               .flags  = IORESOURCE_MEM,
       },
       [1] = {
               .start  = LS2K_RTC_INT0_IRQ,
               .end    = LS2K_RTC_INT0_IRQ,
               .flags  = IORESOURCE_IRQ,
       },
};

static struct platform_device ls2k_rtc_device = {
       .name   = "ls2k-rtc",
       .id     = 0,
       .num_resources  = ARRAY_SIZE(ls2k_rtc_resources),
       .resource       = ls2k_rtc_resources,
};


/*
 * GPIO-I2C
 */
static struct i2c_gpio_platform_data pdata = {
	.sda_pin                = LS2K_GPIO_PIN_1,
	.sda_is_open_drain      = 0,
	.scl_pin                = LS2K_GPIO_PIN_0,
	.scl_is_open_drain      = 0,
	.udelay                 = 100,
};

static struct platform_device ls2k_gpio_i2c_device = {
	.name                   = "i2c-gpio",
	.id                     = 2,
	.num_resources          = 0,
	.resource               = NULL,
	.dev                    = {
		.platform_data  = &pdata,
	},
};

/*
 * I2S_UDA1342
 */
#ifdef CONFIG_SOUND_LS2K_UDA1342
static struct resource ls2k_uda1342_resource[] = {
       [0]={
               .start    = LS2K_I2S_REG_BASE,
               .end      = (LS2K_I2S_REG_BASE + 0x10),
               .flags    = IORESOURCE_MEM,
       },
       [1] = {
               .start    = LS2K_DMA2_IRQ,
               .end      = LS2K_DMA2_IRQ,
               .flags    = IORESOURCE_IRQ,
       },
       [2] = {
               .start    = LS2K_DMA3_IRQ,
               .end      = LS2K_DMA3_IRQ,
               .flags    = IORESOURCE_IRQ,
       },
       [3] = {
               .start    = LS2K_DMA2_REG,
               .end      = LS2K_DMA2_REG,
               .flags    = IORESOURCE_DMA,
       },
       [4] = {
               .start    = LS2K_DMA3_REG,
               .end      = LS2K_DMA3_REG,
               .flags    = IORESOURCE_DMA,
       },
};

static struct platform_device ls2k_audio_device = {
       .name           = "ls2k-audio",
       .id             = -1,
       .num_resources  = ARRAY_SIZE(ls2k_uda1342_resource),
       .resource       = ls2k_uda1342_resource,
};
#endif

/*
 * SDIO
 */
#ifdef CONFIG_MMC_LS2K
static struct resource ls2k_sdio_resources[] = {
       [0] = {
               .start = LS2K_SDIO_REG_BASE,
               .end   = (LS2K_SDIO_REG_BASE + 0xfff),
               .flags = IORESOURCE_MEM,
       },
       [1] = {
               .start = LS2K_SDIO_IRQ,
               .end   = LS2K_SDIO_IRQ,
               .flags = IORESOURCE_IRQ,
       },
       [2] = {
               .start      = LS2K_DMA1_REG,
               .end        = LS2K_DMA1_REG,
               .flags      = IORESOURCE_DMA,
       },
};

static struct platform_device ls2k_sdio_device = {
       .name           = "ls2k_sdio",
       .id             = 0,
       .dev = {
               .dma_mask = &ls2k_sdio_device.dev.coherent_dma_mask,
               .coherent_dma_mask      = DMA_BIT_MASK(32),
               .platform_data = 0,
       },
       .num_resources  = ARRAY_SIZE(ls2k_sdio_resources),
       .resource       = ls2k_sdio_resources,
};
#endif

static struct platform_device *ls2k_platform_devices[] = {
	&uart8250_device,
#ifdef CONFIG_SPI_LS2K
	&ls2k_spi0_device,
#endif
#ifdef CONFIG_MTD_NAND_LS2K
	&ls2k_nand_device,
#endif
#ifdef CONFIG_SOUND_LS2K_UDA1342
	&ls2k_audio_device,
#endif
#ifdef CONFIG_MMC_LS2K
	&ls2k_sdio_device,
#endif
	&ls2k_i2c0_device,
	&ls2k_i2c1_device,
	&ls2k_rtc_device,
};

static struct platform_device *ls2k_i2c_gpio_platform_devices[] = {
	&ls2k_gpio_i2c_device,
};

const struct i2c_board_info __initdata ls2k_gmac_eep_info = {
	I2C_BOARD_INFO("eeprom-mac", 0x50),
};

const struct i2c_board_info __initdata ls2k_fb_eep_info = {
	I2C_BOARD_INFO("eeprom-edid", 0x50),
};

const struct i2c_board_info __initdata ls2k_dvi_fb_eep_info = {
	I2C_BOARD_INFO("dvi-eeprom-edid", 0x50),
};

const struct i2c_board_info __initdata ls2k_codec_uda1342_info = {
	I2C_BOARD_INFO("codec_uda1342", 0x1a),
};

const struct i2c_board_info __initdata ls2k_ds1338_info = {
	I2C_BOARD_INFO("ds1338", 0x68),
};

int __init ls2k_platform_init(void)
{

#define I2C_BUS_0 0
#define I2C_BUS_1 1

if(0)
{
	i2c_register_board_info(I2C_BUS_0, &ls2k_gmac_eep_info, 1);
}
	i2c_register_board_info(I2C_BUS_0, &ls2k_ds1338_info, 1);
	i2c_register_board_info(I2C_BUS_1, &ls2k_fb_eep_info, 1);
	i2c_register_board_info(2, &ls2k_dvi_fb_eep_info, 1);
	platform_add_devices(ls2k_i2c_gpio_platform_devices,
			ARRAY_SIZE(ls2k_i2c_gpio_platform_devices));
#ifdef CONFIG_SPI_LS2K
	spi_register_board_info(ls2k_spi_device, ARRAY_SIZE(ls2k_spi_device));
#endif
#ifdef CONFIG_SOUND_LS2K_UDA1342
	i2c_register_board_info(1, &ls2k_codec_uda1342_info, 1);
#endif
	return platform_add_devices(ls2k_platform_devices,
			ARRAY_SIZE(ls2k_platform_devices));
}

device_initcall(ls2k_platform_init);
