/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#define N 10
#define SQ(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a ## b
#define EMPTY
#define LOG(fmt, ...) printf("log: " fmt "\n", ##__VA_ARGS__)
#define VA(...) count(__VA_ARGS__)
#define LPAREN (
#define SELF(x) x
#if N > 5 && defined(SQ)
int big = 1;
#else
int big = 0;
#endif
#ifdef UNDEFINED_THING
#error should not happen
#elif N == 10
int ten = 1;
#else
int ten = 0;
#endif
#ifndef N
int nn = 0;
#else
int nn = N;
#endif
#if 0
this is ' garbage " that must be skipped
#if 1
#endif
#endif
#undef N
#ifdef N
#error N should be undefined
#endif
#define N 20
int count(int a, ...) { return a; }
#define MULTI(a, \
		b) (a + b)
int main(void) {
	int CAT(var, 1) = 5;
	printf("%d %d %d %d %d\n", N, SQ(3), SQ(1 + 1), MAX(3, 7), MAX(MAX(1, 9), 4));
	printf("%s %s %s\n", STR(hello world), XSTR(N), STR("q"));
	printf("%d %d %d %d\n", var1, big, ten, nn);
	LOG("plain");
	LOG("%d+%d", 1, 2);
	int FOO = 3;
#define FOO (FOO + 1)
	printf("%d %d\n", FOO, SELF(SELF(4)));
	printf("%d %d\n", VA(5, 6, 7), MULTI(1, 2));
	printf("%s:%d\n", __FILE__ + 0 == 0 ? "" : "file", __LINE__);
	EMPTY;
	return 0;
}
