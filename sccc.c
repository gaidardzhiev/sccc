/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

typedef struct {
	uint8_t *data;
	size_t cap, len;
} Buf;

static void buf_init(Buf *b) { b->data = NULL; b->cap = b->len = 0; }

static void buf_reserve(Buf *b, size_t need) {
	if (b->cap >= need) return;
	size_t ncap = b->cap ? b->cap * 2 : 256;
	while (ncap < need) ncap *= 2;
	b->data = (uint8_t *)realloc(b->data, ncap);
	if (!b->data) { perror("realloc"); exit(1); }
	b->cap = ncap;
}

static void buf_append(Buf *b, const void *src, size_t n) {
	if (n == 0) return;
	buf_reserve(b, b->len + n);
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

static void buf_append_byte(Buf *b, uint8_t x) { buf_append(b, &x, 1); }

static void buf_zero_to(Buf *b, size_t n) {
	if (b->len >= n) return;
	buf_reserve(b, n);
	memset(b->data + b->len, 0, n - b->len);
	b->len = n;
}

static void buf_patch32_at(Buf *b, size_t off, uint32_t x) {
	if (off + 4 > b->len) { fprintf(stderr, "patch32 OOB\n"); exit(1); }
	b->data[off+0] = (uint8_t)(x);
	b->data[off+1] = (uint8_t)(x >> 8);
	b->data[off+2] = (uint8_t)(x >> 16);
	b->data[off+3] = (uint8_t)(x >> 24);
}

static uint32_t buf_read32_at(Buf *b, size_t off) {
	return (uint32_t)b->data[off] | ((uint32_t)b->data[off+1] << 8) |
		((uint32_t)b->data[off+2] << 16) | ((uint32_t)b->data[off+3] << 24);
}

typedef struct {
	size_t *v;
	int n, cap;
} Patches;

static void patches_push(Patches *p, size_t x) {
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 8;
		p->v = (size_t *)realloc(p->v, sizeof(size_t) * p->cap);
		if (!p->v) { perror("realloc"); exit(1); }
	}
	p->v[p->n++] = x;
}

static void le16(uint8_t *p, uint16_t x) { p[0]=x; p[1]=x>>8; }
static void le32(uint8_t *p, uint32_t x) { p[0]=x; p[1]=x>>8; p[2]=x>>16; p[3]=x>>24; }
static void le64(uint8_t *p, uint64_t x) { for(int i=0;i<8;i++) p[i]=(uint8_t)(x>>(8*i)); }

static size_t align_up(size_t x, size_t a) { return (x + a - 1) / a * a; }

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	char *s = (char *)malloc(n + 1);
	if (!s) { perror("malloc"); exit(1); }
	if (fread(s, 1, n, f) != (size_t)n) { perror("fread"); exit(1); }
	s[n] = '\0'; fclose(f); return s;
}

#define INTERN_SIZE 8192

typedef struct Str Str;
struct Str {
	char *s;
	int len;
	int kw;
	void *macro;
	Str *next;
};

static Str *intern_tab[INTERN_SIZE];

static Str *intern_str(const char *s, int len) {
	uint32_t h = 2166136261u;
	for (int i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
	Str **slot = &intern_tab[h % INTERN_SIZE];
	for (Str *e = *slot; e; e = e->next)
		if (e->len == len && memcmp(e->s, s, len) == 0) return e;
	Str *e = (Str *)calloc(1, sizeof(Str));
	e->s = (char *)malloc(len + 1);
	memcpy(e->s, s, len);
	e->s[len] = '\0';
	e->len = len;
	e->next = *slot;
	*slot = e;
	return e;
}

static char *intern(const char *s) { return intern_str(s, (int)strlen(s))->s; }

typedef enum {
	T_EOF = 0,
	T_LP = '(',
	T_RP = ')',
	T_LB = '{',
	T_RB = '}',
	T_LBRACKET = '[',
	T_RBRACKET = ']',
	T_SEMI = ';',
	T_COMMA = ',',
	T_DOT = '.',
	T_PLUS = '+',
	T_MINUS = '-',
	T_MUL = '*',
	T_DIV = '/',
	T_MOD = '%',
	T_ASSIGN = '=',
	T_LT = '<',
	T_GT = '>',
	T_NOT = '!',
	T_TILDE = '~',
	T_AMP = '&',
	T_PIPE = '|',
	T_CARET = '^',
	T_QUESTION = '?',
	T_COLON = ':',
	T_HASH = '#',
	T_INT = 256, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE,
	T_EQ, T_NEQ, T_LEQ, T_GEQ,
	T_AND,
	T_OR,
	T_SHL, T_SHR, T_INC, T_DEC, T_ARROW, T_ELLIPSIS, T_HASHHASH,
	T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN, T_MOD_ASSIGN,
	T_AND_ASSIGN, T_OR_ASSIGN, T_XOR_ASSIGN, T_SHL_ASSIGN, T_SHR_ASSIGN,
	T_IDENT, T_NUM, T_FNUM, T_STR
} TKind;

typedef struct {
	TKind kind;
	long long val;
	double fval;
	int is_float;
	int is_unsigned;
	int is_long;
	char *str;
	int len;
	Str *id;
	int line;
	const char *file;
	int bol;
	int space;
	void *hide;
} Tok;

typedef struct {
	const char *s;
	size_t i, n;
	int line;
	const char *file;
	int bol, space;
} Lex;

static const char *kw_names[] = {
"int", "return", "if", "else", "while", "break", "continue",
"char", "short", "long", "signed", "unsigned", "void", "_Bool",
"struct", "union", "enum", "typedef", "static", "extern",
"const", "volatile", "inline", "register", "auto", "restrict",
"sizeof", "for", "do", "switch", "case", "default", "goto",
"float", "double",
"__inline", "__restrict", "__const", "__signed__", "__inline__", "__restrict__",
	NULL
};
static const int kw_kinds[] = {
	T_INT, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE,
	T_INLINE, T_RESTRICT, T_CONST, T_SIGNED, T_INLINE, T_RESTRICT,
	0
};

static void keywords_init(void) {
	for (int i = 0; kw_names[i]; i++)
		intern_str(kw_names[i], (int)strlen(kw_names[i]))->kw = kw_kinds[i];
}

static void lex_error(Lex *L, const char *msg) {
	fprintf(stderr, "%s:%d: error: %s\n", L->file, L->line, msg);
	exit(1);
}

static void skip_ws(Lex *L) {
	while (L->i < L->n) {
		char c = L->s[L->i];
		if (c=='\n') { L->i++; L->line++; L->bol = 1; L->space = 1; continue; }
		if (c==' '||c=='\t'||c=='\r'||c=='\f'||c=='\v') { L->i++; L->space = 1; continue; }
		if (c=='\\' && L->i+1<L->n && L->s[L->i+1]=='\n') { L->i+=2; L->line++; continue; }
		if (c=='\\' && L->i+2<L->n && L->s[L->i+1]=='\r' && L->s[L->i+2]=='\n') { L->i+=3; L->line++; continue; }
		if (c=='/' && L->i+1<L->n && L->s[L->i+1]=='/') {
			L->i+=2; while (L->i<L->n && L->s[L->i]!='\n') L->i++;
			L->space = 1; continue;
		}
		if (c=='/' && L->i+1<L->n && L->s[L->i+1]=='*') {
			L->i+=2;
			while (L->i+1<L->n && !(L->s[L->i]=='*' && L->s[L->i+1]=='/')) {
				if (L->s[L->i]=='\n') L->line++;
				L->i++;
			}
			if (L->i+1<L->n) L->i+=2; else lex_error(L, "unterminated comment");
			L->space = 1; continue;
		}
		break;
	}
}

static int hexval(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return c - 'A' + 10;
}

static int lex_escape(Lex *L) {
	char c = L->s[L->i++];
	if (c == 'n') return '\n';
	if (c == 't') return '\t';
	if (c == 'r') return '\r';
	if (c == 'a') return 7;
	if (c == 'b') return 8;
	if (c == 'f') return 12;
	if (c == 'v') return 11;
	if (c == 'e') return 27;
	if (c == 'x') {
		int v = 0;
		while (L->i < L->n && isxdigit((unsigned char)L->s[L->i]))
			v = v * 16 + hexval(L->s[L->i++]);
		return v;
	}
	if (c >= '0' && c <= '7') {
		int v = c - '0';
		for (int k = 0; k < 2 && L->i < L->n && L->s[L->i] >= '0' && L->s[L->i] <= '7'; k++)
			v = v * 8 + (L->s[L->i++] - '0');
		return v;
	}
	return c;
}

static Tok lex_next(Lex *L) {
	L->space = 0;
	skip_ws(L);
	Tok t;
	memset(&t, 0, sizeof(t));
	t.kind = T_EOF;
	t.line = L->line; t.file = L->file;
	t.bol = L->bol; t.space = L->space;
	if (L->i >= L->n) { t.bol = 1; return t; }
	L->bol = 0;
	char c = L->s[L->i];

	if (isalpha((unsigned char)c) || c=='_' || c=='$') {
		size_t j=L->i;
		while (j<L->n && (isalnum((unsigned char)L->s[j]) || L->s[j]=='_' || L->s[j]=='$')) j++;
		Str *e = intern_str(L->s+L->i, (int)(j-L->i));
		t.kind = e->kw ? (TKind)e->kw : T_IDENT;
		t.str = e->s;
		t.id = e;
		L->i=j; return t;
	}

	if (isdigit((unsigned char)c) || (c=='.' && L->i+1<L->n && isdigit((unsigned char)L->s[L->i+1]))) {
		size_t j = L->i;
		int is_hex = c=='0' && j+1<L->n && (L->s[j+1]=='x' || L->s[j+1]=='X');
		int is_flt = 0;
		while (j < L->n) {
			char d = L->s[j];
			if ((d=='e' || d=='E' || ((d=='p' || d=='P') && is_hex)) && j+1<L->n &&
				(L->s[j+1]=='+' || L->s[j+1]=='-')) {
				if (!is_hex || d=='p' || d=='P') is_flt = 1;
				j += 2;
				continue;
			}
			if (d=='.') { is_flt = 1; j++; continue; }
			if (isalnum((unsigned char)d) || d=='_') {
				if (!is_hex && (d=='e' || d=='E')) is_flt = 1;
				if (is_hex && (d=='p' || d=='P')) is_flt = 1;
				j++;
				continue;
			}
			break;
		}
		if (is_flt) {
			char buf[128];
			size_t len = j - L->i;
			if (len >= sizeof(buf)) lex_error(L, "number literal too long");
			memcpy(buf, L->s + L->i, len);
			buf[len] = 0;
			char *end;
			t.fval = strtod(buf, &end);
			if (*end == 'f' || *end == 'F') { t.is_float = 1; end++; }
			else if (*end == 'l' || *end == 'L') end++;
			if (*end) lex_error(L, "invalid floating constant");
			t.kind = T_FNUM;
			L->i = j;
			return t;
		}
		unsigned long long v=0;
		int base = 10;
		if (is_hex) { base = 16; L->i += 2; }
		else if (c=='0' && L->i+1<L->n && (L->s[L->i+1]=='b' || L->s[L->i+1]=='B')) { base = 2; L->i += 2; }
		else if (c=='0') base = 8;
		for (;;) {
			char d = L->i<L->n ? L->s[L->i] : 0;
			int dv;
			if (d>='0' && d<='9') dv = d-'0';
			else if (base==16 && isxdigit((unsigned char)d)) dv = hexval(d);
			else break;
			if (dv >= base) lex_error(L, "invalid digit in number");
			v = v*base + dv; L->i++;
		}
		int u = 0, l = 0;
		for (;;) {
			char d = L->i<L->n ? L->s[L->i] : 0;
			if (d=='u' || d=='U') { u = 1; L->i++; }
			else if (d=='l' || d=='L') { l = 1; L->i++; }
			else break;
		}
		if (L->i != j) lex_error(L, "invalid number literal");
		if (!l && !u) {
			if (v > 0x7fffffffULL) {
				if (base != 10 && v <= 0xffffffffULL) u = 1;
				else { l = 1; if (base != 10 && v > 0x7fffffffffffffffULL) u = 1; }
			}
		} else if (u && !l) {
			if (v > 0xffffffffULL) l = 1;
		} else if (l && !u) {
			if (base != 10 && v > 0x7fffffffffffffffULL) u = 1;
		}
		t.kind=T_NUM; t.val=(long long)v; t.is_unsigned = u; t.is_long = l;
		if (!l) t.val = u ? (long long)(uint32_t)v : (long long)(int32_t)v;
		return t;
	}

	if (c == '\'') {
		L->i++;
		int v = 0;
		if (L->i >= L->n) lex_error(L, "unterminated character literal");
		if (L->s[L->i] == '\\') { L->i++; v = lex_escape(L); }
		else v = (unsigned char)L->s[L->i++];
		while (L->i < L->n && L->s[L->i] != '\'') {
			int w;
			if (L->s[L->i] == '\\') { L->i++; w = lex_escape(L); }
			else w = (unsigned char)L->s[L->i++];
			v = (v << 8) | (w & 0xff);
		}
		if (L->i >= L->n) lex_error(L, "unterminated character literal");
		L->i++;
		t.kind = T_NUM; t.val = (signed char)v;
		if (v > 255 || v < -128) t.val = v;
		return t;
	}

	if (c == '"') {
		L->i++;
		Buf b; buf_init(&b);
		while (L->i < L->n && L->s[L->i] != '"') {
			if (L->s[L->i] == '\n') lex_error(L, "unterminated string literal");
			if (L->s[L->i] == '\\') { L->i++; buf_append_byte(&b, (uint8_t)lex_escape(L)); }
			else buf_append_byte(&b, (uint8_t)L->s[L->i++]);
		}
		if (L->i >= L->n) lex_error(L, "unterminated string literal");
		L->i++;
		buf_append_byte(&b, 0);
		t.kind = T_STR; t.str = (char *)b.data; t.len = (int)b.len - 1;
		return t;
	}

	char c2=(L->i+1<L->n)?L->s[L->i+1]:'\0';
	char c3=(L->i+2<L->n)?L->s[L->i+2]:'\0';
	if (c=='.'&&c2=='.'&&c3=='.') { L->i+=3; t.kind=T_ELLIPSIS; return t; }
	if (c=='<'&&c2=='<'&&c3=='=') { L->i+=3; t.kind=T_SHL_ASSIGN; return t; }
	if (c=='>'&&c2=='>'&&c3=='=') { L->i+=3; t.kind=T_SHR_ASSIGN; return t; }

	TKind k2 = T_EOF;
	if (c=='='&&c2=='=') k2=T_EQ;
	else if (c=='!'&&c2=='=') k2=T_NEQ;
	else if (c=='<'&&c2=='=') k2=T_LEQ;
	else if (c=='>'&&c2=='=') k2=T_GEQ;
	else if (c=='&'&&c2=='&') k2=T_AND;
	else if (c=='|'&&c2=='|') k2=T_OR;
	else if (c=='<'&&c2=='<') k2=T_SHL;
	else if (c=='>'&&c2=='>') k2=T_SHR;
	else if (c=='+'&&c2=='+') k2=T_INC;
	else if (c=='-'&&c2=='-') k2=T_DEC;
	else if (c=='-'&&c2=='>') k2=T_ARROW;
	else if (c=='#'&&c2=='#') k2=T_HASHHASH;
	else if (c=='+'&&c2=='=') k2=T_ADD_ASSIGN;
	else if (c=='-'&&c2=='=') k2=T_SUB_ASSIGN;
	else if (c=='*'&&c2=='=') k2=T_MUL_ASSIGN;
	else if (c=='/'&&c2=='=') k2=T_DIV_ASSIGN;
	else if (c=='%'&&c2=='=') k2=T_MOD_ASSIGN;
	else if (c=='&'&&c2=='=') k2=T_AND_ASSIGN;
	else if (c=='|'&&c2=='=') k2=T_OR_ASSIGN;
	else if (c=='^'&&c2=='=') k2=T_XOR_ASSIGN;
	if (k2 != T_EOF) { L->i+=2; t.kind=k2; return t; }

	if (strchr("(){}[];,.+-*/%=<>!~&|^?:#", c)) {
		L->i++; t.kind = (TKind)c; return t;
	}
	fprintf(stderr,"%s:%d: error: unexpected character '%c'\n", L->file, L->line, c);
	exit(1);
}

static void lex_init(Lex *L, const char *src, const char *file) {
	L->s=src; L->i=0; L->n=strlen(src); L->line=1; L->file=file; L->bol=1; L->space=0;
}

typedef enum {
	TY_VOID, TY_BOOL, TY_CHAR, TY_SHORT, TY_INT, TY_LONG, TY_FLOAT, TY_DOUBLE,
	TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT
} TyKind;

typedef struct Type Type;
typedef struct Member Member;
typedef struct Param Param;

struct Member {
	char *name;
	Type *ty;
	int off;
	Member *next;
};

struct Param {
	char *name;
	Type *ty;
	Param *next;
};

struct Type {
	TyKind kind;
	int size, align;
	int is_unsigned;
	Type *base;
	int len;
	Member *members;
	int is_union;
	int complete;
	Param *params;
	int nparams;
	int variadic;
	int oldstyle;
	Type *ptr;
	int bit_width;
	int bit_off;
	Type *origin;
};

static Type *ty_void, *ty_bool, *ty_char, *ty_uchar, *ty_short, *ty_ushort;
static Type *ty_int, *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double;

static Type *new_type(TyKind kind, int size, int align) {
	Type *t = (Type *)calloc(1, sizeof(Type));
	t->kind = kind; t->size = size; t->align = align;
	return t;
}

static Type *new_int_type(TyKind kind, int size, int is_unsigned) {
	Type *t = new_type(kind, size, size);
	t->is_unsigned = is_unsigned;
	return t;
}

static void types_init(void) {
	ty_void = new_type(TY_VOID, 1, 1);
	ty_bool = new_int_type(TY_BOOL, 1, 1);
	ty_char = new_int_type(TY_CHAR, 1, 0);
	ty_uchar = new_int_type(TY_CHAR, 1, 1);
	ty_short = new_int_type(TY_SHORT, 2, 0);
	ty_ushort = new_int_type(TY_SHORT, 2, 1);
	ty_int = new_int_type(TY_INT, 4, 0);
	ty_uint = new_int_type(TY_INT, 4, 1);
	ty_long = new_int_type(TY_LONG, 8, 0);
	ty_ulong = new_int_type(TY_LONG, 8, 1);
	ty_float = new_type(TY_FLOAT, 4, 4);
	ty_float->is_unsigned = 1;
	ty_double = new_type(TY_DOUBLE, 8, 8);
}

static Type *ptr_to(Type *base) {
	if (base->ptr) return base->ptr;
	Type *t = new_type(TY_PTR, 8, 8);
	t->base = base;
	t->is_unsigned = 1;
	base->ptr = t;
	return t;
}

static Type *array_of(Type *base, int len) {
	Type *t = new_type(TY_ARRAY, len < 0 ? 0 : base->size * len, base->align);
	t->base = base;
	t->len = len;
	return t;
}

static Type *func_type(Type *ret) {
	Type *t = new_type(TY_FUNC, 1, 1);
	t->base = ret;
	return t;
}

static Type *bitfield_of(Type *base, int width, int off) {
	Type *t = new_type(base->kind, base->size, base->align);
	*t = *base;
	t->ptr = NULL;
	t->bit_width = width;
	t->bit_off = off;
	t->origin = base;
	return t;
}

static int is_integer(Type *t) { return t->kind >= TY_BOOL && t->kind <= TY_LONG; }
static int is_ptr(Type *t) { return t->kind == TY_PTR; }
static int is_flonum(Type *t) { return t->kind == TY_FLOAT || t->kind == TY_DOUBLE; }
static int is_arith(Type *t) { return is_integer(t) || is_flonum(t); }
static int is_scalar(Type *t) { return is_arith(t) || is_ptr(t); }

static Type *int_promote(Type *t) {
	if (t->origin) t = t->origin;
	if (is_integer(t) && t->size < 4) return ty_int;
	return t;
}

static Type *arith_conv(Type *a, Type *b) {
	if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) return ty_double;
	if (a->kind == TY_FLOAT || b->kind == TY_FLOAT) return ty_float;
	a = int_promote(a); b = int_promote(b);
	if (!is_integer(a)) return ty_long;
	if (!is_integer(b)) return ty_long;
	if (a->size > b->size) return a;
	if (b->size > a->size) return b;
	if (b->is_unsigned) return b;
	return a;
}

static long long const_cast_val(long long v, Type *t) {
	if (t->kind == TY_BOOL) return v != 0;
	if (!is_integer(t)) return v;
	if (t->size == 1) return t->is_unsigned ? (long long)(uint8_t)v : (long long)(int8_t)v;
	if (t->size == 2) return t->is_unsigned ? (long long)(uint16_t)v : (long long)(int16_t)v;
	if (t->size == 4) return t->is_unsigned ? (long long)(uint32_t)v : (long long)(int32_t)v;
	return v;
}

