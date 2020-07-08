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

#include <linux/console.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "loongson_drv.h"
#include "loongson_vbios.h"

#define DEVICE_NAME "loongson-drm"
#define DRIVER_NAME "loongson-drm"
#define DRIVER_DESC "Loongson DRM Driver"
#define DRIVER_DATE "20180328"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 3
#define DRIVER_PATCHLEVEL 0

u32 ls_mm_rreg(struct loongson_device *ldev, u32 offset)
{
	u32 val = 0;
	unsigned long flags;

#ifdef CONFIG_CPU_LOONGSON3
	ls7a_read(val, (ulong)ldev->rmmio + offset);
#else
	spin_lock_irqsave(&ldev->mmio_lock, flags);
	val = readl(ldev->rmmio + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);
#endif

	return val;
}

void ls_mm_wreg(struct loongson_device *ldev, u32 offset, u32 val)
{
#ifdef CONFIG_CPU_LOONGSON3
	ls7a_write(val, (ulong)ldev->rmmio + offset);
#else
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	writel(val, ldev->rmmio + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);
#endif
}

u32 ls7a_io_rreg(struct loongson_device *ldev, u32 offset)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	val = readl(ldev->io + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);

	return val;
}

void ls7a_io_wreg(struct loongson_device *ldev, u32 offset, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	writel(val, ldev->io + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);
}

u32 ls_mm_rreg_locked(struct loongson_device *ldev, u32 offset)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	val = readl(ldev->rmmio + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);

	return val;
}

void ls_mm_wreg_locked(struct loongson_device *ldev, u32 offset, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	writel(val, ldev->rmmio + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);
}

u64 ls2k_io_rreg(struct loongson_device *ldev, u32 offset)
{
	u64 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	val = readq(ldev->io + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);

	return val;
}

void ls2k_io_wreg(struct loongson_device *ldev, u32 offset, u64 val)
{
	unsigned long flags;

	spin_lock_irqsave(&ldev->mmio_lock, flags);
	writeq(val, ldev->io + offset);
	spin_unlock_irqrestore(&ldev->mmio_lock, flags);
}

/**
 * loongson_user_framebuffer_destroy -- release framebuffer, clean up framebuffer resource
 *
 * @fb pointer to drm_framebuffer
 */
static void loongson_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct loongson_framebuffer *loongson_fb = to_loongson_framebuffer(fb);
	if (loongson_fb->obj)
		drm_gem_object_unreference_unlocked(loongson_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

/**
 * loongson_fb_funcs --framebuffer hooks sturcture
 */
static const struct drm_framebuffer_funcs loongson_fb_funcs = {
	.destroy = loongson_user_framebuffer_destroy,
};

/**
 *  loongson_framebuffer_init ---registers the framebuffer and
 *   makes it accessible to other threads
 *
 *  @dev      pointer to drm_device structure
 * @lfb      pointer to loongson_framebuffer structure
 * @mode_cmd drm mode framebuffer command
 * @obj      pointer to drm_gem_object structure
 *
 * RETURN
 *  init result
 */
int loongson_framebuffer_init(struct drm_device *dev,
			      struct loongson_framebuffer *lfb,
			      const struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj)
{
	int ret;

	drm_helper_mode_fill_fb_struct(&lfb->base, mode_cmd);
	lfb->obj = obj;
	ret = drm_framebuffer_init(dev, &lfb->base, &loongson_fb_funcs);
	if (ret) {
		DRM_ERROR("drm_framebuffer_init failed: %d\n", ret);
		return ret;
	}
	return 0;
}

/**
 * loongson_user_framebuffer_create
 *
 * @dev       the pointer to drm_device structure
 * @filp      the pointer to drm_file structure
 * @mode_cmd  drm mode framebuffer cmd structure
 *
 * RETURN
 * A new framebuffer with an initial reference count of 1 or a negative
 * error code encoded with ERR_PTR()
 */
static struct drm_framebuffer *
loongson_user_framebuffer_create(struct drm_device *dev, struct drm_file *filp,
				 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct loongson_framebuffer *loongson_fb;
	int ret;

	obj = drm_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	loongson_fb = kzalloc(sizeof(*loongson_fb), GFP_KERNEL);
	if (!loongson_fb) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	ret = loongson_framebuffer_init(dev, loongson_fb, mode_cmd, obj);
	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		kfree(loongson_fb);
		return ERR_PTR(ret);
	}
	return &loongson_fb->base;
}

