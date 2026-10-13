/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

struct pt { int x, y; };

__thread int counter;
__thread long seeded = 42;
static __thread char name[16] = "tls";
__thread struct pt point = { 3, 4 };
_Thread_local short small = -7;
static __thread unsigned char flags = 200;
extern __thread int shared;
extern __thread long table[4];

int bump(void);
long sum_table(void);

static int count_calls(void) {
	static __thread int calls;
	calls = calls + 1;
	return calls;
}

int main(void) {
	int *p = &counter;
	counter += 5;
	counter++;
	++counter;
	*p += 1;
	seeded *= 2;
	point.y += 10;
	name[0] = 'T';
	small -= 1;
	flags = flags + 100;
	shared = 10;
	bump();
	bump();
	count_calls();
	count_calls();
	table[2] = 7;
	printf("counter %d\n", counter);
	printf("seeded %ld\n", seeded);
	printf("name %s %d\n", name, (int)sizeof name);
	printf("point %d %d\n", point.x, point.y);
	printf("small %d\n", small);
	printf("flags %d\n", flags);
	printf("shared %d\n", shared);
	printf("calls %d\n", count_calls());
	printf("table %ld\n", sum_table());
	printf("addresses %d\n", &counter != (int *)&seeded && p == &counter);
	return 0;
}
