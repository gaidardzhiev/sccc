/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdint.h>

#define ALIGNED(n) __attribute__((aligned(n)))
#define CMD(n, f) static const struct cmd cmd_##n __attribute__((section("cmds"), used)) = { #n, f }
#define INIT_TEXT __attribute__((section(".text.init")))

struct ALIGNED(32) Line { int x; };
struct Pad { char c; int v __attribute__((__aligned__(16))); char d; };
struct __attribute__((packed, aligned(4))) PK { char a; int b; };
typedef int aint ALIGNED(8);
struct UseA { char c; aint v; };
struct cmd { const char *name; int (*fn)(int); };

static int big[3] ALIGNED(4096) = { 1, 2, 3 };
static char page[100] __attribute__((aligned(8192)));
_Alignas(64) static char al64;
_Alignas(double) char ald;

int weak_val __attribute__((weak)) = 7;
int strong_over __attribute__((weak)) = 1;
__attribute__((weak)) int wf(void) { return 1; }
__attribute__((weak)) int wg(void) { return 2; }
extern int missing(void) __attribute__((weak));
extern int missing_var __attribute__((weak));
static int real_impl(int x) { return x * 3; }
int alias_fn(int) __attribute__((alias("real_impl")));
int real_var = 42;
extern int alias_var __attribute__((alias("real_var")));
int other_unit(void);
__attribute__((aligned(64))) void aligned_fn(void) {}
__attribute__((noreturn, used, unused, cold)) static void spin(void) { for (;;); }

extern const struct cmd __start_cmds[], __stop_cmds[];
extern struct u { int v; } __start_misc[] __attribute__((weak)), __stop_misc[] __attribute__((weak));
static int dbl(int x) { return 2 * x; }
INIT_TEXT static int neg(int x) { return -x; }
CMD(double, dbl);
CMD(negate, neg);
static const int table[4] __attribute__((section(".rodata.tbl"))) = { 10, 20, 30, 40 };
int counter __attribute__((section("counters"))) = 5;
int zeroed[3] __attribute__((section("counters")));

INIT_TEXT int dispatch(int op) {
	static const void *ops[] = { &&a, &&b };
	static int hits __attribute__((section("counters")));
	hits++;
	goto *ops[op & 1];
a: return 100 + hits;
b: return 200 + hits;
}

int main(void) {
	struct { char c; _Alignas(16) short s; } loc;
	_Alignas(16) char lc;
	printf("%d %d\n", (int)sizeof(struct Line), (int)_Alignof(struct Line));
	printf("%d %d %d\n", (int)__builtin_offsetof(struct Pad, v), (int)sizeof(struct Pad), (int)_Alignof(struct Pad));
	printf("%d %d %d %d\n", (int)sizeof(struct PK), (int)_Alignof(struct PK), (int)_Alignof(aint), (int)__builtin_offsetof(struct UseA, v));
	printf("%d %d %d %d %d\n", (int)((uintptr_t)big % 4096), (int)((uintptr_t)page % 8192), (int)((uintptr_t)&al64 % 64),
		(int)((uintptr_t)&ald % 8), big[2]);
	printf("%d %d\n", (int)((uintptr_t)&loc.s % 16), (int)((uintptr_t)&lc % 16));
	printf("%d %d %d %d\n", weak_val, strong_over, wf(), wg());
	printf("%d %d\n", missing ? 1 : 0, &missing_var ? 1 : 0);
	printf("%d %d %d\n", alias_fn(5), alias_var, &alias_var == &real_var);
	printf("%d %d\n", other_unit(), (int)((uintptr_t)aligned_fn % 64));
	for (const struct cmd *c = __start_cmds; c < __stop_cmds; c++) printf("%s(7) = %d\n", c->name, c->fn(7));
	printf("%d cmds\n", (int)(__stop_cmds - __start_cmds));
	printf("%d %d %d %d\n", table[3], counter, zeroed[2], dispatch(0) + dispatch(1));
	printf("%d\n", __start_misc == __stop_misc);
	(void)spin;
	return 0;
}
