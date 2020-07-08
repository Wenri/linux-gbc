/*
 *  linux/drivers/video/ls_fb.c -- Virtual frame buffer device
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <asm/addrspace.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/addrspace.h>
#include "edid.h"
#include "lsfb.h"


#ifdef LS_FB_DEBUG
#define LS_DEBUG(frm, arg...)	\
	printk("ls_fb: %s %d: "frm, __func__, __LINE__, ##arg);
#else
#define LS_DEBUG(frm, arg...)
#endif /* LS_FB_DEBUG */

struct pix_pll {
	unsigned int l2_div;
	unsigned int l1_loopc;
	unsigned int l1_frefc;
};

static struct eep_info{
	struct i2c_adapter *adapter;
	unsigned short addr;
}eeprom_info;

struct ls_fb_par {
	struct platform_device *pdev;
	struct fb_info *fb_info;
	unsigned long reg_base;
	unsigned int irq;
	unsigned int htotal;
	unsigned int vtotal;
	u8 *edid;
};
static char *mode_option = NULL;
static void *cursor_mem;
static unsigned int cursor_size = 0x1000;
static void *videomemory;
static	dma_addr_t dma_A;
static dma_addr_t cursor_dma;

static bool edid_flag = 0;
static u_long videomemorysize = 0;
module_param(videomemorysize, ulong, 0);
static DEFINE_SPINLOCK(fb_lock);
static void config_pll(unsigned long pll_base, struct pix_pll *pll_cfg);
static struct fb_var_screeninfo ls_fb_default = {
	.xres		= 1280,
	.yres		= 1024,
	.xres_virtual	= 1280,
	.yres_virtual	= 1024,
	.xoffset	= 0,
	.yoffset	= 0,
	.bits_per_pixel = DEFAULT_BITS_PER_PIXEL,
	.red		= { 11, 5 ,0},
	.green		= { 5, 6, 0 },
	.blue		= { 0, 5, 0 },
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.pixclock	= 9259,
	.left_margin	= 248,
	.right_margin	= 48,
	.upper_margin	= 38,
	.lower_margin	= 1,
	.hsync_len	= 112,
	.vsync_len	= 3,
	.sync		= 3,
	.vmode =	FB_VMODE_NONINTERLACED,
};
static struct fb_fix_screeninfo ls_fb_fix = {
	.id =		"Virtual FB",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	1,
	.accel =	FB_ACCEL_NONE,
};

static bool ls_fb_enable = 0;	/* disabled by default */
module_param(ls_fb_enable, bool, 0);

/*
 *  Internal routines
 */

static u_long get_line_length(int xres_virtual, int bpp)
{
	u_long length;

	length = xres_virtual * bpp;
	length = (length + 31) & ~31;
	length >>= 3;
	return (length);
}

/*
 *  Setting the video mode has been split into two parts.
 *  First part, xxxfb_check_var, must not write anything
 *  to hardware, it should only verify and adjust var.
 *  This means it doesn't alter par but it does use hardware
 *  data from it to check this var.
 */

static int ls_fb_check_var(struct fb_var_screeninfo *var,
			 struct fb_info *info)
{
	u_long line_length;