/**
 * loongson_mode_funcs---basic driver provided mode setting functions
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
static const struct drm_mode_config_funcs loongson_mode_funcs = {
	.fb_create = loongson_user_framebuffer_create,
};

#ifdef CONFIG_CPU_SUPPORTS_UNCACHED_ACCELERATED
static void __iomem *ls_uncache_acc_iomap(struct pci_dev *dev, int bar,
					  unsigned long offset,
					  unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (len <= offset || !start)
		return NULL;
	len -= offset;
	start += offset;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (flags & IORESOURCE_IO)
		return __pci_ioport_map(dev, start, len);
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);
		return ioremap_uncached_accelerated(start, len);
	}
	/* What? */
	return NULL;
}
#endif

/**
 * loongson_vram_init --Map the framebuffer from the card and configure the core
 *
 * @ldev pointer to loongson_device
 *
 * RETURN
 *  vram init result
 */
static int loongson_vram_init(struct loongson_device *ldev)
{
	struct resource *r;
	struct apertures_struct *aper = alloc_apertures(1);
	if (!aper)
		return -ENOMEM;

	switch (ldev->gpu) {
	case LS7A_GPU:
		ldev->vram_pdev =
			pci_get_device(PCI_VENDOR_ID_LOONGSON,
				       PCI_DEVICE_ID_LOONGSON_GPU, NULL);
		/* BAR 0 is VRAM */
		ldev->mc.vram_base = pci_resource_start(ldev->vram_pdev, 2);
		ldev->mc.vram_window = pci_resource_len(ldev->vram_pdev, 2);
		ldev->mc.vram_size = ldev->mc.vram_window;
		break;
	case LS2K_GPU:
		ldev->vram_plat_dev = ldev->dev->platformdev;
		/* BAR 0 is VRAM */
		r = platform_get_resource(ldev->vram_plat_dev, IORESOURCE_MEM,
					  1);
		ldev->mc.vram_base = r->start;
		ldev->mc.vram_window = r->end - r->start + 1;
		ldev->mc.vram_size = ldev->mc.vram_window;
		break;
	}

	DRM_INFO("vram base: 0x%llx, size: 0x%llx\n", ldev->mc.vram_base,
		 ldev->mc.vram_size);

	aper->ranges[0].base = ldev->mc.vram_base;
	aper->ranges[0].size = ldev->mc.vram_window;

	remove_conflicting_framebuffers(aper, "loongsonfb", true);
	kfree(aper);

	if (!devm_request_mem_region(ldev->dev->dev, ldev->mc.vram_base,
				     ldev->mc.vram_window,
				     "loongsondrmfb_vram")) {
		DRM_ERROR("can't reserve VRAM\n");
		return -ENXIO;
	}

	return 0;
}

/**
 *  loongson_device_init  ----init drm device
 *
 * @dev   pointer to drm_device structure
 * @flags start up flag
 *
 * RETURN
 *   drm device init result
 */