typedef enum { SYM_VAR, SYM_FUNC, SYM_TYPEDEF, SYM_ENUMC } SymKind;
typedef enum { SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS } SecKind;

typedef struct Sym Sym;
struct Sym {
	char *name;
	SymKind kind;
	Type *ty;
	int depth;
	Sym *next;
	int is_local;
	int stack_off;
	long long val;
	Sym *global;
	int is_static;
	int defined;
	SecKind sect;
	long off;
	long size;
	Sym *gnext;
};

typedef struct Tag Tag;
struct Tag {
	char *name;
	Type *ty;
	int depth;
	Tag *next;
};

typedef struct LoopCtx LoopCtx;
struct LoopCtx {
	int is_switch;
	long cont_target;
	Patches breaks;
	Patches conts;
	Type *sw_type;
	long long *case_vals;
	size_t *case_pos;
	int ncase, capcase;
	long default_pos;
	LoopCtx *parent;
};

typedef struct {
	LoopCtx *top;
} LoopStack;

static void loopstack_init(LoopStack *ls) { ls->top = NULL; }

static LoopCtx *loopstack_push(LoopStack *ls, long cont_target, int is_switch) {
	LoopCtx *lc = (LoopCtx *)calloc(1, sizeof(LoopCtx));
	lc->cont_target = cont_target;
	lc->is_switch = is_switch;
	lc->default_pos = -1;
	lc->parent = ls->top;
	ls->top = lc;
	return lc;
}

static void loopstack_pop(LoopStack *ls) {
	if (!ls->top) { fprintf(stderr, "internal: loopstack underflow\n"); exit(1); }
	ls->top = ls->top->parent;
}

static LoopCtx *loopstack_cur(LoopStack *ls) { return ls->top; }

static LoopCtx *loopstack_loop(LoopStack *ls) {
	LoopCtx *lc = ls->top;
	while (lc && lc->is_switch) lc = lc->parent;
	return lc;
}

static LoopCtx *loopstack_switch(LoopStack *ls) {
	LoopCtx *lc = ls->top;
	while (lc && !lc->is_switch) lc = lc->parent;
	return lc;
}

typedef struct { Buf b; } Code;

static void code_init(Code *c) { buf_init(&c->b); }
static size_t Cpos(Code *c) { return c->b.len; }
static void C8 (Code *c, uint8_t x) { buf_append_byte(&c->b, x); }
static void C32(Code *c, uint32_t x) { uint8_t p[4]; le32(p, x); buf_append(&c->b, p, 4); }
static void C64(Code *c, uint64_t x) { uint8_t p[8]; le64(p, x); buf_append(&c->b, p, 8); }

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11 };

static void emit_mov_rax_imm64(Code *c, uint64_t imm) { C8(c,0x48); C8(c,0xB8); C64(c,imm); }
static void emit_add_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x01); C8(c,0xC8); }
static void emit_sub_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x29); C8(c,0xC8); }
static void emit_and_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x21); C8(c,0xC8); }
static void emit_or_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x09); C8(c,0xC8); }
static void emit_xor_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x31); C8(c,0xC8); }
static void emit_imul_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x0F); C8(c,0xAF); C8(c,0xC1); }
static void emit_cqo(Code *c) { C8(c,0x48); C8(c,0x99); }
static void emit_idiv_rcx(Code *c) { C8(c,0x48); C8(c,0xF7); C8(c,0xF9); }
static void emit_div_rcx(Code *c) { C8(c,0x48); C8(c,0xF7); C8(c,0xF1); }
static void emit_xor_edx_edx(Code *c) { C8(c,0x31); C8(c,0xD2); }
static void emit_xor_eax_eax(Code *c) { C8(c,0x31); C8(c,0xC0); }
static void emit_xchg_rax_rcx(Code *c) { C8(c,0x48); C8(c,0x91); }
static void emit_shl_rax_cl(Code *c) { C8(c,0x48); C8(c,0xD3); C8(c,0xE0); }
static void emit_sar_rax_cl(Code *c) { C8(c,0x48); C8(c,0xD3); C8(c,0xF8); }
static void emit_shr_rax_cl(Code *c) { C8(c,0x48); C8(c,0xD3); C8(c,0xE8); }
static void emit_neg_rax(Code *c) { C8(c,0x48); C8(c,0xF7); C8(c,0xD8); }
static void emit_not_rax(Code *c) { C8(c,0x48); C8(c,0xF7); C8(c,0xD0); }
static void emit_push_rbp(Code *c) { C8(c,0x55); }
static void emit_mov_rbp_rsp(Code *c) { C8(c,0x48); C8(c,0x89); C8(c,0xE5); }
static void emit_leave(Code *c) { C8(c,0xC9); }
static void emit_ret(Code *c) { C8(c,0xC3); }
static void emit_syscall(Code *c) { C8(c,0x0F); C8(c,0x05); }
static void emit_rep_movsb(Code *c) { C8(c,0xF3); C8(c,0xA4); }
static void emit_rep_stosb(Code *c) { C8(c,0xF3); C8(c,0xAA); }
static void emit_call_r11(Code *c) { C8(c,0x41); C8(c,0xFF); C8(c,0xD3); }
static void emit_mov_ecx_imm(Code *c, uint32_t x) { C8(c,0xB9); C32(c,x); }
static void emit_mov_rcx_imm64(Code *c, uint64_t x) { C8(c,0x48); C8(c,0xB9); C64(c,x); }

static void emit_mov_rax_imm(Code *c, long long v) {
	if (v >= 0 && v <= 0xFFFFFFFFLL) { C8(c,0xB8); C32(c,(uint32_t)v); }
	else if (v < 0 && v >= -2147483647LL - 1) { C8(c,0x48); C8(c,0xC7); C8(c,0xC0); C32(c,(uint32_t)v); }
	else emit_mov_rax_imm64(c, (uint64_t)v);
}

static void emit_push(Code *c, int reg) { if (reg >= 8) C8(c,0x41); C8(c,(uint8_t)(0x50 + (reg & 7))); }
static void emit_pop(Code *c, int reg) { if (reg >= 8) C8(c,0x41); C8(c,(uint8_t)(0x58 + (reg & 7))); }

static void emit_mov_reg_reg(Code *c, int dst, int src) {
	C8(c,(uint8_t)(0x48 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0)));
	C8(c,0x89);
	C8(c,(uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7)));
}

static void emit_load_rbp(Code *c, int reg, int disp) {
	C8(c,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0))); C8(c,0x8B);
	C8(c,(uint8_t)(0x85 | ((reg & 7) << 3))); C32(c,(uint32_t)disp);
}
static void emit_store_rbp(Code *c, int reg, int disp) {
	C8(c,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0))); C8(c,0x89);
	C8(c,(uint8_t)(0x85 | ((reg & 7) << 3))); C32(c,(uint32_t)disp);
}

static void emit_lea_rbp(Code *c, int reg, int disp) {
	C8(c,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0))); C8(c,0x8D);
	C8(c,(uint8_t)(0x85 | ((reg & 7) << 3))); C32(c,(uint32_t)disp);
}

static void emit_load_rsp(Code *c, int reg, int disp) {
	C8(c,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0))); C8(c,0x8B);
	C8(c,(uint8_t)(0x84 | ((reg & 7) << 3))); C8(c,0x24); C32(c,(uint32_t)disp);
}
static void emit_store_rsp_rax(Code *c, int disp) {
	C8(c,0x48); C8(c,0x89); C8(c,0x84); C8(c,0x24); C32(c,(uint32_t)disp);
}

static void emit_sub_rsp(Code *c, int n) { C8(c,0x48); C8(c,0x81); C8(c,0xEC); C32(c,(uint32_t)n); }
static void emit_add_rsp(Code *c, int n) { C8(c,0x48); C8(c,0x81); C8(c,0xC4); C32(c,(uint32_t)n); }

static void emit_add_rax_imm(Code *c, long long n) {
	if (n == 0) return;
	if (n >= -2147483647LL - 1 && n <= 2147483647LL) { C8(c,0x48); C8(c,0x05); C32(c,(uint32_t)n); }
	else { emit_mov_rcx_imm64(c, (uint64_t)n); emit_add_rax_rcx(c); }
}

static void emit_scale(Code *c, int reg, int n) {
	if (n == 1) return;
	C8(c,0x48); C8(c,0x69); C8(c,(uint8_t)(0xC0 | (reg << 3) | reg)); C32(c,(uint32_t)n);
}

static void emit_load(Code *c, int size, int is_unsigned) {
	switch (size) {
	case 1:
		if (is_unsigned) { C8(c,0x0F); C8(c,0xB6); C8(c,0x00); }
		else { C8(c,0x48); C8(c,0x0F); C8(c,0xBE); C8(c,0x00); }
		break;
	case 2:
		if (is_unsigned) { C8(c,0x0F); C8(c,0xB7); C8(c,0x00); }
		else { C8(c,0x48); C8(c,0x0F); C8(c,0xBF); C8(c,0x00); }
		break;
	case 4:
		if (is_unsigned) { C8(c,0x8B); C8(c,0x00); }
		else { C8(c,0x48); C8(c,0x63); C8(c,0x00); }
		break;
	default:
		C8(c,0x48); C8(c,0x8B); C8(c,0x00);
	}
}

static void emit_store(Code *c, int size) {
	switch (size) {
	case 1: C8(c,0x88); C8(c,0x01); break;
	case 2: C8(c,0x66); C8(c,0x89); C8(c,0x01); break;
	case 4: C8(c,0x89); C8(c,0x01); break;
	default: C8(c,0x48); C8(c,0x89); C8(c,0x01);
	}
}

static void emit_extend(Code *c, int size, int is_unsigned) {
	switch (size) {
	case 1:
		if (is_unsigned) { C8(c,0x0F); C8(c,0xB6); C8(c,0xC0); }
		else { C8(c,0x48); C8(c,0x0F); C8(c,0xBE); C8(c,0xC0); }
		break;
	case 2:
		if (is_unsigned) { C8(c,0x0F); C8(c,0xB7); C8(c,0xC0); }
		else { C8(c,0x48); C8(c,0x0F); C8(c,0xBF); C8(c,0xC0); }
		break;
	case 4:
		if (is_unsigned) { C8(c,0x89); C8(c,0xC0); }
		else { C8(c,0x48); C8(c,0x63); C8(c,0xC0); }
		break;
	}
}

typedef enum { CMP_EQ, CMP_NEQ, CMP_LT, CMP_GT, CMP_LEQ, CMP_GEQ,
		CMP_ULT, CMP_UGT, CMP_ULEQ, CMP_UGEQ } CmpOp;

static void emit_cmp_bool(Code *c, CmpOp op) {
	C8(c,0x48); C8(c,0x39); C8(c,0xC1);
	C8(c,0x0F);
	switch(op) {
	case CMP_EQ: C8(c,0x94); break;
	case CMP_NEQ: C8(c,0x95); break;
	case CMP_LT: C8(c,0x9C); break;
	case CMP_GT: C8(c,0x9F); break;
	case CMP_LEQ: C8(c,0x9E); break;
	case CMP_GEQ: C8(c,0x9D); break;
	case CMP_ULT: C8(c,0x92); break;
	case CMP_UGT: C8(c,0x97); break;
	case CMP_ULEQ: C8(c,0x96); break;
	case CMP_UGEQ: C8(c,0x93); break;
	}
	C8(c,0xC0);
	C8(c,0x48); C8(c,0x0F); C8(c,0xB6); C8(c,0xC0);
}

static void emit_test_rax_rax(Code *c) { C8(c,0x48); C8(c,0x85); C8(c,0xC0); }

static void emit_mov_rax_rdx(Code *c) { C8(c,0x48); C8(c,0x89); C8(c,0xD0); }

static void emit_logical_not(Code *c) {
	C8(c,0x48); C8(c,0x85); C8(c,0xC0);
	C8(c,0x0F); C8(c,0x94); C8(c,0xC0);
	C8(c,0x48); C8(c,0x0F); C8(c,0xB6); C8(c,0xC0);
}

static void emit_to_bool(Code *c) {
	C8(c,0x48); C8(c,0x85); C8(c,0xC0);
	C8(c,0x0F); C8(c,0x95); C8(c,0xC0);
	C8(c,0x48); C8(c,0x0F); C8(c,0xB6); C8(c,0xC0);
}

static size_t emit_jz_placeholder(Code *c) { C8(c,0x0F); C8(c,0x84); size_t o=Cpos(c); C32(c,0); return o; }
static size_t emit_jnz_placeholder(Code *c) { C8(c,0x0F); C8(c,0x85); size_t o=Cpos(c); C32(c,0); return o; }
static size_t emit_jmp_placeholder(Code *c) { C8(c,0xE9); size_t o=Cpos(c); C32(c,0); return o; }

static void patch_jmp_to(Buf *b, size_t placeholder_off, size_t target) {
	int64_t rel = (int64_t)target - (int64_t)(placeholder_off + 4);
	buf_patch32_at(b, placeholder_off, (uint32_t)(int32_t)rel);
}

static void emit_jmp_to(Code *c, size_t target) {
	size_t ph = emit_jmp_placeholder(c);
	patch_jmp_to(&c->b, ph, target);
}

static void emit_je_to(Code *c, size_t target) {
	C8(c,0x0F); C8(c,0x84); size_t ph = Cpos(c); C32(c,0);
	patch_jmp_to(&c->b, ph, target);
}

static void emit_movq_xmm0_rax(Code *c) { C8(c,0x66); C8(c,0x48); C8(c,0x0F); C8(c,0x6E); C8(c,0xC0); }
static void emit_movq_xmm0_rcx(Code *c) { C8(c,0x66); C8(c,0x48); C8(c,0x0F); C8(c,0x6E); C8(c,0xC1); }
static void emit_movq_xmm1_rax(Code *c) { C8(c,0x66); C8(c,0x48); C8(c,0x0F); C8(c,0x6E); C8(c,0xC8); }
static void emit_movq_rax_xmm0(Code *c) { C8(c,0x66); C8(c,0x48); C8(c,0x0F); C8(c,0x7E); C8(c,0xC0); }
static void emit_movd_eax_xmm0(Code *c) { C8(c,0x66); C8(c,0x0F); C8(c,0x7E); C8(c,0xC0); }
static void emit_xorpd_xmm1(Code *c) { C8(c,0x66); C8(c,0x0F); C8(c,0x57); C8(c,0xC9); }

static void emit_xmm0_to_rax(Code *c, int is_double) {
	if (is_double) emit_movq_rax_xmm0(c); else emit_movd_eax_xmm0(c);
}

static void emit_sse_arith(Code *c, int is_double, int op) {
	C8(c,(uint8_t)(is_double ? 0xF2 : 0xF3)); C8(c,0x0F); C8(c,(uint8_t)op); C8(c,0xC1);
}

static void emit_ucomi(Code *c, int is_double, int swap) {
	if (is_double) C8(c,0x66);
	C8(c,0x0F); C8(c,0x2E); C8(c,(uint8_t)(swap ? 0xC8 : 0xC1));
}

static void emit_setcc_al(Code *c, int cc) { C8(c,0x0F); C8(c,(uint8_t)cc); C8(c,0xC0); }
static void emit_setcc_cl(Code *c, int cc) { C8(c,0x0F); C8(c,(uint8_t)cc); C8(c,0xC1); }
static void emit_movzx_eax_al(Code *c) { C8(c,0x0F); C8(c,0xB6); C8(c,0xC0); }

typedef enum { R_PC32, R_ABS64 } RelKind;

typedef struct {
	RelKind kind;
	size_t pos;
	Sym *sym;
	SecKind sect;
	long long addend;
} Reloc;

typedef struct Label Label;
struct Label {
	char *name;
	long pos;
	Patches patches;
	Label *next;
};

typedef struct {
	Sym *sym;
	Type *ty;
	int frame;
	size_t frame_patch;
	int ret_off;
	int named_gp;
	int named_stack;
	Label *labels;
} Func;

typedef struct Macro Macro;
struct Macro {
	Str *id;
	int is_func;
	int variadic;
	int nparams;
	char **params;
	Tok *body;
	int nbody;
	int busy;
	Macro *next;
};

typedef struct Src Src;
struct Src {
	int is_file;
	Lex L;
	char *dir;
	int ncond;
	Tok *toks;
	int ntoks, pos;
	Macro *m;
	Src *parent;
};

typedef struct {
	Tok *v;
	int n, cap;
} TokVec;

static void tv_push(TokVec *tv, Tok t) {
	if (tv->n == tv->cap) {
		tv->cap = tv->cap ? tv->cap * 2 : 16;
		tv->v = (Tok *)realloc(tv->v, sizeof(Tok) * tv->cap);
		if (!tv->v) { perror("realloc"); exit(1); }
	}
	tv->v[tv->n++] = t;
}

typedef struct {
	Src *src;
	Tok pp_back;
	int has_pp_back;
	int *conds;
	int ncond, capcond;
	Macro *macros;
	const char *incdirs[64];
	int nincdirs;
	const char *defines[64];
	int ndefines;
	char **once;
	int nonce, caponce;
	int in_libc;
	int nostdlib;
	Tok tok;
	Tok pk;
	int has_pk;
	Code text;
	Buf rodata, data;
	long bss_size;
	Reloc *relocs;
	int nrelocs, caprelocs;
	Sym *scope;
	Tag *tags;
	int depth;
	Sym *globals;
	LoopStack loops;
	Func *fn;
	int sp;
	int lv;
	size_t fn_lea_end;
	Sym *fn_lea_sym;
	Patches init_patches;
	int in_pp_if;
} Ctx;

static const char *find_builtin_header(const char *name);
static const char *builtin_libc_src(void);

static char *id_main, *id_syscall, *id_va_start, *id_exit, *id_libc_init, *id_environ;
static char *id_defined, *id_file, *id_line, *id_va_args, *id_func, *id_function, *id_static_assert;

static void error_at(Ctx *ctx, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: error: ", ctx->tok.file ? ctx->tok.file : "?", ctx->tok.line);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void warn_at(Ctx *ctx, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: warning: ", ctx->tok.file ? ctx->tok.file : "?", ctx->tok.line);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static const char *tok_desc(Tok *t) {
	static char buf[8];
	if (t->kind == T_EOF) return "end of file";
	if (t->kind == T_IDENT) return t->str;
	if (t->kind == T_NUM || t->kind == T_FNUM) return "number";
	if (t->kind == T_STR) return "string";
	if (t->kind < 256) { buf[0] = (char)t->kind; buf[1] = 0; return buf; }
	if (t->kind >= T_INT && t->kind <= T_DOUBLE) return kw_names[t->kind - T_INT];
	return "operator";
}

static const char *kind_desc(int k) {
	static char buf[8];
	if (k < 256 && k > 0) { buf[0] = (char)k; buf[1] = 0; return buf; }
	if (k == T_IDENT) return "identifier";
	if (k == T_ARROW) return "->";
	return "token";
}

static long long const_expr(Ctx *ctx);
static void next(Ctx *ctx);

static int pp_sentinel_mark;
static int pp_placemarker_mark;

static int is_sentinel(Tok *t) { return t->kind == T_EOF && t->hide == (void *)&pp_sentinel_mark; }
static int is_placemarker(Tok *t) { return t->kind == T_EOF && t->hide == (void *)&pp_placemarker_mark; }

static void pp_error(Ctx *ctx, Lex *L, const char *msg, const char *arg) {
	(void)ctx;
	fprintf(stderr, "%s:%d: error: %s%s\n", L->file, L->line, msg, arg ? arg : "");
	exit(1);
}

static char *dir_of(const char *path) {
	const char *slash = strrchr(path, '/');
	if (!slash) return intern(".");
	int n = (int)(slash - path);
	if (n == 0) return intern("/");
	return intern_str(path, n)->s;
}

static void push_file(Ctx *ctx, const char *text, const char *path, int is_builtin) {
	int depth = 0;
	for (Src *p = ctx->src; p; p = p->parent) if (p->is_file) depth++;
	if (depth > 64) { fprintf(stderr, "%s: error: #include nested too deeply\n", path); exit(1); }
	Src *s = (Src *)calloc(1, sizeof(Src));
	s->is_file = 1;
	lex_init(&s->L, text, intern(path));
	s->dir = is_builtin ? NULL : dir_of(path);
	s->ncond = ctx->ncond;
	s->parent = ctx->src;
	ctx->src = s;
}

static void push_tokens(Ctx *ctx, Tok *toks, int n, Macro *m) {
	Src *s = (Src *)calloc(1, sizeof(Src));
	s->toks = toks;
	s->ntoks = n;
	s->m = m;
	if (m) m->busy = 1;
	s->parent = ctx->src;
	ctx->src = s;
}

