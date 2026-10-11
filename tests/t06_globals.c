/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

int counter;
static int scount = 5;
char *msg = "hello";
char gbuf[16] = "abc";
const char *names[] = { "zero", "one", "two" };
int tbl[] = { 1, 2, 3, 4 };
long lv = -1;
int *ptr_to_tbl = tbl + 2;
int (*fp)(int);
int sq(int x) { return x * x; }
int (*fptrs[2])(int) = { sq, 0 };
struct kv { const char *k; int v; };
struct kv kvs[] = { { "a", 1 }, { "b", 2 }, { 0, 0 } };
int next_id(void) { static int id = 100; return id++; }
int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
int apply(int (*f)(int), int v) { return f(v); }
int main() {
	counter++;
	if (counter != 1 || scount != 5) return 1;
	if (msg[1] != 'e' || gbuf[2] != 'c' || gbuf[3] != 0) return 2;
	if (!streq(names[2], "two")) return 3;
	if (tbl[3] != 4 || sizeof(tbl) != 16) return 4;
	if (lv != -1) return 5;
	if (*ptr_to_tbl != 3) return 6;
	fp = sq;
	if (fp(5) != 25 || (*fp)(6) != 36) return 7;
	if (fptrs[0](3) != 9 || fptrs[1]) return 8;
	if (kvs[1].v != 2 || !streq(kvs[0].k, "a") || kvs[2].k) return 9;
	if (next_id() != 100 || next_id() != 101) return 10;
	if (apply(sq, 7) != 49 || apply(&sq, 2) != 4) return 11;
	int (*lf)(int) = &sq;
	if (lf(4) != 16) return 12;
	return 0;
}
