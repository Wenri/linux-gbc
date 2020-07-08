/*
 * =====================================================================================
 *
 *       Filename:  2k1000.h
 *
 *    Description:  soc conf register
 *
 *        Version:  1.0
 *        Created:  03/14/2017 02:42:46 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#ifndef __2K1000_H_
#define __2K1000_H_

/* we need to read back to ensure the write is accepted by conf bus */

static inline void ls64_conf_write64(u64 val64, volatile void __iomem *addr)
{

	asm volatile (
	"	.set push			\n"
	"	.set noreorder			\n"
	"	sd	%[v], (%[hw])		\n"
	"	lb	$0, (%[hw])		\n"
	"	.set pop			\n"
	:
	: [hw] "r" (addr),  [v] "r" (val64)
	);
}

static inline void ls64_conf_write32(u32 val, volatile void __iomem *addr)
{
	asm volatile (
	"	.set push			\n"
	"	.set noreorder			\n"
	"	sw	%[v], (%[hw])		\n"
	"	lb	$0, (%[hw])		\n"
	"	.set pop			\n"
	:
	: [hw] "r" (addr), [v] "r" (val)
	);

}

static inline void ls64_conf_write16(u16 val, volatile void __iomem *addr)
{
	asm volatile (
	"	.set push			\n"
	"	.set noreorder			\n"
	"	sh	%[v], (%[hw])		\n"
	"	lb	$0, (%[hw])		\n"
	"	.set pop			\n"
	:
	: [hw] "r" (addr), [v] "r" (val)
	);

}

static inline void ls64_conf_write8(u8 val, volatile void __iomem *addr)
{
	asm volatile (
	"	.set push			\n"
	"	.set noreorder			\n"
	"	sb	%[v], (%[hw])		\n"
	"	lb	$0, (%[hw])		\n"
	"	.set pop			\n"
	:
	:[hw] "r" (addr), [v] "r" (val)
	);

}

#define ls64_conf_read64(x) 	readq(x)
#define ls64_conf_read32(x) 	readl(x)
#define ls64_conf_read16(x) 	readw(x)
#define ls64_conf_read8(x) 	readb(x)

#define CONF_BASE 0x1fe10000

#define CPU_WBASE0_OFF 0x0
#define CPU_WBASE1_OFF 0x8
#define CPU_WBASE2_OFF 0x10
#define CPU_WBASE3_OFF 0x18
#define CPU_WBASE4_OFF 0x20
#define CPU_WBASE5_OFF 0x28
#define CPU_WBASE6_OFF 0x30
#define CPU_WBASE7_OFF 0x38

#define CPU_WMASK0_OFF 0x40
#define CPU_WMASK1_OFF 0x48
#define CPU_WMASK2_OFF 0x50
#define CPU_WMASK3_OFF 0x58
#define CPU_WMASK4_OFF 0x60
#define CPU_WMASK5_OFF 0x68
#define CPU_WMASK6_OFF 0x70
#define CPU_WMASK7_OFF 0x78

#define CPU_WMMAP0_OFF 0x80
#define CPU_WMMAP1_OFF 0x88
#define CPU_WMMAP2_OFF 0x90
#define CPU_WMMAP3_OFF 0x98
#define CPU_WMMAP4_OFF 0xa0
#define CPU_WMMAP5_OFF 0xa8
#define CPU_WMMAP6_OFF 0xb0
#define CPU_WMMAP7_OFF 0xb8


#define SCACHE_LA0_OFF 0x200
#define SCACHE_LA1_OFF 0x208
#define SCACHE_LA2_OFF 0x210
#define SCACHE_LA3_OFF 0x218


#define SCACHE_LM0_OFF 0x240
#define SCACHE_LM1_OFF 0x248
#define SCACHE_LM2_OFF 0x250
#define SCACHE_LM3_OFF 0x258

#define SIGNAL_OFF 0x400


#define GMAC_OFF 0x420
#define UART_OFF 0x428
#define GPU_OFF 0x430
#define APBDMA_OFF 0x438
#define USB_PHY01_OFF 0x440
#define USB_PHY23_OFF 0x448
#define SATA_OFF 0x450
#define SATA_PHY_OFF 0x458
#define MON_WRDMA_OFF 0x460


