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

#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/pm_runtime.h>
#include <drm/drmP.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <linux/gpio.h>
#include <linux/pwm.h>
#include <linux/moduleparam.h>

#include "loongson_drv.h"

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
static struct eep_info{
	struct i2c_adapter *adapter;
	unsigned short addr;
}eeprom_info[2];
#endif

/**
 * loongson_connector_best_encoder
 *
 * @connector: point to the drm_connector structure
 *
 * Select the best encoder for the given connector.Used by both the helpers(in the
 * drm_atomic_helper_check_modeset() function)and in the legacy CRTC helpers
 */
static struct drm_encoder *loongson_connector_best_encoder(struct drm_connector
						  *connector)
{
	int enc_id = connector->encoder_ids[0];
	/* pick the encoder ids */
	if (enc_id)
		return drm_encoder_find(connector->dev, enc_id);
	return NULL;
}


/**
 * loongson_do_probe_ddc_edid
 *
 * @adapter: I2C device adapter
 *
 * Try to fetch EDID information by calling I2C driver functions
 */
static bool loongson_do_probe_ddc_edid(struct i2c_adapter *adapter, unsigned char *buf)
{
	unsigned char start = 0x0;
	unsigned int che_tmp = 0;
	unsigned int i;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = &start,
		},{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = EDID_LENGTH * 2,
			.buf = buf,
		}
	};
	if (i2c_transfer(adapter, msgs, 2) == 2) {
		if (buf[126] != 0) {
			buf[126] = 0;
			che_tmp = 0;
			for(i = 0;i < 127;i++) {
				che_tmp += buf[i];
			}
			buf[127] = 256-(che_tmp)%256;
		}
		if (!drm_edid_block_valid(buf, 0, true, NULL)) {
                        dev_warn_once(&adapter->dev, "Invalid EDID block\n");
                        return false;
                }
        } else {
                 dev_warn_once(&adapter->dev, "unable to read EDID block\n");
                 return false;
        }
        return true;
}

/**
 * loongson_i2c_connector
 *
 * According to i2c bus,acquire screen information
 */
static bool loongson_i2c_connector(struct loongson_connector *ls_connector, unsigned char *buf)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	int id  = ls_connector->connector_id;
	if ( id >> 1 ) {
		DRM_INFO("lson 2k i2c apapter err");
		return false;
	}
	return loongson_do_probe_ddc_edid(eeprom_info[id].adapter, buf);
#else
	if ( ls_connector->i2c != NULL  && ls_connector->i2c->adapter != NULL )
		return loongson_do_probe_ddc_edid(ls_connector->i2c->adapter, buf);
	else{
		DRM_INFO_ONCE("get loongson connector  adapter err\n");
		return false;
	}
#endif
}

/**
 * loongson_vga_get_modes
 *
 * @connetcor: central DRM connector control structure
 *
 * Fill in all modes currently valid for the sink into the connector->probed_modes list.
 * It should also update the EDID property by calling drm_mode_connector_update_edid_property().
 */
static int loongson_vga_get_modes(struct drm_connector *connector)
{
	enum loongson_edid_method ledid_method;
	struct loongson_vbios_connector *lbios_connector;
	struct loongson_connector *ls_connector = to_loongson_connector(connector);
	unsigned char buf[EDID_LENGTH *2];
	bool dret = false;
	int ret = 0;

	lbios_connector = ls_connector->vbios_connector;
	ledid_method = lbios_connector->edid_method;

	if (ledid_method == edid_method_vbios) {
		memcpy(buf, lbios_connector->internal_edid, EDID_LENGTH*2);
		dret = true;
		DRM_INFO_ONCE("conn-%d Do Vbios edid ",ls_connector->connector_id);
	}else
		dret = loongson_i2c_connector(ls_connector, buf);

	if (dret == true) {
		drm_mode_connector_update_edid_property(connector, (struct edid *)buf);
		ret = drm_add_edid_modes(connector, (struct edid *)buf);
	}

	return ret;
}
/*
 * loongson_vga_mode_valid
 *
 * @connector: point to the drm connector
 * @mode: point to the drm_connector structure
 *
 * Validate a mode for a connector, irrespective of the specific display configuration
 */
