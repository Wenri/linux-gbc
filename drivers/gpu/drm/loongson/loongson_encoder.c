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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "loongson_drv.h"
/*
 * The encoder comes after the CRTC in the output pipeline, but before
 * the connector. It's responsible for ensuring that the digital
 * stream is appropriately converted into the output format. Setup is
 * very simple in this case - all we have to do is inform qemu of the
 * colour depth in order to ensure that it displays appropriately
 */

/*
 * These functions are analagous to those in the CRTC code, but are intended
 * to handle any encoder-specific limitations
 */



/**
 * loongson_encoder_resolution_match
 * @hdisplay
 * @vdisplay
 * @ config_param
 * */
int loongson_encoder_resolution_match(unsigned int hdisplay,
		unsigned int vdisplay,
		struct loongson_encoder *ls_encoder)
{
	struct loongson_vbios_encoder *vbios_encoder;
	struct loongson_encoder_config_param *config_param;
	struct loongson_resolution_param *resolution;
	int match_index = 0;

	vbios_encoder = ls_encoder->vbios_encoder;
	if (vbios_encoder->config_type == encoder_transparent)
		return match_index;

	config_param = vbios_encoder->mode_config_tables;
	while(match_index < LS_MAX_RESOLUTIONS ){
		resolution = &config_param->resolution;
		if (hdisplay == resolution->hdisplay)
			if (vdisplay == resolution->vdisplay)
				break;
		match_index++;
		config_param++;
	}
	return match_index;
}

static bool mode_set_i2c(struct loongson_encoder *ls_encoder, struct drm_display_mode *mode)
{
	int i;
	unsigned char write_data[2];
	struct i2c_msg write_msgs;
	struct i2c_adapter *adapter;
	struct loongson_vbios_encoder *vbios_encoder;
	struct loongson_encoder_conf_reg *ls_reg;
	int resolution_index;
	int reg_num;

	vbios_encoder = ls_encoder->vbios_encoder;

	if (!ls_encoder->i2c)
		DRM_ERROR("ls encoder modeset no i2c\n");
	adapter = ls_encoder->i2c->adapter;

	if (!vbios_encoder)
		DRM_ERROR("ls encoder mdoeset no vbios \n");

	resolution_index = loongson_encoder_resolution_match(
			mode->hdisplay,mode->vdisplay,
			ls_encoder);


	if (resolution_index == LS_MAX_RESOLUTIONS){
		DRM_ERROR("ls encoder  mdoeset mach resolution err \n");
		return false;
	}

	reg_num = vbios_encoder->mode_config_tables[resolution_index].encoder_resol_param.reg_num;
	for (i = 0; i < reg_num; i++) {
		ls_reg = &vbios_encoder->mode_config_tables[resolution_index].encoder_resol_param.config_regs[i];
		write_msgs.flags  = 0;
		write_msgs.addr   = ls_reg->dev_addr;
		write_data[0]     = ls_reg->reg;
		write_data[1]     = ls_reg->value;
		write_msgs.buf    = write_data;
		write_msgs.len    = 2;
		if (i2c_transfer(adapter, &write_msgs, 1) != 1)
			DRM_INFO("op encoder dev_addr[%#x] reg[%#x] val[%#x]\n",
					ls_reg->dev_addr, ls_reg->reg,ls_reg->value);
	}

	return true;
}


/**
 * loongson_encoder_mode_set
 *
 * @encoder: encoder object
 * @mode: display mode
 * @adjusted_mode: point to the drm_display_mode structure
 *
 * Used to update the display mode of an encoder
 */
static void loongson_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct loongson_encoder *ls_encoder;
	struct loongson_vbios_encoder  *vbios_encoder;
	ls_encoder = to_loongson_encoder(encoder);
	vbios_encoder  = ls_encoder->vbios_encoder;

	if ( ls_encoder && !ls_encoder->mode_set_method )
		return;

	if (vbios_encoder->config_type == encoder_os_config)
	{
		DRM_INFO("Do encoder-%d mode set hdis  %d  vdisp  %d  encoderid  %d \n",
				ls_encoder->encoder_id, mode->hdisplay, mode->vdisplay, ls_encoder->encoder_id);
		ls_encoder->mode_set_method(ls_encoder, mode);
	}
}