static int loongson_device_init(struct drm_device *dev, uint32_t flags)
{
	struct loongson_device *ldev = dev->dev_private;
	int ret;
	struct resource *r;

	if (!loongson_vbios_init(ldev)) {
		DRM_ERROR("Get vbios failed!\n");
		return -ENOMEM;
	}
	ldev->fb_vram_base = 0x0;

	/*BAR 0 contains registers */
	switch (ldev->gpu) {
	case LS7A_GPU:
		ldev->rmmio_base = pci_resource_start(ldev->dev->pdev, 0);
		ldev->rmmio_size = pci_resource_len(ldev->dev->pdev, 0);
		break;
	case LS2K_GPU:
		r = platform_get_resource(ldev->dev->platformdev,
					  IORESOURCE_MEM, 0);
		ldev->rmmio_base = r->start;
		ldev->rmmio_size = r->end - r->start + 1;
		break;
	}

	if (!devm_request_mem_region(ldev->dev->dev, ldev->rmmio_base,
				     ldev->rmmio_size, "loongsonfb_mmio")) {
		DRM_ERROR("can't reserve mmio registers\n");
		return -ENOMEM;
	}

	switch (ldev->gpu) {
	case LS7A_GPU:
		ldev->rmmio = pcim_iomap(dev->pdev, 0, 0);
		if (ldev->rmmio == NULL)
			return -ENOMEM;
		ldev->io = (void *)TO_UNCAC(LS7A_CHIP_CFG_REG_BASE);
		if (ldev->io == NULL)
			return -ENOMEM;
		break;
	case LS2K_GPU:
		ldev->rmmio = (void *)TO_UNCAC(ldev->rmmio_base);
		if (ldev->rmmio == NULL)
			return -ENOMEM;
		ldev->io = (void *)TO_UNCAC(LS2K_CHIP_CFG_REG_BASE);
		if (ldev->io == NULL)
			return -ENOMEM;
		break;
	}

	DRM_INFO("io: 0x%lx, mmio: 0x%llx, size: 0x%llx\n",
		 (unsigned long)ldev->io, ldev->rmmio_base, ldev->rmmio_size);

	ret = loongson_vram_init(ldev);
	if (ret)
		return ret;

	return 0;
}

/**
 * loongson_gem_create  -- allocate GEM object
 *
 * @dev: pointer to drm_device structure
 * @size: Requested size of buffer object.
 * @iskernel
 * @obj: address of pointer to drm_gem_object
 *
 * RETURN
 *   the result of alloc gem object
 */
int loongson_gem_create(struct drm_device *dev, u32 size, bool iskernel,
			struct drm_gem_object **obj)
{
	struct loongson_bo *astbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	ret = loongson_bo_create(dev, size, 0, 0, &astbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}
	*obj = &astbo->gem;
	return 0;
}

/**
 * loongson_i2c_bus_match
 *
 * @Returns  loongson_i2c pointer
 */
struct loongson_i2c *loongson_i2c_bus_match(struct loongson_device *ldev,
					    unsigned int i2c_id)
{
	unsigned int i;
	struct loongson_i2c *match = NULL, *tables;

	tables = ldev->i2c_bus;
	for (i = 0; i < LS_MAX_I2C_BUS; i++) {
		if (tables->i2c_id == i2c_id && tables->init == true) {
			match = tables;
			break;
		}
		tables++;
	}

	return match;
}

void loongson_i2c_create(struct loongson_i2c *ls_i2c, const char *name)
{
	struct i2c_adapter *i2c_adapter;
	struct i2c_board_info i2c_info;
	struct i2c_client *i2c_cli;

	DRM_DEBUG("loongson gpu add i2c_id %d\n", ls_i2c->i2c_id);
	i2c_adapter = i2c_get_adapter(ls_i2c->i2c_id);
	if (!i2c_adapter) {
		dev_warn_once(ls_i2c->ldev->dev->dev,
				"i2c-%d get adapter err\n", ls_i2c->i2c_id);
		return;
	}

	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	strncpy(i2c_info.type, name, I2C_NAME_SIZE);
	i2c_info.addr = DVO_I2C_ADDR;
	i2c_cli = i2c_new_device(i2c_adapter, &i2c_info);
	if (i2c_cli == NULL) {
		return;
	}

	i2c_put_adapter(i2c_adapter);
	ls_i2c->adapter = i2c_adapter;
	ls_i2c->init = true;
}

void loongson_i2c_add(struct loongson_device *ldev, const char *name)
{
	int i;

	for (i = 0; i < LS_MAX_I2C_BUS; i++) {
		if (ldev->i2c_bus[i].use) {
			ldev->i2c_bus[i].ldev = ldev;
			loongson_i2c_create(&ldev->i2c_bus[i], name);
		}
	}
}

/**
 * loongson_i2c_init prepare i2c-adap for gpu hw
 *
 * @ldev  loongson_device
 */
