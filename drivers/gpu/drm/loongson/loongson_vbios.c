#include "loongson_drv.h"
#include "loongson_legacy_vbios.h"
#include "loongson_vbios.h"

#define VBIOS_START 0x1000
#define VBIOS_SIZE 0x40000
#define VBIOS_DESC_OFFSET 0x6000

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

static struct loongson_vbios_encoder*
get_encoder_legacy(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios *vbios = (struct loongson_vbios*)ldev->vbios;
	struct loongson_vbios_encoder *encoder = NULL;
	u8 *start;
	u32 offset;

	start = (u8 *)vbios;
	offset = vbios->encoder_offset;
	encoder = (struct loongson_vbios_encoder *)(start + offset);
	if (index == 1) {
		offset =  encoder->next;
		encoder = (struct loongson_vbios_encoder *)(start + offset);
	}

	return encoder;
}

static struct loongson_vbios_crtc*
get_crtc_legacy(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios *vbios = ldev->vbios;
	struct loongson_vbios_crtc *crtc = NULL;
	u8 *start;
	u32 offset;

	start = (u8 *)vbios;
	offset = vbios->crtc_offset;
	crtc = (struct loongson_vbios_crtc *)(start + offset);
	if (index == 1) {
		offset = crtc->next;
		crtc = (struct loongson_vbios_crtc *)(start + offset);
	}

	return crtc;
}

static struct loongson_vbios_connector*
get_connector_legacy(struct loongson_device *ldev, u32 index)
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

static struct crtc_timing *
get_crtc_timing_legacy(struct loongson_device *ldev, u32 index)
{
	struct loongson_crtc_config_param *vbios_tables;
	struct loongson_crtc_modeparameter *param;
	struct loongson_vbios_crtc *vbios_crtc;
	struct loongson_timing *tables;
	struct crtc_timing *timing;
	s32 i = 0;
	u32 tables_size = (sizeof(struct loongson_timing)*LS_MAX_RESOLUTIONS);

	vbios_crtc = get_crtc_legacy(ldev,index);

	timing =
		(struct crtc_timing *)kzalloc(sizeof(struct crtc_timing), GFP_KERNEL);
	if (!timing)
		return NULL;

	tables= kzalloc(tables_size, GFP_KERNEL);
	if (!tables) {
		kfree(timing);
		return NULL;
	}

	timing->tables = tables;

	vbios_tables = vbios_crtc->mode_config_tables;

	while(vbios_tables->resolution.used){
		param = &vbios_tables->crtc_resol_param;

		tables->htotal = param->htotal;
		tables->hdisplay = param->hdisplay;
		tables->hsync_start = param->hsync_start;
		tables->hsync_width = param->hsync_width;
		tables->hsync_pll = param->hsync_pll;
		tables->vtotal = param->vtotal;
		tables->vdisplay = param->vdisplay;
		tables->vsync_start = param->vsync_start;
		tables->vsync_width = param->vsync_width;
		tables->vsync_pll = param->vsync_pll;
		tables->clock = param->clock;
		tables->hfreq = param->hfreq;
		tables->vfreq = param->vfreq;
		tables->clock_phase = param->clock_phase;

		i++;
		tables++;
		vbios_tables++;
	}
	timing->num = i;

	return timing;
}

