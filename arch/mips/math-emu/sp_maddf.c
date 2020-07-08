/*
 * IEEE754 floating point arithmetic
 * single precision: MADDF.f (Fused Multiply Add)
 * MADDF.fmt: FPR[fd] = FPR[fr] + (FPR[fs] x FPR[ft])
 *
 * MIPS floating point support
 * Copyright (C) 2015 Imagination Technologies, Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; version 2 of the License.
 */

#include "ieee754sp.h"
#define UINT64_C(x) x##ULL
#define SP_FBITS 23
int dclz(unsigned long int a0);

static ieee754sp _ieee754sp_maddf(ieee754sp z, ieee754sp x,
				ieee754sp y, int opt)
{
	int re;
	int rs;
#if defined (CONFIG_MACH_LOONGSON) || defined (CONFIG_MACH_LOONGSON2)
	u64 rm64;
	u64 zm64;
	unsigned rm;
#else
	unsigned rm;
	unsigned short lxm;
	unsigned short hxm;
	unsigned short lym;
	unsigned short hym;
	unsigned lrm;
	unsigned hrm;

	unsigned t;
	unsigned at;
#endif
	int s;

	COMPXSP;
	COMPYSP;
	u32 zm; int ze; int zs __maybe_unused; int zc;

	EXPLODEXSP;
	EXPLODEYSP;
	EXPLODESP(z, zc, zs, ze, zm)

	FLUSHXSP;
	FLUSHYSP;
	FLUSHSP(z, zc, zs, ze, zm);

	CLEARCX;

	switch (zc) {
	case IEEE754_CLASS_SNAN:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754sp_nanxcpt(z, "maddf.s");
	case IEEE754_CLASS_DNORM:
		SPDNORMx(zm, ze);
	/* QNAN is handled separately below */
	}

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754sp_nanxcpt(y, "maddf.s");

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754sp_nanxcpt(x, "maddf.s");

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return y;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return x;

	/*
	 * Infinity handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754sp_indef();

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		return ieee754sp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		/* Multiplication is 0 so just return z */
		return z;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		SPDNORMX;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		SPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		SPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		/* fall through to real computations */
	}

	/* Finally get to do some computation */

	/*
	 * Do the multiplication bit first
	 *
	 * rm = xm * ym, re = xe + ye basically
	 *
	 * At this point xm and ym should have been normalized.
	 */

	/* rm = xm * ym, re = xe+ye basically */
	assert(xm & SP_HIDDEN_BIT);
	assert(ym & SP_HIDDEN_BIT);

	re = xe + ye;
#if defined (CONFIG_MACH_LOONGSON) || defined (CONFIG_MACH_LOONGSON2)
	rs = xs ^ ys ^ (opt & 2);
	zs = zs ^ (opt & 1) ^ (opt & 2);

	re = xe + ye;

	/* Multiple 24 bit xm and ym to give 48 bit results */
	rm64 = (uint64_t)xm * ym;

	/* Shunt to top of word */
	rm64 = rm64 << 16;

	/* Put explicit bit at bit 62 if necessary */
	if ((int64_t) rm64 < 0) {
		rm64 = rm64 >> 1;
		re++;
	}

	assert(rm64 & (1 << 62));

	if (zc == IEEE754_CLASS_ZERO) {
		/*
		 * Move explicit bit from bit 62 to bit 26 since the
		 * ieee754sp_format code expects the mantissa to be
		 * 27 bits wide (24 + 3 rounding bits).
		 */
		rm = XSPSRS64(rm64, (62 - 26));
		return ieee754sp_format(rs, re, rm);
	}

	/* Move explicit bit from bit 23 to bit 62 */
	zm64 = (uint64_t)zm << (62 - 23);
	assert(zm64 & (1 << 62));

	/* Make the exponents the same */
	if (ze > re) {
		/*
		 * Have to shift r fraction right to align.
		 */
		s = ze - re;
		rm64 = XSPSRS64(rm64, s);
		re += s;
	} else if (re > ze) {
		/*
		 * Have to shift z fraction right to align.
		 */
		s = re - ze;
		zm64 = XSPSRS64(zm64, s);
		ze += s;
	}
	assert(ze == re);
	assert(ze <= SP_EMAX);

	/* Do the addition */
	if (zs == rs) {
		/*
		 * Generate 64 bit result by adding two 63 bit numbers
		 * leaving result in zm64, zs and ze.
		 */
		zm64 = zm64 + rm64;
		if ((int64_t)zm64 < 0) {	/* carry out */
			zm64 = XSPSRS1(zm64);
			ze++;
		}
	} else {
		if (zm64 >= rm64) {
			zm64 = zm64 - rm64;
		} else {
			zm64 = rm64 - zm64;
			zs = rs;
		}
		if (zm64 == 0)
			return ieee754sp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Put explicit bit at bit 62 if necessary.
		 */
		while ((zm64 >> 62) == 0) {
			zm64 <<= 1;
			ze--;
		}
	}

	/*
	 * Move explicit bit from bit 62 to bit 26 since the
	 * ieee754sp_format code expects the mantissa to be
	 * 27 bits wide (24 + 3 rounding bits).
	 */
	zm = XSPSRS64(zm64, (62 - 26));

	return ieee754sp_format(zs, ze, zm);
