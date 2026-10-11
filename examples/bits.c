/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

static int popcount(uint64_t x) {
	int n = 0;
	while (x) {
		x &= x - 1;
		n++;
	}
	return n;
}

static int clz32(uint32_t x) {
	int n = 0;
	if (!x) return 32;
	while (!(x & 0x80000000u)) {
		x <<= 1;
		n++;
	}
	return n;
}

static uint32_t reverse_bits(uint32_t x) {
	x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
	x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
	x = ((x >> 4) & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
	x = ((x >> 8) & 0x00ff00ffu) | ((x & 0x00ff00ffu) << 8);
	return (x >> 16) | (x << 16);
}

static uint32_t rotl(uint32_t x, int k) {
	return (x << k) | (x >> (32 - k));
}

static void binary(const char *label, uint32_t x) {
	printf("%-14s ", label);
	for (int i = 31; i >= 0; i--) {
		putchar(x >> i & 1 ? '1' : '0');
		if (i && i % 8 == 0) putchar('_');
	}
	printf("  0x%08x\n", x);
}

int main(void) {
	uint32_t x = 0xdeadbeef;
	binary("x", x);
	binary("~x", ~x);
	binary("x >> 4", x >> 4);
	binary("x << 4", x << 4);
	binary("rotl(x, 8)", rotl(x, 8));
	binary("reverse(x)", reverse_bits(x));
	binary("x & -x", x & -x);
	binary("x & (x - 1)", x & (x - 1));
	printf("popcount(x) = %d, clz(x >> 9) = %d\n", popcount(x), clz32(x >> 9));
	printf("popcount(UINT64_MAX) = %d\n", popcount(UINT64_MAX));
	for (uint32_t v = 1; v <= 1024; v *= 4) printf("%u is a power of two: %d\n", v, (v & (v - 1)) == 0);
	printf("12 is a power of two: %d\n", (12 & 11) == 0);
	int a = -17;
	printf("-17 >> 2 = %d, -17 / 4 = %d, -17 %% 4 = %d\n", a >> 2, a / 4, a % 4);
	printf("(unsigned)-1 = %u, (unsigned char)300 = %d, (signed char)200 = %d\n", (unsigned)-1, (unsigned char)300, (signed char)200);
	printf("INT_MAX = %d, INT_MIN = %d, LLONG_MAX = %lld\n", INT_MAX, INT_MIN, LLONG_MAX);
	printf("UINT_MAX + 1u = %u, 0u - 1 = %u\n", UINT_MAX + 1u, 0u - 1);
	uint8_t byte = 0xa5;
	printf("byte 0x%02x swapped nibbles 0x%02x\n", byte, (uint8_t)((byte << 4) | (byte >> 4)));
	uint32_t flags = 0;
	flags |= 1u << 3;
	flags |= 1u << 7;
	flags ^= 1u << 3;
	flags |= 1u << 31;
	printf("flags = 0x%08x, bit 7 set: %d, bit 3 set: %d\n", flags, !!(flags & (1u << 7)), !!(flags & (1u << 3)));
	return 0;
}
