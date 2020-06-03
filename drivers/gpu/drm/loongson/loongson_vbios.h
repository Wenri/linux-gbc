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

#endif /* __LOONGSON_VBIOS_H__ */