	/*
	 *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
	 *  as FB_VMODE_SMOOTH_XPAN is only used internally
	 */

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}

	/*
	 *  Some very basic checks
	 */
	if (!var->xres)
		var->xres = 640;
	if (!var->yres)
		var->yres = 480;
	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;
	if (var->bits_per_pixel <= 1)
		var->bits_per_pixel = 1;
	else if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	/*
	 *  Memory limit
	 */
	line_length =
		get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (videomemorysize &&  line_length * var->yres_virtual > videomemorysize)
		return -ENOMEM;

	/*
	 * Now that we checked it we alter var. The reason being is that the video
	 * mode passed in might not work but slight changes to it might make it
	 * work. This way we let the user know what is acceptable.
	 */
	switch (var->bits_per_pixel) {
	case 1:
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 15:		/* RGBA 555 */
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;
	case 16:		/* BGR 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 24:		/* RGB 888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* ARGB 8888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	return 0;
}

#define PLL_REF_CLK_MHZ    100
#define PCLK_PRECISION_INDICATOR 10000

/*for PLL using configuration: (refc, loopc, div). Like LS7A1000 PLL
for C env without decimal fraction support.*/
static unsigned int cal_freq(unsigned int pixclock_khz, struct pix_pll * pll_config)
{
	unsigned int refc_set[] = {4, 5, 3};
	unsigned int prec_set[] = {1, 5, 10, 50, 100};   /*in 1/PCLK_PRECISION_INDICATOR*/
	unsigned int pstdiv, loopc, refc;
	int i, j;
	unsigned int precision_req, precision;
	unsigned int loopc_min, loopc_max, loopc_mid;
	unsigned long long real_dvo, req_dvo;
	int loopc_offset;

	/*try precision from high to low*/
	for (j = 0; j < sizeof(prec_set)/sizeof(int); j++){
		precision_req = prec_set[j];

		/*try each refc*/
		for (i = 0; i < sizeof(refc_set)/sizeof(int); i++) {
			refc = refc_set[i];
			loopc_min = (1200 / PLL_REF_CLK_MHZ) * refc;  /*1200 / (PLL_REF_CLK_MHZ / refc)*/
			loopc_max = (3200 / PLL_REF_CLK_MHZ) * refc;  /*3200 / (PLL_REF_CLK_MHZ / refc)*/
			loopc_mid = (2200 / PLL_REF_CLK_MHZ) * refc;  /*(loopc_min + loopc_max) / 2;*/

			loopc_offset = -1;
			/*try each loopc*/
			for (loopc = loopc_mid; (loopc <= loopc_max) && (loopc >= loopc_min); loopc += loopc_offset) {
				if(loopc_offset < 0){
					loopc_offset = -loopc_offset;
				}else{
					loopc_offset = -(loopc_offset+1);
				}

				pstdiv = loopc * PLL_REF_CLK_MHZ * 1000 / refc / pixclock_khz;
				if((pstdiv > 127) || (pstdiv < 1)) continue;

				/*real_freq is float type which is not available, but read_freq * pstdiv is available.*/
				real_dvo = (loopc * PLL_REF_CLK_MHZ * 1000 / refc);
				req_dvo  = (pixclock_khz * pstdiv);
				precision = abs(real_dvo * PCLK_PRECISION_INDICATOR / req_dvo - PCLK_PRECISION_INDICATOR);

				if(precision < precision_req){
					pll_config->l2_div = pstdiv;
					pll_config->l1_loopc = loopc;
					pll_config->l1_frefc = refc;
#ifdef LS_FB_DEBUG
					printk("for pixclock = %d khz, found: pstdiv = %d, "
					      "loopc = %d, refc = %d, precision = %d / %d.\n", pixclock_khz,
					      pll_config->l2_div, pll_config->l1_loopc, pll_config->l1_frefc, precision+1, PCLK_PRECISION_INDICATOR);
#endif
					if(j > 1){
						printk("Warning: PIX clock precision degraded to %d / %d\n", precision_req, PCLK_PRECISION_INDICATOR);
					}
					return 1;
				}
			}
		}
	}
	return 0;
}

static void config_pll(unsigned long pll_base, struct pix_pll *pll_cfg)
{
	unsigned long val;
#ifdef CONFIG_CPU_LOONGSON2K
     /* set sel_pll_out0 0 */
    val = ls_readq(pll_base);
    val &= ~(1UL << 0);
    ls_writeq(val, pll_base);

    /* pll_pd 1 */
    val = ls_readq(pll_base);
    val |= (1UL << 19);
    ls_writeq(val, pll_base);

    /* set_pll_param 0 */
    val = ls_readq(pll_base);
    val &= ~(1UL << 2);
    ls_writeq(val, pll_base);

    /* set new div ref, loopc, div out */
    /* clear old value first*/
	val = (1 << 7) | (1L << 42) | (3 << 10) |
		((unsigned long)(pll_cfg->l1_loopc) << 32) |
		((unsigned long)(pll_cfg->l1_frefc) << 26);
    ls_writeq(val, pll_base);
    ls_writeq(pll_cfg->l2_div, pll_base + 8);

    /* set_pll_param 1 */
    val = ls_readq(pll_base);
    val |= (1UL << 2);
    ls_writeq(val, pll_base);

    /* pll_pd 0 */
    val = ls_readq(pll_base);
    val &= ~(1UL << 19);
    ls_writeq(val, pll_base);

    /* wait pll lock */
    while(!(ls_readl(pll_base) & 0x10000));
    /* set sel_pll_out0 1 */
    val = ls_readq(pll_base);
    val |= (1UL << 0);
    ls_writeq(val, pll_base);
#else
	/* set sel_pll_out0 0 */
	val = ls_readl(pll_base + 0x4);
	val &= ~(1UL << 8);
	ls_writel(val, pll_base + 0x4);
	/* pll_pd 1 */
	val = ls_readl(pll_base + 0x4);
	val |= (1UL << 13);
	ls_writel(val, pll_base + 0x4);
	/* set_pll_param 0 */
	val = ls_readl(pll_base + 0x4);
	val &= ~(1UL << 11);
	ls_writel(val, pll_base + 0x4);

	/* set new div ref, loopc, div out */
	/* clear old value first*/
	val = ls_readl(pll_base + 0x4);
	val &= ~(0x7fUL << 0);
	val |= (pll_cfg->l1_frefc << 0);
	ls_writel(val, pll_base + 0x4);

	val = ls_readl(pll_base + 0x0);
	val &= ~(0x7fUL << 0);
	val |= (pll_cfg->l2_div << 0);
	val &= ~(0x1ffUL << 21);
	val |= (pll_cfg->l1_loopc << 21);
	ls_writel(val, pll_base + 0x0);

	/* set_pll_param 1 */
	val = ls_readl(pll_base + 0x4);
	val |= (1UL << 11);
	ls_writel(val, pll_base + 0x4);
	/* pll_pd 0 */
	val = ls_readl(pll_base + 0x4);
	val &= ~(1UL << 13);
	ls_writel(val, pll_base + 0x4);
	/* wait pll lock */
	while(!(ls_readl(pll_base + 0x4) & 0x80));
	/* set sel_pll_out0 1 */
	val = ls_readl(pll_base + 0x4);
	val |= (1UL << 8);
	ls_writel(val, pll_base + 0x4);
#endif
}
static void ls_reset_cursor_image(void)
{
	u8 __iomem *addr = (u8 *)DEFAULT_CURSOR_MEM;
	memset(addr, 0, 32*32*4);
}

static int ls_init_regs(struct fb_info *info)
{
	unsigned int pix_freq;
	unsigned int depth;
	unsigned int hr, hss, hse, hfl;
	unsigned int vr, vss, vse, vfl;
	int ret;
	struct pix_pll pll_cfg;
	struct fb_var_screeninfo *var = &info->var;
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;

	hr	= var->xres;
	hss	= hr + var->right_margin;
	hse	= hss + var->hsync_len;
	hfl	= hse + var->left_margin;

	vr	= var->yres;
	vss	= vr + var->lower_margin;
	vse	= vss + var->vsync_len;
	vfl	= vse + var->upper_margin;

	depth = var->bits_per_pixel;
	pix_freq = PICOS2KHZ(var->pixclock);

	ret = cal_freq(pix_freq, &pll_cfg);
	if (ret) {
		config_pll(LS_PIX0_PLL, &pll_cfg);
		config_pll(LS_PIX1_PLL, &pll_cfg);
	}

	ls_writel(dma_A, base + LS_FB_ADDR0_DVO0_REG);
	ls_writel(dma_A, base + LS_FB_ADDR0_DVO1_REG);
	ls_writel(dma_A, base + LS_FB_ADDR1_DVO0_REG);
	ls_writel(dma_A, base + LS_FB_ADDR1_DVO1_REG);
	ls_writel(0, base + LS_FB_DITCFG_DVO0_REG);
	ls_writel(0, base + LS_FB_DITCFG_DVO1_REG);
	ls_writel(0, base + LS_FB_DITTAB_LO_DVO0_REG);
	ls_writel(0, base + LS_FB_DITTAB_LO_DVO1_REG);
	ls_writel(0, base + LS_FB_DITTAB_HI_DVO0_REG);
	ls_writel(0, base + LS_FB_DITTAB_HI_DVO1_REG);
	ls_writel(0x80001311, base + LS_FB_PANCFG_DVO0_REG);
	ls_writel(0x80001311, base + LS_FB_PANCFG_DVO1_REG);
	ls_writel(0x00000000, base + LS_FB_PANTIM_DVO0_REG);
	ls_writel(0x00000000, base + LS_FB_PANTIM_DVO1_REG);

/* these 4 lines cause out of range, because
 * the hfl hss vfl vss are different with PMON vgamode cfg.
 * So the refresh freq in kernel and refresh freq in PMON are different.
 * */
	ls_writel((hfl << 16) | hr, base + LS_FB_HDISPLAY_DVO0_REG);
	ls_writel((hfl << 16) | hr, base + LS_FB_HDISPLAY_DVO1_REG);
	ls_writel(0x40000000 | (hse << 16) | hss, base + LS_FB_HSYNC_DVO0_REG);
	ls_writel(0x40000000 | (hse << 16) | hss, base + LS_FB_HSYNC_DVO1_REG);
	ls_writel((vfl << 16) | vr, base + LS_FB_VDISPLAY_DVO0_REG);
	ls_writel((vfl << 16) | vr, base + LS_FB_VDISPLAY_DVO1_REG);
	ls_writel(0x40000000 | (vse << 16) | vss, base + LS_FB_VSYNC_DVO0_REG);
	ls_writel(0x40000000 | (vse << 16) | vss, base + LS_FB_VSYNC_DVO1_REG);

	switch (depth) {
	case 32:
	case 24:
	if(edid_flag == 0){
		ls_writel(0x00100104, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100304, base + LS_FB_CFG_DVO1_REG);
	}else{
		ls_writel(0x00100304, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100104, base + LS_FB_CFG_DVO1_REG);
	}
		ls_writel((hr * 4 + 255) & ~255, base + LS_FB_STRI_DVO0_REG);
		ls_writel((hr * 4 + 255) & ~255, base + LS_FB_STRI_DVO1_REG);
		break;
	case 16:
	if(edid_flag == 0){
		ls_writel(0x00100103, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100303, base + LS_FB_CFG_DVO1_REG);
	}else{
		ls_writel(0x00100303, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100103, base + LS_FB_CFG_DVO1_REG);
	}
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO0_REG);
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO1_REG);
		break;
	case 15:
	if(edid_flag == 0){
		ls_writel(0x00100102, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100302, base + LS_FB_CFG_DVO1_REG);
	}else{
		ls_writel(0x00100302, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100102, base + LS_FB_CFG_DVO1_REG);
	}
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO0_REG);
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO1_REG);
		break;
	case 12:
	if(edid_flag == 0){
		ls_writel(0x00100101, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100301, base + LS_FB_CFG_DVO1_REG);
	}else{
		ls_writel(0x00100301, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100101, base + LS_FB_CFG_DVO1_REG);
	}
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO0_REG);
		ls_writel((hr * 2 + 255) & ~255, base + LS_FB_STRI_DVO1_REG);
		break;
	default:
	if(edid_flag == 0){
		ls_writel(0x00100104, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100304, base + LS_FB_CFG_DVO1_REG);
	}else{
		ls_writel(0x00100304, base + LS_FB_CFG_DVO0_REG);
		ls_writel(0x00100104, base + LS_FB_CFG_DVO1_REG);
	}
		ls_writel((hr * 4 + 255) & ~255, base + LS_FB_STRI_DVO0_REG);
		ls_writel((hr * 4 + 255) & ~255, base + LS_FB_STRI_DVO1_REG);
		break;
	}

	/* cursor */
	/* Select full color ARGB mode */
	if(edid_flag == 0){
		ls_writel(0x00050202,base + LS_FB_CUR_CFG_REG);
	}else{
		ls_writel(0x00050212,base + LS_FB_CUR_CFG_REG);
	}
	ls_writel(cursor_dma,base + LS_FB_CUR_ADDR_REG);
	ls_writel(0x00060122,base + LS_FB_CUR_LOC_ADDR_REG);
	ls_writel(0x00eeeeee,base + LS_FB_CUR_BACK_REG);
	ls_writel(0x00aaaaaa,base + LS_FB_CUR_FORE_REG);
	ls_reset_cursor_image();

	return 0;
}

