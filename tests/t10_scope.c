#include <stdio.h>
typedef int T;
int shadow = 1;
struct tag { int a; };
int f(void) {
	int shadow = 2;
	{
		int shadow = 3;
		if (shadow != 3) return 1;
	}
	if (shadow != 2) return 2;
	T T2 = 5;
	{
		typedef long T;
		T big = 1L << 40;
		if (sizeof(T) != 8 || big >> 40 != 1) return 3;
	}
	if (sizeof(T) != 4 || T2 != 5) return 4;
	struct tag { long b; long c; };
	struct tag t;
	t.c = 9;
	if (sizeof(t) != 16 || t.c != 9) return 5;
	enum { E1 = 3, E2 };
	if (E2 != 4) return 6;
	for (int i = 0; i < 2; i++) { int shadow = i; (void)shadow; }
	{ extern int shadow; if (shadow != 1) return 7; }
	return 0;
}
struct list { struct list *next; int v; };
int g(void) {
	struct list a, b;
	a.next = &b; b.next = 0; a.v = 1; b.v = 2;
	return a.next->v;
}
int main(void) {
	int r = f();
	if (r) return r;
	if (g() != 2) return 10;
	if (sizeof(struct tag) != 4) return 11;
	return 0;
}
