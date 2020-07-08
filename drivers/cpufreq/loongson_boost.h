#ifndef LOONGSON_BOOST_H
#define LOONGSON_BOOST_H

#define FREQ_DIV	(8)

extern int ls_boost_freq;
extern int ls_stable_base_freq;
extern int ls_boost_supported;
extern int ls_boost_cores;
extern int ls_upper_index;
extern const int RESERVED_FREQ;

/* 3a4000 frequency */
enum freq {
	FREQ_LEV0,
	FREQ_LEV1,
	FREQ_LEV2,
	FREQ_LEV3,

	FREQ_LEV4,
	FREQ_LEV5,
	FREQ_LEV6,
	FREQ_LEV7,

	FREQ_LEV8,
	FREQ_LEV9,
	FREQ_LEV10,
	FREQ_LEV11,

	FREQ_LEV12,
	FREQ_LEV13,
	FREQ_LEV14,
	FREQ_LEV15,
};

#endif