#ifdef LS_FB_DEBUG
void show_var(struct fb_var_screeninfo *var)
{
	printk(" xres: %d\n"
		" yres: %d\n",
		var->xres,
		var->yres);
}
#endif

/* This routine actually sets the video mode. It's in here where we
 * the hardware state info->par and fix which can be affected by the
 * change in par. For this driver it doesn't do much.
 */
static int ls_fb_set_par(struct fb_info *info)
{
	unsigned long flags;
	info->fix.line_length = get_line_length(info->var.xres_virtual,
						info->var.bits_per_pixel);
#ifdef LS_FB_DEBUG
	show_var(&info->var);
#endif
	spin_lock_irqsave(&fb_lock, flags);

	ls_init_regs(info);

	spin_unlock_irqrestore(&fb_lock, flags);
	return 0;
}

/*
 *  Set a single color register. The values supplied are already
 *  rounded down to the hardware's capabilities (according to the
 *  entries in the var structure). Return != 0 for invalid regno.
 */

static int ls_fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
	if (regno >= 256)	/* no. of hw registers */
		return 1;
	/*
	 * Program hardware... do anything you want with transp
	 */

	/* grayscale works only partially under directcolor */
	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue =
			(red * 77 + green * 151 + blue * 28) >> 8;
	}

	/* Directcolor:
	 *   var->{color}.offset contains start of bitfield
	 *   var->{color}.length contains length of bitfield
	 *   {hardwarespecific} contains width of RAMDAC
	 *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
	 *   RAMDAC[X] is programmed to (red, green, blue)
	 *
	 * Pseudocolor:
	 *    uses offset = 0 && length = RAMDAC register width.
	 *    var->{color}.offset is 0
	 *    var->{color}.length contains widht of DAC
	 *    cmap is not used
	 *    RAMDAC[X] is programmed to (red, green, blue)
	 * Truecolor:
	 *    does not use DAC. Usually 3 are present.
	 *    var->{color}.offset contains start of bitfield
	 *    var->{color}.length contains length of bitfield
	 *    cmap is programmed to (red << red.offset) | (green << green.offset) |
	 *                      (blue << blue.offset) | (transp << transp.offset)
	 *    RAMDAC does not exist
	 */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		red = CNVT_TOHW(red, info->var.red.length);
		green = CNVT_TOHW(green, info->var.green.length);
		blue = CNVT_TOHW(blue, info->var.blue.length);
		transp = CNVT_TOHW(transp, info->var.transp.length);
		break;
	case FB_VISUAL_DIRECTCOLOR:
		red = CNVT_TOHW(red, 8);	/* expect 8 bit DAC */
		green = CNVT_TOHW(green, 8);
		blue = CNVT_TOHW(blue, 8);
		/* hey, there is bug in transp handling... */
		transp = CNVT_TOHW(transp, 8);
		break;
	}
