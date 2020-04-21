/*
 * Copyright (c) 2018 Loongson Technology Co., Ltd.
 * Authors:
 *	Li Chenyang <lichenyang@loongson.cn>
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
int loongson_vga_irq_enable_vblank(struct drm_device *dev, unsigned int crtc_id)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	unsigned long base;
	unsigned int val;

	base = (unsigned long)(ldev->rmmio);

	if (crtc_id) {
		val = ls_readq(base + LS_FB_INT_REG);
		val |= LS_FB_VSYNC1_ENABLE;
		ls_writeq(val, base + LS_FB_INT_REG);
	} else {
		val = ls_readq(base + LS_FB_INT_REG);
		val |= LS_FB_VSYNC0_ENABLE;
		ls_writeq(val, base + LS_FB_INT_REG);
	}

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
void loongson_vga_irq_disable_vblank(struct drm_device *dev, unsigned int crtc_id)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	unsigned long base;
	unsigned int val;

	base = (unsigned long)(ldev->rmmio);

	if (crtc_id) {
		val = ls_readq(base + LS_FB_INT_REG);
		val &= ~LS_FB_VSYNC1_ENABLE;
		ls_writeq(val, base + LS_FB_INT_REG);
	} else {
		val = ls_readq(base + LS_FB_INT_REG);
		val &= ~LS_FB_VSYNC0_ENABLE;
		ls_writeq(val, base + LS_FB_INT_REG);
	}
}

irqreturn_t loongson_vga_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct loongson_drm_device *ldev = dev->dev_private;
	struct loongson_crtc *loongson_crtc;
	unsigned long base;
	unsigned int val;

	/*get irq and clear*/
	base = (unsigned long)(ldev->rmmio);
	val = ls_readq(base + LS_FB_INT_REG);
	ls_writeq(val & (0xffff << 16), base + LS_FB_INT_REG);

	/*vga crtc1*/
	if (val & 0x1) {
		loongson_pageflip_irq(ldev,1);
		loongson_crtc = ldev->mode_info[1].crtc;
		drm_crtc_handle_vblank(&loongson_crtc->base);
		ldev->vsync1_count++;
	}
	/*hdmi crtc0*/
	if (val & 0x4) {
		loongson_pageflip_irq(ldev,0);
		loongson_crtc = ldev->mode_info[0].crtc;
		drm_crtc_handle_vblank(&loongson_crtc->base);
		ldev->vsync0_count++;
	}

	return IRQ_HANDLED;
}

int loongson_pageflip_irq(struct loongson_drm_device *ldev, unsigned int crtc_id)
{
	unsigned long flags;
	struct loongson_crtc *loongson_crtc;
	struct loongson_flip_work *works;
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;

	loongson_crtc = ldev->mode_info[crtc_id].crtc;
	crtc = &loongson_crtc->base;
	fb = crtc->primary->fb;

	if (loongson_crtc == NULL)
		return 0;

	spin_lock_irqsave(&ldev->dev->event_lock, flags);
	works = loongson_crtc->pflip_works;
	if (loongson_crtc->pflip_status != LOONGSON_FLIP_SUBMITTED) {
		spin_unlock_irqrestore(&ldev->dev->event_lock, flags);
		return 0;
	}

	/* page flip completed. clean up */
	loongson_crtc->pflip_status = LOONGSON_FLIP_NONE;
	loongson_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if (works->event)
		drm_crtc_send_vblank_event(&loongson_crtc->base, works->event);

	spin_unlock_irqrestore(&ldev->dev->event_lock, flags);
	drm_crtc_vblank_put(&loongson_crtc->base);

	return 0;
}

void loongson_vga_irq_preinstall(struct drm_device *dev)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	unsigned long base;
	unsigned int val;

	base = (unsigned long)(ldev->rmmio);

	/* Disable all interrupts */
	val = (0x0000 << 16);
	ls_writeq(val, base + LS_FB_INT_REG);

	/* Clear bits */
	val = ls_readq(base + LS_FB_INT_REG);
	val &= (0xffff << 16);
	ls_writeq(val, base + LS_FB_INT_REG);
}

int loongson_vga_irq_postinstall(struct drm_device *dev)
{
	dev->max_vblank_count = 0x00ffffff;
	return 0;
}

void loongson_vga_irq_uninstall(struct drm_device *dev)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	unsigned long base;
	unsigned int val;

	base = (unsigned long)(ldev->rmmio);

	if (ldev == NULL) {
		return;
	}

	/* Disable all interrupts */
	val = (0x0000 << 16);
	ls_writeq(val, base + LS_FB_INT_REG);
}

int loongson_irq_init(struct loongson_drm_device *ldev)
{
	int r = 0;
	unsigned long base;

	base = (unsigned long)(ldev->rmmio);
	ldev->vsync0_count = 0;
	ldev->vsync1_count = 0;
	ldev->pageflip_count = 0;

	spin_lock_init(&ldev->irq.lock);
	r = drm_vblank_init(ldev->dev, ldev->num_crtc);
	if (r) {
		return r;
	}
	DRM_INFO("drm vblank init finished\n");

	ldev->irq.installed = true;
	r = drm_irq_install(ldev->dev, ldev->dev->pdev->irq);
	if (r) {
		DRM_INFO("drm_irq_install error:%d\n", r);
		ldev->irq.installed = false;
		return r;
	}

	DRM_INFO("loongson irq initialized\n");
	return 0;
}

u32 loongson_crtc_vblank_count(struct drm_device *dev, unsigned int pipe)
{
	struct loongson_drm_device *ldev = dev->dev_private;

	if (pipe)
		return ldev->vsync1_count;
	else
		return ldev->vsync0_count;
}

void loongson_flip_work_func(struct work_struct *__work)
{
	struct delayed_work *delayed_work =
		container_of(__work, struct delayed_work, work);
	struct loongson_flip_work *work =
		container_of(delayed_work, struct loongson_flip_work, flip_work);
	struct loongson_drm_device *ldev = work->ldev;
	struct loongson_crtc *loongson_crtc = ldev->mode_info[work->crtc_id].crtc;
	struct drm_crtc *crtc = &loongson_crtc->base;
	unsigned long flags;
	unsigned int crtc_address, pitch, y, x;

	y = crtc->y;
	x = crtc->x;
	pitch = crtc->primary->fb->pitches[0];
	crtc_address = (u32)work->base + y * pitch + ALIGN(x,64) * 4;

	/* We borrow the event spin lock for protecting flip_status */
	spin_lock_irqsave(&crtc->dev->event_lock, flags);

	if (loongson_crtc->pflip_status == LOONGSON_FLIP_NONE ||
		ldev->pageflip_count != loongson_crtc_vblank_count(crtc->dev, work->crtc_id)) {
		loongson_set_start_address(crtc, (u32)crtc_address);
		/* Set the flip status */
		loongson_crtc->pflip_status = LOONGSON_FLIP_SUBMITTED;
		ldev->pageflip_count =
			loongson_crtc_vblank_count(crtc->dev, work->crtc_id);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	} else {
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		schedule_delayed_work(&work->flip_work, usecs_to_jiffies(1000));
		return;
	}
}

