/*
 * IEEE754 floating point arithmetic
 * double precision: MADDF.f (Fused Multiply Add)
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

#include "ieee754dp.h"
#define UINT64_C(x) x##ULL
#define DP_FBITS 52

/* 128 bits shift right logical with rounding. */
void srl128(u64 *hptr, u64 *lptr, int count)
{
	u64 low;

	if (count >= 128) {
		*lptr = *hptr != 0 || *lptr != 0;
		*hptr = 0;
	} else if (count >= 64) {
		if (count == 64) {
			*lptr = *hptr | (*lptr != 0);
		} else {
			low = *lptr;
			*lptr = *hptr >> (count - 64);
			*lptr |= (*hptr << (128 - count)) != 0 || low != 0;
		}
		*hptr = 0;
	} else {
		low = *lptr;
		*lptr = low >> count | *hptr << (64 - count);
		*lptr |= (low << (64 - count)) != 0;
		*hptr = *hptr >> count;
	}
}

static ieee754dp _ieee754dp_maddf(ieee754dp z, ieee754dp x,
				ieee754dp y, int opt)
{
	int re;
	int rs;
	unsigned lxm;
	unsigned hxm;
	unsigned lym;
	unsigned hym;
	u64 lrm;
	u64 hrm;
	u64 t;
	u64 at;
	int s;

	COMPXDP;
	COMPYDP;

	u64 zm; int ze; int zs __maybe_unused; int zc;
	u64 lzm = 0;
	u64 hzm = 0;

	EXPLODEXDP;
	EXPLODEYDP;
	EXPLODEDP(z, zc, zs, ze, zm)

	FLUSHXDP;
	FLUSHYDP;
	FLUSHDP(z, zc, zs, ze, zm);

	CLEARCX;

	switch (zc) {
	case IEEE754_CLASS_SNAN:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754dp_nanxcpt(z, "maddf.d");
	case IEEE754_CLASS_DNORM:
		DPDNORMx(zm, ze);
	/* QNAN is handled separately below */
	}

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754dp_nanxcpt(y, "maddf.d");

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754dp_nanxcpt(x, "maddf.d");

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
		return ieee754dp_indef();

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		return ieee754dp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		/* Multiplication is 0 so just return z */
		return z;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		DPDNORMX;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
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
	assert(xm & DP_HIDDEN_BIT);
	assert(ym & DP_HIDDEN_BIT);

	re = xe + ye;
#if defined (CONFIG_MACH_LOONGSON) || defined (CONFIG_MACH_LOONGSON2)
	rs = xs ^ ys ^ (opt & 2);
	zs = zs ^ (opt & 1) ^ (opt & 2);
#else
	rs = xs ^ ys;
#endif

	/* shunt to top of word */
	xm <<= 64 - (DP_FBITS + 1);
	ym <<= 64 - (DP_FBITS + 1);
	/*
	 * Multiply 32 bits xm, ym to give high 32 bits rm with stickness.
	 */

	/* 32 * 32 => 64 */
#define DPXMULT(x, y)	((u64)(x) * (u64)y)

	lxm = xm;
	hxm = xm >> 32;
	lym = ym;
	hym = ym >> 32;

	lrm = DPXMULT(lxm, lym);
	hrm = DPXMULT(hxm, hym);

	t = DPXMULT(lxm, hym);

	at = lrm + (t << 32);
	hrm += at < lrm;
	lrm = at;

	hrm = hrm + (t >> 32);

	t = DPXMULT(hxm, lym);

	at = lrm + (t << 32);
	hrm += at < lrm;
	lrm = at;

	hrm = hrm + (t >> 32);
#if defined (CONFIG_MACH_LOONGSON) || defined (CONFIG_MACH_LOONGSON2)
	/* Put explicit bit at bit 126 if necessary */
	if ((int64_t)hrm < 0) {
		lrm = (hrm << 63) | (lrm >> 1);
		hrm = hrm >> 1;
		re++;
	}

	assert(hrm & (1 << 62));

	if (zc == IEEE754_CLASS_ZERO) {
		/*
		 * Move explicit bit from bit 126 to bit 55 since the
		 * ieee754dp_format code expects the mantissa to be
		 * 56 bits wide (53 + 3 rounding bits).
		 */
		srl128(&hrm, &lrm, (126 - 55));
		return ieee754dp_format(rs, re, lrm);
	}

	/* Move explicit bit from bit 52 to bit 126 */
	lzm = 0;
	hzm = zm << 10;
	assert(hzm & (1 << 62));

	/* Make the exponents the same */
	if (ze > re) {
		/*
		 * Have to shift y fraction right to align.
		 */
		s = ze - re;
		srl128(&hrm, &lrm, s);
		re += s;
	} else if (re > ze) {
		/*
		 * Have to shift x fraction right to align.
		 */
		s = re - ze;
		srl128(&hzm, &lzm, s);
		ze += s;
	}
	assert(ze == re);
	assert(ze <= DP_EMAX);

	/* Do the addition */
	if (zs == rs) {
		/*
		 * Generate 128 bit result by adding two 127 bit numbers
		 * leaving result in hzm:lzm, zs and ze.
		 */
		hzm = hzm + hrm + (lzm > (lzm + lrm));
		lzm = lzm + lrm;
		if ((int64_t)hzm < 0) {        /* carry out */
			srl128(&hzm, &lzm, 1);
			ze++;
		}
	} else {
		if (hzm > hrm || (hzm == hrm && lzm >= lrm)) {
			hzm = hzm - hrm - (lzm < lrm);
			lzm = lzm - lrm;
		} else {
			hzm = hrm - hzm - (lrm < lzm);
			lzm = lrm - lzm;
			zs = rs;
		}
		if (lzm == 0 && hzm == 0)
			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Put explicit bit at bit 126 if necessary.
		 */
		if (hzm == 0) {
			/* left shift by 63 or 64 bits */
			if ((int64_t)lzm < 0) {
				/* MSB of lzm is the explicit bit */
				hzm = lzm >> 1;
				lzm = lzm << 63;
				ze -= 63;
			} else {
				hzm = lzm;
				lzm = 0;
				ze -= 64;
			}
		}

		t = 0;
		while ((hzm >> (62 - t)) == 0)
			t++;

		assert(t <= 62);
		if (t) {
			hzm = hzm << t | lzm >> (64 - t);
			lzm = lzm << t;
			ze -= t;
		}
	}

	/*
	 * Move explicit bit from bit 126 to bit 55 since the
	 * ieee754dp_format code expects the mantissa to be
	 * 56 bits wide (53 + 3 rounding bits).
	 */
	srl128(&hzm, &lzm, (126 - 55));

	return ieee754dp_format(zs, ze, lzm);
#else
	rm = hrm | (lrm != 0);

	/*
	 * Sticky shift down to normal rounding precision.
	 */
	if ((s64) rm < 0) {
		rm = (rm >> (64 - (DP_FBITS + 1 + 3))) |
		     ((rm << (DP_FBITS + 1 + 3)) != 0);
			re++;
	} else {
		rm = (rm >> (64 - (DP_FBITS + 1 + 3 + 1))) |
		     ((rm << (DP_FBITS + 1 + 3 + 1)) != 0);
	}
	assert(rm & (DP_HIDDEN_BIT << 3));

	/* And now the addition */
	assert(zm & DP_HIDDEN_BIT);

	/*
	 * Provide guard,round and stick bit space.
	 */
	zm <<= 3;

	if (ze > re) {
		/*
		 * Have to shift y fraction right to align.
		 */
		s = ze - re;
		rm = XDPSRS(rm, s);
		re += s;
	} else if (re > ze) {
		/*
		 * Have to shift x fraction right to align.
		 */
		s = re - ze;
		zm = XDPSRS(zm, s);
		ze += s;
	}
	assert(ze == re);
	assert(ze <= DP_EMAX);

	if (zs == rs) {
		/*
		 * Generate 28 bit result of adding two 27 bit numbers
		 * leaving result in xm, xs and xe.
		 */
		zm = zm + rm;

		if (zm >> (DP_FBITS + 1 + 3)) { /* carry out */
			zm = XDPSRS1(zm);
			ze++;
		}
	} else {
		if (zm >= rm) {
			zm = zm - rm;
		} else {
			zm = rm - zm;
			zs = rs;
		}
		if (zm == 0)
			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Normalize to rounding precision.
		 */
		while ((zm >> (DP_FBITS + 3)) == 0) {
			zm <<= 1;
			ze--;
		}
	}

	return ieee754dp_format(zs, ze, zm);
#endif
}


ieee754dp ieee754dp_maddf(ieee754dp z, ieee754dp x,
				ieee754dp y)
{
	return 	_ieee754dp_maddf(z, x, y, 0);
}

ieee754dp ieee754dp_nmaddf(ieee754dp z, ieee754dp x,
				ieee754dp y)
{
	return 	_ieee754dp_maddf(z, x, y, 1);
}
ieee754dp ieee754dp_msubf(ieee754dp z, ieee754dp x,
				ieee754dp y)
{
	return 	_ieee754dp_maddf(z, x, y, 2);
}

ieee754dp ieee754dp_nmsubf(ieee754dp z, ieee754dp x,
				ieee754dp y)
{
	return 	_ieee754dp_maddf(z, x, y, 3);
}
