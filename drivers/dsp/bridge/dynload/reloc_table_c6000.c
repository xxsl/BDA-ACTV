/*
 * reloc_table_c6000.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */


/* Tables generated for c6000 */

#define HASH_FUNC(zz) (((((zz) + 1) * UINT32_C(1845)) >> 11) & 63)
#define HASH_L(zz) ((zz) >> 8)
#define HASH_I(zz) ((zz) & 0xFF)

static const u16 rop_map1[] = {
	0,
	1,
	2,
	20,
	4,
	5,
	6,
	15,
	80,
	81,
	82,
	83,
	84,
	85,
	86,
	87,
	17,
	18,
	19,
	21,
	16,
	16394,
	16404,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	32,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	40,
	112,
	113,
	65535,
	16384,
	16385,
	16386,
	16387,
	16388,
	16389,
	16390,
	16391,
	16392,
	16393,
	16395,
	16396,
	16397,
	16398,
	16399,
	16400,
	16401,
	16402,
	16403,
	16405,
	16406,
	65535,
	65535,
	65535
};

static const s16 rop_map2[] = {
	-256,
	-255,
	-254,
	-245,
	-253,
	-252,
	-251,
	-250,
	-241,
	-240,
	-239,
	-238,
	-237,
	-236,
	1813,
	5142,
	-248,
	-247,
	778,
	-244,
	-249,
	-221,
	-211,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-243,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-242,
	-233,
	-232,
	-1,
	-231,
	-230,
	-229,
	-228,
	-227,
	-226,
	-225,
	-224,
	-223,
	5410,
	-220,
	-219,
	-218,
	-217,
	-216,
	-215,
	-214,
	-213,
	5676,
	-210,
	-209,
	-1,
	-1,
	-1
};

static const u16 rop_action[] = {
	2560,
	2304,
	2304,
	2432,
	2432,
	2560,
	2176,
	2304,
	2560,
	3200,
	3328,
	3584,
	3456,
	2304,
	4208,
	20788,
	21812,
	3415,
	3245,
	2311,
	4359,
	19764,
	2311,
	3191,
	3280,
	6656,
	7680,
	8704,
	9728,
	10752,
	11776,
	12800,
	13824,
	14848,
	15872,
	16896,
	17920,
	18944,
	0,
	0,
	0,
	0,
	1536,
	1536,
	1536,
	5632,
	512,
	0
};

static const u16 rop_info[] = {
	0,
	35,
	35,
	35,
	35,
	35,
	35,
	35,
	35,
	39,
	39,
	39,
	39,
	35,
	34,
	283,
	299,
	4135,
	4391,
	291,
	33059,
	283,
	295,
	4647,
	4135,
	64,
	64,
	128,
	64,
	64,
	64,
	64,
	64,
	64,
	64,
	64,
	64,
	128,
	201,
	197,
	74,
	70,
	208,
	196,
	200,
	192,
	192,
	66
};
