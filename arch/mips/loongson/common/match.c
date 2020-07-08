#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/slab.h>

#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
ssize_t arch_print_cpu_modalias(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	ssize_t n;
	u32 i;

	n = sprintf(buf, "cpu:type:%s:feature:",
		    ELF_PLATFORM);

	for (i = 0; i < MAX_CPU_FEATURES; i++)
		if (cpu_have_feature(i)) {
			if (PAGE_SIZE < n + sizeof(",XXXX\n")) {
				WARN(1, "CPU features overflow page\n");
				break;
			}
			n += sprintf(&buf[n], ",%04X", i);
		}
	buf[n++] = '\n';
	return n;
}

int arch_cpu_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf) {
		arch_print_cpu_modalias(NULL, NULL, buf);
		add_uevent_var(env, "MODALIAS=%s", buf);
		kfree(buf);
	}
	return 0;
}
#endif
