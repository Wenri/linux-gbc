#ifndef __LOONGSON_VBIOS_H__
#define __LOONGSON_VBIOS_H__

#define VBIOS_PWM_ID           0x0
#define VBIOS_PWM_PERIOD       0x1
#define VBIOS_PWM_POLARITY     0x2

int loongson_vbios_init(struct loongson_device *ldev);
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