static int loongson_vga_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct loongson_drm_device *ldev = (struct loongson_drm_device*)dev->dev_private;
	int id = connector->index;

        if(mode->hdisplay % 64)
		return MODE_BAD;
	if(mode->hdisplay > ldev->crtc_vbios[id]->crtc_max_weight || mode->vdisplay > ldev->crtc_vbios[id]->crtc_max_height)
		return MODE_BAD;
	return MODE_OK;
}

/**
 * loongson_vga_detect
 *
 * @connector: point to drm_connector
 * @force: bool
 *
 * Check to see if anything is attached to the connector.
 * The parameter force is set to false whilst polling,
 * true when checking the connector due to a user request
 */
static enum drm_connector_status loongson_connector_detect(struct drm_connector
						   *connector, bool force)
{
	struct loongson_connector *ls_connector;
	struct loongson_vbios_connector *lsvbios_conn;
	enum drm_connector_status ret = connector_status_disconnected;
	enum loongson_edid_method ledid_method;
	unsigned char buf[EDID_LENGTH *2];

	ls_connector = to_loongson_connector(connector);
	lsvbios_conn = ls_connector->vbios_connector;

	if (ls_connector->vbios_connector->hot_swap_method == hot_swap_disable)
		return connector_status_connected;

	ledid_method = lsvbios_conn->edid_method;
	DRM_DEBUG("connect%d edid_method:%d\n", ls_connector->connector_id, ledid_method);
	switch (ledid_method) {
	case edid_method_i2c:
	case edid_method_null:
	case edid_method_max:
		if ( loongson_i2c_connector(ls_connector, buf) == true) {
			DRM_INFO_ONCE("Loongson connector%d connected",
					ls_connector->connector_id);

			ret = connector_status_connected;
		}
		break;
	case edid_method_vbios:
	case edid_method_encoder:
		ret = connector_status_connected;
		break;
	}
	return ret;
}

/**
 * loongson_connector_destroy
 *
 * @connector: point to the drm_connector structure
 *
 * Clean up connector resources
 */
static void loongson_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(connector);
}


/**
 * These provide the minimum set of functions required to handle a connector
 *
 * Helper operations for connectors.These functions are used
 * by the atomic and legacy modeset helpers and by the probe helpers.
 */
static const struct drm_connector_helper_funcs loongson_vga_connector_helper_funcs = {
        .get_modes = loongson_vga_get_modes,
        .mode_valid = loongson_vga_mode_valid,
        .best_encoder = loongson_connector_best_encoder,
};

/**
 * @loongson_connector_pwm_get
 *
 * @Param ls_connector loongson drm connector
 *
 * @Returns  0 is ok
 */
unsigned int loongson_connector_pwm_get(struct loongson_connector *ls_connector)
{
	unsigned int duty_ns;
	unsigned int period_ns;
	unsigned int level;
	struct loongson_vbios_connector *vbios_connector=
		ls_connector->vbios_connector;

	if (IS_ERR(ls_connector->bl.pwm))
		return 0;

	period_ns = vbios_connector->bl_pwm.period_ns;
	duty_ns = pwm_get_duty_cycle(ls_connector->bl.pwm);

	level = DIV_ROUND_UP((duty_ns * ls_connector->bl.max), period_ns);
	level = clamp(level, ls_connector->bl.min, ls_connector->bl.max);
	return level;
}

/*
 * @Function  loongson_connector_pwm_set
 *
 *@ls_connector
 **/
void loongson_connector_pwm_set(struct loongson_connector *ls_connector,
				unsigned int level)
{
	unsigned int period_ns;
	unsigned int duty_ns;
	struct loongson_vbios_connector *vbios_connector=
		ls_connector->vbios_connector;

	if (IS_ERR(ls_connector->bl.pwm))
		return ;

	level = clamp(level, ls_connector->bl.min, ls_connector->bl.max);
	period_ns = vbios_connector->bl_pwm.period_ns;
	duty_ns = DIV_ROUND_UP((level * period_ns), ls_connector->bl.max);

	pwm_config(ls_connector->bl.pwm, duty_ns, period_ns);
}

/**
 * @loongson_connector_pwm_enable
 *
 * @Param ls_connector loongson drm connector
 * @Param enable  enable hw
 */
static void
loongson_connector_pwm_enable(struct loongson_connector *ls_connector,
			      bool enable)
{
	struct loongson_vbios_connector *lsvbios_connector;
	lsvbios_connector = ls_connector->vbios_connector;

	if (IS_ERR(ls_connector->bl.pwm))
		return;

	if (enable){
		pwm_enable(ls_connector->bl.pwm);
	}
	else {
		pwm_disable(ls_connector->bl.pwm);
	}
}

