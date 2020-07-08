#ifndef _LSFB_H
#define _LSFB_H

#ifdef CONFIG_CPU_LOONGSON2K
#include <ls2k.h>
#define DEFAULT_BITS_PER_PIXEL		16
#define PD_DC_PLL					19
#define LS_PIX0_PLL					LS2K_PIX0_PLL
#define LS_PIX1_PLL					LS2K_PIX1_PLL
#define ls_readl					ls2k_readl
#define ls_readq					ls2k_readq
#define ls_writel					ls2k_writel
#define ls_writeq					ls2k_writeq
#else
#include <loongson-pch.h>
#include "config-ch7034.h"

#define DEFAULT_BITS_PER_PIXEL		32
#define HT1LO_PCICFG_BASE			0x1a000000
#define LS7A_PCH_CFG_SPACE_REG		(TO_UNCAC(HT1LO_PCICFG_BASE)|0x0000a810)
#define LS7A_PCH_CFG_REG_BASE		((*(volatile unsigned int *)(LS7A_PCH_CFG_SPACE_REG))&0xfffffff0)

#define LS_PIX0_PLL				(LS7A_PCH_CFG_REG_BASE + 0x04b0)
#define LS_PIX1_PLL				(LS7A_PCH_CFG_REG_BASE + 0x04c0)

#define ls_readl					ls7a_readl
#define ls_readq					ls7a_readq
#define ls_writel					ls7a_writel
#define ls_writeq					ls7a_writeq

#endif /* CONFIG_CPU_LOONGSON2K */

#ifdef CONFIG_CPU_LOONGSON3
static unsigned long	lsfb_mem;
static unsigned int		lsfb_dma;
#endif

#define DEFAULT_ADDRESS_FB_MEM		0x900000000ef00000
#define DEFAULT_ADDRESS_FB_DMA		0x0ef00000
#define DEFAULT_ADDRESS_CURSOR_MEM	0x900000000e000000
#define DEFAULT_ADDRESS_PHY_ADDR	0x000000000e000000
#define DEFAULT_ADDRESS_CURSOR_DMA	0x0e000000

#define LSFB_OFFSET		(0xf << 20)
#define LSFB_GPU_MASK	(~(0xffUL << 56))
u64 ls_cursor_mem;
u32 ls_cursor_dma;
u64 ls_fb_mem;
u64 ls_phy_addr;
u32 ls_fb_dma;

#define ON	1
#define OFF	0

#define CUR_WIDTH_SIZE		32
#define CUR_HEIGHT_SIZE		32

#define LO_OFF	0
#define HI_OFF	8

#define DEFAULT_CURSOR_MEM		ls_cursor_mem
#define DEFAULT_CURSOR_DMA		ls_cursor_dma
#define DEFAULT_FB_MEM			ls_fb_mem
#define DEFAULT_PHY_ADDR		ls_phy_addr
#define DEFAULT_FB_DMA			ls_fb_dma

#define CURIOSET_CORLOR		0x4607
#define CURIOSET_POSITION	0x4608
#define CURIOLOAD_ARGB		0x4609
#define CURIOLOAD_IMAGE		0x460A
#define CURIOHIDE_SHOW		0x460B
#define FBEDID_GET			0X860C

#define LS_FB_CFG_DVO0_REG			(0x1240)
#define LS_FB_CFG_DVO1_REG			(0x1250)
#define LS_FB_ADDR0_DVO0_REG		(0x1260)
#define LS_FB_ADDR0_DVO1_REG		(0x1270)
#define LS_FB_STRI_DVO0_REG			(0x1280)
#define LS_FB_STRI_DVO1_REG			(0x1290)

#define LS_FB_DITCFG_DVO0_REG		(0x1360)
#define LS_FB_DITCFG_DVO1_REG		(0x1370)
#define LS_FB_DITTAB_LO_DVO0_REG	(0x1380)
#define LS_FB_DITTAB_LO_DVO1_REG	(0x1390)
#define LS_FB_DITTAB_HI_DVO0_REG	(0x13a0)
#define LS_FB_DITTAB_HI_DVO1_REG	(0x13b0)
#define LS_FB_PANCFG_DVO0_REG		(0x13c0)
#define LS_FB_PANCFG_DVO1_REG		(0x13d0)
#define LS_FB_PANTIM_DVO0_REG		(0x13e0)
#define LS_FB_PANTIM_DVO1_REG		(0x13f0)

#define LS_FB_HDISPLAY_DVO0_REG		(0x1400)
#define LS_FB_HDISPLAY_DVO1_REG		(0x1410)
#define LS_FB_HSYNC_DVO0_REG		(0x1420)
#define LS_FB_HSYNC_DVO1_REG		(0x1430)

#define LS_FB_VDISPLAY_DVO0_REG		(0x1480)
#define LS_FB_VDISPLAY_DVO1_REG		(0x1490)
#define LS_FB_VSYNC_DVO0_REG		(0x14a0)
#define LS_FB_VSYNC_DVO1_REG		(0x14b0)

#define LS_FB_GAMINDEX_DVO0_REG		(0x14e0)
#define LS_FB_GAMINDEX_DVO1_REG		(0x14f0)
#define LS_FB_GAMDATA_DVO0_REG		(0x1500)
#define LS_FB_GAMDATA_DVO1_REG		(0x1510)

#define LS_FB_CUR_CFG_REG			(0x1520)
#define LS_FB_CUR_ADDR_REG			(0x1530)
#define LS_FB_CUR_LOC_ADDR_REG		(0x1540)
#define LS_FB_CUR_BACK_REG			(0x1550)
#define LS_FB_CUR_FORE_REG			(0x1560)

#define LS_FB_INT_REG				(0x1570)

#define LS_FB_ADDR1_DVO0_REG		(0x1580)
#define LS_FB_ADDR1_DVO1_REG		(0x1590)

#define LS_FB_DAC_CTRL_REG			(0x1600)
#define LS_FB_DVO_OUTPUT_REG		(0x1630)

static int ls_fb_address_init(struct fb_info *info);
#endif /* LSFB_H */
