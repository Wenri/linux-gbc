#ifndef __ASM_MACH_LOONGSON3_SPACES_H_
#define __ASM_MACH_LOONGSON3_SPACES_H_

#ifndef CAC_BASE
#if defined(CONFIG_64BIT)
#define CAC_BASE        _AC(0x9800000000000000, UL)
#endif /* CONFIG_64BIT */
#endif /* CONFIG_CAC_BASE */


#include <asm/mach-generic/spaces.h>
#endif
