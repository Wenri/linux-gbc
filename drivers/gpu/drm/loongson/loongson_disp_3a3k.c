#include <drm/drmP.h>
#include "loongson_drv.h"
#include "linux/i2c.h"


#define LS_CH9022_I2C_ID 6
#define LS_CH7034_I2C_ID 7

#define OFFSET0 0xc7
#define OFFSET1 0x1e
#define OFFSET2 0x1a


unsigned char ch7034_reg_config_table[][131][2] = {
      {
      /* IN 800x600,out 800x600,ch7034,bypassmode vga,mode_idx=1 */
          { 0x03, 0x04 },/* page 4 */
          { 0x52, 0xC3 },
          { 0x5A, 0x06 },
          { 0x5A, 0x04 },
          { 0x5A, 0x06 },
          { 0x52, 0xC1 },
          { 0x52, 0xC3 },
          { 0x5A, 0x04 },

          { 0x03, 0x00 },/* page 1 */
          { 0x07, 0xD9 },
          { 0x08, 0xF1 },
          { 0x09, 0x13 },
          { 0x0A, 0xBE },
          { 0x0B, 0x23 },
          { 0x0C, 0x20 },
          { 0x0D, 0x20 },
          { 0x0E, 0x00 },
          { 0x0F, 0x28 },
          { 0x10, 0x80 },
          { 0x11, 0x12 },
          { 0x12, 0x58 },
          { 0x13, 0x74 },
          { 0x14, 0x00 },
          { 0x15, 0x01 },
          { 0x16, 0x04 },
          { 0x17, 0x00 },
          { 0x18, 0x00 },
          { 0x19, 0xF8 },/* freq */
          { 0x1A, 0x9B },
          { 0x1B, 0x78 },
          { 0x1C, 0x69 },
          { 0x1D, 0x78 },
          { 0x1E, 0x00 },/* output is  progressive */
          { 0x1F, 0x23 },
          { 0x20, 0x20 },
          { 0x21, 0x20 },
          { 0x22, 0x00 },
          { 0x23, 0x10 },
          { 0x24, 0x60 },
          { 0x25, 0x12 },
          { 0x26, 0x58 },
          { 0x27, 0x74 },
          { 0x28, 0x00 },
          { 0x29, 0x0A },
          { 0x2A, 0x02 },
          { 0x2B, 0x09 },/* vga output format:bypass mode */
          { 0x2C, 0x00 },
          { 0x2D, 0x00 },
          { 0x2E, 0x3D },
          { 0x2F, 0x00 },
          { 0x32, 0xC0 },
          { 0x36, 0x40 },
          { 0x38, 0x47 },
          { 0x3D, 0x86 },
          { 0x3E, 0x00 },
          { 0x40, 0x0E },
          { 0x4B, 0x40 },/* pwm control */
          { 0x4C, 0x40 },/* lvds output channel order register */
          { 0x4D, 0x80 },
          { 0x54, 0x80 },
          { 0x55, 0x28 },
          { 0x56, 0x80 },
          { 0x57, 0x00 },
          { 0x58, 0x01 },
          { 0x59, 0x04 },
          { 0x5A, 0x02 },
          { 0x5B, 0xF2 },
          { 0x5C, 0xB9 },
          { 0x5D, 0xD6 },
          { 0x5E, 0x54 },
          { 0x60, 0x00 },
          { 0x61, 0x00 },
          { 0x64, 0x2D },
          { 0x68, 0x44 },
          { 0x6A, 0x40 },
          { 0x6B, 0x00 },
          { 0x6C, 0x10 },
          { 0x6D, 0x00 },
          { 0x6E, 0xA0 },
          { 0x70, 0x98 },
          { 0x74, 0x30 },/* scaling control */
          { 0x75, 0x80 },/* scaling control */
          { 0x7E, 0x0F },/* de and pwm control */
          { 0x7F, 0x00 },/* test pattern */

          { 0x03, 0x01 },/* page 2 */
          { 0x08, 0x05 },
          { 0x09, 0x04 },/* diffen register */
          { 0x0B, 0x65 },
          { 0x0C, 0x4A },
          { 0x0D, 0x29 },
          { 0x0F, 0x9C },
          { 0x12, 0xD4 },
          { 0x13, 0x28 },
          { 0x14, 0x83 },
          { 0x15, 0x00 },
          { 0x16, 0x00 },
          { 0x1A, 0x6C },/* DAC termination control register */
          { 0x1B, 0x00 },
          { 0x1C, 0x00 },
          { 0x1D, 0x00 },
          { 0x23, 0x63 },
          { 0x24, 0xB4 },
          { 0x28, 0x54 },
          { 0x29, 0x60 },
          { 0x41, 0x60 },
          { 0x63, 0x2D },/* DE polarity */
          { 0x6B, 0x11 },
          { 0x6C, 0x06 },

          { 0x03, 0x03 },/* page3 */
          { 0x26, 0x00 },
          { 0x28, 0x08 },/* output control:DAC output is VGA */
          { 0x2A, 0x00 },/* output control:HDTV output through scaler */

          { 0x03, 0x04 },/* page 4 */
          { 0x10, 0x00 },
          { 0x11, 0x9B },
          { 0x12, 0x78 },
          { 0x13, 0x02 },
          { 0x14, 0x88 },
          { 0x15, 0x70 },
          { 0x20, 0x00 },
          { 0x21, 0x00 },
          { 0x22, 0x00 },
          { 0x23, 0x00 },
          { 0x24, 0x00 },
          { 0x25, 0x00 },
          { 0x26, 0x00 },
          { 0x54, 0xC4 },
          { 0x55, 0x5B },
          { 0x56, 0x4D },
          { 0x60, 0x01 },
          { 0x61, 0x62 },
      },
};

