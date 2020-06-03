#include "loongson_drv.h"
#include "loongson_legacy_vbios.h"
#include "loongson_vbios.h"

#define VBIOS_START 0x1000
#define VBIOS_SIZE 0x40000

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

static void* get_vbios_from_bios(void)
{
	void *vbios = NULL;

	if (!vgabios_addr)
		return vbios;

	vbios = kzalloc(256*1024, GFP_KERNEL);
	if (!vbios)
		return vbios;

	memcpy(vbios, (void *)vgabios_addr, VBIOS_SIZE);

	DRM_INFO("Get vbios from bios Success.\n");
	return vbios;
}

static void* get_vbios_from_flash(void)
{
	void *vbios = NULL;
	int ret = -1;

	ret = ls_spiflash_read_status();
	if (ret == 0xff)
		return vbios;

	vbios = kzalloc(256*1024, GFP_KERNEL);
	if (!vbios)
		return vbios;

	ret = ls_spiflash_read(VBIOS_START, (u8 *)vbios, VBIOS_SIZE);
	if (ret) {
		kfree(vbios);
		vbios = NULL;
	}

	DRM_INFO("Get vbios from flash Success.\n");
	return vbios;
}

static struct loongson_vbios_connector* get_connector_legacy(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios *vbios = ldev->vbios;
	struct loongson_vbios_connector *connector = NULL;
	u8 *start;
	u32 offset;

	start = (u8 *)vbios;
	offset = vbios->connector_offset;
	connector = (struct loongson_vbios_connector *)(start + offset);
	if (index == 1) {
		offset = connector->next;
		connector = (struct loongson_vbios_connector *)(start + offset);
	}

	return connector;
}

u32 get_connector_type(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	u32 type = -1;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		type = connector->type;
	}

	return type;
}

u16 get_connector_i2cid(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	u16 i2c_id = -1;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		i2c_id = connector->i2c_id;
	}

	return i2c_id;
}

u16 get_hotplug_mode(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	u16 mode = -1;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		mode = connector->hotplug;
	}

	return mode;
}

u16 get_edid_method(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	u16 method = -1;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		method = connector->edid_method;
	}

	return method;
}

u8* get_vbios_edid(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector;
	u8* edid = NULL;

	edid = kzalloc(sizeof(u8) * EDID_LENGTH * 2, GFP_KERNEL);
	if (!edid)
		return edid;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		memcpy(edid, connector->internal_edid, EDID_LENGTH * 2);
	}

	return edid;
}

u32 get_vbios_pwm(struct loongson_device *ldev, u32 index, u16 request)
{
	struct loongson_vbios_connector *connector;
	u32 value = -1;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		switch (request) {
		case VBIOS_PWM_ID:
			value = connector->bl_pwm.pwm_id;
			break;
		case VBIOS_PWM_PERIOD:
			value = connector->bl_pwm.period_ns;
			break;
		case VBIOS_PWM_POLARITY:
			value = connector->bl_pwm.polarity;
		}
	}

	return value;
}

void* loongson_get_vbios(void)
{
	void* vbios = NULL;
	enum loongson_gpu gpu;

	gpu = loongson_find_gpu();
	vbios = get_vbios_from_bios();
	if (vbios == NULL)
		vbios = get_vbios_from_flash();
	if (vbios == NULL && gpu == LS2K_GPU)
		vbios = (struct loongson_vbios *)loongson_vbios_default_legacy();

	return vbios;
}

int loongson_vbios_init(struct loongson_device *ldev)
{
	/* TODO */
	return 0;
}