/**
 * @loongson_connector_pwm_setup
 *
 * @Param ls_connector loongson drm connector
 *
 * @Returns  0 is ok
 */
int loongson_connector_pwm_setup(struct loongson_connector *ls_connector)
{
	struct loongson_vbios_connector *vbios_connector =
		ls_connector->vbios_connector;
	unsigned int pwm_period_ns = vbios_connector->bl_pwm.period_ns;

	ls_connector->bl.hw_enabled = true;
	ls_connector->bl.level = ls_connector->bl.get_brightness(ls_connector);
	pwm_set_period(ls_connector->bl.pwm, pwm_period_ns);
	pwm_set_polarity(ls_connector->bl.pwm,
			vbios_connector->bl_pwm.polarity);

	gpio_direction_output(LOONGSON_GPIO_LCD_EN, 1);
	gpio_direction_output(LOONGSON_GPIO_LCD_VDD,1);

	return 0;
}

/**
 * @loongson_connector_pwm_get_resource
 *
 * @Param ls_connector loongson drm connector
 *
 * @Returns 0 is get resource ok
 */
int loongson_connector_pwm_get_resource(struct loongson_connector *ls_connector)
{
	int ret;

	struct drm_device *dev = ls_connector->base.dev;
	struct loongson_backlight *bl = &ls_connector->bl;
	struct loongson_vbios_connector *vbios_connector = ls_connector->vbios_connector;

	bl->pwm = pwm_request(vbios_connector->bl_pwm.pwm_id, "Loongson_bl");

	if (IS_ERR(bl->pwm)) {
		DRM_DEV_ERROR(dev->dev,"Failed to get the pwm chip\n");
		bl->pwm = NULL;
		return 1;
	}

	ret = gpio_request(LOONGSON_GPIO_LCD_VDD, "GPIO_VDD");
	if (ret) {
            DRM_DEV_INFO(ls_connector->ldev->dev->dev,"EN request error!\n");
	    goto free_gpu_pwm;
	}

	ret = gpio_request(LOONGSON_GPIO_LCD_EN, "GPIO_EN");
        if(ret < 0) {
            DRM_DEV_INFO(ls_connector->ldev->dev->dev,"VDD request error!\n");
	    goto free_gpio_vdd;
        }

	return 0;

free_gpio_vdd:
	gpio_free(LOONGSON_GPIO_LCD_VDD);
free_gpu_pwm:
	pwm_free(bl->pwm);
	bl->pwm = NULL;
	return 1;
}

/**
 * @loongson_connector_pwm_free_resource
 *
 * @Param ls_connector loongson drm connector
 */
void loongson_connector_pwm_free_resource(struct loongson_connector *ls_connector)
{
	struct loongson_backlight *bl = &ls_connector->bl;

	if (bl->pwm)
		pwm_free(bl->pwm);

	gpio_free(LOONGSON_GPIO_LCD_EN);
	gpio_free(LOONGSON_GPIO_LCD_VDD);
}

/**
 * @loongson_connector_lvds_power
 *
 * @ls_connector loongson  drm connector
 * @enable control power-on or power-down
 */
void loongson_connector_lvds_power(struct loongson_connector *ls_connector,
				   bool enable)
{
	gpio_set_value(LOONGSON_GPIO_LCD_EN, enable);
}
/**
 * loongson_connector_backlight_funcs_register
 * @ls_connector loongson drm connector
 * */
void loongson_connector_backlight_pwm_funcs_register(
		struct loongson_connector *ls_connector)
{
	ls_connector->bl.min		= LOONGSON_BL_MIN_LEVEL;
	ls_connector->bl.max		= LOONGSON_BL_MAX_LEVEL;

	ls_connector->bl.get_resource   = loongson_connector_pwm_get_resource;
	ls_connector->bl.free_resource  = loongson_connector_pwm_free_resource;
	ls_connector->bl.setup          = loongson_connector_pwm_setup;
	ls_connector->bl.get_brightness = loongson_connector_pwm_get;
	ls_connector->bl.set_brightness = loongson_connector_pwm_set;
	ls_connector->bl.enable         = loongson_connector_pwm_enable;
	ls_connector->bl.power_op       = loongson_connector_lvds_power;
}