#undef CNVT_TOHW
	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;

		if (regno >= 16)
			return 1;

		v = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset) |
			(transp << info->var.transp.offset);
		switch (info->var.bits_per_pixel) {
		case 8:
			break;
		case 16:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		case 24:
		case 32:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		}
		return 0;
	}
	return 0;
}

static int ls_fb_blank (int blank_mode, struct fb_info *info)
{
#ifdef CONFIG_CPU_LOONGSON2K
	u64 val,reg;
	val = ls_readq(LS_PIX0_PLL);
	reg = ls_readq(LS_PIX1_PLL);
	switch (blank_mode) {
		case FB_BLANK_VSYNC_SUSPEND:
			break;
		case FB_BLANK_HSYNC_SUSPEND:
			break;
		case FB_BLANK_POWERDOWN:
			val |= (1 << PD_DC_PLL);
			reg |= (1 << PD_DC_PLL);
			break;
		case FB_BLANK_NORMAL:
			break;
		case FB_BLANK_UNBLANK:
			val &= ~(1 << PD_DC_PLL);
			reg &= ~(1 << PD_DC_PLL);
			break;
    }
	ls_writeq(val,LS_PIX0_PLL);
	ls_writeq(reg,LS_PIX1_PLL);
#endif
	return 0;
}

/*************************************************************
 *                Hardware Cursor Routines                   *
 *************************************************************/

/**
 * ls_enable_cursor - show or hide the hardware cursor
 * @mode: show (1) or hide (0)
 *
 * Description:
 * Shows or hides the hardware cursor
 */
static void ls_enable_cursor(int mode, unsigned long base)
{
	unsigned int tmp = ls_readl(base + LS_FB_CUR_CFG_REG);
	tmp &= ~0xff;
	if(edid_flag == 0){
	ls_writel(mode ? (tmp | 0x02) : (tmp | 0x00),
			base + LS_FB_CUR_CFG_REG);
	}else{
	ls_writel(mode ? (tmp | 0x12) : (tmp | 0x10),
			base + LS_FB_CUR_CFG_REG);
	}
}

