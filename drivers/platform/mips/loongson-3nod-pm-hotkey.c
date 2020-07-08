/*
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file "COPYING" in the main directory of this archive for
 *  more details.
 *
 *  Copyright (C) 2020 Loongson Technology Corp.
 *  Author: Chao Li, lichao@loongson.cn
 *  Author: Yanbing Lv, lvyanbing@loongson.cn
 */
#include <asm/uaccess.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/video_output.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <asm/bootinfo.h>
#include <linux/module.h>
#include <loongson-pch.h>
#include <linux/dmi.h>
#include <ec_it8528.h>
#include <linux/gpio.h>

#define THIS_DRIVERS_NAME     "3nod_pm_hotkey"
#define MACHINE_IDENTITY      "3Nod-LS3A4000-LS7A-laptop"
#define DEVICE_NAME           "laptop"
#define MANUFACTURER_NAME     "3nod"

#define DEBUG_MSG             "3nod_hotkey"

/* debug msg */
#ifdef DEBUG_HOTKEY
#define LogLocal(format, ...) \
  do{\
    printk(KERN_INFO "[%s:%d]:"format"", DEBUG_MSG, __LINE__, ##__VA_ARGS__); \
  }while(0);
#else
#define LogLocal(format, ...) \
  do{\
  }while(0);
#endif

extern void prom_printf(char *fmt, ...);
extern int it8528_query_get_event_num(void);

static struct platform_device loongson_3nod_device = {
  .name = THIS_DRIVERS_NAME,
  .id = -1,
};

static struct input_dev *nod3_hotkey_input = NULL;

/* dmi and quirk */
#define SIO_IRQ_NUM 14
#define SCI_IRQ_NUM 123

struct quirk_entry {
  int sci_irq_num;
  int is_laptop;
  int is_allinone;
};
static struct quirk_entry quirk_default = {
  .sci_irq_num = SIO_IRQ_NUM,
  .is_laptop = 0,
  .is_allinone = 0,
};
static struct quirk_entry *quirks = &quirk_default;

static int dmi_check_cb(const struct dmi_system_id *dmi)
{
  pr_info("Identified 3nod device model '%s'\n", dmi->ident);

  quirks = dmi->driver_data;

  return 1;
}

static struct quirk_entry quirk_3nod_ls7a_laptop = {
  .sci_irq_num = SCI_IRQ_NUM,
  .is_laptop = 1,
  .is_allinone = 0,
};
static const struct dmi_system_id loongson_device_table[] = {
  {
    .ident = "3nod ls7a1000 laptop",
    .matches = {
      DMI_MATCH(DMI_SYS_VENDOR, "Loongson"),
      DMI_MATCH(DMI_PRODUCT_NAME, MACHINE_IDENTITY),
    },
    .callback = dmi_check_cb,
    .driver_data = &quirk_3nod_ls7a_laptop,
  },
  {}
};
MODULE_DEVICE_TABLE(dmi, loongson_device_table);
static int dmi_checked;

/* SCI device structure */
struct sci_device{
  unsigned char number;/* The sci number get from ec */
  unsigned char parameter;/* Sci count */
  unsigned char irq;/* Irq relative */
  unsigned char irq_data;
  unsigned char name[10];/* Device name */
};
static struct sci_device *loongson_sci_device = NULL;

#define POWER_INFO_CACHED_TIMEOUT   100  /* jiffies */
#define BIT_BAT_POWER_ACIN          (1 << 0)

/* Power information structure */
struct loongson_power_info
{
  /* AC insert or not */
  unsigned int ac_in;
  /* Battery insert or not */
  unsigned int bat_in;
  unsigned int health;

  /* Battery designed capacity */
  unsigned int design_capacity;
  /* Battery designed voltage */
  unsigned int design_voltage;
  /* Battery capacity after full charged */
  unsigned int full_charged_capacity;
  /* Battery Manufacture Date */
  unsigned char manufacture_date[11];
  /* Battery Serial number */
  unsigned char serial_number[8];
  /* Battery Manufacturer Name, max 11 + 1(length) bytes */
  unsigned char manufacturer_name[12];
  /* Battery Device Name, max 7 + 1(length) bytes */
  unsigned char device_name[8];
  /* Battery Technology */
  unsigned int technology;
  /* Battery cell count */
  unsigned char cell_count;

  /* Battery dynamic charge/discharge voltage */
  unsigned int voltage_now;
  /* Battery dynamic charge/discharge average current */
  int current_now;
  int current_sign;
  int current_average;
  /* Battery current remaining capacity */
  unsigned int remain_capacity;
  /* Battery current remaining capacity percent */
  unsigned int remain_capacity_percent;
  /* Battery current temperature */
  unsigned int temperature;
  /* Battery current remaining time (AverageTimeToEmpty) */
  unsigned int remain_time;
  /* Battery current full charging time (averageTimeToFull) */
  unsigned int fullchg_time;
  /* Battery Status */
  unsigned int charge_status;
  /* Battery current cycle count (CycleCount) */
  unsigned int cycle_count;
};
static struct loongson_power_info * power_info = NULL;

/* SCI device event structure */
typedef int (*sci_handler)(int status);
struct sci_event{
  int status_index;
  sci_handler handler;
};

static int loongson_lid_handler(int status);
static int loongson_ac_handler(int status);
static int loongson_bat_handler(int status);

static const struct sci_event sci_handler_table[] = {
  /* ec event number              EC state reg index             handler                  */
  [SCI_EVENT_NUM_AC]             = {0,                           loongson_ac_handler       },
  [SCI_EVENT_NUM_LID]            = {INDEX_POWER_STATUS,          loongson_lid_handler      },
  [SCI_EVENT_NUM_BAT]            = {INDEX_POWER_STATUS,          loongson_bat_handler      },
  [SCI_EVENT_NUM_WLAN]           = {INDEX_DEVICE_STATUS,         NULL                      },
  [SCI_EVENT_NUM_TP]             = {0,                           NULL                      },
  [SCI_EVENT_NUM_BRIGHTNESS_OFF] = {0,                           NULL                      },
  [SCI_EVENT_NUM_BRIGHTNESS_DN]  = {0,                           NULL                      },
  [SCI_EVENT_NUM_BRIGHTNESS_UP]  = {0,                           NULL                      },
  [SCI_EVENT_NUM_SLEEP]          = {0,                           NULL                      },
  [SCI_EVENT_NUM_DISPLAY_TOGGLE] = {0,                           NULL                      },
  [SCI_EVENT_NUM_POWERBTN]       = {0,                           NULL                      }
};

static const struct key_entry loongson_keymap[] = {
  /* type  ec event number                 keycode               */
  {KE_SW,  SCI_EVENT_NUM_LID,             {SW_LID}               },
  {KE_KEY, SCI_EVENT_NUM_WLAN,            {KEY_WLAN}             },
  {KE_KEY, SCI_EVENT_NUM_TP,              {KEY_TOUCHPAD_TOGGLE}  },
  {KE_KEY, SCI_EVENT_NUM_BRIGHTNESS_OFF,  {KEY_DISPLAYTOGGLE}    },
  {KE_KEY, SCI_EVENT_NUM_BRIGHTNESS_DN,   {KEY_BRIGHTNESSDOWN}   },
  {KE_KEY, SCI_EVENT_NUM_BRIGHTNESS_UP,   {KEY_BRIGHTNESSUP}     },
  {KE_KEY, SCI_EVENT_NUM_SLEEP,           {KEY_SLEEP}            },
  {KE_KEY, SCI_EVENT_NUM_DISPLAY_TOGGLE,  {KEY_SWITCHVIDEOMODE}  },
  {KE_KEY, SCI_EVENT_NUM_POWERBTN,        {KEY_POWER}            },
  {KE_END, 0                                                     } /* End */
};

static int loongson_sci_event_probe(struct platform_device *);
static int loongson_laptop_suspend(struct platform_device * pdev, pm_message_t state);
static int loongson_laptop_resume(struct platform_device * pdev);
static struct platform_driver loongson_3nod_pdriver = {
  .probe = loongson_sci_event_probe,
  .driver = {
    .name =  THIS_DRIVERS_NAME,
    .owner = THIS_MODULE,
  },
#ifdef CONFIG_PM
  .suspend = loongson_laptop_suspend,
  .resume  = loongson_laptop_resume,
#endif /* CONFIG_PM */
};

enum{
  APM_BAT_STATUS_HIGH =    0,
  APM_BAT_STATUS_LOW,
  APM_BAT_STATUS_CRITICAL,
  APM_BAT_STATUS_CHARGING,
  APM_BAT_STATUS_NOT_PRESENT,
  APM_BAT_STATUS_UNKNOWN =  0xff
};

/*
 * Function: loongson_power_info_battery_static_update
 *
 * This function will update the static information about battery.
 *
 * */
static void loongson_power_info_battery_static_update(void)
{
  unsigned int bat_serial_number;

  /* Update technology */
  power_info->technology = POWER_SUPPLY_TECHNOLOGY_LION;

  /* Update serial_number */
  bat_serial_number = (it8528_read(INDEX_BATTERY_SN_HIGH) << 8) | it8528_read(INDEX_BATTERY_SN_LOW);
  snprintf(power_info->serial_number, 8, "%x", bat_serial_number);

  /* Update cell_count */
  power_info->cell_count = ((it8528_read(INDEX_BATTERY_CV_HIGH) << 8) | it8528_read(INDEX_BATTERY_CV_LOW)) / 4200;

  /* Update design_capacity */
  power_info->design_capacity = (it8528_read(INDEX_BATTERY_DC_HIGH) << 8) | it8528_read(INDEX_BATTERY_DC_LOW);

  /* Update design_voltage */
  power_info->design_voltage = (it8528_read(INDEX_BATTERY_DV_HIGH) << 8) | it8528_read(INDEX_BATTERY_DV_LOW);

  /* Update full_charged_capacity */
  power_info->full_charged_capacity = (it8528_read(INDEX_BATTERY_FCC_HIGH) << 8) | it8528_read(INDEX_BATTERY_FCC_LOW);

  snprintf(power_info->device_name, 8, "%s", DEVICE_NAME);
  snprintf(power_info->manufacturer_name, 12, "%s", MANUFACTURER_NAME);
}

enum{
  APM_AC_OFFLINE =  0,
  APM_AC_ONLINE,
  APM_AC_BACKUP,
  APM_AC_UNKNOWN =  0xff
};

/*
 *  Function: loongson_power_info_power_status_update
 *
 *  This function will update information about power status.
 *
 * */
static void loongson_power_info_power_status_update(void)
{
  unsigned int power_status = 0;
  unsigned int bat_dc = 0;

  power_status = it8528_read(INDEX_POWER_STATUS);

  power_info->ac_in = (power_status & MASK(BIT_POWER_ACPRES)) ?
    APM_AC_ONLINE : APM_AC_OFFLINE;

  power_info->bat_in = (power_status & MASK(BIT_POWER_BATPRES)) ? 1 : 0;
  bat_dc = it8528_read(INDEX_BATTERY_DC_LOW) | (it8528_read(INDEX_BATTERY_DC_HIGH) << 8);
  if (power_info->bat_in && (bat_dc == 0))
    power_info->bat_in = 0;

  power_info->health = (power_info->bat_in) ?  POWER_SUPPLY_HEALTH_GOOD : POWER_SUPPLY_HEALTH_UNKNOWN;
  if (!power_info->bat_in) {
    power_info->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
  }else {
    if (power_status & MASK(BIT_POWER_BATFCHG)) {
      power_info->charge_status = POWER_SUPPLY_STATUS_FULL;
    }
    else if (power_status & MASK(BIT_POWER_BATCHG)) {
      power_info->charge_status = POWER_SUPPLY_STATUS_CHARGING;
    }
    else {
      power_info->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
    }
  }
}

enum {/* bat_reg_flag */
  BAT_REG_TEMP_FLAG = 1,
  BAT_REG_VOLTAGE_FLAG,
  BAT_REG_CURRENT_FLAG,
  BAT_REG_AC_FLAG,
  BAT_REG_RC_FLAG,
  BAT_REG_FCC_FLAG,
  BAT_REG_ATTE_FLAG,
  BAT_REG_ATTF_FLAG,
  BAT_REG_RSOC_FLAG,
  BAT_REG_CYCLCNT_FLAG
};

/*
 * Function: loongson_power_battery_info_update
 *
 * This function will update the information about battery.
 *
 * */
static void loongson_power_battery_info_update(unsigned char bat_reg_flag)
{
  short bat_info_value = 0;

  switch (bat_reg_flag) {
    /* Update power_info->temperature value */
  case BAT_REG_TEMP_FLAG:
    loongson_power_info_power_status_update();
    bat_info_value = (it8528_read(INDEX_BATTERY_TEMP_HIGH) << 8) | it8528_read(INDEX_BATTERY_TEMP_LOW);
    power_info->temperature = (power_info->bat_in) ? (bat_info_value / 10 - 273) : 0;
    break;
    /* Update power_info->voltage value */
  case BAT_REG_VOLTAGE_FLAG:
    loongson_power_info_power_status_update();
    bat_info_value = (it8528_read(INDEX_BATTERY_VOL_HIGH) << 8) | it8528_read(INDEX_BATTERY_VOL_LOW);
    power_info->voltage_now = (power_info->bat_in) ? bat_info_value : 0;
    break;
    /* Update power_info->current_now value */
  case BAT_REG_CURRENT_FLAG:
    loongson_power_info_power_status_update();
    bat_info_value = (it8528_read(INDEX_BATTERY_CURRENT_HIGH) << 8) | it8528_read(INDEX_BATTERY_CURRENT_LOW);
    power_info->current_now = (power_info->bat_in) ? bat_info_value : 0;
    break;
    /* Update power_info->current_avg value */
  case BAT_REG_AC_FLAG:
    loongson_power_info_power_status_update();
    bat_info_value = (it8528_read(INDEX_BATTERY_AC_HIGH) << 8) | it8528_read(INDEX_BATTERY_AC_LOW);
    power_info->current_average = (power_info->bat_in) ? bat_info_value : 0;
    break;
    /* Update power_info->remain_capacity value */
  case BAT_REG_RC_FLAG:
    power_info->remain_capacity = (it8528_read(INDEX_BATTERY_RC_HIGH) << 8) | it8528_read(INDEX_BATTERY_RC_LOW);
    break;
    /* Update power_info->full_charged_capacity value */
  case BAT_REG_FCC_FLAG:
    power_info->full_charged_capacity = (it8528_read(INDEX_BATTERY_FCC_HIGH) << 8) | it8528_read(INDEX_BATTERY_FCC_LOW);
    break;
    /* Update power_info->remain_time value */
  case BAT_REG_ATTE_FLAG:
    power_info->remain_time = (it8528_read(INDEX_BATTERY_ATTE_HIGH) << 8) | it8528_read(INDEX_BATTERY_ATTE_LOW);
    break;
    /* Update power_info->fullchg_time value */
  case BAT_REG_ATTF_FLAG:
    power_info->fullchg_time = (it8528_read(INDEX_BATTERY_ATTF_HIGH) << 8) | it8528_read(INDEX_BATTERY_ATTF_LOW);
    break;
    /* Update power_info->curr_cap value */
  case BAT_REG_RSOC_FLAG:
    power_info->remain_capacity_percent = it8528_read(INDEX_BATTERY_CAPACITY);
    break;
    /* Update power_info->cycle_count value */
  case BAT_REG_CYCLCNT_FLAG:
    power_info->cycle_count = (it8528_read(INDEX_BATTERY_CYCLECNT_HIGH) << 8) | it8528_read(INDEX_BATTERY_CYCLECNT_LOW);
    break;

  default:
    break;
  }
}

static enum power_supply_property loongson_bat_props[] =
{
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_TECHNOLOGY,
  POWER_SUPPLY_PROP_CYCLE_COUNT,
  POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
  POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
  POWER_SUPPLY_PROP_CURRENT_NOW,
  POWER_SUPPLY_PROP_CURRENT_AVG,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CHARGE_FULL, /* in uAh */
  POWER_SUPPLY_PROP_CHARGE_NOW,
  POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
  POWER_SUPPLY_PROP_TEMP,
  POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
  POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
  /* Properties of type `const char *' */
  POWER_SUPPLY_PROP_MODEL_NAME,
  POWER_SUPPLY_PROP_MANUFACTURER,
  POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

/*
 * Function; loongson_bat_get_property
 * @pws    : power supply device.
 * @psp    : the type of property.
 * @val    : the val will be get.
 *
 * This function will get a message about battery.
 *
 * */
static int loongson_bat_get_property(struct power_supply * pws,
    enum power_supply_property psp, union power_supply_propval * val)
{
  switch (psp) {
    /* Get battery static information. */
  case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
    val->intval = power_info->design_voltage * 1000; /* mV -> uV */
    break;
  case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
    val->intval = power_info->design_capacity * 1000; /* mAh -> uAh */
    break;
  case POWER_SUPPLY_PROP_MODEL_NAME:
    val->strval = power_info->device_name;
    break;
  case POWER_SUPPLY_PROP_MANUFACTURER:
    val->strval = power_info->manufacturer_name;
    break;
  case POWER_SUPPLY_PROP_SERIAL_NUMBER:
    val->strval = power_info->serial_number;
    break;
  case POWER_SUPPLY_PROP_TECHNOLOGY:
    val->intval = power_info->technology;
    break;
    /* Get battery dynamic information. */
  case POWER_SUPPLY_PROP_STATUS:
    loongson_power_info_power_status_update();
    val->intval = power_info->charge_status;
    break;
  case POWER_SUPPLY_PROP_PRESENT:
    loongson_power_info_power_status_update();
    val->intval = power_info->bat_in;
    break;
  case POWER_SUPPLY_PROP_HEALTH:
    loongson_power_info_power_status_update();
    val->intval = power_info->health;
    break;
  case POWER_SUPPLY_PROP_CURRENT_NOW:
    loongson_power_battery_info_update(BAT_REG_CURRENT_FLAG);
    val->intval = power_info->current_now * 1000; /* mA -> uA */
    break;
  case POWER_SUPPLY_PROP_CURRENT_AVG:
    loongson_power_battery_info_update(BAT_REG_AC_FLAG);
    val->intval = power_info->current_average * 1000; /* mA -> uA */
    break;
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    loongson_power_battery_info_update(BAT_REG_VOLTAGE_FLAG);
    val->intval =  power_info->voltage_now * 1000; /* mV -> uV */
    break;
  case POWER_SUPPLY_PROP_CHARGE_NOW:
    loongson_power_battery_info_update(BAT_REG_RC_FLAG);
    val->intval = power_info->remain_capacity * 1000; /* mAh -> uAh */
    break;
  case POWER_SUPPLY_PROP_CAPACITY:
    loongson_power_battery_info_update(BAT_REG_RSOC_FLAG);
    val->intval = power_info->remain_capacity_percent;  /* Percentage */
    break;
  case POWER_SUPPLY_PROP_TEMP:
    loongson_power_battery_info_update(BAT_REG_TEMP_FLAG);
    val->intval = power_info->temperature;   /* Celcius */
    break;
  case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
    loongson_power_battery_info_update(BAT_REG_ATTE_FLAG);
    if (power_info->remain_time == 0xFFFF) {
      power_info->remain_time = 0;
    }
    val->intval = power_info->remain_time * 60;  /* seconds */
    break;
  case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
    loongson_power_battery_info_update(BAT_REG_ATTF_FLAG);
    if (power_info->fullchg_time == 0xFFFF) {
      power_info->fullchg_time = 0;
    }
    val->intval = power_info->fullchg_time * 60;  /* seconds */
    break;
  case POWER_SUPPLY_PROP_CHARGE_FULL:
    loongson_power_battery_info_update(BAT_REG_FCC_FLAG);
    val->intval = power_info->full_charged_capacity * 1000;/* mAh -> uAh */
    break;
  case POWER_SUPPLY_PROP_CYCLE_COUNT:
    loongson_power_battery_info_update(BAT_REG_CYCLCNT_FLAG);
    val->intval = power_info->cycle_count;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}

/* Power supply Battery device object */
static struct power_supply loongson_bat =
{
  .name = "loongson-bat",
  .type = POWER_SUPPLY_TYPE_BATTERY,
  .properties = loongson_bat_props,
  .num_properties = ARRAY_SIZE(loongson_bat_props),
  .get_property = loongson_bat_get_property,
};

/* Power supply AC property object */
static enum power_supply_property loongson_ac_props[] ={
  POWER_SUPPLY_PROP_ONLINE,
};

/*
 * Function: loongson_ac_get_property
 * @pws    : power supply device.
 * @psp    : the type of property.
 * @val    : the val will be get.
 *
 * This function will get properties about ac.
 *
 * */
static int loongson_ac_get_property(struct power_supply * pws,
    enum power_supply_property psp, union power_supply_propval * val)
{
  switch (psp) {
  case POWER_SUPPLY_PROP_ONLINE:
    loongson_power_info_power_status_update();
    val->intval = power_info->ac_in;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}

/* Power supply AC device object */
static struct power_supply loongson_ac =
{
  .name = "loongson-ac",
  .type = POWER_SUPPLY_TYPE_MAINS,
  .properties = loongson_ac_props,
  .num_properties = ARRAY_SIZE(loongson_ac_props),
  .get_property = loongson_ac_get_property,
};

/*
 * Function: loongson_power_info_battery_static_clear
 *
 * Clear battery static information.
 *
 * */
static void loongson_power_info_battery_static_clear(void)
{
  strcpy(power_info->manufacturer_name, "Unknown");
  strcpy(power_info->device_name, "Unknown");
  power_info->technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
  strcpy(power_info->serial_number, "Unknown");
  strcpy(power_info->manufacture_date, "Unknown");
  power_info->cell_count = 0;
  power_info->design_capacity = 0;
  power_info->design_voltage = 0;
}

/*
 * Function: loongson_ac_handler
 * @status : State read from ec.
 *
 * SCI device AC event handler.
 *
 * */
static int loongson_ac_handler(int status)
{
  loongson_power_info_power_status_update();
  /* Report status changed */
  power_supply_changed(&loongson_ac);

  return 0;
}

/*
 * Function: loongson_lid_handler
 * @status : State read from ec.
 *
 * SCI device LID event handler.
 *
 * */
static int loongson_lid_handler(int status)
{
  if (status & BIT(BIT_LIDSTS)) {
    return 0;
  }
  return 1;
}

/*
 * Function: loongson_bat_handler
 * @status : State read from ec.
 *
 * SCI device Battery event handler
 *
 * */
static int loongson_bat_handler(int status)
{
  /* Battery insert/pull-out to handle battery static information. */
  if (status & MASK(BIT_POWER_BATPRES)) {
    /* If battery is insert, get battery static information. */
    loongson_power_info_battery_static_update();
  }else {
    /* Else if battery is pull-out, clear battery static information. */
    loongson_power_info_battery_static_clear();
  }
  /* Report status changed */
  power_supply_changed(&loongson_bat);

  return 0;
}

/*
 * Function: loongson_3nod_sci_hotkey_handler
 * @event  : event number read from ec.
 *
 * SCI device event handler.
 *
 * */
void loongson_3nod_sci_hotkey_handler(int event)
{
  int status = 0;
  struct key_entry * ke = NULL;
  struct sci_event * event_entry = NULL;

  event_entry = (struct sci_event*)&(sci_handler_table[event]);
  LogLocal("event num= 0x%x.\n",event);

  if (0 != event_entry->status_index){
    status = it8528_read(event_entry->status_index);
    LogLocal("ec read. status =0x%x.\n",status);
  }

  if (NULL != event_entry->handler){
    status = event_entry->handler(status);
    LogLocal("handler. status =0x%x.\n",status);
  }

  ke = sparse_keymap_entry_from_scancode(nod3_hotkey_input, event);

  if (ke) {
    if (SW_LID == ke->keycode) {
      LogLocal("lid:\nke.code=0x%x.\nke.keycode=%d.\nke.sw.code=%d.\nke.sw.value=%d.\n\n",
          ke->code, ke->keycode, ke->sw.code, ke->sw.value);
      input_report_switch(nod3_hotkey_input, SW_LID, status);
      input_sync(nod3_hotkey_input);
    } else {
      /* resport key event */
      LogLocal("key:\nke.code=0x%x.\nke.keycode=%d.\nke.sw.code=%d.\nke.sw.value=%d.\n\n",
          ke->code, ke->keycode, ke->sw.code, ke->sw.value);
      sparse_keymap_report_entry(nod3_hotkey_input, ke, 1, true);
      input_sync(nod3_hotkey_input);
    }
  }
}

/*
 * Function: loongson_sci_int_routine
 * @irq    : the number of interrupt.
 * @dev_id : the arg of this routine.
 *
 * sci interrupt handle routine.
 *
 * */
static irqreturn_t loongson_sci_int_routine(int irq, void *dev_id)
{
  int event;
  if ( loongson_sci_device->irq != irq ){
    printk("not loongson_sci_device irq = %d.\n",irq);
    return IRQ_NONE;
  }

  event = it8528_query_get_event_num();
  if ((SCI_EVENT_NUM_AC > event) || (SCI_EVENT_NUM_POWERBTN < event)){
    printk("this sci event num is not find.\n");
    goto exit_event_action;
  }

  /* handle event */
  loongson_3nod_sci_hotkey_handler(event);
  return IRQ_HANDLED;

exit_event_action:
  return IRQ_HANDLED;
}

/*
 * Function: sci_pci_init
 *
 * SCI driver pci driver init.
 *
 * */
static int sci_pci_init(void)
{
  int ret = 0;

  /* Alloc sci device */
  loongson_sci_device = kmalloc(sizeof(struct sci_device), GFP_KERNEL);
  if (!loongson_sci_device) {
    printk(KERN_ERR "Loongson-3nod: Malloc mem space for sci_dvice failed.\n");
    goto fail_out;
  }

  /* get sci irq number */
  if (loongson_pch->board_type == LS7A) {
    loongson_sci_device->irq = SCI_IRQ_NUM;
  }else{
    printk(KERN_INFO "board_type is not LS7A.\n");
    goto fail_out;
  }

  /* init sci device */
  loongson_sci_device->irq_data = 0x00;
  loongson_sci_device->number = 0x00;
  loongson_sci_device->parameter = 0x00;
  strcpy(loongson_sci_device->name, "3nod_ec");

  /* Regist pci irq */
  ret = request_irq(loongson_sci_device->irq, loongson_sci_int_routine,
      IRQF_SHARED, loongson_sci_device->name, loongson_sci_device);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod: Request irq fail.\n");
    goto fail_out;
  }

  return 0;

fail_out:
  kfree(loongson_sci_device);
  return ret;
}

/*
 * Function: loongson_3nod_hotkey_init
 *
 * register Hotkey input device and init it.
 *
 * */
static int loongson_3nod_hotkey_init(void)
{
  int ret = 0;

  /* Alloc Hotkey input device */
  nod3_hotkey_input = input_allocate_device();
  if (!nod3_hotkey_input){
    printk(KERN_ERR "Alloc Hotkey input device Fail!");
    goto fail_allocate_device;
  }

  /* init Hotkey input device */
  nod3_hotkey_input->name = "Loongson-3nod PM Hotkey Hotkeys";
  nod3_hotkey_input->phys = "button/input0";
  nod3_hotkey_input->id.bustype = BUS_HOST;
  nod3_hotkey_input->dev.parent = NULL;
  ret = sparse_keymap_setup(nod3_hotkey_input, loongson_keymap, NULL);
  if (ret) {
    printk(KERN_ERR " Loongson-3nod sparse Hotkey input device keymap fail!\n");
    goto fail_sparse_keymap;
  }

  /* register input device */
  ret = input_register_device(nod3_hotkey_input);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod register Hotkey input device fail!\n");
    goto fail_register_input_device;
  }
  return 0;

fail_register_input_device:
  input_unregister_device(nod3_hotkey_input);
fail_sparse_keymap:
  sparse_keymap_free(nod3_hotkey_input);
fail_allocate_device:
  input_free_device(nod3_hotkey_input);
  nod3_hotkey_input = NULL;
  return ret;
}

/*
 * Function: loongson_sci_event_probe
 * @dev    : the platform device.
 *
 * The function will be run, when the driver matches the device.
 *
 * */
static int loongson_sci_event_probe(struct platform_device *dev)
{
  int ret = 0;

  /* alloc and Register Hotkey  input device */
  if (loongson_3nod_hotkey_init()) {
    printk(KERN_ERR "Loongson-3nod Platform Driver: Hotkey init fail.\n");
    goto fail_power_info_alloc;
  }

  /* Register power supply START */
  power_info = kzalloc(sizeof(struct loongson_power_info), GFP_KERNEL);
  if (!power_info) {
    printk(KERN_ERR "Loongson-3nod Platform Driver: Alloc memory for power_info failed!\n");
    ret = -ENOMEM;
    goto fail_power_info_alloc;
  }

  /* Update power info */
  loongson_power_info_power_status_update();
  if (power_info->bat_in) {
    /* Get battery static information. */
    loongson_power_info_battery_static_update();
  }else {
    printk(KERN_ERR "Loongson-3nod Platform Driver: The battery does not exist!!\n");
  }

  /* Register Battery support*/
  ret = power_supply_register(NULL, &loongson_bat);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod Platform Driver: power bat supply register fail.\n");
    ret = -ENOMEM;
    goto fail_bat_power_supply_register;
  }

  /* Register adapter support*/
  ret = power_supply_register(NULL, &loongson_ac);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod Platform Driver: power ac supply register fail.\n");
    ret = -ENOMEM;
    goto fail_ac_power_supply_register;
  }

  /* SCI PCI Driver Init Start */
  ret = sci_pci_init();
  if (ret) {
    printk(KERN_ERR "Loongson-3nod: SCI Regist pci driver fail.\n");
    goto fail_sci_pci_driver_init;
  }

  return 0;

fail_sci_pci_driver_init:
fail_ac_power_supply_register:
  power_supply_unregister(&loongson_bat);
fail_bat_power_supply_register:
  kfree(power_info);
fail_power_info_alloc:
  return ret;
}