static bool
parse_vbios_crtc(struct desc_node *this, struct vbios_cmd *cmd)
{
	bool ret = true;
	u64 request = (u64)cmd->req;
	u32 *val = (u32 *)cmd->res;
	struct vbios_crtc *crtc = (struct vbios_crtc *)this->data;

	switch(request) {
	case VBIOS_CRTC_ID:
		*val = crtc->crtc_id;
		break;
	case VBIOS_CRTC_ENCODER_ID:
		*val = crtc->encoder_id;
		break;
	case VBIOS_CRTC_MAX_FREQ:
		*val = crtc->max_freq;
		break;
	case VBIOS_CRTC_MAX_WIDTH:
		*val = crtc->max_width;
		break;
	case VBIOS_CRTC_MAX_HEIGHT:
		*val = crtc->max_height;
		break;
	case VBIOS_CRTC_IS_VB_TIMING:
		*val = crtc->is_vb_timing;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool
parse_vbios_connector(struct desc_node *this, struct vbios_cmd *cmd)
{
	bool ret = true;
	u64 request = (u64)cmd->req;
	u32 *val = (u32 *)cmd->res;
	struct vbios_connector *connector = (struct vbios_connector *)this->data;

	switch(request) {
	case VBIOS_CONNECTOR_I2C_ID:
		*val = connector->i2c_id;
		break;
	case VBIOS_CONNECTOR_INTERNAL_EDID:
		memcpy(val, connector->internal_edid, EDID_LENGTH * 2);
		break;
	case VBIOS_CONNECTOR_TYPE:
		*val = connector->type;
		break;
	case VBIOS_CONNECTOR_HOTPLUG:
		*val = connector->hotplug;
		break;
	case VBIOS_CONNECTOR_EDID_METHOD:
		*val = connector->edid_method;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool
parse_vbios_encoder(struct desc_node *this, struct vbios_cmd *cmd)
{
	bool ret = true;
	u64 request = (u64)cmd->req;
	u32 *val = (u32 *)cmd->res;
	struct vbios_encoder *encoder = (struct vbios_encoder *)this->data;

	switch (request) {
	case VBIOS_ENCODER_I2C_ID:
		*val = encoder->i2c_id;
		break;
	case VBIOS_ENCODER_CONNECTOR_ID:
		*val = encoder->connector_id;
		break;
	case VBIOS_ENCODER_TYPE:
		*val = encoder->type;
		break;
	case VBIOS_ENCODER_CONFIG_TYPE:
		*val = encoder->config_type;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool
parse_vbios_cfg_encoder(struct desc_node *this, struct vbios_cmd *cmd)
{
	bool ret = true;
	u64 request = (u64)cmd->req;
	u32 *val = (u32 *)cmd->res;
	struct cfg_encoder *cfg_encoder;
	struct cfg_encoder *cfg;
	struct vbios_cfg_encoder *vbios_cfg_encoder;
	u32 num, size, i = 0;

	vbios_cfg_encoder = (struct vbios_cfg_encoder *)this->data;
	size = sizeof(struct vbios_cfg_encoder);
	num = this->desc->size/size;

	switch (request) {
	case VBIOS_ENCODER_CONFIG_PARAM:
		cfg_encoder =
			(struct cfg_encoder *)kzalloc(sizeof(struct cfg_encoder) * num, GFP_KERNEL);
		cfg = cfg_encoder;
		for (i = 0; i < num; i++) {
			cfg->reg_num = vbios_cfg_encoder->reg_num;
			cfg->hdisplay = vbios_cfg_encoder->hdisplay;
			cfg->vdisplay = vbios_cfg_encoder->vdisplay;
			memcpy(&cfg->config_regs,
					&vbios_cfg_encoder->config_regs,
					sizeof(struct vbios_conf_reg) * 256);

			cfg++;
			vbios_cfg_encoder++;
		}
		cmd->res = (void *)cfg_encoder;
		break;
	case VBIOS_ENCODER_CONFIG_NUM:
		*val = num;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool
parse_vbios_backlight(struct desc_node *this, struct vbios_cmd *cmd)
{

	return 0;
}

static bool
parse_vbios_pwm(struct desc_node *this, struct vbios_cmd *cmd)
{
	bool ret = true;
	u16 request = (u64)cmd->req;
	u32 *val = (u32 *)cmd->res;
	struct vbios_pwm *pwm = (struct vbios_pwm *)this->data;

	switch(request) {
	case VBIOS_PWM_ID:
		*val = pwm->pwm;
		break;
	case VBIOS_PWM_PERIOD:
		*val = pwm->peroid;
		break;
	case VBIOS_PWM_POLARITY:
		*val = pwm->polarity;
		break;
	default:
		ret  = false;
		break;
	}

	return ret;
}

static bool
parse_vbios_header(struct desc_node *this, struct vbios_cmd *cmd)
{

	return true;
}

static bool
parse_vbios_default(struct desc_node *this, struct vbios_cmd *cmd)
{
	struct vbios_desc *vb_desc ;

	vb_desc = this->desc;
	DRM_WARN("Current descriptor[T-%d][V-%d] cannot be interprete.\n",
			vb_desc->type, vb_desc->ver);
	return false;
}

#define FUNC(t, v, f) \
{ \
	.type = t, .ver = v, .func = f,\
}

static struct desc_func tables[] = {
	FUNC(desc_backlight, ver_v1, parse_vbios_backlight),
	FUNC(desc_pwm, ver_v1, parse_vbios_pwm),
	FUNC(desc_header, ver_v1, parse_vbios_header),
	FUNC(desc_crtc, ver_v1, parse_vbios_crtc),
	FUNC(desc_connector, ver_v1, parse_vbios_connector),
	FUNC(desc_encoder, ver_v1, parse_vbios_encoder),
	FUNC(desc_cfg_encoder, ver_v1, parse_vbios_cfg_encoder),
};


static inline parse_func *get_parse_func(struct vbios_desc *vb_desc)
{
	int i;
	u32 type = vb_desc->type;
	u32 ver  = vb_desc->ver;
	parse_func  *func = parse_vbios_default;
	u32 tt_num = ARRAY_SIZE(tables);

	for (i = 0; i < tt_num; i++) {
		if ((tables[i].ver == ver) && (tables[i].type == type)) {
			func = tables[i].func;
			break;
		}
	}

	return func;
}

static inline void free_desc_list(struct loongson_device *ldev)
{
	struct desc_node *node, *tmp;

	list_for_each_entry_safe(node, tmp,&ldev->desc_list, head) {
		list_del(&node->head);
		kfree(node);
	}
}

static inline u32
insert_desc_list(struct loongson_device *ldev, struct vbios_desc *vb_desc)
{
	struct desc_node *node;
	parse_func  *func = NULL;

	WARN_ON(!ldev || !vb_desc);
	node = (struct desc_node *)kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	func = get_parse_func(vb_desc);
	node->parse = func;
	node->desc  = (void *)vb_desc;
	node->data  = ((u8 *)ldev->vbios + vb_desc->offset);
	list_add_tail(&node->head, &ldev->desc_list);

	return 0;
}

static u32 parse_vbios_desc(struct loongson_device *ldev)
{
	u32 ret = 0;
	struct vbios_desc *desc;
	enum desc_type type = 0;
	u8 *vbios = (u8 *)ldev->vbios;

	WARN_ON(!vbios);

	desc = (struct vbios_desc *)(vbios + VBIOS_DESC_OFFSET);
	while (1) {
		type = desc->type;
		if (type == desc_max)
			break;

		ret = insert_desc_list(ldev, desc);
		if (ret)
			DRM_DEBUG_KMS("Parse T-%d V-%d failed[%d]\n",
					desc->ver, desc->type, ret);

		desc++;
	}

	return ret;
}

static inline struct desc_node *
get_desc_node(struct loongson_device *ldev, u16 type, u8 index)
{
	struct desc_node *node, *tmp;
	struct vbios_desc *vb_desc;

	list_for_each_entry_safe(node, tmp, &ldev->desc_list, head) {
		vb_desc = node->desc;
		if (vb_desc->type == type && vb_desc->index == index)
			break;
	}

	return node;
}

static bool
vbios_get_data(struct loongson_device *ldev, struct vbios_cmd *cmd)
{
	bool ret = false;
	struct desc_node *node;

	WARN_ON(!cmd);

	node = get_desc_node(ldev, cmd->type, cmd->index);
	if (node && node->parse) {
		ret = node->parse(node, cmd);
	}
	return ret;
}

u32 get_connector_type(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	struct vbios_cmd vbt_cmd;
	u32 type = -1;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		type = connector->type;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_connector;
		vbt_cmd.req = (void *)(ulong)VBIOS_CONNECTOR_TYPE;
		vbt_cmd.res = (void *)(ulong)&type;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			type = -1;
		}
	}

	return type;
}

u16 get_connector_i2cid(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	struct vbios_cmd vbt_cmd;
	u16 i2c_id = -1;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		i2c_id = connector->i2c_id;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_connector;
		vbt_cmd.req = (void *)(ulong)VBIOS_CONNECTOR_I2C_ID;
		vbt_cmd.res = (void *)(ulong)&i2c_id;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			i2c_id = -1;
		}
	}

	return i2c_id;
}

u16 get_hotplug_mode(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	struct vbios_cmd vbt_cmd;
	u16 mode = -1;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		mode = connector->hotplug;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_connector;
		vbt_cmd.req = (void *)(ulong)VBIOS_CONNECTOR_HOTPLUG;
		vbt_cmd.res = (void *)(ulong)&mode;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			mode = -1;
		}
	}

	return mode;
}

u16 get_edid_method(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector = NULL;
	struct vbios_cmd vbt_cmd;
	u16 method = -1;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		method = connector->edid_method;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_connector;
		vbt_cmd.req = (void *)(ulong)VBIOS_CONNECTOR_EDID_METHOD;
		vbt_cmd.res = (void *)(ulong)&method;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			method = -1;
		}
	}

	return method;
}

