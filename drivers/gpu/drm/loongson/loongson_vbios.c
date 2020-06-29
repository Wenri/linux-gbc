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

#define VBIOS_START_ADDR 0x1000
#define VBIOS_SIZE 0x1E000

uint POLYNOMIAL = 0xEDB88320 ;
int have_table = 0 ;
uint table[256] ;

u64 vgabios_addr __attribute__((weak)) = 0;
extern unsigned char ls_spiflash_read_status(void);
extern int ls_spiflash_read(int addr, unsigned char *buf,int data_len);

int __attribute__((weak))
ls_spiflash_read(int addr, unsigned char *buf, int data_len)
{
	return 0;
}

unsigned char __attribute__((weak)) ls_spiflash_read_status(void)
{
	return 0xff;
}

void make_table(void)
{
	int i, j;
	have_table = 1 ;
	for (i = 0 ; i < 256 ; i++)
		for (j = 0, table[i] = i ; j < 8 ; j++)
			table[i] = (table[i]>>1)^((table[i]&1)?POLYNOMIAL:0) ;
}

uint lscrc32(uint crc, char *buff, int len)
{
	int i;
	if (!have_table) make_table();
	crc = ~crc;
	for (i = 0; i < len; i++)
		crc = (crc >> 8) ^ table[(crc ^ buff[i]) & 0xff];
	return ~crc;
}

void * loongson_vbios_default(void)
{
	struct loongson_vbios *vbios;
	struct loongson_vbios_crtc * crtc_vbios[2];
	struct loongson_vbios_connector *connector_vbios[2];
	struct loongson_vbios_encoder *encoder_vbios[2];
	unsigned char * vbios_start;
	char * title="Loongson-VBIOS";
	int i;
	enum loongson_gpu gpu;

	vbios = kzalloc(120*1024, GFP_KERNEL);
	vbios_start = (unsigned char *)vbios;
	gpu = loongson_find_gpu();

	i = 0;
	while (*title != '\0') {
		if (i > 15) {
			vbios->title[15] = '\0';
			break;
		}
		vbios->title[i++] = *title;
		title++;
	}

	/*Build loongson_vbios struct*/
	vbios->version_major = 0;
	vbios->version_minor = 1;
	vbios->crtc_num = 2;
	vbios->crtc_offset = sizeof(struct loongson_vbios);
	vbios->connector_num = 2;
	vbios->connector_offset = sizeof(struct loongson_vbios) +
		2 * sizeof(struct loongson_vbios_crtc);
	vbios->encoder_num = 2;
	vbios->encoder_offset = sizeof(struct loongson_vbios) +
		2 * sizeof(struct loongson_vbios_crtc) +
		2 * sizeof(struct loongson_vbios_connector);

	/*Build loongson_vbios_crtc struct*/
	crtc_vbios[0] = (struct loongson_vbios_crtc *)(vbios_start +
			vbios->crtc_offset);
	crtc_vbios[1] = (struct loongson_vbios_crtc *)(vbios_start +
			vbios->crtc_offset + sizeof(struct loongson_vbios_crtc));

	crtc_vbios[0]->next_crtc_offset = sizeof(struct loongson_vbios) +
		sizeof(struct loongson_vbios_crtc);
	crtc_vbios[0]->crtc_id = 0;
	crtc_vbios[0]->crtc_version = default_version;
	crtc_vbios[0]->crtc_max_weight = 2048;
	crtc_vbios[0]->crtc_max_height = 2048;
	crtc_vbios[0]->encoder_id = 0;
	crtc_vbios[0]->use_local_param = false;

	crtc_vbios[1]->next_crtc_offset = 0;
	crtc_vbios[1]->crtc_id = 1;
	crtc_vbios[1]->crtc_version = default_version;
	crtc_vbios[1]->crtc_max_weight = 2048;
	crtc_vbios[1]->crtc_max_height = 2048;
	crtc_vbios[1]->encoder_id = 1;
	crtc_vbios[1]->use_local_param = false;

	/*Build loongson_vbios_encoder struct*/
	encoder_vbios[0] = (struct loongson_vbios_encoder *)(vbios_start +
			vbios->encoder_offset);
	encoder_vbios[1] = (struct loongson_vbios_encoder *)
		(vbios_start + vbios->encoder_offset +
		 sizeof(struct loongson_vbios_encoder));

	/*Build loongson_vbios_connector struct*/
	connector_vbios[0] = (struct loongson_vbios_connector *)
		(vbios_start + vbios->connector_offset);
	connector_vbios[1] = (struct loongson_vbios_connector *)
		(vbios_start + vbios->connector_offset +
		 sizeof(struct loongson_vbios_connector));

	connector_vbios[0]->next_connector_offset =
		vbios->connector_offset + sizeof(struct loongson_vbios_connector);
	connector_vbios[1]->next_connector_offset = 0;

	switch (gpu) {
	case LS7A_GPU:
		encoder_vbios[0]->i2c_id = 6;
		encoder_vbios[1]->i2c_id = 7;

		encoder_vbios[0]->config_type = encoder_transparent;
		encoder_vbios[1]->config_type = encoder_transparent;

		connector_vbios[0]->i2c_id = 6;
		connector_vbios[1]->i2c_id = 7;

		connector_vbios[0]->hot_swap_method = hot_swap_polling;
		connector_vbios[1]->hot_swap_method = hot_swap_polling;

		connector_vbios[0]->edid_method = edid_method_i2c;
		connector_vbios[1]->edid_method = edid_method_i2c;
		break;
	case LS2K_GPU:
		encoder_vbios[0]->i2c_id = 2;
		encoder_vbios[1]->i2c_id = 3;

		encoder_vbios[0]->config_type = encoder_bios_config;
		encoder_vbios[1]->config_type = encoder_bios_config;

		connector_vbios[0]->i2c_id = 2;
		connector_vbios[1]->i2c_id = 3;

		connector_vbios[0]->edid_method = edid_method_i2c;
		connector_vbios[1]->edid_method = edid_method_i2c;
		break;
	}

	connector_vbios[0]->i2c_type = i2c_type_gpio;
	connector_vbios[1]->i2c_type = i2c_type_gpio;

	connector_vbios[0]->hot_swap_method = hot_swap_polling;
	connector_vbios[1]->hot_swap_method = hot_swap_polling;

	/*Build loongson_vbios_encoder struct*/
	encoder_vbios[0] = (struct loongson_vbios_encoder *)
		(vbios_start + vbios->encoder_offset);
	encoder_vbios[1] = (struct loongson_vbios_encoder *)
		(vbios_start + vbios->encoder_offset +
		 sizeof(struct loongson_vbios_encoder));

	encoder_vbios[0]->next_encoder_offset = vbios->encoder_offset +
		sizeof(struct loongson_vbios_encoder);
	encoder_vbios[1]->next_encoder_offset = 0;

	encoder_vbios[0]->crtc_id = 0;
	encoder_vbios[1]->crtc_id = 1;

	encoder_vbios[0]->connector_id = 0;
	encoder_vbios[1]->connector_id = 1;

	encoder_vbios[0]->i2c_type = i2c_type_gpio;
	encoder_vbios[1]->i2c_type = i2c_type_gpio;

	return (void *)vbios;
}