/**
 * loongson_connector_backlight_updat
 * @bd      backlight device
 * @return  operation ok retuen 0
 * */
static int loongson_connector_backlight_update(struct backlight_device *bd)
{
	bool enable;
	struct loongson_connector *ls_connector = bl_get_data(bd);
	struct drm_device *dev = ls_connector->base.dev;
	struct loongson_backlight *backlight = &ls_connector->bl;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (bd->props.brightness < backlight->min){
		bd->props.brightness = backlight->level;
		drm_modeset_unlock(&dev->mode_config.connection_mutex);
		return -EINVAL;
	}

	enable = bd->props.power == FB_BLANK_UNBLANK;
	ls_connector->bl.enable(ls_connector, enable);
	if (backlight->power_op)
		backlight->power_op(ls_connector, enable);
	backlight->hw_enabled = enable;

	backlight->level = bd->props.brightness;
	ls_connector->bl.set_brightness(ls_connector, backlight->level);

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	return 0;
}
/**
 * loongson_connector_get_brightness
 * @ls_connector loongson drm connector
 * */
static int loongson_connector_get_brightness(struct backlight_device *bd)
{
	struct loongson_connector *ls_connector = bl_get_data(bd);

	if (ls_connector->bl.get_brightness)
		return ls_connector->bl.get_brightness(ls_connector);

	return -ENOEXEC;
}

static const struct backlight_ops ls_backlight_device_ops = {
	.update_status  = loongson_connector_backlight_update,
	.get_brightness = loongson_connector_get_brightness,
};
/**
 * loongson_connector_backlight_register
 * @ls_connector loongson drm connector
 * @return  0 is ok .
 * */
int loongson_connector_backlight_register(struct loongson_connector *ls_connector)
{
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = ls_connector->bl.max;
	props.brightness = ls_connector->bl.level;
;
	ls_connector->bl.device =
		backlight_device_register("loongson-gpu",
					  ls_connector->base.kdev,
					  ls_connector,
					  &ls_backlight_device_ops, &props);

	if (IS_ERR(ls_connector->bl.device)) {
		DRM_DEV_ERROR(ls_connector->ldev->dev->dev,
				"Failed to register backlight\n");

		ls_connector->bl.device = NULL;
		return -ENODEV;
	}

	DRM_INFO("Loongson connector%s bl sysfs interface registered\n",
		      ls_connector->base.name);

	return 0;
}
/** loongson_connector_pwm_init
 * @ls_connector loongson drm connector
 * */
int loongson_connector_pwm_init(struct loongson_connector *ls_connector)
{
	int ret=1;
	struct loongson_backlight *bl = &ls_connector->bl;

	if (bl->get_resource)
		ret = bl->get_resource(ls_connector);
	if (ret) {
		DRM_DEV_INFO(ls_connector->ldev->dev->dev,"get resrouce err");
		return ret;
	}

	if (bl->setup)
		ret = bl->setup(ls_connector);
	if (ret) {
		DRM_DEV_INFO(ls_connector->ldev->dev->dev,"pwm set err");
		return ret;
	}
	return ret;
}
/**
 * loongson_connector_late_register
 * @connector  drm connector
 * @returns
 * */
int  loongson_connector_late_register(struct drm_connector *connector)
{
	int ret = 1;
	struct loongson_vbios_connector *vbios_connector;
	struct loongson_connector *ls_connector =
		to_loongson_connector(connector);

	vbios_connector = ls_connector->vbios_connector;

	if ((vbios_connector->type == DRM_MODE_CONNECTOR_LVDS) ||
			(vbios_connector->type == DRM_MODE_CONNECTOR_eDP))
	{
		loongson_connector_backlight_pwm_funcs_register(ls_connector);
		ret = loongson_connector_pwm_init(ls_connector);
		if (ret == 0){
			ret = loongson_connector_backlight_register(ls_connector);

			if (ret == 0)
				ls_connector->bl.present = true;
		}
	}

	return 0;
}

/**
 * @loongson_connector_early_unregister
 *
 * @Param connector loongson drm connector
 */
void loongson_connector_early_unregister(struct drm_connector *connector)
{
	struct loongson_connector *ls_connector;
	struct loongson_backlight *bl;
	ls_connector = to_loongson_connector(connector);

	if (IS_ERR(ls_connector))
		return;

	bl = &ls_connector->bl;
	if (bl->present == true) {
		if (bl->free_resource)
			bl->free_resource(ls_connector);
	}
	bl->present = false;
}
/**
 * These provide the minimum set of functions required to handle a connector
 *
 * Control connectors on a given device.
 * The functions below allow the core DRM code to control connectors,
 * enumerate available modes and so on.
 */