#define PLL_SYS0_OFF 0x480
#define PLL_SYS1_OFF 0x488
#define PLL_DLL0_OFF 0x490
#define PLL_DLL1_OFF 0x498
#define PLL_DC0_OFF 0x4a0
#define PLL_DC1_OFF 0x4a8
#define PLL_PIX00_OFF 0x4b0
#define PLL_PIX01_OFF 0x4b8
#define PLL_PIX10_OFF 0x4c0
#define PLL_PIX11_OFF 0x4c8
#define PLL_FREQ_SC 0x4d0


#define GPIO0_OEN_OFF 0x500
#define GPIO1_OEN_OFF 0x508
#define GPIO0_O_OFF 0x510
#define GPIO1_O_OFF 0x518
#define GPIO0_I_OFF 0x520
#define GPIO1_I_OFF 0x528
#define GPIO0_INT_OFF 0x528
#define GPIO1_INT_OFF 0x530

#define PCIE0_REG0_OFF 0x580
#define PCIE0_REG1_OFF 0x588
#define PCIE0_PHY_OFF 0x590

#define PCIE1_REG0_OFF 0x5a0
#define PCIE1_REG1_OFF 0x5a8
#define PCIE1_PHY_OFF 0x5b0

#define CONF_DMA0_OFF 0xc00


#define CONF_DMA1_OFF 0xc10


#define CONF_DMA2_OFF 0xc20


#define CONF_DMA3_OFF 0xc30


#define CONF_DMA4_OFF 0xc40

#define C0_IPISR_OFF 0x1000
#define C0_IPIEN_OFF 0x1004
#define C0_IPISET_OFF 0x1008
#define C0_IPICLR_OFF 0x100c


#define C0_MAIL0_OFF 0x1020
#define C0_MAIL1_OFF 0x1028
#define C0_MAIL2_OFF 0x1030
#define C0_MAIL3_OFF 0x1038

#define INTSR0_OFF 0x1040
#define INTSR1_OFF 0x1048


#define C1_IPISR_OFF 0x1100
#define C1_IPIEN_OFF 0x1104
#define C1_IPISET_OFF 0x1108
#define C1_IPICLR_OFF 0x110c


#define C1_MAIL0_OFF 0x1120
#define C1_MAIL1_OFF 0x1128
#define C1_MAIL2_OFF 0x1130
#define C1_MAIL3_OFF 0x1138

#define INT_LO_OFF 0x1400
#define INT_HI_OFF 0x1440

#define INT_RTEBASE_OFF 0x0
#define INT_SR_OFF 0x20
#define INT_EN_OFF 0x24
#define INT_SET_OFF 0x28
#define INT_CLR_OFF 0x2c
#define INT_PLE_OFF 0x30
#define INT_EDG_OFF 0x34
#define INT_BCE_OFF 0x38
#define INT_AUTO_OFF 0x3c


#define THSENS_INT_CTL_HI_OFF 0x1500
#define THSENS_INT_CTL_LO_OFF 0x1508
#define THSENS_INT_CTL_SR_CLR_OFF 0x1510


#define THSENS_SCAL_OFF 0x1520


#define W4_BASE0_OFF 0x2400
#define W4_BASE1_OFF 0x2408
#define W4_BASE2_OFF 0x2410
#define W4_BASE3_OFF 0x2418
#define W4_BASE4_OFF 0x2420
#define W4_BASE5_OFF 0x2428
#define W4_BASE6_OFF 0x2430
#define W4_BASE7_OFF 0x2438

#define W4_MASK0_OFF 0x2440
#define W4_MASK1_OFF 0x2448
#define W4_MASK2_OFF 0x2450
#define W4_MASK3_OFF 0x2458
#define W4_MASK4_OFF 0x2460
#define W4_MASK5_OFF 0x2468
#define W4_MASK6_OFF 0x2470
#define W4_MASK7_OFF 0x2478

#define W4_MMAP0_OFF 0x2480
#define W4_MMAP1_OFF 0x2488
#define W4_MMAP2_OFF 0x2490
#define W4_MMAP3_OFF 0x2498
#define W4_MMAP4_OFF 0x24a0
#define W4_MMAP5_OFF 0x24a8
#define W4_MMAP6_OFF 0x24b0
#define W4_MMAP7_OFF 0x24b8