static char *read_line(Lex *L) {
	Buf b; buf_init(&b);
	while (L->i < L->n) {
		char c = L->s[L->i];
		if (c == '\n') break;
		if (c == '\\' && L->i+1 < L->n && L->s[L->i+1] == '\n') { L->i += 2; L->line++; continue; }
		if (c == '\\' && L->i+2 < L->n && L->s[L->i+1] == '\r' && L->s[L->i+2] == '\n') { L->i += 3; L->line++; continue; }
		if (c == '/' && L->i+1 < L->n && L->s[L->i+1] == '/') {
			while (L->i < L->n && L->s[L->i] != '\n') L->i++;
			break;
		}
		if (c == '/' && L->i+1 < L->n && L->s[L->i+1] == '*') {
			L->i += 2;
			while (L->i+1 < L->n && !(L->s[L->i] == '*' && L->s[L->i+1] == '/')) {
				if (L->s[L->i] == '\n') L->line++;
				L->i++;
			}
			L->i += 2;
			buf_append_byte(&b, ' ');
			continue;
		}
		if (c == '"' || c == '\'') {
			buf_append_byte(&b, (uint8_t)c);
			L->i++;
			while (L->i < L->n && L->s[L->i] != c && L->s[L->i] != '\n') {
				if (L->s[L->i] == '\\' && L->i+1 < L->n) buf_append_byte(&b, (uint8_t)L->s[L->i++]);
				buf_append_byte(&b, (uint8_t)L->s[L->i++]);
			}
			if (L->i < L->n && L->s[L->i] == c) { buf_append_byte(&b, (uint8_t)c); L->i++; }
			continue;
		}
		buf_append_byte(&b, (uint8_t)c);
		L->i++;
	}
	if (L->i < L->n) { L->i++; L->line++; }
	L->bol = 1;
	buf_append_byte(&b, 0);
	return (char *)b.data;
}

static void skip_line(Lex *L) {
	while (L->i < L->n) {
		char c = L->s[L->i];
		if (c == '\n') { L->i++; L->line++; return; }
		if (c == '\\' && L->i+1 < L->n && L->s[L->i+1] == '\n') { L->i += 2; L->line++; continue; }
		if (c == '/' && L->i+1 < L->n && L->s[L->i+1] == '/') {
			while (L->i < L->n && L->s[L->i] != '\n') L->i++;
			continue;
		}
		if (c == '/' && L->i+1 < L->n && L->s[L->i+1] == '*') {
			L->i += 2;
			while (L->i+1 < L->n && !(L->s[L->i] == '*' && L->s[L->i+1] == '/')) {
				if (L->s[L->i] == '\n') L->line++;
				L->i++;
			}
			L->i += 2;
			continue;
		}
		if (c == '"' || c == '\'') {
			L->i++;
			while (L->i < L->n && L->s[L->i] != c && L->s[L->i] != '\n') {
				if (L->s[L->i] == '\\' && L->i+1 < L->n) L->i++;
				L->i++;
			}
			if (L->i < L->n && L->s[L->i] == c) L->i++;
			continue;
		}
		L->i++;
	}
}

static int word_is(const char *p, int len, const char *w) {
	return (int)strlen(w) == len && strncmp(p, w, len) == 0;
}

static void skip_group(Ctx *ctx, Lex *L) {
	int depth = 0;
	for (;;) {
		if (L->i >= L->n) pp_error(ctx, L, "unterminated conditional directive", NULL);
		size_t start = L->i;
		int line = L->line;
		size_t j = L->i;
		while (j < L->n && (L->s[j] == ' ' || L->s[j] == '\t')) j++;
		if (j < L->n && L->s[j] == '#') {
			size_t k = j + 1;
			while (k < L->n && (L->s[k] == ' ' || L->s[k] == '\t')) k++;
			size_t w = k;
			while (w < L->n && isalpha((unsigned char)L->s[w])) w++;
			const char *d = L->s + k;
			int len = (int)(w - k);
			if (word_is(d, len, "if") || word_is(d, len, "ifdef") || word_is(d, len, "ifndef")) depth++;
			else if (word_is(d, len, "endif")) {
				if (depth == 0) { L->i = start; L->line = line; L->bol = 1; return; }
				depth--;
			} else if (depth == 0 && (word_is(d, len, "elif") || word_is(d, len, "else"))) {
				L->i = start; L->line = line; L->bol = 1; return;
			}
		}
		skip_line(L);
	}
}

static int param_index(Macro *m, Tok *t) {
	if (!m->is_func || !t->id) return -1;
	for (int i = 0; i < m->nparams; i++)
		if (m->params[i] == t->str) return i;
	return -1;
}

static void define_macro(Ctx *ctx, Macro *m) {
	m->id->macro = m;
	m->next = ctx->macros;
	ctx->macros = m;
}

static void pp_define(Ctx *ctx, Lex *sub) {
	Tok nt = lex_next(sub);
	if (!nt.id) pp_error(ctx, sub, "macro name must be an identifier", NULL);
	Macro *m = (Macro *)calloc(1, sizeof(Macro));
	m->id = nt.id;
	Tok t = lex_next(sub);
	if (t.kind == T_LP && !t.space) {
		m->is_func = 1;
		char *params[128];
		t = lex_next(sub);
		if (t.kind != T_RP) {
			for (;;) {
				if (t.kind == T_ELLIPSIS) {
					m->variadic = 1;
					params[m->nparams++] = id_va_args;
					t = lex_next(sub);
					break;
				}
				if (!t.id) pp_error(ctx, sub, "invalid macro parameter list", NULL);
				if (m->nparams >= 127) pp_error(ctx, sub, "too many macro parameters", NULL);
				params[m->nparams++] = t.str;
				t = lex_next(sub);
				if (t.kind != T_COMMA) break;
				t = lex_next(sub);
			}
			if (t.kind != T_RP) pp_error(ctx, sub, "expected ')' in macro parameter list", NULL);
		}
		m->params = (char **)malloc(sizeof(char *) * (m->nparams + 1));
		for (int i = 0; i < m->nparams; i++) m->params[i] = params[i];
		t = lex_next(sub);
	}
	TokVec body;
	memset(&body, 0, sizeof(body));
	while (t.kind != T_EOF) { tv_push(&body, t); t = lex_next(sub); }
	m->body = body.v;
	m->nbody = body.n;
	define_macro(ctx, m);
}

static void pp_define_str(Ctx *ctx, const char *def) {
	Lex sub;
	lex_init(&sub, def, "<command line>");
	pp_define(ctx, &sub);
}

static void cond_push(Ctx *ctx, int taken) {
	if (ctx->ncond == ctx->capcond) {
		ctx->capcond = ctx->capcond ? ctx->capcond * 2 : 16;
		ctx->conds = (int *)realloc(ctx->conds, sizeof(int) * ctx->capcond);
	}
	ctx->conds[ctx->ncond++] = taken;
}

static long long pp_eval(Ctx *ctx, Lex *sub) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	for (;;) {
		Tok t = lex_next(sub);
		if (t.kind == T_EOF) break;
		if (t.id && t.str == id_defined) {
			Tok a = lex_next(sub);
			int paren = a.kind == T_LP;
			if (paren) a = lex_next(sub);
			if (!a.id) pp_error(ctx, sub, "macro name expected after 'defined'", NULL);
			int def = a.id->macro != NULL || a.str == id_file || a.str == id_line;
			if (paren && lex_next(sub).kind != T_RP) pp_error(ctx, sub, "expected ')' after 'defined'", NULL);
			t.kind = T_NUM; t.val = def; t.id = NULL; t.str = NULL;
			t.is_unsigned = 0; t.is_long = 0;
		}
		tv_push(&v, t);
	}
	Tok end;
	memset(&end, 0, sizeof(end));
	end.kind = T_EOF; end.hide = (void *)&pp_sentinel_mark;
	end.file = sub->file; end.line = sub->line;
	tv_push(&v, end);

	Tok saved_tok = ctx->tok, saved_pk = ctx->pk, saved_back = ctx->pp_back;
	int saved_has_pk = ctx->has_pk, saved_has_back = ctx->has_pp_back;
	push_tokens(ctx, v.v, v.n, NULL);
	Src *frame = ctx->src;
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	ctx->in_pp_if = 1;
	next(ctx);
	if (ctx->tok.kind == T_EOF) pp_error(ctx, sub, "#if with no expression", NULL);
	long long r = const_expr(ctx);
	if (!is_sentinel(&ctx->tok)) pp_error(ctx, sub, "extra tokens in #if expression", NULL);
	ctx->in_pp_if = 0;
	if (ctx->src == frame) ctx->src = frame->parent;
	ctx->tok = saved_tok; ctx->pk = saved_pk; ctx->pp_back = saved_back;
	ctx->has_pk = saved_has_pk; ctx->has_pp_back = saved_has_back;
	return r;
}

static int file_readable(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

static int is_once(Ctx *ctx, const char *path) {
	for (int i = 0; i < ctx->nonce; i++)
		if (strcmp(ctx->once[i], path) == 0) return 1;
	return 0;
}

static void pp_include(Ctx *ctx, Src *s, Lex *sub) {
	const char *p = sub->s + sub->i;
	while (*p == ' ' || *p == '\t') p++;
	char close;
	if (*p == '"') close = '"';
	else if (*p == '<') close = '>';
	else pp_error(ctx, sub, "#include expects \"FILENAME\" or <FILENAME>", NULL);
	const char *q = strchr(p + 1, close);
	if (!q) pp_error(ctx, sub, "unterminated #include file name", NULL);
	char name[1024];
	int n = (int)(q - p - 1);
	if (n >= 1000) pp_error(ctx, sub, "#include file name too long", NULL);
	memcpy(name, p + 1, n);
	name[n] = 0;

	char path[2048];
	int found = 0;
	if (name[0] == '/') {
		snprintf(path, sizeof(path), "%s", name);
		found = file_readable(path);
	}
	if (!found && close == '"' && s->dir) {
		snprintf(path, sizeof(path), "%s/%s", s->dir, name);
		found = file_readable(path);
	}
	for (int i = 0; !found && i < ctx->nincdirs; i++) {
		snprintf(path, sizeof(path), "%s/%s", ctx->incdirs[i], name);
		found = file_readable(path);
	}
	if (found) {
		if (is_once(ctx, path)) return;
		push_file(ctx, read_file(path), path, 0);
		return;
	}
	const char *b = find_builtin_header(name);
	if (!b) pp_error(ctx, sub, "file not found: ", name);
	snprintf(path, sizeof(path), "<%s>", name);
	if (is_once(ctx, path)) return;
	push_file(ctx, b, path, 1);
}

static void pp_directive(Ctx *ctx, Src *s) {
	Lex *L = &s->L;
	int line = L->line;
	char *text = read_line(L);
	Lex sub;
	lex_init(&sub, text, L->file);
	sub.line = line;
	Tok d = lex_next(&sub);
	if (d.kind == T_EOF) return;
	if (d.kind == T_NUM) return;
	if (!d.id) pp_error(ctx, &sub, "invalid preprocessing directive", NULL);
	const char *name = d.str;

	if (strcmp(name, "define") == 0) { pp_define(ctx, &sub); return; }
	if (strcmp(name, "undef") == 0) {
		Tok t = lex_next(&sub);
		if (!t.id) pp_error(ctx, &sub, "macro name must be an identifier", NULL);
		t.id->macro = NULL;
		return;
	}
	if (strcmp(name, "include") == 0) { pp_include(ctx, s, &sub); return; }
	if (strcmp(name, "ifdef") == 0 || strcmp(name, "ifndef") == 0) {
		Tok t = lex_next(&sub);
		if (!t.id) pp_error(ctx, &sub, "macro name must be an identifier", NULL);
		int def = t.id->macro != NULL;
		if (name[2] == 'n') def = !def;
		cond_push(ctx, def);
		if (!def) skip_group(ctx, L);
		return;
	}
	if (strcmp(name, "if") == 0) {
		int v = pp_eval(ctx, &sub) != 0;
		cond_push(ctx, v);
		if (!v) skip_group(ctx, L);
		return;
	}
	if (strcmp(name, "elif") == 0) {
		if (ctx->ncond <= s->ncond) pp_error(ctx, &sub, "#elif without #if", NULL);
		if (ctx->conds[ctx->ncond - 1]) { skip_group(ctx, L); return; }
		int v = pp_eval(ctx, &sub) != 0;
		if (v) ctx->conds[ctx->ncond - 1] = 1;
		else skip_group(ctx, L);
		return;
	}
	if (strcmp(name, "else") == 0) {
		if (ctx->ncond <= s->ncond) pp_error(ctx, &sub, "#else without #if", NULL);
		if (ctx->conds[ctx->ncond - 1]) { skip_group(ctx, L); return; }
		ctx->conds[ctx->ncond - 1] = 1;
		return;
	}
	if (strcmp(name, "endif") == 0) {
		if (ctx->ncond <= s->ncond) pp_error(ctx, &sub, "#endif without #if", NULL);
		ctx->ncond--;
		return;
	}
	if (strcmp(name, "error") == 0) pp_error(ctx, &sub, "#error", sub.s + sub.i);
	if (strcmp(name, "warning") == 0) {
		fprintf(stderr, "%s:%d: warning: #warning%s\n", L->file, line, sub.s + sub.i);
		return;
	}
	if (strcmp(name, "pragma") == 0) {
		Tok t = lex_next(&sub);
		if (t.id && strcmp(t.str, "once") == 0) {
			if (ctx->nonce == ctx->caponce) {
				ctx->caponce = ctx->caponce ? ctx->caponce * 2 : 16;
				ctx->once = (char **)realloc(ctx->once, sizeof(char *) * ctx->caponce);
			}
			ctx->once[ctx->nonce++] = (char *)L->file;
		}
		return;
	}
	if (strcmp(name, "line") == 0) {
		Tok t = lex_next(&sub);
		if (t.kind == T_NUM) L->line = (int)t.val;
		return;
	}
	pp_error(ctx, &sub, "invalid preprocessing directive #", name);
}

static Tok raw_next(Ctx *ctx) {
	if (ctx->has_pp_back) { ctx->has_pp_back = 0; return ctx->pp_back; }
	for (;;) {
		Src *s = ctx->src;
		if (!s) {
			Tok t;
			memset(&t, 0, sizeof(t));
			t.kind = T_EOF; t.bol = 1;
			return t;
		}
		if (!s->is_file) {
			if (s->pos < s->ntoks) return s->toks[s->pos++];
			if (s->m) s->m->busy = 0;
			ctx->src = s->parent;
			continue;
		}
		Tok t = lex_next(&s->L);
		if (t.kind == T_HASH && t.bol) { pp_directive(ctx, s); continue; }
		if (t.kind == T_EOF) {
			if (ctx->ncond > s->ncond) pp_error(ctx, &s->L, "unterminated conditional directive", NULL);
			if (!s->parent) return t;
			ctx->src = s->parent;
			continue;
		}
		return t;
	}
}

static const char *punct_str(int k) {
	static char one[2];
	switch (k) {
	case T_EQ: return "=="; case T_NEQ: return "!="; case T_LEQ: return "<=";
	case T_GEQ: return ">="; case T_AND: return "&&"; case T_OR: return "||";
	case T_SHL: return "<<"; case T_SHR: return ">>"; case T_INC: return "++";
	case T_DEC: return "--"; case T_ARROW: return "->"; case T_ELLIPSIS: return "...";
	case T_HASHHASH: return "##";
	case T_ADD_ASSIGN: return "+="; case T_SUB_ASSIGN: return "-="; case T_MUL_ASSIGN: return "*=";
	case T_DIV_ASSIGN: return "/="; case T_MOD_ASSIGN: return "%="; case T_AND_ASSIGN: return "&=";
	case T_OR_ASSIGN: return "|="; case T_XOR_ASSIGN: return "^="; case T_SHL_ASSIGN: return "<<=";
	case T_SHR_ASSIGN: return ">>=";
	}
	one[0] = (char)k;
	one[1] = 0;
	return one;
}

static void tok_spell(Buf *b, Tok *t) {
	char tmp[64];
	if (t->kind == T_EOF) return;
	if (t->id) { buf_append(b, t->str, strlen(t->str)); return; }
	if (t->kind == T_NUM) {
		snprintf(tmp, sizeof(tmp), "%lld%s%s", t->val, t->is_unsigned ? "u" : "", t->is_long ? "l" : "");
		buf_append(b, tmp, strlen(tmp));
		return;
	}
	if (t->kind == T_STR) {
		buf_append_byte(b, '"');
		for (int i = 0; i < t->len; i++) {
			unsigned char c = (unsigned char)t->str[i];
			if (c == '"' || c == '\\') { buf_append_byte(b, '\\'); buf_append_byte(b, c); }
			else if (c == '\n') buf_append(b, "\\n", 2);
			else if (c == '\t') buf_append(b, "\\t", 2);
			else if (c < 32 || c >= 127) { snprintf(tmp, sizeof(tmp), "\\%03o", c); buf_append(b, tmp, 4); }
			else buf_append_byte(b, c);
		}
		buf_append_byte(b, '"');
		return;
	}
	const char *p = punct_str(t->kind);
	buf_append(b, p, strlen(p));
}

static Tok stringify(TokVec *arg, Tok *at) {
	Buf b; buf_init(&b);
	for (int i = 0; i < arg->n; i++) {
		if (i > 0 && arg->v[i].space) buf_append_byte(&b, ' ');
		tok_spell(&b, &arg->v[i]);
	}
	buf_append_byte(&b, 0);
	Tok t;
	memset(&t, 0, sizeof(t));
	t.kind = T_STR;
	t.str = (char *)b.data;
	t.len = (int)b.len - 1;
	t.file = at->file;
	t.line = at->line;
	return t;
}

static Tok paste(Ctx *ctx, Tok *a, Tok *b) {
	if (is_placemarker(a)) return *b;
	if (is_placemarker(b)) return *a;
	Buf s; buf_init(&s);
	tok_spell(&s, a);
	tok_spell(&s, b);
	buf_append_byte(&s, 0);
	Lex L;
	lex_init(&L, (char *)s.data, a->file);
	L.line = a->line;
	Tok t = lex_next(&L);
	if (lex_next(&L).kind != T_EOF) pp_error(ctx, &L, "pasting does not give a valid token: ", (char *)s.data);
	t.file = a->file;
	t.line = a->line;
	t.space = a->space;
	t.bol = 0;
	return t;
}

static Tok expand_next(Ctx *ctx);

static TokVec expand_list(Ctx *ctx, Tok *toks, int n) {
	TokVec in, out;
	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	for (int i = 0; i < n; i++) tv_push(&in, toks[i]);
	Tok end;
	memset(&end, 0, sizeof(end));
	end.kind = T_EOF;
	end.hide = (void *)&pp_sentinel_mark;
	tv_push(&in, end);
	Tok saved_back = ctx->pp_back;
	int saved_has_back = ctx->has_pp_back;
	ctx->has_pp_back = 0;
	push_tokens(ctx, in.v, in.n, NULL);
	Src *frame = ctx->src;
	for (;;) {
		Tok t = expand_next(ctx);
		if (is_sentinel(&t)) break;
		if (t.kind == T_EOF) { fprintf(stderr, "error: unterminated macro argument\n"); exit(1); }
		tv_push(&out, t);
	}
	if (ctx->src == frame) ctx->src = frame->parent;
	ctx->pp_back = saved_back;
	ctx->has_pp_back = saved_has_back;
	return out;
}

static int expand_macro(Ctx *ctx, Tok *t) {
	Macro *m = (Macro *)t->id->macro;
	TokVec args[129];
	int nargs = 0;
	if (m->is_func) {
		Tok p = raw_next(ctx);
		if (p.kind != T_LP) { ctx->pp_back = p; ctx->has_pp_back = 1; return 0; }
		TokVec cur;
		memset(&cur, 0, sizeof(cur));
		int depth = 0;
		for (;;) {
			Tok a = raw_next(ctx);
			if (a.kind == T_EOF) error_at(ctx, "unterminated argument list invoking macro '%s'", m->id->s);
			if (a.kind == T_LP) depth++;
			else if (a.kind == T_RP) { if (depth == 0) break; depth--; }
			else if (a.kind == T_COMMA && depth == 0 && !(m->variadic && nargs == m->nparams - 1)) {
				if (nargs >= 128) error_at(ctx, "too many macro arguments");
				args[nargs++] = cur;
				memset(&cur, 0, sizeof(cur));
				continue;
			}
			tv_push(&cur, a);
		}
		args[nargs++] = cur;
		if (m->nparams == 0 && nargs == 1 && args[0].n == 0) nargs = 0;
		if (m->variadic && nargs == m->nparams - 1) { memset(&args[nargs], 0, sizeof(TokVec)); nargs++; }
		if (nargs != m->nparams)
			error_at(ctx, "macro '%s' requires %d arguments, but %d given", m->id->s, m->nparams, nargs);
	}

	TokVec out, exp[129];
	int done[129];
	memset(&out, 0, sizeof(out));
	for (int i = 0; i < nargs; i++) done[i] = 0;
	for (int i = 0; i < m->nbody; i++) {
		Tok *bt = &m->body[i];
		if (m->is_func && bt->kind == T_HASH && i + 1 < m->nbody) {
			int pi = param_index(m, &m->body[i+1]);
			if (pi >= 0) { tv_push(&out, stringify(&args[pi], t)); i++; continue; }
		}
		if (bt->kind == T_HASHHASH && m->variadic && out.n > 0 && out.v[out.n-1].kind == T_COMMA &&
			i + 1 < m->nbody && param_index(m, &m->body[i+1]) == m->nparams - 1) {
			TokVec *va = &args[m->nparams - 1];
			if (va->n == 0) out.n--;
			else for (int j = 0; j < va->n; j++) tv_push(&out, va->v[j]);
			i++;
			continue;
		}
		int pi = param_index(m, bt);
		if (pi >= 0) {
			int adj = (i > 0 && m->body[i-1].kind == T_HASHHASH) ||
				(i + 1 < m->nbody && m->body[i+1].kind == T_HASHHASH);
			if (adj) {
				if (args[pi].n == 0) {
					Tok pm;
					memset(&pm, 0, sizeof(pm));
					pm.kind = T_EOF;
					pm.hide = (void *)&pp_placemarker_mark;
					tv_push(&out, pm);
				}
				for (int j = 0; j < args[pi].n; j++) tv_push(&out, args[pi].v[j]);
			} else {
				if (!done[pi]) { exp[pi] = expand_list(ctx, args[pi].v, args[pi].n); done[pi] = 1; }
				for (int j = 0; j < exp[pi].n; j++) {
					Tok e = exp[pi].v[j];
					if (j == 0) e.space = bt->space;
					tv_push(&out, e);
				}
			}
			continue;
		}
		Tok c = *bt;
		c.file = t->file;
		c.line = t->line;
		c.bol = 0;
		if (i == 0) c.space = t->space;
		tv_push(&out, c);
	}

	TokVec res;
	memset(&res, 0, sizeof(res));
	for (int j = 0; j < out.n; j++) {
		if (out.v[j].kind == T_HASHHASH && res.n > 0 && j + 1 < out.n) {
			Tok l = res.v[--res.n];
			res.v[res.n++] = paste(ctx, &l, &out.v[++j]);
			continue;
		}
		tv_push(&res, out.v[j]);
	}
	int k = 0;
	for (int j = 0; j < res.n; j++)
		if (!is_placemarker(&res.v[j])) res.v[k++] = res.v[j];
	res.n = k;
	push_tokens(ctx, res.v, res.n, m);
	return 1;
}

static Tok expand_next(Ctx *ctx) {
	for (;;) {
		Tok t = raw_next(ctx);
		if (!t.id) return t;
		if (t.str == id_line) {
			t.kind = T_NUM; t.val = t.line; t.id = NULL; t.str = NULL;
			t.is_unsigned = 0; t.is_long = 0;
			return t;
		}
		if (t.str == id_file) {
			t.kind = T_STR; t.str = (char *)t.file; t.len = (int)strlen(t.file); t.id = NULL;
			return t;
		}
		Macro *m = (Macro *)t.id->macro;
		if (!m) return t;
		if (m->busy || t.hide == (void *)m) { t.hide = (void *)m; return t; }
		if (!expand_macro(ctx, &t)) return t;
	}
}

static void pp_begin_unit(Ctx *ctx) {
	static const char *predef[] = {
"__STDC__ 1", "__STDC_VERSION__ 199901L", "__STDC_HOSTED__ 1",
"__x86_64__ 1", "__x86_64 1", "__amd64__ 1", "__linux__ 1", "__linux 1",
"__unix__ 1", "__unix 1", "__LP64__ 1", "_LP64 1", "__ELF__ 1",
"__CHAR_BIT__ 8", "__SIZEOF_INT__ 4", "__SIZEOF_LONG__ 8",
"__SIZEOF_POINTER__ 8", "__SIZEOF_SHORT__ 2", "__SIZEOF_LONG_LONG__ 8",
"__attribute__(x)", "__extension__", "__volatile__ volatile", "__asm__(x)",
		NULL
	};
	for (int i = 0; predef[i]; i++) pp_define_str(ctx, predef[i]);
	for (int i = 0; i < ctx->ndefines; i++) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", ctx->defines[i]);
		char *eq = strchr(buf, '=');
		if (eq) *eq = ' ';
		else if (strlen(buf) < sizeof(buf) - 3) strcat(buf, " 1");
		pp_define_str(ctx, buf);
	}
}