static void loongson_i2c_init(struct loongson_device *ldev)
{
	if (get_loongson_i2c(ldev))
		loongson_i2c_add(ldev, DVO_I2C_NAME);
}

/**
 * loongson_modeset_init --- init kernel mode setting
 *
 * @ldev: pointer to loongson_device structure
 *
 * RETURN
 *  return init result
 */
int loongson_modeset_init(struct loongson_device *ldev)
{
	struct loongson_encoder *ls_encoder;
	struct loongson_connector *ls_connector;
	struct loongson_crtc *ls_crtc;
	int ret, i;

	ldev->dev->mode_config.max_width = LOONGSON_MAX_FB_WIDTH;
	ldev->dev->mode_config.max_height = LOONGSON_MAX_FB_HEIGHT;

	ldev->dev->mode_config.cursor_width = 32;
	ldev->dev->mode_config.cursor_height = 32;

	ldev->dev->mode_config.fb_base = ldev->mc.vram_base;

	if (ldev->gpu == LS7A_GPU)
		loongson_i2c_init(ldev);

	for (i = 0; (i < ldev->num_crtc && i < LS_MAX_MODE_INFO); i++) {
		ls_crtc = loongson_crtc_init(ldev, i);
		if (ls_crtc) {
			ldev->mode_info[i].crtc = ls_crtc;
			ls_encoder = loongson_encoder_init(ldev,
							   ls_crtc->encoder_id);
			if (ls_encoder == NULL)
				continue;
			ldev->mode_info[i].encoder = ls_encoder;
			ls_connector = loongson_connector_init(
				ldev, ls_encoder->connector_id);
			if (ls_connector == NULL)
				continue;

			ldev->mode_info[i].connector = ls_connector;

			if (ls_encoder && ls_connector) {
				drm_mode_connector_attach_encoder(
					&ls_connector->base, &ls_encoder->base);
			}
			ldev->mode_info[i].mode_config_initialized = true;
		} else {
			DRM_WARN("loongson CRT-%d init failed\n", i);
		}
	}

	ret = loongson_fbdev_init(ldev);

	if (ret) {
		DRM_ERROR("loongson_fbdev_init failed\n");
		return ret;
	}

	return 0;
}

/**
 * loongson_modeset_fini --- deinit kernel mode setting
 *
 * @ldev: pointer to loongson_device structure
 *
 * RETURN
 */
void loongson_modeset_fini(struct loongson_device *ldev)
{
}

/*
 * Userspace get information ioctl
 */
/**
 *ioctl_get_fb_vram_base - answer a device specific request.
 *
 * @rdev: drm device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace drivers.
 * Returns 0 on success, -EINVAL on failure.
 */
static int ioctl_get_fb_vram_base(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct loongson_device *ldev = dev->dev_private;
	struct drm_loongson_param *args = data;

	args->value = ldev->fb_vram_base;

	return 0;
}

/**
 *ioctl_get_bo_vram_base - answer a device specific request.
 *
 * @rdev: drm device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace drivers.
 * Returns 0 on success, -EINVAL on failure.
 */
static int ioctl_get_bo_vram_base(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct loongson_device *ldev = dev->dev_private;
	struct drm_loongson_param *args = data;
	struct drm_gem_object *obj;
	struct loongson_bo *bo;
	int ret;
	unsigned long gpu_addr;

	obj = drm_gem_object_lookup(file_priv, args->value);
	if (obj == NULL)
		return -ENOENT;
	bo = gem_to_loongson_bo(obj);
	ret = loongson_bo_reserve(bo, false);
	if (ret)
		return ret;
	gpu_addr = loongson_bo_gpu_offset(bo);
	loongson_bo_unreserve(bo);
	ldev->fb_vram_base = gpu_addr;
	args->value = gpu_addr;
	DRM_DEBUG("loongson_get_bo_vram_base bo=%p, fb_vram_base=%lx\n", bo,
		  gpu_addr);
	return 0;
}