static const struct drm_connector_funcs loongson_vga_connector_funcs = {
        .dpms = drm_helper_connector_dpms,
        .detect = loongson_connector_detect,
	.late_register = loongson_connector_late_register,
	.early_unregister = loongson_connector_early_unregister,
        .fill_modes = drm_helper_probe_single_connector_modes,
        .destroy = loongson_connector_destroy,
};


#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

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

static int dvi_eep_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	eeprom_info[0].adapter = client->adapter;
	eeprom_info[0].addr = client->addr;
	return 0;
}


static int vga_eep_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	eeprom_info[1].adapter = client->adapter;
	eeprom_info[1].addr = client->addr;
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
	.probe = vga_eep_probe,
	.remove = eep_remove,
	.id_table = vga_eep_ids,
};

static struct i2c_driver dvi_eep_driver = {
	.driver = {
		.name = "dvi_eep-edid",
		.owner = THIS_MODULE,
	},
	.probe = dvi_eep_probe,
	.remove = eep_remove,
	.id_table = dvi_eep_ids,
};



#endif


/**
 * loongson_vga_init
 *
 * @dev: drm device
 * @connector_id:
 *
 * Vga is the interface between host and monitor
 * This function is to init vga
 */
struct loongson_connector *loongson_connector_init(struct loongson_drm_device *ldev, int index)
{
	struct drm_connector *connector;
	struct loongson_connector *ls_connector;
	struct loongson_vbios_connector *lsvbios_connector = ldev->connector_vbios[index];


	ls_connector = kzalloc(sizeof(struct loongson_connector), GFP_KERNEL);
	if (!ls_connector)
		return NULL;

	ls_connector->vbios_connector = lsvbios_connector;
	ls_connector->ldev   = ldev;
	ls_connector->connector_id = index;

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	if(index == 0){
		i2c_add_driver(&dvi_eep_driver);
	}else{
		i2c_add_driver(&vga_eep_driver);
	}
#else
	ls_connector->i2c = loongson_i2c_bus_match(ldev,lsvbios_connector->i2c_id);
	if (!ls_connector->i2c) {
		DRM_INFO("lson connector-%d match i2c-%d err\n",index,lsvbios_connector->i2c_id);
	}
#endif
	connector = &ls_connector->base;

	drm_connector_init(ldev->dev, connector,
			   &loongson_vga_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_connector_helper_add(connector, &loongson_vga_connector_helper_funcs);

	drm_connector_register(connector);

	switch(ldev->connector_vbios[index]->hot_swap_method){
		case hot_swap_irq:
			connector->polled = DRM_CONNECTOR_POLL_HPD;
			break;
		case hot_swap_polling:
			connector->polled = DRM_CONNECTOR_POLL_CONNECT
				| DRM_CONNECTOR_POLL_DISCONNECT;
			break;
		case hot_swap_disable:
		default:
			connector->polled = 0;
			break;
	}
	return ls_connector;
}
/**
 * loongson_connector_bl_resume
 *
 * @ls_connector loongson drm connector
 * */
void  loongson_connector_bl_resume(struct loongson_connector *ls_connector)
{

	struct loongson_backlight *backlight = &ls_connector->bl;

	if (backlight->present == true) {
		backlight->setup(ls_connector);
		backlight->set_brightness(ls_connector,
				backlight->device->props.brightness);

		backlight->enable(ls_connector, backlight->hw_enabled);
		if (backlight->power_op)
			backlight->power_op(ls_connector, backlight->hw_enabled);
	}
}
/**
 * loongson_connector_resume
 *
 * @ldev loongson drm device
 * */
void loongson_connector_resume(struct loongson_drm_device *ldev)
{
	struct loongson_mode_info *ls_mode_info;
	struct loongson_connector *ls_connector;
	int i;

	for (i = 0; i< LS_MAX_MODE_INFO; i++) {
		ls_mode_info = &ldev->mode_info[i];
		if (ls_mode_info->mode_config_initialized == true){
			ls_connector = ls_mode_info->connector;
			loongson_connector_bl_resume(ls_connector);
		}
	}
}