static void pp_end_unit(Ctx *ctx) {
	for (Macro *m = ctx->macros; m; m = m->next) m->id->macro = NULL;
	ctx->macros = NULL;
	ctx->src = NULL;
	ctx->ncond = 0;
	ctx->has_pp_back = 0;
}

static Tok pp_next(Ctx *ctx) { return expand_next(ctx); }

static void next(Ctx *ctx) {
	if (ctx->has_pk) { ctx->tok = ctx->pk; ctx->has_pk = 0; }
	else ctx->tok = pp_next(ctx);
}

static Tok *peek(Ctx *ctx) {
	if (!ctx->has_pk) { ctx->pk = pp_next(ctx); ctx->has_pk = 1; }
	return &ctx->pk;
}

static int consume(Ctx *ctx, TKind k) {
	if (ctx->tok.kind != k) return 0;
	next(ctx);
	return 1;
}

static void expect(Ctx *ctx, TKind k) {
	if (ctx->tok.kind != k)
		error_at(ctx, "expected '%s', got '%s'", kind_desc(k), tok_desc(&ctx->tok));
	next(ctx);
}

static Sym *lookup(Ctx *ctx, char *name) {
	for (Sym *s = ctx->scope; s; s = s->next)
		if (s->name == name) return s;
	return NULL;
}

static Sym *push_sym(Ctx *ctx, char *name, SymKind kind, Type *ty) {
	Sym *s = (Sym *)calloc(1, sizeof(Sym));
	s->name = name; s->kind = kind; s->ty = ty;
	s->depth = ctx->depth;
	s->next = ctx->scope;
	ctx->scope = s;
	return s;
}

static void enter_scope(Ctx *ctx) { ctx->depth++; }

static void leave_scope(Ctx *ctx) {
	while (ctx->scope && ctx->scope->depth >= ctx->depth) ctx->scope = ctx->scope->next;
	while (ctx->tags && ctx->tags->depth >= ctx->depth) ctx->tags = ctx->tags->next;
	ctx->depth--;
}

static Tag *find_tag(Ctx *ctx, char *name) {
	for (Tag *t = ctx->tags; t; t = t->next)
		if (t->name == name) return t;
	return NULL;
}

static void push_tag(Ctx *ctx, char *name, Type *ty) {
	Tag *t = (Tag *)calloc(1, sizeof(Tag));
	t->name = name; t->ty = ty; t->depth = ctx->depth;
	t->next = ctx->tags;
	ctx->tags = t;
}

static Sym *lookup_file_scope(Ctx *ctx, char *name) {
	for (Sym *s = ctx->scope; s; s = s->next)
		if (s->name == name && s->depth == 0) return s;
	return NULL;
}

static Sym *get_global(Ctx *ctx, char *name, SymKind kind, Type *ty, int is_static) {
	Sym *s = lookup_file_scope(ctx, name);
	if (s && s->global) s = s->global;
	if (!s && !is_static) {
		for (Sym *g = ctx->globals; g; g = g->gnext)
			if (g->name == name && !g->is_static && (g->kind == SYM_VAR || g->kind == SYM_FUNC)) { s = g; break; }
	}
	if (s) {
		if (s->kind != kind) error_at(ctx, "'%s' redeclared as a different kind of symbol", name);
		if (ty->kind == TY_ARRAY && s->ty->kind == TY_ARRAY && s->ty->len < 0) s->ty = ty;
		if (ty->kind == TY_FUNC && s->ty->oldstyle && !ty->oldstyle) s->ty = ty;
		if (is_static) s->is_static = 1;
		return s;
	}
	s = (Sym *)calloc(1, sizeof(Sym));
	s->name = name; s->kind = kind; s->ty = ty; s->is_static = is_static;
	s->gnext = ctx->globals;
	ctx->globals = s;
	return s;
}

static void bind_global(Ctx *ctx, Sym *g) {
	if (ctx->depth == 0) {
		Sym *s = lookup_file_scope(ctx, g->name);
		if (s == g) return;
		g->depth = 0;
		g->next = ctx->scope;
		ctx->scope = g;
		return;
	}
	Sym *a = push_sym(ctx, g->name, g->kind, g->ty);
	a->global = g;
}

static void gen_push(Ctx *ctx) { emit_push(&ctx->text, RAX); ctx->sp++; }
static void gen_pop(Ctx *ctx, int reg) { emit_pop(&ctx->text, reg); ctx->sp--; }

static void add_reloc(Ctx *ctx, RelKind kind, size_t pos, Sym *sym, SecKind sect, long long addend) {
	if (ctx->nrelocs == ctx->caprelocs) {
		ctx->caprelocs = ctx->caprelocs ? ctx->caprelocs * 2 : 256;
		ctx->relocs = (Reloc *)realloc(ctx->relocs, sizeof(Reloc) * ctx->caprelocs);
		if (!ctx->relocs) { perror("realloc"); exit(1); }
	}
	Reloc *r = &ctx->relocs[ctx->nrelocs++];
	r->kind = kind; r->pos = pos; r->sym = sym; r->sect = sect; r->addend = addend;
}

static void emit_lea_sym(Ctx *ctx, Sym *sym) {
	Code *c = &ctx->text;
	C8(c,0x48); C8(c,0x8D); C8(c,0x05);
	add_reloc(ctx, R_PC32, Cpos(c), sym, SEC_TEXT, 0);
	C32(c,0);
}

static void emit_lea_rodata(Ctx *ctx, int reg, long off) {
	Code *c = &ctx->text;
	C8(c,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0))); C8(c,0x8D); C8(c,(uint8_t)(0x05 | ((reg & 7) << 3)));
	add_reloc(ctx, R_PC32, Cpos(c), NULL, SEC_RODATA, off);
	C32(c,0);
}

static void emit_call_sym(Ctx *ctx, Sym *sym) {
	Code *c = &ctx->text;
	C8(c,0xE8);
	add_reloc(ctx, R_PC32, Cpos(c), sym, SEC_TEXT, 0);
	C32(c,0);
}

static long add_rodata(Ctx *ctx, const void *p, size_t n) {
	while (ctx->rodata.len % 8) buf_append_byte(&ctx->rodata, 0);
	long off = (long)ctx->rodata.len;
	buf_append(&ctx->rodata, p, n);
	return off;
}

static void emit_memcpy(Code *c, int n) { emit_mov_ecx_imm(c, (uint32_t)n); emit_rep_movsb(c); }

static int alloc_local(Ctx *ctx, int size, int align) {
	Func *f = ctx->fn;
	if (!f) return 0;
	if (align < 1) align = 1;
	f->frame += size;
	f->frame = (int)align_up((size_t)f->frame, (size_t)align);
	return -f->frame;
}

static void emit_shift_imm(Code *c, int ext, int n) {
	if (n == 0) return;
	C8(c,0x48); C8(c,0xC1); C8(c,(uint8_t)(0xC0 | (ext << 3))); C8(c,(uint8_t)n);
}

static void emit_bf_load(Code *c, Type *ty) {
	emit_load(c, ty->size, 1);
	emit_shift_imm(c, 4, 64 - ty->bit_off - ty->bit_width);
	emit_shift_imm(c, ty->is_unsigned ? 5 : 7, 64 - ty->bit_width);
}

static void emit_bf_store(Code *c, Type *ty) {
	uint64_t mask = ty->bit_width >= 64 ? ~0ULL : (1ULL << ty->bit_width) - 1;
	emit_mov_reg_reg(c, RDX, RAX);
	C8(c,0x48); C8(c,0xBE); C64(c, mask);
	C8(c,0x48); C8(c,0x21); C8(c,0xF2);
	if (ty->bit_off) {
		C8(c,0x48); C8(c,0xC1); C8(c,0xE2); C8(c,(uint8_t)ty->bit_off);
		C8(c,0x48); C8(c,0xC1); C8(c,0xE6); C8(c,(uint8_t)ty->bit_off);
	}
	C8(c,0x48); C8(c,0xF7); C8(c,0xD6);
	emit_mov_reg_reg(c, RAX, RCX);
	emit_load(c, ty->size, 1);
	C8(c,0x48); C8(c,0x21); C8(c,0xF0);
	C8(c,0x48); C8(c,0x09); C8(c,0xD0);
	emit_store(c, ty->size);
	emit_mov_reg_reg(c, RAX, RDX);
	emit_shift_imm(c, 4, 64 - ty->bit_off - ty->bit_width);
	emit_shift_imm(c, ty->is_unsigned ? 5 : 7, 64 - ty->bit_width);
}

static void emit_load_type(Code *c, Type *ty) {
	if (ty->bit_width) emit_bf_load(c, ty);
	else emit_load(c, ty->size, ty->is_unsigned);
}

static void emit_store_type(Code *c, Type *ty) {
	if (ty->bit_width) emit_bf_store(c, ty);
	else emit_store(c, ty->size);
}

static Type *rval(Ctx *ctx, Type *ty) {
	if (ty->kind == TY_ARRAY) { ctx->lv = 0; return ptr_to(ty->base); }
	if (ty->kind == TY_FUNC) { ctx->lv = 0; return ptr_to(ty); }
	if (ctx->lv) {
		if (ty->kind != TY_STRUCT && ty->kind != TY_VOID) emit_load_type(&ctx->text, ty);
		ctx->lv = 0;
	}
	if (ty->origin) ty = ty->origin;
	return ty;
}

static void gen_fnonzero(Ctx *ctx, Type *ty) {
	Code *c = &ctx->text;
	emit_movq_xmm0_rax(c);
	emit_xorpd_xmm1(c);
	emit_ucomi(c, ty->kind == TY_DOUBLE, 0);
	emit_setcc_al(c, 0x95);
	emit_setcc_cl(c, 0x9A);
	C8(c,0x08); C8(c,0xC8);
	emit_movzx_eax_al(c);
}

static void gen_truth(Ctx *ctx, Type *ty) {
	if (is_flonum(ty)) gen_fnonzero(ctx, ty);
}

static void gen_fcast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (from->kind == to->kind) return;
	int td = to->kind == TY_DOUBLE;
	if (is_flonum(from) && is_flonum(to)) {
		emit_movq_xmm0_rax(c);
		C8(c,(uint8_t)(td ? 0xF3 : 0xF2)); C8(c,0x0F); C8(c,0x5A); C8(c,0xC0);
		emit_xmm0_to_rax(c, td);
		return;
	}
	if (is_flonum(to)) {
		uint8_t pfx = (uint8_t)(td ? 0xF2 : 0xF3);
		if (from->size == 8 && from->is_unsigned) {
			emit_test_rax_rax(c);
			C8(c,0x0F); C8(c,0x88); size_t js = Cpos(c); C32(c,0);
			C8(c,pfx); C8(c,0x48); C8(c,0x0F); C8(c,0x2A); C8(c,0xC0);
			size_t jdone = emit_jmp_placeholder(c);
			patch_jmp_to(&c->b, js, Cpos(c));
			emit_mov_reg_reg(c, RCX, RAX);
			C8(c,0x48); C8(c,0xD1); C8(c,0xE9);
			C8(c,0x83); C8(c,0xE0); C8(c,0x01);
			C8(c,0x48); C8(c,0x09); C8(c,0xC1);
			C8(c,pfx); C8(c,0x48); C8(c,0x0F); C8(c,0x2A); C8(c,0xC1);
			C8(c,pfx); C8(c,0x0F); C8(c,0x58); C8(c,0xC0);
			patch_jmp_to(&c->b, jdone, Cpos(c));
		} else {
			C8(c,pfx); C8(c,0x48); C8(c,0x0F); C8(c,0x2A); C8(c,0xC0);
		}
		emit_xmm0_to_rax(c, td);
		return;
	}
	if (to->kind == TY_BOOL) { gen_fnonzero(ctx, from); return; }
	emit_movq_xmm0_rax(c);
	C8(c,(uint8_t)(from->kind == TY_DOUBLE ? 0xF2 : 0xF3)); C8(c,0x48); C8(c,0x0F); C8(c,0x2C); C8(c,0xC0);
	if (is_integer(to) && to->size < 8) emit_extend(c, to->size, to->is_unsigned);
}

static void gen_cast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (to->kind == TY_VOID) return;
	if (is_flonum(from) || is_flonum(to)) {
		if (is_scalar(from) && is_scalar(to)) gen_fcast(ctx, from, to);
		return;
	}
	if (to->kind == TY_BOOL) {
		if (from->kind != TY_BOOL) emit_to_bool(c);
		return;
	}
	if (!is_integer(to) || to->size >= 8) return;
	if (is_integer(from)) {
		if (from->size < to->size && (from->is_unsigned || !to->is_unsigned)) return;
		if (from->size == to->size && from->is_unsigned == to->is_unsigned) return;
	}
	emit_extend(c, to->size, to->is_unsigned);
}

static void gen_struct_copy_to_rdi(Ctx *ctx, int size) {
	Code *c = &ctx->text;
	emit_mov_reg_reg(c, RSI, RAX);
	emit_mov_reg_reg(c, RAX, RDI);
	emit_memcpy(c, size);
}

static Type *parse_expr(Ctx *ctx);
static Type *parse_assign(Ctx *ctx);
static Type *parse_cast(Ctx *ctx);
static Type *parse_unary(Ctx *ctx);
static long long const_expr(Ctx *ctx);
static Type *declarator(Ctx *ctx, Type *ty, char **name);
static void parse_stmt(Ctx *ctx);

enum { ST_NONE, ST_TYPEDEF, ST_STATIC, ST_EXTERN };

static int is_typename(Ctx *ctx, Tok *t) {
	switch (t->kind) {
	case T_INT: case T_CHAR: case T_SHORT: case T_LONG: case T_SIGNED: case T_UNSIGNED:
	case T_VOID: case T_BOOL: case T_FLOAT: case T_DOUBLE:
	case T_STRUCT: case T_UNION: case T_ENUM: case T_TYPEDEF:
	case T_STATIC: case T_EXTERN: case T_CONST: case T_VOLATILE: case T_INLINE:
	case T_REGISTER: case T_AUTO: case T_RESTRICT:
		return 1;
	case T_IDENT: {
		Sym *s = lookup(ctx, t->str);
		return s && s->kind == SYM_TYPEDEF;
	}
	default:
		return 0;
	}
}

static int is_qualifier(int k) { return k == T_CONST || k == T_VOLATILE || k == T_RESTRICT; }

static Type *struct_decl(Ctx *ctx);
static Type *enum_decl(Ctx *ctx);

static Type *declspec(Ctx *ctx, int *storage) {
	int nlong = 0, nshort = 0, nint = 0, nchar = 0, nvoid = 0, nbool = 0, nfloat = 0, ndouble = 0;
	int is_signed = 0, is_unsigned = 0;
	Type *user = NULL;
	if (storage) *storage = ST_NONE;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_TYPEDEF || k == T_STATIC || k == T_EXTERN) {
			if (!storage) error_at(ctx, "storage class specifier not allowed here");
			*storage = k == T_TYPEDEF ? ST_TYPEDEF : k == T_STATIC ? ST_STATIC : ST_EXTERN;
			next(ctx); continue;
		}
		if (is_qualifier(k) || k == T_INLINE || k == T_REGISTER || k == T_AUTO) { next(ctx); continue; }
		if (k == T_VOID) { nvoid++; next(ctx); continue; }
		if (k == T_BOOL) { nbool++; next(ctx); continue; }
		if (k == T_FLOAT) { nfloat++; next(ctx); continue; }
		if (k == T_DOUBLE) { ndouble++; next(ctx); continue; }
		if (k == T_CHAR) { nchar++; next(ctx); continue; }
		if (k == T_SHORT) { nshort++; next(ctx); continue; }
		if (k == T_INT) { nint++; next(ctx); continue; }
		if (k == T_LONG) { nlong++; next(ctx); continue; }
		if (k == T_SIGNED) { is_signed = 1; next(ctx); continue; }
		if (k == T_UNSIGNED) { is_unsigned = 1; next(ctx); continue; }
		if (k == T_STRUCT || k == T_UNION) { user = struct_decl(ctx); continue; }
		if (k == T_ENUM) { user = enum_decl(ctx); continue; }
		if (k == T_IDENT && !user && !nlong && !nshort && !nint && !nchar && !nvoid && !nbool &&
			!nfloat && !ndouble &&
			!is_signed && !is_unsigned) {
			Sym *s = lookup(ctx, ctx->tok.str);
			if (s && s->kind == SYM_TYPEDEF) { user = s->ty; next(ctx); continue; }
		}
		break;
	}
	if (user) return user;
	if (nvoid) return ty_void;
	if (nbool) return ty_bool;
	if (nfloat) return ty_float;
	if (ndouble) return ty_double;
	if (nchar) return is_unsigned ? ty_uchar : ty_char;
	if (nshort) return is_unsigned ? ty_ushort : ty_short;
	if (nlong) return is_unsigned ? ty_ulong : ty_long;
	(void)is_signed;
	return is_unsigned ? ty_uint : ty_int;
}

