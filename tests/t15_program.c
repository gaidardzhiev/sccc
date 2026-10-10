#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

typedef enum { V_INT, V_STR, V_LIST } VKind;
typedef struct Value Value;
struct Value {
	VKind kind;
	union {
		long i;
		char *s;
		struct { Value **items; int n, cap; } list;
	} u;
};

static Value *v_int(long i) { Value *v = calloc(1, sizeof *v); v->kind = V_INT; v->u.i = i; return v; }
static Value *v_str(const char *s) { Value *v = calloc(1, sizeof *v); v->kind = V_STR; v->u.s = strdup(s); return v; }
static Value *v_list(void) { Value *v = calloc(1, sizeof *v); v->kind = V_LIST; return v; }
static void list_push(Value *l, Value *x) {
	if (l->u.list.n == l->u.list.cap) {
		l->u.list.cap = l->u.list.cap ? l->u.list.cap * 2 : 4;
		l->u.list.items = realloc(l->u.list.items, sizeof(Value *) * l->u.list.cap);
	}
	l->u.list.items[l->u.list.n++] = x;
}

static void print_value(FILE *f, Value *v) {
	switch (v->kind) {
	case V_INT: fprintf(f, "%ld", v->u.i); break;
	case V_STR: fprintf(f, "\"%s\"", v->u.s); break;
	case V_LIST:
		fputc('[', f);
		for (int i = 0; i < v->u.list.n; i++) { if (i) fputs(", ", f); print_value(f, v->u.list.items[i]); }
		fputc(']', f);
		break;
	}
}

typedef struct Entry { char *key; Value *val; struct Entry *next; } Entry;
#define NBUCKET 64
static Entry *table[NBUCKET];
static unsigned hash(const char *s) { unsigned h = 5381; while (*s) h = h * 33 ^ (unsigned char)*s++; return h; }
static void set_var(const char *k, Value *v) {
	unsigned h = hash(k) % NBUCKET;
	for (Entry *e = table[h]; e; e = e->next) if (!strcmp(e->key, k)) { e->val = v; return; }
	Entry *e = malloc(sizeof *e);
	e->key = strdup(k); e->val = v; e->next = table[h]; table[h] = e;
}
static Value *get_var(const char *k) {
	for (Entry *e = table[hash(k) % NBUCKET]; e; e = e->next) if (!strcmp(e->key, k)) return e->val;
	return NULL;
}

typedef enum { TK_NUM, TK_ID, TK_STR, TK_OP, TK_END } TokKind;
typedef struct { TokKind kind; long num; char text[64]; } Token;
static const char *src;
static Token tok;

static void error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stdout, "error: ");
	vfprintf(stdout, fmt, ap);
	fputc('\n', stdout);
	va_end(ap);
}

static void lex(void) {
	while (isspace((unsigned char)*src)) src++;
	if (!*src) { tok.kind = TK_END; return; }
	if (isdigit((unsigned char)*src)) { tok.kind = TK_NUM; tok.num = strtol(src, (char **)&src, 10); return; }
	if (isalpha((unsigned char)*src) || *src == '_') {
		int n = 0;
		while (isalnum((unsigned char)*src) || *src == '_') tok.text[n++] = *src++;
		tok.text[n] = 0; tok.kind = TK_ID; return;
	}
	if (*src == '"') {
		int n = 0; src++;
		while (*src && *src != '"') tok.text[n++] = *src++;
		tok.text[n] = 0; if (*src) src++; tok.kind = TK_STR; return;
	}
	tok.kind = TK_OP; tok.text[0] = *src++; tok.text[1] = 0;
	if ((tok.text[0] == '=' || tok.text[0] == '!' || tok.text[0] == '<' || tok.text[0] == '>') && *src == '=') {
		tok.text[1] = *src++; tok.text[2] = 0;
	}
}
static int is_op(const char *s) { return tok.kind == TK_OP && !strcmp(tok.text, s); }

typedef long (*BinFn)(long, long);
static long f_add(long a, long b) { return a + b; }
static long f_sub(long a, long b) { return a - b; }
static long f_mul(long a, long b) { return a * b; }
static long f_div(long a, long b) { return b ? a / b : 0; }
static long f_mod(long a, long b) { return b ? a % b : 0; }
static long f_lt(long a, long b) { return a < b; }
static long f_eq(long a, long b) { return a == b; }
static struct { const char *op; int prec; BinFn fn; } binops[] = {
	{ "==", 1, f_eq }, { "<", 2, f_lt }, { "+", 3, f_add }, { "-", 3, f_sub },
	{ "*", 4, f_mul }, { "/", 4, f_div }, { "%", 4, f_mod },
};

