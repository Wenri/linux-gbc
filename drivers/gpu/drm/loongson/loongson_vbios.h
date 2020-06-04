#ifndef __LOONGSON_VBIOS_H__
#define __LOONGSON_VBIOS_H__

#define VBIOS_PWM_ID           0x0
#define VBIOS_PWM_PERIOD       0x1
#define VBIOS_PWM_POLARITY     0x2

struct desc_node;
struct vbios_cmd;
typedef bool (parse_func)(struct desc_node *, struct vbios_cmd *);

enum desc_type {
	desc_header = 0,
	desc_crtc,
	desc_encoder,
	desc_connector,
	desc_i2c,
	desc_pwm,
	desc_gpio,
	desc_backlight,
	desc_fan,
	desc_irq_vblank,
	desc_cfg_encoder,
	desc_max = 0xffff
};

enum vbios_backlight_type {
	bl_unuse,
	bl_ec,
	bl_pwm
};

enum desc_ver {
	ver_v1,
};

struct vbios_header {
	u32 feature;
	u8  oem_vendor[32];
	u8  oem_product[32];
	u32 legacy_offset;
	u32 legacy_size;
	u32 desc_offset;
	u32 desc_size;
	u32 data_offset;
	u32 data_size;
} __attribute__((packed));

struct vbios_backlight {
	u32 feature;
	u8  index;
	u8  used;
	enum vbios_backlight_type  type;
} __attribute__((packed));

struct vbios_pwm {
	u32 feature;
	u8  index;
	u8  pwm;
	u8  polarity;
	u32 peroid;
} __attribute__((packed));

struct vbios_desc {
	u16 type;
	u8  ver;
	u8  index;
	u32 offset;
	u32 size;
	u64 ext[2];
} __attribute__((packed));


struct vbios_cmd {
	u8    index;
	enum desc_type type;
	void *req;
	void *res;
};

#define FUNC(t, v, f) \
{ \
	.type = t, .ver = v, .func = f,\
}

struct desc_func {
	enum desc_type type;
	u16 ver;
	s8 *name;
	u8 index;
	parse_func *func;
};

struct desc_node {
	struct list_head head;
	u8 *data;
	struct vbios_desc *desc;
	parse_func *parse;
};

int loongson_vbios_init(struct loongson_device *ldev);
void loongson_vbios_exit(struct loongson_device *ldev);
void* loongson_get_vbios(void);
bool is_legacy_vbios(struct loongson_vbios *vbios);
u32 get_connector_type(struct loongson_device *ldev, u32 index);
u16 get_connector_i2cid(struct loongson_device *ldev, u32 index);
u16 get_hotplug_mode(struct loongson_device *ldev, u32 index);
u8* get_vbios_edid(struct loongson_device *ldev, u32 index);
u16 get_edid_method(struct loongson_device *ldev, u32 index);
u32 get_vbios_pwm(struct loongson_device *ldev, u32 index, u16 request);

u32 get_crtc_id(struct loongson_device *ldev, u32 index);
u32 get_crtc_max_freq(struct loongson_device *ldev, u32 index);
u32 get_crtc_max_width(struct loongson_device *ldev, u32 index);
u32 get_crtc_max_height(struct loongson_device *ldev, u32 index);
u32 get_crtc_encoder_id(struct loongson_device *ldev, u32 index);
bool get_crtc_is_vb_timing(struct loongson_device *ldev, u32 index);

struct crtc_timing *
get_crtc_timing(struct loongson_device *ldev, u32 index);

u32 get_encoder_connector_id(struct loongson_device *ldev, u32 index);
u32 get_encoder_i2c_id(struct loongson_device *ldev, u32 index);
enum encoder_config
get_encoder_config_type(struct loongson_device *ldev, u32 index);
enum encoder_type
get_encoder_type(struct loongson_device *ldev, u32 index);
struct encoder_config_param*
get_encoder_config(struct loongson_device *ldev, u32 index);

#endif /* __LOONGSON_VBIOS_H__ */