int loongson_vbios_title_check(struct loongson_vbios *vbios){
	char *title = "Loongson-VBIOS";
	int i;

	i = 0;
	while (*title != '\0' && i <= 15) {
		if (vbios->title[i++] != *title) {
			DRM_ERROR("VBIOS title is wrong,use default setting!\n");
			return -EINVAL;
		}
		title++;
	}
	return 0;
}

int loongson_vbios_crc_check(void *vbios) {
	unsigned int crc;

	crc = lscrc32(0,(unsigned char *)vbios, VBIOS_SIZE - 0x4);
	if(*(unsigned int *)((unsigned char *)vbios + VBIOS_SIZE - 0x4) != crc){
		DRM_ERROR("VBIOS crc check is wrong,use default setting!\n");
		return -EINVAL;
	}
	return 0;
}

int loongson_vbios_init(struct loongson_device *ldev)
{
	struct loongson_vbios *vbios;
	int i;
	unsigned char * vbios_start;

	ldev->vbios = NULL;

	if (vgabios_addr) {
		if(loongson_vbios_crc_check((void *)vgabios_addr) ||
		loongson_vbios_title_check((struct loongson_vbios *)vgabios_addr)){
			DRM_ERROR("UEFI get wrong vbios!");
		} else {
			DRM_INFO("VBIOS get from UEFI check success!\n");
			ldev->vbios = (struct loongson_vbios *)vgabios_addr;
		}
	}
	else if (ls_spiflash_read_status() == 0xff) {
		DRM_INFO("There is no VBIOS flash chip,use default setting!\n");
		ldev->vbios = (struct loongson_vbios *)loongson_vbios_default();
	} else {
		DRM_INFO("Read VBIOS data from spi flash.\n");
		ldev->vbios = kzalloc(120*1024,GFP_KERNEL);
		ls_spiflash_read(VBIOS_START_ADDR,
				(unsigned char *)ldev->vbios, VBIOS_SIZE);

		/*Check VBIOS data.If data is wrong,use default setting*/
		if(loongson_vbios_crc_check((void *)ldev->vbios) ||
				loongson_vbios_title_check(ldev->vbios)) {
			kfree(ldev->vbios);
			ldev->vbios = (struct loongson_vbios *)loongson_vbios_default();
		} else {
			DRM_INFO("VBIOS get from SPI check success!\n");
		}
	}

	if (ldev->vbios->version_minor == 1 && ldev->vbios->version_major == 0 )
		ldev->vbios = (struct loongson_vbios *)loongson_vbios_default();

	vbios = ldev->vbios;
	vbios_start = (unsigned char *)vbios;

	if(vbios == NULL)
		return -1;

	/*get crtc struct points*/
	ldev->crtc_vbios[0] =
		(struct loongson_vbios_crtc *)(vbios_start + vbios->crtc_offset);
	if (vbios->crtc_num > 1) {
		for (i = 1; i < vbios->crtc_num; i++) {
		ldev->crtc_vbios[i] =
			(struct loongson_vbios_crtc *)
			(vbios_start + ldev->crtc_vbios[i - 1]->next_crtc_offset);
		}
	}

	/*get connector struct points*/
	ldev->connector_vbios [0] = (struct loongson_vbios_connector *)
		(vbios_start + vbios->connector_offset);

	if (vbios->connector_num > 1) {
		for(i = 1;i < vbios->connector_num; i++){
			ldev->connector_vbios[i] =
				(struct loongson_vbios_connector *)
				(vbios_start + ldev->connector_vbios[i - 1]->next_connector_offset);
		}
	}

	/*get encoder struct points*/
	ldev->encoder_vbios[0] = (struct loongson_vbios_encoder *)(vbios_start
			+ vbios->encoder_offset);
	if (vbios->encoder_num > 1) {
		ldev->encoder_vbios[1] = (struct loongson_vbios_encoder	*)
			(vbios_start + ldev->encoder_vbios[0]->next_encoder_offset);
	}
	loongson_vbios_information_display(ldev);
	return 0;
}