#ifdef CONFIG_PM
/* Platform device suspend handler */
static int loongson_laptop_suspend(struct platform_device * pdev, pm_message_t state)
{
  return 0;
}

/* Platform device resume handler */
static int loongson_laptop_resume(struct platform_device * pdev)
{
  /* Process LID event */
  loongson_3nod_sci_hotkey_handler(SCI_EVENT_NUM_LID);

  /* Update the power statu when resume */
  power_supply_changed(&loongson_ac);

  return 0;
}
#else
static int loongson_laptop_suspend(struct platform_device * pdev, pm_message_t state)
{
  return 0;
}

static int loongson_laptop_resume(struct platform_device * pdev)
{
  return 0;
}
#endif /* CONFIG_PM */

/*
 * Function: get_version
 *
 * Used for get the version of this driver.
 *
 * */
static ssize_t get_version(struct device_driver *driver, char *buf)
{
  return sprintf(buf, "%s\n", "1.0");
}
static DRIVER_ATTR(version, S_IRUGO, get_version, NULL);

/*
 * Function: loongson_3nod_laptop_init
 *
 * module init.
 *
 * */
static int __init loongson_3nod_laptop_init(void)
{
  int ret = 0;

  if (!dmi_check_system(loongson_device_table)) {
    printk(KERN_ERR "3nod dmi_check_cb Fail!.\n");
    return -ENODEV;
  }
  dmi_checked = 1;

  /* register device */
  ret = platform_device_register(&loongson_3nod_device);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod PM Hotkey Platform Driver: Fail regist loongson platform device.\n");
    goto fail_register_platform_device;
  }

  /* register driver */
  ret = platform_driver_register(&loongson_3nod_pdriver);
  if (ret) {
    printk(KERN_ERR "Loongson-3nod PM Hotkey Platform Driver: Fail regist loongson platform driver.\n");
    goto fail_register_platform_driver;
  }

  ret = driver_create_file(&loongson_3nod_pdriver.driver, &driver_attr_version);
  if (ret){
    printk(KERN_ERR "Loongson-3nod creat file fail!!.\n");
    goto fail_create_file;
  }

  return 0;

fail_create_file:
  driver_remove_file(&loongson_3nod_pdriver.driver, &driver_attr_version);
fail_register_platform_driver:
  platform_driver_unregister(&loongson_3nod_pdriver);
fail_register_platform_device:
  platform_device_unregister(&loongson_3nod_device);

  return ret;
}

static void __exit loongson_3nod_laptop_exit(void)
{
  platform_driver_unregister(&loongson_3nod_pdriver);
}

module_init(loongson_3nod_laptop_init);
module_exit(loongson_3nod_laptop_exit);

MODULE_AUTHOR("lvyanbing@Loongson.com");
MODULE_DESCRIPTION("Loongson-3nod PM Hotkey Driver");
MODULE_LICENSE("GPL");