u8* get_vbios_edid(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_connector *connector;
	struct vbios_cmd vbt_cmd;
	u8* edid = NULL;
	bool ret = false;

	edid = kzalloc(sizeof(u8) * EDID_LENGTH * 2, GFP_KERNEL);
	if (!edid)
		return edid;

	if (is_legacy_vbios(ldev->vbios)) {
		connector = get_connector_legacy(ldev, index);
		memcpy(edid, connector->internal_edid, EDID_LENGTH * 2);
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_connector;
		vbt_cmd.req = (void *)(ulong)VBIOS_CONNECTOR_INTERNAL_EDID;
		vbt_cmd.res = (void *)(ulong)&edid;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			return NULL;
		}
	}

	return edid;
}

u32 get_vbios_pwm(struct loongson_device *ldev, u32 index, u16 request)
{
	bool ret = false;
	struct loongson_vbios_connector *connector;
	struct vbios_cmd vbt_cmd;
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
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type  = desc_pwm;
		vbt_cmd.req   = (void *)(ulong)request;
		vbt_cmd.res   = (void *)(ulong)&value;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			/*TODO
			 * add debug mesg
			 * */
			value = 0xffffffff;
		}
	}

	return value;
}

u32 get_crtc_id(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	struct vbios_cmd vbt_cmd;
	u32 crtc_id = 0;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		crtc_id = crtc->crtc_id;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_ID;
		vbt_cmd.res = (void *)(ulong)&crtc_id;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			crtc_id = 0;
		}
	}

	return crtc_id;
}

