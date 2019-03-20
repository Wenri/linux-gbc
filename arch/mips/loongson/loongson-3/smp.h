/* for Loongson-3A smp support */
extern unsigned long long smp_group[4];

/* 4 groups(nodes) in maximum in numa case */
#define smp_core_group0_base	(smp_group[0])
#define smp_core_group1_base	(smp_group[1])
#define smp_core_group2_base	(smp_group[2])
#define smp_core_group3_base	(smp_group[3])

/* 4 cores in each group(node) */
#define smp_core0_offset  0x000
#define smp_core1_offset  0x100
#define smp_core2_offset  0x200
#define smp_core3_offset  0x300

/* ipi registers offsets */
#define STATUS0  0x00
#define EN0      0x04
#define SET0     0x08
#define CLEAR0   0x0c
#define STATUS1  0x10
#define MASK1    0x14
#define SET1     0x18
#define CLEAR1   0x1c
#define BUF      0x20

/* CPU ipi csr offset */
#define LOONGSON_IPI_STATUS_OFFSET	0x00001000
#define LOONGSON_IPI_EN_OFFSET		0x00001004
#define LOONGSON_IPI_CLEAR_OFFSET	0x0000100C
#define LOONGSON_IPI_SEND_OFFSET	0x00001040
#define LOONGSON_IPI_SEND_BLOCK_SHIFT	31
#define LOONGSON_IPI_SEND_CPU_SHIFT	16
#define LOONGSON_MAIL_BUF_OFFSET	0x00001020
#define LOONGSON_MAIL_SEND_OFFSET	0x00001048
#define LOONGSON_MAIL_SEND_BLOCK_SHIFT	31
#define LOONGSON_MAIL_SEND_CPU_SHIFT	16
#define LOONGSON_MAIL_SEND_BOX_SHIFT	2
#define LOONGSON_MAIL_SEND_BOX_TO_LOW(box)	(box << 1)
#define LOONGSON_MAIL_SEND_BOX_TO_HIGHT(box)	((box << 1) + 1)
#define LOONGSON_MAIL_SEND_VAL_SHIFT	32
#define LOONGSON_MAIL_SEND_H32_MASK	0xFFFFFFFF00000000ULL

