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

#include "loongson_drv.h"

#define adapter_to_i2c_client(d) container_of(d, struct i2c_client, adapter)

#define DVO_I2C_NAME "loongson_dvo_i2c"

static struct eep_info{
	struct i2c_adapter *adapter;
	unsigned short addr;
}eeprom_info[2];

struct i2c_client * loongson_drm_i2c_client[2];

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
static bool loongson_do_probe_ddc_edid(struct i2c_adapter *adapter, unsigned int id, unsigned char *buf)
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
static bool loongson_i2c_connector(unsigned int id, unsigned char *buf)
{
	if (eeprom_info[id].adapter)
		return loongson_do_probe_ddc_edid(eeprom_info[id].adapter, id, buf);
	else
		return false;
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
	struct drm_device *dev = connector->dev;
	struct loongson_drm_device *ldev = dev->dev_private;
	struct loongson_vbios_connector *lbios_connector =
			ldev->connector_vbios[drm_connector_index(connector)];
        enum loongson_edid_method ledid_method;
        unsigned char *buf = kmalloc(EDID_LENGTH *2, GFP_KERNEL);
	struct edid *edid = NULL;
	bool dret = false;
	int ret = 0;

	if (!buf) {
                dev_warn(dev->dev, "Unable to allocate memory for EDID block\n");
                return 0;
        }

	ledid_method = lbios_connector->edid_method;

	DRM_DEBUG("connecotro_id = %d\n",connector->index);
	dret = loongson_i2c_connector(connector->index, buf);
	if (dret == true) {
		if (ledid_method == edid_method_vbios)
			memcpy(buf, lbios_connector->internal_edid, EDID_LENGTH*2);

		edid = (struct edid *)buf;
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
	}

	kfree(buf);
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
 * loongson_i2c_destroy
 *
 * @i2c: point to loongson_i2c_chan
 *
 * Destroy i2c adapter
 */
void loongson_i2c_destroy(struct loongson_i2c_chan *i2c)
{
	if (!i2c)
		return;
	i2c_del_adapter(i2c->adapter);
}

/* Power on when there is noconnector,support hotplug */
static bool poweron_flags = true;

/**
 * loongson_vga_boots
 *
 * Power on when the host is not connected to any monitor
 */
static enum drm_connector_status loongson_vga_boots(enum drm_connector_status status,
                                                        enum drm_connector_status ret)
{
        if (status == connector_status_unknown &&
			ret == connector_status_disconnected)
        {
                ret = connector_status_connected;
        }
        if (status == connector_status_connected &&
			ret == connector_status_connected)
        {
                poweron_flags = false;
        }
        if (poweron_flags == true && status == connector_status_connected &&
			ret == connector_status_disconnected)
        {
                ret =connector_status_connected;
        }
        return ret;
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
static enum drm_connector_status loongson_vga_detect(struct drm_connector
						   *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct loongson_drm_device *ldev = dev->dev_private;
	enum drm_connector_status ret = connector_status_disconnected;
	enum loongson_edid_method ledid_method;
	int i;
	enum drm_connector_status status;
	status = connector->status;

	ledid_method = ldev->connector_vbios[connector->index]->edid_method;

	DRM_DEBUG("loongson_vga_detect connect_id=%d, ledid_method=%d\n", connector->index, ledid_method);

	switch(ledid_method){
		case edid_method_i2c:
		case edid_method_vbios:
			i = pm_runtime_get_sync(connector->dev->dev);
			if (i < 0)
				ret = connector_status_disconnected;
			i = loongson_vga_get_modes(connector);
			if (i)
			{
				DRM_INFO_ONCE("loongson_vga_detect: connected");
				ret = connector_status_connected;
			}
			/*FIXED: FIX vbios v0.1 */
			if (ldev->vbios->version_major ==0  &&
					ldev->vbios->version_minor ==1)
				ret = loongson_vga_boots(status,ret);

			pm_runtime_mark_last_busy(connector->dev->dev);
			pm_runtime_put_autosuspend(connector->dev->dev);
			break;
		case edid_method_encoder:
		case edid_method_null:
		case edid_method_max:
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
	struct loongson_connector *loongson_connector = to_loongson_connector(connector);
	loongson_i2c_destroy(loongson_connector->i2c);
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
 * These provide the minimum set of functions required to handle a connector
 *
 * Control connectors on a given device.
 * The functions below allow the core DRM code to control connectors,
 * enumerate available modes and so on.
 */
static const struct drm_connector_funcs loongson_vga_connector_funcs = {
        .dpms = drm_helper_connector_dpms,
        .detect = loongson_vga_detect,
        .fill_modes = drm_helper_probe_single_connector_modes,
        .destroy = loongson_connector_destroy,
};


static const unsigned short normal_i2c[] = { 0x50, I2C_CLIENT_END };




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
	struct i2c_adapter * i2c_adap;
	struct i2c_board_info i2c_info;


	ls_connector = kzalloc(sizeof(struct loongson_connector), GFP_KERNEL);
	if (!ls_connector)
		return NULL;

	ls_connector->vbios_connector = ldev->connector_vbios[index];
	ls_connector->ldev   = ldev;

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	if(index == 0){
		i2c_add_driver(&dvi_eep_driver);
	}else{
		i2c_add_driver(&vga_eep_driver);
	}
#else
	i2c_adap = i2c_get_adapter(ldev->connector_vbios[index]->i2c_id);
	/*TODO encoder Use the same DTS*/
	if (ldev->connector_vbios[index]->edid_method == edid_method_encoder) {
		eeprom_info[index].adapter = i2c_adap;
		goto connector_init;
	}
	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	strlcpy(i2c_info.type, DVO_I2C_NAME, I2C_NAME_SIZE);
	i2c_info.addr = normal_i2c[0];
	loongson_drm_i2c_client[index] = i2c_new_device(i2c_adap, &i2c_info);
	i2c_put_adapter(i2c_adap);

	if(loongson_drm_i2c_client[index] != NULL){
		eeprom_info[index].adapter= loongson_drm_i2c_client[index]->adapter;
		eeprom_info[index].addr = 0x50;
	}else{
		return NULL;
	}
#endif
connector_init:

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
			connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
			break;
		case hot_swap_disable:
		default:
			connector->polled = 0;
			break;
	}
	return ls_connector;
}
