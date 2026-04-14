// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Davidlohr Bueso <davidlohr.bueso@hp.com>
 *
 * Fast exact integer square root based on firelzrd's isqrt (CLZ + LUT +
 * Newton-Raphson), replacing the Guy L. Steele shift-and-subtract
 * implementation.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/isqrt.h>

/**
 * int_sqrt - rough approximation to sqrt
 * @x: integer of which to calculate the sqrt
 *
 * A very rough approximation to the sqrt() function.
 */
unsigned long int_sqrt(unsigned long x)
{
	if (x <= 1)
		return x;

#if BITS_PER_LONG == 64
	return (unsigned long)isqrt64(x);
#else
	return (unsigned long)isqrt32(x);
#endif
}
EXPORT_SYMBOL(int_sqrt);
