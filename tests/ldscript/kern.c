/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

extern char __bss_start[], __bss_end[], _end[], _etext[];
extern void (*__init_start[])(void), (*__init_end[])(void);
extern long marker_value, provided_used;
extern char table_bytes[];

int counter = 3;
static char buf[100];
long table[4] = { 1, 2, 3, 4 };
const char *msg = "hello from the kernel";
char *ends[] = { __bss_start, __bss_end, _end, _etext };
void *marks[] = { &marker_value, &provided_used, table_bytes };

__attribute__((section(".kdata"))) int kd[2] = { 7, 8 };
__attribute__((section(".text.unlikely"))) int cold(int x) { return x * 2; }
__attribute__((section(".discard.me"))) int dropped_data = 5;

void init_a(void) { counter += 1; }
void init_b(void) { counter *= 2; }
void init_c(void) { counter -= 1; }

int kmain(void) {
	for (void (**f)(void) = __init_start; f < __init_end; f++) (*f)();
	buf[0] = (char)counter;
	return cold(buf[0]) + kd[0] + (int)table[1];
}
