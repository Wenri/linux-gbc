#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int i, ret;

#ifdef CONFIG_NUMA
	for_each_online_node(i)
		register_one_node(i);
#endif /* CONFIG_NUMA */

	for_each_present_cpu(i) {
		struct cpu *c = &per_cpu(cpu_devices, i);

		c->hotpluggable = 1;
		ret = register_cpu(c, i);
		if (ret)
			printk(KERN_WARNING "topology_init: register_cpu %d "
			       "failed (%d)\n", i, ret);
	}

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
void setup_cpu_topology(int cpu)
{
	int ret;
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	c->hotpluggable = 1;
	ret = register_cpu(c, cpu);
	if (ret)
		printk(KERN_WARNING "topology_init: register_cpu %d "
			"failed (%d)\n", cpu, ret);
}

void clear_cpu_topology(int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	c->hotpluggable = 0;
	unregister_cpu(c);
}
#endif
subsys_initcall(topology_init);