static void ls_load_cursor_image(int width, int height, u8 *data)
{
	u32 __iomem *addr = (u32 *)DEFAULT_CURSOR_MEM;
	int row, col, i, j, bit = 0;
	col = (width > CUR_HEIGHT_SIZE)? CUR_HEIGHT_SIZE : width;
	row = (height > CUR_WIDTH_SIZE)? CUR_WIDTH_SIZE : height;

	for (i = 0; i < CUR_HEIGHT_SIZE; i++) {
		for (j = 0; j < CUR_WIDTH_SIZE; j++) {
			if (i < height && j < width) {
				bit = data[(i * width + width - j) >> 3] &
					(1 << ((i * width + width - j) & 0x7));
				addr[i * CUR_WIDTH_SIZE + j] =
					bit ? 0xffffffff : 0;
			 } else {
				addr[i * CUR_WIDTH_SIZE + j] = 0x0;
			 }
		}
	}
}


static int ls_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;

	if (cursor->image.width > CUR_WIDTH_SIZE ||
			cursor->image.height > CUR_HEIGHT_SIZE)
		return -ENXIO;

	ls_enable_cursor(OFF, base);

	if (cursor->set & FB_CUR_SETPOS) {
		u32 tmp;

		tmp = (cursor->image.dx - info->var.xoffset) & 0xffff;
		tmp |= (cursor->image.dy - info->var.yoffset) << 16;
		ls_writel(tmp, base + LS_FB_CUR_LOC_ADDR_REG);
	}

	if (cursor->set & FB_CUR_SETSIZE)
		ls_reset_cursor_image();

	if (cursor->set & FB_CUR_SETHOT) {
		u32 hot = (cursor->hot.x << 16) | (cursor->hot.y << 8);
		u32 con = ls_readl(base + LS_FB_CUR_CFG_REG) & 0xff;
		ls_writel(hot | con, base + LS_FB_CUR_CFG_REG);
	}

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		int size = ((cursor->image.width + 7) >> 3) *
			cursor->image.height;
		int i;
		u8 *data = kmalloc(32 * 32 * 4, GFP_ATOMIC);

		if (data == NULL)
			return -ENOMEM;

		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				data[i] = cursor->image.data[i] ^ cursor->mask[i];
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < size; i++)
				data[i] = cursor->image.data[i] & cursor->mask[i];
			break;
		}

		ls_load_cursor_image(cursor->image.width,
				       cursor->image.height, data);
		kfree(data);
	}

	if (cursor->enable)
		ls_enable_cursor(ON, base);

	return 0;
}

struct cursor_req {
	u32 x;
	u32 y;
};

static int ls_fb_ioctl(struct fb_info *info, unsigned int cmd,
		                        unsigned long arg)
{
	u32 tmp;
	struct cursor_req req;
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;
	void __user *argp = (void __user *)arg;
	u8 *cursor_base = (u8 *)DEFAULT_CURSOR_MEM;

	switch (cmd) {
	case CURIOSET_CORLOR:
		break;
	case CURIOSET_POSITION:
		LS_DEBUG("CURIOSET_POSITION\n");
		if (copy_from_user(&req, argp, sizeof(struct cursor_req)))
			return -EFAULT;
		tmp = (req.x - info->var.xoffset) & 0xffff;
		tmp |= (req.y - info->var.yoffset) << 16;
		ls_writel(tmp, base + LS_FB_CUR_LOC_ADDR_REG);
		break;
	case CURIOLOAD_ARGB:
		LS_DEBUG("CURIOLOAD_ARGB\n");
		if (copy_from_user(cursor_base, argp, 32 * 32 * 4))
			return -EFAULT;
		break;
	case CURIOHIDE_SHOW:
		LS_DEBUG("CURIOHIDE_SHOW:%s\n", arg ? "show" : "hide");
		ls_enable_cursor(arg, base);
		break;
	case FBEDID_GET:
		LS_DEBUG("COPY EDID TO USER\n");
		if (copy_to_user(argp, par->edid, EDID_LENGTH))
			return -EFAULT;
	default:
		return -ENOTTY;

	}

	return 0;
}

/*
 *  Pan or Wrap the Display
 *
 *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */

static int ls_fb_pan_display(struct fb_var_screeninfo *var,
			struct fb_info *info)
{
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0
			|| var->yoffset >= info->var.yres_virtual
			|| var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset + var->xres > info->var.xres_virtual ||
			var->yoffset + var->yres > info->var.yres_virtual)
			return -EINVAL;
	}
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