u32 get_crtc_max_freq(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	struct vbios_cmd vbt_cmd;
	u32 max_freq = 0;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		max_freq = crtc->crtc_max_freq;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_MAX_FREQ;
		vbt_cmd.res = (void *)(ulong)&max_freq;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			max_freq = 0;
		}
	}

	return max_freq;
}

u32 get_crtc_max_width(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	struct vbios_cmd vbt_cmd;
	u32 max_width = 0;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		max_width = crtc->crtc_max_weight;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_MAX_WIDTH;
		vbt_cmd.res = (void *)(ulong)&max_width;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			max_width = 0;
		}
	}

	return max_width;
}

u32 get_crtc_max_height(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	struct vbios_cmd vbt_cmd;
	u32 max_height = 0;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		max_height = crtc->crtc_max_height;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_MAX_HEIGHT;
		vbt_cmd.res = (void *)(ulong)&max_height;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			max_height = 0;
		}
	}

	return max_height;
}

u32 get_crtc_encoder_id(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	u32 encoder_id = 0;
	struct vbios_cmd vbt_cmd;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		encoder_id = crtc->encoder_id;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_ENCODER_ID;
		vbt_cmd.res = (void *)(ulong)&encoder_id;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			encoder_id = 0;
		}
	}

	return encoder_id;
}

bool get_crtc_is_vb_timing(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_crtc *crtc;
	struct vbios_cmd vbt_cmd;
	bool vb_timing = false;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		crtc = get_crtc_legacy(ldev, index);
		vb_timing = crtc->use_local_param;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_crtc;
		vbt_cmd.req = (void *)(ulong)VBIOS_CRTC_IS_VB_TIMING;
		vbt_cmd.res = (void *)(ulong)&vb_timing;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			vb_timing = false;
		}
	}

	return vb_timing;
}

struct crtc_timing *get_crtc_timing(struct loongson_device *ldev,
		u32 index)
{
	if (is_legacy_vbios(ldev->vbios))
		return get_crtc_timing_legacy(ldev, index);

	return NULL;
}

