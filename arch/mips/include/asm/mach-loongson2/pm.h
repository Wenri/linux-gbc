#ifndef __LS2K_PM_H
#define __LS2K_PM_H

#define S_GPMCR 	0
#define R_GPMCR		0x4
#define RTC_GPMCR		0x8
#define PM1_STS			0xc
#define PM1_SR			0x10
#define PM1_CTR			0x14
#define PM1_TIMER		0x18
#define PM_PCTR			0x1c

#define GPE0_STS		0x28
#define GPE0_SR			0x2c
#define RST_CTR			0x30
#define WD_CR			0x34
#define WD_TIMER		0x38

#define THER_SCR		0x4c
#define G_RTC_1			0x50
#define G_RTC_2			0x54


#define DPM_CfG			0x400
#define DPM_STS			0x404
#define DPM_CTR			0x408

#endif