#else
	rs = xs ^ ys;

	/* shunt to top of word */
	xm <<= 32 - (SP_FBITS + 1);
	ym <<= 32 - (SP_FBITS + 1);

	/*
	 * Multiply 32 bits xm, ym to give high 32 bits rm with stickness.
	 */
	lxm = xm & 0xffff;
	hxm = xm >> 16;
	lym = ym & 0xffff;
	hym = ym >> 16;

	lrm = lxm * lym;	/* 16 * 16 => 32 */
	hrm = hxm * hym;	/* 16 * 16 => 32 */

	t = lxm * hym; /* 16 * 16 => 32 */
	at = lrm + (t << 16);
	hrm += at < lrm;
	lrm = at;
	hrm = hrm + (t >> 16);

	t = hxm * lym; /* 16 * 16 => 32 */
	at = lrm + (t << 16);
	hrm += at < lrm;
	lrm = at;
	hrm = hrm + (t >> 16);

	rm = hrm | (lrm != 0);


	/*
	 * Sticky shift down to normal rounding precision.
	 */
	if ((int) rm < 0) {
		rm = (rm >> (32 - (SP_FBITS + 1 + 3))) |
		    ((rm << (SP_FBITS + 1 + 3)) != 0);
		re++;
	} else {
		rm = (rm >> (32 - (SP_FBITS + 1 + 3 + 1))) |
		     ((rm << (SP_FBITS + 1 + 3 + 1)) != 0);
	}
	assert(rm & (SP_HIDDEN_BIT << 3));

	/* And now the addition */

	assert(zm & SP_HIDDEN_BIT);

	/*
	 * Provide guard,round and stick bit space.
	 */
	zm <<= 3;

	if (ze > re) {
		/*
		 * Have to shift y fraction right to align.
		 */
		s = ze - re;
		SPXSRSYn(s);
	} else if (re > ze) {
		/*
		 * Have to shift x fraction right to align.
		 */
		s = re - ze;
		SPXSRSYn(s);
	}
	assert(ze == re);
	assert(ze <= SP_EMAX);

	if (zs == rs) {
		/*
		 * Generate 28 bit result of adding two 27 bit numbers
		 * leaving result in zm, zs and ze.
		 */
		zm = zm + rm;

		if (zm >> (SP_FBITS + 1 + 3)) { /* carry out */
			SPXSRSX1();
		}
	} else {
		if (zm >= rm) {
			zm = zm - rm;
		} else {
			zm = rm - zm;
			zs = rs;
		}
		if (zm == 0)
			return ieee754sp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Normalize in extended single precision
		 */
		while ((zm >> (SP_MBITS + 3)) == 0) {
			zm <<= 1;
			ze--;
		}

	}
	return ieee754sp_format(zs, ze, zm);
#endif
}

ieee754sp ieee754sp_maddf(ieee754sp z, ieee754sp x,
				ieee754sp y)
{
	return 	_ieee754sp_maddf(z, x, y, 0);
}

ieee754sp ieee754sp_nmaddf(ieee754sp z, ieee754sp x,
				ieee754sp y)
{
	return 	_ieee754sp_maddf(z, x, y, 1);
}
ieee754sp ieee754sp_msubf(ieee754sp z, ieee754sp x,
				ieee754sp y)
{
	return 	_ieee754sp_maddf(z, x, y, 2);
}

ieee754sp ieee754sp_nmsubf(ieee754sp z, ieee754sp x,
				ieee754sp y)
{
	return 	_ieee754sp_maddf(z, x, y, 3);
}
