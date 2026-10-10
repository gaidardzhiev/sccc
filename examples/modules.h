#ifndef MODULES_H
#define MODULES_H

typedef struct {
	int data[32];
	int top;
} Stack;

void stack_init(Stack *s);
int stack_push(Stack *s, int v);
int stack_pop(Stack *s, int *v);
int stack_size(const Stack *s);

extern int stack_operations;

#endif
