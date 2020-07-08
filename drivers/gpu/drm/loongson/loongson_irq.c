/*
 * Copyright (c) 2018 Loongson Technology Co., Ltd.
 * Authors:
 *	Zhu Chen <zhuchen@loongson.cn>
 *	Fang Yaling <fangyaling@loongson.cn>
 *	Zhang Dandan <zhangdandan@loongson.cn>
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "loongson_drv.h"


/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int loongson_vga_irq_enable_vblank(struct drm_device *dev,unsigned int crtc_id)
{
	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void loongson_vga_irq_disable_vblank(struct drm_device *dev,unsigned int crtc_id)
{

}

irqreturn_t loongson_vga_irq_handler(int irq,void *arg)
{
	return 0;
}

void loongson_vga_irq_preinstall(struct drm_device *dev)
{

}

int loongson_vga_irq_postinstall(struct drm_device *dev)
{
	return 0;
}

void loongson_vga_irq_uninstall(struct drm_device *dev)
{
}

/*
 * We need a special version, instead of just using drm_irq_install(),
 * because we need to register the irq via omapdss.  Once omapdss and
 * omapdrm are merged together we can assign the dispc hwmod data to
 * ourselves and drop these and just use drm_irq_{install,uninstall}()
 */

int loongson_vga_drm_irq_install(struct drm_device *dev)
{
	return 0;
}

int loongson_vga_drm_irq_uninstall(struct drm_device *dev)
{
	return 0;
}