static Type *func_params(Ctx *ctx, Type *ret) {
	Type *ft = func_type(ret);
	if (ctx->tok.kind == T_RP) { next(ctx); ft->oldstyle = 1; return ft; }
	if (ctx->tok.kind == T_VOID && peek(ctx)->kind == T_RP) { next(ctx); next(ctx); return ft; }
	Param head; head.next = NULL;
	Param *cur = &head;
	for (;;) {
		if (ctx->tok.kind == T_ELLIPSIS) { next(ctx); ft->variadic = 1; break; }
		Type *b = declspec(ctx, NULL);
		char *name = NULL;
		Type *t = declarator(ctx, b, &name);
		if (t->kind == TY_ARRAY) t = ptr_to(t->base);
		else if (t->kind == TY_FUNC) t = ptr_to(t);
		Param *p = (Param *)calloc(1, sizeof(Param));
		p->name = name; p->ty = t;
		cur->next = p; cur = p;
		ft->nparams++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RP);
	ft->params = head.next;
	return ft;
}

static Type *type_suffix(Ctx *ctx, Type *ty) {
	if (ctx->tok.kind == T_LP) {
		next(ctx);
		return func_params(ctx, ty);
	}
	if (ctx->tok.kind == T_LBRACKET) {
		next(ctx);
		while (is_qualifier(ctx->tok.kind) || ctx->tok.kind == T_STATIC) next(ctx);
		int len = -1;
		if (ctx->tok.kind != T_RBRACKET) len = (int)const_expr(ctx);
		expect(ctx, T_RBRACKET);
		ty = type_suffix(ctx, ty);
		return array_of(ty, len);
	}
	return ty;
}

static int is_nested_declarator(Ctx *ctx) {
	Tok *p = peek(ctx);
	if (p->kind == T_MUL || p->kind == T_LP || p->kind == T_LBRACKET) return 1;
	if (p->kind == T_IDENT && !is_typename(ctx, p)) return 1;
	return 0;
}

static Type *declarator(Ctx *ctx, Type *ty, char **name) {
	while (ctx->tok.kind == T_MUL) {
		next(ctx);
		while (is_qualifier(ctx->tok.kind)) next(ctx);
		ty = ptr_to(ty);
	}
	if (ctx->tok.kind == T_LP && is_nested_declarator(ctx)) {
		next(ctx);
		Type *ph = new_type(TY_VOID, 1, 1);
		Type *inner = declarator(ctx, ph, name);
		expect(ctx, T_RP);
		ty = type_suffix(ctx, ty);
		*ph = *ty;
		return inner;
	}
	if (ctx->tok.kind == T_IDENT) {
		if (name) *name = ctx->tok.str;
		next(ctx);
	}
	return type_suffix(ctx, ty);
}

static Type *parse_typename(Ctx *ctx) {
	Type *b = declspec(ctx, NULL);
	return declarator(ctx, b, NULL);
}

static Type *struct_decl(Ctx *ctx) {
	int is_union = ctx->tok.kind == T_UNION;
	next(ctx);
	char *tag = NULL;
	if (ctx->tok.kind == T_IDENT) { tag = ctx->tok.str; next(ctx); }
	if (tag && ctx->tok.kind != T_LB) {
		Tag *t = find_tag(ctx, tag);
		if (t) return t->ty;
		Type *ty = new_type(TY_STRUCT, 0, 1);
		ty->is_union = is_union;
		push_tag(ctx, tag, ty);
		return ty;
	}
	Type *ty = NULL;
	if (tag) {
		Tag *t = find_tag(ctx, tag);
		if (t && t->depth == ctx->depth && !t->ty->complete) ty = t->ty;
	}
	if (!ty) {
		ty = new_type(TY_STRUCT, 0, 1);
		ty->is_union = is_union;
		if (tag) push_tag(ctx, tag, ty);
	}
	expect(ctx, T_LB);
	Member head; head.next = NULL;
	Member *cur = &head;
	long bits = 0;
	int size = 0, align = 1;
	while (ctx->tok.kind != T_RB) {
		Type *base = declspec(ctx, NULL);
		if (ctx->tok.kind == T_SEMI && base->kind == TY_STRUCT) {
			int a = base->align;
			int moff = is_union ? 0 : (int)align_up((size_t)((bits + 7) / 8), (size_t)a);
			for (Member *bm = base->members; bm; bm = bm->next) {
				Member *m = (Member *)calloc(1, sizeof(Member));
				m->name = bm->name; m->ty = bm->ty; m->off = moff + bm->off;
				cur->next = m; cur = m;
			}
			if (a > align) align = a;
			if (is_union) { if (base->size > size) size = base->size; }
			else bits = (long)(moff + base->size) * 8;
			next(ctx);
			continue;
		}
		for (;;) {
			char *name = NULL;
			Type *mt = base;
			if (ctx->tok.kind != T_COLON) mt = declarator(ctx, base, &name);
			if (consume(ctx, T_COLON)) {
				int w = (int)const_expr(ctx);
				if (!is_integer(mt)) error_at(ctx, "bit-field has invalid type");
				int unit = mt->size * 8;
				if (w < 0 || w > unit) error_at(ctx, "invalid bit-field width");
				long pos = is_union ? 0 : bits;
				if (w == 0) {
					if (!is_union) bits = (long)align_up((size_t)bits, (size_t)unit);
				} else {
					if (pos / unit != (pos + w - 1) / unit) pos = (long)align_up((size_t)pos, (size_t)unit);
					if (name) {
						Member *m = (Member *)calloc(1, sizeof(Member));
						m->name = name;
						m->off = (int)(pos / unit) * mt->size;
						m->ty = bitfield_of(mt, w, (int)(pos % unit));
						cur->next = m; cur = m;
						if (mt->align > align) align = mt->align;
					}
					if (is_union) { int sz = (w + 7) / 8; if (sz > size) size = sz; }
					else bits = pos + w;
				}
				if (!consume(ctx, T_COMMA)) break;
				continue;
			}
			Member *m = (Member *)calloc(1, sizeof(Member));
			m->name = name; m->ty = mt;
			if (mt->align > align) align = mt->align;
			if (is_union) {
				m->off = 0;
				if (mt->size > size) size = mt->size;
			} else {
				int off = (int)align_up((size_t)((bits + 7) / 8), (size_t)mt->align);
				m->off = off;
				bits = (long)(off + mt->size) * 8;
			}
			cur->next = m; cur = m;
			if (!consume(ctx, T_COMMA)) break;
		}
		expect(ctx, T_SEMI);
	}
	next(ctx);
	if (!is_union) size = (int)((bits + 7) / 8);
	ty->members = head.next;
	ty->align = align;
	ty->size = (int)align_up((size_t)size, (size_t)align);
	ty->complete = 1;
	return ty;
}

static Type *enum_decl(Ctx *ctx) {
	next(ctx);
	if (ctx->tok.kind == T_IDENT) next(ctx);
	if (ctx->tok.kind != T_LB) return ty_int;
	next(ctx);
	long long v = 0;
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected enumerator name");
		char *name = ctx->tok.str;
		next(ctx);
		if (consume(ctx, T_ASSIGN)) v = const_expr(ctx);
		Sym *s = push_sym(ctx, name, SYM_ENUMC, ty_int);
		s->val = v++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RB);
	return ty_int;
}

static Member *find_member(Type *ty, char *name) {
	for (Member *m = ty->members; m; m = m->next)
		if (m->name == name) return m;
	return NULL;
}

static int binop_prec(int k) {
	switch (k) {
	case T_OR: return 1;
	case T_AND: return 2;
	case T_PIPE: return 3;
	case T_CARET: return 4;
	case T_AMP: return 5;
	case T_EQ: case T_NEQ: return 6;
	case T_LT: case T_GT: case T_LEQ: case T_GEQ: return 7;
	case T_SHL: case T_SHR: return 8;
	case T_PLUS: case T_MINUS: return 9;
	case T_MUL: case T_DIV: case T_MOD: return 10;
	default: return 0;
	}
}

static long long sizeof_operand(Ctx *ctx);
static long long ce_cond(Ctx *ctx);

static long long ce_unary(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_MINUS) { next(ctx); return -ce_unary(ctx); }
	if (k == T_PLUS) { next(ctx); return ce_unary(ctx); }
	if (k == T_TILDE) { next(ctx); return ~ce_unary(ctx); }
	if (k == T_NOT) { next(ctx); return !ce_unary(ctx); }
	if (k == T_SIZEOF) { next(ctx); return sizeof_operand(ctx); }
	if (k == T_LP) {
		if (!ctx->in_pp_if && is_typename(ctx, peek(ctx))) {
			next(ctx);
			Type *t = parse_typename(ctx);
			expect(ctx, T_RP);
			return const_cast_val(ce_unary(ctx), t);
		}
		next(ctx);
		long long v = const_expr(ctx);
		expect(ctx, T_RP);
		return v;
	}
	if (k == T_NUM) { long long v = ctx->tok.val; next(ctx); return v; }
	if (k == T_FNUM) { long long v = (long long)ctx->tok.fval; next(ctx); return v; }
	if (k == T_IDENT) {
		Sym *s = lookup(ctx, ctx->tok.str);
		if (s && s->kind == SYM_ENUMC) { next(ctx); return s->val; }
		if (ctx->in_pp_if) { next(ctx); return 0; }
		error_at(ctx, "'%s' is not a constant", ctx->tok.str);
	}
	error_at(ctx, "expected constant expression, got '%s'", tok_desc(&ctx->tok));
	return 0;
}

static long long ce_binary(Ctx *ctx, int minprec) {
	long long l = ce_unary(ctx);
	for (;;) {
		int k = ctx->tok.kind;
		int p = binop_prec(k);
		if (p == 0 || p < minprec) return l;
		next(ctx);
		long long r = ce_binary(ctx, p + 1);
		switch (k) {
		case T_OR: l = l || r; break;
		case T_AND: l = l && r; break;
		case T_PIPE: l = l | r; break;
		case T_CARET: l = l ^ r; break;
		case T_AMP: l = l & r; break;
		case T_EQ: l = l == r; break;
		case T_NEQ: l = l != r; break;
		case T_LT: l = l < r; break;
		case T_GT: l = l > r; break;
		case T_LEQ: l = l <= r; break;
		case T_GEQ: l = l >= r; break;
		case T_SHL: l = (long long)((unsigned long long)l << r); break;
		case T_SHR: l = l >> r; break;
		case T_PLUS: l = l + r; break;
		case T_MINUS: l = l - r; break;
		case T_MUL: l = l * r; break;
		case T_DIV: l = r ? l / r : 0; break;
		case T_MOD: l = r ? l % r : 0; break;
		}
	}
}

static long long ce_cond(Ctx *ctx) {
	long long c = ce_binary(ctx, 1);
	if (!consume(ctx, T_QUESTION)) return c;
	long long a = const_expr(ctx);
	expect(ctx, T_COLON);
	long long b = ce_cond(ctx);
	return c ? a : b;
}

static long long const_expr(Ctx *ctx) { return ce_cond(ctx); }

static double fce_add(Ctx *ctx);

static double fce_unary(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_MINUS) { next(ctx); return -fce_unary(ctx); }
	if (k == T_PLUS) { next(ctx); return fce_unary(ctx); }
	if (k == T_LP) {
		if (is_typename(ctx, peek(ctx))) {
			next(ctx);
			Type *t = parse_typename(ctx);
			expect(ctx, T_RP);
			double v = fce_unary(ctx);
			if (t->kind == TY_FLOAT) return (float)v;
			if (is_integer(t)) return (double)const_cast_val((long long)v, t);
			return v;
		}
		next(ctx);
		double v = fce_add(ctx);
		expect(ctx, T_RP);
		return v;
	}
	if (k == T_FNUM) { double v = ctx->tok.fval; next(ctx); return v; }
	if (k == T_NUM) {
		double v = ctx->tok.is_unsigned ? (double)(unsigned long long)ctx->tok.val : (double)ctx->tok.val;
		next(ctx);
		return v;
	}
	return (double)ce_unary(ctx);
}

static double fce_mul(Ctx *ctx) {
	double v = fce_unary(ctx);
	for (;;) {
		if (consume(ctx, T_MUL)) v = v * fce_unary(ctx);
		else if (consume(ctx, T_DIV)) v = v / fce_unary(ctx);
		else return v;
	}
}

static double fce_add(Ctx *ctx) {
	double v = fce_mul(ctx);
	for (;;) {
		if (consume(ctx, T_PLUS)) v = v + fce_mul(ctx);
		else if (consume(ctx, T_MINUS)) v = v - fce_mul(ctx);
		else return v;
	}
}

static uint64_t float_bits(double v, Type *t) {
	if (t->kind == TY_FLOAT) {
		float f = (float)v;
		uint32_t b;
		memcpy(&b, &f, 4);
		return b;
	}
	uint64_t b;
	memcpy(&b, &v, 8);
	return b;
}

static long long type_size_checked(Ctx *ctx, Type *t) {
	if (t->kind == TY_STRUCT && !t->complete) error_at(ctx, "invalid application of 'sizeof' to an incomplete type");
	if (t->bit_width) error_at(ctx, "'sizeof' applied to a bit-field");
	return t->size;
}

static long long sizeof_operand(Ctx *ctx) {
	if (ctx->tok.kind == T_LP && is_typename(ctx, peek(ctx))) {
		next(ctx);
		Type *t = parse_typename(ctx);
		expect(ctx, T_RP);
		return type_size_checked(ctx, t);
	}
	size_t pos = Cpos(&ctx->text);
	int nrel = ctx->nrelocs, sp = ctx->sp;
	Type *t = parse_unary(ctx);
	ctx->text.b.len = pos;
	ctx->nrelocs = nrel;
	ctx->sp = sp;
	ctx->lv = 0;
	ctx->fn_lea_sym = NULL;
	return type_size_checked(ctx, t);
}

static const int argregs[6] = { RDI, RSI, RDX, RCX, R8, R9 };

static Type *parse_builtin_syscall(Ctx *ctx) {
	Code *c = &ctx->text;
	static const int sysregs[7] = { RAX, RDI, RSI, RDX, R10, R8, R9 };
	next(ctx);
	expect(ctx, T_LP);
	int n = 0;
	while (ctx->tok.kind != T_RP) {
		rval(ctx, parse_assign(ctx));
		gen_push(ctx);
		n++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RP);
	if (n < 1 || n > 7) error_at(ctx, "__syscall takes 1 to 7 arguments");
	for (int i = n - 1; i >= 0; i--) gen_pop(ctx, sysregs[i]);
	emit_syscall(c);
	ctx->lv = 0;
	return ty_long;
}

static Type *parse_builtin_va_start(Ctx *ctx) {
	Code *c = &ctx->text;
	next(ctx);
	expect(ctx, T_LP);
	if (!ctx->fn || !ctx->fn->ty->variadic) error_at(ctx, "va_start used outside a variadic function");
	rval(ctx, parse_assign(ctx));
	while (consume(ctx, T_COMMA)) { size_t p = Cpos(c); gen_push(ctx); parse_assign(ctx); c->b.len = p; ctx->sp--; ctx->lv = 0; }
	expect(ctx, T_RP);
	C8(c,0xC7); C8(c,0x00); C32(c,(uint32_t)(ctx->fn->named_gp * 8));
	C8(c,0xC7); C8(c,0x40); C8(c,0x04); C32(c,48);
	emit_lea_rbp(c, RCX, 16 + 8 * ctx->fn->named_stack);
	C8(c,0x48); C8(c,0x89); C8(c,0x48); C8(c,0x08);
	emit_lea_rbp(c, RCX, -48);
	C8(c,0x48); C8(c,0x89); C8(c,0x48); C8(c,0x10);
	ctx->lv = 0;
	return ty_void;
}

static Type *parse_call(Ctx *ctx, Type *fty) {
	Code *c = &ctx->text;
	Sym *direct = NULL;
	if (fty->kind == TY_FUNC && ctx->fn_lea_sym && ctx->fn_lea_end == Cpos(c)) {
		direct = ctx->fn_lea_sym;
		c->b.len -= 7;
		ctx->nrelocs--;
	} else {
		fty = rval(ctx, fty);
		if (!is_ptr(fty) || fty->base->kind != TY_FUNC) error_at(ctx, "called object is not a function");
		fty = fty->base;
		gen_push(ctx);
	}
	ctx->fn_lea_sym = NULL;
	Type *ret = fty->base;
	int sret = ret->kind == TY_STRUCT;
	int nargs = 0;
	Param *p = fty->params;
	while (ctx->tok.kind != T_RP) {
		Type *at = rval(ctx, parse_assign(ctx));
		if (p) {
			if (is_scalar(p->ty) && is_scalar(at)) gen_cast(ctx, at, p->ty);
			p = p->next;
		} else if (at->kind == TY_FLOAT) gen_cast(ctx, at, ty_double);
		gen_push(ctx);
		nargs++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RP);
	if (!fty->oldstyle && !fty->variadic && nargs != fty->nparams)
		error_at(ctx, "wrong number of arguments (expected %d, got %d)", fty->nparams, nargs);
	if (fty->variadic && nargs < fty->nparams)
		error_at(ctx, "too few arguments (expected at least %d, got %d)", fty->nparams, nargs);

	int nreg = sret ? 5 : 6;
	int nstack = nargs > nreg ? nargs - nreg : 0;
	int pad = (ctx->sp + nstack) % 2 ? 1 : 0;
	int extra = nstack + pad;
	int temp = 0;
	if (sret) temp = alloc_local(ctx, ret->size, ret->align);
	if (extra) emit_sub_rsp(c, extra * 8);
	for (int j = 0; j < nstack; j++) {
		int a = nreg + j;
		emit_load_rsp(c, RAX, 8 * extra + 8 * (nargs - 1 - a));
		emit_store_rsp_rax(c, 8 * j);
	}
	int r = 0;
	if (sret) emit_lea_rbp(c, argregs[r++], temp);
	for (int a = 0; a < nargs && a < nreg; a++)
		emit_load_rsp(c, argregs[r++], 8 * extra + 8 * (nargs - 1 - a));
	if (!direct) emit_load_rsp(c, R11, 8 * extra + 8 * nargs);
	emit_xor_eax_eax(c);
	if (direct) emit_call_sym(ctx, direct);
	else emit_call_r11(c);
	int drop = extra + nargs + (direct ? 0 : 1);
	if (drop) emit_add_rsp(c, drop * 8);
	ctx->sp -= nargs + (direct ? 0 : 1);
	ctx->lv = 0;
	return ret;
}

static Type *parse_primary(Ctx *ctx) {
	Code *c = &ctx->text;
	ctx->lv = 0;
	if (ctx->tok.kind == T_NUM) {
		Type *ty = ctx->tok.is_long ? (ctx->tok.is_unsigned ? ty_ulong : ty_long)
			: (ctx->tok.is_unsigned ? ty_uint : ty_int);
		emit_mov_rax_imm(c, ctx->tok.val);
		next(ctx);
		return ty;
	}
	if (ctx->tok.kind == T_FNUM) {
		Type *ty = ctx->tok.is_float ? ty_float : ty_double;
		uint64_t bits = float_bits(ctx->tok.fval, ty);
		if (bits <= 0xFFFFFFFFULL) emit_mov_rax_imm(c, (long long)bits);
		else emit_mov_rax_imm64(c, bits);
		next(ctx);
		return ty;
	}
	if (ctx->tok.kind == T_STR) {
		Buf s; buf_init(&s);
		while (ctx->tok.kind == T_STR) { buf_append(&s, ctx->tok.str, ctx->tok.len); next(ctx); }
		buf_append_byte(&s, 0);
		long off = add_rodata(ctx, s.data, s.len);
		emit_lea_rodata(ctx, RAX, off);
		int n = (int)s.len;
		free(s.data);
		ctx->lv = 1;
		return array_of(ty_char, n);
	}
	if (ctx->tok.kind == T_LP) {
		next(ctx);
		Type *ty = parse_expr(ctx);
		expect(ctx, T_RP);
		return ty;
	}
	if (ctx->tok.kind == T_IDENT) {
		char *name = ctx->tok.str;
		if (name == id_syscall) return parse_builtin_syscall(ctx);
		if (name == id_va_start) return parse_builtin_va_start(ctx);
		if ((name == id_func || name == id_function) && ctx->fn) {
			const char *fname = ctx->fn->sym->name;
			long off = add_rodata(ctx, fname, strlen(fname) + 1);
			emit_lea_rodata(ctx, RAX, off);
			next(ctx);
			ctx->lv = 1;
			return array_of(ty_char, (int)strlen(fname) + 1);
		}
		Sym *s = lookup(ctx, name);
		next(ctx);
		if (!s) {
			if (ctx->tok.kind != T_LP) error_at(ctx, "undeclared variable '%s'", name);
			warn_at(ctx, "implicit declaration of function '%s'", name);
			Type *ft = func_type(ty_int);
			ft->oldstyle = 1;
			Sym *g = get_global(ctx, name, SYM_FUNC, ft, 0);
			int d = ctx->depth;
			ctx->depth = 0;
			bind_global(ctx, g);
			ctx->depth = d;
			s = g;
		}
		if (s->kind == SYM_ENUMC) { emit_mov_rax_imm(c, s->val); return ty_int; }
		if (s->kind == SYM_TYPEDEF) error_at(ctx, "unexpected type name '%s'", name);
		if (s->kind == SYM_FUNC) {
			Sym *g = s->global ? s->global : s;
			emit_lea_sym(ctx, g);
			ctx->fn_lea_end = Cpos(c);
			ctx->fn_lea_sym = g;
			return g->ty;
		}
		if (s->is_local) emit_lea_rbp(c, RAX, s->stack_off);
		else emit_lea_sym(ctx, s->global ? s->global : s);
		ctx->lv = 1;
		return s->ty;
	}
	error_at(ctx, "unexpected token in expression: '%s'", tok_desc(&ctx->tok));
	return NULL;
}

