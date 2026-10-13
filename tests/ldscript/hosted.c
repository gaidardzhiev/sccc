/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

#include <stdio.h>
#include <string.h>

struct cmd { const char *name; int (*fn)(int); };

static int twice(int x) { return 2 * x; }
static int square(int x) { return x * x; }
static int negate(int x) { return -x; }

__attribute__((section(".cmds.b"), used)) static const struct cmd c2 = { "square", square };
__attribute__((section(".cmds.a"), used)) static const struct cmd c1 = { "twice", twice };
__attribute__((section(".cmds.c"), used)) static const struct cmd c3 = { "negate", negate };

extern const struct cmd __cmds_start[], __cmds_end[];
extern char __data_start[], _edata[], __bss_begin[], _end[], banner[];
extern char data_page_offset[];

static char zero[5000];
int counter = 7;

int main(void) {
	for (const struct cmd *c = __cmds_start; c < __cmds_end; c++) printf("%s(5) = %d\n", c->name, c->fn(5));
	printf("data aligned: %d\n", (int)(((unsigned long)__data_start & 0xFFF) == (unsigned long)data_page_offset));
	printf("counter in data: %d\n", (char *)&counter >= __data_start && (char *)&counter < _edata);
	printf("zero in bss: %d\n", zero >= __bss_begin && zero + sizeof zero <= _end);
	printf("banner: %s\n", banner);
	counter += (int)strlen(zero);
	printf("counter: %d\n", counter);
	return 0;
}
