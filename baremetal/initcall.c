/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define initcall(fn) static int (*const fn##_call)(void) __attribute__((section("initcalls"), used)) = fn
#define min(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })
#define print(x) _Generic((x), char *: kputs, const char *: kputs, default: kputn)(x)
#define HIGH 0xFFFFFFFF80000000ul

extern int (*const __start_initcalls[])(void), (*const __stop_initcalls[])(void);
extern void platform_hook(void) __attribute__((weak));

static unsigned long pml4[512] __attribute__((aligned(4096)));
static unsigned long pdpt_lo[512] __attribute__((aligned(4096)));
static unsigned long pdpt_hi[512] __attribute__((aligned(4096)));
static unsigned long pd[512] __attribute__((aligned(4096)));
static volatile unsigned long probe = 0x5CCC;

static int paging(void) {
	int aligned = !((unsigned long)pml4 % 4096 | (unsigned long)pdpt_lo % 4096 |
		(unsigned long)pdpt_hi % 4096 | (unsigned long)pd % 4096);
	for (unsigned long i = 0; i < 512; i++) pd[i] = i << 21 | 0x83;
	pdpt_lo[0] = (unsigned long)pd | 3;
	pdpt_hi[510] = (unsigned long)pd | 3;
	pml4[0] = (unsigned long)pdpt_lo | 3;
	pml4[511] = (unsigned long)pdpt_hi | 3;
	__write_cr3((unsigned long)pml4);
	volatile unsigned long *alias = (volatile unsigned long *)(HIGH + (unsigned long)&probe);
	int ok = *alias == 0x5CCC;
	*alias = 0xBEEF;
	ok = ok && probe == 0xBEEF && __read_cr3() == (unsigned long)pml4;
	check("paging", aligned && ok);
	return !(aligned && ok);
}

static int dispatch(void) {
	static const void *ops[] = { &&push, &&add, &&mul, &&halt };
	static const unsigned char code[] = { 0, 6, 0, 7, 2, 0, 3, 1, 3 };
	const unsigned char *pc = code;
	unsigned long st[8];
	int sp = 0;
	goto *ops[*pc++];
push:
	st[sp++] = *pc++;
	goto *ops[*pc++];
add:
	sp--;
	st[sp - 1] += st[sp];
	goto *ops[*pc++];
mul:
	sp--;
	st[sp - 1] *= st[sp];
	goto *ops[*pc++];
halt:
	check("dispatch", sp == 1 && st[0] == 45);
	return st[0] != 45;
}

static int kind(int c) {
	switch (c) {
	case '0' ... '9': return 1;
	case 'a' ... 'z': case 'A' ... 'Z': return 2;
	default: return 0;
	}
}

static int ranges(void) {
	static const unsigned char level[8] = { [0 ... 3] = 1, [4 ... 7] = 2 };
	const char *s = "k9 Z";
	int sum = 0;
	for (int i = 0; s[i]; i++) sum = sum * 3 + kind(s[i]);
	int ok = sum == 2 * 27 + 1 * 9 + 0 * 3 + 2 && level[3] == 1 && level[4] == 2;
	ok = ok && min(sum, 100) == sum && min(-1, sizeof(long)) == 8;
	check("ranges", ok);
	return !ok;
}

static int answer(void) { return 42; }
int answer_alias(void) __attribute__((alias("answer")));

static int symbols(void) {
	int ok = &platform_hook == 0 && answer_alias() == 42 && __builtin_types_compatible_p(typeof(answer), int (void));
	check("symbols", ok);
	return !ok;
}

initcall(paging);
initcall(dispatch);
initcall(ranges);
initcall(symbols);

void kmain(void) {
	int n = 0, bad = 0;
	serial_init();
	print("initcall\n");
	for (int (*const *f)(void) = __start_initcalls; f < __stop_initcalls; f++, n++) bad |= (*f)();
	print("ran ");
	print(n);
	print(" initcalls\n");
	check("initcalls", n == 4 && !bad);
	finish();
}
