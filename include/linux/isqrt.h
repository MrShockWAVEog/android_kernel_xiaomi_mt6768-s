/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ISQRT_H
#define _LINUX_ISQRT_H
/*
 * Fast Integer Square Root Library (isqrt)
 * Version: 1.0
 * Copyright (C) 2026 Masahito Suzuki
 *
 * @file isqrt.h
 * @brief A C99 header-only library for computing exact floor(sqrt(v))
 *        for unsigned integers, faster than loop-based algorithms.
 *
 * @details
 * This library provides high-performance integer square root functions
 * optimized for platforms without a Floating-Point Unit (FPU) or where
 * FPU usage is prohibitively expensive (e.g., within a kernel context).
 *
 * It serves as a faster, drop-in replacement for loop-based algorithms
 * such as the Linux kernel's int_sqrt (Guy L. Steele shift-and-subtract).
 *
 * Four functions are provided, each using the optimal strategy for its
 * bit-width:
 *
 *   isqrt8(v)  — u8  -> u8   (~ 1 cy, pure LUT)
 *   isqrt16(v) — u16 -> u16  (~ 7 cy, 0 divisions)
 *   isqrt32(v) — u32 -> u32  (~10 cy, 1 division)
 *   isqrt64(v) — u64 -> u64  (~38 cy, 2 divisions)
 *
 * All functions return exact floor(sqrt(v)). Decode is simply h * h
 * (a single integer multiply), requiring no special decoder.
 *
 * --- Strategies ---
 *
 * 16-bit: A CLZ-based zone encoding produces a fast initial estimate.
 *   A 256-byte delta correction table (indexed by the estimate) adjusts
 *   it to within +/-1 of the true value. A single multiply-compare
 *   fixup resolves the result exactly. No divisions are performed.
 *
 * 32/64-bit: A shared 128-entry sqrt lookup table (256 bytes), indexed
 *   by exponent parity and top 6 mantissa bits, provides an initial
 *   estimate with ~0.8% accuracy. Newton-Raphson iterations (1 for
 *   32-bit, 2 for 64-bit) converge to within a small delta, which the
 *   fixup resolves. The 32/64-bit functions are generated from a common
 *   macro (_ISQRT_DEFINE) parameterized by bit-width and NR count.
 *
 * --- Performance vs int_sqrt ---
 *
 *   int_sqrt is loop-based (N/2 iterations for N-bit input), so its
 *   cost grows linearly with bit-width. isqrt uses a constant-time
 *   CLZ + LUT estimate followed by a fixed number of NR steps, giving
 *   consistent performance regardless of input value.
 *
 *   | Width | int_sqrt | isqrt  | Speedup |
 *   |-------|----------|--------|---------|
 *   |   u8  |   N/A    |  ~1 cy |  (LUT)  |
 *   |  u16  |  ~15 cy  |  ~7 cy |  x2.2   |
 *   |  u32  |  ~80 cy  | ~10 cy |  x8.0   |
 *   |  u64  | ~115 cy  | ~38 cy |  x3.0   |
 *
 * @note
 * This library relies on GCC/Clang-specific built-in functions:
 *   __builtin_clz()   (32-bit count leading zeros)
 *   __builtin_clzll()  (64-bit count leading zeros)
 * Porting to other compilers requires providing equivalents
 * (e.g., MSVC's _BitScanReverse / _BitScanReverse64).
 */

#include <linux/types.h>

/* ================================================================ */
/* 8-bit: full LUT (256 bytes, 0 computation)                       */
/* ================================================================ */

/**
 * @brief Full lookup table for 8-bit integer square root.
 *
 * Stores exact floor(sqrt(i)) for i = 0..255. Output range is 0..15.
 * At 256 bytes this fits comfortably in L1 cache, making a direct
 * table lookup the fastest possible implementation.
 */
static const u8 _isqrt8_lut[256] = {
	 0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3,
	 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5,
	 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	 9, 9, 9, 9,10,10,10,10,10,10,10,10,10,10,10,10,
	10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,
	11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
	12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,
	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
	13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,
	14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
	14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
};

/**
 * @brief Computes exact floor(sqrt(v)) for u8.
 *
 * Pure table lookup — the fastest possible implementation.
 * Cost: ~1 cycle (single indexed memory load).
 *
 * @param v The input value (0..255).
 * @return Exact floor(sqrt(v)) as u8 (0..15).
 */
static inline u8 isqrt8(u8 v)
{
	return _isqrt8_lut[v];
}

/* ================================================================ */
/* 16-bit: zone encoding + delta LUT + fixup (0 divisions)          */
/* ================================================================ */

/**
 * @brief Delta correction table for the 16-bit zone-based estimate.
 *
 * The zone encoding always overestimates floor(sqrt(v)). This 256-byte
 * table, indexed by the zone-encoded estimate h (0..255), corrects it
 * to within +/-1 of the true value. Values range from -11 to 0.
 */