static Value *parse_expr(int minprec);

static Value *parse_primary(void) {
	if (tok.kind == TK_NUM) { Value *v = v_int(tok.num); lex(); return v; }
	if (tok.kind == TK_STR) { Value *v = v_str(tok.text); lex(); return v; }
	if (tok.kind == TK_ID) {
		char name[64];
		strcpy(name, tok.text);
		lex();
		if (is_op("(")) {
			lex();
			Value *args = v_list();
			while (!is_op(")")) { list_push(args, parse_expr(0)); if (is_op(",")) lex(); }
			lex();
			if (!strcmp(name, "len")) {
				Value *a = args->u.list.items[0];
				return v_int(a->kind == V_STR ? (long)strlen(a->u.s) : a->u.list.n);
			}
			if (!strcmp(name, "list")) return args;
			if (!strcmp(name, "sum")) {
				long s = 0;
				Value *l = args->u.list.items[0];
				for (int i = 0; i < l->u.list.n; i++) s += l->u.list.items[i]->u.i;
				return v_int(s);
			}
			error("unknown function %s", name);
			return v_int(0);
		}
		Value *v = get_var(name);
		if (!v) { error("undefined variable '%s'", name); return v_int(0); }
		return v;
	}
	if (is_op("(")) { lex(); Value *v = parse_expr(0); if (is_op(")")) lex(); return v; }
	if (is_op("-")) { lex(); Value *v = parse_primary(); return v_int(-v->u.i); }
	error("unexpected '%s'", tok.text);
	lex();
	return v_int(0);
}

static Value *parse_expr(int minprec) {
	Value *l = parse_primary();
	for (;;) {
		int found = -1;
		for (int i = 0; i < (int)(sizeof binops / sizeof binops[0]); i++)
			if (is_op(binops[i].op) && binops[i].prec >= minprec) { found = i; break; }
		if (found < 0) return l;
		lex();
		Value *r = parse_expr(binops[found].prec + 1);
		if (l->kind == V_STR && r->kind == V_STR && binops[found].fn == f_add) {
			char *s = malloc(strlen(l->u.s) + strlen(r->u.s) + 1);
			strcpy(s, l->u.s); strcat(s, r->u.s);
			l = v_str(s);
			free(s);
		} else l = v_int(binops[found].fn(l->u.i, r->u.i));
	}
}

static void run_line(const char *line) {
	src = line;
	lex();
	if (tok.kind == TK_ID) {
		const char *save = src;
		Token t = tok;
		lex();
		if (is_op("=")) {
			lex();
			set_var(t.text, parse_expr(0));
			return;
		}
		src = save;
		tok = t;
	}
	Value *v = parse_expr(0);
	print_value(stdout, v);
	putchar('\n');
}

typedef struct { char name[16]; int score; } Player;
static int by_score(const void *a, const void *b) {
	const Player *x = a, *y = b;
	if (x->score != y->score) return y->score - x->score;
	return strcmp(x->name, y->name);
}

static int process(int n) {
	int *buf = malloc(n * sizeof(int));
	int result = -1;
	if (!buf) goto out;
	for (int i = 0; i < n; i++) buf[i] = i * i % 17;
	result = 0;
	for (int i = 0; i < n; i++) {
		if (buf[i] == 16) goto done;
		result += buf[i];
	}
done:
	free(buf);
out:
	return result;
}

int main(void) {
	const char *prog[] = {
"x = 6 * 7",
"y = x - 2 * (3 + 4)",
"x + y",
"s = \"hello\" + \", \" + \"world\"",
"s",
"len(s)",
"l = list(1, 2, 3, x, list(y, \"z\"))",
"l",
"len(l)",
"sum(list(1, 2, 3, 4, 100))",
"10 % 4 == 2",
"3 < 2",
"-x + 50",
"undefined_thing + 1",
"foo(1)",
	};
	for (int i = 0; i < (int)(sizeof prog / sizeof prog[0]); i++) run_line(prog[i]);
	Player ps[] = { { "carol", 50 }, { "alice", 90 }, { "bob", 50 }, { "dave", 70 } };
	qsort(ps, 4, sizeof(Player), by_score);
	for (int i = 0; i < 4; i++) printf("%-6s %3d\n", ps[i].name, ps[i].score);
	printf("%d %d\n", process(3), process(10));
	assert(process(3) == 5);
	int words = 0;
	char text[] = "the quick  brown fox\tjumps";
	for (char *w = strtok(text, " \t"); w; w = strtok(NULL, " \t")) words++;
	printf("words=%d\n", words);
	return 0;
}