static struct drm_ioctl_desc loongson_ioctls_kms[DRM_COMMAND_END -
						 DRM_COMMAND_BASE] = {
	DRM_IOCTL_DEF_DRV(LOONGSON_GET_FB_VRAM_BASE, ioctl_get_fb_vram_base,
			  DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(LOONGSON_GET_BO_VRAM_BASE, ioctl_get_bo_vram_base,
			  DRM_UNLOCKED | DRM_AUTH),
};
#define DRM_LOONGSON_KMS_MAX_IOCTLS 2

/**
 * loongson_load_kms - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
static int loongson_load_kms(struct drm_device *dev, unsigned long flags)
{
	struct loongson_device *ldev;
	int ret, r;

	ldev = devm_kzalloc(dev->dev, sizeof(struct loongson_device),
			    GFP_KERNEL);
	if (ldev == NULL)
		return -ENOMEM;
	dev->dev_private = (void *)ldev;
	ldev->dev = dev;
	ldev->gpu = loongson_find_gpu();
	spin_lock_init(&ldev->mmio_lock);

	ret = loongson_device_init(dev, flags);
	DRM_DEBUG("end loongson drm device init.\n");
	loongson_ttm_init(ldev);

	drm_mode_config_init(dev);
	dev->mode_config.funcs = (void *)&loongson_mode_funcs;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	r = loongson_modeset_init(ldev);
	if (r) {
		dev_err(&dev->pdev->dev,
			"Fatal error during modeset init: %d\n", r);
	}

	ldev->inited = true;

	/*Enable IRQ*/
	loongson_irq_init(ldev);

	/* Make small buffers to store a hardware
	 * cursor (double buffered icon updates) */
	loongson_bo_create(dev, roundup(32 * 32 * 4, PAGE_SIZE), 0, 0,
			   &ldev->cursor.pixels);
	drm_kms_helper_poll_init(dev);
	return 0;
}

/**
 * loongson_unload_kms--release drm resource
 *
 * @dev: pointer to drm_device
 *
 * RETURN
 *  release success or fail
 */
static int loongson_unload_kms(struct drm_device *dev)
{
	struct loongson_device *ldev = dev->dev_private;

	if (ldev == NULL)
		return 0;

	loongson_modeset_fini(ldev);
	loongson_fbdev_fini(ldev);
	drm_mode_config_cleanup(dev);
	loongson_ttm_fini(ldev);
	dev->dev_private = NULL;
	ldev->inited = false;
	return 0;
}

/**
 *  loongson_dumb_create --dump alloc support
 *
 * @file: pointer to drm_file structure,DRM file private date
 * @dev:  pointer to drm_device structure
 * @args: a dumb scanout buffer param
 *
 * RETURN
 *  dum alloc result
 */
int loongson_dumb_create(struct drm_file *file, struct drm_device *dev,
			 struct drm_mode_create_dumb *args)
{
	int ret;
	struct drm_gem_object *gobj;
	struct loongson_bo *bo;
	u32 handle;
	u64 gpu_addr;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = loongson_gem_create(dev, args->size, false, &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_unreference_unlocked(gobj);
	if (ret)
		return ret;

	args->handle = handle;

	/* Cursor buffer size is 32×32 which should be in system memory */
	if (args->width == 32 && args->height == 32)
		return 0;

	bo = gem_to_loongson_bo(gobj);
	ret = loongson_bo_reserve(bo, false);
	if (ret)
		return ret;
	ret = loongson_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	if (ret) {
		loongson_bo_unreserve(bo);
		return ret;
	}
	loongson_bo_unreserve(bo);

	return 0;
}

/**
 * loongson_bo_unref -- reduce ttm object buffer refer
 *
 * @bo: the pointer to loongson ttm buffer object
 */
void loongson_bo_unref(struct loongson_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->bo);
	ttm_bo_unref(&tbo);
	*bo = NULL;
}

/**
 * loongson_gem_free_object --free gem object
 *
 * @obj: the pointer to drm_gem_object
 */
void loongson_gem_free_object(struct drm_gem_object *obj)
{
	struct loongson_bo *loongson_bo = gem_to_loongson_bo(obj);

	loongson_bo_unref(&loongson_bo);
}

