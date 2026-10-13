/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define TNAME(x) _Generic((x), char: "char", signed char: "schar", unsigned char: "uchar", \
	short: "short", int: "int", unsigned: "unsigned", long: "long", unsigned long: "ulong", \
	long long: "llong", unsigned long long: "ullong", float: "float", double: "double", \
	char *: "char*", int *: "int*", default: "other")
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define swap(a, b) do { typeof(a) _t = (a); (a) = (b); (b) = _t; } while (0)
#define likely(x) __builtin_expect(!!(x), 1)

struct S { char c; double d; };
struct P { int x, y; };
struct In { int a; char b[6]; };
struct Out { char c; struct In in[3]; long z; };
struct E { int a, b; const char *s; };
struct msg { int len; char data[0]; };
enum { E1 = 5 };
typedef int myint;

static int g = 3;
static int n;
static int calls;
static const int off_tab[] = { offsetof(struct Out, z), __builtin_offsetof(struct Out, in[2].b[1]) };
static const char *names[] = { [0 ... 3] = "low", [4 ... 6] = "mid", [7] = "top" };
static int tab[10] = { [2 ... 5] = 7, [4] = 1 };
static struct E es[4] = { [0 ... 2] = { 1, 2, "e" }, [1 ... 3].b = 9 };
static int auto_len[] = { [3 ... 5] = 2 };
static int empty[0];

static int sq(int x) { return x * x; }
static double half(double x) { return x / 2; }
static int next_id(void) { return ++n; }
static int side(void) { calls++; return 1; }
static int twice(int x) { return 2 * x; }
static int thrice(int x) { return 3 * x; }
#define F(x) _Generic((x), int: sq, default: half)(x)

static void generic(void) {
	char c = 0; signed char sc = 0; long long ll = 0; int arr[3]; char buf[4];
	typeof(arr) a2; __typeof__(c) c2 = 'x'; typeof(int *) ip = arr;
	typeof(sq) *fp = sq;
	enum { A = _Alignof(long) * 2 };
	printf("%s %s %s %s %s %s\n", TNAME(c), TNAME(sc), TNAME(ll), TNAME(1LL), TNAME(1ULL), TNAME(1UL));
	printf("%s %s %s %s %s\n", TNAME(arr), TNAME(buf), TNAME(1.0f), TNAME(c + 1), TNAME(&arr));
	printf("%d %d %d %d\n", (int)sizeof(a2), (int)sizeof(c2), (int)sizeof(ip), c2);
	printf("%d %g %d\n", F(7), F(3.0), fp(5));
	printf("%d %d %d %d\n", (int)_Alignof(struct S), (int)__alignof__(double), (int)_Alignof(char), (int)__alignof__(c));
	printf("%d %s\n", A, _Generic(0, default: "dflt", long: "l"));
}

static void builtins(void) {
	int v = 4;
	enum { K = __builtin_choose_expr(E1 > 3, 10, 20), T = __builtin_types_compatible_p(long, long) };
	printf("%d %d\n", off_tab[0], off_tab[1]);
	printf("%d %d %d %d\n", __builtin_constant_p(3), __builtin_constant_p(E1 * 2 + sizeof(int)),
		__builtin_constant_p(v), __builtin_constant_p(g));
	printf("%d %d %d\n", __builtin_constant_p((myint)7), __builtin_constant_p(side()), calls);
	printf("%d %d %d %d\n", __builtin_types_compatible_p(int, myint), __builtin_types_compatible_p(int, long),
		__builtin_types_compatible_p(typeof(v), int), __builtin_types_compatible_p(int[3], int[]));
	printf("%d %d\n", __builtin_types_compatible_p(char *, unsigned char *), __builtin_types_compatible_p(struct In, struct In));
	if (likely(v == 4)) printf("likely\n");
	printf("%ld\n", __builtin_expect(v + 1, 0));
	printf("%d %d\n", __builtin_choose_expr(1, twice, thrice)(5), __builtin_choose_expr(0, twice, thrice)(5));
	printf("%s\n", __builtin_choose_expr(sizeof(long) == 8, "lp64", 42));
	switch (v) { case offsetof(struct In, b): printf("case off\n"); break; default: break; }
	printf("%d %d\n", K, T);
	if (v == 5) __builtin_unreachable();
}

