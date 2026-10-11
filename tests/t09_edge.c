/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

struct S3 { char a, b, c; };
struct Big { long v[5]; };
typedef struct { int x; char name[10]; } Rec;

long sum_va(int n, ...) {
	va_list ap;
	va_start(ap, n);
	long s = 0;
	for (int i = 0; i < n; i++) s += va_arg(ap, long);
	va_end(ap);
	return s;
}
int mix_va(const char *fmt, ...) {
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	int r = 0;
	for (const char *p = fmt; *p; p++) {
		if (*p == 'i') r += va_arg(ap, int);
		else if (*p == 's') r += (int)strlen(va_arg(ap, char *));
	}
	int first = va_arg(ap2, int);
	va_end(ap);
	va_end(ap2);
	return r * 100 + first;
}
struct Big mkbig(int a, int b, int c, int d, int e, int f, int g) {
	struct Big r;
	r.v[0] = a + b; r.v[1] = c + d; r.v[2] = e + f; r.v[3] = g; r.v[4] = a * g;
	return r;
}
long stackargs(struct S3 s, long a, long b, long c, long d, long e, long f, struct Big big, long g) {
	return s.a + s.b + s.c + a + b + c + d + e + f + big.v[0] + big.v[4] + g;
}
Rec mkrec(const char *n, int x) { Rec r; memset(&r, 0, sizeof r); strcpy(r.name, n); r.x = x; return r; }
int depth(int n) { return n == 0 ? 0 : 1 + depth(n - 1); }
static int counter;
int bump(void) { return ++counter; }
int (*pick(int w))(void) { return w ? bump : 0; }
typedef int (*binop)(int, int);
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
binop ops[] = { add, sub };
int call_all(binop *o, int n) { int r = 0; for (int i = 0; i < n; i++) r += o[i](10, 3); return r; }

int main(void) {
	if (sum_va(8, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L) != 36) return 1;
	if (sum_va(0) != 0) return 2;
	if (mix_va("isi", 5, "abc", 7, 99) != 1505) return 3;
	struct Big b = mkbig(1, 2, 3, 4, 5, 6, 7);
	if (b.v[0] != 3 || b.v[3] != 7 || b.v[4] != 7) return 4;
	struct S3 s3 = { 1, 2, 3 };
	if (stackargs(s3, 10, 20, 30, 40, 50, 60, b, 1000) != 1226) return 5;
	Rec r = mkrec("bob", 42);
	if (strcmp(r.name, "bob") || r.x != 42 || mkrec("al", 1).name[1] != 'l') return 6;
	if (depth(10000) != 10000) return 7;
	if (pick(1)() != 1 || pick(1)() != 2 || pick(0)) return 8;
	if (call_all(ops, 2) != 20) return 9;
	char buf[100];
	snprintf(buf, sizeof buf, "%d %s %ld %c %d %d %d %d %d", 1, "two", 3L, '4', 5, 6, 7, 8, 9);
	if (strcmp(buf, "1 two 3 4 5 6 7 8 9")) return 10;
	snprintf(buf, sizeof buf, "%d-%d", add(sub(9, 4), add(1, 1)), (int)strlen(buf));
	if (strcmp(buf, "7-19")) return 11;
	int arr[3][2] = { {1, 2}, {3, 4}, {5, 6} };
	int (*row)[2] = arr + 1;
	if (row[1][1] != 6 || (*row)[0] != 3 || sizeof(*row) != 8) return 12;
	char *strs[] = { "a", "bb", "ccc" };
	if (strlen(strs[2]) != 3 || sizeof(strs) != 24) return 13;
	unsigned x = 1;
	x <<= 31;
	if (x != 0x80000000u || (int)x >= 0) return 14;
	long long ll = 0x7fffffffffffffffLL;
	ll++;
	if (ll >= 0) return 15;
	int i = 0;
	i = i++ + 1;
	char c = -1;
	if ((unsigned char)c != 255 || c != -1) return 16;
	int q = 10;
	q = q > 5 ? q < 20 ? 1 : 2 : 3;
	if (q != 1) return 17;
	struct { int a[3]; } w = { { 7, 8, 9 } };
	if (w.a[2] != 9) return 18;
	const char *msg = "x" "y" "z";
	if (strcmp(msg, "xyz")) return 19;
	if (sizeof("abc") != 4 || sizeof(int[5]) != 20) return 20;
	int *p = &arr[0][0];
	if (p[5] != 6) return 21;
	if (!(1 ? 0 : 1) != 1) return 22;
	switch (3) { default: q = 50; break; case 1: q = 1; }
	if (q != 50) return 23;
	return 0;
}
