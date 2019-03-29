#include <linux/err.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#ifdef CONFIG_CPU_LOONGSON3
#include <loongson.h>
#include <boot_param.h>
#include <loongson_hwmon.h>
#else
#include <ls2k.h>
#endif

/* Allow other reference temperatures to fixup the original cpu temperature */
int __weak fixup_cpu_temp(int cpu, int cputemp)
{
	return cputemp;
}

/*
 * Loongson-3 series cpu has two sensors inside,
 * each of them from 0 to 255,
 * if more than 127, that is dangerous.
 * here only provide sensor1 data, because it always hot than sensor0
 */
int loongson_cpu_temp(int cpu)
{
	int cputemp;
	u32 reg, prid_rev;

	switch(current_cpu_type()){
	case CPU_LOONGSON3:
		reg = LOONGSON_CHIPTEMP(cpu);
		prid_rev = read_c0_prid() & PRID_REV_MASK;
		switch (prid_rev) {
		case PRID_REV_LOONGSON3A_R1:
			reg = (reg >> 8) & 0xff;
			break;
		case PRID_REV_LOONGSON3A_R2:
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			reg = ((reg >> 8) & 0xff) - 100;
			break;
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			reg = (reg & 0xffff)*731/0x4000 - 273;
			break;
		}
		break;
	case CPU_LOONGSON2K:
		reg = (LOONGSON_CHIPTEMP(cpu) & 0xffff) - 100;
		break;
#ifdef CONFIG_CPU_LOONGSON3
	case CPU_LOONGSON3_COMP:
		reg = (read_csr(LOONGSON_CPU_TEMPERATURE_OFFSET) & 0xff);
#endif
	}
	cputemp = (int)reg * 1000;
	return fixup_cpu_temp(cpu, cputemp);

}
EXPORT_SYMBOL(loongson_cpu_temp);

/* ========================= Hwmon ====================== */
static struct device *cpu_hwmon_dev;

static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, get_hwmon_name, NULL, 0);

static struct attribute *cpu_hwmon_attributes[] =
{
	&sensor_dev_attr_name.dev_attr.attr,
	NULL
};

/* Hwmon device attribute group */
static struct attribute_group cpu_hwmon_attribute_group =
{
	.attrs = cpu_hwmon_attributes,
};

/* Hwmon device get name */
static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "cpu-hwmon\n");
}

/* ========================= Temperature ====================== */

static ssize_t get_cpu0_temp(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t get_cpu1_temp(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t cpu0_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t cpu1_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, get_cpu0_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, cpu0_temp_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, get_cpu1_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, cpu1_temp_label, NULL, 2);

static const struct attribute *hwmon_cputemp1[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	NULL
};

static const struct attribute *hwmon_cputemp2[] = {
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	NULL
};

static ssize_t cpu0_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CPU 0 Temprature\n");
}

static ssize_t cpu1_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CPU 1 Temprature\n");
}

static ssize_t get_cpu0_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int value = loongson_cpu_temp(0);
	return sprintf(buf, "%d\n", value);
}

static ssize_t get_cpu1_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int value = loongson_cpu_temp(1);
	return sprintf(buf, "%d\n", value);
}

static int create_sysfs_cputemp_files(struct kobject *kobj)
{
	int ret;

	ret = sysfs_create_files(kobj, hwmon_cputemp1);
	if (ret)
		goto sysfs_create_temp1_fail;

	if (nr_cpus_loongson <= cores_per_package)
		return 0;

	ret = sysfs_create_files(kobj, hwmon_cputemp2);
	if (ret)
		goto sysfs_create_temp2_fail;

	return 0;

sysfs_create_temp2_fail:
	sysfs_remove_files(kobj, hwmon_cputemp1);

sysfs_create_temp1_fail:
	return -1;
}

static void remove_sysfs_cputemp_files(struct kobject *kobj)
{
	sysfs_remove_files(&cpu_hwmon_dev->kobj, hwmon_cputemp1);

	if (nr_cpus_loongson > cores_per_package)
		sysfs_remove_files(&cpu_hwmon_dev->kobj, hwmon_cputemp2);
}

#define CPU_THERMAL_THRESHOLD 98000
static struct delayed_work thermal_work;