static void stmt_expr(void) {
	int i = 3, j = 9, k, sum = 0;
	printf("%d %g\n", max(i, j), max(2.5, 1.0));
	k = max(next_id(), 0);
	printf("%d %d\n", k, n);
	k = ({ int s = 0; for (int q = 1; q <= 10; q++) s += q; s; }) + 1;
	printf("%d\n", k);
	struct P p = ({ struct P t = { 4, 5 }; t; });
	printf("%d %d %d\n", p.x, p.y, ({ struct P t = { 7, 8 }; t; }).y);
	swap(i, j);
	printf("%d %d\n", i, j);
	for (int q = 0; q < 10; q++) sum += ({ int r; if (q % 2) { r = q; } else { r = 0; } r; });
	printf("%d\n", sum);
	for (int q = 0; q < 5; q++) { ({ if (q == 3) break; 0; }); printf("q%d ", q); }
	printf("\n");
	char *s = ({ static char sbuf[8] = "abc"; sbuf; });
	printf("%s %d\n", s, ({ 1; 2; }) * 10 + ({ label: 3; }));
	({ ; });
	printf("%d\n", (int)sizeof(({ (short)1; })));
	int r = __extension__ ({ int t = k; t * 2; });
	printf("%d\n", r);
}

static const char *kind(int c) {
	switch (c) {
	case '0' ... '9': return "digit";
	case 'a' ... 'z': case 'A' ... 'Z': return "letter";
	case -5 ... -1: return "neg";
	case ' ': return "space";
	default: return "other";
	}
}

static int ukind(unsigned u) {
	switch (u) { case 0 ... 9: return 1; case 0xfffffff0u ... 0xffffffffu: return 2; default: return 0; }
}

static void ranges(void) {
	int v = 5;
	int loc[8] = { [1 ... 6] = v * 2, [3] = -1 };
	struct E le[3] = { [0 ... 2] = { v, v + 1, "x" } };
	long ll[5] = { [0 ... 4] = 0x1122334455667788 };
	const char *t = "a Z 5 ?";
	for (int i = 0; i < 8; i++) printf("%s ", names[i]);
	printf("\n");
	for (int i = 0; i < 10; i++) printf("%d ", tab[i]);
	printf("\n");
	for (int i = 0; i < 4; i++) printf("{%d %d %s} ", es[i].a, es[i].b, es[i].s ? es[i].s : "-");
	printf("%d\n", (int)(sizeof(auto_len) / sizeof(int)));
	for (int i = 0; t[i]; i++) printf("%s ", kind(t[i]));
	printf("%s %s\n", kind(-3), kind(-6));
	printf("%d %d %d %d\n", ukind(3), ukind(0xfffffff5u), ukind(100), ukind(0xffffffffu));
	for (int i = 0; i < 8; i++) printf("%d ", loc[i]);
	for (int i = 0; i < 3; i++) printf("{%d %d %s} ", le[i].a, le[i].b, le[i].s);
	printf("%lx %lx\n", ll[0], ll[4]);
}

enum { PUSH, ADD, MUL, PRINT, HALT };

static int vm(const int *code) {
	static const void *ops[] = { &&op_push, &&op_add, &&op_mul, &&op_print, &&op_halt };
	int st[16], sp = 0, steps = 0;
	const int *pc = code;
	goto *ops[*pc++];
op_push: st[sp++] = *pc++; steps++; goto *ops[*pc++];
op_add: sp--; st[sp-1] += st[sp]; steps++; goto *ops[*pc++];
op_mul: sp--; st[sp-1] *= st[sp]; steps++; goto *ops[*pc++];
op_print: printf("%d\n", st[sp-1]); steps++; goto *ops[*pc++];
op_halt: return steps;
}

static int pick(int x) {
	void *t[] = { &&zero, &&one, &&many };
	void *p = x < 2 ? t[x] : t[2];
	goto *p;
zero: return 100;
one: return 101;
many: return (int)sizeof(&&zero) + 200;
}

static void labels(void) {
	int prog[] = { PUSH, 6, PUSH, 7, MUL, PUSH, 3, ADD, PRINT, HALT };
	void *a = &&end;
	printf("%d\n", vm(prog));
	printf("%d %d %d\n", pick(0), pick(1), pick(5));
	goto *a;
	printf("skipped\n");
end:
	printf("%d\n", (int)(&&end == a));
}

static void zero_len(void) {
	struct msg *m = malloc(sizeof(struct msg) + 6);
	int z[0];
	m->len = 6;
	memcpy(m->data, "hello", 6);
	printf("%d %s %d %d %d\n", (int)sizeof(struct msg), m->data, (int)sizeof(empty), (int)sizeof(z), m->len);
	free(m);
}

_Noreturn void stop(void);

int main(void) {
	generic();
	builtins();
	stmt_expr();
	ranges();
	labels();
	zero_len();
	return 0;
}
