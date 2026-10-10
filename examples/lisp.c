#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum { NUM, SYM, CONS, PRIM, LAMBDA, NIL } Kind;

typedef struct Obj Obj;
typedef Obj *(*Prim)(Obj *args);

struct Obj {
	Kind kind;
	long num;
	char *sym;
	Obj *car, *cdr;
	Prim prim;
	Obj *env;
};

static Obj nil_obj = { NIL, 0, NULL, NULL, NULL, NULL, NULL };
static Obj *nil = &nil_obj;
static Obj *symbols;
static Obj *truth;
static const char *src;

static Obj *alloc(Kind k) {
	Obj *o = calloc(1, sizeof *o);
	o->kind = k;
	return o;
}

static Obj *cons(Obj *a, Obj *b) {
	Obj *o = alloc(CONS);
	o->car = a;
	o->cdr = b;
	return o;
}

static Obj *num(long v) {
	Obj *o = alloc(NUM);
	o->num = v;
	return o;
}

static Obj *intern(const char *name) {
	for (Obj *s = symbols; s != nil; s = s->cdr)
		if (strcmp(s->car->sym, name) == 0) return s->car;
	Obj *o = alloc(SYM);
	o->sym = malloc(strlen(name) + 1);
	strcpy(o->sym, name);
	symbols = cons(o, symbols);
	return o;
}

static Obj *read_expr(void);

static void skip_space(void) {
	for (;;) {
		while (isspace((unsigned char)*src)) src++;
		if (*src != ';') return;
		while (*src && *src != '\n') src++;
	}
}

static Obj *read_list(void) {
	skip_space();
	if (*src == ')') {
		src++;
		return nil;
	}
	Obj *head = read_expr();
	return cons(head, read_list());
}

static Obj *read_expr(void) {
	skip_space();
	if (*src == '(') {
		src++;
		return read_list();
	}
	if (*src == '\'') {
		src++;
		return cons(intern("quote"), cons(read_expr(), nil));
	}
	char buf[64];
	int n = 0;
	while (*src && !isspace((unsigned char)*src) && *src != '(' && *src != ')' && n < 63) buf[n++] = *src++;
	buf[n] = 0;
	if (isdigit((unsigned char)buf[0]) || (buf[0] == '-' && isdigit((unsigned char)buf[1]))) return num(strtol(buf, NULL, 10));
	return intern(buf);
}

static void print(Obj *o) {
	switch (o->kind) {
	case NUM: printf("%ld", o->num); break;
	case SYM: printf("%s", o->sym); break;
	case NIL: printf("()"); break;
	case PRIM: printf("<primitive>"); break;
	case LAMBDA: printf("<lambda>"); break;
	case CONS:
		putchar('(');
		for (;;) {
			print(o->car);
			o = o->cdr;
			if (o->kind != CONS) break;
			putchar(' ');
		}
		if (o != nil) {
			printf(" . ");
			print(o);
		}
		putchar(')');
		break;
	}
}

static Obj *lookup(Obj *env, Obj *sym) {
	for (; env != nil; env = env->cdr)
		if (env->car->car == sym) return env->car;
	return NULL;
}

static Obj *eval(Obj *o, Obj *env);

static Obj *eval_list(Obj *l, Obj *env) {
	if (l == nil) return nil;
	Obj *head = eval(l->car, env);
	return cons(head, eval_list(l->cdr, env));
}

static Obj *global;

static Obj *eval(Obj *o, Obj *env) {
	for (;;) {
		if (o->kind == SYM) {
			Obj *b = lookup(env, o);
			if (!b) {
				printf("unbound symbol %s\n", o->sym);
				exit(1);
			}
			return b->cdr;
		}
		if (o->kind != CONS) return o;
		Obj *op = o->car, *args = o->cdr;
		if (op == intern("quote")) return args->car;
		if (op == intern("if")) {
			o = eval(args->car, env) != nil ? args->cdr->car : (args->cdr->cdr != nil ? args->cdr->cdr->car : nil);
			continue;
		}
		if (op == intern("define")) {
			Obj *v = eval(args->cdr->car, env);
			global->cdr = cons(cons(args->car, v), global->cdr);
			return v;
		}
		if (op == intern("lambda")) {
			Obj *l = alloc(LAMBDA);
			l->car = args->car;
			l->cdr = args->cdr->car;
			l->env = env;
			return l;
		}
		Obj *fn = eval(op, env);
		Obj *vals = eval_list(args, env);
		if (fn->kind == PRIM) return fn->prim(vals);
		if (fn->kind != LAMBDA) {
			printf("not a function\n");
			exit(1);
		}
		Obj *e = fn->env;
		for (Obj *p = fn->car; p != nil; p = p->cdr, vals = vals->cdr) e = cons(cons(p->car, vals->car), e);
		env = e;
		o = fn->cdr;
	}
}

static Obj *p_add(Obj *a) { long s = 0; for (; a != nil; a = a->cdr) s += a->car->num; return num(s); }
static Obj *p_mul(Obj *a) { long s = 1; for (; a != nil; a = a->cdr) s *= a->car->num; return num(s); }
static Obj *p_sub(Obj *a) { return num(a->cdr == nil ? -a->car->num : a->car->num - a->cdr->car->num); }
static Obj *p_lt(Obj *a) { return a->car->num < a->cdr->car->num ? truth : nil; }
static Obj *p_eq(Obj *a) {
	Obj *x = a->car, *y = a->cdr->car;
	return (x == y || (x->kind == NUM && y->kind == NUM && x->num == y->num)) ? truth : nil;
}
static Obj *p_cons(Obj *a) { return cons(a->car, a->cdr->car); }
static Obj *p_car(Obj *a) { return a->car->car; }
static Obj *p_cdr(Obj *a) { return a->car->cdr; }
static Obj *p_null(Obj *a) { return a->car == nil ? truth : nil; }
static Obj *p_list(Obj *a) { return a; }

static void def_prim(const char *name, Prim p) {
	Obj *o = alloc(PRIM);
	o->prim = p;
	global->cdr = cons(cons(intern(name), o), global->cdr);
}

int main(void) {
	static char program[8192];
	size_t n = fread(program, 1, sizeof program - 1, stdin);
	program[n] = 0;
	symbols = nil;
	truth = intern("t");
	global = cons(cons(truth, truth), nil);
	def_prim("+", p_add);
	def_prim("*", p_mul);
	def_prim("-", p_sub);
	def_prim("<", p_lt);
	def_prim("=", p_eq);
	def_prim("cons", p_cons);
	def_prim("car", p_car);
	def_prim("cdr", p_cdr);
	def_prim("null?", p_null);
	def_prim("list", p_list);
	src = program;
	for (;;) {
		skip_space();
		if (!*src) break;
		Obj *e = read_expr();
		print(eval(e, global));
		putchar('\n');
	}
	return 0;
}