u32 get_encoder_connector_id(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_encoder *encoder = NULL;
	u32 connector_id = 0;
	struct vbios_cmd vbt_cmd;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		encoder = get_encoder_legacy(ldev, index);
		connector_id = encoder->connector_id;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_CONNECTOR_ID;
		vbt_cmd.res = (void *)(ulong)&connector_id;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			connector_id = 0;
		}
	}

	return connector_id;
}

u32 get_encoder_i2c_id(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_encoder *encoder = NULL;
	u32 i2c_id = 0;
	struct vbios_cmd vbt_cmd;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		encoder = get_encoder_legacy(ldev, index);
		i2c_id = encoder->i2c_id;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_I2C_ID;
		vbt_cmd.res = (void *)(ulong)&i2c_id;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			i2c_id = 0;
		}
	}

	return i2c_id;
}

struct cfg_encoder *
get_encoder_config(struct loongson_device *ldev, u32 index)
{
	struct cfg_encoder *encoder_config = NULL;
	struct cfg_encoder *encoder_cfg = NULL;
	struct loongson_vbios_encoder *encoder = NULL;
	struct encoder_config_param *vbios_cfg;
	struct vbios_cmd vbt_cmd;
	u32 i, size = 0;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		encoder = get_encoder_legacy(ldev, index);
		vbios_cfg = encoder->encoder_config;
		size = sizeof(struct cfg_encoder);
		encoder_config = kzalloc(size * LS_MAX_RESOLUTIONS, GFP_KERNEL);
		encoder_cfg = encoder_config;
		for (i = 0; i < LS_MAX_RESOLUTIONS; i++ ) {
			encoder_cfg->hdisplay = vbios_cfg->resolution.hdisplay;
			encoder_cfg->vdisplay = vbios_cfg->resolution.vdisplay;
			encoder_cfg->reg_num = vbios_cfg->encoder_param.reg_num;
			memcpy(&encoder_cfg->config_regs,
					&vbios_cfg->encoder_param.config_regs,
					sizeof(struct config_reg) * 256);
			encoder_cfg++;
			vbios_cfg++;
		}
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_cfg_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_CONFIG_PARAM;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret) {
			encoder_config = (struct cfg_encoder *)vbt_cmd.res;
		}
	}

	return encoder_config;
}

u32 get_encoder_cfg_num(struct loongson_device *ldev, u32 index)
{
	struct vbios_cmd vbt_cmd;
	bool ret = false;
	u32 cfg_num = 0;

	if (is_legacy_vbios(ldev->vbios)) {
		cfg_num = LS_MAX_RESOLUTIONS;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_cfg_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_CONFIG_NUM;
		vbt_cmd.res = (void *)(ulong)&cfg_num;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			cfg_num = 0;
		}
	}

	return cfg_num;
}

enum encoder_config
get_encoder_config_type(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_encoder *encoder = NULL;
	enum encoder_config config_type = encoder_bios_config;
	struct vbios_cmd vbt_cmd;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		encoder = get_encoder_legacy(ldev, index);
		config_type = encoder->config_type;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_CONFIG_TYPE;
		vbt_cmd.res = (void *)(ulong)&config_type;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			config_type = encoder_bios_config;
		}
	}

	return config_type;
}

enum encoder_type get_encoder_type(struct loongson_device *ldev, u32 index)
{
	struct loongson_vbios_encoder *encoder;
	enum encoder_type type = encoder_dac;
	struct vbios_cmd vbt_cmd;
	bool ret = false;

	if (is_legacy_vbios(ldev->vbios)) {
		encoder = get_encoder_legacy(ldev, index);
		type = encoder->type;
	} else {
		vbt_cmd.index = index;
		vbt_cmd.type = desc_encoder;
		vbt_cmd.req = (void *)(ulong)VBIOS_ENCODER_TYPE;
		vbt_cmd.res = (void *)(ulong)&type;
		ret = vbios_get_data(ldev, &vbt_cmd);
		if (ret == false) {
			type = encoder_dac;
		}
	}

	return type;
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
	u32 ret;

	INIT_LIST_HEAD(&ldev->desc_list);

	ret = parse_vbios_desc(ldev);

	return ret;
}

void loongson_vbios_exit(struct loongson_device *ldev)
{
	if (!is_legacy_vbios(ldev->vbios)) {
		free_desc_list(ldev);
	}

	kfree(ldev->vbios);
}

