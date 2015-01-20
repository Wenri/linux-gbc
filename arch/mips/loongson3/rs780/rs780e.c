
#include <south-bridge.h>

extern void rs780_init_irq(void);
extern void rs780_irq_dispatch(unsigned int pending);
extern int loongson_rtc_platform_init(void);

#ifdef CONFIG_SWIOTLB
extern void rs780_plat_swiotlb_setup(void);
#endif

extern void rs780_pcibios_init(void);

static void __init rs780_arch_initcall(void)
{
	rs780_pcibios_init();
}

static void __init rs780_device_initcall(void)
{
	loongson_rtc_platform_init();
}

const struct south_bridge rs780_south_bridge = {
	.sb_init_irq		= rs780_init_irq,
#ifdef CONFIG_SWIOTLB
	.sb_init_swiotlb	= rs780_plat_swiotlb_setup,
#endif
	.sb_irq_dispatch	= rs780_irq_dispatch,
	.sb_arch_initcall	= rs780_arch_initcall,
	.sb_device_initcall	= rs780_device_initcall,
};