static void loongson_encoder_reset_ch9022(struct i2c_adapter *adapter)
{
	unsigned char rbuf;
	unsigned char offset;
	unsigned char *wbuf = kmalloc(2*sizeof(unsigned char), GFP_KERNEL);
	struct i2c_msg msgsw = {
		.addr =0x39,
		.flags = 0,
		.len = 2,
		.buf = wbuf,
	};
	struct i2c_msg msgsr[] = {
		{
			.addr = 0x39,
			.flags = 0,
			.len = 1,
			.buf = &offset,
		},{
			.addr = 0x39,
			.flags = 1,
			.len = 1,
			.buf = &rbuf,
		}
	};

	/*write 0xc7 0x00*/
	msgsw.buf[0] = OFFSET0;
	msgsw.buf[1] = 0x00;
	if (i2c_transfer(adapter, &msgsw, 1) != 1) {
		DRM_INFO("write 0xc7 err\n");
		goto wfree;
	}

        /*read 0x1b*/
	offset =0x1b;
	msgsr[0].buf = &offset;
	if (i2c_transfer(adapter, msgsr, 2) != 2) {
		DRM_INFO("read 0x1b error\n");
		goto wfree;
	}

	/*read 0x1c*/
	offset =0x1c;
	msgsr[0].buf = &offset;
	if (i2c_transfer(adapter, msgsr, 2) != 2) {
		DRM_INFO("read 0x1c error\n");
		goto wfree;
	}

	/*read 0x1d*/
	offset =0x1d;
	msgsr[0].buf = &offset;
	if (i2c_transfer(adapter, msgsr, 2) != 2) {
		DRM_INFO("read 0x1d error\n");
		goto wfree;
	}

	/* read 0x1e*/
	offset =OFFSET1;
	msgsr[0].buf = &offset;
	if (i2c_transfer(adapter, msgsr, 2) != 2) {
		DRM_INFO("read 0x1e error\n");
		goto wfree;
	}

	DRM_DEBUG("=====read 0x1e %#x\n",rbuf);
	/* write 0x1e*/
	rbuf &= ~(0x3);
	DRM_DEBUG("=====write 0x1e %#x\n",rbuf);
	msgsw.buf[0] = OFFSET1;
	msgsw.buf[1] = rbuf;
	if (i2c_transfer(adapter, &msgsw, 1) != 1) {
		DRM_INFO("write 0x1e err\n");
		goto wfree;
	}

	/* read 0x1a */
	offset = OFFSET2;
	msgsr[0].buf = &offset;
	if (i2c_transfer(adapter, msgsr, 2) != 2) {
		DRM_INFO("read 0x1a err\n");
		goto wfree;
	}

	/* write 0x1a */
	rbuf &= ~(1 << 4);
	msgsw.buf[0] = OFFSET2;
	msgsw.buf[1] = rbuf;
	if (i2c_transfer(adapter, &msgsw, 1) != 1) {
		DRM_INFO("write 0x1a err\n");
		goto wfree;
	}
	DRM_DEBUG("reset 9022 success\n");

wfree:
	kfree(wbuf);
}


static void loongson_encoder_reset_ch7034(struct i2c_adapter *adapter)
{
	int i;
	unsigned char *buf = kmalloc(2*sizeof(unsigned char), GFP_KERNEL);
	struct i2c_msg msgs = {
		.addr = 0x75,
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
		msgs.buf[0] = ch7034_reg_config_table[0][i][0];
		msgs.buf[1] = ch7034_reg_config_table[0][i][1];
		if (i2c_transfer(adapter, &msgs, 1) != 1)
			printk("i %d buf 0x%x 0x%x\n", i, msgs.buf[0], msgs.buf[1]);
	}
	kfree(buf);
	return;
}

bool loongson_encoder_reset_3a3k(struct loongson_encoder *ls_encoder, struct drm_display_mode *mode)
{
	struct loongson_vbios_encoder  *lsvbios_encoder;
	struct i2c_adapter *adapter;
	lsvbios_encoder = ls_encoder->vbios_encoder;

	if (!ls_encoder->i2c) {
		DRM_ERROR("Loongson encoder modeset no i2c\n");
		return false;
	}

	adapter = ls_encoder->i2c->adapter;
	if (!adapter) {
		DRM_ERROR("encoder resume adapter null\n");
		return false;
	}

	switch (lsvbios_encoder->i2c_id) {
	case LS_CH9022_I2C_ID:
		if (adapter)
			loongson_encoder_reset_ch9022(adapter);
		break;
	case LS_CH7034_I2C_ID:
		if (adapter)
			loongson_encoder_reset_ch7034(adapter);
		break;
	default:
		DRM_INFO("Loongson resume 3A3K failed, try update vbios version\n");
		return false;
	}

	return true;
}