/**
 * loongson_encoder_dpms
 *
 * @encoder: encoder object
 *
 * Control power levels on the encoder
 *
 */
static void loongson_encoder_dpms(struct drm_encoder *encoder, int state)
{

}


/**
 * loongson_encoder_prepare
 *
 * @encoder: encoder object
 *
 * Prepare the encoder for a subsequent modeset
 */
static void loongson_encoder_prepare(struct drm_encoder *encoder)
{
}


/**
 * loongson_encoder_commit
 *
 * @encoder: point to tne drm_encoder structure
 *
 * Commit the new mode on the encoder after a modeset
 */
static void loongson_encoder_commit(struct drm_encoder *encoder)
{
}


/**
 * loongson_encoder_destroy
 *
 * @encoder: encoder object
 *
 * Clean up encoder resources
 */
static void loongson_encoder_destroy(struct drm_encoder *encoder)
{
	struct loongson_encoder *loongson_encoder = to_loongson_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(loongson_encoder);
}


/**
 * These provide the minimum set of functions required to handle a encoder
 *
 *  Helper operations for encoders
 */
static const struct drm_encoder_helper_funcs loongson_encoder_helper_funcs = {
	.dpms = loongson_encoder_dpms,
	.mode_set = loongson_encoder_mode_set,
	.prepare = loongson_encoder_prepare,
	.commit = loongson_encoder_commit,
};


/**
 * These provide the minimum set of functions required to handle a encoder
 *
 * Encoder controls,encoder sit between CRTCs and connectors
 */
static const struct drm_encoder_funcs loongson_encoder_encoder_funcs = {
	.destroy = loongson_encoder_destroy,
};


/**
 * loongson_encoder_init
 *
 * @dev: point to the drm_device structure
 *
 * Init encoder
 */
struct loongson_encoder *loongson_encoder_init(struct loongson_drm_device *ldev, int index)
{
	struct drm_encoder *encoder;
	struct loongson_encoder *ls_encoder;
	struct loongson_vbios_encoder *lsvbios_enc = ldev->encoder_vbios[index];

	ls_encoder = kzalloc(sizeof(struct loongson_encoder), GFP_KERNEL);
	if (!ls_encoder)
		return NULL;

	DRM_INFO("loongson encoder_index %d\n",index);
	ls_encoder->encoder_id = index;
	ls_encoder->ldev = ldev;

	ls_encoder->mode_set_method = mode_set_i2c;
	ls_encoder->vbios_encoder = lsvbios_enc;

	if (ldev->vbios->version_minor == 1 && ldev->vbios->version_major == 0 )
		ls_encoder->mode_set_method = loongson_encoder_reset_3a3k;

	encoder = &ls_encoder->base;
	encoder->possible_crtcs = 1 << index;

	ls_encoder->i2c = loongson_i2c_bus_match(ldev,lsvbios_enc->i2c_id);
	if (!ls_encoder->i2c) {
		DRM_INFO("lson encoder-%d match i2c-%d adap err\n",index,lsvbios_enc->i2c_id);
	}

	drm_encoder_init(ldev->dev, encoder, &loongson_encoder_encoder_funcs,
			lsvbios_enc->type, NULL);

	drm_encoder_helper_add(encoder, &loongson_encoder_helper_funcs);

	return ls_encoder;
}

void loongson_encoder_do_resume(struct loongson_encoder *ls_encoder)
{
	if (ls_encoder) {
		DRM_INFO("Do ls encoder-%d resmue\n",ls_encoder->encoder_id);
		ls_encoder->mode_set_method(ls_encoder,&ls_encoder->base.crtc->mode);
	}
}

void loongson_encoder_resume(struct loongson_drm_device *ldev)
{
	int i;
	struct loongson_mode_info *ls_mode_info;
	struct loongson_encoder   *ls_encoder;

	for (i = 0; i< LS_MAX_MODE_INFO; i++){
		ls_mode_info = &ldev->mode_info[i];
		if (ls_mode_info->mode_config_initialized == true){
			ls_encoder = ls_mode_info->encoder;
			if ( ls_encoder->vbios_encoder->config_type == encoder_bios_config )
				continue;
			loongson_encoder_do_resume(ls_encoder);
		}
	}
}
