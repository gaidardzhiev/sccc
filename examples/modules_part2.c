/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "modules.h"

int stack_operations;

static int bump(void) {
	return ++stack_operations;
}

void stack_init(Stack *s) {
	s->top = 0;
	bump();
}

int stack_push(Stack *s, int v) {
	bump();
	if (s->top == 32) return 0;
	s->data[s->top++] = v;
	return 1;
}

int stack_pop(Stack *s, int *v) {
	bump();
	if (s->top == 0) return 0;
	*v = s->data[--s->top];
	return 1;
}

int stack_size(const Stack *s) {
	return s->top;
}