#ifndef MODULE
static int __init ls_fb_setup(char *options)
{
	char *this_opt;
	ls_fb_enable = 1;

	if (!options || !*options)
		return 1;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "disable", 7))
			ls_fb_enable = 0;
		else
			mode_option = this_opt;
	}
	return 1;
}
#endif  /* MODULE */
static unsigned char *fb_do_probe_ddc_edid(struct i2c_adapter *adapter)
{
	unsigned char start = 0x0;
	unsigned char *buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
	struct i2c_msg msgs[] = {
		{
			.addr = eeprom_info.addr,
			.flags = 0,
			.len = 1,
			.buf = &start,
		},{
			.addr = eeprom_info.addr,
			.flags = I2C_M_RD,
			.len = EDID_LENGTH,
			.buf = buf,
		}
	};
	if (!buf){
		dev_warn(&adapter->dev, "unable to allocate memory for EDID "
			"block.\n");
		return NULL;
	}
	if (i2c_transfer(adapter, msgs, 2) == 2){
		return buf;
	}
	dev_warn(&adapter->dev, "unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}

#ifndef CONFIG_CPU_LOONGSON2K
/* Display chip */
static void fb_do_probe_ddc_ch7034(struct i2c_adapter *adapter)
{
	int i;
	unsigned char *buf = kmalloc(2*sizeof(unsigned char), GFP_KERNEL);
	struct i2c_msg msgs = {
		.addr = eeprom_info.addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
	if (!buf){
		dev_warn(&adapter->dev, "unable to allocate memory for CH7034"
			"block.\n");
		return;
	}
	for (i = 0; i < 131; i++) {
		msgs.buf[0] = CH7034_VGA_REG_TABLE[0][i][0];
		msgs.buf[1] = CH7034_VGA_REG_TABLE[0][i][1];
		if (i2c_transfer(adapter, &msgs, 1) != 1)
			printk("i %d buf 0x%x 0x%x\n", i, msgs.buf[0], msgs.buf[1]);
	}
	return;
}
#endif

static const struct i2c_device_id dvi_eep_ids[] = {
	{ "dvi-eeprom-edid", 0 },
	{ /* END OF LIST */ }
};

static const struct i2c_device_id vga_eep_ids[] = {
	{ "eeprom-edid", 2 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, dvi_eep_ids);
MODULE_DEVICE_TABLE(i2c, vga_eep_ids);

static int eep_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	eeprom_info.adapter = client->adapter;
	eeprom_info.addr = client->addr;
	return 0;
}

static int eep_remove(struct i2c_client *client)
{
	i2c_unregister_device(client);
	return 0;
}
static struct i2c_driver vga_eep_driver = {
	.driver = {
		.name = "vga_eep-edid",
		.owner = THIS_MODULE,
	},
	.probe = eep_probe,
	.remove = eep_remove,
	.id_table = vga_eep_ids,
};

static struct i2c_driver dvi_eep_driver = {
	.driver = {
		.name = "dvi_eep-edid",
		.owner = THIS_MODULE,
	},
	.probe = eep_probe,
	.remove = eep_remove,
	.id_table = dvi_eep_ids,
};

static unsigned char *ls_fb_i2c_connector(struct ls_fb_par *fb_par)
{
	unsigned char *edid = NULL;

	LS_DEBUG("edid entry\n");
	if (i2c_add_driver(&dvi_eep_driver)) {
		pr_err("i2c-%d No eeprom device register!",(int)dvi_eep_driver.id_table->driver_data);
		return NULL;
	}
	if (eeprom_info.adapter)
#ifdef CONFIG_CPU_LOONGSON2K
		edid = fb_do_probe_ddc_edid(eeprom_info.adapter);
#else
		fb_do_probe_ddc_ch7034(eeprom_info.adapter);
#endif
	if (!edid) {
		if (i2c_add_driver(&vga_eep_driver)) {
			pr_err("i2c-%d No eeprom device register!",(int)vga_eep_driver.id_table->driver_data);
			return NULL;
		}
		edid_flag = 1;
		if (eeprom_info.adapter)
			edid = fb_do_probe_ddc_edid(eeprom_info.adapter);
	}

	if (edid){
		fb_par->edid = edid;
	}
	return edid;
}

static int ls_fb_address_init(struct fb_info *info)
{
		struct resource *r;
		struct ls_fb_par *par;
#ifdef CONFIG_CPU_LOONGSON3
		struct pci_dev *pdev;

		pdev= pci_get_device(PCI_VENDOR_ID_LOONGSON,PCI_DEVICE_ID_LOONGSON_GPU ,NULL);
		if(pdev){
				/*get frame buffer address from memory of GPU device*/
				ls_cursor_dma = pci_resource_start(pdev,2);
				ls_cursor_mem = LS7A_HT1_BASE|ls_cursor_dma;

			    ls_fb_mem = ls_cursor_mem + LSFB_OFFSET;
				ls_fb_dma = ls_cursor_dma + LSFB_OFFSET;
				ls_phy_addr   = ls_fb_mem & LSFB_GPU_MASK;

				pci_enable_device_mem(pdev);
		}else{
#endif
				/*get frame buffer address from system memory*/
				par = (struct ls_fb_par *)info->par;
				r = platform_get_resource(par->pdev, IORESOURCE_MEM, 1);
				if (!r) {
						printk("ls-fb: get fb memory address fail!\n");
						return -ENOMEM;
				}

				ls_cursor_dma = r->start;
				ls_cursor_mem = TO_CAC(ls_cursor_dma);

				ls_fb_dma = ls_cursor_dma + LSFB_OFFSET;
				ls_fb_mem = ls_cursor_mem + LSFB_OFFSET;
				ls_phy_addr = ls_fb_dma;

#ifdef CONFIG_CPU_LOONGSON3
		}
#endif
		return 0;
}

static int ls_find_init_mode(struct fb_info *info)
{
		int ret;
        struct fb_videomode mode;
        struct fb_var_screeninfo var;
        struct fb_monspecs *specs = &info->monspecs;
		struct ls_fb_par *par = info->par;
        int found = 0;
		unsigned char *edid;
        INIT_LIST_HEAD(&info->modelist);

		ret = ls_fb_address_init(info);
		if(ret)
			return ret;
        memset(&mode, 0, sizeof(struct fb_videomode));
        memset(&var, 0, sizeof(struct fb_var_screeninfo));
		var.bits_per_pixel = DEFAULT_BITS_PER_PIXEL;
		edid = ls_fb_i2c_connector(par);
		if (!edid){
			goto def;
		}
		fb_edid_to_monspecs(par->edid, specs);
		if (specs->modedb == NULL){
			printk("ls-fb: Unable to get Mode Database\n");
			goto def;
		}
		fb_videomode_to_modelist(specs->modedb,specs->modedb_len,
						&info->modelist);
		if (specs->modedb != NULL){
			const struct fb_videomode *m;
			if (!found){
				m = fb_find_best_display(&info->monspecs, &info->modelist);
				mode = *m;
				found= 1;
			}
			fb_videomode_to_var(&var,&mode);
		}
		if (mode_option) {
				printk("mode_option: %s\n", mode_option);
				fb_find_mode(&var, info, mode_option, specs->modedb,
								specs->modedb_len, (found) ? &mode : NULL,info->var.bits_per_pixel);
				info->var = var;
		}
	else{
		info->var = ls_fb_default;
	}
	info->var = var;
	fb_destroy_modedb(specs->modedb);
	specs->modedb = NULL;
	return 0;
def:
	if (mode_option) {
		printk("mode_option: %s\n", mode_option);
		fb_find_mode(&var, info, mode_option, specs->modedb,
				specs->modedb_len, (found) ? &mode : NULL,info->var.bits_per_pixel);
		info->var = var;
	} else {
	
		info->var = ls_fb_default;
	}

	return 0;
}

/* irq */
static irqreturn_t lsfb_irq(int irq, void *dev_id)
{
	unsigned int val, cfg;
	unsigned long flags;
	struct fb_info *info = (struct fb_info *) dev_id;
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;

	spin_lock_irqsave(&fb_lock, flags);

	val = ls_readl(base + LS_FB_INT_REG);
	ls_writel(val & (0xffff << 16), base + LS_FB_INT_REG);

	cfg = ls_readl(base + LS_FB_CFG_DVO1_REG);
	/* if underflow, reset VGA */
	if (val & 0x280) {
		ls_writel(0, base + LS_FB_CFG_DVO1_REG);
		ls_writel(cfg, base + LS_FB_CFG_DVO1_REG);
	}

	spin_unlock_irqrestore(&fb_lock, flags);

	return IRQ_HANDLED;
}

static struct fb_ops ls_fb_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var		= ls_fb_check_var,
	.fb_set_par		= ls_fb_set_par,
	.fb_setcolreg		= ls_fb_setcolreg,
	.fb_blank		= ls_fb_blank,
	.fb_pan_display		= ls_fb_pan_display,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
	.fb_cursor		= ls_fb_cursor,
	.fb_ioctl		= ls_fb_ioctl,
	.fb_compat_ioctl	= ls_fb_ioctl,
};

/*
 *  Initialisation
 */
static int ls_fb_probe(struct platform_device *dev)
{
	int irq;
	struct fb_info *info;
	int retval = -ENOMEM;
	struct ls_fb_par *par;
	struct resource *r;
	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "no irq for device\n");
		return -ENOENT;
	}

	info = framebuffer_alloc(sizeof(u32) * 256, &dev->dev);
	if (!info)
		return -ENOMEM;

	info->fix = ls_fb_fix;
	info->node = -1;
	info->fbops = &ls_fb_ops;
	info->pseudo_palette = info->par;
	info->flags = FBINFO_FLAG_DEFAULT;

	par = kzalloc(sizeof(struct ls_fb_par), GFP_KERNEL);
	if (!par) {
		retval = -ENOMEM;
		goto release_info;
	}

	info->par = par;
	par->fb_info = info;
	par->pdev = dev;
	par->irq = irq;
	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!r) {
		retval = -ENOMEM;
		goto release_par;
	}

	par->reg_base = r->start;
	retval = ls_find_init_mode(info);
	if(retval)
		goto release_par;

	if (!videomemorysize) {
		videomemorysize = info->var.xres_virtual *
					info->var.yres_virtual *
					info->var.bits_per_pixel / 8;
	}

	/*
	 * For real video cards we use ioremap.
	 */
	videomemory = (void *)DEFAULT_FB_MEM;
	dma_A = (dma_addr_t)DEFAULT_FB_DMA;
	if(hw_coherentio != 1)
		videomemory = TO_UNCAC((u64)videomemory);
	pr_info("videomemory=%lx\n",(unsigned long)videomemory);
	pr_info("videomemorysize=%lx\n",videomemorysize);
	pr_info("dma_A=%x\n",(int)dma_A);
	memset(videomemory, 0, videomemorysize);

	cursor_mem = (void *)DEFAULT_CURSOR_MEM;
	cursor_dma = (dma_addr_t)DEFAULT_CURSOR_DMA;
	if(hw_coherentio != 1)
		cursor_mem = TO_UNCAC((u64)cursor_mem);
	memset (cursor_mem,0x88FFFF00,cursor_size);

	info->screen_base = (char __iomem *)videomemory;
	info->fix.smem_start = DEFAULT_PHY_ADDR;
	info->fix.smem_len = videomemorysize;

	retval = fb_alloc_cmap(&info->cmap, 32, 0);
	if (retval < 0) goto release_par;

	info->fbops->fb_check_var(&info->var, info);
	par->htotal = info->var.xres;
	par->vtotal = info->var.yres;
	retval = register_framebuffer(info);
	if (retval < 0)
		goto release_map;

	retval = request_irq(irq, lsfb_irq, IRQF_DISABLED, dev->name, info);
	if (retval) {
		dev_err(&dev->dev, "cannot get irq %d - err %d\n", irq, retval);
		goto unreg_info;
	}

	platform_set_drvdata(dev, info);

	pr_info("fb%d: Virtual frame buffer device, using %ldK of"
			"video memory\n", info->node, videomemorysize >> 10);

	return 0;
