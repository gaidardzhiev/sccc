/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

long add3(long a, long b, long c);
void *copy_bytes(void *dst, const void *src, long n);
long sum_table(const long *p, long n);
int classify(long v);
int call_back(int x);
void say(int x);
char *fill_buf(void);
long bit_ops(unsigned long v);
int far_branch(int x);
int overridable(void);
int use_hook(void);
int hook_called;

extern long asm_values[];
extern int asm_count;
extern long *asm_ptr;
extern char scratch[];

int c_counter = 5;

int c_square(int x) { return x * x; }

int overridable(void) { return 2; }

int maybe_hook(void) { hook_called++; return 42; }

int main(void) {
	char buf[32];
	printf("add3 %ld\n", add3(1, 20, 300));
	copy_bytes(buf, "assembled", 10);
	printf("copy %s\n", buf);
	printf("sum %ld count %d ptr %ld\n", sum_table(asm_values, asm_count), asm_count, *asm_ptr);
	for (long i = -1; i <= 4; i++) printf("classify %ld %d\n", i, classify(i));
	printf("call_back %d\n", call_back(4));
	say(7);
	char *f = fill_buf();
	printf("fill %s %d\n", f, (int)strlen(scratch));
	printf("bits %lx %lx\n", bit_ops(0x81), bit_ops(0x400));
	printf("far %d %d\n", far_branch(0), far_branch(9));
	int h = use_hook();
	printf("override %d hook %d %d\n", overridable(), h, hook_called);
	return 0;
}
