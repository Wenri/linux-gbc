#ifndef __LOONGSON_VBIOS_H__
#define __LOONGSON_VBIOS_H__

int loongson_vbios_init(struct loongson_device *ldev);
void* loongson_get_vbios(void);
bool is_legacy_vbios(struct loongson_vbios *vbios);

#endif /* __LOONGSON_VBIOS_H__ */