/**
 * loongson_bo_mmap_offset -- Return sanitized offset for user-space mmaps
 *
 * @bo: the pointer to loongson ttm buffer object
 *
 * RETURN
 *  Offset of @node for byte-based addressing
 * 0 if the node does not have an object allocatedS
 */
static inline u64 loongson_bo_mmap_offset(struct loongson_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
}

/**
 * loongson_dumb_mmap_offset --return sanitized offset for userspace mmaps
 *
 * @file: the pointer to drm_file structure,DRM file private date
 * @dev: the pointer to drm_device structrure
 * @handle: user space handle
 * @offset: return value pointer
 *
 * RETRUN
 *  return sainitized offset
 */
int loongson_dumb_mmap_offset(struct drm_file *file, struct drm_device *dev,
			      uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	struct loongson_bo *bo;

	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return -ENOENT;

	bo = gem_to_loongson_bo(obj);
	*offset = loongson_bo_mmap_offset(bo);

	drm_gem_object_unreference_unlocked(obj);
	return 0;
}

int loongson_dumb_destroy(struct drm_file *file, struct drm_device *dev,
			  uint32_t handle)
{
	struct drm_gem_object *obj;
	struct loongson_bo *bo;
	int ret;

	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return -ENOENT;

	bo = gem_to_loongson_bo(obj);

	if (bo->pin_count) {
		ret = loongson_bo_reserve(bo, false);
		if (ret)
			return ret;
		ret = loongson_bo_unpin(bo);
		loongson_bo_unreserve(bo);
	}
	return drm_gem_dumb_destroy(file, dev, handle);
}

/**
 * loongson_open_kms -Driver callback when a new struct drm_file is opened.
 * Useful for setting up driver-private data structures like buffer allocators,
 *  execution contexts or similar things.
 *
 * @dev DRM device
 * @file DRM file private date
 *
 * RETURN
 * 0 on success, a negative error code on failure, which will be promoted to
 *  userspace as the result of the open() system call.
 */
static int loongson_open_kms(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;

	DRM_DEBUG("open: dev=%p, file=%p", dev, file);

	return 0;
}

/**
 * loongson_drm_driver_fops - file operations structure
 *
 * .open: file open
 * .release : file close
 * .unlocked_ioctl:
 * .mmap: map device memory to process address space
 * .poll: device status: POLLIN POLLOUT POLLPRI
 * .compat_ioctl: used in 64 bit
 * .read: sync and read data from device
 */
static const struct file_operations loongson_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = loongson_drm_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.read = drm_read,
};

/**
 * loongson_kms_driver - DRM device structure
 *
 * .load: driver callback to complete initialization steps after the driver is registered
 * .unload:Reverse the effects of the driver load callback
 * .open:Driver callback when a new struct drm_file is opened
 * .fops:File operations for the DRM device node.
 * .gem_free_object:deconstructor for drm_gem_objects
 * .dumb_create:This creates a new dumb buffer in the driver’s backing storage manager
 *  (GEM, TTM or something else entirely) and returns the resulting buffer handle.
 *  This handle can then be wrapped up into a framebuffer modeset object
 * .dumb_map_offset:Allocate an offset in the drm device node’s address space
 *  to be able to memory map a dumb buffer
 * .dump_destory:This destroys the userspace handle for the given dumb backing storage buffer
 */
static struct drm_driver loongson_kms_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_HAVE_IRQ,
	.load = loongson_load_kms,
	.unload = loongson_unload_kms,
	.open = loongson_open_kms,
	.set_busid = drm_pci_set_busid,
	.fops = &loongson_drm_driver_fops,
	.gem_free_object = loongson_gem_free_object,
	.dumb_create = loongson_dumb_create,
	.dumb_map_offset = loongson_dumb_mmap_offset,
	.dumb_destroy = loongson_dumb_destroy,
	.ioctls = loongson_ioctls_kms,
	.num_ioctls = DRM_LOONGSON_KMS_MAX_IOCTLS,

	/*vblank*/
	.enable_vblank = loongson_irq_enable_vblank,
	.disable_vblank = loongson_irq_disable_vblank,
	.get_vblank_counter = loongson_crtc_vblank_count,
	/*IRQ*/
	.irq_preinstall = loongson_irq_preinstall,
	.irq_postinstall = loongson_irq_postinstall,
	.irq_uninstall = loongson_irq_uninstall,
	.irq_handler = loongson_irq_handler,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int loongson_platform_probe(struct platform_device *pdev)
{
	return drm_platform_init(&loongson_kms_driver, pdev);
}

