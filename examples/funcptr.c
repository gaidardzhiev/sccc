/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*BinOp)(int, int);

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
static int maxi(int a, int b) { return a > b ? a : b; }

static struct {
	const char *name;
	BinOp fn;
} ops[] = { { "add", add }, { "sub", sub }, { "mul", mul }, { "max", maxi } };

static int fold(const int *a, int n, int init, BinOp f) {
	for (int i = 0; i < n; i++) init = f(init, a[i]);
	return init;
}

static void map(int *a, int n, int (*f)(int)) {
	for (int i = 0; i < n; i++) a[i] = f(a[i]);
}

static int square(int x) { return x * x; }
static int negate(int x) { return -x; }

static int by_len(const void *a, const void *b) {
	const char *x = *(const char *const *)a, *y = *(const char *const *)b;
	size_t lx = strlen(x), ly = strlen(y);
	return lx != ly ? (int)lx - (int)ly : strcmp(x, y);
}

static BinOp lookup(const char *name) {
	for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
		if (strcmp(ops[i].name, name) == 0) return ops[i].fn;
	return NULL;
}

static int (*pick(int which))(int) {
	return which ? square : negate;
}

int main(void) {
	int a[] = { 3, 1, 4, 1, 5, 9, 2, 6 };
	int n = sizeof a / sizeof a[0];
	for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
		printf("fold %s = %d\n", ops[i].name, fold(a, n, i == 2 ? 1 : 0, ops[i].fn));
	printf("lookup(\"mul\")(6, 7) = %d\n", lookup("mul")(6, 7));
	printf("(*lookup(\"sub\"))(6, 7) = %d\n", (*lookup("sub"))(6, 7));
	printf("lookup(\"div\") is %s\n", lookup("div") ? "found" : "NULL");
	map(a, n, pick(1));
	printf("squared:");
	for (int i = 0; i < n; i++) printf(" %d", a[i]);
	printf("\n");
	map(a, n, pick(0));
	printf("negated:");
	for (int i = 0; i < n; i++) printf(" %d", a[i]);
	printf("\n");
	const char *words[] = { "pear", "fig", "banana", "kiwi", "apple", "date", "cherry" };
	qsort(words, 7, sizeof words[0], by_len);
	printf("by length:");
	for (int i = 0; i < 7; i++) printf(" %s", words[i]);
	printf("\n");
	int (*say)(const char *) = puts;
	say("called puts through a pointer");
	return 0;
}