static Type *gen_binop(Ctx *ctx, int op, Type *lt, Type *rt);

static Type *gen_incdec(Ctx *ctx, Type *ty, int delta, int post) {
	Code *c = &ctx->text;
	if (!ctx->lv) error_at(ctx, "lvalue required as increment/decrement operand");
	if (!is_scalar(ty)) error_at(ctx, "invalid operand to increment/decrement");
	ctx->lv = 0;
	int step = is_ptr(ty) ? ty->base->size : 1;
	gen_push(ctx);
	emit_load_type(c, ty);
	if (post) gen_push(ctx);
	if (ty->kind == TY_BOOL) emit_mov_rax_imm(c, delta > 0 ? 1 : 0);
	else if (is_flonum(ty)) {
		emit_mov_reg_reg(c, RCX, RAX);
		emit_mov_rax_imm64(c, float_bits(1.0, ty));
		gen_binop(ctx, delta > 0 ? T_PLUS : T_MINUS, ty, ty);
	} else {
		emit_add_rax_imm(c, (long long)delta * step);
		if (ty->size < 8) emit_extend(c, ty->size, ty->is_unsigned);
	}
	if (post) gen_pop(ctx, R8);
	gen_pop(ctx, RCX);
	emit_store_type(c, ty);
	if (post) emit_mov_reg_reg(c, RAX, R8);
	return ty->origin ? ty->origin : ty;
}

static Type *member_access(Ctx *ctx, Type *ty) {
	if (ty->kind != TY_STRUCT) error_at(ctx, "member access on non-struct");
	if (!ty->complete) error_at(ctx, "member access on incomplete type");
	if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected member name");
	Member *m = find_member(ty, ctx->tok.str);
	if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
	next(ctx);
	emit_add_rax_imm(&ctx->text, m->off);
	ctx->lv = 1;
	return m->ty;
}

static Type *parse_postfix(Ctx *ctx) {
	Code *c = &ctx->text;
	Type *ty = parse_primary(ctx);
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_LBRACKET) {
			next(ctx);
			ty = rval(ctx, ty);
			gen_push(ctx);
			Type *it = rval(ctx, parse_expr(ctx));
			expect(ctx, T_RBRACKET);
			gen_pop(ctx, RCX);
			if (is_ptr(ty) && is_integer(it)) {
				emit_scale(c, RAX, ty->base->size);
				ty = ty->base;
			} else if (is_integer(ty) && is_ptr(it)) {
				emit_scale(c, RCX, it->base->size);
				ty = it->base;
			} else error_at(ctx, "subscripted value is not an array or pointer");
			emit_add_rax_rcx(c);
			ctx->lv = 1;
			continue;
		}
		if (k == T_LP) { next(ctx); ty = parse_call(ctx, ty); continue; }
		if (k == T_DOT) { next(ctx); ty = member_access(ctx, ty); continue; }
		if (k == T_ARROW) {
			next(ctx);
			ty = rval(ctx, ty);
			if (!is_ptr(ty)) error_at(ctx, "'->' on non-pointer");
			ty = member_access(ctx, ty->base);
			continue;
		}
		if (k == T_INC || k == T_DEC) {
			next(ctx);
			ty = gen_incdec(ctx, ty, k == T_INC ? 1 : -1, 1);
			continue;
		}
		return ty;
	}
}

static Type *parse_unary(Ctx *ctx) {
	Code *c = &ctx->text;
	int k = ctx->tok.kind;
	if (k == T_PLUS || k == T_MINUS || k == T_TILDE) {
		next(ctx);
		Type *ty = rval(ctx, parse_cast(ctx));
		if (is_flonum(ty) && k != T_TILDE) {
			if (k == T_MINUS) {
				if (ty->kind == TY_DOUBLE) { emit_mov_rcx_imm64(c, 0x8000000000000000ULL); emit_xor_rax_rcx(c); }
				else { C8(c,0x35); C32(c,0x80000000u); }
			}
			return ty;
		}
		if (!is_integer(ty)) error_at(ctx, "invalid operand to unary operator");
		ty = int_promote(ty);
		if (k == T_MINUS) emit_neg_rax(c);
		if (k == T_TILDE) emit_not_rax(c);
		if (ty->size == 4) emit_extend(c, 4, ty->is_unsigned);
		return ty;
	}
	if (k == T_NOT) {
		next(ctx);
		gen_truth(ctx, rval(ctx, parse_cast(ctx)));
		emit_logical_not(c);
		return ty_int;
	}
	if (k == T_AMP) {
		next(ctx);
		Type *ty = parse_cast(ctx);
		if (ty->kind == TY_FUNC) { ctx->lv = 0; ctx->fn_lea_sym = NULL; return ptr_to(ty); }
		if (!ctx->lv) error_at(ctx, "lvalue required as unary '&' operand");
		if (ty->bit_width) error_at(ctx, "cannot take address of bit-field");
		ctx->lv = 0;
		return ptr_to(ty);
	}
	if (k == T_MUL) {
		next(ctx);
		Type *ty = rval(ctx, parse_cast(ctx));
		if (!is_ptr(ty)) error_at(ctx, "invalid type argument of unary '*'");
		ty = ty->base;
		if (ty->kind == TY_FUNC) { ctx->lv = 0; return ty; }
		ctx->lv = 1;
		return ty;
	}
	if (k == T_INC || k == T_DEC) {
		next(ctx);
		Type *ty = parse_unary(ctx);
		return gen_incdec(ctx, ty, k == T_INC ? 1 : -1, 0);
	}
	if (k == T_SIZEOF) {
		next(ctx);
		long long n = sizeof_operand(ctx);
		emit_mov_rax_imm(c, n);
		ctx->lv = 0;
		return ty_ulong;
	}
	return parse_postfix(ctx);
}

static Type *local_init(Ctx *ctx, Type *ty, int rel);
static int is_string_init(Ctx *ctx, Type *ty);

static Type *parse_cast(Ctx *ctx) {
	if (ctx->tok.kind == T_LP && is_typename(ctx, peek(ctx))) {
		next(ctx);
		Type *t = parse_typename(ctx);
		expect(ctx, T_RP);
		if (ctx->tok.kind == T_LB) {
			if (!ctx->fn) error_at(ctx, "compound literal outside a function");
			Patches saved = ctx->init_patches;
			memset(&ctx->init_patches, 0, sizeof(Patches));
			t = local_init(ctx, t, 0);
			int off = alloc_local(ctx, t->size, t->align);
			for (int i = 0; i < ctx->init_patches.n; i++) {
				size_t pos = ctx->init_patches.v[i];
				buf_patch32_at(&ctx->text.b, pos, buf_read32_at(&ctx->text.b, pos) + (uint32_t)off);
			}
			free(ctx->init_patches.v);
			ctx->init_patches = saved;
			emit_lea_rbp(&ctx->text, RAX, off);
			ctx->lv = 1;
			return t;
		}
		Type *from = rval(ctx, parse_cast(ctx));
		if (t->kind == TY_STRUCT) error_at(ctx, "cast to struct type");
		gen_cast(ctx, from, t);
		ctx->lv = 0;
		return t;
	}
	return parse_unary(ctx);
}

static void gen_operands_to(Ctx *ctx, Type *lt, Type *rt, Type *t) {
	Code *c = &ctx->text;
	if (lt->kind == t->kind && rt->kind == t->kind) return;
	emit_push(c, RCX); ctx->sp++;
	gen_cast(ctx, rt, t);
	gen_pop(ctx, RCX);
	gen_push(ctx);
	emit_mov_reg_reg(c, RAX, RCX);
	gen_cast(ctx, lt, t);
	emit_mov_reg_reg(c, RCX, RAX);
	gen_pop(ctx, RAX);
}

static Type *gen_fbinop(Ctx *ctx, int op, Type *lt, Type *rt) {
	Code *c = &ctx->text;
	if (!is_arith(lt) || !is_arith(rt)) error_at(ctx, "invalid operands to binary operator");
	Type *t = arith_conv(lt, rt);
	int d = t->kind == TY_DOUBLE;
	gen_operands_to(ctx, lt, rt, t);
	emit_movq_xmm0_rcx(c);
	emit_movq_xmm1_rax(c);
	switch (op) {
	case T_PLUS: emit_sse_arith(c, d, 0x58); break;
	case T_MINUS: emit_sse_arith(c, d, 0x5C); break;
	case T_MUL: emit_sse_arith(c, d, 0x59); break;
	case T_DIV: emit_sse_arith(c, d, 0x5E); break;
	case T_GT: emit_ucomi(c, d, 0); emit_setcc_al(c, 0x97); emit_movzx_eax_al(c); return ty_int;
	case T_GEQ: emit_ucomi(c, d, 0); emit_setcc_al(c, 0x93); emit_movzx_eax_al(c); return ty_int;
	case T_LT: emit_ucomi(c, d, 1); emit_setcc_al(c, 0x97); emit_movzx_eax_al(c); return ty_int;
	case T_LEQ: emit_ucomi(c, d, 1); emit_setcc_al(c, 0x93); emit_movzx_eax_al(c); return ty_int;
	case T_EQ:
		emit_ucomi(c, d, 0); emit_setcc_al(c, 0x94); emit_setcc_cl(c, 0x9B);
		C8(c,0x20); C8(c,0xC8);
		emit_movzx_eax_al(c);
		return ty_int;
	case T_NEQ:
		emit_ucomi(c, d, 0); emit_setcc_al(c, 0x95); emit_setcc_cl(c, 0x9A);
		C8(c,0x08); C8(c,0xC8);
		emit_movzx_eax_al(c);
		return ty_int;
	default:
		error_at(ctx, "invalid operands to binary operator");
	}
	emit_xmm0_to_rax(c, d);
	return t;
}

static Type *gen_binop(Ctx *ctx, int op, Type *lt, Type *rt) {
	Code *c = &ctx->text;
	if (is_flonum(lt) || is_flonum(rt)) return gen_fbinop(ctx, op, lt, rt);
	if (op == T_PLUS || op == T_MINUS) {
		if (is_ptr(lt) && is_integer(rt)) {
			emit_scale(c, RAX, lt->base->size);
			if (op == T_PLUS) emit_add_rax_rcx(c);
			else { emit_xchg_rax_rcx(c); emit_sub_rax_rcx(c); }
			return lt;
		}
		if (is_integer(lt) && is_ptr(rt) && op == T_PLUS) {
			emit_scale(c, RCX, rt->base->size);
			emit_add_rax_rcx(c);
			return rt;
		}
		if (is_ptr(lt) && is_ptr(rt) && op == T_MINUS) {
			emit_xchg_rax_rcx(c);
			emit_sub_rax_rcx(c);
			int sz = lt->base->size;
			if (sz > 1) { emit_mov_ecx_imm(c, (uint32_t)sz); emit_cqo(c); emit_idiv_rcx(c); }
			return ty_long;
		}
	}
	if (op == T_EQ || op == T_NEQ || op == T_LT || op == T_GT || op == T_LEQ || op == T_GEQ) {
		Type *ct = (is_ptr(lt) || is_ptr(rt)) ? ty_ulong : arith_conv(lt, rt);
		if (ct->size == 4 && ct->is_unsigned) { C8(c,0x89); C8(c,0xC0); C8(c,0x89); C8(c,0xC9); }
		int u = ct->is_unsigned;
		CmpOp cmp = CMP_EQ;
		switch (op) {
		case T_EQ: cmp = CMP_EQ; break;
		case T_NEQ: cmp = CMP_NEQ; break;
		case T_LT: cmp = u ? CMP_ULT : CMP_LT; break;
		case T_GT: cmp = u ? CMP_UGT : CMP_GT; break;
		case T_LEQ: cmp = u ? CMP_ULEQ : CMP_LEQ; break;
		case T_GEQ: cmp = u ? CMP_UGEQ : CMP_GEQ; break;
		}
		emit_cmp_bool(c, cmp);
		return ty_int;
	}
	if (!is_integer(lt) || !is_integer(rt)) error_at(ctx, "invalid operands to binary operator");
	if (op == T_SHL || op == T_SHR) {
		Type *t = int_promote(lt);
		emit_xchg_rax_rcx(c);
		if (op == T_SHL) emit_shl_rax_cl(c);
		else if (t->is_unsigned) emit_shr_rax_cl(c);
		else emit_sar_rax_cl(c);
		if (t->size == 4) emit_extend(c, 4, t->is_unsigned);
		return t;
	}
	Type *t = arith_conv(lt, rt);
	if (t->size == 4 && t->is_unsigned) { C8(c,0x89); C8(c,0xC0); C8(c,0x89); C8(c,0xC9); }
	switch (op) {
	case T_PLUS: emit_add_rax_rcx(c); break;
	case T_MINUS: emit_xchg_rax_rcx(c); emit_sub_rax_rcx(c); break;
	case T_MUL: emit_imul_rax_rcx(c); break;
	case T_DIV:
	case T_MOD:
		emit_xchg_rax_rcx(c);
		if (t->is_unsigned) { emit_xor_edx_edx(c); emit_div_rcx(c); }
		else { emit_cqo(c); emit_idiv_rcx(c); }
		if (op == T_MOD) emit_mov_rax_rdx(c);
		break;
	case T_AMP: emit_and_rax_rcx(c); break;
	case T_PIPE: emit_or_rax_rcx(c); break;
	case T_CARET: emit_xor_rax_rcx(c); break;
	default: error_at(ctx, "internal: bad binary operator");
	}
	if (t->size == 4) emit_extend(c, 4, t->is_unsigned);
	return t;
}

static Type *parse_binary(Ctx *ctx, int minprec) {
	Code *c = &ctx->text;
	Type *lt = parse_cast(ctx);
	for (;;) {
		int k = ctx->tok.kind;
		int p = binop_prec(k);
		if (p == 0 || p < minprec) return lt;
		next(ctx);
		lt = rval(ctx, lt);
		if (k == T_AND || k == T_OR) {
			gen_truth(ctx, lt);
			emit_test_rax_rax(c);
			size_t sc = k == T_AND ? emit_jz_placeholder(c) : emit_jnz_placeholder(c);
			gen_truth(ctx, rval(ctx, parse_binary(ctx, p + 1)));
			emit_to_bool(c);
			size_t done = emit_jmp_placeholder(c);
			patch_jmp_to(&c->b, sc, Cpos(c));
			emit_mov_rax_imm(c, k == T_AND ? 0 : 1);
			patch_jmp_to(&c->b, done, Cpos(c));
			lt = ty_int;
			continue;
		}
		gen_push(ctx);
		Type *rt = rval(ctx, parse_binary(ctx, p + 1));
		gen_pop(ctx, RCX);
		lt = gen_binop(ctx, k, lt, rt);
	}
}

static Type *parse_cond(Ctx *ctx) {
	Code *c = &ctx->text;
	Type *ty = parse_binary(ctx, 1);
	if (ctx->tok.kind != T_QUESTION) return ty;
	next(ctx);
	gen_truth(ctx, rval(ctx, ty));
	emit_test_rax_rax(c);
	size_t jelse = emit_jz_placeholder(c);
	Type *a = rval(ctx, parse_expr(ctx));
	expect(ctx, T_COLON);
	size_t jend = emit_jmp_placeholder(c);
	patch_jmp_to(&c->b, jelse, Cpos(c));
	Type *b = rval(ctx, parse_cond(ctx));
	ctx->lv = 0;
	Type *t = a;
	if (is_arith(a) && is_arith(b)) t = arith_conv(a, b);
	else if (is_ptr(a)) t = a;
	else if (is_ptr(b)) t = b;
	if (is_arith(t)) gen_cast(ctx, b, t);
	if (is_arith(t) && a != t) {
		size_t jskip = emit_jmp_placeholder(c);
		patch_jmp_to(&c->b, jend, Cpos(c));
		gen_cast(ctx, a, t);
		patch_jmp_to(&c->b, jskip, Cpos(c));
	} else patch_jmp_to(&c->b, jend, Cpos(c));
	return t;
}

static int compound_op(int k) {
	switch (k) {
	case T_ADD_ASSIGN: return T_PLUS;
	case T_SUB_ASSIGN: return T_MINUS;
	case T_MUL_ASSIGN: return T_MUL;
	case T_DIV_ASSIGN: return T_DIV;
	case T_MOD_ASSIGN: return T_MOD;
	case T_AND_ASSIGN: return T_AMP;
	case T_OR_ASSIGN: return T_PIPE;
	case T_XOR_ASSIGN: return T_CARET;
	case T_SHL_ASSIGN: return T_SHL;
	case T_SHR_ASSIGN: return T_SHR;
	default: return 0;
	}
}

static Type *parse_assign(Ctx *ctx) {
	Code *c = &ctx->text;
	Type *lt = parse_cond(ctx);
	int k = ctx->tok.kind;
	if (k == T_ASSIGN) {
		if (!ctx->lv) error_at(ctx, "lvalue required as left operand of assignment");
		if (lt->kind == TY_ARRAY || lt->kind == TY_FUNC) error_at(ctx, "assignment to array or function");
		next(ctx);
		ctx->lv = 0;
		gen_push(ctx);
		Type *rt = rval(ctx, parse_assign(ctx));
		if (lt->kind == TY_STRUCT) {
			if (rt->kind != TY_STRUCT || rt->size != lt->size) error_at(ctx, "incompatible types in struct assignment");
			gen_pop(ctx, RDI);
			gen_struct_copy_to_rdi(ctx, lt->size);
			ctx->lv = 0;
			return lt;
		}
		gen_cast(ctx, rt, lt);
		gen_pop(ctx, RCX);
		emit_store_type(c, lt);
		ctx->lv = 0;
		return lt->origin ? lt->origin : lt;
	}
	int op = compound_op(k);
	if (op) {
		if (!ctx->lv) error_at(ctx, "lvalue required as left operand of assignment");
		if (!is_scalar(lt)) error_at(ctx, "invalid compound assignment");
		next(ctx);
		ctx->lv = 0;
		gen_push(ctx);
		emit_load_type(c, lt);
		gen_push(ctx);
		Type *rt = rval(ctx, parse_assign(ctx));
		gen_pop(ctx, RCX);
		Type *vt = lt->origin ? lt->origin : lt;
		Type *res = gen_binop(ctx, op, vt, rt);
		gen_cast(ctx, res, vt);
		gen_pop(ctx, RCX);
		emit_store_type(c, lt);
		ctx->lv = 0;
		return vt;
	}
	return lt;
}

static Type *parse_expr(Ctx *ctx) {
	Type *ty = parse_assign(ctx);
	while (ctx->tok.kind == T_COMMA) {
		next(ctx);
		ctx->lv = 0;
		ty = parse_assign(ctx);
	}
	return ty;
}

static void parse_cond_expr(Ctx *ctx) {
	Type *ty = rval(ctx, parse_expr(ctx));
	if (!is_scalar(ty)) error_at(ctx, "scalar required in condition");
	gen_truth(ctx, ty);
}

static void emit_lea_init(Ctx *ctx, int reg, int rel) {
	Code *c = &ctx->text;
	emit_lea_rbp(c, reg, rel);
	patches_push(&ctx->init_patches, Cpos(c) - 4);
}

static int is_string_init(Ctx *ctx, Type *ty) {
	if (ty->kind != TY_ARRAY || ty->base->size != 1 || !is_integer(ty->base)) return 0;
	if (ctx->tok.kind == T_STR) return 1;
	return ctx->tok.kind == T_LB && peek(ctx)->kind == T_STR;
}