static int loongson_platform_remove(struct platform_device *pdev)
{
	return 0;
}

/**
 * pciidlist -- loongson pci device id
 *
 * __u32 vendor, device            Vendor and device ID or PCI_ANY_ID
 * __u32 subvendor, subdevice     Subsystem ID's or PCI_ANY_ID
 * __u32 class, class_mask        (class,subclass,prog-if) triplet
 * kernel_ulong_t driver_data     Data private to the driver
 */
static struct pci_device_id pciidlist[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC) },
	{ 0, 0, 0, 0, 0, 0, 0 }
};

/**
 * loongson_pci_probe -- add pci device
 *
 * @pdev PCI device
 * @ent pci device id
 */
static int loongson_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	return drm_get_pci_dev(pdev, ent, &loongson_kms_driver);
}

/**
 * loongson_pci_remove -- release drm device
 *
 * @pdev PCI device
 */
static void loongson_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	drm_put_dev(dev);
}

/*loongson_drm_suspend - initiate device suspend
 *
 * @pdev: drm dev pointer
 * @state: suspend state
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int loongson_drm_suspend(struct drm_device *dev)
{
	u32 r;
	struct loongson_bo *lbo;
	struct drm_framebuffer *drm_fb;
	struct loongson_framebuffer *lfb;
	struct loongson_device *ldev = dev->dev_private;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	drm_kms_helper_poll_disable(dev);

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, dev) {
		lfb = to_loongson_framebuffer(drm_fb);
		lbo = gem_to_loongson_bo(lfb->obj);
		r = loongson_bo_reserve(lbo, false);
		if (unlikely(r))
			continue;

		r = loongson_bo_unpin(lbo);
		loongson_bo_unreserve(lbo);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	/* evict vram memory. copy fb from vram to ttm pages*/
	ttm_bo_evict_mm(&ldev->ttm.bdev, TTM_PL_VRAM);

	console_lock();
	loongson_fbdev_set_suspend(ldev, 1);
	console_unlock();
	return 0;
}

/*
 *  * loongson_drm_resume - initiate device suspend
 *
 * @pdev: drm dev pointer
 * @state: suspend state
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int loongson_drm_resume(struct drm_device *dev)
{
	u32 r;
	u64 gpu_addr;
	struct loongson_bo *lbo;
	struct drm_framebuffer *drm_fb;
	struct loongson_framebuffer *lfb;
	struct loongson_device *ldev = dev->dev_private;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, dev) {
		lfb = to_loongson_framebuffer(drm_fb);
		lbo = gem_to_loongson_bo(lfb->obj);
		r = loongson_bo_reserve(lbo, false);
		if (unlikely(r))
			continue;

		loongson_bo_pin(lbo, TTM_PL_FLAG_VRAM, &gpu_addr);
		loongson_bo_unreserve(lbo);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	console_lock();
	loongson_encoder_resume(ldev);
	drm_helper_resume_force_mode(dev);
	drm_kms_helper_poll_enable(dev);
	loongson_fbdev_set_suspend(ldev, 0);
	loongson_connector_resume(ldev);
	console_unlock();
	return 0;
}

/**
 * loongson_pmops_suspend
 *
 * @dev   pointer to the device
 *
 * Executed before putting the system into a sleep state in which the
 * contents of main memory are preserved.
 */
static int loongson_pmops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return loongson_drm_suspend(drm_dev);
}

/**
 * loongson_pmops_resume
 *
 * @dev pointer to the device
 *
 * Executed after waking the system up from a sleep state in which the
 * contents of main memory were preserved.
 */
static int loongson_pmops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_resume");
	return loongson_drm_resume(drm_dev);
}

