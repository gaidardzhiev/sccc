/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include "modules.h"

static int bump(void) {
	return -1;
}

int main(void) {
	Stack s;
	int v;
	stack_init(&s);
	for (int i = 1; i <= 40; i++)
		if (!stack_push(&s, i * i)) {
			printf("stack full at %d\n", i);
			break;
		}
	printf("size %d\n", stack_size(&s));
	printf("popped:");
	for (int i = 0; i < 5 && stack_pop(&s, &v); i++) printf(" %d", v);
	printf("\nsize %d, operations %d, local bump() %d\n", stack_size(&s), stack_operations, bump());
	return 0;
}