static Buf read_string_lit(Ctx *ctx) {
	Buf s; buf_init(&s);
	int braced = consume(ctx, T_LB);
	while (ctx->tok.kind == T_STR) { buf_append(&s, ctx->tok.str, ctx->tok.len); next(ctx); }
	buf_append_byte(&s, 0);
	if (braced) { consume(ctx, T_COMMA); expect(ctx, T_RB); }
	return s;
}

static void skip_designator_eq(Ctx *ctx) { consume(ctx, T_ASSIGN); }

static void data_write(Ctx *ctx, long off, long long v, int size) {
	buf_zero_to(&ctx->data, (size_t)(off + size));
	for (int i = 0; i < size; i++) ctx->data.data[off + i] = (uint8_t)(v >> (8 * i));
}

static int global_addr_init(Ctx *ctx, long off) {
	int amp = 0;
	Tok *t = &ctx->tok;
	if (t->kind == T_AMP) amp = 1;
	else if (t->kind != T_IDENT) return 0;
	Tok *id = amp ? peek(ctx) : t;
	if (id->kind != T_IDENT) return 0;
	Sym *s = lookup(ctx, id->str);
	if (!s || (s->kind != SYM_VAR && s->kind != SYM_FUNC) || s->is_local) return 0;
	if (!amp && s->kind == SYM_VAR && s->ty->kind != TY_ARRAY) return 0;
	if (amp) next(ctx);
	next(ctx);
	Sym *g = s->global ? s->global : s;
	Type *ty = s->ty;
	long long addend = 0;
	for (;;) {
		if (ctx->tok.kind == T_LBRACKET && ty->kind == TY_ARRAY) {
			next(ctx);
			addend += const_expr(ctx) * ty->base->size;
			expect(ctx, T_RBRACKET);
			ty = ty->base;
		} else if (ctx->tok.kind == T_DOT && ty->kind == TY_STRUCT) {
			next(ctx);
			Member *m = find_member(ty, ctx->tok.str);
			if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
			next(ctx);
			addend += m->off;
			ty = m->ty;
		} else break;
	}
	int esz = ty->kind == TY_ARRAY && !amp ? ty->base->size : 1;
	if (ctx->tok.kind == T_PLUS) { next(ctx); addend += const_expr(ctx) * esz; }
	else if (ctx->tok.kind == T_MINUS) { next(ctx); addend -= const_expr(ctx) * esz; }
	data_write(ctx, off, 0, 8);
	add_reloc(ctx, R_ABS64, (size_t)off, g, SEC_TEXT, addend);
	return 1;
}

static void init_scalar(Ctx *ctx, Type *ty, long off, int global) {
	if (!global) {
		emit_lea_init(ctx, RAX, (int)off);
		gen_push(ctx);
		Type *rt = rval(ctx, parse_assign(ctx));
		gen_cast(ctx, rt, ty);
		gen_pop(ctx, RCX);
		emit_store_type(&ctx->text, ty);
		return;
	}
	if (ctx->tok.kind == T_STR && is_ptr(ty)) {
		Buf s; buf_init(&s);
		while (ctx->tok.kind == T_STR) { buf_append(&s, ctx->tok.str, ctx->tok.len); next(ctx); }
		buf_append_byte(&s, 0);
		long ro = add_rodata(ctx, s.data, s.len);
		free(s.data);
		data_write(ctx, off, 0, 8);
		add_reloc(ctx, R_ABS64, (size_t)off, NULL, SEC_RODATA, ro);
	} else if (is_flonum(ty)) {
		data_write(ctx, off, (long long)float_bits(fce_add(ctx), ty), ty->size);
	} else if (ty->bit_width) {
		uint64_t mask = ty->bit_width >= 64 ? ~0ULL : (1ULL << ty->bit_width) - 1;
		uint64_t v = ((uint64_t)const_expr(ctx) & mask) << ty->bit_off;
		buf_zero_to(&ctx->data, (size_t)(off + ty->size));
		uint64_t cur = 0;
		for (int i = 0; i < ty->size; i++) cur |= (uint64_t)ctx->data.data[off + i] << (8 * i);
		cur = (cur & ~(mask << ty->bit_off)) | v;
		data_write(ctx, off, (long long)cur, ty->size);
	} else if (!(is_ptr(ty) || ty->size == 8) || !global_addr_init(ctx, off)) {
		long long v = const_expr(ctx);
		data_write(ctx, off, const_cast_val(v, ty), ty->size);
	}
}

static Type *init_string(Ctx *ctx, Type *ty, long off, int global) {
	Buf s = read_string_lit(ctx);
	if (ty->len < 0) ty = array_of(ty->base, (int)s.len);
	int n = (int)s.len < ty->len ? (int)s.len : ty->len;
	if (global) {
		buf_zero_to(&ctx->data, (size_t)(off + ty->size));
		memcpy(ctx->data.data + off, s.data, n);
	} else {
		long ro = add_rodata(ctx, s.data, s.len);
		emit_lea_rodata(ctx, RSI, ro);
		emit_lea_init(ctx, RDI, (int)off);
		emit_memcpy(&ctx->text, n);
	}
	free(s.data);
	return ty;
}

static int init_continue(Ctx *ctx, int braced) {
	if (ctx->tok.kind != T_COMMA) return 0;
	if (!braced) {
		Tok *p = peek(ctx);
		if (p->kind == T_RB || p->kind == T_DOT || p->kind == T_LBRACKET) return 0;
	}
	next(ctx);
	return ctx->tok.kind != T_RB;
}

static Type *init_value(Ctx *ctx, Type *ty, long off, int global, int top);

static void init_designated(Ctx *ctx, Type *ty, long off, int global) {
	if (ctx->tok.kind == T_LBRACKET && ty->kind == TY_ARRAY) {
		next(ctx);
		long i = (long)const_expr(ctx);
		expect(ctx, T_RBRACKET);
		init_designated(ctx, ty->base, off + i * ty->base->size, global);
		return;
	}
	if (ctx->tok.kind == T_DOT && ty->kind == TY_STRUCT) {
		next(ctx);
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected field name");
		Member *m = find_member(ty, ctx->tok.str);
		if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
		next(ctx);
		init_designated(ctx, m->ty, off + m->off, global);
		return;
	}
	skip_designator_eq(ctx);
	init_value(ctx, ty, off, global, 0);
}

static int init_elems(Ctx *ctx, Type *ty, long off, int global, int braced) {
	if (ctx->tok.kind == T_RB) return 0;
	if (ty->kind == TY_ARRAY) {
		int esz = ty->base->size;
		int i = 0, maxi = 0;
		for (;;) {
			int chained = 0;
			if (ctx->tok.kind == T_LBRACKET) {
				if (!braced) break;
				next(ctx);
				i = (int)const_expr(ctx);
				expect(ctx, T_RBRACKET);
				chained = ctx->tok.kind == T_DOT || ctx->tok.kind == T_LBRACKET;
				if (!chained) skip_designator_eq(ctx);
			}
			if (ty->len >= 0 && i >= ty->len) {
				if (braced) error_at(ctx, "excess elements in array initializer");
				break;
			}
			if (chained) init_designated(ctx, ty->base, off + (long)i * esz, global);
			else init_value(ctx, ty->base, off + (long)i * esz, global, 0);
			i++;
			if (i > maxi) maxi = i;
			if (!braced && ty->len >= 0 && i >= ty->len) break;
			if (!init_continue(ctx, braced)) break;
		}
		return maxi;
	}
	Member *m = ty->members;
	for (;;) {
		int chained = 0;
		if (ctx->tok.kind == T_DOT) {
			if (!braced) break;
			next(ctx);
			if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected field name");
			m = find_member(ty, ctx->tok.str);
			if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
			next(ctx);
			chained = ctx->tok.kind == T_DOT || ctx->tok.kind == T_LBRACKET;
			if (!chained) skip_designator_eq(ctx);
		}
		if (!m) {
			if (braced) error_at(ctx, "excess elements in struct initializer");
			break;
		}
		if (chained) init_designated(ctx, m->ty, off + m->off, global);
		else init_value(ctx, m->ty, off + m->off, global, 0);
		m = ty->is_union ? NULL : m->next;
		if (!braced && !m) break;
		if (!init_continue(ctx, braced)) break;
	}
	return 0;
}

static Type *init_value(Ctx *ctx, Type *ty, long off, int global, int top) {
	if (ty->kind == TY_ARRAY) {
		if (is_string_init(ctx, ty)) return init_string(ctx, ty, off, global);
		int braced = consume(ctx, T_LB);
		if (!braced && top) error_at(ctx, "array initializer must be a brace-enclosed list");
		int n = init_elems(ctx, ty, off, global, braced);
		if (braced) expect(ctx, T_RB);
		if (ty->len < 0) ty = array_of(ty->base, n);
		return ty;
	}
	if (ty->kind == TY_STRUCT) {
		if (ctx->tok.kind != T_LB && top && !global) {
			Type *rt = rval(ctx, parse_assign(ctx));
			if (rt->kind != TY_STRUCT || rt->size != ty->size) error_at(ctx, "incompatible struct initializer");
			emit_lea_init(ctx, RDI, (int)off);
			gen_struct_copy_to_rdi(ctx, ty->size);
			return ty;
		}
		int braced = consume(ctx, T_LB);
		if (!braced && top) error_at(ctx, "struct initializer must be a brace-enclosed list");
		init_elems(ctx, ty, off, global, braced);
		if (braced) expect(ctx, T_RB);
		return ty;
	}
	if (consume(ctx, T_LB)) {
		init_scalar(ctx, ty, off, global);
		consume(ctx, T_COMMA);
		expect(ctx, T_RB);
		return ty;
	}
	init_scalar(ctx, ty, off, global);
	return ty;
}

static Type *local_init(Ctx *ctx, Type *ty, int rel) {
	Code *c = &ctx->text;
	if (ty->kind == TY_ARRAY || (ty->kind == TY_STRUCT && ctx->tok.kind == T_LB)) {
		emit_lea_init(ctx, RDI, rel);
		emit_xor_eax_eax(c);
		C8(c,0xB9); size_t szpos = Cpos(c); C32(c,0);
		emit_rep_stosb(c);
		ty = init_value(ctx, ty, rel, 0, 1);
		buf_patch32_at(&c->b, szpos, (uint32_t)ty->size);
		return ty;
	}
	return init_value(ctx, ty, rel, 0, 1);
}

static Type *global_init(Ctx *ctx, Type *ty, long off) {
	ty = init_value(ctx, ty, off, 1, 1);
	buf_zero_to(&ctx->data, (size_t)(off + ty->size));
	return ty;
}

static void define_global_var(Ctx *ctx, Sym *g, Type *ty, int has_init) {
	if (has_init) {
		if (g->defined && g->sect == SEC_DATA) error_at(ctx, "redefinition of '%s'", g->name);
		buf_zero_to(&ctx->data, align_up(ctx->data.len, (size_t)(ty->align < 8 ? 8 : ty->align)));
		long off = (long)ctx->data.len;
		ty = global_init(ctx, ty, off);
		g->ty = ty;
		g->sect = SEC_DATA;
		g->off = off;
		g->size = ty->size;
		g->defined = 1;
		buf_zero_to(&ctx->data, (size_t)(off + ty->size));
		return;
	}
	if (g->defined) return;
	if (ty->kind == TY_ARRAY && ty->len < 0) return;
	ctx->bss_size = (long)align_up((size_t)ctx->bss_size, (size_t)(ty->align < 8 ? 8 : ty->align));
	g->sect = SEC_BSS;
	g->off = ctx->bss_size;
	g->size = ty->size;
	g->defined = 1;
	ctx->bss_size += ty->size;
}

static void parse_block(Ctx *ctx);

static Label *find_label(Ctx *ctx, char *name) {
	for (Label *l = ctx->fn->labels; l; l = l->next)
		if (l->name == name) return l;
	Label *l = (Label *)calloc(1, sizeof(Label));
	l->name = name;
	l->pos = -1;
	l->next = ctx->fn->labels;
	ctx->fn->labels = l;
	return l;
}

static int parse_static_assert(Ctx *ctx) {
	if (ctx->tok.kind != T_IDENT || ctx->tok.str != id_static_assert) return 0;
	next(ctx);
	expect(ctx, T_LP);
	long long v = const_expr(ctx);
	const char *msg = "static assertion failed";
	if (consume(ctx, T_COMMA)) {
		if (ctx->tok.kind != T_STR) error_at(ctx, "expected string literal in _Static_assert");
		msg = ctx->tok.str;
		next(ctx);
		while (ctx->tok.kind == T_STR) next(ctx);
	}
	expect(ctx, T_RP);
	if (!v) error_at(ctx, "static assertion failed: %s", msg);
	expect(ctx, T_SEMI);
	return 1;
}

static void parse_local_decl(Ctx *ctx) {
	int storage;
	Type *base = declspec(ctx, &storage);
	if (ctx->tok.kind == T_SEMI) { next(ctx); return; }
	for (;;) {
		char *name = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		if (storage == ST_TYPEDEF) {
			push_sym(ctx, name, SYM_TYPEDEF, ty);
		} else if (ty->kind == TY_FUNC) {
			Sym *g = get_global(ctx, name, SYM_FUNC, ty, 0);
			bind_global(ctx, g);
		} else if (storage == ST_EXTERN) {
			Sym *g = get_global(ctx, name, SYM_VAR, ty, 0);
			bind_global(ctx, g);
		} else if (storage == ST_STATIC) {
			Sym *g = (Sym *)calloc(1, sizeof(Sym));
			g->name = name; g->kind = SYM_VAR; g->ty = ty; g->is_static = 1;
			g->gnext = ctx->globals;
			ctx->globals = g;
			Sym *a = push_sym(ctx, name, SYM_VAR, ty);
			a->global = g;
			int has_init = consume(ctx, T_ASSIGN);
			define_global_var(ctx, g, ty, has_init);
			a->ty = g->ty;
		} else {
			for (Sym *s = ctx->scope; s && s->depth == ctx->depth; s = s->next)
				if (s->name == name) error_at(ctx, "redeclaration of '%s'", name);
			if (ty->kind == TY_VOID) error_at(ctx, "variable '%s' declared void", name);
			Sym *s = push_sym(ctx, name, SYM_VAR, ty);
			s->is_local = 1;
			if (consume(ctx, T_ASSIGN)) {
				Patches saved = ctx->init_patches;
				memset(&ctx->init_patches, 0, sizeof(Patches));
				int known = !(ty->kind == TY_ARRAY && ty->len < 0);
				int off = 0;
				if (known) off = alloc_local(ctx, ty->size, ty->align);
				s->stack_off = off;
				ty = local_init(ctx, ty, 0);
				if (!known) {
					off = alloc_local(ctx, ty->size, ty->align);
					s->stack_off = off;
				}
				s->ty = ty;
				for (int i = 0; i < ctx->init_patches.n; i++) {
					size_t pos = ctx->init_patches.v[i];
					buf_patch32_at(&ctx->text.b, pos, buf_read32_at(&ctx->text.b, pos) + (uint32_t)off);
				}
				free(ctx->init_patches.v);
				ctx->init_patches = saved;
			} else {
				if (ty->kind == TY_ARRAY && ty->len < 0) error_at(ctx, "array size missing in '%s'", name);
				if (ty->kind == TY_STRUCT && !ty->complete) error_at(ctx, "storage size of '%s' isn't known", name);
				s->stack_off = alloc_local(ctx, ty->size, ty->align);
			}
		}
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_SEMI);
}

static void parse_stmt(Ctx *ctx) {
	Code *c = &ctx->text;
	int k = ctx->tok.kind;

	if (k == T_LB) { parse_block(ctx); return; }
	if (k == T_SEMI) { next(ctx); return; }

	if (k == T_RETURN) {
		next(ctx);
		if (ctx->tok.kind != T_SEMI) {
			Type *t = rval(ctx, parse_expr(ctx));
			Type *rt = ctx->fn->ty->base;
			if (rt->kind == TY_STRUCT) {
				emit_load_rbp(c, RDI, ctx->fn->ret_off);
				gen_struct_copy_to_rdi(ctx, rt->size);
			} else if (rt->kind != TY_VOID) gen_cast(ctx, t, rt);
		}
		expect(ctx, T_SEMI);
		emit_leave(c);
		emit_ret(c);
		return;
	}

	if (k == T_IF) {
		next(ctx);
		expect(ctx, T_LP);
		parse_cond_expr(ctx);
		expect(ctx, T_RP);
		emit_test_rax_rax(c);
		size_t jz_patch = emit_jz_placeholder(c);
		parse_stmt(ctx);
		if (ctx->tok.kind == T_ELSE) {
			next(ctx);
			size_t jmp_patch = emit_jmp_placeholder(c);
			patch_jmp_to(&c->b, jz_patch, Cpos(c));
			parse_stmt(ctx);
			patch_jmp_to(&c->b, jmp_patch, Cpos(c));
		} else {
			patch_jmp_to(&c->b, jz_patch, Cpos(c));
		}
		return;
	}

	if (k == T_BREAK) {
		next(ctx);
		expect(ctx, T_SEMI);
		LoopCtx *lc = loopstack_cur(&ctx->loops);
		if (!lc) error_at(ctx, "'break' outside loop");
		patches_push(&lc->breaks, emit_jmp_placeholder(c));
		return;
	}

	if (k == T_CONTINUE) {
		next(ctx);
		expect(ctx, T_SEMI);
		LoopCtx *lc = loopstack_loop(&ctx->loops);
		if (!lc) error_at(ctx, "'continue' outside loop");
		if (lc->cont_target >= 0) emit_jmp_to(c, (size_t)lc->cont_target);
		else patches_push(&lc->conts, emit_jmp_placeholder(c));
		return;
	}

	if (k == T_WHILE) {
		next(ctx);
		size_t loop_top = Cpos(c);
		expect(ctx, T_LP);
		parse_cond_expr(ctx);
		expect(ctx, T_RP);
		emit_test_rax_rax(c);
		size_t jz_patch = emit_jz_placeholder(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, (long)loop_top, 0);
		parse_stmt(ctx);
		loopstack_pop(&ctx->loops);
		emit_jmp_to(c, loop_top);
		size_t loop_end = Cpos(c);
		patch_jmp_to(&c->b, jz_patch, loop_end);
		for (int i = 0; i < lc->breaks.n; i++)
			patch_jmp_to(&c->b, lc->breaks.v[i], loop_end);
		return;
	}

	if (k == T_DO) {
		next(ctx);
		size_t loop_top = Cpos(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, -1, 0);
		parse_stmt(ctx);
		loopstack_pop(&ctx->loops);
		for (int i = 0; i < lc->conts.n; i++)
			patch_jmp_to(&c->b, lc->conts.v[i], Cpos(c));
		expect(ctx, T_WHILE);
		expect(ctx, T_LP);
		parse_cond_expr(ctx);
		expect(ctx, T_RP);
		expect(ctx, T_SEMI);
		emit_test_rax_rax(c);
		size_t jnz = emit_jnz_placeholder(c);
		patch_jmp_to(&c->b, jnz, loop_top);
		for (int i = 0; i < lc->breaks.n; i++)
			patch_jmp_to(&c->b, lc->breaks.v[i], Cpos(c));
		return;
	}

	if (k == T_FOR) {
		next(ctx);
		expect(ctx, T_LP);
		enter_scope(ctx);
		if (ctx->tok.kind != T_SEMI) {
			if (is_typename(ctx, &ctx->tok)) parse_local_decl(ctx);
			else { parse_expr(ctx); ctx->lv = 0; expect(ctx, T_SEMI); }
		} else next(ctx);
		size_t top = Cpos(c);
		int has_cond = ctx->tok.kind != T_SEMI;
		size_t jz_end = 0;
		if (has_cond) {
			parse_cond_expr(ctx);
			emit_test_rax_rax(c);
			jz_end = emit_jz_placeholder(c);
		}
		expect(ctx, T_SEMI);
		size_t jbody = emit_jmp_placeholder(c);
		size_t step = Cpos(c);
		if (ctx->tok.kind != T_RP) { parse_expr(ctx); ctx->lv = 0; }
		expect(ctx, T_RP);
		emit_jmp_to(c, top);
		patch_jmp_to(&c->b, jbody, Cpos(c));
		LoopCtx *lc = loopstack_push(&ctx->loops, (long)step, 0);
		parse_stmt(ctx);
		loopstack_pop(&ctx->loops);
		emit_jmp_to(c, step);
		size_t end = Cpos(c);
		if (has_cond) patch_jmp_to(&c->b, jz_end, end);
		for (int i = 0; i < lc->breaks.n; i++)
			patch_jmp_to(&c->b, lc->breaks.v[i], end);
		leave_scope(ctx);
		return;
	}

	if (k == T_SWITCH) {
		next(ctx);
		expect(ctx, T_LP);
		Type *t = rval(ctx, parse_expr(ctx));
		expect(ctx, T_RP);
		if (!is_integer(t)) error_at(ctx, "switch quantity not an integer");
		int tmp = alloc_local(ctx, 8, 8);
		emit_store_rbp(c, RAX, tmp);
		size_t jdispatch = emit_jmp_placeholder(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, -1, 1);
		lc->sw_type = int_promote(t);
		parse_stmt(ctx);
		loopstack_pop(&ctx->loops);
		size_t jend = emit_jmp_placeholder(c);
		patch_jmp_to(&c->b, jdispatch, Cpos(c));
		emit_load_rbp(c, RAX, tmp);
		for (int i = 0; i < lc->ncase; i++) {
			emit_mov_rcx_imm64(c, (uint64_t)lc->case_vals[i]);
			C8(c,0x48); C8(c,0x39); C8(c,0xC8);
			emit_je_to(c, lc->case_pos[i]);
		}
		if (lc->default_pos >= 0) emit_jmp_to(c, (size_t)lc->default_pos);
		size_t end = Cpos(c);
		patch_jmp_to(&c->b, jend, end);
		for (int i = 0; i < lc->breaks.n; i++)
			patch_jmp_to(&c->b, lc->breaks.v[i], end);
		return;
	}

	if (k == T_CASE) {
		next(ctx);
		LoopCtx *lc = loopstack_switch(&ctx->loops);
		if (!lc) error_at(ctx, "'case' outside switch");
		long long v = const_cast_val(const_expr(ctx), lc->sw_type);
		expect(ctx, T_COLON);
		if (lc->ncase == lc->capcase) {
			lc->capcase = lc->capcase ? lc->capcase * 2 : 16;
			lc->case_vals = (long long *)realloc(lc->case_vals, sizeof(long long) * lc->capcase);
			lc->case_pos = (size_t *)realloc(lc->case_pos, sizeof(size_t) * lc->capcase);
		}
		lc->case_vals[lc->ncase] = v;
		lc->case_pos[lc->ncase] = Cpos(c);
		lc->ncase++;
		if (ctx->tok.kind != T_RB) parse_stmt(ctx);
		return;
	}

	if (k == T_DEFAULT) {
		next(ctx);
		expect(ctx, T_COLON);
		LoopCtx *lc = loopstack_switch(&ctx->loops);
		if (!lc) error_at(ctx, "'default' outside switch");
		lc->default_pos = (long)Cpos(c);
		if (ctx->tok.kind != T_RB) parse_stmt(ctx);
		return;
	}

	if (k == T_GOTO) {
		next(ctx);
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected label name");
		Label *l = find_label(ctx, ctx->tok.str);
		next(ctx);
		expect(ctx, T_SEMI);
		if (l->pos >= 0) emit_jmp_to(c, (size_t)l->pos);
		else patches_push(&l->patches, emit_jmp_placeholder(c));
		return;
	}

	if (k == T_IDENT && peek(ctx)->kind == T_COLON) {
		Label *l = find_label(ctx, ctx->tok.str);
		if (l->pos >= 0) error_at(ctx, "duplicate label '%s'", ctx->tok.str);
		next(ctx);
		next(ctx);
		l->pos = (long)Cpos(c);
		for (int i = 0; i < l->patches.n; i++)
			patch_jmp_to(&c->b, l->patches.v[i], (size_t)l->pos);
		if (ctx->tok.kind != T_RB) parse_stmt(ctx);
		return;
	}

	if (is_typename(ctx, &ctx->tok)) { parse_local_decl(ctx); return; }
	if (parse_static_assert(ctx)) return;

	parse_expr(ctx);
	ctx->lv = 0;
	expect(ctx, T_SEMI);
}

