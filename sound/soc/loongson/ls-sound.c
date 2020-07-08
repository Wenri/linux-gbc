/*
 * ls-sound.c  --  SoC audio for loongson sound card.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/i2c.h>

#include "ls-pcm.h"

static struct snd_soc_card snd_soc_z2;

static int edb93xx_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return 0;
}

static struct snd_soc_ops edb93xx_ops = {
	.hw_params	= edb93xx_hw_params,
};

static struct snd_soc_dai_link z2_dai = {
#ifdef CONFIG_SND_SOC_UDA1342
	.name		= "uda1342",
	.stream_name	= "UDA1342 Duplex",
	.codec_dai_name	= "uda1342-hifi",
	.codec_name	= "uda1342-codec.1-001a",
#else
	.name		= "dummy",
	.stream_name	= "dummy Duplex",
	.codec_dai_name	= "snd-soc-dummy-dai",
	.codec_name	= "snd-soc-dummy",

#endif
	.cpu_dai_name	= "ls-i2s",
	.ops = &edb93xx_ops,
	.platform_name = "ls-pcm-audio",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_z2 = {
	.name		= "ls",
	.owner		= THIS_MODULE,
	.dai_link	= &z2_dai,
	.num_links	= 1,
};

static struct platform_device *ls_snd_ac97_device;

#ifndef CONFIG_SND_SOC_UDA1342
static int i2c_write_codec(struct i2c_adapter *adapter, unsigned char reg, unsigned char *val)
{
	unsigned char msg[3] = {reg, *val, *(val + 1)};
	struct i2c_msg msgs[] = {
	    {
		.addr   = 0x1a,
		.flags  = 0,
		.len    = 3,
		.buf    = &msg[0],
	    }
	};

	if (i2c_transfer(adapter, msgs, 1) == 1) {
		return 0;
	}
	return -1;
}
static int codec_i2c_init(void)
{
	struct i2c_adapter *adapter;
	unsigned char set_reg[]={
	/******  reg   bit_hi bit_lo  ****/
	   0x00,   0x14,   0x02,
	   0x01,   0x00,   0x14,
	   0x10,   0xff,   0x03,
	   0x11,   0x30,   0x30,
	   0x12,   0xc4,   0x00,
	   0x20,   0x00,   0x30,
	   0x21,   0x00,   0x30,
	};
	int j;
	unsigned char  val[2] = {0x94,0x02};

	adapter = i2c_get_adapter(1);

	i2c_write_codec(adapter, 0x0, val);
		udelay(15000);

	for(j = 0; j < ARRAY_SIZE(set_reg) / 3; j++)
		i2c_write_codec(adapter, set_reg[j * 3], &set_reg[3 * j + 1]);

	return 0;
}
#endif

static int ls_sound_drv_probe(struct platform_device *pdev)
{
	int ret;
	ls_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!ls_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(ls_snd_ac97_device,
				&snd_soc_z2);
	snd_soc_z2.dev = &ls_snd_ac97_device->dev;
#ifndef CONFIG_SND_SOC_UDA1342
	codec_i2c_init();
#endif
	ret = platform_device_add(ls_snd_ac97_device);

	if (ret)
		platform_device_put(ls_snd_ac97_device);

	pr_debug("Exited %s\n", __func__);

	return 0;
}

static int ls_sound_drv_remove(struct platform_device *pdev)
{
	pr_debug("Entered %s\n", __func__);
	platform_device_unregister(ls_snd_ac97_device);
	pr_debug("Exited %s\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id snd_ls_sound_dt_match[] = {
	{ .compatible = "loongson,ls-sound", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_ls_sound_dt_match);
#endif

static struct platform_driver ls_sound_driver = {
	.probe = ls_sound_drv_probe,
	.remove = ls_sound_drv_remove,
	.driver = {
		.name = "ls-sound",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(snd_ls_sound_dt_match),
#endif
	},
};

static int __init ls_sound_init(void)
{
	int ret;

	pr_debug("Entered %s\n", __func__);

	ret = platform_driver_register(&ls_sound_driver);
	if(ret){
		printk(KERN_ALERT"ERROR: register device!\n");
		return -EINVAL;
	}

	return ret;
}

static void __exit ls_sound_exit(void)
{
	platform_driver_unregister(&ls_sound_driver);
}

module_init(ls_sound_init);
module_exit(ls_sound_exit);

MODULE_AUTHOR("mashuai");
MODULE_DESCRIPTION("ALSA SoC LOONGSON");
MODULE_LICENSE("GPL");
