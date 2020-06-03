#include "loongson_drv.h"
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

