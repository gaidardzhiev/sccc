/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

#define MAXD 3000

typedef struct {
	int len;
	unsigned char d[MAXD];
} Big;

static void big_set(Big *b, int v) {
	b->len = 0;
	do {
		b->d[b->len++] = (unsigned char)(v % 10);
		v /= 10;
	} while (v);
}

static void big_mul(Big *b, int m) {
	int carry = 0;
	for (int i = 0; i < b->len; i++) {
		int t = b->d[i] * m + carry;
		b->d[i] = (unsigned char)(t % 10);
		carry = t / 10;
	}
	while (carry) {
		b->d[b->len++] = (unsigned char)(carry % 10);
		carry /= 10;
	}
}

static void big_add(Big *r, const Big *a, const Big *b) {
	int carry = 0, n = a->len > b->len ? a->len : b->len;
	for (int i = 0; i < n; i++) {
		int t = (i < a->len ? a->d[i] : 0) + (i < b->len ? b->d[i] : 0) + carry;
		r->d[i] = (unsigned char)(t % 10);
		carry = t / 10;
	}
	r->len = n;
	if (carry) r->d[r->len++] = 1;
}

static void big_print(const char *label, const Big *b) {
	printf("%s (%d digits):\n", label, b->len);
	for (int i = b->len - 1, n = 0; i >= 0; i--) {
		putchar('0' + b->d[i]);
		if (++n % 70 == 0 && i) putchar('\n');
	}
	putchar('\n');
}

int main(void) {
	static Big f, a, b, c;
	big_set(&f, 1);
	for (int i = 2; i <= 100; i++) big_mul(&f, i);
	big_print("100!", &f);
	big_set(&f, 1);
	for (int i = 0; i < 256; i++) big_mul(&f, 2);
	big_print("2^256", &f);
	big_set(&a, 0);
	big_set(&b, 1);
	for (int i = 0; i < 500; i++) {
		big_add(&c, &a, &b);
		a = b;
		b = c;
	}
	big_print("fib(500)", &a);
	return 0;
}