static void parse_block(Ctx *ctx) {
	expect(ctx, T_LB);
	enter_scope(ctx);
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in block");
		parse_stmt(ctx);
	}
	leave_scope(ctx);
	expect(ctx, T_RB);
}

static void parse_func_body(Ctx *ctx, Sym *fs, Type *fty) {
	Code *c = &ctx->text;
	Func f;
	memset(&f, 0, sizeof(f));
	f.sym = fs;
	f.ty = fty;
	ctx->fn = &f;
	ctx->sp = 0;
	fs->off = (long)Cpos(c);
	fs->sect = SEC_TEXT;
	fs->defined = 1;

	emit_push_rbp(c);
	emit_mov_rbp_rsp(c);
	C8(c,0x48); C8(c,0x81); C8(c,0xEC);
	f.frame_patch = Cpos(c);
	C32(c, 0);

	enter_scope(ctx);
	int sret = fty->base->kind == TY_STRUCT;
	int nreg = sret ? 5 : 6;
	int nregparams = fty->nparams < nreg ? fty->nparams : nreg;
	int nsave = (fty->variadic ? 6 : nregparams + sret);
	if (nsave > 0 || fty->variadic) {
		alloc_local(ctx, 48, 8);
		for (int i = 0; i < nsave; i++) emit_store_rbp(c, argregs[i], -48 + 8 * i);
	}
	if (sret) f.ret_off = -48;
	int idx = 0;
	for (Param *p = fty->params; p; p = p->next, idx++) {
		int slot;
		if (idx < nreg) slot = -48 + 8 * (idx + sret);
		else slot = 16 + 8 * (idx - nreg);
		int off = slot;
		if (p->ty->kind == TY_STRUCT) {
			off = alloc_local(ctx, p->ty->size, p->ty->align);
			emit_load_rbp(c, RSI, slot);
			emit_lea_rbp(c, RDI, off);
			emit_memcpy(c, p->ty->size);
		}
		if (p->name) {
			Sym *s = push_sym(ctx, p->name, SYM_VAR, p->ty);
			s->is_local = 1;
			s->stack_off = off;
		}
	}
	f.named_gp = nregparams + sret;
	f.named_stack = fty->nparams > nreg ? fty->nparams - nreg : 0;

	expect(ctx, T_LB);
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in function body");
		parse_stmt(ctx);
	}
	next(ctx);
	leave_scope(ctx);

	emit_xor_eax_eax(c);
	emit_leave(c);
	emit_ret(c);

	for (Label *l = f.labels; l; l = l->next)
		if (l->pos < 0) error_at(ctx, "label '%s' used but not defined", l->name);

	uint32_t frame_size = (uint32_t)align_up((size_t)f.frame, 16);
	buf_patch32_at(&c->b, f.frame_patch, frame_size);
	fs->size = (long)Cpos(c) - fs->off;
	ctx->fn = NULL;
}

static void parse_toplevel(Ctx *ctx) {
	int storage;
	if (parse_static_assert(ctx)) return;
	Type *base = declspec(ctx, &storage);
	if (consume(ctx, T_SEMI)) return;
	for (;;) {
		char *name = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		if (storage == ST_TYPEDEF) {
			push_sym(ctx, name, SYM_TYPEDEF, ty);
		} else if (ty->kind == TY_FUNC) {
			Sym *g = get_global(ctx, name, SYM_FUNC, ty, storage == ST_STATIC);
			bind_global(ctx, g);
			if (ctx->tok.kind == T_LB) {
				if (g->defined) {
					if (!ctx->in_libc) error_at(ctx, "redefinition of '%s'", name);
					g = (Sym *)calloc(1, sizeof(Sym));
					g->name = name; g->kind = SYM_FUNC; g->is_static = 1;
				}
				g->ty = ty;
				parse_func_body(ctx, g, ty);
				return;
			}
		} else {
			Sym *g = get_global(ctx, name, SYM_VAR, ty, storage == ST_STATIC);
			bind_global(ctx, g);
			int has_init = consume(ctx, T_ASSIGN);
			if (storage != ST_EXTERN || has_init) define_global_var(ctx, g, g->ty, has_init);
		}
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_SEMI);
}

static void compile_unit(Ctx *ctx, const char *src, const char *file, int is_builtin) {
	pp_begin_unit(ctx);
	push_file(ctx, src, file, is_builtin);
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	next(ctx);
	while (ctx->tok.kind != T_EOF) parse_toplevel(ctx);
	pp_end_unit(ctx);
	ctx->scope = NULL;
	ctx->tags = NULL;
}

#define ELF_BASE 0x400000ULL

static void shdr(uint8_t *p, uint32_t name, uint32_t type, uint64_t flags, uint64_t addr,
			uint64_t off, uint64_t size, uint32_t link, uint32_t info, uint64_t align, uint64_t entsize) {
	le32(p+0x00,name); le32(p+0x04,type); le64(p+0x08,flags); le64(p+0x10,addr);
	le64(p+0x18,off); le64(p+0x20,size); le32(p+0x28,link); le32(p+0x2C,info);
	le64(p+0x30,align); le64(p+0x38,entsize);
}

static uint32_t strtab_add(Buf *b, const char *s) {
	uint32_t off = (uint32_t)b->len;
	buf_append(b, s, strlen(s) + 1);
	return off;
}

static void add_elf_sym(Buf *symtab, Buf *strtab, Sym *s, uint64_t *sect_va, int bind) {
	static const int shndx[4] = { 1, 2, 3, 4 };
	uint8_t e[24];
	memset(e, 0, 24);
	le32(e, strtab_add(strtab, s->name));
	e[4] = (uint8_t)((bind << 4) | (s->kind == SYM_FUNC ? 2 : 1));
	le16(e+6, (uint16_t)shndx[s->sect]);
	le64(e+8, sect_va[s->sect] + s->off);
	le64(e+16, (uint64_t)s->size);
	buf_append(symtab, e, 24);
}

static void write_elf(Ctx *ctx, const char *out_path) {
	const size_t headers = 64 + 2*56;
	Code *text = &ctx->text;
	if (ctx->data.len == 0) buf_zero_to(&ctx->data, 8);

	size_t text_off = headers;
	size_t ro_off = align_up(text_off + text->b.len, 16);
	size_t text_end = ro_off + ctx->rodata.len;
	size_t data_off = align_up(text_end, 0x1000);
	uint64_t text_va = ELF_BASE + text_off;
	uint64_t ro_va = ELF_BASE + ro_off;
	uint64_t data_va = ELF_BASE + data_off;
	uint64_t bss_va = align_up(data_va + ctx->data.len, 16);
	uint64_t sect_va[4];
	sect_va[SEC_TEXT] = text_va; sect_va[SEC_RODATA] = ro_va;
	sect_va[SEC_DATA] = data_va; sect_va[SEC_BSS] = bss_va;

	for (int i = 0; i < ctx->nrelocs; i++) {
		Reloc *r = &ctx->relocs[i];
		uint64_t target;
		if (r->sym) {
			if (!r->sym->defined) { fprintf(stderr, "error: undefined reference to '%s'\n", r->sym->name); exit(1); }
			target = sect_va[r->sym->sect] + r->sym->off + r->addend;
		} else target = sect_va[r->sect] + r->addend;
		if (r->kind == R_PC32) {
			int64_t rel = (int64_t)target - (int64_t)(text_va + r->pos + 4);
			if (rel != (int32_t)rel) { fprintf(stderr, "internal: relocation out of range\n"); exit(1); }
			buf_patch32_at(&text->b, r->pos, (uint32_t)(int32_t)rel);
		} else {
			le64(ctx->data.data + r->pos, target);
		}
	}

	Buf file; buf_init(&file);
	buf_zero_to(&file, headers);
	buf_append(&file, text->b.data, text->b.len);
	buf_zero_to(&file, ro_off);
	buf_append(&file, ctx->rodata.data, ctx->rodata.len);
	buf_zero_to(&file, data_off);
	buf_append(&file, ctx->data.data, ctx->data.len);

	Buf symtab, strtab, shstr;
	buf_init(&symtab); buf_init(&strtab); buf_init(&shstr);
	buf_append_byte(&strtab, 0);
	buf_zero_to(&symtab, 24);
	int nlocal = 1;
	{
		uint8_t e[24];
		memset(e, 0, 24);
		le32(e, strtab_add(&strtab, "_start"));
		e[4] = 2;
		le16(e+6, 1);
		le64(e+8, text_va);
		buf_append(&symtab, e, 24);
		nlocal++;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (Sym *s = ctx->globals; s; s = s->gnext) {
			if (!s->defined || (s->kind != SYM_FUNC && s->kind != SYM_VAR)) continue;
			if (s->is_static != (pass == 0)) continue;
			add_elf_sym(&symtab, &strtab, s, sect_va, pass == 0 ? 0 : 1);
			if (pass == 0) nlocal++;
		}
	}

	buf_append_byte(&shstr, 0);
	uint32_t n_text = strtab_add(&shstr, ".text");
	uint32_t n_ro = strtab_add(&shstr, ".rodata");
	uint32_t n_data = strtab_add(&shstr, ".data");
	uint32_t n_bss = strtab_add(&shstr, ".bss");
	uint32_t n_sym = strtab_add(&shstr, ".symtab");
	uint32_t n_str = strtab_add(&shstr, ".strtab");
	uint32_t n_shs = strtab_add(&shstr, ".shstrtab");

	buf_zero_to(&file, align_up(file.len, 8));
	size_t sym_off = file.len;
	buf_append(&file, symtab.data, symtab.len);
	size_t str_off = file.len;
	buf_append(&file, strtab.data, strtab.len);
	size_t shs_off = file.len;
	buf_append(&file, shstr.data, shstr.len);
	buf_zero_to(&file, align_up(file.len, 8));
	size_t sh_off = file.len;
	buf_zero_to(&file, sh_off + 8 * 64);
	uint8_t *S = file.data + sh_off;
	shdr(S+1*64, n_text, 1, 6, text_va, text_off, text->b.len, 0, 0, 16, 0);
	shdr(S+2*64, n_ro, 1, 2, ro_va, ro_off, ctx->rodata.len, 0, 0, 16, 0);
	shdr(S+3*64, n_data, 1, 3, data_va, data_off, ctx->data.len, 0, 0, 16, 0);
	shdr(S+4*64, n_bss, 8, 3, bss_va, data_off + (bss_va - data_va), (uint64_t)ctx->bss_size, 0, 0, 16, 0);
	shdr(S+5*64, n_sym, 2, 0, 0, sym_off, symtab.len, 6, (uint32_t)nlocal, 8, 24);
	shdr(S+6*64, n_str, 3, 0, 0, str_off, strtab.len, 0, 0, 1, 0);
	shdr(S+7*64, n_shs, 3, 0, 0, shs_off, shstr.len, 0, 0, 1, 0);

	uint8_t *E=file.data;
	E[0]=0x7F;E[1]='E';E[2]='L';E[3]='F';E[4]=2;E[5]=1;E[6]=1;E[7]=0;
	le16(E+0x10,2); le16(E+0x12,62); le32(E+0x14,1);
	le64(E+0x18,text_va);
	le64(E+0x20,64); le64(E+0x28,sh_off); le32(E+0x30,0);
	le16(E+0x34,64); le16(E+0x36,56); le16(E+0x38,2);
	le16(E+0x3A,64); le16(E+0x3C,8); le16(E+0x3E,7);

	uint8_t *P=file.data+64;
	le32(P+0x00,1); le32(P+0x04,5);
	le64(P+0x08,0); le64(P+0x10,ELF_BASE); le64(P+0x18,ELF_BASE);
	le64(P+0x20,text_end); le64(P+0x28,text_end); le64(P+0x30,0x1000);
	P += 56;
	le32(P+0x00,1); le32(P+0x04,6);
	le64(P+0x08,data_off); le64(P+0x10,data_va); le64(P+0x18,data_va);
	le64(P+0x20,ctx->data.len); le64(P+0x28,(bss_va - data_va) + (uint64_t)ctx->bss_size); le64(P+0x30,0x1000);

	remove(out_path);
	FILE *out=fopen(out_path,"wb");
	if (!out) { perror(out_path); exit(1); }
	if (fwrite(file.data,1,file.len,out)!=file.len) { perror("fwrite"); exit(1); }
	fclose(out);
	chmod(out_path, 0755);

	free(file.data); free(symtab.data); free(strtab.data); free(shstr.data);
}

static void emit_start(Ctx *ctx, Sym *main_sym, Sym *exit_sym, Sym *init_sym, Sym *environ_sym) {
	Code *c = &ctx->text;
	C8(c,0x31); C8(c,0xED);
	C8(c,0x48); C8(c,0x8B); C8(c,0x3C); C8(c,0x24);
	C8(c,0x48); C8(c,0x8D); C8(c,0x74); C8(c,0x24); C8(c,0x08);
	C8(c,0x48); C8(c,0x8D); C8(c,0x54); C8(c,0xFE); C8(c,0x08);
	C8(c,0x48); C8(c,0x83); C8(c,0xE4); C8(c,0xF0);
	if (environ_sym) {
		C8(c,0x48); C8(c,0x89); C8(c,0x15);
		add_reloc(ctx, R_PC32, Cpos(c), environ_sym, SEC_TEXT, 0);
		C32(c,0);
	}
	if (init_sym) {
		emit_push(c, RDI); emit_push(c, RSI); emit_push(c, RDX); emit_push(c, RDX);
		emit_call_sym(ctx, init_sym);
		emit_pop(c, RDX); emit_pop(c, RDX); emit_pop(c, RSI); emit_pop(c, RDI);
	}
	emit_call_sym(ctx, main_sym);
	C8(c,0x89); C8(c,0xC7);
	if (exit_sym) emit_call_sym(ctx, exit_sym);
	C8(c,0xB8); C32(c,231);
	emit_syscall(c);
}

static void ctx_init(Ctx *ctx) {
	memset(ctx, 0, sizeof(*ctx));
	code_init(&ctx->text);
	buf_init(&ctx->rodata);
	buf_init(&ctx->data);
	loopstack_init(&ctx->loops);
}

int main(int argc, char **argv) {
	const char *out = "a.out";
	const char *inputs[64];
	int ninputs = 0;
	Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
	ctx_init(ctx);
	for (int i=1;i<argc;i++) {
		const char *a = argv[i];
		if (strcmp(a,"-o")==0&&i+1<argc) out=argv[++i];
		else if (a[0]=='-' && a[1]=='I' && ctx->nincdirs < 64) ctx->incdirs[ctx->nincdirs++] = a[2] ? a+2 : (i+1<argc ? argv[++i] : ".");
		else if (a[0]=='-' && a[1]=='D' && ctx->ndefines < 64) ctx->defines[ctx->ndefines++] = a[2] ? a+2 : (i+1<argc ? argv[++i] : "");
		else if (strcmp(a,"-nostdlib")==0) ctx->nostdlib = 1;
		else if (a[0]=='-') { fprintf(stderr,"unknown arg: %s\n",a); return 1; }
		else if (ninputs < 64) inputs[ninputs++] = a;
	}
	if (ninputs == 0) { fprintf(stderr,"usage: %s <src>... [-o <out>] [-I dir] [-D name[=val]] [-nostdlib]\n",argv[0]); return 1; }

	keywords_init();
	types_init();
	id_main = intern("main");
	id_syscall = intern("__syscall");
	id_va_start = intern("__builtin_va_start");
	id_exit = intern("exit");
	id_libc_init = intern("__libc_init");
	id_environ = intern("environ");
	id_defined = intern("defined");
	id_file = intern("__FILE__");
	id_line = intern("__LINE__");
	id_va_args = intern("__VA_ARGS__");
	id_func = intern("__func__");
	id_function = intern("__FUNCTION__");
	id_static_assert = intern("_Static_assert");

	Type *mt = func_type(ty_int);
	mt->oldstyle = 1;
	Sym *main_sym = get_global(ctx, id_main, SYM_FUNC, mt, 0);
	Sym *exit_sym = NULL, *init_sym = NULL, *environ_sym = NULL;
	if (!ctx->nostdlib) {
		Type *vt = func_type(ty_void);
		vt->oldstyle = 1;
		exit_sym = get_global(ctx, id_exit, SYM_FUNC, vt, 0);
		init_sym = get_global(ctx, id_libc_init, SYM_FUNC, vt, 0);
		environ_sym = get_global(ctx, id_environ, SYM_VAR, ptr_to(ptr_to(ty_char)), 0);
	}
	emit_start(ctx, main_sym, exit_sym, init_sym, environ_sym);

	for (int i = 0; i < ninputs; i++) {
		char *src = read_file(inputs[i]);
		compile_unit(ctx, src, inputs[i], 0);
	}
	if (!ctx->nostdlib) {
		ctx->in_libc = 1;
		compile_unit(ctx, builtin_libc_src(), "<libc>", 1);
		ctx->in_libc = 0;
	}
	if (!main_sym->defined) { fprintf(stderr, "error: undefined reference to 'main'\n"); return 1; }

	write_elf(ctx, out);
	return 0;
}

#include "builtin.h"

static const char *find_builtin_header(const char *name) {
	for (int i = 0; builtin_names[i]; i++)
		if (strcmp(builtin_names[i], name) == 0) return builtin_srcs[i];
	return NULL;
}

static const char *builtin_libc_src(void) {
	Buf b; buf_init(&b);
	for (int i = 0; builtin_libc[i]; i++) buf_append(&b, builtin_libc[i], strlen(builtin_libc[i]));
	buf_append_byte(&b, 0);
	return (const char *)b.data;
}