#define w5_BASE0_OFF 0x2500
#define w5_BASE1_OFF 0x2508
#define w5_BASE2_OFF 0x2510
#define w5_BASE3_OFF 0x2518
#define w5_BASE4_OFF 0x2520
#define w5_BASE5_OFF 0x2528
#define w5_BASE6_OFF 0x2530
#define w5_BASE7_OFF 0x2538

#define w5_MASK0_OFF 0x2540
#define w5_MASK1_OFF 0x2548
#define w5_MASK2_OFF 0x2550
#define w5_MASK3_OFF 0x2558
#define w5_MASK4_OFF 0x2560
#define w5_MASK5_OFF 0x2568
#define w5_MASK6_OFF 0x2570
#define w5_MASK7_OFF 0x2578

#define w5_MMAP0_OFF 0x2580
#define w5_MMAP1_OFF 0x2588
#define w5_MMAP2_OFF 0x2590
#define w5_MMAP3_OFF 0x2598
#define w5_MMAP4_OFF 0x25a0
#define w5_MMAP5_OFF 0x25a8
#define w5_MMAP6_OFF 0x25b0
#define w5_MMAP7_OFF 0x25b8

#define PCI_HEAD20_OFF 0x3000


#define PCI_HEAD30_OFF 0x3040


#define PCI_HEAD31_OFF 0x3080


#define PCI_HEAD40_OFF 0x30c0


#define PCI_HEAD41_OFF 0x3100


#define PCI_HEAD42_OFF 0x3140


#define PCI_HEAD50_OFF 0x3180


#define PCI_HEAD60_OFF 0x31c0


#define PCI_HEAD70_OFF 0x3200


#define PCI_HEAD80_OFF 0x3240


#define PCI_HEADF0_OFF 0x32c0


#define PCI_CFG20_OFF 0x3800
#define PCI_CFG30_OFF 0x3808
#define PCI_CFG31_OFF 0x3810
#define PCI_CFG40_OFF 0x3818
#define PCI_CFG41_OFF 0x3820
#define PCI_CFG42_OFF 0x3828
#define PCI_CFG50_OFF 0x3830
#define PCI_CFG60_OFF 0x3838
#define PCI_CFG70_OFF 0x3840
#define PCI_CFG80_OFF 0x3848
#define PCI_CFGF0_OFF 0x3850

#define CHIP_ID_OFF 0x3ff8
/* end of conf bus */

#define APB_BASE	0x1fe00000

#define UART0_OFF	0x0
#define UART1_OFF	0x100
#define UART2_OFF	0x200
#define UART3_OFF	0x300
#define UART4_OFF	0x400
#define UART5_OFF	0x500
#define UART6_OFF	0x600
#define UART7_OFF	0x700
#define UART8_OFF	0x800
#define UART9_OFF	0x900
#define UARTA_OFF	0xa00
#define UARTB_OFF	0xb00

#define I2C0_OFF	0x1000
#define I2C1_OFF	0x1800

#define PWM_OFF		0x2000

#define HPET_OFF	0x4000
#define AC97_OFF	0x5000
#define NAND_OFF	0x6000

#define ACPI_OFF	0x7000

#define RTC_OFF		0x7800

#define DES_OFF		0x8000
#define AES_OFF		0x9000
#define RSA_OFF		0xa000
#define RNG_OFF		0xb000
#define SDIO_OFF	0xc000
#define I2S_OFF		0xd000


#define IPI_BASE_OF(i)	CKSEG1ADDR((CONF_BASE+0x100*i))

#define IPI_OFF_STATUS 		0x1000
#define IPI_OFF_ENABLE 		0x1004
#define IPI_OFF_SET	 	0x1008
#define IPI_OFF_CLEAR		0x100c
#define IPI_OFF_MAILBOX0	0x1020
#define IPI_OFF_MAILBOX1	0x1028
#define IPI_OFF_MAILBOX2	0x1030
#define IPI_OFF_MAILBOX3	0x1038


#endif  /*__2K1000_H_  */