static const int8_t _isqrt16_delta_lut[256] = {
	  0,  0,  0, -1,  0, -1, -1, -1,  0, -1, -1, -1, -1, -1, -1, -1,
	  0, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -1, -1, -1, -1, -1,
	  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3,
	 -3, -3, -2, -2, -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2,
	 -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -5, -5, -5, -5, -6,
	 -6, -5, -5, -5, -4, -4, -4, -3, -3, -3, -3, -2, -2, -2, -2, -2,
	 -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -4,
	 -4, -4, -4, -4, -4, -5, -5, -5, -5, -6, -6, -6, -6, -6, -7, -7,
	 -7, -7, -8, -8, -8, -8, -9, -9, -9, -9,-10,-10,-10,-11,-11,-11,
	-11,-11,-10,-10,-10, -9, -9, -9, -8, -8, -8, -7, -7, -7, -6, -6,
	 -6, -6, -5, -5, -5, -5, -4, -4, -4, -4, -4, -3, -3, -3, -3, -3,
	 -3, -3, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/**
 * @brief Computes exact floor(sqrt(v)) for u16.
 *
 * Uses CLZ-based zone encoding for a fast initial estimate, a 256-byte
 * delta correction table, and a single multiply-compare fixup.
 * Cost: ~7 cycles, 0 divisions.
 *
 * @param v The input value (0..65535).
 * @return Exact floor(sqrt(v)) as u16.
 */
static inline u16 isqrt16(u16 v)
{
	u32 e;
	u32 zone;
	u32 msb;
	u32 sub_bits;
	u32 sub_m;
	u32 h;
	u32 y;
	if (v <= 1) return v;
	e = 31 - __builtin_clz((u32)v);
	zone = e >> 1;
	if (!zone) return 1;
	msb = e & 1;
	sub_bits = zone - 1;
	sub_m = (v >> (e - sub_bits)) & ((1u << sub_bits) - 1);
	h = (1u << zone) | (msb << sub_bits) | sub_m;
	y = h + _isqrt16_delta_lut[h];
	if ((y + 1) * (y + 1) <= (u32)v) y++;
	if (y > 0 && y * y > (u32)v) y--;
	return (u16)y;
}

/* ================================================================ */
/* Shared sqrt LUT for 32/64-bit initial estimates (256 bytes)      */
/* ================================================================ */

/**
 * @brief Initial sqrt estimate table shared by isqrt32 and isqrt64.
 *
 * Indexed by (exponent_parity << 6 | top_6_mantissa_bits).
 * Each entry stores floor(sqrt((1 or 2) * (1 + i/64)) * 256):
 *   entries 0..63:   even exponent — sqrt(1 + i/64) * 256
 *   entries 64..127: odd exponent  — sqrt(2 * (1 + i/64)) * 256
 *
 * The initial estimate is computed as:
 *   y = _isqrt_est_lut[index] << (exponent / 2) >> 8
 * giving ~0.8% accuracy, sufficient for Newton-Raphson convergence.
 */
static const u16 _isqrt_est_lut[128] = {
	256,257,259,261,263,265,267,269,271,273,275,277,278,280,282,284,
	286,288,289,291,293,295,296,298,300,301,303,305,306,308,310,311,
	313,315,316,318,320,321,323,324,326,327,329,331,332,334,335,337,
	338,340,341,343,344,346,347,349,350,352,353,354,356,357,359,360,
	362,364,367,370,373,375,378,381,384,386,389,391,394,397,399,402,
	404,407,409,412,414,417,419,422,424,426,429,431,434,436,438,441,
	443,445,448,450,452,454,457,459,461,463,465,468,470,472,474,476,
	478,481,483,485,487,489,491,493,495,497,499,501,503,505,507,509,
};

/* ================================================================ */
/* 32/64-bit: CLZ + sqrt LUT + Newton-Raphson + fixup               */
/* ================================================================ */

/**
 * @brief Internal helper: count leading zeros for 32 or 64-bit values.
 */
#define _isqrt_clz(v, bits) ((bits) == 64 ? \
	(unsigned)__builtin_clzll((u64)(v)) : \
	(unsigned)__builtin_clz((u32)(v)))

/**
 * @brief Generates isqrt{bits}() using CLZ + LUT + Newton-Raphson.
 *
 * @param bits  Bit-width of the input/output type (32 or 64).
 * @param nr    Number of Newton-Raphson iterations.
 *              1 for 32-bit (~10 cy), 2 for 64-bit (~38 cy).
 *
 * The generated function computes exact floor(sqrt(v)) by:
 * 1. CLZ to extract the exponent e = floor(log2(v)).
 * 2. LUT lookup using exponent parity + top 6 mantissa bits.
 * 3. Newton-Raphson: y = (y + v/y) / 2, repeated `nr` times.
 * 4. Clamp to the maximum valid result (2^(bits/2) - 1).
 * 5. Fixup: adjust +/-1 by multiply-compare until y^2 <= v < (y+1)^2.
 */
#define _ISQRT_DEFINE(bits, nr) \
static inline u##bits isqrt##bits(u##bits v) \
{\
	int _i; \
	u##bits e, half_e, frac6, y, max_y; \
	\
	if (v <= 1) return v; \
	e = (bits) - 1 - _isqrt_clz(v, bits); \
	half_e = e >> 1; \
	frac6 = (e >= 6) ? (v >> (e - 6)) & 0x3F : (v << (6 - e)) & 0x3F; \
	y = (u##bits)_isqrt_est_lut[((e & 1) << 6) | frac6] << half_e >> 8; \
	y |= !y; \
	for (_i = 0; _i < (nr); _i++) \
		y = (y + v / y) >> 1; \
	max_y = ((u##bits)1 << ((bits) / 2)) - 1; \
	if (y > max_y) y = max_y; \
	while (y > 0 && y * y > v) y--; \
	while ((y + 1) * (y + 1) <= v && y < max_y) y++; \
	return y; \
}

/** @brief Computes exact floor(sqrt(v)) for u32. ~10 cy, 1 division. */
_ISQRT_DEFINE(32, 1)
/** @brief Computes exact floor(sqrt(v)) for u64. ~38 cy, 2 divisions. */
_ISQRT_DEFINE(64, 2)
#endif /* _LINUX_ISQRT_H */