static void do_thermal_timer(struct work_struct *work)
{
	int value = loongson_cpu_temp(0);
	if ((value <= CPU_THERMAL_THRESHOLD) || (loongson_hwmon == 0)) {
		schedule_delayed_work(&thermal_work, msecs_to_jiffies(5000));
	} else {
		pr_emerg("!!!High temperature: %d. Power-off NOW!\n", value);
		orderly_poweroff(true);
	}
}

static int __init loongson_hwmon_init(void)
{
	int ret;

	printk(KERN_INFO "Loongson Hwmon Enter...\n");

	cpu_hwmon_dev = hwmon_device_register(NULL);
	if (IS_ERR(cpu_hwmon_dev)) {
		ret = -ENOMEM;
		printk(KERN_ERR "hwmon_device_register fail!\n");
		goto fail_hwmon_device_register;
	}

	ret = sysfs_create_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);
	if (ret) {
		printk(KERN_ERR "fail to create loongson hwmon!\n");
		goto fail_sysfs_create_group_hwmon;
	}

	ret = create_sysfs_cputemp_files(&cpu_hwmon_dev->kobj);
	if (ret) {
		printk(KERN_ERR "fail to create cpu temprature interface!\n");
		goto fail_create_sysfs_cputemp_files;
	}

	INIT_DEFERRABLE_WORK(&thermal_work, do_thermal_timer);
	return ret;

fail_create_sysfs_cputemp_files:
	sysfs_remove_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);

fail_sysfs_create_group_hwmon:
	hwmon_device_unregister(cpu_hwmon_dev);

fail_hwmon_device_register:
	return ret;
}

static void __exit loongson_hwmon_exit(void)
{
	cancel_delayed_work_sync(&thermal_work);
	remove_sysfs_cputemp_files(&cpu_hwmon_dev->kobj);
	sysfs_remove_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);
	hwmon_device_unregister(cpu_hwmon_dev);
}

static int ls_hwmon_probe(struct platform_device *pdev)
{
	struct resource *mem = NULL;
	struct device_node *np;
	struct ls_temp_id *hwmon_id;
	int ret;
	int id = 0;
	int max_id = 0;

	np = of_node_get(pdev->dev.of_node);
	if(np){
		if(of_property_read_u32(np, "max-id", &max_id))
			pr_err("not found max-id property!\n");

		if(of_property_read_u32(np, "id", &id))
			pr_err("not found id property!\n");
	}else{
		hwmon_id = pdev->dev.platform_data;
		max_id = hwmon_id->max_id;
		id = pdev->id;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem){
		ret = -ENODEV;
		goto out;
	}

	if(!request_mem_region(mem->start, resource_size(mem), pdev->name)){
		ret = -EBUSY;
		goto out;
	}

	loongson_chiptemp[id] = (u64)ioremap(mem->start, resource_size(mem));

	if (id == max_id)
		schedule_delayed_work(&thermal_work, msecs_to_jiffies(20000));
	return 0;

out:
	pr_err("%s: %s: missing mandatory property\n", __func__, pdev->name);
	return ret;
}

int ls_hwmon_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls_hwmon_id_table[] = {
	{ .compatible = "loongson,ls-hwmon" },
	{},
};
#endif

static struct platform_driver ls_hwmon_driver = {
	.probe	= ls_hwmon_probe,
	.remove = ls_hwmon_remove,
	.driver = {
		.name = "ls-hwmon",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_hwmon_id_table),
#endif
	},
};

static int __init ls_hwmon_init_driver(void)
{
	loongson_hwmon_init();
	return platform_driver_register(&ls_hwmon_driver);
}

static void __exit ls_hwmon_exit_driver(void)
{
	platform_driver_unregister(&ls_hwmon_driver);
	loongson_hwmon_exit();
}

module_init(ls_hwmon_init_driver);
module_exit(ls_hwmon_exit_driver);

MODULE_AUTHOR("Yu Xiang <xiangy@lemote.com>");
MODULE_AUTHOR("Huacai Chen <chenhc@lemote.com>");
MODULE_DESCRIPTION("Loongson CPU Hwmon driver");