unreg_info:
	unregister_framebuffer(info);
release_map:
	fb_dealloc_cmap(&info->cmap);
release_par:
	kfree(par);
release_info:
	platform_set_drvdata(dev, NULL);
	framebuffer_release(info);
	return retval;
}

static int ls_fb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct ls_fb_par *par = info->par;
	int irq = par->irq;

	free_irq(irq, info);
	fb_dealloc_cmap(&info->cmap);
	unregister_framebuffer(info);
	platform_set_drvdata(dev, info);
	framebuffer_release(info);
	if (par->edid)
		kfree(par->edid);
	kfree(par);

	return 0;
}

#if    defined(CONFIG_SUSPEND)
/**
 **      ls_fb_suspend - Suspend the device.
 **      @dev: platform device
 **      @msg: the suspend event code.
 **
 **      See Documentation/power/devices.txt for more information
 **/
static u32 output_mode;
void console_lock(void);
void console_unlock(void);

static int ls_fb_suspend(struct platform_device *dev, pm_message_t msg)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;

	console_lock();
	fb_set_suspend(info, 1);
	console_unlock();

	output_mode = ls_readl(base + LS_FB_DVO_OUTPUT_REG);

	return 0;
}

/**
 **      ls_fb_resume - Resume the device.
 **      @dev: platform device
 **
 **      See Documentation/power/devices.txt for more information
 **/
