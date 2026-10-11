/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

struct inner { int b, c; };
struct outer { int a; struct inner in; int d; };
struct named { const char *name; int vals[3]; };

int g2[2][3] = { 1, 2, 3, 4, 5, 6 };
struct outer go = { 1, 2, 3, 4 };
struct named gn[] = { "x", 1, 2, 3, "y", { 4, 5 } };
int gpart[3][2] = { { 1 }, { 2, 3 }, 4 };
char gs[2][4] = { "ab", "cde" };
struct inner gdi[3] = { [2] = { 9, 8 }, [0].c = 7 };
_Static_assert(sizeof(struct outer) == 16, "outer size");

int main(void) {
	_Static_assert(sizeof(int) == 4, "int size");
	int l2[2][3] = { 1, 2, 3, 4, 5, 6 };
	struct outer lo = { 1, 2, 3, 4 };
	struct named ln[] = { "x", 1, 2, 3, "y", { 4, 5 } };
	int lpart[3][2] = { { 1 }, { 2, 3 }, 4 };
	char ls[2][4] = { "ab", "cde" };
	int sum = 0;
	for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) sum += g2[i][j] * (i + 1) + l2[i][j];
	printf("%d\n", sum);
	printf("%d %d %d %d | %d %d %d %d\n", go.a, go.in.b, go.in.c, go.d, lo.a, lo.in.b, lo.in.c, lo.d);
	printf("%s %d %d %d %s %d %d %d %zu\n", gn[0].name, gn[0].vals[0], gn[0].vals[2], gn[1].vals[0],
		gn[1].name, gn[1].vals[1], ln[1].vals[0], ln[0].vals[1], sizeof(ln) / sizeof(ln[0]));
	printf("%d\n", (int)(sizeof(gn) / sizeof(gn[0])));
	printf("%d %d %d %d %d %d\n", gpart[0][0], gpart[0][1], gpart[1][1], gpart[2][0], lpart[2][0], lpart[0][1]);
	printf("%s %s %s %s\n", gs[0], gs[1], ls[0], ls[1]);
	printf("%d %d %d %d\n", gdi[2].b, gdi[2].c, gdi[0].c, gdi[1].b);
	printf("%s %s\n", __func__, __FUNCTION__);
	int arr[] = { [4] = 1, [1] = 2 };
	printf("%zu %d %d\n", sizeof(arr) / sizeof(int), arr[1], arr[4]);
	struct outer cl = (struct outer){ .d = 5, .in = { 6, 7 } };
	printf("%d %d %d %d\n", cl.a, cl.in.b, cl.in.c, cl.d);
	int *ip = (int[]){ 10, 20, 30 };
	printf("%d\n", ip[2]);
	return 0;
}
