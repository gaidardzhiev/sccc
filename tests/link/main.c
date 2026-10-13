/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

struct entry { const char *name; int (*fn)(void); };
extern struct entry __start_mytab[], __stop_mytab[];

int counter;
int shared_buf[16];
int pic_base = 100;
int lib_a(void);
int hook(void);
extern int pic_value;
int pic_call(int x);
int sum_commons(void);
double scale(double x, float f);
extern __thread int tls_counter;
extern __thread long tls_zero;
int tls_step(void);

int main(void) {
	for (struct entry *e = __start_mytab; e < __stop_mytab; e++) printf("%s %d\n", e->name, e->fn());
	counter += 5;
	printf("lib_a %d hook %d\n", lib_a(), hook());
	printf("pic %d %d\n", pic_value, pic_call(3));
	printf("commons %d\n", sum_commons());
	printf("scale %g\n", scale(1.5, 2.0f));
	tls_counter += 10;
	int step = tls_step();
	printf("tls %d %d %ld\n", step, tls_counter, tls_zero);
	return 0;
}
