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

/*
  Hide the cursor off screen. We can't disable the cursor hardware because it
  takes too long to re-activate and causes momentary corruption
*/
static void loongson_hide_cursor(struct drm_crtc *crtc)
{
	unsigned int tmp;
	unsigned long base;
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev =
		(struct loongson_device *)dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	unsigned int crtc_id = loongson_crtc->crtc_id;

	base = (unsigned long)(ldev->rmmio);

	tmp = ls_mm_rreg(ldev, LS_FB_CUR_CFG_REG);
	tmp &= ~0xff;
	if (ldev->clone_mode == true) {
		ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, tmp | 0x00);
		ldev->cursor_showed = false;
		return;
	} else {
		if (ldev->cursor_crtc_id != crtc_id)
			return;

		if (crtc_id) {
			ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, tmp | 0x10);
		} else {
			ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, tmp | 0x00);
		}
		ldev->cursor_showed = false;
	}
}

static void loongson_show_cursor(struct drm_crtc *crtc)
{
	unsigned long base;
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev =
		(struct loongson_device *)dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	unsigned int crtc_id = loongson_crtc->crtc_id;

	base = (unsigned long)(ldev->rmmio);
	if (ldev->clone_mode == true) {
		ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, 0x00050202);
		ldev->cursor_crtc_id = 0;
		ldev->cursor_showed = true;
	} else {
		if ((ldev->cursor_crtc_id == crtc_id) ||
		    (ldev->cursor_crtc_id == ldev->num_crtc)) {
			if (crtc_id == 0) {
				ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, 0x00050202);
			} else {
				ls_mm_wreg(ldev, LS_FB_CUR_CFG_REG, 0x00050212);
			}

			ldev->cursor_showed = true;
			ldev->cursor_crtc_id = crtc_id;
		}
	}
}

int loongson_crtc_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			      uint32_t handle, uint32_t width, uint32_t height,
			      int32_t hot_x, int32_t hot_y)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev =
		(struct loongson_device *)dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	struct loongson_bo *pixels = ldev->cursor.pixels;
	struct drm_gem_object *obj;
	struct loongson_bo *bo = NULL;
	int ret = 0;
	unsigned int crtc_id;
	u32 gpu_addr;
	unsigned long base;

	base = (unsigned long)(ldev->rmmio);
	crtc_id = loongson_crtc->crtc_id;

	if ((width != 32 || height != 32) && handle) {
		return -EINVAL;
	}

	if (!handle || !file_priv) {
		loongson_hide_cursor(crtc);
		return 0;
	}

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj)
		return -ENOENT;

	ret = loongson_bo_reserve(pixels, true);
	if (ret) {
		goto out_unref;
	}

	/* Move cursor buffers into VRAM if they aren't already */
	if (!pixels->pin_count) {
		ret = loongson_bo_pin(pixels, TTM_PL_FLAG_VRAM,
				      &ldev->cursor.pixels_gpu_addr);
		if (ret)
			goto out1;
	}

	bo = gem_to_loongson_bo(obj);
	ret = loongson_bo_reserve(bo, true);
	if (ret) {
		dev_err(&dev->pdev->dev, "failed to reserve user bo\n");
		goto out1;
	}
	if (!bo->kmap.virtual) {
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret) {
			dev_err(&dev->pdev->dev,
				"failed to kmap user buffer updates\n");
			goto out2;
		}
	}

	/* Map up-coming buffer to write colour indices */
	if (!pixels->kmap.virtual) {
		ret = ttm_bo_kmap(&pixels->bo, 0, pixels->bo.num_pages,
				  &pixels->kmap);
		if (ret) {
			dev_err(&dev->pdev->dev,
				"failed to kmap cursor updates\n");
			goto out3;
		}
	}

	memcpy(pixels->kmap.virtual, bo->kmap.virtual, 32 * 32 * 4);
	/* Program gpu address of cursor buffer */
	gpu_addr = ldev->cursor.pixels_gpu_addr;
	ls_mm_wreg(ldev, LS_FB_CUR_ADDR_REG, gpu_addr);
	ls_mm_wreg(ldev, LS_FB_CUR_BACK_REG, 0x00eeeeee);
	ls_mm_wreg(ldev, LS_FB_CUR_FORE_REG, 0x00aaaaaa);
	loongson_show_cursor(crtc);
	ret = 0;

	ttm_bo_kunmap(&pixels->kmap);
out3:
	ttm_bo_kunmap(&bo->kmap);
out2:
	loongson_bo_unreserve(bo);
out1:
	if (ret)
		loongson_hide_cursor(crtc);
	loongson_bo_unreserve(pixels);

out_unref:
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

int loongson_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct loongson_device *ldev =
		(struct loongson_device *)crtc->dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	int xorign = 0, yorign = 0;
	unsigned int crtc_id;
	unsigned long base;
	u32 tmp;

	base = (unsigned long)(ldev->rmmio);
	crtc_id = loongson_crtc->crtc_id;

	/*upper edge condition*/
	yorign = y + crtc->y;
	if (yorign < 0)
		y = 0;

	/*left edge conditon*/
	xorign = x + crtc->x;
	if (xorign < 0)
		x = 0;

	/* move from one crtc to another, check which crtc should he shown
	 * the x or y < 0, it means the cursor it out of current review,
	 * && xorign/ yorign > 0, it means the cursor is in the framebuffer
	 * but not in curren review */
	if ((x < 0 && xorign > 0) || (y < 0 && yorign > 0)) {
		if (ldev->cursor_crtc_id == crtc_id &&
		    ldev->clone_mode == false)
			/*the cursor is not show, so hide if the (x,y)
			 * is in active crtc*/
			loongson_hide_cursor(crtc);
		return 0;
	}

	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	tmp = x & 0xffff;
	tmp |= y << 16;
	ls_mm_wreg(ldev, LS_FB_CUR_LOC_ADDR_REG, tmp);
	tmp = ls_mm_rreg(ldev, LS_FB_CUR_CFG_REG);
	tmp &= ~0xff;

	if (ldev->cursor_crtc_id != crtc_id && ldev->clone_mode == false) {
		ldev->cursor_crtc_id = crtc_id;
		if (ldev->cursor_showed) {
			ldev->cursor_showed = false;
		}
	}
	if (!ldev->cursor_showed)
		loongson_show_cursor(crtc);
	return 0;
}