int loongson_vbios_information_display(struct loongson_device *ldev){

	int i;
	struct loongson_vbios_crtc  *crtc;
	struct loongson_vbios_encoder *encoder;
	struct loongson_vbios_connector *connector;
	char *config_method;

	char *encoder_methods[] = {
		"NONE",
		"OS",
		"BIOS"
	};

	char *edid_methods[] = {
		"No EDID",
		"Reading EDID via built-in I2C",
		"Use the VBIOS built-in EDID information",
		"Get EDID via encoder chip"
	};

	char *detect_methods[] = {
		"NONE",
		"POLL",
		"HPD"
	};

	DRM_INFO("Loongson Vbios: V%d.%d\n",
			ldev->vbios->version_major,ldev->vbios->version_minor);

	DRM_INFO("crtc:%d encoder:%d connector:%d\n",
			ldev->vbios->crtc_num,
			ldev->vbios->encoder_num,
			ldev->vbios->connector_num);

	for(i=0; i<ldev->vbios->crtc_num; i++){
		crtc = ldev->crtc_vbios[i];
		encoder = ldev->encoder_vbios[crtc->encoder_id];
		config_method = encoder_methods[encoder->config_type];
		connector = ldev->connector_vbios[encoder->connector_id];
		DRM_INFO("\tencoder%d(%s) i2c:%d \n", crtc->encoder_id, config_method, encoder->i2c_id);
		DRM_INFO("\tconnector%d:\n", encoder->connector_id);
		DRM_INFO("\t    %s", edid_methods[connector->edid_method]);
		DRM_INFO("\t    Detect:%s\n", detect_methods[connector->hot_swap_method]);
	}

	return 0;
}