static int ls_fb_resume(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct ls_fb_par *par = (struct ls_fb_par *)info->par;
	unsigned long base = par->reg_base;

	ls_fb_set_par(info);
	ls_writel(output_mode, base + LS_FB_DVO_OUTPUT_REG);

	console_lock();
	fb_set_suspend(info, 0);
	console_unlock();

	return 0;
}
#endif

#ifdef CONFIG_OF
static struct of_device_id ls_fb_id_table[] = {
	{ .compatible = "loongson,ls-fb", },
	{},
};
#endif
static struct platform_driver ls_fb_driver = {
	.probe	= ls_fb_probe,
	.remove = ls_fb_remove,
#ifdef	CONFIG_SUSPEND
	.suspend = ls_fb_suspend,
	.resume	 = ls_fb_resume,
#endif
	.driver = {
		.name	= "ls-fb",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_fb_id_table),
#endif
	},
};

static struct pci_device_id ls_fb_devices[] = {
	{PCI_DEVICE(0x14, 0x7a06)},
	{0, 0, 0, 0, 0, 0, 0}
};

/*
 * DC
 */
static struct resource ls_dc_resources[] = {
	[0] = {
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ls_dc_device = {
	.name           = "ls-fb",
	.id             = 0,
	.num_resources	= ARRAY_SIZE(ls_dc_resources),
	.resource	= ls_dc_resources,
};

static int ls_fb_pci_register(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	int ret;
	unsigned char v8;

	pr_debug("ls_fb_pci_register BEGIN\n");

	/* Enable device in PCI config */
	ret = pci_enable_device(pdev);
	if (ret < 0) {
		printk(KERN_ERR "lsfb (%s): Cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	/* request the mem regions */
	ret = pci_request_region(pdev, 0, "lsfb io");
	if (ret < 0) {
		printk( KERN_ERR "lsfb (%s): cannot request region 0.\n",
			pci_name(pdev));
		goto err_out;
	}

	ls_dc_resources[0].start = pci_resource_start (pdev, 0);
	ls_dc_resources[0].end = pci_resource_end(pdev, 0);
	/* need api from pci irq */
	ret = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &v8);

	if (ret == PCIBIOS_SUCCESSFUL) {

		ls_dc_resources[1].start = v8;
		ls_dc_resources[1].end = v8;

		platform_device_register(&ls_dc_device);
	}

err_out:
	return ret;
}

static void ls_fb_pci_unregister(struct pci_dev *pdev)
{
	pci_release_region(pdev, 0);
}

#ifdef	CONFIG_SUSPEND
int ls_fb_pci_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	ls_fb_suspend(&ls_dc_device, mesg);
	pci_save_state(pdev);
	return 0;
}
#endif

#ifdef	CONFIG_SUSPEND
int ls_fb_pci_resume(struct pci_dev *pdev)
{
	ls_fb_resume(&ls_dc_device);
	return 0;
}
#endif

static struct pci_driver ls_fb_pci_driver = {
	.name		= "ls-fb",
	.id_table	= ls_fb_devices,
	.probe		= ls_fb_pci_register,
	.remove		= ls_fb_pci_unregister,
#ifdef	CONFIG_SUSPEND
	.suspend = ls_fb_pci_suspend,
	.resume	 = ls_fb_pci_resume,
#endif
};

static int __init ls_fb_init (void)
{
	int ret;
	struct pci_dev *pdev = NULL;
#ifndef MODULE
	char *option = NULL;
#endif
	/* Discrete card prefer */
	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev))) {
		if (pdev->vendor != PCI_VENDOR_ID_LOONGSON)
			return 0;
	}
#ifndef MODULE
	if (fb_get_options("ls-fb", &option))
		return -ENODEV;
	ls_fb_setup(option);
#endif
	ret = platform_driver_register(&ls_fb_driver);
	if (!ret)
		ret = pci_register_driver (&ls_fb_pci_driver);
	return ret;
}

static void __exit ls_fb_exit (void)
{
	platform_driver_unregister(&ls_fb_driver);
	pci_unregister_driver (&ls_fb_pci_driver);
}

module_init(ls_fb_init);
module_exit(ls_fb_exit);

MODULE_LICENSE("GPL");
