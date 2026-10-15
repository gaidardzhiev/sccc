/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

static long sys(long n, long a, long b, long c, long d) {
	long r;
	register long r10 __asm__("r10") = d;
	__asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
	return r;
}

static long len(const char *s) {
	long n = 0;
	while (s[n]) n++;
	return n;
}

static void put(const char *s) {
	sys(1, 1, (long)s, len(s), 0);
}

static void putnum(long v) {
	char b[24];
	int i = 23;
	b[i] = 0;
	if (!v) b[--i] = '0';
	while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
	put(b + i);
}

void _start(void) {
	put("sccc init: hello from userspace\n");
	put("sccc init: pid ");
	putnum(sys(39, 0, 0, 0, 0));
	put("\n");
	sys(169, 0xfee1dead, 672274793, 0x01234567, 0);
	for (;;) sys(34, 0, 0, 0, 0);
}