/**
 * loongson_pmops_freeze
 *
 * @dev pointer to the device
 *
 * Executed after waking the system up from a freezz state in which the
 * contents of main memory were preserved.
 */
static int loongson_pmops_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	loongson_drm_suspend(drm_dev);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls_fb_id_table[] = {
	{
		.compatible = "loongson,ls-fb",
	},
	{},
};
#endif

static struct platform_driver loongson_platform_driver = {
	.probe	= loongson_platform_probe,
	.remove = loongson_platform_remove,
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "ls-fb",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_fb_id_table),
#endif
	},
};

/*
 * * struct dev_pm_ops - device PM callbacks
 *
 *@suspend:  Executed before putting the system into a sleep state in which the
 *           contents of main memory are preserved.
 *@resume:   Executed after waking the system up from a sleep state in which the
 *           contents of main memory were preserved.
 *@freeze:   Hibernation-specific, executed before creating a hibernation image.
 *           Analogous to @suspend(), but it should not enable the device to signal
 *           wakeup events or change its power state.  The majority of subsystems
 *           (with the notable exception of the PCI bus type) expect the driver-level
 *           @freeze() to save the device settings in memory to be used by @restore()
 *           during the subsequent resume from hibernation.
 *@thaw:     Hibernation-specific, executed after creating a hibernation image OR
 *           if the creation of an image has failed.  Also executed after a failing
 *           attempt to restore the contents of main memory from such an image.
 *           Undo the changes made by the preceding @freeze(), so the device can be
 *           operated in the same way as immediately before the call to @freeze().
 *@poweroff: Hibernation-specific, executed after saving a hibernation image.

 *           Analogous to @suspend(), but it need not save the device's settings in
 *           memory.
 *@restore:  Hibernation-specific, executed after restoring the contents of main
 *           memory from a hibernation image, analogous to @resume().
 */
static const struct dev_pm_ops loongson_pmops = {
	.suspend = loongson_pmops_suspend,
	.resume = loongson_pmops_resume,
	.freeze = loongson_pmops_freeze,
	.poweroff = loongson_pmops_freeze,
	.restore = loongson_pmops_resume,
};

/**
 * loongson_pci_driver -- pci driver structure
 *
 * .id_table : must be non-NULL for probe to be called
 * .probe: New device inserted
 * .remove: Device removed
 * .resume: Device suspended
 * .suspend: Device woken up
 */
static struct pci_driver loongson_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = loongson_pci_probe,
	.remove = loongson_pci_remove,
	.driver.pm = &loongson_pmops,
};

enum loongson_gpu loongson_find_gpu(void)
{
	struct pci_dev *pdev = NULL;
	enum loongson_gpu gpu;

	pdev = pci_get_device(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC,
			      NULL);
	if (pdev)
		gpu = LS7A_GPU;
	else
		gpu = LS2K_GPU;

	return gpu;
}

/**
 * loongson_init()  -- kernel module init function
 */
static int __init loongson_init(void)
{
	int ret = -1;
	enum loongson_gpu gpu;
	struct pci_dev *pdev = NULL;

	/* Discrete card prefer */
	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev))) {
		if (pdev->vendor != PCI_VENDOR_ID_LOONGSON)
			return 0;
	}

	gpu = loongson_find_gpu();
	switch (gpu) {
	case LS7A_GPU:
		ret = pci_register_driver(&loongson_pci_driver);
		break;
	case LS2K_GPU:
		ret = platform_driver_register(&loongson_platform_driver);
		break;
	}

	return ret;
}

/**
 * loongson_exit()  -- kernel module exit function
 */
static void __exit loongson_exit(void)
{
	enum loongson_gpu gpu;

	gpu = loongson_find_gpu();
	switch (gpu) {
	case LS7A_GPU:
		pci_unregister_driver(&loongson_pci_driver);
		break;
	case LS2K_GPU:
		platform_driver_unregister(&loongson_platform_driver);
		break;
	}
}

module_init(loongson_init);
module_exit(loongson_exit);
MODULE_AUTHOR("Zhu Chen <zhuchen@loongson.cn>");
MODULE_DESCRIPTION("DRIVER_DESC");
MODULE_LICENSE("GPL");
