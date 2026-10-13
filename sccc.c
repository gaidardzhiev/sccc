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
	T_DOLLAR = '$',
	T_AT = '@',
	T_INT = 256, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE, T_TYPEOF, T_ALIGNOF, T_ALIGNAS, T_NORETURN, T_GENERIC,
	T_EQ, T_NEQ, T_LEQ, T_GEQ,
	T_AND,
	T_OR,
	T_SHL, T_SHR, T_INC, T_DEC, T_ARROW, T_ELLIPSIS, T_HASHHASH,
	T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN, T_MOD_ASSIGN,
	T_AND_ASSIGN, T_OR_ASSIGN, T_XOR_ASSIGN, T_SHL_ASSIGN, T_SHR_ASSIGN,
	T_IDENT, T_NUM, T_FNUM, T_STR
} TKind;

typedef struct Attr Attr;
struct Attr {
	int align;
	char *section;
	char *alias;
};

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
	int attrs;
	Attr *attr;
} Tok;

typedef struct {
	const char *s;
	size_t i, n;
	int line;
	const char *file;
	int bol, space;
	int as;
} Lex;

static const char *kw_names[] = {
	"int", "return", "if", "else", "while", "break", "continue",
	"char", "short", "long", "signed", "unsigned", "void", "_Bool",
	"struct", "union", "enum", "typedef", "static", "extern",
	"const", "volatile", "inline", "register", "auto", "restrict",
	"sizeof", "for", "do", "switch", "case", "default", "goto",
	"float", "double", "typeof", "_Alignof", "_Alignas", "_Noreturn", "_Generic",
	"__inline", "__restrict", "__const", "__signed__", "__inline__", "__restrict__",
	"__typeof__", "__typeof", "__alignof__", "__alignof", "__signed", "__volatile",
	NULL
};
static const int kw_kinds[] = {
	T_INT, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE, T_TYPEOF, T_ALIGNOF, T_ALIGNAS, T_NORETURN, T_GENERIC,
	T_INLINE, T_RESTRICT, T_CONST, T_SIGNED, T_INLINE, T_RESTRICT,
	T_TYPEOF, T_TYPEOF, T_ALIGNOF, T_ALIGNOF, T_SIGNED, T_VOLATILE,
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

static int lex_asm;

static void skip_ws(Lex *L) {
	while (L->i < L->n) {
		char c = L->s[L->i];
		if (c=='#' && (L->as == 2 || (L->as == 1 && !L->bol))) {
			while (L->i<L->n && L->s[L->i]!='\n') L->i++;
			L->space = 1; continue;
		}
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

	if (L->as && isdigit((unsigned char)c)) {
		size_t j = L->i;
		while (j<L->n && isdigit((unsigned char)L->s[j])) j++;
		if (j<L->n && (L->s[j]=='b' || L->s[j]=='f') &&
			!(j+1<L->n && (isalnum((unsigned char)L->s[j+1]) || L->s[j+1]=='_'))) {
			t.kind = T_IDENT;
			t.id = intern_str(L->s+L->i, (int)(j+1-L->i));
			t.str = t.id->s;
			L->i = j+1; return t;
		}
	}

	if (isalpha((unsigned char)c) || c=='_' || (c=='$' && !L->as) ||
		(c=='.' && L->as && !(L->i+1<L->n && isdigit((unsigned char)L->s[L->i+1])))) {
		size_t j=L->i;
		while (j<L->n && (isalnum((unsigned char)L->s[j]) || L->s[j]=='_' || (L->s[j]=='$' && !L->as) || (L->s[j]=='.' && L->as))) j++;
		Str *e = intern_str(L->s+L->i, (int)(j-L->i));
		t.kind = e->kw && !L->as ? (TKind)e->kw : T_IDENT;
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
			else if (d=='l' || d=='L') { l++; L->i++; }
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
		if (L->as && (L->i >= L->n || L->s[L->i] != '\'')) { t.kind = T_NUM; t.val = v; return t; }
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

	if (strchr("(){}[];,.+-*/%=<>!~&|^?:#", c) || (L->as && (c=='$' || c=='@'))) {
		L->i++; t.kind = (TKind)c; return t;
	}
	fprintf(stderr,"%s:%d: error: unexpected character '%c'\n", L->file, L->line, c);
	exit(1);
}

static void lex_init(Lex *L, const char *src, const char *file) {
	L->s=src; L->i=0; L->n=strlen(src); L->line=1; L->file=file; L->bol=1; L->space=0;
	L->as = lex_asm ? 3 : 0;
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
	int bit_unit;
	Type *origin;
};

static Type *ty_void, *ty_bool, *ty_char, *ty_uchar, *ty_short, *ty_ushort;
static Type *ty_int, *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double;
static Type *ty_schar, *ty_llong, *ty_ullong;

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
	ty_schar = new_int_type(TY_CHAR, 1, 0);
	ty_llong = new_int_type(TY_LONG, 8, 0);
	ty_ullong = new_int_type(TY_LONG, 8, 1);
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
	t->bit_unit = base->size;
	t->origin = base;
	return t;
}

static int is_integer(Type *t) { return t->kind >= TY_BOOL && t->kind <= TY_LONG; }
static int is_ptr(Type *t) { return t->kind == TY_PTR; }
static int is_flonum(Type *t) { return t->kind == TY_FLOAT || t->kind == TY_DOUBLE; }
static int is_arith(Type *t) { return is_integer(t) || is_flonum(t); }
static int is_scalar(Type *t) { return is_arith(t) || is_ptr(t); }

static Type *canon(Type *t) {
	while (t->origin) t = t->origin;
	return t;
}

static int same_type(Type *a, Type *b) {
	a = canon(a); b = canon(b);
	if (a == b) return 1;
	if (a->kind != b->kind) return 0;
	switch (a->kind) {
	case TY_PTR:
		return same_type(a->base, b->base);
	case TY_ARRAY:
		return same_type(a->base, b->base) && (a->len < 0 || b->len < 0 || a->len == b->len);
	case TY_FUNC: {
		if (!same_type(a->base, b->base)) return 0;
		if (a->oldstyle || b->oldstyle) return 1;
		if (a->nparams != b->nparams || a->variadic != b->variadic) return 0;
		for (Param *p = a->params, *q = b->params; p; p = p->next, q = q->next)
			if (!same_type(p->ty, q->ty)) return 0;
		return 1;
	}
	case TY_VOID:
		return 1;
	default:
		return 0;
	}
}

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
enum { SEC_ABS = -1, SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS, SEC_NAMED };
enum { SK_CODE, SK_RO, SK_DATA, SK_NOALLOC };

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
	int interrupt;
	int naked;
	int align;
	int weak, weak_unit, weak_def;
	char *alias_name;
	Sym *alias;
	char *section;
	int sect;
	long off;
	long size;
	int is_asm;
	int asm_reg;
	int elf_index;
	long common;
	int common_align;
	long got_off;
	int ref;
	int tentative;
	int ld_out;
	int piece;
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
	long long *case_hi;
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

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, ENC_RIP };

static void enc_rex(Code *c, int w, int r, int x, int b, int force) {
	int rex = (w ? 8 : 0) | (r >= 8 ? 4 : 0) | (x >= 8 ? 2 : 0) | (b >= 8 ? 1 : 0);
	if (rex || force) C8(c,(uint8_t)(0x40 | rex));
}

static void enc_reg(Code *c, int reg, int rm) { C8(c,(uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7))); }

static size_t enc_mem(Code *c, int reg, int base, int index, int scale, long long disp, int wide, int sib_abs) {
	int r = (reg & 7) << 3;
	int ss = scale == 8 ? 3 : scale == 4 ? 2 : scale == 2 ? 1 : 0;
	size_t at;
	if (base == ENC_RIP) {
		C8(c,(uint8_t)(r | 5));
		at = Cpos(c); C32(c,(uint32_t)disp);
		return at;
	}
	if (base < 0) {
		if (index >= 0) { C8(c,(uint8_t)(r | 4)); C8(c,(uint8_t)((ss << 6) | ((index & 7) << 3) | 5)); }
		else if (sib_abs) { C8(c,(uint8_t)(r | 4)); C8(c,0x25); }
		else C8(c,(uint8_t)(r | 5));
		at = Cpos(c); C32(c,(uint32_t)disp);
		return at;
	}
	int mod = wide ? 2 : disp == 0 && (base & 7) != 5 ? 0 : disp >= -128 && disp <= 127 ? 1 : 2;
	if (index >= 0 || (base & 7) == 4) {
		C8(c,(uint8_t)((mod << 6) | r | 4));
		C8(c,(uint8_t)((ss << 6) | ((index < 0 ? 4 : index & 7) << 3) | (base & 7)));
	} else C8(c,(uint8_t)((mod << 6) | r | (base & 7)));
	at = Cpos(c);
	if (mod == 1) C8(c,(uint8_t)disp);
	else if (mod == 2) C32(c,(uint32_t)disp);
	return at;
}

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

static void emit_push(Code *c, int reg) { enc_rex(c, 0, 0, 0, reg, 0); C8(c,(uint8_t)(0x50 + (reg & 7))); }
static void emit_pop(Code *c, int reg) { enc_rex(c, 0, 0, 0, reg, 0); C8(c,(uint8_t)(0x58 + (reg & 7))); }

static void emit_mov_reg_reg(Code *c, int dst, int src) {
	enc_rex(c, 1, src, 0, dst, 0); C8(c,0x89); enc_reg(c, src, dst);
}

static void emit_rm_wide(Code *c, uint8_t op, int reg, int base, int disp) {
	enc_rex(c, 1, reg, 0, base, 0); C8(c,op); enc_mem(c, reg, base, -1, 1, disp, 1, 0);
}

static void emit_load_rbp(Code *c, int reg, int disp) { emit_rm_wide(c, 0x8B, reg, RBP, disp); }
static void emit_store_rbp(Code *c, int reg, int disp) { emit_rm_wide(c, 0x89, reg, RBP, disp); }
static void emit_lea_rbp(Code *c, int reg, int disp) { emit_rm_wide(c, 0x8D, reg, RBP, disp); }
static void emit_load_rsp(Code *c, int reg, int disp) { emit_rm_wide(c, 0x8B, reg, RSP, disp); }
static void emit_store_rsp_rax(Code *c, int disp) { emit_rm_wide(c, 0x89, RAX, RSP, disp); }

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

static size_t jfwd(Code *c, uint8_t op) { C8(c,op); C8(c,0); return Cpos(c) - 1; }
static void jland(Code *c, size_t p) { c->b.data[p] = (uint8_t)(Cpos(c) - p - 1); }

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

typedef enum { R_PC32, R_ABS64, R_ABS32, R_ABS32S, R_ABS16, R_ABS8, R_PC16, R_PC8, R_PC64, R_PLT32 } RelKind;

static const int rel_size[] = { 4, 8, 4, 4, 2, 1, 2, 1, 8, 4 };
static const int rel_elf[] = { 2, 1, 10, 11, 12, 14, 13, 15, 24, 4 };

static int rel_pc(int k) { return k == R_PC32 || k == R_PC16 || k == R_PC8 || k == R_PC64 || k == R_PLT32; }

typedef struct {
	RelKind kind;
	size_t pos;
	int in;
	Sym *sym;
	int sect;
	long long addend;
	int piece;
} Reloc;

typedef struct {
	char *name;
	int kind;
	int align;
	Buf b;
} Sec;

typedef struct {
	int sect;
	long start, end;
	char *name;
	const char *file, *arch;
	int align, note;
	int out;
	uint64_t va;
} Piece;

typedef struct Ld Ld;

typedef struct Label Label;
struct Label {
	char *name;
	long pos;
	Patches patches;
	Patches abs;
	Label *next;
};

typedef struct {
	Sym *sym;
	Type *ty;
	int frame;
	size_t frame_patch;
	int ret_off;
	int named_gp;
	int named_sse;
	int named_stack;
	int save_off;
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
	int freestanding;
	int binary;
	int image;
	int multiboot;
	int no_float;
	int decl_attrs;
	Attr decl_attr;
	int unit;
	int text_align, data_align, bss_align, rodata_align;
	int has_ttext;
	uint64_t ttext;
	Sym *entry_sym;
	Tok tok;
	Tok pk;
	int has_pk;
	Code text;
	Buf rodata, data;
	long bss_size;
	Sec *secs;
	int nsecs, capsecs;
	int cur_text, cur_data;
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
	int pp_bol;
	Buf text_home;
	size_t lv_end;
	Sym *lv_sym;
	int lv_off;
	int asm_count;
	Ld *ld;
	int track;
	Piece *pieces;
	int npieces, cappieces;
	long *covered;
	int *unit_align;
	int ncovered, rel_mark;
	const char *arch, *member;
} Ctx;

static const char *find_builtin_header(const char *name);
static const char *builtin_libc_src(void);

static char *id_va_arg;
static char *id_main, *id_syscall, *id_va_start, *id_exit, *id_libc_init, *id_environ;
enum { ATTR_INTERRUPT = 1, ATTR_PACKED = 2, ATTR_WEAK = 4, ATTR_NAKED = 8 };
static char *id_attribute, *id_attribute2, *id_interrupt, *id_packed, *id_aligned, *id_weak, *id_alias, *id_section;
static char *id_defined, *id_file, *id_line, *id_va_args, *id_func, *id_function, *id_static_assert;
static char *id_asm, *id_asm2, *id_asm3, *id_naked;
static char *id_expect, *id_constant_p, *id_types_compatible_p, *id_choose_expr, *id_offsetof, *id_unreachable, *id_trap;

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
	if (t->kind >= T_INT && t->kind <= T_GENERIC) return kw_names[t->kind - T_INT];
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
	if (!d.id && L->as) return;
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
	if (L->as) return;
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
		snprintf(tmp, sizeof(tmp), "%lld%s%s", t->val, t->is_unsigned ? "u" : "", t->is_long > 1 ? "ll" : t->is_long ? "l" : "");
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
	for (int j = 0; j < res.n; j++) res.v[j].bol = j == 0 && t->bol;
	if (!res.n && t->bol) ctx->pp_bol = 1;
	push_tokens(ctx, res.v, res.n, m);
	return 1;
}

static Tok expand_next(Ctx *ctx) {
	for (;;) {
		Tok t = raw_next(ctx);
		if (ctx->pp_bol) { t.bol = 1; ctx->pp_bol = 0; }
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
		"__extension__", "__volatile__ volatile",
		NULL
	};
	for (int i = 0; predef[i]; i++) pp_define_str(ctx, predef[i]);
	if (ctx->freestanding) pp_define_str(ctx, "__STDC_HOSTED__ 0");
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

static long long eval_tokens(Ctx *ctx, TokVec *v) {
	TokVec e;
	memset(&e, 0, sizeof(e));
	for (int i = 0; i < v->n; i++) tv_push(&e, v->v[i]);
	Tok end;
	memset(&end, 0, sizeof(end));
	end.kind = T_EOF; end.hide = (void *)&pp_sentinel_mark;
	end.file = ctx->tok.file; end.line = ctx->tok.line;
	tv_push(&e, end);
	Tok saved_tok = ctx->tok, saved_pk = ctx->pk, saved_back = ctx->pp_back;
	int saved_has_pk = ctx->has_pk, saved_has_back = ctx->has_pp_back;
	push_tokens(ctx, e.v, e.n, NULL);
	Src *frame = ctx->src;
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	next(ctx);
	long long r = const_expr(ctx);
	if (!is_sentinel(&ctx->tok)) error_at(ctx, "expected constant expression in attribute");
	if (ctx->src == frame) ctx->src = frame->parent;
	ctx->tok = saved_tok; ctx->pk = saved_pk; ctx->pp_back = saved_back;
	ctx->has_pk = saved_has_pk; ctx->has_pp_back = saved_has_back;
	return r;
}

static char *attr_string(Ctx *ctx, char *name, TokVec *v) {
	Buf b; buf_init(&b);
	for (int i = 0; i < v->n; i++) {
		if (v->v[i].kind != T_STR) error_at(ctx, "attribute '%s' takes a string", name);
		buf_append(&b, v->v[i].str, v->v[i].len);
	}
	if (!b.len) error_at(ctx, "attribute '%s' takes a string", name);
	char *r = intern_str((char *)b.data, (int)b.len)->s;
	free(b.data);
	return r;
}

static int apply_attr(Ctx *ctx, Attr *a, char *name, TokVec *args, int has_args) {
	int n = (int)strlen(name);
	if (n > 4 && name[0] == '_' && name[1] == '_' && name[n-1] == '_' && name[n-2] == '_')
		name = intern_str(name + 2, n - 4)->s;
	if (name == id_interrupt) return ATTR_INTERRUPT;
	if (name == id_packed) return ATTR_PACKED;
	if (name == id_weak) return ATTR_WEAK;
	if (name == id_naked) return ATTR_NAKED;
	if (name == id_aligned) {
		long long v = has_args ? eval_tokens(ctx, args) : 16;
		if (v <= 0 || (v & (v - 1)) || v > 0x10000000) error_at(ctx, "requested alignment is not a positive power of 2");
		if (v > a->align) a->align = (int)v;
	} else if (name == id_section) a->section = attr_string(ctx, name, args);
	else if (name == id_alias) a->alias = attr_string(ctx, name, args);
	return 0;
}

static int skip_attribute(Ctx *ctx, Attr *a) {
	int attrs = 0, depth = 0;
	char *name = NULL;
	TokVec args;
	memset(&args, 0, sizeof(args));
	do {
		Tok t = pp_next(ctx);
		if (t.kind == T_EOF) error_at(ctx, "unterminated __attribute__");
		if (depth == 0 && t.kind != T_LP) error_at(ctx, "expected '(' after __attribute__");
		if (depth >= 3) {
			if (t.kind == T_LP) depth++;
			else if (t.kind == T_RP && --depth == 2) {
				attrs |= apply_attr(ctx, a, name, &args, 1);
				name = NULL;
				args.n = 0;
				continue;
			}
			tv_push(&args, t);
			continue;
		}
		if (t.kind == T_LP) {
			if (depth == 2 && !name) error_at(ctx, "expected attribute name");
			depth++;
		} else if (t.kind == T_RP || (t.kind == T_COMMA && depth == 2)) {
			if (depth == 2 && name) attrs |= apply_attr(ctx, a, name, &args, 0);
			name = NULL;
			if (t.kind == T_RP) depth--;
		} else if (depth == 2 && t.id && !name) name = t.str;
		else error_at(ctx, "malformed __attribute__");
	} while (depth > 0);
	free(args.v);
	return attrs;
}

static void attr_merge(Attr *d, Attr *s) {
	if (!s) return;
	if (s->align > d->align) d->align = s->align;
	if (s->section) d->section = s->section;
	if (s->alias) d->alias = s->alias;
}

static Tok parse_next(Ctx *ctx) {
	int attrs = 0;
	Attr *a = NULL;
	for (;;) {
		Tok t = pp_next(ctx);
		if (t.kind == T_IDENT && (t.str == id_attribute || t.str == id_attribute2)) {
			if (!a) a = (Attr *)calloc(1, sizeof(Attr));
			attrs |= skip_attribute(ctx, a);
			continue;
		}
		t.attrs |= attrs;
		if (a) { attr_merge(a, t.attr); t.attr = a; }
		return t;
	}
}

static void next(Ctx *ctx) {
	if (ctx->has_pk) { ctx->tok = ctx->pk; ctx->has_pk = 0; }
	else ctx->tok = parse_next(ctx);
}

static Tok *peek(Ctx *ctx) {
	if (!ctx->has_pk) { ctx->pk = parse_next(ctx); ctx->has_pk = 1; }
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

static TokVec capture_expr(Ctx *ctx) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_EOF) error_at(ctx, "unexpected end of file");
		if (depth == 0 && (k == T_COMMA || k == T_RP || k == T_RB || k == T_SEMI)) break;
		if (k == T_LP || k == T_LBRACKET || k == T_LB) depth++;
		else if (k == T_RP || k == T_RBRACKET || k == T_RB) depth--;
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	return v;
}

static void replay(Ctx *ctx, TokVec *v) {
	TokVec r;
	memset(&r, 0, sizeof(r));
	for (int i = 0; i < v->n; i++) tv_push(&r, v->v[i]);
	tv_push(&r, ctx->tok);
	if (ctx->has_pk) tv_push(&r, ctx->pk);
	if (ctx->has_pp_back) tv_push(&r, ctx->pp_back);
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	push_tokens(ctx, r.v, r.n, NULL);
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
		if (s->is_asm) { s->is_asm = 0; s->kind = kind; s->ty = ty; }
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

static void add_reloc(Ctx *ctx, RelKind kind, size_t pos, Sym *sym, int sect, long long addend) {
	if (ctx->nrelocs == ctx->caprelocs) {
		ctx->caprelocs = ctx->caprelocs ? ctx->caprelocs * 2 : 256;
		ctx->relocs = (Reloc *)realloc(ctx->relocs, sizeof(Reloc) * ctx->caprelocs);
		if (!ctx->relocs) { perror("realloc"); exit(1); }
	}
	Reloc *r = &ctx->relocs[ctx->nrelocs++];
	r->kind = kind; r->pos = pos; r->sym = sym; r->sect = sect; r->addend = addend; r->piece = 0;
	r->in = kind == R_PC32 || kind == R_PLT32 ? ctx->cur_text : ctx->cur_data;
}

static int section_index(Ctx *ctx, char *name, int kind) {
	if (kind == SK_CODE && strcmp(name, ".text") == 0) return SEC_TEXT;
	if (kind == SK_DATA && strcmp(name, ".data") == 0) return SEC_DATA;
	for (int i = 0; i < ctx->nsecs; i++) {
		if (ctx->secs[i].name != name) continue;
		if (ctx->secs[i].kind != kind) error_at(ctx, "section '%s' is used for different kinds of objects", name);
		return SEC_NAMED + i;
	}
	if (ctx->nsecs == ctx->capsecs) {
		ctx->capsecs = ctx->capsecs ? ctx->capsecs * 2 : 8;
		ctx->secs = (Sec *)realloc(ctx->secs, sizeof(Sec) * ctx->capsecs);
		if (!ctx->secs) { perror("realloc"); exit(1); }
	}
	Sec *sec = &ctx->secs[ctx->nsecs++];
	memset(sec, 0, sizeof(Sec));
	sec->name = name;
	sec->kind = kind;
	sec->align = kind == SK_CODE ? 16 : 1;
	return SEC_NAMED + ctx->nsecs - 1;
}

static Buf *sec_buf(Ctx *ctx, int sect) {
	if (sect == ctx->cur_text) return &ctx->text.b;
	if (sect == SEC_TEXT) return &ctx->text_home;
	if (sect == SEC_RODATA) return &ctx->rodata;
	if (sect == SEC_DATA) return &ctx->data;
	return &ctx->secs[sect - SEC_NAMED].b;
}

static int is_noalloc(Ctx *ctx, int sect) {
	return sect >= SEC_NAMED && ctx->secs[sect - SEC_NAMED].kind == SK_NOALLOC;
}

static int *align_ref(Ctx *ctx, int sect) {
	return sect == SEC_TEXT ? &ctx->text_align : sect == SEC_DATA ? &ctx->data_align :
		sect == SEC_BSS ? &ctx->bss_align : sect == SEC_RODATA ? &ctx->rodata_align : &ctx->secs[sect - SEC_NAMED].align;
}

static void raise_align(Ctx *ctx, int sect, int a) {
	int *p = align_ref(ctx, sect);
	if (a > *p) *p = a;
}

static const char *sect_name(Ctx *ctx, int sect) {
	return sect == SEC_TEXT ? ".text" : sect == SEC_RODATA ? ".rodata" : sect == SEC_DATA ? ".data" :
		sect == SEC_BSS ? ".bss" : ctx->secs[sect - SEC_NAMED].name;
}

static long sect_len(Ctx *ctx, int sect) { return sect == SEC_BSS ? ctx->bss_size : (long)sec_buf(ctx, sect)->len; }

static void piece_grow(Ctx *ctx) {
	int n = SEC_NAMED + ctx->nsecs;
	if (n <= ctx->ncovered) return;
	ctx->covered = (long *)realloc(ctx->covered, sizeof(long) * n);
	ctx->unit_align = (int *)realloc(ctx->unit_align, sizeof(int) * n);
	if (!ctx->covered || !ctx->unit_align) { perror("realloc"); exit(1); }
	for (int i = ctx->ncovered; i < n; i++) ctx->covered[i] = ctx->unit_align[i] = 0;
	ctx->ncovered = n;
}

static void add_piece(Ctx *ctx, int sect, long start, long end, char *name, const char *file, int align) {
	if (!ctx->track) return;
	piece_grow(ctx);
	if (ctx->npieces == ctx->cappieces) {
		ctx->cappieces = ctx->cappieces ? ctx->cappieces * 2 : 64;
		ctx->pieces = (Piece *)realloc(ctx->pieces, sizeof(Piece) * ctx->cappieces);
		if (!ctx->pieces) { perror("realloc"); exit(1); }
	}
	Piece *p = &ctx->pieces[ctx->npieces++];
	p->sect = sect;
	p->start = start;
	p->end = end;
	p->name = name;
	p->file = ctx->member ? ctx->member : file;
	p->arch = ctx->arch;
	p->align = align > 0 ? align : 1;
	p->note = !strncmp(name, ".note", 5);
	p->out = -2;
	p->va = 0;
	if (end > ctx->covered[sect]) ctx->covered[sect] = end;
}

static int unit_piece(Ctx *ctx, int first, int sect, long off) {
	int best = 0;
	for (int i = first; i < ctx->npieces; i++) {
		Piece *p = &ctx->pieces[i];
		if (p->sect != sect || p->start > off || off > p->end) continue;
		if (!best || off < p->end) best = i + 1;
		if (off < p->end) break;
	}
	return best;
}

static void piece_begin(Ctx *ctx) {
	if (!ctx->track) return;
	ctx->rel_mark = ctx->nrelocs;
	piece_grow(ctx);
	for (int s = 0; s < ctx->ncovered; s++) {
		int *a = align_ref(ctx, s);
		ctx->unit_align[s] = *a;
		*a = 0;
	}
}

static void piece_end(Ctx *ctx, const char *file) {
	if (!ctx->track) return;
	int n0 = ctx->ncovered, first = ctx->npieces;
	piece_grow(ctx);
	for (int s = 0; s < ctx->ncovered; s++) {
		int *a = align_ref(ctx, s);
		long len = sect_len(ctx, s);
		if (len > ctx->covered[s] && !is_noalloc(ctx, s))
			add_piece(ctx, s, ctx->covered[s], len, intern(sect_name(ctx, s)), file, *a);
		ctx->covered[s] = len;
		if (s < n0 && ctx->unit_align[s] > *a) *a = ctx->unit_align[s];
	}
	for (int i = first - 1; i >= 0 && ctx->pieces[i].file == file; i--) first = i;
	if (first == ctx->npieces) return;
	for (Sym *g = ctx->globals; g; g = g->gnext)
		if (g->defined && !g->piece && g->sect != SEC_ABS && (g->kind == SYM_FUNC || g->kind == SYM_VAR))
			g->piece = unit_piece(ctx, first, g->sect, g->off);
	for (int i = ctx->rel_mark; i < ctx->nrelocs; i++) {
		Reloc *r = &ctx->relocs[i];
		if (!r->sym && !r->piece && r->sect != SEC_ABS) r->piece = unit_piece(ctx, first, r->sect, (long)r->addend);
	}
}

static void emit_lea_rip(Ctx *ctx, int reg, Sym *sym, int sect, long off) {
	Code *c = &ctx->text;
	enc_rex(c, 1, reg, 0, 0, 0); C8(c,0x8D);
	add_reloc(ctx, R_PC32, enc_mem(c, reg, ENC_RIP, -1, 1, 0, 1, 0), sym, sect, off);
}

static void emit_lea_sym(Ctx *ctx, Sym *sym) { emit_lea_rip(ctx, RAX, sym, SEC_TEXT, 0); }
static void emit_lea_rodata(Ctx *ctx, int reg, long off) { emit_lea_rip(ctx, reg, NULL, SEC_RODATA, off); }

static void emit_call_sym(Ctx *ctx, Sym *sym) {
	Code *c = &ctx->text;
	C8(c,0xE8);
	add_reloc(ctx, R_PLT32, Cpos(c), sym, SEC_TEXT, 0);
	C32(c,0);
}

static long add_rodata(Ctx *ctx, const void *p, size_t n) {
	while (ctx->rodata.len % 8) buf_append_byte(&ctx->rodata, 0);
	long off = (long)ctx->rodata.len;
	buf_append(&ctx->rodata, p, n);
	return off;
}

static void emit_memcpy(Code *c, int n) { emit_mov_ecx_imm(c, (uint32_t)n); emit_rep_movsb(c); }

typedef struct {
	size_t pos;
	int nrel, sp;
} Mark;

static Mark mark_code(Ctx *ctx) {
	Mark m;
	m.pos = Cpos(&ctx->text);
	m.nrel = ctx->nrelocs;
	m.sp = ctx->sp;
	return m;
}

static void drop_code(Ctx *ctx, Mark m) {
	if (ctx->fn) {
		for (Label *l = ctx->fn->labels; l; l = l->next) {
			int k = 0;
			for (int i = 0; i < l->patches.n; i++) if (l->patches.v[i] < m.pos) l->patches.v[k++] = l->patches.v[i];
			l->patches.n = k;
			k = 0;
			for (int i = 0; i < l->abs.n; i++) if ((int)l->abs.v[i] < m.nrel) l->abs.v[k++] = l->abs.v[i];
			l->abs.n = k;
		}
	}
	ctx->text.b.len = m.pos;
	ctx->nrelocs = m.nrel;
	ctx->sp = m.sp;
	ctx->lv = 0;
	ctx->fn_lea_sym = NULL;
}

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

static void emit_bf_unit_load(Code *c, int n) {
	if (n == 1 || n == 2 || n == 4 || n == 8) { emit_load(c, n, 1); return; }
	C8(c,0x49); C8(c,0x89); C8(c,0xC3);
	C8(c,0x31); C8(c,0xC0);
	for (int i = n - 1; i >= 0; i--) {
		if (i < n - 1) { C8(c,0x48); C8(c,0xC1); C8(c,0xE0); C8(c,0x08); }
		C8(c,0x41); C8(c,0x8A); C8(c,0x43); C8(c,(uint8_t)i);
	}
}

static void emit_bf_unit_store(Code *c, int n) {
	if (n == 1 || n == 2 || n == 4 || n == 8) { emit_store(c, n); return; }
	for (int i = 0; i < n; i++) {
		if (i) { C8(c,0x48); C8(c,0xC1); C8(c,0xE8); C8(c,0x08); }
		C8(c,0x88); C8(c,0x41); C8(c,(uint8_t)i);
	}
}

static void emit_bf_load(Code *c, Type *ty) {
	emit_bf_unit_load(c, ty->bit_unit);
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
	emit_bf_unit_load(c, ty->bit_unit);
	C8(c,0x48); C8(c,0x21); C8(c,0xF0);
	C8(c,0x48); C8(c,0x09); C8(c,0xD0);
	emit_bf_unit_store(c, ty->bit_unit);
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
static int is_asm_kw(Ctx *ctx);
static char *parse_asm_label(Ctx *ctx);
static void asm_statement(Ctx *ctx, int top);
static int asm_regnum(const char *name);

enum { ST_NONE, ST_TYPEDEF, ST_STATIC, ST_EXTERN };

static int is_typename(Ctx *ctx, Tok *t) {
	switch (t->kind) {
	case T_INT: case T_CHAR: case T_SHORT: case T_LONG: case T_SIGNED: case T_UNSIGNED:
	case T_VOID: case T_BOOL: case T_FLOAT: case T_DOUBLE:
	case T_STRUCT: case T_UNION: case T_ENUM: case T_TYPEDEF:
	case T_STATIC: case T_EXTERN: case T_CONST: case T_VOLATILE: case T_INLINE:
	case T_REGISTER: case T_AUTO: case T_RESTRICT: case T_TYPEOF: case T_ALIGNAS: case T_NORETURN:
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
static Type *parse_typename(Ctx *ctx);

static Type *typeof_operand(Ctx *ctx) {
	next(ctx);
	expect(ctx, T_LP);
	Type *t;
	if (is_typename(ctx, &ctx->tok)) t = parse_typename(ctx);
	else {
		Mark m = mark_code(ctx);
		t = parse_expr(ctx);
		drop_code(ctx, m);
		if (t->bit_width) t = t->origin;
	}
	expect(ctx, T_RP);
	return t;
}

static Type *declspec(Ctx *ctx, int *storage) {
	int nlong = 0, nshort = 0, nint = 0, nchar = 0, nvoid = 0, nbool = 0, nfloat = 0, ndouble = 0;
	int is_signed = 0, is_unsigned = 0;
	Type *user = NULL;
	int attrs = 0;
	Attr da;
	memset(&da, 0, sizeof(da));
	if (storage) *storage = ST_NONE;
	for (;;) {
		int k = ctx->tok.kind;
		attrs |= ctx->tok.attrs;
		attr_merge(&da, ctx->tok.attr);
		if (k == T_TYPEDEF || k == T_STATIC || k == T_EXTERN) {
			if (!storage) error_at(ctx, "storage class specifier not allowed here");
			*storage = k == T_TYPEDEF ? ST_TYPEDEF : k == T_STATIC ? ST_STATIC : ST_EXTERN;
			next(ctx); continue;
		}
		if (is_qualifier(k) || k == T_INLINE || k == T_REGISTER || k == T_AUTO || k == T_NORETURN) { next(ctx); continue; }
		if (k == T_ALIGNAS) {
			next(ctx);
			expect(ctx, T_LP);
			int a = is_typename(ctx, &ctx->tok) ? parse_typename(ctx)->align : (int)const_expr(ctx);
			expect(ctx, T_RP);
			if (a > da.align) da.align = a;
			continue;
		}
		if (k == T_TYPEOF) { user = typeof_operand(ctx); continue; }
		if (k == T_VOID) { nvoid++; next(ctx); continue; }
		if (k == T_BOOL) { nbool++; next(ctx); continue; }
		if ((k == T_FLOAT || k == T_DOUBLE) && ctx->no_float) error_at(ctx, "floating point is disabled by -mgeneral-regs-only");
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
	ctx->decl_attrs = attrs;
	ctx->decl_attr = da;
	if (user) return user;
	if (nvoid) return ty_void;
	if (nbool) return ty_bool;
	if (nfloat) return ty_float;
	if (ndouble) return ty_double;
	if (nchar) return is_unsigned ? ty_uchar : is_signed ? ty_schar : ty_char;
	if (nshort) return is_unsigned ? ty_ushort : ty_short;
	if (nlong > 1) return is_unsigned ? ty_ullong : ty_llong;
	if (nlong) return is_unsigned ? ty_ulong : ty_long;
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

typedef struct {
	char *name;
	Type *ty;
	int width;
	int align;
} Field;

static void packed_unit(Ctx *ctx, Member *m) {
	int pos = 8 * m->off + m->ty->bit_off;
	int n = (pos % 8 + m->ty->bit_width + 7) / 8;
	if (n > 8) error_at(ctx, "bit-field '%s' spans more than 8 bytes", m->name);
	m->off = pos / 8;
	m->ty->bit_off = pos % 8;
	m->ty->bit_unit = n;
}

static void struct_layout(Ctx *ctx, Type *ty, Field *fs, int nf, int packed, int salign) {
	Member head; head.next = NULL;
	Member *cur = &head;
	int is_union = ty->is_union;
	long bits = 0;
	int size = 0, align = 1;
	for (int i = 0; i < nf; i++) {
		Type *mt = fs[i].ty;
		char *name = fs[i].name;
		int a = packed ? 1 : mt->align;
		if (fs[i].align > a) a = fs[i].align;
		if (fs[i].width < -1) {
			int moff = is_union ? 0 : (int)align_up((size_t)((bits + 7) / 8), (size_t)a);
			for (Member *bm = mt->members; bm; bm = bm->next) {
				Member *m = (Member *)calloc(1, sizeof(Member));
				m->name = bm->name; m->ty = bm->ty; m->off = moff + bm->off;
				if (packed && bm->ty->bit_width) { m->ty = (Type *)malloc(sizeof(Type)); *m->ty = *bm->ty; }
				cur->next = m; cur = m;
			}
			if (a > align) align = a;
			if (is_union) { if (mt->size > size) size = mt->size; }
			else bits = (long)(moff + mt->size) * 8;
			continue;
		}
		if (fs[i].width >= 0) {
			int w = fs[i].width;
			int unit = mt->size * 8;
			long pos = is_union ? 0 : bits;
			if (w == 0) {
				if (!is_union) bits = (long)align_up((size_t)bits, (size_t)unit);
			} else {
				if (!packed && pos / unit != (pos + w - 1) / unit) pos = (long)align_up((size_t)pos, (size_t)unit);
				if (name) {
					Member *m = (Member *)calloc(1, sizeof(Member));
					m->name = name;
					if (packed) {
						m->off = (int)(pos / 8);
						m->ty = bitfield_of(mt, w, (int)(pos % 8));
					} else {
						m->off = (int)(pos / unit) * mt->size;
						m->ty = bitfield_of(mt, w, (int)(pos % unit));
					}
					cur->next = m; cur = m;
					if (a > align) align = a;
				}
				if (is_union) { int sz = (w + 7) / 8; if (sz > size) size = sz; }
				else bits = pos + w;
			}
			continue;
		}
		Member *m = (Member *)calloc(1, sizeof(Member));
		m->name = name; m->ty = mt;
		if (a > align) align = a;
		if (is_union) {
			m->off = 0;
			if (mt->size > size) size = mt->size;
		} else {
			int off = (int)align_up((size_t)((bits + 7) / 8), (size_t)a);
			m->off = off;
			bits = (long)(off + mt->size) * 8;
		}
		cur->next = m; cur = m;
	}
	if (!is_union) size = (int)((bits + 7) / 8);
	if (salign > align) align = salign;
	ty->members = head.next;
	ty->align = align;
	ty->size = (int)align_up((size_t)size, (size_t)align);
	if (packed)
		for (Member *m = ty->members; m; m = m->next)
			if (m->ty->bit_width) packed_unit(ctx, m);
	ty->complete = 1;
}

static Type *struct_decl(Ctx *ctx) {
	int is_union = ctx->tok.kind == T_UNION;
	next(ctx);
	int attrs = ctx->tok.attrs;
	Attr sa;
	memset(&sa, 0, sizeof(sa));
	attr_merge(&sa, ctx->tok.attr);
	char *tag = NULL;
	if (ctx->tok.kind == T_IDENT) { tag = ctx->tok.str; next(ctx); attrs |= ctx->tok.attrs; attr_merge(&sa, ctx->tok.attr); }
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
	Field *fs = NULL;
	int nf = 0, capf = 0;
	while (ctx->tok.kind != T_RB) {
		Type *base = declspec(ctx, NULL);
		int balign = ctx->decl_attr.align;
		if (ctx->tok.kind == T_SEMI && base->kind == TY_STRUCT) {
			if (nf == capf) { capf = capf ? capf * 2 : 16; fs = (Field *)realloc(fs, sizeof(Field) * capf); }
			fs[nf].name = NULL; fs[nf].ty = base; fs[nf].width = -2; fs[nf].align = balign; nf++;
			next(ctx);
			continue;
		}
		for (;;) {
			char *name = NULL;
			Type *mt = base;
			int w = -1;
			if (ctx->tok.kind != T_COLON) mt = declarator(ctx, base, &name);
			if (consume(ctx, T_COLON)) {
				w = (int)const_expr(ctx);
				if (!is_integer(mt)) error_at(ctx, "bit-field has invalid type");
				if (w < 0 || w > mt->size * 8) error_at(ctx, "invalid bit-field width");
			}
			if (nf == capf) { capf = capf ? capf * 2 : 16; fs = (Field *)realloc(fs, sizeof(Field) * capf); }
			fs[nf].name = name; fs[nf].ty = mt; fs[nf].width = w;
			fs[nf].align = ctx->tok.attr && ctx->tok.attr->align > balign ? ctx->tok.attr->align : balign;
			nf++;
			if (!consume(ctx, T_COMMA)) break;
		}
		expect(ctx, T_SEMI);
	}
	next(ctx);
	attrs |= ctx->tok.attrs;
	attr_merge(&sa, ctx->tok.attr);
	struct_layout(ctx, ty, fs, nf, (attrs & ATTR_PACKED) != 0, sa.align);
	free(fs);
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
static long long alignof_operand(Ctx *ctx);
static long long ce_cond(Ctx *ctx);

static int tokens_const(Ctx *ctx, TokVec *v) {
	for (int i = 0; i < v->n; i++) {
		Tok *t = &v->v[i];
		int k = t->kind;
		if (k == T_IDENT) {
			Sym *s = lookup(ctx, t->str);
			if (!s || (s->kind != SYM_ENUMC && s->kind != SYM_TYPEDEF)) return 0;
		} else if (k == T_STRUCT || k == T_UNION || k == T_ENUM) {
			if (i + 1 < v->n && v->v[i+1].kind == T_IDENT) i++;
		} else if (k == T_SIZEOF || k == T_ALIGNOF) {
			if (i + 1 < v->n && v->v[i+1].kind == T_LP) {
				int d = 0;
				do {
					i++;
					if (v->v[i].kind == T_LP) d++;
					else if (v->v[i].kind == T_RP) d--;
				} while (d > 0 && i + 1 < v->n);
			} else if (i + 1 < v->n) i++;
		} else if (k == T_ASSIGN || k == T_INC || k == T_DEC || k == T_COMMA || k == T_LB ||
			(k >= T_ADD_ASSIGN && k <= T_SHR_ASSIGN)) return 0;
	}
	return v->n > 0;
}

static long long offsetof_operand(Ctx *ctx) {
	Type *ty = parse_typename(ctx);
	expect(ctx, T_COMMA);
	long long off = 0;
	for (int first = 1;; first = 0) {
		if (first || consume(ctx, T_DOT)) {
			if (ty->kind != TY_STRUCT || !ty->complete) error_at(ctx, "offsetof applied to a non-struct type");
			if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected member name");
			Member *m = find_member(ty, ctx->tok.str);
			if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
			if (m->ty->bit_width) error_at(ctx, "offsetof applied to a bit-field");
			next(ctx);
			off += m->off;
			ty = m->ty;
		} else if (consume(ctx, T_LBRACKET)) {
			if (ty->kind != TY_ARRAY) error_at(ctx, "subscripted value is not an array");
			off += const_expr(ctx) * ty->base->size;
			expect(ctx, T_RBRACKET);
			ty = ty->base;
		} else break;
	}
	return off;
}

static long long builtin_const(Ctx *ctx, char *name) {
	long long v = 0;
	next(ctx);
	expect(ctx, T_LP);
	if (name == id_constant_p) {
		TokVec t = capture_expr(ctx);
		v = tokens_const(ctx, &t);
		free(t.v);
	} else if (name == id_types_compatible_p) {
		Type *a = parse_typename(ctx);
		expect(ctx, T_COMMA);
		v = same_type(a, parse_typename(ctx));
	} else v = offsetof_operand(ctx);
	expect(ctx, T_RP);
	return v;
}

static int is_builtin_const(char *name) {
	return name == id_constant_p || name == id_types_compatible_p || name == id_offsetof;
}

static long long ce_unary(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_MINUS) { next(ctx); return -ce_unary(ctx); }
	if (k == T_PLUS) { next(ctx); return ce_unary(ctx); }
	if (k == T_TILDE) { next(ctx); return ~ce_unary(ctx); }
	if (k == T_NOT) { next(ctx); return !ce_unary(ctx); }
	if (k == T_SIZEOF) { next(ctx); return sizeof_operand(ctx); }
	if (k == T_ALIGNOF) { next(ctx); return alignof_operand(ctx); }
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
		char *name = ctx->tok.str;
		if (is_builtin_const(name)) return builtin_const(ctx, name);
		if (name == id_expect || name == id_choose_expr) {
			next(ctx);
			expect(ctx, T_LP);
			long long v = const_expr(ctx);
			expect(ctx, T_COMMA);
			if (name == id_choose_expr) {
				if (v) { v = const_expr(ctx); expect(ctx, T_COMMA); free(capture_expr(ctx).v); }
				else { free(capture_expr(ctx).v); expect(ctx, T_COMMA); v = const_expr(ctx); }
			} else const_expr(ctx);
			expect(ctx, T_RP);
			return v;
		}
		Sym *s = lookup(ctx, name);
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
	Mark m = mark_code(ctx);
	Type *t = parse_unary(ctx);
	drop_code(ctx, m);
	return type_size_checked(ctx, t);
}

static long long alignof_operand(Ctx *ctx) {
	if (ctx->tok.kind == T_LP && is_typename(ctx, peek(ctx))) {
		next(ctx);
		Type *t = parse_typename(ctx);
		expect(ctx, T_RP);
		return t->align;
	}
	Mark m = mark_code(ctx);
	Type *t = parse_unary(ctx);
	drop_code(ctx, m);
	return t->align;
}

static const int argregs[6] = { RDI, RSI, RDX, RCX, R8, R9 };

enum { CL_INT = 1, CL_SSE = 2 };

static int classify_at(Type *t, int off, int *cls) {
	if (t->kind == TY_STRUCT) {
		for (Member *m = t->members; m; m = m->next)
			if (!classify_at(m->ty, off + m->off, cls)) return 0;
		return 1;
	}
	if (t->kind == TY_ARRAY) {
		for (int i = 0; i < t->len; i++)
			if (!classify_at(t->base, off + i * t->base->size, cls)) return 0;
		return 1;
	}
	if (t->bit_width) { cls[off / 8] |= CL_INT; return 1; }
	if (t->size == 0) return 1;
	if (off % t->size || off / 8 > 1) return 0;
	cls[off / 8] |= is_flonum(t) ? CL_SSE : CL_INT;
	return 1;
}

static int classify(Type *t, int *cls) {
	cls[0] = cls[1] = 0;
	if (t->kind == TY_VOID) return 0;
	if (t->kind != TY_STRUCT) { cls[0] = is_flonum(t) ? CL_SSE : CL_INT; return 1; }
	if (t->size > 16 || !classify_at(t, 0, cls)) return -1;
	int n = (t->size + 7) / 8;
	for (int i = 0; i < n; i++) if (cls[i] & CL_INT) cls[i] = CL_INT;
	return n;
}

static int count_cls(int *cls, int n, int k) {
	int r = 0;
	for (int i = 0; i < n; i++) if (cls[i] == k) r++;
	return r;
}

static void emit_xmm_rm(Code *c, uint8_t pfx, uint8_t op, int x, int base, int disp) {
	C8(c,pfx); enc_rex(c, 0, x, 0, base, 0); C8(c,0x0F); C8(c,op); enc_mem(c, x, base, -1, 1, disp, 1, 0);
}

static void emit_movq_xmm_load(Code *c, int x, int base, int disp) { emit_xmm_rm(c, 0xF3, 0x7E, x, base, disp); }
static void emit_movq_xmm_store(Code *c, int x, int base, int disp) { emit_xmm_rm(c, 0x66, 0xD6, x, base, disp); }

static void emit_copy_to_rbp(Code *c, int src_reg, int off, int size) {
	if (src_reg != RSI) emit_mov_reg_reg(c, RSI, src_reg);
	emit_lea_rbp(c, RDI, off);
	emit_memcpy(c, size);
}

static void emit_extend_ret(Code *c, Type *t) {
	if (t->kind == TY_BOOL) { emit_movzx_eax_al(c); return; }
	if (is_integer(t) && t->size < 8) emit_extend(c, t->size, t->is_unsigned);
}

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

typedef struct {
	const char *name;
	int nargs;
	int regs[3];
	Type **ret;
	int len;
	const char *code;
} MachBuiltin;

static const MachBuiltin mach_builtins[] = {
	{ "__outb", 2, { RDX, RAX }, &ty_void, 1, "\xEE" },
	{ "__outw", 2, { RDX, RAX }, &ty_void, 2, "\x66\xEF" },
	{ "__outl", 2, { RDX, RAX }, &ty_void, 1, "\xEF" },
	{ "__inb", 1, { RDX }, &ty_uchar, 4, "\xEC\x0F\xB6\xC0" },
	{ "__inw", 1, { RDX }, &ty_ushort, 5, "\x66\xED\x0F\xB7\xC0" },
	{ "__inl", 1, { RDX }, &ty_uint, 1, "\xED" },
	{ "__cli", 0, { 0 }, &ty_void, 1, "\xFA" },
	{ "__sti", 0, { 0 }, &ty_void, 1, "\xFB" },
	{ "__hlt", 0, { 0 }, &ty_void, 1, "\xF4" },
	{ "__pause", 0, { 0 }, &ty_void, 2, "\xF3\x90" },
	{ "__lgdt", 1, { RAX }, &ty_void, 3, "\x0F\x01\x10" },
	{ "__lidt", 1, { RAX }, &ty_void, 3, "\x0F\x01\x18" },
	{ "__ltr", 1, { RAX }, &ty_void, 3, "\x0F\x00\xD8" },
	{ "__read_cr0", 0, { 0 }, &ty_ulong, 3, "\x0F\x20\xC0" },
	{ "__read_cr2", 0, { 0 }, &ty_ulong, 3, "\x0F\x20\xD0" },
	{ "__read_cr3", 0, { 0 }, &ty_ulong, 3, "\x0F\x20\xD8" },
	{ "__read_cr4", 0, { 0 }, &ty_ulong, 3, "\x0F\x20\xE0" },
	{ "__write_cr0", 1, { RAX }, &ty_void, 3, "\x0F\x22\xC0" },
	{ "__write_cr2", 1, { RAX }, &ty_void, 3, "\x0F\x22\xD0" },
	{ "__write_cr3", 1, { RAX }, &ty_void, 3, "\x0F\x22\xD8" },
	{ "__write_cr4", 1, { RAX }, &ty_void, 3, "\x0F\x22\xE0" },
	{ "__rdmsr", 1, { RCX }, &ty_ulong, 9, "\x0F\x32\x48\xC1\xE2\x20\x48\x09\xD0" },
	{ "__wrmsr", 2, { RCX, RAX }, &ty_void, 9, "\x48\x89\xC2\x48\xC1\xEA\x20\x0F\x30" },
	{ "__invlpg", 1, { RAX }, &ty_void, 3, "\x0F\x01\x38" },
	{ "__cpuid", 3, { RAX, RCX, RDI }, &ty_void, 15, "\x53\x0F\xA2\x89\x07\x89\x5F\x04\x89\x4F\x08\x89\x57\x0C\x5B" },
	{ "__rdtsc", 0, { 0 }, &ty_ulong, 9, "\x0F\x31\x48\xC1\xE2\x20\x48\x09\xD0" },
	{ NULL, 0, { 0 }, NULL, 0, NULL }
};
static char *mach_ids[sizeof(mach_builtins) / sizeof(mach_builtins[0])];

static Type *parse_builtin_mach(Ctx *ctx, const MachBuiltin *b) {
	Code *c = &ctx->text;
	next(ctx);
	expect(ctx, T_LP);
	int n = 0;
	while (ctx->tok.kind != T_RP) {
		Type *t = rval(ctx, parse_assign(ctx));
		if (!is_integer(t) && t->kind != TY_PTR) error_at(ctx, "%s takes integer or pointer arguments", b->name);
		gen_push(ctx);
		n++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RP);
	if (n != b->nargs) error_at(ctx, "%s takes %d argument%s", b->name, b->nargs, b->nargs == 1 ? "" : "s");
	for (int i = n - 1; i >= 0; i--) gen_pop(ctx, b->regs[i]);
	for (int i = 0; i < b->len; i++) C8(c, (uint8_t)b->code[i]);
	ctx->lv = 0;
	return *b->ret;
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
	C8(c,0xC7); C8(c,0x40); C8(c,0x04); C32(c,(uint32_t)(48 + 16 * ctx->fn->named_sse));
	emit_lea_rbp(c, RCX, 16 + ctx->fn->named_stack);
	C8(c,0x48); C8(c,0x89); C8(c,0x48); C8(c,0x08);
	emit_lea_rbp(c, RCX, ctx->fn->save_off);
	C8(c,0x48); C8(c,0x89); C8(c,0x48); C8(c,0x10);
	ctx->lv = 0;
	return ty_void;
}

static Type *parse_builtin_va_arg(Ctx *ctx) {
	Code *c = &ctx->text;
	int cls[2];
	size_t stack[2];
	int nj = 0;
	next(ctx);
	expect(ctx, T_LP);
	Type *at = rval(ctx, parse_assign(ctx));
	if (!is_ptr(at)) error_at(ctx, "va_arg needs a va_list");
	expect(ctx, T_COMMA);
	Type *t = parse_typename(ctx);
	expect(ctx, T_RP);
	int n = classify(t, cls);
	emit_mov_reg_reg(c, RCX, RAX);
	if (n > 0) {
		int ng = count_cls(cls, n, CL_INT), ns = count_cls(cls, n, CL_SSE);
		if (ng) {
			C8(c,0x8B); C8(c,0x01);
			C8(c,0x3D); C32(c,(uint32_t)(48 - 8 * ng));
			C8(c,0x0F); C8(c,0x87); stack[nj++] = Cpos(c); C32(c,0);
		}
		if (ns) {
			C8(c,0x8B); C8(c,0x41); C8(c,0x04);
			C8(c,0x3D); C32(c,(uint32_t)(176 - 16 * ns));
			C8(c,0x0F); C8(c,0x87); stack[nj++] = Cpos(c); C32(c,0);
		}
		int tmp = t->kind == TY_STRUCT ? alloc_local(ctx, 16, 16) : 0;
		for (int i = 0; i < n; i++) {
			if (!cls[i]) continue;
			int fp = cls[i] == CL_SSE;
			C8(c,0x8B); if (fp) { C8(c,0x41); C8(c,0x04); } else C8(c,0x01);
			C8(c,0x48); C8(c,0x03); C8(c,0x41); C8(c,0x10);
			if (fp) { C8(c,0x83); C8(c,0x41); C8(c,0x04); C8(c,0x10); }
			else { C8(c,0x83); C8(c,0x01); C8(c,0x08); }
			if (tmp) {
				C8(c,0x48); C8(c,0x8B); C8(c,0x10);
				emit_store_rbp(c, RDX, tmp + 8 * i);
			}
		}
		if (tmp) emit_lea_rbp(c, RAX, tmp);
		size_t done = emit_jmp_placeholder(c);
		for (int i = 0; i < nj; i++) patch_jmp_to(&c->b, stack[i], Cpos(c));
		C8(c,0x48); C8(c,0x8B); C8(c,0x41); C8(c,0x08);
		if (t->align > 8) { C8(c,0x48); C8(c,0x83); C8(c,0xC0); C8(c,0x0F); C8(c,0x48); C8(c,0x83); C8(c,0xE0); C8(c,0xF0); }
		C8(c,0x48); C8(c,0x8D); C8(c,0x90); C32(c,(uint32_t)align_up((size_t)t->size, 8));
		C8(c,0x48); C8(c,0x89); C8(c,0x51); C8(c,0x08);
		patch_jmp_to(&c->b, done, Cpos(c));
	} else {
		C8(c,0x48); C8(c,0x8B); C8(c,0x41); C8(c,0x08);
		if (t->align > 8) { C8(c,0x48); C8(c,0x83); C8(c,0xC0); C8(c,0x0F); C8(c,0x48); C8(c,0x83); C8(c,0xE0); C8(c,0xF0); }
		C8(c,0x48); C8(c,0x8D); C8(c,0x90); C32(c,(uint32_t)align_up((size_t)t->size, 8));
		C8(c,0x48); C8(c,0x89); C8(c,0x51); C8(c,0x08);
	}
	ctx->lv = 1;
	return t;
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
	int nargs = 0, cap = 8;
	Type **ats = (Type **)malloc(sizeof(Type *) * cap);
	Param *p = fty->params;
	while (ctx->tok.kind != T_RP) {
		Type *at = rval(ctx, parse_assign(ctx));
		if (p) {
			if (is_scalar(p->ty) && is_scalar(at)) { gen_cast(ctx, at, p->ty); at = p->ty; }
			p = p->next;
		} else if (at->kind == TY_FLOAT) { gen_cast(ctx, at, ty_double); at = ty_double; }
		else if (is_integer(at)) at = int_promote(at);
		if (at->origin) at = canon(at);
		gen_push(ctx);
		if (nargs == cap) ats = (Type **)realloc(ats, sizeof(Type *) * (cap *= 2));
		ats[nargs++] = at;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RP);
	if (!fty->oldstyle && !fty->variadic && nargs != fty->nparams)
		error_at(ctx, "wrong number of arguments (expected %d, got %d)", fty->nparams, nargs);
	if (fty->variadic && nargs < fty->nparams)
		error_at(ctx, "too few arguments (expected at least %d, got %d)", fty->nparams, nargs);

	int rcls[2], cls[2];
	int rn = classify(ret, rcls);
	int sret = ret->kind == TY_STRUCT && rn < 0;
	int *where = (int *)malloc(sizeof(int) * (nargs + 1));
	int *tmp = (int *)malloc(sizeof(int) * (nargs + 1));
	int gp = sret, sse = 0, stk = 0;
	for (int a = 0; a < nargs; a++) {
		int n = classify(ats[a], cls);
		int ng = n > 0 ? count_cls(cls, n, CL_INT) : 0, ns = n > 0 ? count_cls(cls, n, CL_SSE) : 0;
		tmp[a] = 0;
		if (n >= 0 && gp + ng <= 6 && sse + ns <= 8) {
			where[a] = -1;
			gp += ng;
			sse += ns;
			if (ats[a]->kind == TY_STRUCT && n > 0) tmp[a] = alloc_local(ctx, 16, 16);
		} else {
			if (ats[a]->align > 8) stk = (int)align_up((size_t)stk, 16);
			where[a] = stk;
			stk += (int)align_up((size_t)ats[a]->size, 8);
		}
	}
	int area = stk + ((ctx->sp * 8 + stk) % 16 ? 8 : 0);
	int extra = area / 8;
	int temp = 0;
	if (sret) temp = alloc_local(ctx, ret->size, ret->align);
	if (area) emit_sub_rsp(c, area);
	for (int a = 0; a < nargs; a++) {
		int disp = area + 8 * (nargs - 1 - a);
		if (where[a] >= 0) {
			if (ats[a]->kind == TY_STRUCT) {
				emit_load_rsp(c, RSI, disp);
				C8(c,0x48); C8(c,0x8D); C8(c,0xBC); C8(c,0x24); C32(c,(uint32_t)where[a]);
				emit_memcpy(c, ats[a]->size);
			} else {
				emit_load_rsp(c, RAX, disp);
				emit_store_rsp_rax(c, where[a]);
			}
		} else if (tmp[a]) {
			emit_load_rsp(c, RSI, disp);
			emit_copy_to_rbp(c, RSI, tmp[a], ats[a]->size);
		}
	}
	gp = 0;
	sse = 0;
	if (sret) emit_lea_rbp(c, argregs[gp++], temp);
	for (int a = 0; a < nargs; a++) {
		if (where[a] >= 0) continue;
		int disp = area + 8 * (nargs - 1 - a);
		int n = classify(ats[a], cls);
		if (ats[a]->kind != TY_STRUCT) {
			if (cls[0] == CL_SSE) emit_movq_xmm_load(c, sse++, RSP, disp);
			else emit_load_rsp(c, argregs[gp++], disp);
			continue;
		}
		for (int i = 0; i < n; i++) {
			if (cls[i] == CL_INT) emit_load_rbp(c, argregs[gp++], tmp[a] + 8 * i);
			else if (cls[i] == CL_SSE) emit_movq_xmm_load(c, sse++, RBP, tmp[a] + 8 * i);
		}
	}
	if (!direct) emit_load_rsp(c, R11, area + 8 * nargs);
	if (sse) emit_mov_rax_imm(c, sse);
	else emit_xor_eax_eax(c);
	if (direct) emit_call_sym(ctx, direct);
	else emit_call_r11(c);
	int drop = extra + nargs + (direct ? 0 : 1);
	if (drop) emit_add_rsp(c, drop * 8);
	ctx->sp -= nargs + (direct ? 0 : 1);
	if (ret->kind == TY_STRUCT && rn >= 0) {
		int t = alloc_local(ctx, 16, 16), g = 0, x = 0;
		static const int retregs[2] = { RAX, RDX };
		for (int i = 0; i < rn; i++) {
			if (rcls[i] == CL_INT) emit_store_rbp(c, retregs[g++], t + 8 * i);
			else if (rcls[i] == CL_SSE) emit_movq_xmm_store(c, x++, RBP, t + 8 * i);
		}
		emit_lea_rbp(c, RAX, t);
	} else if (ret->kind == TY_DOUBLE) emit_movq_rax_xmm0(c);
	else if (ret->kind == TY_FLOAT) emit_movd_eax_xmm0(c);
	else emit_extend_ret(c, canon(ret));
	free(ats); free(where); free(tmp);
	ctx->lv = 0;
	return ret;
}

static int is_expr_stmt(Ctx *ctx) {
	switch (ctx->tok.kind) {
	case T_LB: case T_SEMI: case T_RETURN: case T_IF: case T_WHILE: case T_DO: case T_FOR:
	case T_SWITCH: case T_CASE: case T_DEFAULT: case T_GOTO: case T_BREAK: case T_CONTINUE:
		return 0;
	case T_IDENT:
		if (ctx->tok.str == id_static_assert || peek(ctx)->kind == T_COLON || is_asm_kw(ctx)) return 0;
		return !is_typename(ctx, &ctx->tok);
	default:
		return !is_typename(ctx, &ctx->tok);
	}
}

static void define_label(Ctx *ctx);

static Type *parse_stmt_expr(Ctx *ctx) {
	if (!ctx->fn) error_at(ctx, "statement expression outside a function");
	next(ctx);
	next(ctx);
	enter_scope(ctx);
	Type *ty = ty_void;
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in statement expression");
		ty = ty_void;
		if (ctx->tok.kind == T_IDENT && peek(ctx)->kind == T_COLON) { define_label(ctx); continue; }
		if (is_expr_stmt(ctx)) {
			Type *t = parse_expr(ctx);
			expect(ctx, T_SEMI);
			if (ctx->tok.kind == T_RB) ty = rval(ctx, t);
			ctx->lv = 0;
		} else parse_stmt(ctx);
	}
	leave_scope(ctx);
	next(ctx);
	expect(ctx, T_RP);
	ctx->lv = 0;
	return ty;
}

static Type *parse_builtin_expect(Ctx *ctx) {
	next(ctx);
	expect(ctx, T_LP);
	Type *t = rval(ctx, parse_assign(ctx));
	if (!is_scalar(t)) error_at(ctx, "__builtin_expect takes a scalar argument");
	gen_cast(ctx, t, ty_long);
	expect(ctx, T_COMMA);
	Mark m = mark_code(ctx);
	parse_assign(ctx);
	drop_code(ctx, m);
	expect(ctx, T_RP);
	return ty_long;
}

static Type *parse_builtin_choose(Ctx *ctx) {
	next(ctx);
	expect(ctx, T_LP);
	long long v = const_expr(ctx);
	expect(ctx, T_COMMA);
	Type *res = NULL;
	int lv = 0;
	Sym *lea_sym = NULL;
	size_t lea_end = 0;
	for (int i = 0; i < 2; i++) {
		if (i) expect(ctx, T_COMMA);
		if ((i == 0) == (v != 0)) {
			res = parse_assign(ctx);
			lv = ctx->lv;
			lea_sym = ctx->fn_lea_sym;
			lea_end = ctx->fn_lea_end;
		} else {
			Mark m = mark_code(ctx);
			parse_assign(ctx);
			drop_code(ctx, m);
		}
	}
	expect(ctx, T_RP);
	ctx->lv = lv;
	if (lea_sym && lea_end == Cpos(&ctx->text)) { ctx->fn_lea_sym = lea_sym; ctx->fn_lea_end = lea_end; }
	return res;
}

static Type *parse_generic(Ctx *ctx) {
	next(ctx);
	expect(ctx, T_LP);
	Mark m = mark_code(ctx);
	Type *ct = parse_assign(ctx);
	drop_code(ctx, m);
	if (ct->kind == TY_ARRAY) ct = ptr_to(ct->base);
	else if (ct->kind == TY_FUNC) ct = ptr_to(ct);
	expect(ctx, T_COMMA);
	Type *res = NULL;
	int lv = 0, has_def = 0;
	Sym *lea_sym = NULL;
	size_t lea_end = 0;
	TokVec def;
	memset(&def, 0, sizeof(def));
	for (;;) {
		if (consume(ctx, T_DEFAULT)) {
			expect(ctx, T_COLON);
			if (has_def) error_at(ctx, "duplicate default association in _Generic");
			has_def = 1;
			def = capture_expr(ctx);
		} else {
			Type *t = parse_typename(ctx);
			expect(ctx, T_COLON);
			if (!res && same_type(ct, t)) {
				res = parse_assign(ctx);
				lv = ctx->lv;
				lea_sym = ctx->fn_lea_sym;
				lea_end = ctx->fn_lea_end;
			} else {
				m = mark_code(ctx);
				parse_assign(ctx);
				drop_code(ctx, m);
			}
		}
		if (!consume(ctx, T_COMMA)) break;
	}
	if (!res) {
		if (!has_def) error_at(ctx, "_Generic selector matches no association");
		replay(ctx, &def);
		res = parse_assign(ctx);
		lv = ctx->lv;
		lea_sym = ctx->fn_lea_sym;
		lea_end = ctx->fn_lea_end;
	}
	free(def.v);
	expect(ctx, T_RP);
	ctx->lv = lv;
	if (lea_sym && lea_end == Cpos(&ctx->text)) { ctx->fn_lea_sym = lea_sym; ctx->fn_lea_end = lea_end; }
	return res;
}

static Type *parse_primary(Ctx *ctx) {
	Code *c = &ctx->text;
	ctx->lv = 0;
	if (ctx->tok.kind == T_GENERIC) return parse_generic(ctx);
	if (ctx->tok.kind == T_NUM) {
		Type *ty = ctx->tok.is_long > 1 ? (ctx->tok.is_unsigned ? ty_ullong : ty_llong)
			: ctx->tok.is_long ? (ctx->tok.is_unsigned ? ty_ulong : ty_long)
			: (ctx->tok.is_unsigned ? ty_uint : ty_int);
		emit_mov_rax_imm(c, ctx->tok.val);
		next(ctx);
		return ty;
	}
	if (ctx->tok.kind == T_FNUM) {
		if (ctx->no_float) error_at(ctx, "floating point is disabled by -mgeneral-regs-only");
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
	if (ctx->tok.kind == T_LP && peek(ctx)->kind == T_LB) return parse_stmt_expr(ctx);
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
		if (name == id_va_arg) return parse_builtin_va_arg(ctx);
		if (is_builtin_const(name)) {
			emit_mov_rax_imm(c, builtin_const(ctx, name));
			return name == id_offsetof ? ty_ulong : ty_int;
		}
		if (name == id_expect) return parse_builtin_expect(ctx);
		if (name == id_choose_expr) return parse_builtin_choose(ctx);
		if (name == id_unreachable || name == id_trap) {
			next(ctx);
			expect(ctx, T_LP);
			expect(ctx, T_RP);
			C8(c,0x0F); C8(c,0x0B);
			return ty_void;
		}
		if (name[0] == '_' && name[1] == '_')
			for (int i = 0; mach_ids[i]; i++)
				if (name == mach_ids[i]) return parse_builtin_mach(ctx, &mach_builtins[i]);
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
		ctx->lv_end = Cpos(c);
		ctx->lv_sym = s->is_local ? NULL : s->global ? s->global : s;
		ctx->lv_off = s->stack_off;
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
	if (k == T_AND && peek(ctx)->kind == T_IDENT) {
		if (!ctx->fn) error_at(ctx, "label address outside a function");
		next(ctx);
		Label *l = find_label(ctx, ctx->tok.str);
		next(ctx);
		C8(c,0x48); C8(c,0x8D); C8(c,0x05);
		size_t ph = Cpos(c);
		C32(c,0);
		if (l->pos >= 0) patch_jmp_to(&c->b, ph, (size_t)l->pos);
		else patches_push(&l->patches, ph);
		ctx->lv = 0;
		return ptr_to(ty_void);
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
	if (k == T_SIZEOF || k == T_ALIGNOF) {
		next(ctx);
		long long n = k == T_SIZEOF ? sizeof_operand(ctx) : alignof_operand(ctx);
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
	if (t->kind == T_AND && ctx->fn && peek(ctx)->kind == T_IDENT) {
		next(ctx);
		Label *l = find_label(ctx, ctx->tok.str);
		next(ctx);
		data_write(ctx, off, 0, 8);
		if (l->pos < 0) patches_push(&l->abs, (size_t)ctx->nrelocs);
		add_reloc(ctx, R_ABS64, (size_t)off, NULL, ctx->cur_text, l->pos);
		return 1;
	}
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
		buf_zero_to(&ctx->data, (size_t)(off + ty->bit_unit));
		uint64_t cur = 0;
		for (int i = 0; i < ty->bit_unit; i++) cur |= (uint64_t)ctx->data.data[off + i] << (8 * i);
		cur = (cur & ~(mask << ty->bit_off)) | v;
		data_write(ctx, off, (long long)cur, ty->bit_unit);
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

static void init_repeat(Ctx *ctx, long off, int esz, int n, int global, int nrel) {
	if (!global) {
		emit_lea_init(ctx, RSI, (int)off);
		emit_lea_init(ctx, RDI, (int)(off + esz));
		emit_memcpy(&ctx->text, n * esz);
		return;
	}
	buf_zero_to(&ctx->data, (size_t)(off + (long)(n + 1) * esz));
	for (int k = 1; k <= n; k++) memcpy(ctx->data.data + off + (long)k * esz, ctx->data.data + off, esz);
	int end = ctx->nrelocs;
	for (int r = nrel; r < end; r++) {
		Reloc x = ctx->relocs[r];
		if (x.kind != R_ABS64 || x.in != ctx->cur_data || (long)x.pos < off || (long)x.pos >= off + esz) continue;
		for (int k = 1; k <= n; k++) add_reloc(ctx, x.kind, x.pos + (size_t)k * esz, x.sym, x.sect, x.addend);
	}
}

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
			int chained = 0, hi = -1;
			if (ctx->tok.kind == T_LBRACKET) {
				if (!braced) break;
				next(ctx);
				i = (int)const_expr(ctx);
				if (consume(ctx, T_ELLIPSIS)) {
					hi = (int)const_expr(ctx);
					if (hi < i) error_at(ctx, "empty index range in initializer");
				}
				expect(ctx, T_RBRACKET);
				chained = ctx->tok.kind == T_DOT || ctx->tok.kind == T_LBRACKET;
				if (!chained) skip_designator_eq(ctx);
			}
			if (ty->len >= 0 && i >= ty->len) {
				if (braced) error_at(ctx, "excess elements in array initializer");
				break;
			}
			if (ty->len >= 0 && hi >= ty->len) error_at(ctx, "array index range in initializer exceeds array bounds");
			if (hi > i && chained) {
				TokVec v = capture_expr(ctx);
				for (int j = i; j <= hi; j++) {
					replay(ctx, &v);
					init_designated(ctx, ty->base, off + (long)j * esz, global);
				}
				free(v.v);
				i = hi;
			} else {
				int nrel = ctx->nrelocs;
				if (chained) init_designated(ctx, ty->base, off + (long)i * esz, global);
				else init_value(ctx, ty->base, off + (long)i * esz, global, 0);
				if (hi > i) {
					init_repeat(ctx, off + (long)i * esz, esz, hi - i, global, nrel);
					i = hi;
				}
			}
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

static Type *aligned_type(Type *ty, int align) {
	if (!align || align == ty->align) return ty;
	Type *t = (Type *)malloc(sizeof(Type));
	*t = *ty;
	t->ptr = NULL;
	t->align = align;
	t->origin = ty;
	return t;
}

static int weak_here(Ctx *ctx, Sym *g, int attrs) {
	return (attrs & ATTR_WEAK) || (g->weak && g->weak_unit == ctx->unit);
}

static void decl_attrs(Ctx *ctx, Sym *g, int attrs, Attr *a) {
	if (attrs & ATTR_WEAK) { g->weak = 1; g->weak_unit = ctx->unit; }
	if (a->section) g->section = a->section;
	if (a->align > g->align) g->align = a->align;
	if (a->alias) {
		if (g->defined) error_at(ctx, "redefinition of '%s'", g->name);
		g->alias_name = a->alias;
		g->defined = 1;
	}
}

static Sym *discard_sym(char *name, SymKind kind) {
	Sym *d = (Sym *)calloc(1, sizeof(Sym));
	d->name = name; d->kind = kind; d->is_static = 1;
	return d;
}

static int var_align(Sym *g, Type *ty, int min) {
	int a = ty->align < min ? min : ty->align;
	return g->align > a ? g->align : a;
}

static void define_global_var(Ctx *ctx, Sym *g, Type *ty, int has_init) {
	int sect = SEC_DATA;
	if (g->section && (has_init || strcmp(g->section, ".bss") != 0))
		sect = section_index(ctx, g->section, strncmp(g->section, ".rodata", 7) == 0 ? SK_RO : SK_DATA);
	if (has_init || sect != SEC_DATA) {
		if (g->defined && g->sect != SEC_BSS) error_at(ctx, "redefinition of '%s'", g->name);
		Buf saved = ctx->data;
		if (sect != SEC_DATA) { ctx->data = *sec_buf(ctx, sect); ctx->cur_data = sect; }
		int a = var_align(g, ty, sect == SEC_DATA ? 8 : 1);
		raise_align(ctx, sect, a);
		buf_zero_to(&ctx->data, align_up(ctx->data.len, (size_t)a));
		long off = (long)ctx->data.len;
		if (has_init) ty = global_init(ctx, ty, off);
		g->tentative = 0;
		g->ty = ty;
		g->sect = sect;
		g->off = off;
		g->size = ty->size;
		g->defined = 1;
		buf_zero_to(&ctx->data, (size_t)(off + ty->size));
		if (sect != SEC_DATA) { *sec_buf(ctx, sect) = ctx->data; ctx->data = saved; ctx->cur_data = SEC_DATA; }
		return;
	}
	if (g->defined) return;
	if (ty->kind == TY_ARRAY && ty->len < 0) return;
	int a = var_align(g, ty, 8);
	raise_align(ctx, SEC_BSS, a);
	ctx->bss_size = (long)align_up((size_t)ctx->bss_size, (size_t)a);
	g->sect = SEC_BSS;
	g->off = ctx->bss_size;
	g->size = ty->size;
	g->defined = 1;
	g->tentative = !g->is_static;
	ctx->bss_size += ty->size;
}

static void parse_block(Ctx *ctx);

static void define_label(Ctx *ctx) {
	Code *c = &ctx->text;
	Label *l = find_label(ctx, ctx->tok.str);
	if (l->pos >= 0) error_at(ctx, "duplicate label '%s'", ctx->tok.str);
	next(ctx);
	next(ctx);
	l->pos = (long)Cpos(c);
	for (int i = 0; i < l->patches.n; i++)
		patch_jmp_to(&c->b, l->patches.v[i], (size_t)l->pos);
	for (int i = 0; i < l->abs.n; i++)
		ctx->relocs[l->abs.v[i]].addend = l->pos;
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
	int battrs = ctx->decl_attrs;
	Attr battr = ctx->decl_attr;
	if (ctx->tok.kind == T_SEMI) { next(ctx); return; }
	for (;;) {
		char *name = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		char *label = parse_asm_label(ctx);
		int attrs = battrs | ctx->tok.attrs;
		Attr da = battr;
		attr_merge(&da, ctx->tok.attr);
		if (da.alias || (da.section && storage != ST_STATIC))
			error_at(ctx, "attribute '%s' is not allowed on a local declaration", da.alias ? "alias" : "section");
		if (storage == ST_TYPEDEF) {
			push_sym(ctx, name, SYM_TYPEDEF, aligned_type(ty, da.align));
		} else if (ty->kind == TY_FUNC || storage == ST_EXTERN) {
			int kind = ty->kind == TY_FUNC ? SYM_FUNC : SYM_VAR;
			Sym *g = get_global(ctx, label ? label : name, kind, ty, 0);
			if (label) push_sym(ctx, name, kind, ty)->global = g;
			else bind_global(ctx, g);
			decl_attrs(ctx, g, attrs, &da);
		} else if (storage == ST_STATIC) {
			Sym *g = (Sym *)calloc(1, sizeof(Sym));
			g->name = name; g->kind = SYM_VAR; g->ty = ty; g->is_static = 1;
			g->align = da.align;
			g->section = da.section;
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
			if (da.align > ty->align) ty = aligned_type(ty, da.align);
			if (ty->align > 16) error_at(ctx, "local variable '%s' is aligned to more than 16 bytes", name);
			Sym *s = push_sym(ctx, name, SYM_VAR, ty);
			s->is_local = 1;
			if (label) {
				int r = asm_regnum(label);
				if (r < 0 || r == RSP || r == RBP) error_at(ctx, "invalid register '%s' for '%s'", label, name);
				s->asm_reg = r + 1;
			}
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
	if (is_asm_kw(ctx)) { asm_statement(ctx, 0); return; }

	if (k == T_RETURN) {
		next(ctx);
		if (ctx->tok.kind != T_SEMI) {
			Type *t = rval(ctx, parse_expr(ctx));
			Type *rt = ctx->fn->ty->base;
			int cls[2];
			int n = classify(rt, cls);
			if (rt->kind == TY_STRUCT && n < 0) {
				emit_load_rbp(c, RDI, ctx->fn->ret_off);
				gen_struct_copy_to_rdi(ctx, rt->size);
			} else if (rt->kind == TY_STRUCT) {
				int tmp = alloc_local(ctx, 16, 16), g = 0, x = 0;
				static const int retregs[2] = { RAX, RDX };
				emit_copy_to_rbp(c, RAX, tmp, rt->size);
				for (int i = 0; i < n; i++) {
					if (cls[i] == CL_SSE) emit_movq_xmm_load(c, x++, RBP, tmp + 8 * i);
					else if (cls[i] == CL_INT) emit_load_rbp(c, retregs[g++], tmp + 8 * i);
				}
			} else if (rt->kind != TY_VOID) {
				gen_cast(ctx, t, rt);
				if (rt->kind == TY_DOUBLE) emit_movq_xmm0_rax(c);
				else if (rt->kind == TY_FLOAT) { C8(c,0x66); C8(c,0x0F); C8(c,0x6E); C8(c,0xC0); }
			}
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
			if (lc->case_hi[i] != lc->case_vals[i]) {
				emit_mov_reg_reg(c, RCX, RAX);
				C8(c,0x48); C8(c,0xBA); C64(c, (uint64_t)lc->case_vals[i]);
				C8(c,0x48); C8(c,0x29); C8(c,0xD1);
				C8(c,0x48); C8(c,0xBA); C64(c, (uint64_t)lc->case_hi[i] - (uint64_t)lc->case_vals[i]);
				C8(c,0x48); C8(c,0x39); C8(c,0xD1);
				C8(c,0x0F); C8(c,0x86); size_t ph = Cpos(c); C32(c,0);
				patch_jmp_to(&c->b, ph, lc->case_pos[i]);
				continue;
			}
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
		long long hi = v;
		if (consume(ctx, T_ELLIPSIS)) hi = const_cast_val(const_expr(ctx), lc->sw_type);
		expect(ctx, T_COLON);
		if (lc->ncase == lc->capcase) {
			lc->capcase = lc->capcase ? lc->capcase * 2 : 16;
			lc->case_vals = (long long *)realloc(lc->case_vals, sizeof(long long) * lc->capcase);
			lc->case_hi = (long long *)realloc(lc->case_hi, sizeof(long long) * lc->capcase);
			lc->case_pos = (size_t *)realloc(lc->case_pos, sizeof(size_t) * lc->capcase);
		}
		if (lc->sw_type->is_unsigned ? (unsigned long long)hi < (unsigned long long)v : hi < v)
			warn_at(ctx, "empty range specified");
		else {
			lc->case_vals[lc->ncase] = v;
			lc->case_hi[lc->ncase] = hi;
			lc->case_pos[lc->ncase] = Cpos(c);
			lc->ncase++;
		}
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
		if (consume(ctx, T_MUL)) {
			Type *t = rval(ctx, parse_expr(ctx));
			if (!is_ptr(t)) error_at(ctx, "computed goto needs a pointer");
			expect(ctx, T_SEMI);
			C8(c,0xFF); C8(c,0xE0);
			return;
		}
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected label name");
		Label *l = find_label(ctx, ctx->tok.str);
		next(ctx);
		expect(ctx, T_SEMI);
		if (l->pos >= 0) emit_jmp_to(c, (size_t)l->pos);
		else patches_push(&l->patches, emit_jmp_placeholder(c));
		return;
	}

	if (k == T_IDENT && peek(ctx)->kind == T_COLON) {
		define_label(ctx);
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

static const int irqregs[9] = { RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11 };

static void emit_interrupt_entry(Ctx *ctx, Type *fty) {
	Code *c = &ctx->text;
	Param *p = fty->params;
	int err = fty->nparams == 2;
	if (!ctx->no_float) error_at(ctx, "interrupt handlers need -mgeneral-regs-only");
	if (fty->base->kind != TY_VOID || fty->variadic || fty->nparams < 1 || fty->nparams > 2 || p->ty->kind != TY_PTR
		|| (err && (!is_integer(p->next->ty) || p->next->ty->size != 8)))
		error_at(ctx, "interrupt handler must be void f(frame *) or void f(frame *, unsigned long)");
	C8(c,0xFC);
	for (int i = 0; i < 9; i++) emit_push(c, irqregs[i]);
	if (err) { C8(c,0x48); C8(c,0x83); C8(c,0xEC); C8(c,0x08); }
	C8(c,0x48); C8(c,0x8D); C8(c,0x7C); C8(c,0x24); C8(c,(uint8_t)(err ? 0x58 : 0x48));
	if (err) { C8(c,0x48); C8(c,0x8B); C8(c,0x74); C8(c,0x24); C8(c,0x50); }
	C8(c,0xE8);
	size_t call = Cpos(c);
	C32(c,0);
	if (err) { C8(c,0x48); C8(c,0x83); C8(c,0xC4); C8(c,0x08); }
	for (int i = 8; i >= 0; i--) emit_pop(c, irqregs[i]);
	if (err) { C8(c,0x48); C8(c,0x83); C8(c,0xC4); C8(c,0x08); }
	C8(c,0x48); C8(c,0xCF);
	buf_patch32_at(&c->b, call, (uint32_t)(Cpos(c) - call - 4));
}

static void parse_func_body(Ctx *ctx, Sym *fs, Type *fty) {
	Code *c = &ctx->text;
	Func f;
	memset(&f, 0, sizeof(f));
	f.sym = fs;
	f.ty = fty;
	ctx->fn = &f;
	ctx->sp = 0;
	if (fs->align > 1) {
		while (Cpos(c) % fs->align) C8(c,0xCC);
		raise_align(ctx, ctx->cur_text, fs->align);
	}
	fs->off = (long)Cpos(c);
	fs->sect = ctx->cur_text;
	fs->defined = 1;
	if (fs->naked) {
		if (fs->interrupt) error_at(ctx, "a function cannot be both naked and an interrupt handler");
		enter_scope(ctx);
		expect(ctx, T_LB);
		while (ctx->tok.kind != T_RB) {
			if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in function body");
			if (!is_asm_kw(ctx)) error_at(ctx, "a naked function may contain only asm statements");
			parse_stmt(ctx);
		}
		next(ctx);
		leave_scope(ctx);
		fs->size = (long)Cpos(c) - fs->off;
		ctx->fn = NULL;
		return;
	}
	if (fs->interrupt) emit_interrupt_entry(ctx, fty);

	emit_push_rbp(c);
	emit_mov_rbp_rsp(c);
	C8(c,0x48); C8(c,0x81); C8(c,0xEC);
	f.frame_patch = Cpos(c);
	C32(c, 0);

	enter_scope(ctx);
	int cls[2];
	int sret = fty->base->kind == TY_STRUCT && classify(fty->base, cls) < 0;
	int gp = 0, sse = 0, stk = 16;
	if (fty->variadic) {
		f.save_off = alloc_local(ctx, 176, 16);
		for (int i = 0; i < 6; i++) emit_store_rbp(c, argregs[i], f.save_off + 8 * i);
		if (!ctx->no_float) {
			C8(c,0x84); C8(c,0xC0);
			size_t skip = jfwd(c, 0x74);
			for (int i = 0; i < 8; i++) emit_movq_xmm_store(c, i, RBP, f.save_off + 48 + 16 * i);
			jland(c, skip);
		}
	}
	if (sret) {
		f.ret_off = fty->variadic ? f.save_off : alloc_local(ctx, 8, 8);
		if (!fty->variadic) emit_store_rbp(c, RDI, f.ret_off);
		gp = 1;
	}
	for (Param *p = fty->params; p; p = p->next) {
		int n = classify(p->ty, cls);
		int ng = n > 0 ? count_cls(cls, n, CL_INT) : 0, ns = n > 0 ? count_cls(cls, n, CL_SSE) : 0;
		int off;
		if (n >= 0 && gp + ng <= 6 && sse + ns <= 8) {
			int a = p->ty->align > 8 ? p->ty->align : 8;
			off = alloc_local(ctx, n > 0 ? 8 * n : 1, a);
			for (int i = 0; i < n; i++) {
				if (cls[i] == CL_INT) emit_store_rbp(c, argregs[gp++], off + 8 * i);
				else if (cls[i] == CL_SSE) emit_movq_xmm_store(c, sse++, RBP, off + 8 * i);
			}
		} else {
			if (p->ty->align > 8) stk = (int)align_up((size_t)stk, 16);
			off = stk;
			stk += (int)align_up((size_t)p->ty->size, 8);
		}
		if (p->name) {
			Sym *s = push_sym(ctx, p->name, SYM_VAR, p->ty);
			s->is_local = 1;
			s->stack_off = off;
		}
	}
	f.named_gp = gp;
	f.named_sse = sse;
	f.named_stack = stk - 16;

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

static void parse_func_in(Ctx *ctx, Sym *g, Type *ty) {
	int sect = g->section ? section_index(ctx, g->section, SK_CODE) : SEC_TEXT;
	if (sect == SEC_TEXT) { parse_func_body(ctx, g, ty); return; }
	ctx->text_home = ctx->text.b;
	ctx->text.b = ctx->secs[sect - SEC_NAMED].b;
	ctx->cur_text = sect;
	parse_func_body(ctx, g, ty);
	ctx->secs[sect - SEC_NAMED].b = ctx->text.b;
	ctx->text.b = ctx->text_home;
	ctx->cur_text = SEC_TEXT;
}

static void parse_toplevel(Ctx *ctx) {
	int storage;
	if (parse_static_assert(ctx)) return;
	if (is_asm_kw(ctx)) { asm_statement(ctx, 1); return; }
	Type *base = declspec(ctx, &storage);
	int attrs = ctx->decl_attrs;
	Attr battr = ctx->decl_attr;
	if (consume(ctx, T_SEMI)) return;
	for (;;) {
		char *name = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		char *label = parse_asm_label(ctx);
		attrs |= ctx->tok.attrs;
		Attr da = battr;
		attr_merge(&da, ctx->tok.attr);
		if ((attrs & ATTR_INTERRUPT) && (ty->kind != TY_FUNC || storage == ST_TYPEDEF)) error_at(ctx, "interrupt attribute applies only to functions");
		if (storage == ST_TYPEDEF) {
			push_sym(ctx, name, SYM_TYPEDEF, aligned_type(ty, da.align));
		} else if (ty->kind == TY_FUNC) {
			Sym *g = get_global(ctx, label ? label : name, SYM_FUNC, ty, storage == ST_STATIC);
			if (label) push_sym(ctx, name, SYM_FUNC, ty)->global = g;
			else bind_global(ctx, g);
			decl_attrs(ctx, g, attrs, &da);
			if (attrs & ATTR_INTERRUPT) g->interrupt = 1;
			if (attrs & ATTR_NAKED) g->naked = 1;
			if (ctx->tok.kind == T_LB) {
				int weak = weak_here(ctx, g, attrs);
				if (g->defined) {
					if (ctx->in_libc || (weak && !g->alias_name)) {
						Sym *d = discard_sym(name, SYM_FUNC);
						d->interrupt = (attrs & ATTR_INTERRUPT) != 0;
						d->naked = (attrs & ATTR_NAKED) != 0;
						d->align = g->align;
						d->section = g->section;
						g = d;
					} else if (g->weak_def && !g->alias_name) g->weak = 0;
					else error_at(ctx, "redefinition of '%s'", name);
				}
				g->weak_def = weak;
				g->ty = ty;
				parse_func_in(ctx, g, ty);
				return;
			}
		} else {
			Sym *g = get_global(ctx, label ? label : name, SYM_VAR, ty, storage == ST_STATIC);
			if (label) push_sym(ctx, name, SYM_VAR, ty)->global = g;
			else bind_global(ctx, g);
			decl_attrs(ctx, g, attrs, &da);
			int has_init = consume(ctx, T_ASSIGN);
			int weak = weak_here(ctx, g, attrs);
			if (has_init && g->defined && (g->sect != SEC_BSS || g->alias_name)) {
				if (weak && !g->alias_name) {
					Sym *d = discard_sym(name, SYM_VAR);
					d->align = g->align;
					d->section = g->section;
					d->ty = g->ty;
					g = d;
				} else if (g->weak_def && !g->alias_name) g->weak = g->defined = 0;
			}
			if (storage != ST_EXTERN || has_init) {
				define_global_var(ctx, g, g->ty, has_init);
				if (has_init) g->weak_def = weak;
			}
		}
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_SEMI);
}

static void resolve_aliases(Ctx *ctx, const char *file) {
	for (Sym *g = ctx->globals; g; g = g->gnext) {
		if (!g->alias_name || g->alias) continue;
		Sym *t = lookup_file_scope(ctx, g->alias_name);
		if (t && t->global) t = t->global;
		if (!t || t == g || (t->kind != SYM_FUNC && t->kind != SYM_VAR)) {
			fprintf(stderr, "%s: error: '%s' is an alias of undefined symbol '%s'\n", file, g->name, g->alias_name);
			exit(1);
		}
		g->alias = t;
	}
}

static void compile_unit(Ctx *ctx, const char *src, const char *file, int is_builtin) {
	ctx->unit++;
	pp_begin_unit(ctx);
	push_file(ctx, src, file, is_builtin);
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	next(ctx);
	while (ctx->tok.kind != T_EOF) parse_toplevel(ctx);
	resolve_aliases(ctx, file);
	pp_end_unit(ctx);
	ctx->scope = NULL;
	ctx->tags = NULL;
}

enum { RC_GP, RC_SEG, RC_CR, RC_DR, RC_XMM, RC_IP };
enum { OP_REG, OP_IMM, OP_MEM };

typedef struct {
	char *name;
	int rc, num, size, hi;
} AsReg;

static AsReg as_regs[160];
static int nas_regs;

typedef struct AsSym AsSym;
struct AsSym {
	char *name;
	Sym *sym;
	int pass;
	int sect;
	long off;
	int is_abs;
	long long val;
	int global, weak, local, type;
	long size;
	int has_size;
	int count, count_pass;
	int fixed, hidden, csym;
	Label *clab;
	AsSym *next;
};

typedef struct AsTree AsTree;

typedef struct {
	long long v;
	AsSym *add, *sub;
	AsTree *t;
} AsVal;

struct AsTree {
	int op;
	AsVal l, r;
};

typedef struct {
	int kind;
	int rc, reg, size, hi;
	int base, index, scale, asz, seg;
	int star;
	AsVal val;
} AsOp;

typedef struct {
	int sect;
	long pos, end;
	int size, pc, sign, plt;
	AsVal val;
	const char *file;
	int line;
} AsFix;

typedef struct {
	int sect;
	long pos;
	int idx;
	AsVal val;
} AsJmp;

typedef struct {
	Ctx *ctx;
	int pass, mode, sect, prev;
	int stack[16], nstack;
	Code c;
	Tok tok, pk;
	int nl, has_pk;
	AsSym *tab[256];
	AsFix *fix;
	int nfix, capfix;
	AsJmp *jmp;
	int njmp, capjmp;
	char *longj;
	int nj, capj;
	int changed;
	uint8_t pend[4];
	int npend;
	size_t *base;
	int nbase;
	long bss_base;
	int done;
	Lex *lx;
	int snip;
	const char *sfile;
	int sline;
	int cond[16], ncond;
	char *bss_name;
	long *bpos;
	char **bname;
	int nbm, capbm;
} As;

enum {
	AI_ALU, AI_SHIFT, AI_UNARY, AI_INCDEC, AI_IMUL, AI_MOV, AI_MOVABS, AI_LEA, AI_TEST, AI_XCHG,
	AI_MOVX, AI_FIXED, AI_FIXSZ, AI_STR, AI_PUSH, AI_POP, AI_PUSHF, AI_PUSHA, AI_JMP, AI_CALL,
	AI_JCC, AI_LOOP, AI_LJMP, AI_RET, AI_INT, AI_IN, AI_OUT, AI_PREFIX, AI_SETCC, AI_CMOV,
	AI_0F, AI_BT, AI_BSWAP, AI_SHD, AI_GRP, AI_NOP, AI_SSE, AI_MOVD, AI_CVTI, AI_CVTF
};
enum { F_SUF = 1, F_REGSRC = 2, F_BYTE = 4, F_MEM = 8, F_DT = 16, F_WQ = 32, F_W = 64, F_FAR = 128 };

typedef struct {
	const char *name;
	uint8_t kind, op, ext, sz, flags;
	const char *code;
} AsIns;

static const AsIns as_table[] = {
	{ "add", AI_ALU, 0, 0, 0, F_SUF, NULL }, { "or", AI_ALU, 0, 1, 0, F_SUF, NULL },
	{ "adc", AI_ALU, 0, 2, 0, F_SUF, NULL }, { "sbb", AI_ALU, 0, 3, 0, F_SUF, NULL },
	{ "and", AI_ALU, 0, 4, 0, F_SUF, NULL }, { "sub", AI_ALU, 0, 5, 0, F_SUF, NULL },
	{ "xor", AI_ALU, 0, 6, 0, F_SUF, NULL }, { "cmp", AI_ALU, 0, 7, 0, F_SUF, NULL },
	{ "rol", AI_SHIFT, 0, 0, 0, F_SUF, NULL }, { "ror", AI_SHIFT, 0, 1, 0, F_SUF, NULL },
	{ "rcl", AI_SHIFT, 0, 2, 0, F_SUF, NULL }, { "rcr", AI_SHIFT, 0, 3, 0, F_SUF, NULL },
	{ "shl", AI_SHIFT, 0, 4, 0, F_SUF, NULL }, { "sal", AI_SHIFT, 0, 4, 0, F_SUF, NULL },
	{ "shr", AI_SHIFT, 0, 5, 0, F_SUF, NULL }, { "sar", AI_SHIFT, 0, 7, 0, F_SUF, NULL },
	{ "not", AI_UNARY, 0, 2, 0, F_SUF, NULL }, { "neg", AI_UNARY, 0, 3, 0, F_SUF, NULL },
	{ "mul", AI_UNARY, 0, 4, 0, F_SUF, NULL }, { "div", AI_UNARY, 0, 6, 0, F_SUF, NULL },
	{ "idiv", AI_UNARY, 0, 7, 0, F_SUF, NULL },
	{ "inc", AI_INCDEC, 0, 0, 0, F_SUF, NULL }, { "dec", AI_INCDEC, 0, 1, 0, F_SUF, NULL },
	{ "imul", AI_IMUL, 0, 0, 0, F_SUF, NULL },
	{ "mov", AI_MOV, 0, 0, 0, F_SUF, NULL }, { "movabs", AI_MOVABS, 0, 0, 0, F_SUF, NULL },
	{ "lea", AI_LEA, 0, 0, 0, F_SUF, NULL }, { "test", AI_TEST, 0, 0, 0, F_SUF, NULL },
	{ "xchg", AI_XCHG, 0, 0, 0, F_SUF, NULL },
	{ "movzbw", AI_MOVX, 0xB6, 1, 2, 0, NULL }, { "movzbl", AI_MOVX, 0xB6, 1, 4, 0, NULL },
	{ "movzbq", AI_MOVX, 0xB6, 1, 8, 0, NULL }, { "movzwl", AI_MOVX, 0xB7, 2, 4, 0, NULL },
	{ "movzwq", AI_MOVX, 0xB7, 2, 8, 0, NULL }, { "movsbw", AI_MOVX, 0xBE, 1, 2, 0, NULL },
	{ "movsbl", AI_MOVX, 0xBE, 1, 4, 0, NULL }, { "movsbq", AI_MOVX, 0xBE, 1, 8, 0, NULL },
	{ "movswl", AI_MOVX, 0xBF, 2, 4, 0, NULL }, { "movswq", AI_MOVX, 0xBF, 2, 8, 0, NULL },
	{ "movslq", AI_MOVX, 0x63, 4, 8, 0, NULL }, { "movsxd", AI_MOVX, 0x63, 4, 0, 0, NULL },
	{ "movzx", AI_MOVX, 0xB6, 0, 0, 0, NULL }, { "movsx", AI_MOVX, 0xBE, 0, 0, 0, NULL },
	{ "hlt", AI_FIXED, 0, 0, 0, 0, "\xF4" }, { "cli", AI_FIXED, 0, 0, 0, 0, "\xFA" },
	{ "sti", AI_FIXED, 0, 0, 0, 0, "\xFB" }, { "clc", AI_FIXED, 0, 0, 0, 0, "\xF8" },
	{ "stc", AI_FIXED, 0, 0, 0, 0, "\xF9" }, { "cld", AI_FIXED, 0, 0, 0, 0, "\xFC" },
	{ "std", AI_FIXED, 0, 0, 0, 0, "\xFD" }, { "cmc", AI_FIXED, 0, 0, 0, 0, "\xF5" },
	{ "pause", AI_FIXED, 0, 0, 0, 0, "\xF3\x90" }, { "int3", AI_FIXED, 0, 0, 0, 0, "\xCC" },
	{ "leave", AI_FIXED, 0, 0, 0, F_SUF, "\xC9" }, { "ud2", AI_FIXED, 0, 0, 0, 0, "\x0F\x0B" },
	{ "cpuid", AI_FIXED, 0, 0, 0, 0, "\x0F\xA2" }, { "rdtsc", AI_FIXED, 0, 0, 0, 0, "\x0F\x31" },
	{ "rdtscp", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xF9" }, { "rdmsr", AI_FIXED, 0, 0, 0, 0, "\x0F\x32" },
	{ "wrmsr", AI_FIXED, 0, 0, 0, 0, "\x0F\x30" }, { "rdpmc", AI_FIXED, 0, 0, 0, 0, "\x0F\x33" },
	{ "clts", AI_FIXED, 0, 0, 0, 0, "\x0F\x06" }, { "wbinvd", AI_FIXED, 0, 0, 0, 0, "\x0F\x09" },
	{ "invd", AI_FIXED, 0, 0, 0, 0, "\x0F\x08" }, { "swapgs", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xF8" },
	{ "syscall", AI_FIXED, 0, 0, 0, 0, "\x0F\x05" }, { "sysenter", AI_FIXED, 0, 0, 0, 0, "\x0F\x34" },
	{ "sysexit", AI_FIXED, 0, 0, 0, 0, "\x0F\x35" }, { "mfence", AI_FIXED, 0, 0, 0, 0, "\x0F\xAE\xF0" },
	{ "lfence", AI_FIXED, 0, 0, 0, 0, "\x0F\xAE\xE8" }, { "sfence", AI_FIXED, 0, 0, 0, 0, "\x0F\xAE\xF8" },
	{ "monitor", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC8" }, { "mwait", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC9" },
	{ "lahf", AI_FIXED, 0, 0, 0, 0, "\x9F" }, { "sahf", AI_FIXED, 0, 0, 0, 0, "\x9E" },
	{ "xgetbv", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xD0" }, { "xsetbv", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xD1" },
	{ "cbtw", AI_FIXED, 0, 0, 2, 0, "\x98" }, { "cwtl", AI_FIXED, 0, 0, 4, 0, "\x98" },
	{ "cltq", AI_FIXED, 0, 0, 8, 0, "\x98" }, { "cwtd", AI_FIXED, 0, 0, 2, 0, "\x99" },
	{ "cltd", AI_FIXED, 0, 0, 4, 0, "\x99" }, { "cqto", AI_FIXED, 0, 0, 8, 0, "\x99" },
	{ "cbw", AI_FIXED, 0, 0, 2, 0, "\x98" }, { "cwde", AI_FIXED, 0, 0, 4, 0, "\x98" },
	{ "cdqe", AI_FIXED, 0, 0, 8, 0, "\x98" }, { "cwd", AI_FIXED, 0, 0, 2, 0, "\x99" },
	{ "cdq", AI_FIXED, 0, 0, 4, 0, "\x99" }, { "cqo", AI_FIXED, 0, 0, 8, 0, "\x99" },
	{ "iret", AI_FIXSZ, 0, 0, 0, F_SUF, "\xCF" }, { "sysret", AI_FIXSZ, 0, 0, 0, F_SUF, "\x0F\x07" },
	{ "movs", AI_STR, 0xA4, 0, 0, F_SUF, NULL }, { "cmps", AI_STR, 0xA6, 0, 0, F_SUF, NULL },
	{ "stos", AI_STR, 0xAA, 0, 0, F_SUF, NULL }, { "lods", AI_STR, 0xAC, 0, 0, F_SUF, NULL },
	{ "scas", AI_STR, 0xAE, 0, 0, F_SUF, NULL }, { "ins", AI_STR, 0x6C, 0, 0, F_SUF, NULL },
	{ "outs", AI_STR, 0x6E, 0, 0, F_SUF, NULL },
	{ "push", AI_PUSH, 0, 0, 0, F_SUF, NULL }, { "pop", AI_POP, 0, 0, 0, F_SUF, NULL },
	{ "pushf", AI_PUSHF, 0x9C, 0, 0, F_SUF, NULL }, { "popf", AI_PUSHF, 0x9D, 0, 0, F_SUF, NULL },
	{ "pusha", AI_PUSHA, 0x60, 0, 0, F_SUF, NULL }, { "popa", AI_PUSHA, 0x61, 0, 0, F_SUF, NULL },
	{ "jmp", AI_JMP, 0xEB, 4, 0, F_SUF, NULL }, { "call", AI_CALL, 0xE8, 2, 0, F_SUF, NULL },
	{ "loop", AI_LOOP, 0xE2, 0, 0, 0, NULL }, { "loope", AI_LOOP, 0xE1, 0, 0, 0, NULL },
	{ "loopz", AI_LOOP, 0xE1, 0, 0, 0, NULL }, { "loopne", AI_LOOP, 0xE0, 0, 0, 0, NULL },
	{ "loopnz", AI_LOOP, 0xE0, 0, 0, 0, NULL }, { "jcxz", AI_LOOP, 0xE3, 0, 2, 0, NULL },
	{ "jecxz", AI_LOOP, 0xE3, 0, 4, 0, NULL }, { "jrcxz", AI_LOOP, 0xE3, 0, 8, 0, NULL },
	{ "ljmp", AI_LJMP, 0xEA, 5, 0, F_SUF, NULL }, { "lcall", AI_LJMP, 0x9A, 3, 0, F_SUF, NULL },
	{ "ret", AI_RET, 0xC3, 0, 0, F_SUF, NULL }, { "lret", AI_RET, 0xCB, 0, 0, F_SUF | F_FAR, NULL },
	{ "int", AI_INT, 0, 0, 0, 0, NULL },
	{ "in", AI_IN, 0, 0, 0, F_SUF, NULL }, { "out", AI_OUT, 0, 0, 0, F_SUF, NULL },
	{ "rep", AI_PREFIX, 0xF3, 0, 0, 0, NULL }, { "repe", AI_PREFIX, 0xF3, 0, 0, 0, NULL },
	{ "repz", AI_PREFIX, 0xF3, 0, 0, 0, NULL }, { "repne", AI_PREFIX, 0xF2, 0, 0, 0, NULL },
	{ "repnz", AI_PREFIX, 0xF2, 0, 0, 0, NULL }, { "lock", AI_PREFIX, 0xF0, 0, 0, 0, NULL },
	{ "data16", AI_PREFIX, 0x66, 0, 0, 0, NULL }, { "data32", AI_PREFIX, 0x66, 0, 0, 0, NULL },
	{ "addr16", AI_PREFIX, 0x67, 0, 0, 0, NULL }, { "addr32", AI_PREFIX, 0x67, 0, 0, 0, NULL },
	{ "bsf", AI_0F, 0xBC, 0, 0, F_SUF, NULL }, { "bsr", AI_0F, 0xBD, 0, 0, F_SUF, NULL },
	{ "popcnt", AI_0F, 0xB8, 0, 0, F_SUF, "\xF3" }, { "lzcnt", AI_0F, 0xBD, 0, 0, F_SUF, "\xF3" },
	{ "tzcnt", AI_0F, 0xBC, 0, 0, F_SUF, "\xF3" },
	{ "xadd", AI_0F, 0xC1, 0, 0, F_SUF | F_REGSRC | F_BYTE, NULL },
	{ "cmpxchg", AI_0F, 0xB1, 0, 0, F_SUF | F_REGSRC | F_BYTE, NULL },
	{ "bt", AI_BT, 0xA3, 4, 0, F_SUF, NULL }, { "bts", AI_BT, 0xAB, 5, 0, F_SUF, NULL },
	{ "btr", AI_BT, 0xB3, 6, 0, F_SUF, NULL }, { "btc", AI_BT, 0xBB, 7, 0, F_SUF, NULL },
	{ "bswap", AI_BSWAP, 0, 0, 0, F_SUF, NULL },
	{ "shld", AI_SHD, 0xA4, 0, 0, F_SUF, NULL }, { "shrd", AI_SHD, 0xAC, 0, 0, F_SUF, NULL },
	{ "lgdt", AI_GRP, 0, 2, 0, F_SUF | F_MEM | F_DT, "\x0F\x01" },
	{ "lidt", AI_GRP, 0, 3, 0, F_SUF | F_MEM | F_DT, "\x0F\x01" },
	{ "sgdt", AI_GRP, 0, 0, 0, F_SUF | F_MEM | F_DT, "\x0F\x01" },
	{ "sidt", AI_GRP, 0, 1, 0, F_SUF | F_MEM | F_DT, "\x0F\x01" },
	{ "lldt", AI_GRP, 0, 2, 0, F_SUF, "\x0F\x00" }, { "ltr", AI_GRP, 0, 3, 0, F_SUF, "\x0F\x00" },
	{ "sldt", AI_GRP, 0, 0, 1, F_SUF, "\x0F\x00" }, { "str", AI_GRP, 0, 1, 1, F_SUF, "\x0F\x00" },
	{ "verr", AI_GRP, 0, 4, 0, F_SUF, "\x0F\x00" }, { "verw", AI_GRP, 0, 5, 0, F_SUF, "\x0F\x00" },
	{ "smsw", AI_GRP, 0, 4, 1, F_SUF, "\x0F\x01" }, { "lmsw", AI_GRP, 0, 6, 0, F_SUF, "\x0F\x01" },
	{ "invlpg", AI_GRP, 0, 7, 0, F_MEM, "\x0F\x01" },
	{ "fxsave", AI_GRP, 0, 0, 0, F_SUF | F_MEM | F_WQ, "\x0F\xAE" },
	{ "fxrstor", AI_GRP, 0, 1, 0, F_SUF | F_MEM | F_WQ, "\x0F\xAE" },
	{ "fxsave64", AI_GRP, 0, 0, 0, F_MEM | F_W, "\x0F\xAE" },
	{ "fxrstor64", AI_GRP, 0, 1, 0, F_MEM | F_W, "\x0F\xAE" },
	{ "ldmxcsr", AI_GRP, 0, 2, 0, F_MEM, "\x0F\xAE" }, { "stmxcsr", AI_GRP, 0, 3, 0, F_MEM, "\x0F\xAE" },
	{ "xsave", AI_GRP, 0, 4, 0, F_SUF | F_MEM | F_WQ, "\x0F\xAE" },
	{ "xrstor", AI_GRP, 0, 5, 0, F_SUF | F_MEM | F_WQ, "\x0F\xAE" },
	{ "clflush", AI_GRP, 0, 7, 0, F_MEM, "\x0F\xAE" },
	{ "cmpxchg8b", AI_GRP, 0, 1, 0, F_MEM, "\x0F\xC7" },
	{ "cmpxchg16b", AI_GRP, 0, 1, 0, F_MEM | F_W, "\x0F\xC7" },
	{ "nop", AI_NOP, 0, 0, 0, F_SUF, NULL },
	{ "movaps", AI_SSE, 0x28, 0x29, 0, 0, NULL }, { "movups", AI_SSE, 0x10, 0x11, 0, 0, NULL },
	{ "movapd", AI_SSE, 0x28, 0x29, 0, 0, "\x66" }, { "movupd", AI_SSE, 0x10, 0x11, 0, 0, "\x66" },
	{ "movdqa", AI_SSE, 0x6F, 0x7F, 0, 0, "\x66" }, { "movdqu", AI_SSE, 0x6F, 0x7F, 0, 0, "\xF3" },
	{ "movss", AI_SSE, 0x10, 0x11, 0, 0, "\xF3" }, { "movsd", AI_SSE, 0x10, 0x11, 0, 0, "\xF2" },
	{ "addsd", AI_SSE, 0x58, 0, 0, 0, "\xF2" }, { "subsd", AI_SSE, 0x5C, 0, 0, 0, "\xF2" },
	{ "mulsd", AI_SSE, 0x59, 0, 0, 0, "\xF2" }, { "divsd", AI_SSE, 0x5E, 0, 0, 0, "\xF2" },
	{ "sqrtsd", AI_SSE, 0x51, 0, 0, 0, "\xF2" }, { "minsd", AI_SSE, 0x5D, 0, 0, 0, "\xF2" },
	{ "maxsd", AI_SSE, 0x5F, 0, 0, 0, "\xF2" }, { "addss", AI_SSE, 0x58, 0, 0, 0, "\xF3" },
	{ "subss", AI_SSE, 0x5C, 0, 0, 0, "\xF3" }, { "mulss", AI_SSE, 0x59, 0, 0, 0, "\xF3" },
	{ "divss", AI_SSE, 0x5E, 0, 0, 0, "\xF3" }, { "sqrtss", AI_SSE, 0x51, 0, 0, 0, "\xF3" },
	{ "minss", AI_SSE, 0x5D, 0, 0, 0, "\xF3" }, { "maxss", AI_SSE, 0x5F, 0, 0, 0, "\xF3" },
	{ "addps", AI_SSE, 0x58, 0, 0, 0, NULL }, { "addpd", AI_SSE, 0x58, 0, 0, 0, "\x66" },
	{ "subps", AI_SSE, 0x5C, 0, 0, 0, NULL }, { "subpd", AI_SSE, 0x5C, 0, 0, 0, "\x66" },
	{ "mulps", AI_SSE, 0x59, 0, 0, 0, NULL }, { "mulpd", AI_SSE, 0x59, 0, 0, 0, "\x66" },
	{ "divps", AI_SSE, 0x5E, 0, 0, 0, NULL }, { "divpd", AI_SSE, 0x5E, 0, 0, 0, "\x66" },
	{ "andps", AI_SSE, 0x54, 0, 0, 0, NULL }, { "andpd", AI_SSE, 0x54, 0, 0, 0, "\x66" },
	{ "andnps", AI_SSE, 0x55, 0, 0, 0, NULL }, { "andnpd", AI_SSE, 0x55, 0, 0, 0, "\x66" },
	{ "orps", AI_SSE, 0x56, 0, 0, 0, NULL }, { "orpd", AI_SSE, 0x56, 0, 0, 0, "\x66" },
	{ "xorps", AI_SSE, 0x57, 0, 0, 0, NULL }, { "xorpd", AI_SSE, 0x57, 0, 0, 0, "\x66" },
	{ "pxor", AI_SSE, 0xEF, 0, 0, 0, "\x66" }, { "por", AI_SSE, 0xEB, 0, 0, 0, "\x66" },
	{ "pand", AI_SSE, 0xDB, 0, 0, 0, "\x66" }, { "paddq", AI_SSE, 0xD4, 0, 0, 0, "\x66" },
	{ "ucomisd", AI_SSE, 0x2E, 0, 0, 0, "\x66" }, { "ucomiss", AI_SSE, 0x2E, 0, 0, 0, NULL },
	{ "comisd", AI_SSE, 0x2F, 0, 0, 0, "\x66" }, { "comiss", AI_SSE, 0x2F, 0, 0, 0, NULL },
	{ "cvtsd2ss", AI_SSE, 0x5A, 0, 0, 0, "\xF2" }, { "cvtss2sd", AI_SSE, 0x5A, 0, 0, 0, "\xF3" },
	{ "movd", AI_MOVD, 0, 0, 4, 0, NULL },
	{ "cvtsi2sd", AI_CVTI, 0x2A, 0, 0, F_SUF, "\xF2" }, { "cvtsi2ss", AI_CVTI, 0x2A, 0, 0, F_SUF, "\xF3" },
	{ "cvttsd2si", AI_CVTF, 0x2C, 0, 0, F_SUF, "\xF2" }, { "cvtsd2si", AI_CVTF, 0x2D, 0, 0, F_SUF, "\xF2" },
	{ "cvttss2si", AI_CVTF, 0x2C, 0, 0, F_SUF, "\xF3" }, { "cvtss2si", AI_CVTF, 0x2D, 0, 0, F_SUF, "\xF3" },
	{ NULL, 0, 0, 0, 0, 0, NULL }
};

static const char *as_ccnames[] = {
	"o", "no", "b", "c", "nae", "ae", "nb", "nc", "e", "z", "ne", "nz", "be", "na", "a", "nbe",
	"s", "ns", "p", "pe", "np", "po", "l", "nge", "ge", "nl", "le", "ng", "g", "nle", NULL
};
static const uint8_t as_cccodes[] = {
	0, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15
};

static AsIns *as_ins;
static char **as_ids;
static int nas_ins;
static char *id_dot;

static void as_reg_add(const char *name, int rc, int num, int size, int hi) {
	AsReg *r = &as_regs[nas_regs++];
	r->name = intern(name); r->rc = rc; r->num = num; r->size = size; r->hi = hi;
}

static void as_init(void) {
	static const char *r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	static const char *r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
	static const char *r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char *r8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };
	static const char *rh[] = { "ah", "ch", "dh", "bh" };
	static const char *sr[] = { "es", "cs", "ss", "ds", "fs", "gs" };
	char b[16];
	if (as_ins) return;
	for (int i = 0; i < 16; i++) {
		if (i < 8) {
			as_reg_add(r64[i], RC_GP, i, 8, 0); as_reg_add(r32[i], RC_GP, i, 4, 0);
			as_reg_add(r16[i], RC_GP, i, 2, 0); as_reg_add(r8[i], RC_GP, i, 1, 0);
		} else {
			snprintf(b, sizeof(b), "r%d", i); as_reg_add(b, RC_GP, i, 8, 0);
			snprintf(b, sizeof(b), "r%dd", i); as_reg_add(b, RC_GP, i, 4, 0);
			snprintf(b, sizeof(b), "r%dw", i); as_reg_add(b, RC_GP, i, 2, 0);
			snprintf(b, sizeof(b), "r%db", i); as_reg_add(b, RC_GP, i, 1, 0);
			snprintf(b, sizeof(b), "r%dl", i); as_reg_add(b, RC_GP, i, 1, 0);
		}
		snprintf(b, sizeof(b), "cr%d", i); as_reg_add(b, RC_CR, i, 8, 0);
		snprintf(b, sizeof(b), "dr%d", i); as_reg_add(b, RC_DR, i, 8, 0);
		snprintf(b, sizeof(b), "xmm%d", i); as_reg_add(b, RC_XMM, i, 16, 0);
	}
	for (int i = 0; i < 4; i++) as_reg_add(rh[i], RC_GP, 4 + i, 1, 1);
	for (int i = 0; i < 6; i++) as_reg_add(sr[i], RC_SEG, i, 2, 0);
	as_reg_add("rip", RC_IP, ENC_RIP, 8, 0);

	int n = 0;
	while (as_table[n].name) n++;
	int ncc = 0;
	while (as_ccnames[ncc]) ncc++;
	as_ins = (AsIns *)calloc(n + 3 * ncc, sizeof(AsIns));
	as_ids = (char **)calloc(n + 3 * ncc, sizeof(char *));
	for (int i = 0; i < n; i++) as_ins[nas_ins++] = as_table[i];
	for (int k = 0; k < 3; k++)
		for (int i = 0; i < ncc; i++) {
			AsIns *e = &as_ins[nas_ins++];
			snprintf(b, sizeof(b), "%s%s", k == 0 ? "j" : k == 1 ? "set" : "cmov", as_ccnames[i]);
			e->name = intern(b);
			e->kind = k == 0 ? AI_JCC : k == 1 ? AI_SETCC : AI_CMOV;
			e->ext = as_cccodes[i];
			e->flags = k == 2 ? F_SUF : 0;
		}
	for (int i = 0; i < nas_ins; i++) as_ids[i] = intern(as_ins[i].name);
	id_dot = intern(".");
}

static const AsIns *as_find(char *name, int *suffix) {
	for (int i = 0; i < nas_ins; i++)
		if (as_ids[i] == name) { *suffix = 0; return &as_ins[i]; }
	int n = (int)strlen(name);
	if (n < 2) return NULL;
	int c = name[n-1];
	int sz = c == 'b' ? 1 : c == 'w' ? 2 : c == 'l' ? 4 : c == 'q' ? 8 : 0;
	if (!sz) return NULL;
	char *base = intern_str(name, n - 1)->s;
	for (int i = 0; i < nas_ins; i++)
		if (as_ids[i] == base && (as_ins[i].flags & F_SUF)) { *suffix = sz; return &as_ins[i]; }
	return NULL;
}

static void as_next(As *A) {
	if (A->has_pk) { A->tok = A->pk; A->has_pk = 0; }
	else A->tok = A->lx ? lex_next(A->lx) : pp_next(A->ctx);
	A->nl = A->tok.bol;
	if (A->snip) {
		A->ctx->tok = A->tok;
		A->ctx->tok.file = A->sfile;
		A->ctx->tok.line = A->sline;
	} else if (!A->nl) A->ctx->tok = A->tok;
}

static Tok *as_peek(As *A) {
	if (!A->has_pk) { A->pk = A->lx ? lex_next(A->lx) : pp_next(A->ctx); A->has_pk = 1; }
	return &A->pk;
}

static int as_at_end(As *A) {
	return A->tok.kind == T_EOF || A->tok.kind == T_SEMI || A->nl;
}

static int as_is(As *A, int k) { return (int)A->tok.kind == k && !A->nl; }

static void as_expect(As *A, int k) {
	if (!as_is(A, k)) error_at(A->ctx, "expected '%s', got '%s'", kind_desc(k), as_at_end(A) ? "end of line" : tok_desc(&A->tok));
	as_next(A);
}

static unsigned as_hash(const char *s) {
	unsigned h = 0;
	while (*s) h = h * 31 + (unsigned char)*s++;
	return h % 256;
}

static AsSym *as_sym(As *A, char *name) {
	unsigned h = as_hash(name);
	for (AsSym *s = A->tab[h]; s; s = s->next)
		if (s->name == name) return s;
	AsSym *s = (AsSym *)calloc(1, sizeof(AsSym));
	s->name = name;
	s->sym = (Sym *)calloc(1, sizeof(Sym));
	s->sym->name = name;
	s->sym->kind = SYM_VAR;
	s->sym->ty = ty_char;
	s->sym->is_static = 1;
	s->sym->is_asm = 1;
	s->next = A->tab[h];
	A->tab[h] = s;
	if (A->snip && !isdigit((unsigned char)name[0]) && strncmp(name, ".L", 2) != 0 && name != id_dot) {
		Sym *c = lookup(A->ctx, name);
		if (c && c->global) c = c->global;
		if (!c || !(c->kind == SYM_FUNC || (c->kind == SYM_VAR && !c->is_local))) {
			c = NULL;
			for (Sym *g = A->ctx->globals; g; g = g->gnext)
				if (g->name == name && !g->is_static && (g->kind == SYM_FUNC || g->kind == SYM_VAR)) { c = g; break; }
		}
		if (c) { s->sym = c; s->csym = 1; }
	}
	return s;
}

static AsSym *as_numlab(As *A, const char *digits, int len, int dir) {
	AsSym *b = as_sym(A, intern_str(digits, len)->s);
	if (b->count_pass != A->pass) { b->count_pass = A->pass; b->count = 0; }
	int k = dir < 0 ? b->count - 1 : b->count;
	if (k < 0) error_at(A->ctx, "undefined local label '%.*sb'", len, digits);
	if (dir == 0) b->count++;
	char buf[64];
	snprintf(buf, sizeof(buf), "%.*s\001%d", len, digits, k);
	return as_sym(A, intern(buf));
}

static long as_pos(As *A) { return A->sect == SEC_BSS ? A->ctx->bss_size : (long)Cpos(&A->c); }

static int as_defined(As *A, AsSym *s) { return s && (s->pass == A->pass || s->fixed); }

static void as_define(As *A, AsSym *s) {
	if (as_defined(A, s)) error_at(A->ctx, "symbol '%s' is already defined", s->name);
	s->pass = A->pass;
	s->is_abs = 0;
	s->sect = A->sect;
	s->off = as_pos(A);
}

static AsVal as_apply(As *A, int op, AsVal l, AsVal r);

static void as_fold(As *A, AsVal *x) {
	if (x->t) {
		AsVal l = x->t->l, r = x->t->r;
		as_fold(A, &l);
		as_fold(A, &r);
		*x = as_apply(A, x->t->op, l, r);
		if (x->t) return;
	}
	if (x->add && as_defined(A, x->add) && x->add->is_abs) { x->v += x->add->val; x->add = NULL; }
	if (x->sub && as_defined(A, x->sub) && x->sub->is_abs) { x->v -= x->sub->val; x->sub = NULL; }
	if (x->add && x->add == x->sub) x->add = x->sub = NULL;
	if (x->add && x->sub && as_defined(A, x->add) && as_defined(A, x->sub) && x->add->sect == x->sub->sect) {
		x->v += x->add->off - x->sub->off;
		x->add = x->sub = NULL;
	}
}

static int as_known(AsVal *v) { return !v->add && !v->sub && !v->t; }

static AsVal as_tree(int op, AsVal l, AsVal r) {
	AsVal v;
	v.v = 0; v.add = v.sub = NULL;
	v.t = (AsTree *)calloc(1, sizeof(AsTree));
	v.t->op = op; v.t->l = l; v.t->r = r;
	return v;
}

static AsVal as_sum(As *A, AsVal a, AsVal b) {
	AsVal r;
	AsSym *adds[2], *subs[2];
	int na = 0, ns = 0;
	if (a.t || b.t) return as_tree(T_PLUS, a, b);
	r.v = a.v + b.v;
	r.t = NULL;
	if (a.add) adds[na++] = a.add;
	if (b.add) adds[na++] = b.add;
	if (a.sub) subs[ns++] = a.sub;
	if (b.sub) subs[ns++] = b.sub;
	for (int i = 0; i < na; i++)
		for (int j = 0; j < ns; j++)
			if (adds[i] && adds[i] == subs[j]) { adds[i] = subs[j] = NULL; break; }
	r.add = r.sub = NULL;
	for (int i = 0; i < na; i++) if (adds[i]) { if (r.add) error_at(A->ctx, "invalid sum of symbols"); r.add = adds[i]; }
	for (int i = 0; i < ns; i++) if (subs[i]) { if (r.sub) error_at(A->ctx, "invalid difference of symbols"); r.sub = subs[i]; }
	as_fold(A, &r);
	return r;
}

static AsVal as_const(long long v) { AsVal r; r.v = v; r.add = r.sub = NULL; r.t = NULL; return r; }

static AsVal as_neg(As *A, AsVal v) {
	AsVal r;
	if (v.t) return as_tree(T_MINUS, as_const(0), v);
	r.v = -v.v; r.add = v.sub; r.sub = v.add; r.t = NULL;
	(void)A;
	return r;
}

static AsVal as_apply(As *A, int op, AsVal l, AsVal r) {
	if (op == T_PLUS) return as_sum(A, l, r);
	if (op == T_MINUS) return as_sum(A, l, as_neg(A, r));
	if (!as_known(&l) || !as_known(&r)) return as_tree(op, l, r);
	long long a = l.v, b = r.v;
	switch (op) {
	case T_TILDE: a = ~a; break;
	case T_NOT: a = !a; break;
	case T_MUL: a = a * b; break;
	case T_DIV: case T_MOD:
		if (b == 0) error_at(A->ctx, "division by zero");
		a = op == T_DIV ? a / b : a % b;
		break;
	case T_SHL: a = (long long)((unsigned long long)a << (b & 63)); break;
	case T_SHR: a = a >> (b & 63); break;
	case T_AMP: a &= b; break;
	case T_PIPE: a |= b; break;
	case T_CARET: a ^= b; break;
	case T_LT: a = a < b; break;
	case T_GT: a = a > b; break;
	case T_LEQ: a = a <= b; break;
	case T_GEQ: a = a >= b; break;
	case T_EQ: a = a == b; break;
	case T_NEQ: a = a != b; break;
	case T_AND: a = a && b; break;
	case T_OR: a = a || b; break;
	default: error_at(A->ctx, "invalid operator in expression");
	}
	return as_const(a);
}

static AsVal as_binary(As *A, int minprec);

static AsVal as_primary(As *A) {
	Tok t = A->tok;
	if (as_at_end(A)) error_at(A->ctx, "expected an expression");
	if (t.kind == T_NUM) { as_next(A); return as_const(t.val); }
	if (t.kind == T_LP) {
		as_next(A);
		AsVal v = as_binary(A, 1);
		as_expect(A, T_RP);
		return v;
	}
	if (t.kind == T_MINUS || t.kind == T_TILDE || t.kind == T_NOT || t.kind == T_PLUS) {
		as_next(A);
		AsVal v = as_primary(A);
		if (t.kind == T_PLUS) return v;
		if (t.kind == T_MINUS) return as_neg(A, v);
		return as_apply(A, t.kind, v, as_const(0));
	}
	if (t.kind == T_IDENT) {
		as_next(A);
		AsVal r = as_const(0);
		int n = (int)strlen(t.str);
		if (t.str == id_dot) {
			AsSym *d = (AsSym *)calloc(1, sizeof(AsSym));
			d->name = id_dot;
			d->pass = A->pass;
			d->sect = A->sect;
			d->off = as_pos(A);
			r.add = d;
			return r;
		}
		if (isdigit((unsigned char)t.str[0])) r.add = as_numlab(A, t.str, n - 1, t.str[n-1] == 'b' ? -1 : 1);
		else r.add = as_sym(A, t.str);
		as_fold(A, &r);
		return r;
	}
	error_at(A->ctx, "unexpected '%s' in expression", tok_desc(&t));
	return as_const(0);
}

static AsVal as_binary(As *A, int minprec) {
	AsVal l = as_primary(A);
	for (;;) {
		int op = A->tok.kind;
		int prec = binop_prec(op);
		if (A->nl || prec == 0 || prec < minprec) return l;
		as_next(A);
		AsVal r = as_binary(A, prec + 1);
		l = as_apply(A, op, l, r);
	}
}

static AsVal as_expr(As *A) { return as_binary(A, 1); }

static long long as_absexpr(As *A) {
	AsVal v = as_expr(A);
	if (!as_known(&v)) error_at(A->ctx, "expression must be constant here");
	return v.v;
}

static const AsReg *as_reg(As *A) {
	as_expect(A, '%');
	if (!as_is(A, T_IDENT)) error_at(A->ctx, "expected a register name after '%%'");
	for (int i = 0; i < nas_regs; i++)
		if (as_regs[i].name == A->tok.str) { as_next(A); return &as_regs[i]; }
	error_at(A->ctx, "unknown register '%%%s'", A->tok.str);
	return NULL;
}

static int as_addr_reg(As *A, int *asz) {
	const AsReg *r = as_reg(A);
	if (r->rc != RC_GP && r->rc != RC_IP) error_at(A->ctx, "'%%%s' cannot be used in an address", r->name);
	if (r->rc == RC_GP && (r->size == 1)) error_at(A->ctx, "'%%%s' cannot be used in an address", r->name);
	if (*asz && *asz != r->size) error_at(A->ctx, "address registers have different sizes");
	*asz = r->size;
	return r->num;
}

static void as_operand(As *A, AsOp *o) {
	memset(o, 0, sizeof(*o));
	o->base = o->index = o->seg = -1;
	o->scale = 1;
	if (as_is(A, T_MUL)) { o->star = 1; as_next(A); }
	if (as_is(A, T_DOLLAR)) {
		as_next(A);
		o->kind = OP_IMM;
		o->val = as_expr(A);
		return;
	}
	if (as_is(A, '%')) {
		const AsReg *r = as_reg(A);
		if (r->rc == RC_SEG && as_is(A, T_COLON)) {
			as_next(A);
			o->seg = r->num;
		} else {
			if (r->rc == RC_IP) error_at(A->ctx, "'%%rip' can only be a base register");
			o->kind = OP_REG;
			o->rc = r->rc; o->reg = r->num; o->size = r->size; o->hi = r->hi;
			return;
		}
	}
	o->kind = OP_MEM;
	if (!(as_is(A, T_LP) && (as_peek(A)->kind == '%' || as_peek(A)->kind == T_COMMA))) o->val = as_expr(A);
	if (!as_is(A, T_LP)) return;
	as_next(A);
	if (as_is(A, '%')) o->base = as_addr_reg(A, &o->asz);
	if (as_is(A, T_COMMA)) {
		as_next(A);
		if (as_is(A, '%')) {
			o->index = as_addr_reg(A, &o->asz);
			if (o->index == ENC_RIP) error_at(A->ctx, "'%%rip' cannot be an index");
			if (o->index == RSP && o->asz != 2) error_at(A->ctx, "the stack pointer cannot be an index");
		}
		if (as_is(A, T_COMMA)) {
			as_next(A);
			long long s = as_absexpr(A);
			if (s != 1 && s != 2 && s != 4 && s != 8) error_at(A->ctx, "scale must be 1, 2, 4 or 8");
			o->scale = (int)s;
		}
	}
	as_expect(A, T_RP);
	if (o->base == ENC_RIP && o->index >= 0) error_at(A->ctx, "'%%rip' cannot be used with an index");
}

static void as_byte(As *A, int x) {
	if (A->sect == SEC_BSS) {
		if (x) error_at(A->ctx, "nonzero data in .bss");
		A->ctx->bss_size++;
		return;
	}
	C8(&A->c, (uint8_t)x);
}

static void as_put(uint8_t *p, long long v, int size) {
	for (int i = 0; i < size; i++) p[i] = (uint8_t)((unsigned long long)v >> (8 * i));
}

static void as_check(As *A, long long v, int size, int sign) {
	if (size >= 8) return;
	long long lo = -(1LL << (8 * size - 1));
	long long hi = sign ? (1LL << (8 * size - 1)) - 1 : (long long)((1ULL << (8 * size)) - 1);
	if (v < lo || v > hi) error_at(A->ctx, "value %lld does not fit in %d byte%s", v, size, size == 1 ? "" : "s");
}

static void as_fixup(As *A, long pos, int size, int pc, int sign, long end, AsVal *v) {
	if (A->sect == SEC_BSS) error_at(A->ctx, "relocated data in .bss");
	if (A->nfix == A->capfix) {
		A->capfix = A->capfix ? A->capfix * 2 : 64;
		A->fix = (AsFix *)realloc(A->fix, sizeof(AsFix) * A->capfix);
		if (!A->fix) { perror("realloc"); exit(1); }
	}
	AsFix *f = &A->fix[A->nfix++];
	f->sect = A->sect; f->pos = pos; f->size = size; f->pc = pc; f->sign = sign; f->end = end;
	f->val = *v; f->file = A->ctx->tok.file; f->line = A->ctx->tok.line; f->plt = 0;
}

static void as_imm(As *A, AsVal *v, int size, int sign) {
	if (as_known(v)) {
		as_check(A, v->v, size, sign);
		for (int i = 0; i < size; i++) as_byte(A, (int)(((unsigned long long)v->v >> (8 * i)) & 0xFF));
		return;
	}
	as_fixup(A, as_pos(A), size, 0, sign, 0, v);
	for (int i = 0; i < size; i++) as_byte(A, 0);
}

static void as_pcrel(As *A, AsVal *v, int size) {
	long pos = as_pos(A);
	as_fixup(A, pos, size, 1, 1, pos + size, v);
	for (int i = 0; i < size; i++) as_byte(A, 0);
}

static long long as_trunc(long long v, int sz) {
	if (sz == 1) return (int8_t)v;
	if (sz == 2) return (int16_t)v;
	if (sz == 4) return (int32_t)v;
	return v;
}

static int as_fits8(AsVal *v, int sz) {
	if (!as_known(v)) return 0;
	long long t = sz == 8 ? v->v : as_trunc(v->v, sz);
	return t >= -128 && t <= 127;
}

static int as_defseg(AsOp *m) {
	return m->base == RSP || m->base == RBP ? 2 : 3;
}

static void as_prefixes(As *A, int sz, AsOp *m) {
	static const uint8_t segpfx[] = { 0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65 };
	Code *c = &A->c;
	int p66 = 0, p67 = 0;
	for (int i = 0; i < A->npend; i++) {
		if (A->pend[i] == 0x66) p66 = 1;
		if (A->pend[i] == 0x67) p67 = 1;
	}
	if (m) {
		if (m->seg >= 0 && m->seg != as_defseg(m)) C8(c, segpfx[m->seg]);
		int da = A->mode == 16 ? 2 : A->mode == 32 ? 4 : 8;
		if (m->asz && m->asz != da) {
			if ((A->mode == 64 && m->asz == 2) || (A->mode != 64 && m->asz == 8))
				error_at(A->ctx, "%d-bit address in %d-bit code", m->asz * 8, A->mode);
			p67 = 1;
		}
	}
	if (p67) C8(c,0x67);
	if (sz == 8 && A->mode != 64) error_at(A->ctx, "64-bit operand in %d-bit code", A->mode);
	if (p66 || (sz == 2 && A->mode != 16) || (sz == 4 && A->mode == 16)) C8(c,0x66);
	for (int i = 0; i < A->npend; i++)
		if (A->pend[i] != 0x66 && A->pend[i] != 0x67) C8(c, A->pend[i]);
	A->npend = 0;
}

static void as_rex(As *A, int w, int r, int x, int b, int force, int hi) {
	if (w || force || r >= 8 || x >= 8 || b >= 8) {
		if (A->mode != 64) error_at(A->ctx, "instruction needs 64-bit code");
		if (hi) error_at(A->ctx, "%%ah, %%ch, %%dh and %%bh cannot be used here");
	}
	enc_rex(&A->c, w, r, x, b, force);
}

static void as_byte_reg(AsOp *o, int *force, int *hi) {
	if (o->kind != OP_REG || o->rc != RC_GP || o->size != 1) return;
	if (o->hi) *hi = 1;
	else if (o->reg >= 4 && o->reg < 8) *force = 1;
}

static void as_mem16(As *A, int reg, AsOp *m) {
	static const int combo[8][3] = {
		{ RBX, RSI, 0 }, { RBX, RDI, 1 }, { RBP, RSI, 2 }, { RBP, RDI, 3 },
		{ RSI, -1, 4 }, { RDI, -1, 5 }, { RBP, -1, 6 }, { RBX, -1, 7 }
	};
	Code *c = &A->c;
	int known = as_known(&m->val);
	long long v = known ? m->val.v : 0;
	int rm = -1;
	if (m->scale != 1) error_at(A->ctx, "16-bit addresses have no scale");
	if (m->base < 0 && m->index < 0) {
		C8(c,(uint8_t)(((reg & 7) << 3) | 6));
		as_imm(A, &m->val, 2, 0);
		return;
	}
	for (int i = 0; i < 8; i++)
		if (combo[i][0] == m->base && combo[i][1] == m->index) rm = combo[i][2];
	if (rm < 0) error_at(A->ctx, "invalid 16-bit address");
	int mod = !known ? 2 : v == 0 && rm != 6 ? 0 : v >= -128 && v <= 127 ? 1 : 2;
	C8(c,(uint8_t)((mod << 6) | ((reg & 7) << 3) | rm));
	if (mod == 1) C8(c,(uint8_t)v);
	else if (mod == 2) as_imm(A, &m->val, 2, 0);
}

static void as_mem(As *A, int reg, AsOp *m, int imm) {
	Code *c = &A->c;
	if (m->asz == 2 || (!m->asz && A->mode == 16)) { as_mem16(A, reg, m); return; }
	int known = as_known(&m->val);
	long long v = known ? m->val.v : 0;
	int wide64 = A->mode == 64 && m->asz != 4;
	if (m->base == ENC_RIP) {
		if (A->mode != 64) error_at(A->ctx, "'%%rip' needs 64-bit code");
		size_t at = enc_mem(c, reg, ENC_RIP, -1, 1, v, 1, 0);
		if (known) as_check(A, v, 4, 1);
		else A->c.b.len = at, as_pcrel(A, &m->val, 4), A->fix[A->nfix-1].end += imm;
		return;
	}
	if (known) {
		as_check(A, v, 4, wide64 || m->base >= 0 || m->index >= 0);
		v = (int32_t)v;
	}
	size_t at = enc_mem(c, reg, m->base, m->index, m->scale, v, !known, A->mode == 64);
	if (!known) {
		A->c.b.len = at;
		as_imm(A, &m->val, 4, wide64);
	}
}

static void as_rm(As *A, int sz, int mp, const char *op, int nop, int reg, AsOp *ro, AsOp *rm, int imm) {
	int force = 0, hi = 0, x = 0, b = 0;
	if (ro) { reg = ro->reg; as_byte_reg(ro, &force, &hi); }
	if (rm->kind == OP_REG) { b = rm->reg; as_byte_reg(rm, &force, &hi); }
	else if (rm->kind == OP_MEM) {
		if (rm->base >= 0 && rm->base < 16 && rm->asz != 2) b = rm->base;
		if (rm->index >= 0 && rm->asz != 2) x = rm->index;
	} else error_at(A->ctx, "an immediate is not valid here");
	as_prefixes(A, sz, rm->kind == OP_MEM ? rm : NULL);
	if (mp) C8(&A->c, (uint8_t)mp);
	as_rex(A, sz == 8, reg, x, b, force, hi);
	for (int i = 0; i < nop; i++) C8(&A->c, (uint8_t)op[i]);
	if (rm->kind == OP_REG) enc_reg(&A->c, reg, rm->reg);
	else as_mem(A, reg, rm, imm);
}

static void as_head(As *A, int sz, AsOp *r) {
	int force = 0, hi = 0;
	if (r) as_byte_reg(r, &force, &hi);
	as_prefixes(A, sz, NULL);
	as_rex(A, sz == 8, 0, 0, r ? r->reg : 0, force, hi);
}

static void as_opreg(As *A, int sz, int op, AsOp *r) {
	as_head(A, sz, r);
	C8(&A->c, (uint8_t)(op + (r->reg & 7)));
}

static int as_sz(As *A, int suf, AsOp *o, int n) {
	int sz = 0;
	for (int i = 0; i < n; i++) {
		if (o[i].kind != OP_REG || o[i].rc != RC_GP) continue;
		if (sz && sz != o[i].size) error_at(A->ctx, "operand sizes do not match");
		sz = o[i].size;
	}
	if (suf) {
		if (sz && sz != suf) error_at(A->ctx, "suffix does not match the register size");
		sz = suf;
	}
	if (!sz) error_at(A->ctx, "operand size unknown; add a b, w, l or q suffix");
	return sz;
}

static int as_isreg(AsOp *o, int rc) { return o->kind == OP_REG && o->rc == rc; }
static int as_isacc(AsOp *o) { return as_isreg(o, RC_GP) && o->reg == 0 && !o->hi; }
static int as_isabs(AsOp *o) { return o->kind == OP_MEM && o->base < 0 && o->index < 0; }

static int as_isdx(AsOp *o) {
	if (as_isreg(o, RC_GP)) return o->reg == RDX && o->size == 2;
	return o->kind == OP_MEM && o->base == RDX && o->asz == 2 && o->index < 0 && !o->val.add && !o->val.v;
}

static void as_nops(As *A, const char *name, int n, int want) {
	if (n != want) error_at(A->ctx, "'%s' takes %d operand%s", name, want, want == 1 ? "" : "s");
}

static int as_default(As *A) { return A->mode == 64 ? 8 : A->mode == 32 ? 4 : 2; }

static void as_jmpidx(As *A) {
	if (A->nj == A->capj) {
		int n = A->capj ? A->capj * 2 : 64;
		A->longj = (char *)realloc(A->longj, n);
		if (!A->longj) { perror("realloc"); exit(1); }
		memset(A->longj + A->capj, 0, n - A->capj);
		A->capj = n;
	}
	A->nj++;
}

static int as_near(As *A, AsVal *v, long end) {
	AsSym *s = v->add;
	if (!s || v->sub || s->weak || s->is_abs) return 0;
	if (as_defined(A, s)) {
		if (s->sect != A->sect) return 0;
		long long d = s->off + v->v - end;
		return d >= -128 && d <= 127;
	}
	if (s->pass == A->pass - 1) return s->sect == A->sect;
	return A->pass == 1 && s->pass == 0;
}

static void as_branch(As *A, const AsIns *in, AsOp *o, int n, char *name) {
	Code *c = &A->c;
	int rs = A->mode == 16 ? 2 : 4;
	as_nops(A, name, n, 1);
	if (o->star || o->kind == OP_REG) {
		if (in->kind == AI_JCC) error_at(A->ctx, "conditional jumps cannot be indirect");
		if (o->kind == OP_REG && (o->rc != RC_GP || o->size != as_default(A))) error_at(A->ctx, "invalid register for an indirect branch");
		as_rm(A, 0, 0, "\xFF", 1, in->ext, NULL, o, 0);
		return;
	}
	if (o->kind != OP_MEM || o->base >= 0 || o->index >= 0 || o->seg >= 0) error_at(A->ctx, "invalid branch target");
	as_prefixes(A, 0, NULL);
	if (in->kind == AI_CALL) {
		C8(c,0xE8);
		as_pcrel(A, &o->val, rs);
		A->fix[A->nfix - 1].plt = 1;
		return;
	}
	as_jmpidx(A);
	int idx = A->nj - 1;
	if (!A->longj[idx] && as_near(A, &o->val, as_pos(A) + 2)) {
		C8(c,(uint8_t)(in->kind == AI_JMP ? 0xEB : 0x70 + in->ext));
		if (A->njmp == A->capjmp) {
			A->capjmp = A->capjmp ? A->capjmp * 2 : 64;
			A->jmp = (AsJmp *)realloc(A->jmp, sizeof(AsJmp) * A->capjmp);
			if (!A->jmp) { perror("realloc"); exit(1); }
		}
		AsJmp *j = &A->jmp[A->njmp++];
		j->sect = A->sect; j->pos = as_pos(A); j->idx = idx; j->val = o->val;
		C8(c,0);
		return;
	}
	A->longj[idx] = 1;
	if (in->kind == AI_JMP) C8(c,0xE9);
	else { C8(c,0x0F); C8(c,(uint8_t)(0x80 + in->ext)); }
	as_pcrel(A, &o->val, rs);
	A->fix[A->nfix - 1].plt = 1;
}

static void as_movd(As *A, int q, AsOp *s, AsOp *d) {
	if (as_isreg(d, RC_XMM)) {
		if (as_isreg(s, RC_XMM) || (q && s->kind == OP_MEM)) { as_rm(A, 0, 0xF3, "\x0F\x7E", 2, 0, d, s, 0); return; }
		int sz = q ? 8 : as_isreg(s, RC_GP) ? s->size : 4;
		if (sz < 4 || (s->kind != OP_MEM && !as_isreg(s, RC_GP))) error_at(A->ctx, "invalid operands for movd/movq");
		as_rm(A, sz == 8 ? 8 : 0, 0x66, "\x0F\x6E", 2, 0, d, s, 0);
		return;
	}
	if (!as_isreg(s, RC_XMM)) error_at(A->ctx, "invalid operands for movd/movq");
	if (q && d->kind == OP_MEM) { as_rm(A, 0, 0x66, "\x0F\xD6", 2, 0, s, d, 0); return; }
	int sz = q ? 8 : as_isreg(d, RC_GP) ? d->size : 4;
	if (sz < 4 || (d->kind != OP_MEM && !as_isreg(d, RC_GP))) error_at(A->ctx, "invalid operands for movd/movq");
	as_rm(A, sz == 8 ? 8 : 0, 0x66, "\x0F\x7E", 2, 0, s, d, 0);
}

static void as_moffs(As *A, int sz, int op, AsOp *m) {
	as_prefixes(A, sz, m);
	as_rex(A, sz == 8, 0, 0, 0, 0, 0);
	C8(&A->c, (uint8_t)op);
	as_imm(A, &m->val, A->mode == 16 ? 2 : 4, 0);
}

static void as_mov(As *A, int suf, AsOp *o, int n, char *name) {
	Code *c = &A->c;
	AsOp *s = &o[0], *d = &o[1];
	as_nops(A, name, n, 2);
	if (as_isreg(s, RC_XMM) || as_isreg(d, RC_XMM)) {
		if (suf != 8) error_at(A->ctx, "use movd or movq for xmm registers");
		as_movd(A, 1, s, d);
		return;
	}
	if (as_isreg(s, RC_SEG) || as_isreg(d, RC_SEG)) {
		AsOp *sr = as_isreg(s, RC_SEG) ? s : d, *x = sr == s ? d : s;
		if (x->kind == OP_IMM || as_isreg(x, RC_SEG)) error_at(A->ctx, "invalid segment register move");
		if (sr == d && sr->reg == 1) error_at(A->ctx, "cannot move to %%cs");
		int sz = sr == s && as_isreg(x, RC_GP) && x->size != 8 ? x->size : 0;
		as_rm(A, sz, 0, sr == s ? "\x8C" : "\x8E", 1, 0, sr, x, 0);
		return;
	}
	if (as_isreg(s, RC_CR) || as_isreg(d, RC_CR) || as_isreg(s, RC_DR) || as_isreg(d, RC_DR)) {
		int to = as_isreg(d, RC_CR) || as_isreg(d, RC_DR);
		AsOp *cr = to ? d : s, *g = to ? s : d;
		if (!as_isreg(g, RC_GP) || g->size != (A->mode == 64 ? 8 : 4)) error_at(A->ctx, "control and debug registers move to and from %s registers", A->mode == 64 ? "64-bit" : "32-bit");
		char op[2] = { 0x0F, (char)((cr->rc == RC_CR ? 0x20 : 0x21) + (to ? 2 : 0)) };
		as_rm(A, 0, 0, op, 2, 0, cr, g, 0);
		return;
	}
	int sz = as_sz(A, suf, o, 2);
	if (d->kind == OP_IMM) error_at(A->ctx, "invalid destination for mov");
	if (s->kind == OP_IMM) {
		if (d->kind == OP_REG) {
			if (sz == 8) {
				if (as_known(&s->val) && (s->val.v < -2147483647LL - 1 || s->val.v > 2147483647LL)) {
					as_opreg(A, 8, 0xB8, d);
					as_imm(A, &s->val, 8, 0);
					return;
				}
				as_rm(A, 8, 0, "\xC7", 1, 0, NULL, d, 4);
				as_imm(A, &s->val, 4, 1);
				return;
			}
			as_opreg(A, sz, sz == 1 ? 0xB0 : 0xB8, d);
			as_imm(A, &s->val, sz, 0);
			return;
		}
		int isz = sz == 8 ? 4 : sz;
		as_rm(A, sz, 0, sz == 1 ? "\xC6" : "\xC7", 1, 0, NULL, d, isz);
		as_imm(A, &s->val, isz, sz == 8);
		return;
	}
	if (s->kind == OP_REG) {
		if (A->mode != 64 && as_isacc(s) && as_isabs(d)) { as_moffs(A, sz, sz == 1 ? 0xA2 : 0xA3, d); return; }
		as_rm(A, sz, 0, sz == 1 ? "\x88" : "\x89", 1, 0, s, d, 0);
		return;
	}
	if (d->kind != OP_REG) error_at(A->ctx, "mov cannot move memory to memory");
	if (A->mode != 64 && as_isacc(d) && as_isabs(s)) { as_moffs(A, sz, sz == 1 ? 0xA0 : 0xA1, s); return; }
	as_rm(A, sz, 0, sz == 1 ? "\x8A" : "\x8B", 1, 0, d, s, 0);
	(void)c;
}

static void as_alu(As *A, const AsIns *in, int sz, AsOp *s, AsOp *d) {
	Code *c = &A->c;
	char op[1];
	if (d->kind == OP_IMM) error_at(A->ctx, "invalid destination operand");
	if (s->kind == OP_IMM) {
		int isz = sz == 8 ? 4 : sz;
		AsVal t = s->val;
		if (as_known(&t)) {
			as_check(A, t.v, isz, sz == 8);
			if (sz < 8) t.v = as_trunc(t.v, sz);
		}
		if (sz > 1 && as_fits8(&t, sz)) {
			as_rm(A, sz, 0, "\x83", 1, in->ext, NULL, d, 1);
			as_imm(A, &t, 1, 1);
		} else if (as_isacc(d)) {
			as_head(A, sz, NULL);
			C8(c,(uint8_t)(in->ext * 8 + (sz == 1 ? 4 : 5)));
			as_imm(A, &t, isz, sz == 8 || as_known(&t));
		} else {
			as_rm(A, sz, 0, sz == 1 ? "\x80" : "\x81", 1, in->ext, NULL, d, isz);
			as_imm(A, &t, isz, sz == 8 || as_known(&t));
		}
		return;
	}
	if (s->kind == OP_REG) {
		op[0] = (char)(in->ext * 8 + (sz == 1 ? 0 : 1));
		as_rm(A, sz, 0, op, 1, 0, s, d, 0);
		return;
	}
	if (d->kind != OP_REG) error_at(A->ctx, "too many memory operands");
	op[0] = (char)(in->ext * 8 + (sz == 1 ? 2 : 3));
	as_rm(A, sz, 0, op, 1, 0, d, s, 0);
}

static void as_shift(As *A, const AsIns *in, int sz, AsOp *o, int n) {
	AsOp *d = &o[n - 1];
	if (n == 1) { as_rm(A, sz, 0, sz == 1 ? "\xD0" : "\xD1", 1, in->ext, NULL, d, 0); return; }
	if (as_isreg(&o[0], RC_GP) && o[0].reg == RCX && o[0].size == 1) {
		as_rm(A, sz, 0, sz == 1 ? "\xD2" : "\xD3", 1, in->ext, NULL, d, 0);
		return;
	}
	if (o[0].kind != OP_IMM) error_at(A->ctx, "shift count must be an immediate or %%cl");
	if (as_known(&o[0].val) && o[0].val.v == 1) { as_rm(A, sz, 0, sz == 1 ? "\xD0" : "\xD1", 1, in->ext, NULL, d, 0); return; }
	as_rm(A, sz, 0, sz == 1 ? "\xC0" : "\xC1", 1, in->ext, NULL, d, 1);
	as_imm(A, &o[0].val, 1, 0);
}

static void as_pushpop(As *A, const AsIns *in, int suf, AsOp *o, int n, char *name) {
	Code *c = &A->c;
	int push = in->kind == AI_PUSH;
	int def = as_default(A);
	as_nops(A, name, n, 1);
	if (as_isreg(o, RC_SEG)) {
		if (o->reg >= 4) { C8(c,0x0F); C8(c,(uint8_t)(0xA0 + (o->reg - 4) * 8 + !push)); return; }
		if (A->mode == 64 || (!push && o->reg == 1)) error_at(A->ctx, "invalid segment register for %s", name);
		as_prefixes(A, 0, NULL);
		C8(c,(uint8_t)(o->reg * 8 + 6 + !push));
		return;
	}
	int sz = suf ? suf : as_isreg(o, RC_GP) ? o->size : def;
	if (as_isreg(o, RC_GP) && o->size != sz) error_at(A->ctx, "suffix does not match the register size");
	if ((sz != 2 && sz != def && !(A->mode == 16 && sz == 4)) || (A->mode == 64 && sz == 4)) error_at(A->ctx, "invalid operand size for %s", name);
	int psz = sz == def ? 0 : sz;
	if (o->kind == OP_REG) {
		if (o->rc != RC_GP) error_at(A->ctx, "invalid register for %s", name);
		as_opreg(A, psz, push ? 0x50 : 0x58, o);
		return;
	}
	if (o->kind == OP_IMM) {
		if (!push) error_at(A->ctx, "cannot pop into an immediate");
		as_prefixes(A, psz, NULL);
		if (as_fits8(&o->val, 8)) { C8(c,0x6A); as_imm(A, &o->val, 1, 1); }
		else { C8(c,0x68); as_imm(A, &o->val, sz == 2 ? 2 : 4, sz == 8); }
		return;
	}
	as_rm(A, psz, 0, push ? "\xFF" : "\x8F", 1, push ? 6 : 0, NULL, o, 0);
}

static void as_inout(As *A, const AsIns *in, int suf, AsOp *o, int n) {
	Code *c = &A->c;
	int isin = in->kind == AI_IN;
	AsOp *port, *acc = NULL;
	if (n == 2) { port = isin ? &o[0] : &o[1]; acc = isin ? &o[1] : &o[0]; }
	else if (n == 1) port = &o[0];
	else error_at(A->ctx, "'%s' needs a port", isin ? "in" : "out");
	int sz = suf;
	if (acc) {
		if (!as_isacc(acc) || acc->size == 8) error_at(A->ctx, "the data operand must be %%al, %%ax or %%eax");
		if (sz && sz != acc->size) error_at(A->ctx, "suffix does not match the register size");
		sz = acc->size;
	}
	if (!sz || sz == 8) error_at(A->ctx, "operand size unknown; add a b, w or l suffix");
	if (port->kind == OP_IMM) {
		as_prefixes(A, sz == 1 ? 0 : sz, NULL);
		C8(c,(uint8_t)((isin ? 0xE4 : 0xE6) + (sz != 1)));
		as_imm(A, &port->val, 1, 0);
		return;
	}
	if (!as_isdx(port)) error_at(A->ctx, "the port must be an immediate or %%dx");
	as_prefixes(A, sz == 1 ? 0 : sz, NULL);
	C8(c,(uint8_t)((isin ? 0xEC : 0xEE) + (sz != 1)));
}

static void as_insn(As *A, char *name) {
	Ctx *ctx = A->ctx;
	Code *c = &A->c;
	int suf = 0, sz;
	const AsIns *in = as_find(name, &suf);
	AsOp o[3];
	int n = 0;
	char op[3];
	if (!in) error_at(ctx, "unknown instruction '%s'", name);
	if (A->sect == SEC_BSS) error_at(ctx, "instruction in .bss");
	if (in->kind == AI_PREFIX) {
		if (as_at_end(A)) { as_prefixes(A, 0, NULL); C8(c, in->op); return; }
		if (A->npend == 4 || !as_is(A, T_IDENT)) error_at(ctx, "expected an instruction after '%s'", name);
		A->pend[A->npend++] = in->op;
		char *next_name = A->tok.str;
		as_next(A);
		as_insn(A, next_name);
		return;
	}
	if (!as_at_end(A))
		for (;;) {
			if (n == 3) error_at(ctx, "too many operands");
			as_operand(A, &o[n++]);
			if (!as_is(A, T_COMMA)) break;
			as_next(A);
		}
	switch (in->kind) {
	case AI_ALU:
		as_nops(A, name, n, 2);
		as_alu(A, in, as_sz(A, suf, o, 2), &o[0], &o[1]);
		break;
	case AI_SHIFT:
		if (n != 1 && n != 2) as_nops(A, name, n, 2);
		as_shift(A, in, as_sz(A, suf, &o[n-1], 1), o, n);
		break;
	case AI_UNARY:
		as_nops(A, name, n, 1);
		sz = as_sz(A, suf, o, 1);
		as_rm(A, sz, 0, sz == 1 ? "\xF6" : "\xF7", 1, in->ext, NULL, &o[0], 0);
		break;
	case AI_INCDEC:
		as_nops(A, name, n, 1);
		sz = as_sz(A, suf, o, 1);
		if (A->mode != 64 && as_isreg(&o[0], RC_GP) && sz != 1) as_opreg(A, sz, in->ext ? 0x48 : 0x40, &o[0]);
		else as_rm(A, sz, 0, sz == 1 ? "\xFE" : "\xFF", 1, in->ext, NULL, &o[0], 0);
		break;
	case AI_IMUL:
		if (n == 1) {
			sz = as_sz(A, suf, o, 1);
			as_rm(A, sz, 0, sz == 1 ? "\xF6" : "\xF7", 1, 5, NULL, &o[0], 0);
			break;
		}
		if (!as_isreg(&o[n-1], RC_GP)) error_at(ctx, "the destination of imul must be a register");
		sz = as_sz(A, suf, &o[n-1], 1);
		if (sz == 1) error_at(ctx, "imul with two or three operands has no byte form");
		if (o[0].kind == OP_IMM) {
			AsOp *src = n == 3 ? &o[1] : &o[n-1];
			AsVal t = o[0].val;
			if (as_known(&t)) { as_check(A, t.v, sz == 8 ? 4 : sz, sz == 8); if (sz < 8) t.v = as_trunc(t.v, sz); }
			if (as_fits8(&t, sz)) { as_rm(A, sz, 0, "\x6B", 1, 0, &o[n-1], src, 1); as_imm(A, &t, 1, 1); }
			else { as_rm(A, sz, 0, "\x69", 1, 0, &o[n-1], src, sz == 8 ? 4 : sz); as_imm(A, &t, sz == 8 ? 4 : sz, 1); }
			break;
		}
		as_nops(A, name, n, 2);
		as_rm(A, sz, 0, "\x0F\xAF", 2, 0, &o[1], &o[0], 0);
		break;
	case AI_MOV:
		as_mov(A, suf, o, n, name);
		break;
	case AI_MOVABS:
		as_nops(A, name, n, 2);
		sz = as_sz(A, suf, o, 2);
		if (A->mode != 64) error_at(ctx, "movabs needs 64-bit code");
		if (o[0].kind == OP_IMM && as_isreg(&o[1], RC_GP) && sz == 8) {
			as_opreg(A, 8, 0xB8, &o[1]);
			as_imm(A, &o[0].val, 8, 0);
		} else if (as_isacc(&o[1]) && as_isabs(&o[0])) {
			as_prefixes(A, sz, &o[0]); as_rex(A, sz == 8, 0, 0, 0, 0, 0);
			C8(c,(uint8_t)(sz == 1 ? 0xA0 : 0xA1)); as_imm(A, &o[0].val, 8, 0);
		} else if (as_isacc(&o[0]) && as_isabs(&o[1])) {
			as_prefixes(A, sz, &o[1]); as_rex(A, sz == 8, 0, 0, 0, 0, 0);
			C8(c,(uint8_t)(sz == 1 ? 0xA2 : 0xA3)); as_imm(A, &o[1].val, 8, 0);
		} else error_at(ctx, "invalid operands for movabs");
		break;
	case AI_LEA:
		as_nops(A, name, n, 2);
		if (o[0].kind != OP_MEM || !as_isreg(&o[1], RC_GP)) error_at(ctx, "lea takes a memory operand and a register");
		sz = as_sz(A, suf, &o[1], 1);
		as_rm(A, sz, 0, "\x8D", 1, 0, &o[1], &o[0], 0);
		break;
	case AI_TEST:
		as_nops(A, name, n, 2);
		sz = as_sz(A, suf, o, 2);
		if (o[0].kind == OP_IMM) {
			int isz = sz == 8 ? 4 : sz;
			if (as_isacc(&o[1])) { as_head(A, sz, NULL); C8(c,(uint8_t)(sz == 1 ? 0xA8 : 0xA9)); }
			else as_rm(A, sz, 0, sz == 1 ? "\xF6" : "\xF7", 1, 0, NULL, &o[1], isz);
			as_imm(A, &o[0].val, isz, sz == 8);
		} else if (o[0].kind == OP_REG) as_rm(A, sz, 0, sz == 1 ? "\x84" : "\x85", 1, 0, &o[0], &o[1], 0);
		else if (o[1].kind == OP_REG) as_rm(A, sz, 0, sz == 1 ? "\x84" : "\x85", 1, 0, &o[1], &o[0], 0);
		else error_at(ctx, "invalid operands for test");
		break;
	case AI_XCHG:
		as_nops(A, name, n, 2);
		sz = as_sz(A, suf, o, 2);
		if (sz > 1 && o[0].kind == OP_REG && o[1].kind == OP_REG && (as_isacc(&o[0]) || as_isacc(&o[1]))
			&& !(A->mode == 64 && sz == 4 && as_isacc(&o[0]) && as_isacc(&o[1]))) {
			as_opreg(A, sz == 8 && as_isacc(&o[0]) && as_isacc(&o[1]) ? 0 : sz, 0x90, as_isacc(&o[0]) ? &o[1] : &o[0]);
		} else if (o[0].kind == OP_REG) as_rm(A, sz, 0, sz == 1 ? "\x86" : "\x87", 1, 0, &o[0], &o[1], 0);
		else if (o[1].kind == OP_REG) as_rm(A, sz, 0, sz == 1 ? "\x86" : "\x87", 1, 0, &o[1], &o[0], 0);
		else error_at(ctx, "invalid operands for xchg");
		break;
	case AI_MOVX: {
		as_nops(A, name, n, 2);
		if (!as_isreg(&o[1], RC_GP)) error_at(ctx, "the destination of %s must be a register", name);
		int ssz = in->ext ? in->ext : as_isreg(&o[0], RC_GP) ? o[0].size : 0;
		int dsz = in->sz ? in->sz : o[1].size;
		if (!ssz) error_at(ctx, "source size unknown; use movzbl, movsbl and the like");
		if (as_isreg(&o[0], RC_GP) && o[0].size != ssz) error_at(ctx, "source register has the wrong size");
		if (o[1].size != dsz || dsz <= ssz) error_at(ctx, "invalid destination size for %s", name);
		if (in->op == 0x63) { as_rm(A, dsz, 0, "\x63", 1, 0, &o[1], &o[0], 0); break; }
		op[0] = 0x0F; op[1] = (char)(in->op + (!in->ext && ssz == 2));
		as_rm(A, dsz, 0, op, 2, 0, &o[1], &o[0], 0);
		break;
	}
	case AI_FIXED:
		as_nops(A, name, n, 0);
		as_head(A, in->sz, NULL);
		for (const char *p = in->code; *p; p++) C8(c, (uint8_t)*p);
		break;
	case AI_FIXSZ:
		as_nops(A, name, n, 0);
		sz = suf ? suf : A->mode == 16 ? 2 : 4;
		as_head(A, sz, NULL);
		for (const char *p = in->code; *p; p++) C8(c, (uint8_t)*p);
		break;
	case AI_STR:
		if (!suf) error_at(ctx, "string instructions need a b, w, l or q suffix");
		for (int i = 0; i < n; i++) if (o[i].kind != OP_MEM) error_at(ctx, "invalid operand for a string instruction");
		as_head(A, suf, NULL);
		C8(c,(uint8_t)(in->op + (suf != 1)));
		break;
	case AI_PUSH: case AI_POP:
		as_pushpop(A, in, suf, o, n, name);
		break;
	case AI_PUSHF: case AI_PUSHA:
		as_nops(A, name, n, 0);
		sz = suf ? suf : as_default(A);
		if (in->kind == AI_PUSHA && A->mode == 64) error_at(ctx, "%s is not valid in 64-bit code", name);
		if ((A->mode == 64 && sz == 4) || (A->mode != 64 && sz == 8) || sz == 1) error_at(ctx, "invalid operand size for %s", name);
		as_head(A, sz == as_default(A) ? 0 : sz, NULL);
		C8(c, in->op);
		break;
	case AI_JMP: case AI_CALL: case AI_JCC:
		as_branch(A, in, o, n, name);
		break;
	case AI_LOOP: {
		as_nops(A, name, n, 1);
		int da = as_default(A);
		int asz = in->sz ? in->sz : da;
		if ((A->mode == 64 && asz == 2) || (A->mode != 64 && asz == 8)) error_at(ctx, "%s is not valid in %d-bit code", name, A->mode);
		if (o[0].kind != OP_MEM || !as_isabs(&o[0])) error_at(ctx, "invalid branch target");
		as_prefixes(A, 0, NULL);
		if (asz != da) C8(c,0x67);
		C8(c, in->op);
		as_pcrel(A, &o[0].val, 1);
		break;
	}
	case AI_LJMP:
		if (n == 1) {
			if (!o[0].star || o[0].kind != OP_MEM) error_at(ctx, "%s takes $segment, $offset or *memory", name);
			as_rm(A, suf == 2 || suf == 4 ? suf : 0, 0, "\xFF", 1, in->ext, NULL, &o[0], 0);
			break;
		}
		as_nops(A, name, n, 2);
		if (A->mode == 64) error_at(ctx, "%s $segment, $offset is not valid in 64-bit code", name);
		if (o[0].kind != OP_IMM || o[1].kind != OP_IMM) error_at(ctx, "%s takes $segment, $offset", name);
		sz = suf ? suf : A->mode == 16 ? 2 : 4;
		as_head(A, sz, NULL);
		C8(c, in->op);
		as_imm(A, &o[1].val, sz, 0);
		as_imm(A, &o[0].val, 2, 0);
		break;
	case AI_RET:
		if (n > 1 || (n == 1 && o[0].kind != OP_IMM)) error_at(ctx, "%s takes an optional immediate", name);
		if (in->flags & F_FAR) sz = suf ? suf : A->mode == 16 ? 2 : 4;
		else {
			sz = suf ? suf : as_default(A);
			if ((A->mode == 64 && sz == 4) || (A->mode != 64 && sz == 8)) error_at(ctx, "invalid operand size for %s", name);
			if (sz == as_default(A)) sz = 0;
		}
		as_head(A, sz, NULL);
		C8(c,(uint8_t)(in->op - (n == 1)));
		if (n == 1) as_imm(A, &o[0].val, 2, 0);
		break;
	case AI_INT:
		as_nops(A, name, n, 1);
		if (o[0].kind != OP_IMM) error_at(ctx, "int takes an immediate");
		as_prefixes(A, 0, NULL);
		if (as_known(&o[0].val) && o[0].val.v == 3) { C8(c,0xCC); break; }
		C8(c,0xCD);
		as_imm(A, &o[0].val, 1, 0);
		break;
	case AI_IN: case AI_OUT:
		as_inout(A, in, suf, o, n);
		break;
	case AI_SETCC:
		as_nops(A, name, n, 1);
		if (o[0].kind == OP_IMM || (o[0].kind == OP_REG && (o[0].rc != RC_GP || o[0].size != 1))) error_at(ctx, "%s takes a byte register or memory", name);
		op[0] = 0x0F; op[1] = (char)(0x90 + in->ext);
		as_rm(A, 0, 0, op, 2, 0, NULL, &o[0], 0);
		break;
	case AI_CMOV:
		as_nops(A, name, n, 2);
		if (!as_isreg(&o[1], RC_GP)) error_at(ctx, "the destination of %s must be a register", name);
		sz = as_sz(A, suf, o, 2);
		if (sz == 1) error_at(ctx, "%s has no byte form", name);
		op[0] = 0x0F; op[1] = (char)(0x40 + in->ext);
		as_rm(A, sz, 0, op, 2, 0, &o[1], &o[0], 0);
		break;
	case AI_0F: {
		as_nops(A, name, n, 2);
		AsOp *r = in->flags & F_REGSRC ? &o[0] : &o[1], *m = in->flags & F_REGSRC ? &o[1] : &o[0];
		if (!as_isreg(r, RC_GP) || m->kind == OP_IMM) error_at(ctx, "invalid operands for %s", name);
		sz = as_sz(A, suf, o, 2);
		if (sz == 1 && !(in->flags & F_BYTE)) error_at(ctx, "%s has no byte form", name);
		op[0] = 0x0F; op[1] = (char)(in->op - (sz == 1));
		as_rm(A, sz, in->code ? (uint8_t)in->code[0] : 0, op, 2, 0, r, m, 0);
		break;
	}
	case AI_BT:
		as_nops(A, name, n, 2);
		sz = as_sz(A, suf, o, 2);
		if (sz == 1) error_at(ctx, "%s has no byte form", name);
		if (o[0].kind == OP_IMM) {
			as_rm(A, sz, 0, "\x0F\xBA", 2, in->ext, NULL, &o[1], 1);
			as_imm(A, &o[0].val, 1, 0);
		} else {
			if (o[0].kind != OP_REG) error_at(ctx, "invalid operands for %s", name);
			op[0] = 0x0F; op[1] = (char)in->op;
			as_rm(A, sz, 0, op, 2, 0, &o[0], &o[1], 0);
		}
		break;
	case AI_BSWAP:
		as_nops(A, name, n, 1);
		sz = as_sz(A, suf, o, 1);
		if (!as_isreg(&o[0], RC_GP) || sz < 4) error_at(ctx, "bswap takes a 32-bit or 64-bit register");
		as_head(A, sz, &o[0]);
		C8(c,0x0F); C8(c,(uint8_t)(0xC8 + (o[0].reg & 7)));
		break;
	case AI_SHD:
		as_nops(A, name, n, 3);
		if (!as_isreg(&o[1], RC_GP)) error_at(ctx, "invalid operands for %s", name);
		sz = as_sz(A, suf, &o[1], 2);
		if (sz == 1) error_at(ctx, "%s has no byte form", name);
		op[0] = 0x0F;
		if (o[0].kind == OP_IMM) {
			op[1] = (char)in->op;
			as_rm(A, sz, 0, op, 2, 0, &o[1], &o[2], 1);
			as_imm(A, &o[0].val, 1, 0);
		} else {
			if (!as_isreg(&o[0], RC_GP) || o[0].reg != RCX || o[0].size != 1) error_at(ctx, "the count of %s must be an immediate or %%cl", name);
			op[1] = (char)(in->op + 1);
			as_rm(A, sz, 0, op, 2, 0, &o[1], &o[2], 0);
		}
		break;
	case AI_GRP:
		as_nops(A, name, n, 1);
		if (o[0].kind == OP_IMM || (o[0].kind == OP_REG && ((in->flags & F_MEM) || o[0].rc != RC_GP)))
			error_at(ctx, "invalid operand for %s", name);
		sz = 0;
		if (in->flags & F_W) sz = 8;
		else if ((in->flags & F_WQ) && suf == 8) sz = 8;
		else if ((in->flags & F_DT) && suf && suf != 8) sz = suf == (A->mode == 16 ? 2 : 4) ? 0 : suf == 2 ? 2 : 4;
		else if (as_isreg(&o[0], RC_GP) && (o[0].size == 8 || in->sz)) sz = o[0].size;
		as_rm(A, sz, 0, in->code, 2, in->ext, NULL, &o[0], 0);
		break;
	case AI_NOP:
		if (n == 0) {
			as_head(A, suf == 2 ? 2 : 0, NULL);
			C8(c,0x90);
			break;
		}
		as_nops(A, name, n, 1);
		sz = as_sz(A, suf, o, 1);
		if (sz == 1) error_at(ctx, "nop has no byte form");
		as_rm(A, sz, 0, "\x0F\x1F", 2, 0, NULL, &o[0], 0);
		break;
	case AI_SSE: {
		if (n == 0 && strcmp(name, "movsd") == 0) {
			as_head(A, 4, NULL);
			C8(c,0xA5);
			break;
		}
		as_nops(A, name, n, 2);
		int mp = in->code ? (uint8_t)in->code[0] : 0;
		op[0] = 0x0F;
		if (as_isreg(&o[1], RC_XMM) && (as_isreg(&o[0], RC_XMM) || o[0].kind == OP_MEM)) {
			op[1] = (char)in->op;
			as_rm(A, 0, mp, op, 2, 0, &o[1], &o[0], 0);
		} else if (in->ext && as_isreg(&o[0], RC_XMM) && o[1].kind == OP_MEM) {
			op[1] = (char)in->ext;
			as_rm(A, 0, mp, op, 2, 0, &o[0], &o[1], 0);
		} else error_at(ctx, "invalid operands for %s", name);
		break;
	}
	case AI_MOVD:
		as_nops(A, name, n, 2);
		as_movd(A, 0, &o[0], &o[1]);
		break;
	case AI_CVTI:
		as_nops(A, name, n, 2);
		if (!as_isreg(&o[1], RC_XMM) || (o[0].kind != OP_MEM && !as_isreg(&o[0], RC_GP))) error_at(ctx, "invalid operands for %s", name);
		sz = as_sz(A, suf, o, 1);
		if (sz < 4) error_at(ctx, "invalid operand size for %s", name);
		op[0] = 0x0F; op[1] = (char)in->op;
		as_rm(A, sz == 8 ? 8 : 0, (uint8_t)in->code[0], op, 2, 0, &o[1], &o[0], 0);
		break;
	case AI_CVTF:
		as_nops(A, name, n, 2);
		if (!as_isreg(&o[1], RC_GP) || (o[0].kind != OP_MEM && !as_isreg(&o[0], RC_XMM))) error_at(ctx, "invalid operands for %s", name);
		sz = as_sz(A, suf, &o[1], 1);
		if (sz < 4) error_at(ctx, "invalid operand size for %s", name);
		op[0] = 0x0F; op[1] = (char)in->op;
		as_rm(A, sz == 8 ? 8 : 0, (uint8_t)in->code[0], op, 2, 0, &o[1], &o[0], 0);
		break;
	}
	if (A->npend) error_at(ctx, "prefix cannot be used with '%s'", name);
}

static int as_is_code(Ctx *ctx, int sect) {
	return sect == SEC_TEXT || (sect >= SEC_NAMED && ctx->secs[sect - SEC_NAMED].kind == SK_CODE);
}

static void as_flush(As *A) {
	if (A->sect != SEC_BSS) *sec_buf(A->ctx, A->sect) = A->c.b;
}

static void as_enter(As *A, int sect) {
	as_flush(A);
	A->prev = A->sect;
	A->sect = sect;
	if (sect != SEC_BSS) A->c.b = *sec_buf(A->ctx, sect);
	else if (A->ctx->track && !A->snip) {
		if (A->nbm == A->capbm) {
			A->capbm = A->capbm ? A->capbm * 2 : 16;
			A->bpos = (long *)realloc(A->bpos, sizeof(long) * A->capbm);
			A->bname = (char **)realloc(A->bname, sizeof(char *) * A->capbm);
			if (!A->bpos || !A->bname) { perror("realloc"); exit(1); }
		}
		A->bpos[A->nbm] = A->ctx->bss_size;
		A->bname[A->nbm++] = A->bss_name ? A->bss_name : intern(".bss");
	}
}

static size_t as_base(As *A, int sect) {
	if (sect == SEC_BSS) return (size_t)A->bss_base;
	return sect < A->nbase ? A->base[sect] : 0;
}

static char *as_name(As *A) {
	if (!as_is(A, T_IDENT)) error_at(A->ctx, "expected a symbol name");
	char *n = A->tok.str;
	as_next(A);
	return n;
}

static void as_skip_line(As *A) {
	while (!as_at_end(A)) as_next(A);
}

static int as_section(As *A) {
	Ctx *ctx = A->ctx;
	char *name;
	if (as_is(A, T_STR)) { name = intern_str(A->tok.str, A->tok.len)->s; as_next(A); }
	else {
		Buf b; buf_init(&b);
		name = as_name(A);
		buf_append(&b, name, strlen(name));
		while (!as_at_end(A) && A->tok.kind != T_COMMA && !A->tok.space) {
			tok_spell(&b, &A->tok);
			as_next(A);
		}
		name = intern_str((char *)b.data, (int)b.len)->s;
		free(b.data);
	}
	int kind = strncmp(name, ".text", 5) == 0 ? SK_CODE : strncmp(name, ".rodata", 7) == 0 ? SK_RO :
		!strncmp(name, ".note", 5) || !strncmp(name, ".debug", 6) || !strcmp(name, ".comment") ? SK_NOALLOC : SK_DATA;
	int nobits = strncmp(name, ".bss", 4) == 0;
	if (as_is(A, T_COMMA)) {
		as_next(A);
		if (!as_is(A, T_STR)) error_at(ctx, "expected section flags");
		const char *f = A->tok.str;
		kind = !strchr(f, 'a') ? SK_NOALLOC : strchr(f, 'x') ? SK_CODE : strchr(f, 'w') ? SK_DATA : SK_RO;
		as_next(A);
		if (as_is(A, T_COMMA)) {
			as_next(A);
			if (as_is(A, T_AT) || as_is(A, '%')) as_next(A);
			if (as_is(A, T_IDENT) || as_is(A, T_STR)) {
				nobits = strcmp(A->tok.str, "nobits") == 0;
				as_next(A);
			}
		}
		as_skip_line(A);
	}
	if (nobits && kind != SK_CODE) { A->bss_name = name; return SEC_BSS; }
	if (kind == SK_RO && strcmp(name, ".rodata") == 0) return SEC_RODATA;
	return section_index(ctx, name, kind);
}

static void as_align(As *A, long long a, int has_fill, long long fill, long long max) {
	if (a <= 0 || (a & (a - 1)) || a > 0x10000000) error_at(A->ctx, "alignment must be a power of 2");
	raise_align(A->ctx, A->sect, (int)a);
	long pos = as_pos(A);
	long pad = (long)align_up((size_t)pos, (size_t)a) - pos;
	if (max > 0 && pad > max) return;
	int f = has_fill ? (int)(fill & 0xFF) : as_is_code(A->ctx, A->sect) ? 0x90 : 0;
	while (pad-- > 0) as_byte(A, f);
}

static void as_fill(As *A, long long count, int size, long long value) {
	if (count < 0) error_at(A->ctx, "negative repeat count");
	if (size < 0 || size > 8) error_at(A->ctx, "fill size must be 0 to 8");
	for (long long i = 0; i < count; i++)
		for (int k = 0; k < size; k++) as_byte(A, k < 4 ? (int)(((unsigned long long)value >> (8 * k)) & 0xFF) : 0);
}

static void as_data(As *A, int size) {
	for (;;) {
		if (size == 1 && as_is(A, T_STR)) {
			for (int i = 0; i < A->tok.len; i++) as_byte(A, (uint8_t)A->tok.str[i]);
			as_next(A);
		} else {
			AsVal v = as_expr(A);
			as_imm(A, &v, size, 0);
		}
		if (!as_is(A, T_COMMA)) return;
		as_next(A);
	}
}

static void as_ascii(As *A, int zero) {
	for (;;) {
		if (!as_is(A, T_STR)) error_at(A->ctx, "expected a string");
		while (as_is(A, T_STR)) {
			for (int i = 0; i < A->tok.len; i++) as_byte(A, (uint8_t)A->tok.str[i]);
			as_next(A);
		}
		if (zero) as_byte(A, 0);
		if (!as_is(A, T_COMMA)) return;
		as_next(A);
	}
}

static void as_org(As *A, AsVal v, int fill) {
	long target;
	if (as_known(&v)) target = (long)as_base(A, A->sect) + (long)v.v;
	else if (v.add && !v.sub && !v.t && as_defined(A, v.add) && !v.add->is_abs && v.add->sect == A->sect) target = v.add->off + (long)v.v;
	else { error_at(A->ctx, "the location counter can only be set to an address in this section"); return; }
	if (target < as_pos(A)) error_at(A->ctx, "cannot move the location counter backwards");
	while (as_pos(A) < target) as_byte(A, fill);
}

static void as_set(As *A, char *name) {
	AsVal v = as_expr(A);
	if (name == id_dot) { as_org(A, v, 0); return; }
	AsSym *s = isdigit((unsigned char)name[0]) ? NULL : as_sym(A, name);
	if (!s) error_at(A->ctx, "invalid symbol name '%s'", name);
	if (as_defined(A, s) && !s->is_abs) error_at(A->ctx, "symbol '%s' is already defined", name);
	if (as_known(&v)) {
		s->is_abs = 1;
		s->val = v.v;
	} else if (v.add && !v.sub && as_defined(A, v.add) && !v.add->is_abs) {
		s->is_abs = 0;
		s->sect = v.add->sect;
		s->off = v.add->off + (long)v.v;
		if (!s->type) s->type = v.add->type;
	} else error_at(A->ctx, "'%s' must be set to a constant or to a defined symbol plus a constant", name);
	s->pass = A->pass;
}

static void as_syms(As *A, int what) {
	for (;;) {
		AsSym *s = as_sym(A, as_name(A));
		if (what == 1) s->global = 1;
		else if (what == 2) s->local = 1;
		else if (what == 3) s->weak = 1;
		if (!as_is(A, T_COMMA)) return;
		as_next(A);
	}
}

static void as_comm(As *A, int global) {
	AsSym *s = as_sym(A, as_name(A));
	as_expect(A, T_COMMA);
	long long size = as_absexpr(A), al = 1;
	if (as_is(A, T_COMMA)) { as_next(A); al = as_absexpr(A); }
	else while (al < 16 && al * 2 <= size) al *= 2;
	if (size < 0) error_at(A->ctx, "negative size");
	if (global) s->global = 1;
	else s->local = 1;
	if (as_defined(A, s)) return;
	int save = A->sect, prev = A->prev;
	char *bn = A->bss_name;
	A->bss_name = NULL;
	as_enter(A, SEC_BSS);
	as_align(A, al, 0, 0, 0);
	as_define(A, s);
	s->size = (long)size;
	s->has_size = 1;
	A->ctx->bss_size += (long)size;
	A->bss_name = bn;
	as_enter(A, save);
	A->prev = prev;
}

static void as_incbin(As *A) {
	if (!as_is(A, T_STR)) error_at(A->ctx, "expected a file name");
	char path[2048];
	const char *name = A->tok.str;
	snprintf(path, sizeof(path), "%s", name);
	if (name[0] != '/' && !file_readable(path)) snprintf(path, sizeof(path), "%s/%s", dir_of(A->tok.file), name);
	as_next(A);
	long long skip = 0, count = -1;
	if (as_is(A, T_COMMA)) { as_next(A); skip = as_absexpr(A); }
	if (as_is(A, T_COMMA)) { as_next(A); count = as_absexpr(A); }
	FILE *f = fopen(path, "rb");
	if (!f) error_at(A->ctx, "cannot open '%s'", name);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	if (skip < 0 || skip > n) error_at(A->ctx, "invalid skip for .incbin");
	if (count < 0 || count > n - skip) count = n - skip;
	fseek(f, (long)skip, SEEK_SET);
	for (long long i = 0; i < count; i++) as_byte(A, fgetc(f) & 0xFF);
	fclose(f);
}

static void as_directive(As *A, char *d) {
	Ctx *ctx = A->ctx;
	long long a, b, c;
	if (!strcmp(d, ".text")) as_enter(A, SEC_TEXT);
	else if (!strcmp(d, ".data")) as_enter(A, SEC_DATA);
	else if (!strcmp(d, ".bss")) { A->bss_name = NULL; as_enter(A, SEC_BSS); }
	else if (!strcmp(d, ".rodata")) as_enter(A, SEC_RODATA);
	else if (!strcmp(d, ".section")) as_enter(A, as_section(A));
	else if (!strcmp(d, ".pushsection")) {
		if (A->nstack == 16) error_at(ctx, "sections nested too deeply");
		A->stack[A->nstack++] = A->sect;
		as_enter(A, as_section(A));
	} else if (!strcmp(d, ".popsection")) {
		if (!A->nstack) error_at(ctx, ".popsection without .pushsection");
		as_enter(A, A->stack[--A->nstack]);
	} else if (!strcmp(d, ".previous")) as_enter(A, A->prev);
	else if (!strcmp(d, ".code16")) A->mode = 16;
	else if (!strcmp(d, ".code32")) A->mode = 32;
	else if (!strcmp(d, ".code64")) A->mode = 64;
	else if (!strcmp(d, ".globl") || !strcmp(d, ".global")) as_syms(A, 1);
	else if (!strcmp(d, ".local")) as_syms(A, 2);
	else if (!strcmp(d, ".weak")) as_syms(A, 3);
	else if (!strcmp(d, ".hidden") || !strcmp(d, ".protected") || !strcmp(d, ".internal")) as_syms(A, 0);
	else if (!strcmp(d, ".type")) {
		AsSym *s = as_sym(A, as_name(A));
		as_expect(A, T_COMMA);
		if (as_is(A, T_AT) || as_is(A, '%')) as_next(A);
		if (!as_is(A, T_IDENT) && !as_is(A, T_STR)) error_at(ctx, "expected a symbol type");
		if (!strcmp(A->tok.str, "function")) s->type = 1;
		else if (!strcmp(A->tok.str, "object")) s->type = 2;
		as_next(A);
	} else if (!strcmp(d, ".size")) {
		AsSym *s = as_sym(A, as_name(A));
		as_expect(A, T_COMMA);
		AsVal v = as_expr(A);
		if (as_known(&v)) { s->size = (long)v.v; s->has_size = 1; }
	} else if (!strcmp(d, ".set") || !strcmp(d, ".equ")) {
		char *name = A->tok.str == id_dot ? id_dot : as_name(A);
		if (name == id_dot) as_next(A);
		as_expect(A, T_COMMA);
		as_set(A, name);
	} else if (!strcmp(d, ".byte")) as_data(A, 1);
	else if (!strcmp(d, ".word") || !strcmp(d, ".short") || !strcmp(d, ".hword") || !strcmp(d, ".value")) as_data(A, 2);
	else if (!strcmp(d, ".long") || !strcmp(d, ".int")) as_data(A, 4);
	else if (!strcmp(d, ".quad")) as_data(A, 8);
	else if (!strcmp(d, ".ascii")) as_ascii(A, 0);
	else if (!strcmp(d, ".asciz") || !strcmp(d, ".string")) as_ascii(A, 1);
	else if (!strcmp(d, ".zero")) as_fill(A, as_absexpr(A), 1, 0);
	else if (!strcmp(d, ".skip") || !strcmp(d, ".space")) {
		a = as_absexpr(A);
		b = 0;
		if (as_is(A, T_COMMA)) { as_next(A); b = as_absexpr(A); }
		as_fill(A, a, 1, b);
	} else if (!strcmp(d, ".fill")) {
		a = as_absexpr(A);
		b = 1; c = 0;
		if (as_is(A, T_COMMA)) { as_next(A); b = as_absexpr(A); }
		if (as_is(A, T_COMMA)) { as_next(A); c = as_absexpr(A); }
		as_fill(A, a, (int)b, c);
	} else if (!strcmp(d, ".align") || !strcmp(d, ".balign") || !strcmp(d, ".p2align")) {
		int has_fill = 0;
		a = as_absexpr(A);
		b = c = 0;
		if (as_is(A, T_COMMA)) {
			as_next(A);
			if (!as_is(A, T_COMMA)) { b = as_absexpr(A); has_fill = 1; }
			if (as_is(A, T_COMMA)) { as_next(A); c = as_absexpr(A); }
		}
		if (d[1] == 'p') {
			if (a < 0 || a > 28) error_at(ctx, "invalid alignment");
			a = 1LL << a;
		}
		as_align(A, a, has_fill, b, c);
	} else if (!strcmp(d, ".org")) {
		AsVal v = as_expr(A);
		b = 0;
		if (as_is(A, T_COMMA)) { as_next(A); b = as_absexpr(A); }
		as_org(A, v, (int)(b & 0xFF));
	} else if (!strcmp(d, ".comm")) as_comm(A, 1);
	else if (!strcmp(d, ".lcomm")) as_comm(A, 0);
	else if (!strcmp(d, ".incbin")) as_incbin(A);
	else if (!strcmp(d, ".end")) A->done = 1;
	else if (!strcmp(d, ".intel_syntax")) error_at(ctx, "only AT&T syntax is supported");
	else if (!strcmp(d, ".file") || !strcmp(d, ".ident") || !strcmp(d, ".loc") || !strcmp(d, ".att_syntax") ||
		!strncmp(d, ".cfi_", 5) || !strcmp(d, ".addrsig")) as_skip_line(A);
	else error_at(ctx, "unknown directive '%s'", d);
}

static int as_cond(As *A, char *d) {
	Ctx *ctx = A->ctx;
	int skip = A->ncond && A->cond[A->ncond - 1] != 1;
	if (!strncmp(d, ".if", 3)) {
		const char *k = d + 3;
		int v = 0;
		if (A->ncond == 16) error_at(ctx, "conditionals nested too deeply");
		if (skip) { as_skip_line(A); A->cond[A->ncond++] = 2; return 1; }
		if (!strcmp(k, "def") || !strcmp(k, "ndef") || !strcmp(k, "notdef")) {
			AsSym *s = as_sym(A, as_name(A));
			v = as_defined(A, s) == (k[0] == 'd');
		} else {
			long long x = as_absexpr(A);
			if (!*k) v = x != 0;
			else if (!strcmp(k, "eq")) v = x == 0;
			else if (!strcmp(k, "ne")) v = x != 0;
			else if (!strcmp(k, "gt")) v = x > 0;
			else if (!strcmp(k, "ge")) v = x >= 0;
			else if (!strcmp(k, "lt")) v = x < 0;
			else if (!strcmp(k, "le")) v = x <= 0;
			else error_at(ctx, "unknown directive '%s'", d);
		}
		A->cond[A->ncond++] = v;
		return 1;
	}
	if (strcmp(d, ".else") && strcmp(d, ".elseif") && strcmp(d, ".endif")) return 0;
	if (!A->ncond) error_at(ctx, "%s without .if", d);
	int *c = &A->cond[A->ncond - 1];
	if (!strcmp(d, ".endif")) A->ncond--;
	else if (!strcmp(d, ".else")) *c = *c == 0 ? 1 : 2;
	else if (*c == 0) *c = as_absexpr(A) != 0;
	else { as_skip_line(A); *c = 2; }
	return 1;
}

static int as_stmt(As *A) {
	Ctx *ctx = A->ctx;
	Tok t = A->tok;
	if (t.kind == T_IDENT && t.str[0] == '.' && !strncmp(t.str, ".if", 3)) { as_next(A); as_cond(A, t.str); return 0; }
	if (t.kind == T_IDENT && (!strcmp(t.str, ".else") || !strcmp(t.str, ".elseif") || !strcmp(t.str, ".endif"))) {
		as_next(A);
		as_cond(A, t.str);
		return 0;
	}
	if (A->ncond && A->cond[A->ncond - 1] != 1) {
		as_next(A);
		as_skip_line(A);
		return 0;
	}
	if (t.kind == T_NUM) {
		char b[32];
		as_next(A);
		if (!as_is(A, T_COLON)) error_at(ctx, "expected ':' after a local label");
		as_next(A);
		int n = snprintf(b, sizeof(b), "%lld", t.val);
		as_define(A, as_numlab(A, b, n, 0));
		return 1;
	}
	if (t.kind != T_IDENT) error_at(ctx, "expected an instruction, a directive or a label, got '%s'", tok_desc(&t));
	as_next(A);
	if (as_is(A, T_COLON)) {
		if (t.str == id_dot) error_at(ctx, "'.' cannot be a label");
		as_next(A);
		as_define(A, as_sym(A, t.str));
		return 1;
	}
	if (as_is(A, T_ASSIGN)) { as_next(A); as_set(A, t.str); return 0; }
	if (t.str[0] == '.') { as_directive(A, t.str); return 0; }
	as_insn(A, t.str);
	return 0;
}

static void as_restore(As *A) {
	Ctx *ctx = A->ctx;
	for (int s = 0; s < SEC_NAMED + ctx->nsecs; s++)
		if (s != SEC_BSS) sec_buf(ctx, s)->len = s < A->nbase ? A->base[s] : 0;
	ctx->bss_size = A->bss_base;
}

static void as_relax(As *A) {
	for (int i = 0; i < A->njmp; i++) {
		AsJmp *j = &A->jmp[i];
		AsSym *s = j->val.add;
		long long d = as_defined(A, s) ? s->off + j->val.v - (j->pos + 1) : 0;
		if (!as_defined(A, s) || s->weak || s->is_abs || s->sect != j->sect || d < -128 || d > 127) {
			A->longj[j->idx] = 1;
			A->changed = 1;
		} else sec_buf(A->ctx, j->sect)->data[j->pos] = (uint8_t)d;
	}
}

static void as_undefined_local(As *A, AsSym *s) {
	const char *p = strchr(s->name, '\001');
	if (p) error_at(A->ctx, "undefined local label '%.*sf'", (int)(p - s->name), s->name);
	error_at(A->ctx, "undefined local symbol '%s'", s->name);
}

static void as_resolve(As *A) {
	Ctx *ctx = A->ctx;
	for (int i = 0; i < A->nfix; i++) {
		AsFix *f = &A->fix[i];
		AsVal v = f->val;
		ctx->tok.file = f->file;
		ctx->tok.line = f->line;
		as_fold(A, &v);
		AsSym *s = v.add;
		if (v.t) error_at(ctx, "expression is not constant");
		if (v.sub && !f->pc && s && as_defined(A, v.sub) && !v.sub->is_abs && v.sub->sect == f->sect) {
			v.v += f->pos + f->size - v.sub->off;
			v.sub = NULL;
			f->pc = 1;
			f->end = f->pos + f->size;
		}
		if (v.sub) error_at(ctx, "cannot subtract '%s' here", v.sub->name);
		int local = s && as_defined(A, s) && !s->weak;
		if (s && s->clab && !local) {
			if (!f->pc || f->size != 4 || f->end != f->pos + 4 || f->sect != ctx->cur_text || v.v)
				error_at(ctx, "a C label can only be the target of a jump");
			patches_push(&s->clab->patches, (size_t)f->pos);
			continue;
		}
		if (s && !local && !s->hidden && (isdigit((unsigned char)s->name[0]) || s->local || !strncmp(s->name, ".L", 2))) as_undefined_local(A, s);
		Buf *b = sec_buf(ctx, f->sect);
		if (local && f->pc && s->sect == f->sect) {
			long long d = s->off + v.v - f->end;
			if (f->size == 1 && (d < -128 || d > 127)) error_at(ctx, "branch target out of range");
			as_check(A, d, f->size, 1);
			as_put(b->data + f->pos, d, f->size);
			continue;
		}
		if (!s && !f->pc) {
			as_check(A, v.v, f->size, f->sign);
			as_put(b->data + f->pos, v.v, f->size);
			continue;
		}
		RelKind k = f->pc ? (f->size == 8 ? R_PC64 : f->size == 4 ? R_PC32 : f->size == 2 ? R_PC16 : R_PC8) :
			f->size == 8 ? R_ABS64 : f->size == 4 ? (f->sign ? R_ABS32S : R_ABS32) : f->size == 2 ? R_ABS16 : R_ABS8;
		long long add = v.v - (f->pc ? f->end - (f->pos + f->size) : 0);
		if (!s) add_reloc(ctx, k, (size_t)f->pos, NULL, SEC_ABS, add);
		else if (local) add_reloc(ctx, k, (size_t)f->pos, NULL, s->sect, s->off + add);
		else add_reloc(ctx, f->plt && k == R_PC32 ? R_PLT32 : k, (size_t)f->pos, s->sym, 0, add);
		ctx->relocs[ctx->nrelocs - 1].in = f->sect;
	}
}

static void as_finish(As *A, const char *file, int nrel) {
	Ctx *ctx = A->ctx;
	for (int h = 0; h < 256; h++)
		for (AsSym *s = A->tab[h]; s; s = s->next) {
			Sym *y = s->sym;
			if (isdigit((unsigned char)s->name[0]) || s->is_abs) continue;
			int def = as_defined(A, s);
			if (def) {
				y->defined = 1;
				y->sect = s->sect;
				y->off = s->off;
				y->size = s->has_size ? s->size : 0;
				y->kind = s->type ? (s->type == 1 ? SYM_FUNC : SYM_VAR) : as_is_code(ctx, s->sect) ? SYM_FUNC : SYM_VAR;
				y->weak_def = s->weak;
			}
			y->weak = s->weak;
			if (!s->global && !s->weak && def) {
				if (strncmp(s->name, ".L", 2) != 0) { y->gnext = ctx->globals; ctx->globals = y; }
				continue;
			}
			if (!def && (s->local || !strncmp(s->name, ".L", 2))) continue;
			Sym *g = NULL;
			for (Sym *p = ctx->globals; p; p = p->gnext)
				if (p->name == y->name && !p->is_static && (p->kind == SYM_FUNC || p->kind == SYM_VAR)) { g = p; break; }
			if (!g) {
				y->is_static = 0;
				y->gnext = ctx->globals;
				ctx->globals = y;
				continue;
			}
			if (def) {
				if (!g->defined || (g->weak_def && !s->weak)) {
					g->defined = 1;
					g->sect = y->sect;
					g->off = y->off;
					g->size = y->size;
					g->weak = g->weak_def = s->weak;
					if (g->is_asm) g->kind = y->kind;
				} else if (!s->weak) {
					fprintf(stderr, "%s: error: redefinition of '%s'\n", file, s->name);
					exit(1);
				}
			} else if (s->weak && !g->defined) g->weak = 1;
			for (int i = nrel; i < ctx->nrelocs; i++)
				if (ctx->relocs[i].sym == y) ctx->relocs[i].sym = g;
		}
}

static void assemble_unit(Ctx *ctx, const char *src, const char *file, int cpp) {
	As *A = (As *)calloc(1, sizeof(As));
	A->ctx = ctx;
	as_init();
	ctx->unit++;
	A->nbase = SEC_NAMED + ctx->nsecs;
	A->base = (size_t *)malloc(sizeof(size_t) * A->nbase);
	for (int s = 0; s < A->nbase; s++) A->base[s] = s == SEC_BSS ? 0 : sec_buf(ctx, s)->len;
	A->bss_base = ctx->bss_size;
	int nonce = ctx->nonce;
	int nrel = ctx->nrelocs;
	for (A->pass = 1;; A->pass++) {
		if (A->pass > 1) as_restore(A);
		lex_asm = 1;
		if (cpp) {
			pp_begin_unit(ctx);
			pp_define_str(ctx, "__ASSEMBLER__ 1");
		}
		push_file(ctx, src, file, 0);
		ctx->src->L.as = cpp ? 1 : 2;
		ctx->has_pk = 0;
		ctx->has_pp_back = 0;
		A->has_pk = 0;
		A->mode = 64;
		A->sect = A->prev = SEC_TEXT;
		A->c.b = ctx->text.b;
		A->nfix = A->njmp = A->nj = A->nstack = A->npend = A->nbm = A->ncond = 0;
		A->changed = A->done = 0;
		A->bss_name = NULL;
		as_next(A);
		while (A->tok.kind != T_EOF && !A->done) {
			if (A->tok.kind == T_SEMI) { as_next(A); continue; }
			ctx->tok = A->tok;
			if (as_stmt(A)) continue;
			if (!A->done && !as_at_end(A)) error_at(ctx, "unexpected '%s' at the end of the statement", tok_desc(&A->tok));
		}
		if (A->ncond) error_at(ctx, ".if without .endif");
		as_flush(A);
		pp_end_unit(ctx);
		ctx->nonce = nonce;
		lex_asm = 0;
		as_relax(A);
		if (!A->changed) break;
	}
	as_resolve(A);
	as_finish(A, file, nrel);
	for (int i = 0; i < A->nbm; i++) {
		long end = i + 1 < A->nbm ? A->bpos[i + 1] : ctx->bss_size;
		if (end > A->bpos[i]) add_piece(ctx, SEC_BSS, A->bpos[i], end, A->bname[i], file, ctx->bss_align);
	}
	free(A->bpos);
	free(A->bname);
}

typedef struct {
	char *name;
	Sym *sym;
	Label *lab;
} AsHide;

static void as_finish_snippet(As *A) {
	Ctx *ctx = A->ctx;
	for (int h = 0; h < 256; h++)
		for (AsSym *s = A->tab[h]; s; s = s->next) {
			Sym *y = s->sym;
			if (s->hidden || isdigit((unsigned char)s->name[0]) || s->is_abs || s->name == id_dot) continue;
			int def = s->pass == A->pass;
			int dotl = strncmp(s->name, ".L", 2) == 0;
			if (def) {
				if (s->csym && y->defined) error_at(ctx, "redefinition of '%s'", s->name);
				y->defined = 1;
				y->sect = s->sect;
				y->off = s->off;
				y->size = s->has_size ? s->size : 0;
				if (!s->csym || y->is_asm)
					y->kind = s->type ? (s->type == 1 ? SYM_FUNC : SYM_VAR) : as_is_code(ctx, s->sect) ? SYM_FUNC : SYM_VAR;
				y->weak_def = s->weak;
			}
			if (s->weak) y->weak = 1;
			if (s->csym) {
				if (s->global) y->is_static = 0;
				continue;
			}
			if (dotl || (!def && s->local)) continue;
			y->is_static = def && !s->global && !s->weak;
			y->gnext = ctx->globals;
			ctx->globals = y;
			if (def && ctx->depth == 0) bind_global(ctx, y);
		}
}

static void assemble_snippet(Ctx *ctx, const char *text, AsHide *hid, int nh, const char *file, int line) {
	As *A = (As *)calloc(1, sizeof(As));
	Tok saved = ctx->tok;
	Lex L;
	A->ctx = ctx;
	A->snip = 1;
	A->sfile = file;
	A->sline = line;
	as_init();
	for (int i = 0; i < nh; i++) {
		AsSym *s = as_sym(A, hid[i].name);
		s->hidden = 1;
		if (hid[i].sym) { s->sym = hid[i].sym; s->csym = 1; }
		if (hid[i].lab) {
			if (hid[i].lab->pos >= 0) { s->fixed = 1; s->sect = ctx->cur_text; s->off = hid[i].lab->pos; }
			else s->clab = hid[i].lab;
		}
	}
	A->nbase = SEC_NAMED + ctx->nsecs;
	A->base = (size_t *)malloc(sizeof(size_t) * A->nbase);
	for (int s = 0; s < A->nbase; s++) A->base[s] = s == SEC_BSS ? 0 : sec_buf(ctx, s)->len;
	A->bss_base = ctx->bss_size;
	for (A->pass = 1;; A->pass++) {
		if (A->pass > 1) as_restore(A);
		lex_init(&L, text, file);
		L.as = 2;
		L.line = line;
		A->lx = &L;
		A->has_pk = 0;
		A->mode = 64;
		A->sect = A->prev = ctx->cur_text;
		A->c.b = *sec_buf(ctx, A->sect);
		A->nfix = A->njmp = A->nj = A->nstack = A->npend = 0;
		A->changed = A->done = 0;
		as_next(A);
		while (A->tok.kind != T_EOF && !A->done) {
			if (A->tok.kind == T_SEMI) { as_next(A); continue; }
			if (as_stmt(A)) continue;
			if (!A->done && !as_at_end(A)) error_at(ctx, "unexpected '%s' at the end of an asm statement", tok_desc(&A->tok));
		}
		as_flush(A);
		as_relax(A);
		if (!A->changed) break;
	}
	as_resolve(A);
	as_finish_snippet(A);
	ctx->tok = saved;
	ctx->fn_lea_sym = NULL;
	ctx->lv_end = 0;
}

enum { AK_REG, AK_MEM, AK_IMM, AK_LABEL };

typedef struct {
	char *name;
	int out, plus, early, kind;
	int creg, cmem, cimm, n8;
	int reg, fixed, match;
	Type *ty;
	int slot, vslot, pslot;
	int mem;
	long off;
	Sym *sym;
	long long imm;
	Label *lab;
} AsmOp;

static const int asm_pool[] = { RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11, RBX, R12, R13, R14, R15 };

static int asm_regnum(const char *name) {
	as_init();
	if (*name == '%') name++;
	char *id = intern(name);
	for (int i = 0; i < nas_regs; i++)
		if (as_regs[i].name == id && as_regs[i].rc == RC_GP && !as_regs[i].hi) return as_regs[i].num;
	return -1;
}

static const char *asm_regname(int reg, int size) {
	for (int i = 0; i < nas_regs; i++)
		if (as_regs[i].rc == RC_GP && as_regs[i].num == reg && as_regs[i].size == size && !as_regs[i].hi) return as_regs[i].name;
	return "?";
}

static char *asm_string(Ctx *ctx) {
	Buf b; buf_init(&b);
	if (ctx->tok.kind != T_STR) error_at(ctx, "expected a string");
	while (ctx->tok.kind == T_STR) {
		buf_append(&b, ctx->tok.str, ctx->tok.len);
		next(ctx);
	}
	buf_append_byte(&b, 0);
	return (char *)b.data;
}

static int is_asm_kw(Ctx *ctx) {
	Tok *t = &ctx->tok;
	if (t->kind != T_IDENT) return 0;
	if (t->str == id_asm2 || t->str == id_asm3) return 1;
	if (t->str != id_asm) return 0;
	int k = peek(ctx)->kind;
	return k == T_LP || k == T_VOLATILE || k == T_GOTO || k == T_INLINE;
}

static char *parse_asm_label(Ctx *ctx) {
	if (!is_asm_kw(ctx)) return NULL;
	int attrs = ctx->tok.attrs;
	Attr *a = ctx->tok.attr;
	next(ctx);
	expect(ctx, T_LP);
	char *s = asm_string(ctx);
	if (ctx->tok.kind != T_RP) error_at(ctx, "expected ')' after an asm label");
	next(ctx);
	ctx->tok.attrs |= attrs;
	if (a) {
		if (!ctx->tok.attr) ctx->tok.attr = a;
		else attr_merge(ctx->tok.attr, a);
	}
	return intern(s);
}

static void asm_constraint(Ctx *ctx, AsmOp *o, const char *c, AsmOp *outs, int nout) {
	o->fixed = o->match = -1;
	if (o->out) {
		if (*c == '+') o->plus = 1;
		else if (*c != '=') error_at(ctx, "output constraint '%s' must begin with '=' or '+'", c);
		c++;
	}
	for (; *c; c++) {
		int f = -1;
		switch (*c) {
		case '&': o->early = 1; break;
		case '%': case ' ': case '*': case '!': case '?': break;
		case 'r': case 'q': case 'R': case 'Q': case 'l': case 'U': o->creg = 1; break;
		case 'g': o->creg = o->cmem = o->cimm = 1; break;
		case 'm': case 'o': case 'V': case '<': case '>': o->cmem = 1; break;
		case 'N': o->n8 = 1; o->cimm = 1; break;
		case 'i': case 'n': case 's': case 'I': case 'J': case 'K': case 'L': case 'M': case 'O': case 'e': case 'Z': case 'E': case 'F':
			o->cimm = 1; break;
		case 'a': f = RAX; break;
		case 'b': f = RBX; break;
		case 'c': f = RCX; break;
		case 'd': f = RDX; break;
		case 'S': f = RSI; break;
		case 'D': f = RDI; break;
		case '[': {
			const char *e = strchr(c, ']');
			if (!e) error_at(ctx, "unterminated operand name in constraint");
			char *n = intern_str(c + 1, (int)(e - c - 1))->s;
			for (int i = 0; i < nout; i++) if (outs[i].name == n) o->match = i;
			if (o->match < 0) error_at(ctx, "no output operand named '%s'", n);
			c = e;
			break;
		}
		default:
			if (isdigit((unsigned char)*c)) {
				int m = 0;
				while (isdigit((unsigned char)c[1])) m = m * 10 + (*c++ - '0');
				m = m * 10 + (*c - '0');
				if (o->out || m >= nout) error_at(ctx, "matching constraint must refer to an output operand");
				o->match = m;
				break;
			}
			error_at(ctx, "unsupported asm constraint '%c'", *c);
		}
		if (f >= 0 && o->fixed < 0) o->fixed = f;
	}
	if (o->out && o->cimm && !o->creg && !o->cmem && o->fixed < 0) error_at(ctx, "an output cannot be an immediate");
	if (!o->creg && !o->cmem && !o->cimm && o->fixed < 0 && o->match < 0) error_at(ctx, "invalid asm constraint");
}

static void asm_regvar(Ctx *ctx, AsmOp *o) {
	if (!ctx->lv || Cpos(&ctx->text) != ctx->lv_end || ctx->lv_sym) return;
	for (Sym *s = ctx->scope; s; s = s->next)
		if (s->is_local && s->stack_off == ctx->lv_off && s->asm_reg) { o->fixed = s->asm_reg - 1; return; }
}

static void asm_scalar(Ctx *ctx, Type *ty) {
	if (!is_scalar(ty) || ty->size > 8) error_at(ctx, "asm register operand must be a scalar");
}

static void asm_output(Ctx *ctx, AsmOp *o) {
	Code *c = &ctx->text;
	Mark m = mark_code(ctx);
	Type *ty = parse_assign(ctx);
	if (!ctx->lv) error_at(ctx, "asm output operand is not an lvalue");
	int simple = Cpos(c) == ctx->lv_end;
	o->ty = ty;
	if (o->fixed < 0) asm_regvar(ctx, o);
	if (o->cmem && !o->creg && o->fixed < 0) {
		o->kind = AK_MEM;
		if (simple) {
			o->mem = ctx->lv_sym ? 2 : 1;
			o->sym = ctx->lv_sym;
			o->off = ctx->lv_off;
			drop_code(ctx, m);
		} else {
			gen_push(ctx);
			o->slot = ctx->sp;
		}
		ctx->lv = 0;
		return;
	}
	asm_scalar(ctx, ty);
	o->kind = AK_REG;
	gen_push(ctx);
	o->slot = ctx->sp;
	if (o->plus) {
		emit_load_type(c, ty);
		gen_push(ctx);
		o->vslot = ctx->sp;
	}
	ctx->lv = 0;
}

static void asm_input(Ctx *ctx, AsmOp *o, AsmOp *outs) {
	Code *c = &ctx->text;
	if (o->match >= 0) {
		AsmOp *t = &outs[o->match];
		Type *ty = rval(ctx, parse_assign(ctx));
		if (t->kind == AK_MEM) { o->kind = AK_MEM; o->mem = t->mem; o->sym = t->sym; o->off = t->off; o->slot = t->slot; o->match = -1; return; }
		asm_scalar(ctx, ty);
		o->kind = AK_REG;
		o->ty = ty;
		gen_push(ctx);
		o->slot = ctx->sp;
		return;
	}
	if (o->cimm) {
		TokVec v = capture_expr(ctx);
		if (tokens_const(ctx, &v)) {
			long long k = eval_tokens(ctx, &v);
			if (!o->n8 || (k >= 0 && k <= 255)) {
				o->kind = AK_IMM;
				o->imm = k;
				o->ty = ty_long;
				return;
			}
		}
		replay(ctx, &v);
	}
	Mark m = mark_code(ctx);
	Type *ty = parse_assign(ctx);
	if (o->cimm && !ctx->lv) {
		Sym *s = NULL;
		if (ctx->lv_sym && ctx->lv_end == Cpos(c) && ty->kind == TY_PTR) s = ctx->lv_sym;
		else if (ctx->fn_lea_sym && ctx->fn_lea_end == Cpos(c)) s = ctx->fn_lea_sym;
		if (s) {
			drop_code(ctx, m);
			o->kind = AK_IMM;
			o->sym = s;
			o->ty = ty_long;
			return;
		}
	}
	if (o->cimm && !o->creg && !o->cmem && o->fixed < 0) error_at(ctx, "asm operand needs a constant");
	if (o->fixed < 0) asm_regvar(ctx, o);
	if (o->cmem && !o->creg && o->fixed < 0) {
		o->kind = AK_MEM;
		o->ty = ty;
		if (ctx->lv && Cpos(c) == ctx->lv_end) {
			o->mem = ctx->lv_sym ? 2 : 1;
			o->sym = ctx->lv_sym;
			o->off = ctx->lv_off;
			drop_code(ctx, m);
		} else if (ctx->lv) {
			gen_push(ctx);
			o->slot = ctx->sp;
		} else {
			o->mem = 1;
			o->off = alloc_local(ctx, 8, 8);
			emit_store_rbp(c, RAX, (int)o->off);
		}
		ctx->lv = 0;
		return;
	}
	ty = rval(ctx, ty);
	asm_scalar(ctx, ty);
	o->kind = AK_REG;
	o->ty = ty;
	gen_push(ctx);
	o->slot = ctx->sp;
}

static int asm_operands(Ctx *ctx, AsmOp *ops, int n, int out, int nout) {
	if (ctx->tok.kind == T_COLON || ctx->tok.kind == T_RP) return n;
	for (;;) {
		if (n >= 30) error_at(ctx, "too many asm operands");
		AsmOp *o = &ops[n];
		memset(o, 0, sizeof(*o));
		o->out = out;
		if (consume(ctx, T_LBRACKET)) {
			if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected an operand name");
			o->name = ctx->tok.str;
			next(ctx);
			expect(ctx, T_RBRACKET);
		}
		char *con = asm_string(ctx);
		asm_constraint(ctx, o, con, ops, nout);
		expect(ctx, T_LP);
		if (out) asm_output(ctx, o);
		else asm_input(ctx, o, ops);
		expect(ctx, T_RP);
		n++;
		if (!consume(ctx, T_COMMA)) return n;
	}
}

static int asm_take(Ctx *ctx, int *used) {
	for (int i = 0; i < (int)(sizeof(asm_pool) / sizeof(asm_pool[0])); i++)
		if (!(*used & (1 << asm_pool[i]))) { *used |= 1 << asm_pool[i]; return asm_pool[i]; }
	error_at(ctx, "asm statement needs too many registers");
	return 0;
}

static int asm_size(Type *ty) {
	int s = ty ? ty->size : 8;
	return s == 1 || s == 2 || s == 4 ? s : 8;
}

static void asm_optext(Ctx *ctx, Buf *b, AsmOp *o, int mod, int idx) {
	char t[96];
	t[0] = 0;
	if (mod == 'z') {
		snprintf(t, sizeof(t), "%c", "bwlq"[asm_size(o->ty) == 1 ? 0 : asm_size(o->ty) == 2 ? 1 : asm_size(o->ty) == 4 ? 2 : 3]);
	} else if (o->kind == AK_REG) {
		if (mod == 'h') {
			if (o->reg > RBX) error_at(ctx, "%%h needs %%rax, %%rbx, %%rcx or %%rdx");
			snprintf(t, sizeof(t), "%%%ch", "acdb"[o->reg]);
		} else {
			int sz = mod == 'b' ? 1 : mod == 'w' ? 2 : mod == 'k' ? 4 : mod == 'q' || mod == 'a' ? 8 : asm_size(o->ty);
			snprintf(t, sizeof(t), "%s%%%s%s", mod == 'a' ? "(" : "", asm_regname(o->reg, sz), mod == 'a' ? ")" : "");
		}
	} else if (o->kind == AK_MEM) {
		if (o->mem == 1) snprintf(t, sizeof(t), "%ld(%%rbp)", o->off);
		else if (o->mem == 2) snprintf(t, sizeof(t), "__sccc_op%d(%%rip)", idx);
		else snprintf(t, sizeof(t), "(%%%s)", asm_regname(o->reg, 8));
	} else if (o->kind == AK_IMM) {
		const char *d = mod == 'c' || mod == 'P' || mod == 'p' || mod == 'n' || mod == 'a' ? "" : "$";
		if (o->sym) snprintf(t, sizeof(t), "%s__sccc_op%d", d, idx);
		else snprintf(t, sizeof(t), "%s%lld", d, mod == 'n' ? -o->imm : o->imm);
	} else snprintf(t, sizeof(t), "__sccc_op%d", idx);
	buf_append(b, t, strlen(t));
}

static char *asm_expand(Ctx *ctx, const char *p, AsmOp *ops, int n) {
	Buf b; buf_init(&b);
	int uniq = ctx->asm_count++;
	while (*p) {
		if (*p != '%') { buf_append_byte(&b, (uint8_t)*p++); continue; }
		p++;
		if (*p == '%' || *p == '{' || *p == '}' || *p == '|') { buf_append_byte(&b, (uint8_t)*p++); continue; }
		if (*p == '=') {
			char t[16];
			snprintf(t, sizeof(t), "%d", uniq);
			buf_append(&b, t, strlen(t));
			p++;
			continue;
		}
		int mod = 0;
		if (isalpha((unsigned char)*p) && (isdigit((unsigned char)p[1]) || p[1] == '[')) mod = *p++;
		int idx = -1;
		if (isdigit((unsigned char)*p)) {
			idx = 0;
			while (isdigit((unsigned char)*p)) idx = idx * 10 + (*p++ - '0');
		} else if (*p == '[') {
			const char *e = strchr(p, ']');
			if (!e) error_at(ctx, "unterminated operand name in asm template");
			char *nm = intern_str(p + 1, (int)(e - p - 1))->s;
			for (int i = 0; i < n; i++) if (ops[i].name == nm) idx = i;
			if (idx < 0) error_at(ctx, "no asm operand named '%s'", nm);
			p = e + 1;
		} else error_at(ctx, "invalid operand reference in asm template");
		if (idx >= n) error_at(ctx, "asm operand number %d out of range", idx);
		asm_optext(ctx, &b, &ops[idx], mod, idx);
	}
	buf_append_byte(&b, 0);
	return (char *)b.data;
}

static void asm_clobber(Ctx *ctx, const char *name, int *mask) {
	if (!strcmp(name, "memory") || !strcmp(name, "cc") || !strcmp(name, "dirflag") || !strcmp(name, "fpsr") ||
		!strcmp(name, "flags")) return;
	const char *n = *name == '%' ? name + 1 : name;
	if (!strncmp(n, "xmm", 3) || !strncmp(n, "st", 2) || !strncmp(n, "mm", 2)) return;
	int r = asm_regnum(n);
	if (r < 0) error_at(ctx, "unknown register '%s' in clobber list", name);
	if (r == RSP || r == RBP) error_at(ctx, "'%s' cannot be clobbered", name);
	*mask |= 1 << r;
}

static void asm_statement(Ctx *ctx, int top) {
	Code *c = &ctx->text;
	const char *file = ctx->tok.file;
	int line = ctx->tok.line;
	int is_goto = 0;
	as_init();
	next(ctx);
	for (;;) {
		if (ctx->tok.kind == T_VOLATILE || ctx->tok.kind == T_INLINE) next(ctx);
		else if (ctx->tok.kind == T_GOTO) { is_goto = 1; next(ctx); }
		else break;
	}
	expect(ctx, T_LP);
	char *tmpl = asm_string(ctx);
	if (ctx->tok.kind == T_RP && !is_goto) {
		next(ctx);
		expect(ctx, T_SEMI);
		assemble_snippet(ctx, tmpl, NULL, 0, file, line);
		return;
	}
	if (top) error_at(ctx, "asm at file scope cannot have operands");
	if (!ctx->fn || ctx->fn->sym->naked) error_at(ctx, "asm with operands is not allowed here");
	AsmOp ops[32];
	int nout = 0, n = 0, clob = 0;
	int sp0 = ctx->sp;
	if (consume(ctx, T_COLON)) nout = n = asm_operands(ctx, ops, 0, 1, 0);
	if (consume(ctx, T_COLON)) n = asm_operands(ctx, ops, n, 0, nout);
	if (consume(ctx, T_COLON) && ctx->tok.kind == T_STR) {
		for (;;) {
			char *cl = asm_string(ctx);
			asm_clobber(ctx, cl, &clob);
			if (!consume(ctx, T_COMMA)) break;
		}
	}
	if (is_goto) {
		expect(ctx, T_COLON);
		for (;;) {
			if (n >= 30) error_at(ctx, "too many asm operands");
			if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected a label name");
			AsmOp *o = &ops[n++];
			memset(o, 0, sizeof(*o));
			o->kind = AK_LABEL;
			o->name = ctx->tok.str;
			o->lab = find_label(ctx, ctx->tok.str);
			next(ctx);
			if (!consume(ctx, T_COMMA)) break;
		}
		if (nout) error_at(ctx, "asm goto with outputs is not supported");
	}
	expect(ctx, T_RP);
	expect(ctx, T_SEMI);

	int used_out = clob, used_in = clob;
	for (int i = 0; i < n; i++) {
		AsmOp *o = &ops[i];
		if (o->kind == AK_REG && o->fixed >= 0) {
			int bit = 1 << o->fixed;
			int *u = o->out ? &used_out : &used_in;
			if (*u & bit) error_at(ctx, "asm register '%%%s' is used twice", asm_regname(o->fixed, 8));
			*u |= bit;
			o->reg = o->fixed;
			if (o->plus) used_in |= bit;
		}
	}
	for (int i = nout; i < n; i++)
		if (ops[i].kind == AK_REG && ops[i].fixed >= 0)
			for (int j = 0; j < nout; j++)
				if (ops[j].kind == AK_REG && ops[j].early && ops[j].reg == ops[i].reg && ops[j].fixed >= 0)
					error_at(ctx, "asm input uses the register of an early clobber output");
	for (int i = 0; i < nout; i++) {
		AsmOp *o = &ops[i];
		if (o->kind != AK_REG || o->fixed >= 0) continue;
		int u = used_out | used_in;
		o->reg = asm_take(ctx, &u);
		used_out |= 1 << o->reg;
		if (o->plus) used_in |= 1 << o->reg;
	}
	for (int i = nout; i < n; i++) {
		AsmOp *o = &ops[i];
		if (o->kind == AK_REG && o->match >= 0) { o->reg = ops[o->match].reg; used_in |= 1 << o->reg; continue; }
		if ((o->kind == AK_REG && o->fixed < 0) || (o->kind == AK_MEM && o->mem == 0)) {
			int u = used_out | used_in;
			o->reg = asm_take(ctx, &u);
			used_in |= 1 << o->reg;
		}
	}
	for (int i = 0; i < nout; i++) {
		AsmOp *o = &ops[i];
		if (o->kind == AK_MEM && o->mem == 0) {
			int u = used_out | used_in;
			o->reg = asm_take(ctx, &u);
			used_in |= 1 << o->reg;
		}
	}

	static const int callee[] = { RBX, R12, R13, R14, R15 };
	int all = used_out | used_in, nsave = 0;
	int saves[5];
	for (int i = 0; i < 5; i++)
		if (all & (1 << callee[i])) saves[nsave++] = callee[i];
	if (is_goto && nsave) error_at(ctx, "asm goto cannot use %%rbx or %%r12 to %%r15");
	for (int i = 0; i < nsave; i++) { emit_push(c, saves[i]); ctx->sp++; }
	for (int i = 0; i < n; i++) {
		AsmOp *o = &ops[i];
		if (o->kind == AK_REG && (!o->out || o->plus)) emit_load_rsp(c, o->reg, (ctx->sp - (o->plus ? o->vslot : o->slot)) * 8);
		else if (o->kind == AK_MEM && o->mem == 0) emit_load_rsp(c, o->reg, (ctx->sp - o->slot) * 8);
	}
	if (is_goto && ctx->sp > sp0) {
		emit_add_rsp(c, (ctx->sp - sp0) * 8);
		ctx->sp = sp0;
	}

	AsHide hid[32];
	int nh = 0;
	char hn[32];
	for (int i = 0; i < n; i++) {
		AsmOp *o = &ops[i];
		if (!(o->kind == AK_LABEL || (o->kind == AK_MEM && o->mem == 2) || (o->kind == AK_IMM && o->sym))) continue;
		snprintf(hn, sizeof(hn), "__sccc_op%d", i);
		hid[nh].name = intern(hn);
		hid[nh].sym = o->kind == AK_LABEL ? NULL : o->sym;
		hid[nh].lab = o->kind == AK_LABEL ? o->lab : NULL;
		nh++;
	}
	char *text = asm_expand(ctx, tmpl, ops, n);
	assemble_snippet(ctx, text, hid, nh, file, line);
	free(text);

	int npush = 0;
	for (int i = 0; i < nout; i++)
		if (ops[i].kind == AK_REG) { emit_push(c, ops[i].reg); ctx->sp++; ops[i].pslot = ctx->sp; npush++; }
	for (int i = 0; i < nout; i++) {
		AsmOp *o = &ops[i];
		if (o->kind != AK_REG) continue;
		emit_load_rsp(c, RCX, (ctx->sp - o->slot) * 8);
		emit_load_rsp(c, RAX, (ctx->sp - o->pslot) * 8);
		emit_store_type(c, o->ty);
	}
	if (npush) { emit_add_rsp(c, npush * 8); ctx->sp -= npush; }
	for (int i = nsave - 1; i >= 0; i--) { emit_pop(c, saves[i]); ctx->sp--; }
	if (ctx->sp > sp0) { emit_add_rsp(c, (ctx->sp - sp0) * 8); ctx->sp = sp0; }
	ctx->lv = 0;
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

static void add_elf_sym(Buf *symtab, Buf *strtab, Sym *s, uint64_t *sect_va, int *shndx, int bind) {
	uint8_t e[24];
	memset(e, 0, 24);
	le32(e, strtab_add(strtab, s->name));
	e[4] = (uint8_t)((bind << 4) | (s->kind == SYM_FUNC ? 2 : 1));
	le16(e+6, (uint16_t)(s->sect == SEC_ABS ? 0xFFF1 : shndx[s->sect]));
	le64(e+8, s->sect == SEC_ABS ? (uint64_t)s->off : sect_va[s->sect] + s->off);
	le64(e+16, (uint64_t)s->size);
	buf_append(symtab, e, 24);
}

static void write_out(const char *out_path, Buf *file, int mode) {
	remove(out_path);
	FILE *out=fopen(out_path,"wb");
	if (!out) { perror(out_path); exit(1); }
	if (fwrite(file->data,1,file->len,out)!=file->len) { perror("fwrite"); exit(1); }
	fclose(out);
	if (mode) chmod(out_path, mode);
}

static const char *find_builtin_boot(const char *name);
static void ctx_init(Ctx *ctx);

static Buf assemble_blob(const char *name, const char *params) {
	Ctx *b = (Ctx *)malloc(sizeof(Ctx));
	ctx_init(b);
	Buf src; buf_init(&src);
	buf_append(&src, params, strlen(params));
	buf_append(&src, find_builtin_boot(name), strlen(find_builtin_boot(name)) + 1);
	assemble_unit(b, (const char *)src.data, name, 0);
	if (b->nrelocs || b->rodata.len || b->data.len || b->bss_size || b->nsecs) { fprintf(stderr, "internal: %s is not position independent\n", name); exit(1); }
	free(src.data);
	return b->text.b;
}

static void write_image(const char *out_path, Buf *file, uint64_t dest, uint64_t end, uint64_t entry, int high) {
	size_t size = align_up(file->len, 512);
	if (dest < 0x100000 || end > 0x40000000) { fprintf(stderr, "error: an image must load between 1 MB and 1 GB\n"); exit(1); }
	if (size > 0x8FC00) { fprintf(stderr, "error: kernel too large for the boot sector (%lu bytes, limit 588800)\n", (unsigned long)size); exit(1); }
	char params[512];
	snprintf(params, sizeof(params), "\t.set DEST, %lu\n\t.set SIZE, %lu\n\t.set ZERO, %lu\n\t.set ENTRY, %lu\n\t.set NSECT, %lu\n\t.set HIGH, %d\n",
		(unsigned long)dest, (unsigned long)size, end > dest + size ? (unsigned long)(end - dest - size) : 0UL,
		(unsigned long)entry, (unsigned long)(size / 512), high);
	Buf b = assemble_blob("img.s", params);
	buf_append(&b, file->data, file->len);
	buf_zero_to(&b, align_up(b.len, 16 * 63 * 512));
	write_out(out_path, &b, 0);
	free(b.data);
}

static void write_multiboot(const char *out_path, Buf *file, uint64_t text, uint64_t end, uint64_t entry, int high) {
	uint64_t load = text - 0x1000;
	uint64_t pt = align_up(end, 0x1000);
	uint64_t top = pt + 0x7000;
	if (load < 0x100000 || top > 0x40000000) { fprintf(stderr, "error: a multiboot kernel must load between 1 MB and 1 GB\n"); exit(1); }
	char params[512];
	snprintf(params, sizeof(params), "\t.set LOAD, %lu\n\t.set END, %lu\n\t.set TOP, %lu\n\t.set PT, %lu\n\t.set ENTRY, %lu\n\t.set HIGH, %d\n",
		(unsigned long)load, (unsigned long)(text + file->len), (unsigned long)top, (unsigned long)pt, (unsigned long)entry, high);
	Buf b = assemble_blob("multiboot.s", params);
	buf_append(&b, file->data, file->len);
	write_out(out_path, &b, 0);
	free(b.data);
}

static void resolve_alias_syms(Ctx *ctx) {
	for (Sym *s = ctx->globals; s; s = s->gnext) {
		if (!s->alias) continue;
		Sym *t = s->alias;
		for (int n = 0; t->alias && n < 64; n++) t = t->alias;
		if (t->alias || !t->defined) { fprintf(stderr, "error: '%s' is an alias of undefined symbol '%s'\n", s->name, s->alias_name); exit(1); }
		s->sect = t->sect;
		s->off = t->off;
		s->size = t->size;
		s->piece = t->piece;
	}
}

static void define_bounds(Ctx *ctx) {
	for (Sym *g = ctx->globals; g; g = g->gnext) {
		if (g->defined || g->is_static) continue;
		int stop = strncmp(g->name, "__stop_", 7) == 0;
		if (!stop && strncmp(g->name, "__start_", 8) != 0) continue;
		const char *name = g->name + (stop ? 7 : 8);
		for (int i = 0; i < ctx->nsecs; i++) {
			if (strcmp(ctx->secs[i].name, name) != 0) continue;
			g->sect = SEC_NAMED + i;
			g->off = stop ? (long)ctx->secs[i].b.len : 0;
			g->size = 0;
			g->defined = 1;
		}
	}
}

static int sec_flags(Ctx *ctx, int sect) {
	if (sect == SEC_TEXT) return 6;
	if (sect == SEC_RODATA) return 2;
	if (sect == SEC_DATA || sect == SEC_BSS) return 3;
	int k = ctx->secs[sect - SEC_NAMED].kind;
	return k == SK_CODE ? 6 : k == SK_RO ? 2 : 3;
}


static Sym *link_global(Ctx *ctx, char *name, int *created);

typedef struct LdExpr LdExpr;
struct LdExpr {
	int op;
	uint64_t v;
	char *name;
	LdExpr *a, *b, *c;
};

typedef struct LdPat LdPat;
struct LdPat {
	char *pat;
	int sort;
	LdPat *excl;
	LdPat *next;
};

enum { LS_ASSIGN, LS_SECTION, LS_INPUT, LS_DATA, LS_FILL, LS_ASSERT };
enum { LD_SORT_NAME = 1, LD_SORT_ALIGN };

typedef struct LdStmt LdStmt;
struct LdStmt {
	int kind, line;
	LdStmt *next;
	char *name;
	int op, provide, size, noload, orphan, byname, exact;
	LdStmt *anchor;
	LdExpr *e, *addr, *at, *align;
	char *msg;
	LdStmt *body;
	LdPat *file, *secs;
	int sortfile;
	int phdr[8], nphdr, has_phdr;
	uint8_t *fill;
	int nfill;
	int *pc;
	int npc, cappc;
	int out;
	uint64_t dva, dval;
};

typedef struct {
	char *name;
	LdStmt *st;
	uint64_t vma, lma, size, off;
	uint64_t align;
	int flags, nobits, kept, syms, discard, shndx, seg;
} LdOut;

typedef struct { uint64_t v; int sec; } LdVal;

typedef struct {
	char *name;
	LdVal val;
	int provide, pass;
} LdSym;

typedef struct {
	char *name;
	uint32_t type, flags;
	int has_flags;
	LdExpr *at;
	uint64_t off, vaddr, paddr, filesz, memsz, align;
} LdSeg;

struct Ld {
	const char *path;
	LdStmt *top, **tail;
	char *entry;
	const char *entry_cmd;
	int format;
	LdSeg *segs;
	int nsegs, capsegs, nphdrs;
	LdOut *outs;
	int nouts, capouts;
	LdSym *syms;
	int nsyms, capsyms;
	int **bysect;
	int *nbysect;
	uint64_t dot, start, delta;
	LdOut *cur;
	int has_delta, pass, final;
	int line;
};

typedef struct {
	const char *p, *file;
	int line;
} LdLex;


#define LD_SYMCH "_.$"
#define LD_SECCH "_.$-/\\~%@"
#define LD_PATCH "_.$-/\\~*?[]^!:+%@"

static void ld_err(LdLex *L, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: error: ", L->file, L->line);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void ld_fail(Ld *ld, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	if (ld->line) fprintf(stderr, "%s:%d: error: ", ld->path, ld->line);
	else fprintf(stderr, "%s: error: ", ld->path);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void ld_ws(LdLex *L) {
	for (;;) {
		if (*L->p == '\n') { L->line++; L->p++; }
		else if (isspace((unsigned char)*L->p)) L->p++;
		else if (L->p[0] == '/' && L->p[1] == '*') {
			const char *e = strstr(L->p + 2, "*/");
			if (!e) ld_err(L, "unterminated comment");
			for (; L->p < e; L->p++) if (*L->p == '\n') L->line++;
			L->p = e + 2;
		} else return;
	}
}

static int ld_at(LdLex *L, const char *s) {
	ld_ws(L);
	return strncmp(L->p, s, strlen(s)) == 0;
}

static int ld_accept(LdLex *L, const char *s) {
	if (!ld_at(L, s)) return 0;
	L->p += strlen(s);
	return 1;
}

static void ld_expect(LdLex *L, const char *s) {
	if (!ld_accept(L, s)) ld_err(L, "expected '%s'", s);
}

static char *ld_word(LdLex *L, const char *set) {
	ld_ws(L);
	const char *q = L->p;
	if (*q == '"') {
		const char *e = strchr(q + 1, '"');
		if (!e) ld_err(L, "unterminated string");
		L->p = e + 1;
		return intern_str(q + 1, (int)(e - q - 1))->s;
	}
	while (*L->p && (isalnum((unsigned char)*L->p) || strchr(set, *L->p))) L->p++;
	if (L->p == q) return NULL;
	return intern_str(q, (int)(L->p - q))->s;
}

static char *ld_need_word(LdLex *L, const char *set, const char *what) {
	char *w = ld_word(L, set);
	if (!w) ld_err(L, "expected %s", what);
	return w;
}

static LdExpr *ld_node(int op, LdExpr *a, LdExpr *b) {
	LdExpr *e = (LdExpr *)calloc(1, sizeof(LdExpr));
	e->op = op;
	e->a = a;
	e->b = b;
	return e;
}

static LdExpr *ld_expr(LdLex *L);

static const struct { const char *name; int op, args; } ld_funcs[] = {
	{ "ALIGN", 'a', 1 }, { "NEXT", 'a', 1 }, { "ADDR", 'D', 1 }, { "LOADADDR", 'M', 1 }, { "SIZEOF", 'Z', 1 },
	{ "ALIGNOF", 'Y', 1 }, { "ABSOLUTE", 'B', 1 }, { "DEFINED", 'd', 1 }, { "MAX", 'x', 2 }, { "MIN", 'm', 2 },
	{ "LOG2CEIL", 'g', 1 }, { "CONSTANT", 'P', 1 }, { "DATA_SEGMENT_ALIGN", 'S', 2 },
	{ "DATA_SEGMENT_END", 'B', 1 }, { "DATA_SEGMENT_RELRO_END", 'R', 2 }, { "SEGMENT_START", 'R', 2 },
	{ NULL, 0, 0 }
};

static LdExpr *ld_primary(LdLex *L) {
	ld_ws(L);
	char c = *L->p;
	if (c == '(') {
		L->p++;
		LdExpr *e = ld_expr(L);
		ld_expect(L, ")");
		return e;
	}
	if (c == '-' || c == '~' || c == '!') {
		L->p++;
		return ld_node(c == '-' ? 'n' : c, ld_primary(L), NULL);
	}
	if (c == '+') { L->p++; return ld_primary(L); }
	if (isdigit((unsigned char)c)) {
		const char *q = L->p;
		char *end;
		uint64_t v;
		int hex = q[0] == '0' && (q[1] == 'x' || q[1] == 'X');
		if (hex) v = strtoull(q + 2, &end, 16);
		else if (q[0] == '0' && isdigit((unsigned char)q[1])) v = strtoull(q, &end, 8);
		else v = strtoull(q, &end, 10);
		LdExpr *e = ld_node('#', NULL, NULL);
		if (*end == 'K' || *end == 'k') { v <<= 10; end++; hex = 0; }
		else if (*end == 'M' || *end == 'm') { v <<= 20; end++; hex = 0; }
		if (isalnum((unsigned char)*end) || *end == '_') ld_err(L, "bad number");
		if (hex) e->name = intern_str(q + 2, (int)(end - q - 2))->s;
		e->v = v;
		L->p = end;
		return e;
	}
	char *w = ld_word(L, LD_SYMCH);
	if (!w) ld_err(L, "expected an expression");
	if (!strcmp(w, ".")) return ld_node('.', NULL, NULL);
	if (!strcmp(w, "SIZEOF_HEADERS") || !strcmp(w, "sizeof_headers")) return ld_node('H', NULL, NULL);
	if (!strcmp(w, "ORIGIN") || !strcmp(w, "LENGTH")) ld_err(L, "MEMORY regions are not supported");
	for (int i = 0; ld_funcs[i].name; i++) {
		if (strcmp(w, ld_funcs[i].name)) continue;
		ld_expect(L, "(");
		int op = ld_funcs[i].op;
		LdExpr *e = ld_node(op, NULL, NULL);
		if (op == 'D' || op == 'M' || op == 'Z' || op == 'Y' || op == 'd' || op == 'P') {
			e->name = ld_need_word(L, LD_SECCH, "a name");
			if (op == 'P' && strcmp(e->name, "MAXPAGESIZE") && strcmp(e->name, "COMMONPAGESIZE")) ld_err(L, "unknown constant '%s'", e->name);
		} else if (!strcmp(w, "SEGMENT_START")) {
			ld_need_word(L, LD_SECCH, "a segment name");
			ld_expect(L, ",");
			e->a = ld_expr(L);
		} else {
			e->a = ld_expr(L);
			if (ld_funcs[i].args == 2 || (op == 'a' && ld_at(L, ","))) {
				ld_expect(L, ",");
				e->b = ld_expr(L);
				if (op == 'a') e->op = 'b';
			}
		}
		ld_expect(L, ")");
		return e;
	}
	LdExpr *e = ld_node('s', NULL, NULL);
	e->name = w;
	return e;
}

static int ld_binop(LdLex *L, int *op, int *len) {
	ld_ws(L);
	const char *p = L->p;
	static const char *two[] = { "||", "&&", "==", "!=", "<=", ">=", "<<", ">>", NULL };
	static const char ops2[] = "OAENLGlr";
	static const int prec2[] = { 1, 2, 6, 6, 7, 7, 8, 8 };
	for (int i = 0; two[i]; i++) {
		if (strncmp(p, two[i], 2)) continue;
		if ((ops2[i] == 'l' || ops2[i] == 'r') && p[2] == '=') return 0;
		*op = ops2[i];
		*len = 2;
		return prec2[i];
	}
	if (p[1] == '=' && strchr("|&+-*/", p[0])) return 0;
	*len = 1;
	*op = p[0];
	switch (p[0]) {
	case '|': return 3;
	case '^': return 4;
	case '&': return 5;
	case '<': case '>': return 7;
	case '+': case '-': return 9;
	case '*': case '/': case '%': return 10;
	}
	return 0;
}

static LdExpr *ld_binary(LdLex *L, int minprec) {
	LdExpr *l = ld_primary(L);
	for (;;) {
		int op, len, prec = ld_binop(L, &op, &len);
		if (!prec || prec < minprec) return l;
		L->p += len;
		l = ld_node(op, l, ld_binary(L, prec + 1));
	}
}

static LdExpr *ld_expr(LdLex *L) {
	LdExpr *c = ld_binary(L, 1);
	if (!ld_accept(L, "?")) return c;
	LdExpr *e = ld_node('?', c, ld_expr(L));
	ld_expect(L, ":");
	e->c = ld_expr(L);
	return e;
}

static LdStmt *ld_stmt(LdLex *L, int kind) {
	LdStmt *st = (LdStmt *)calloc(1, sizeof(LdStmt));
	st->kind = kind;
	st->line = L->line;
	st->out = -1;
	return st;
}

static int ld_assign_op(LdLex *L, char **name) {
	ld_ws(L);
	size_t n = strlen(*name);
	if (n > 1 && strchr("+-*/&|", (*name)[n - 1]) && L->p[0] == '=') {
		int op = (*name)[n - 1];
		*name = intern_str(*name, (int)n - 1)->s;
		L->p++;
		return op;
	}
	if (L->p[0] == '=' && L->p[1] != '=') { L->p++; return '='; }
	if ((L->p[0] == '<' || L->p[0] == '>') && L->p[1] == L->p[0] && L->p[2] == '=') {
		int op = L->p[0] == '<' ? 'l' : 'r';
		L->p += 3;
		return op;
	}
	if (L->p[1] == '=' && strchr("+-*/&|", L->p[0])) {
		int op = L->p[0];
		L->p += 2;
		return op;
	}
	return 0;
}

static LdStmt *ld_assignment(LdLex *L, char *name, int op, int provide) {
	LdStmt *st = ld_stmt(L, LS_ASSIGN);
	st->name = name;
	st->op = op;
	st->provide = provide;
	st->e = ld_expr(L);
	return st;
}

static LdStmt *ld_wrapped(LdLex *L, int provide) {
	ld_expect(L, "(");
	char *name = ld_need_word(L, LD_SECCH, "a symbol");
	int op = ld_assign_op(L, &name);
	if (op != '=') ld_err(L, "expected '='");
	LdStmt *st = ld_assignment(L, name, op, provide);
	ld_expect(L, ")");
	return st;
}

static LdStmt *ld_assert(LdLex *L) {
	LdStmt *st = ld_stmt(L, LS_ASSERT);
	ld_expect(L, "(");
	st->e = ld_expr(L);
	ld_expect(L, ",");
	st->msg = ld_need_word(L, LD_SECCH, "a message");
	ld_expect(L, ")");
	return st;
}

static int ld_sort_kind(const char *w) {
	if (!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_INIT_PRIORITY")) return LD_SORT_NAME;
	if (!strcmp(w, "SORT_BY_ALIGNMENT")) return LD_SORT_ALIGN;
	if (!strcmp(w, "SORT_NONE")) return 0;
	return -1;
}

static LdPat *ld_pat(char *w, int sort, LdPat *excl) {
	LdPat *p = (LdPat *)calloc(1, sizeof(LdPat));
	p->pat = w;
	p->sort = sort;
	p->excl = excl;
	return p;
}

static LdPat *ld_patlist(LdLex *L) {
	LdPat *head = NULL, **tail = &head;
	ld_expect(L, "(");
	while (!ld_accept(L, ")")) {
		ld_accept(L, ",");
		if (ld_at(L, ")")) continue;
		*tail = ld_pat(ld_need_word(L, LD_PATCH, "a file name"), 0, NULL);
		tail = &(*tail)->next;
	}
	return head;
}

static LdPat **ld_secpats(LdLex *L, LdPat **tail, int sort) {
	while (!ld_at(L, ")")) {
		if (ld_accept(L, ",")) continue;
		LdPat *excl = NULL;
		char *w = ld_need_word(L, LD_PATCH, "a section name");
		if (!strcmp(w, "EXCLUDE_FILE")) {
			excl = ld_patlist(L);
			w = ld_need_word(L, LD_PATCH, "a section name");
		}
		int k = ld_sort_kind(w);
		if (k >= 0 && ld_at(L, "(")) {
			ld_expect(L, "(");
			tail = ld_secpats(L, tail, sort ? sort : k);
			ld_expect(L, ")");
			continue;
		}
		*tail = ld_pat(w, sort, excl);
		tail = &(*tail)->next;
	}
	return tail;
}

static LdStmt *ld_input(LdLex *L, char *w) {
	LdStmt *st = ld_stmt(L, LS_INPUT);
	LdPat *excl = NULL;
	if (!strcmp(w, "EXCLUDE_FILE")) {
		excl = ld_patlist(L);
		w = ld_need_word(L, LD_PATCH, "a file name");
	}
	if (ld_sort_kind(w) >= 0 && ld_at(L, "(")) {
		ld_expect(L, "(");
		w = ld_need_word(L, LD_PATCH, "a file name");
		ld_expect(L, ")");
		st->sortfile = 1;
	}
	st->file = ld_pat(w, 0, excl);
	if (ld_accept(L, "(")) {
		ld_secpats(L, &st->secs, 0);
		ld_expect(L, ")");
	}
	return st;
}

static void ld_fill_pattern(LdStmt *st, LdExpr *e, LdLex *L) {
	if (e->op == '#' && e->name) {
		int n = (int)strlen(e->name);
		st->nfill = (n + 1) / 2;
		st->fill = (uint8_t *)calloc(st->nfill, 1);
		for (int i = 0; i < n; i++) {
			int d = hexval(e->name[n - 1 - i]);
			st->fill[st->nfill - 1 - i / 2] |= (uint8_t)(d << (i % 2 * 4));
		}
		return;
	}
	if (e->op != '#') ld_err(L, "a fill pattern must be a number");
	st->nfill = 4;
	st->fill = (uint8_t *)malloc(4);
	for (int i = 0; i < 4; i++) st->fill[i] = (uint8_t)(e->v >> (24 - 8 * i));
}

static LdStmt *ld_body(LdLex *L) {
	LdStmt *head = NULL, **tail = &head;
	while (!ld_accept(L, "}")) {
		if (!*L->p) ld_err(L, "expected '}'");
		if (ld_accept(L, ";")) continue;
		int line = L->line;
		char *w = ld_need_word(L, LD_PATCH, "a section command");
		LdStmt *st;
		int op;
		if (!strcmp(w, "KEEP")) {
			ld_expect(L, "(");
			st = ld_input(L, ld_need_word(L, LD_PATCH, "a file name"));
			ld_expect(L, ")");
		} else if (!strcmp(w, "BYTE") || !strcmp(w, "SHORT") || !strcmp(w, "LONG") || !strcmp(w, "QUAD") || !strcmp(w, "SQUAD")) {
			st = ld_stmt(L, LS_DATA);
			st->size = w[0] == 'B' ? 1 : w[0] == 'S' && w[1] == 'H' ? 2 : w[0] == 'L' ? 4 : 8;
			ld_expect(L, "(");
			st->e = ld_expr(L);
			ld_expect(L, ")");
		} else if (!strcmp(w, "FILL")) {
			st = ld_stmt(L, LS_FILL);
			ld_expect(L, "(");
			ld_fill_pattern(st, ld_expr(L), L);
			ld_expect(L, ")");
		} else if (!strcmp(w, "PROVIDE") || !strcmp(w, "PROVIDE_HIDDEN")) st = ld_wrapped(L, 1);
		else if (!strcmp(w, "HIDDEN")) st = ld_wrapped(L, 0);
		else if (!strcmp(w, "ASSERT")) st = ld_assert(L);
		else if (!strcmp(w, "CREATE_OBJECT_SYMBOLS") || !strcmp(w, "CONSTRUCTORS")) continue;
		else if (!strcmp(w, "INPUT_SECTION_FLAGS") || !strcmp(w, "INCLUDE")) ld_err(L, "'%s' is not supported", w);
		else if ((op = ld_assign_op(L, &w)) != 0) st = ld_assignment(L, w, op, 0);
		else st = ld_input(L, w);
		st->line = line;
		*tail = st;
		tail = &st->next;
	}
	return head;
}

static int ld_phdr_index(Ld *ld, LdLex *L, char *name) {
	for (int i = 0; i < ld->nsegs; i++) if (ld->segs[i].name == name) return i;
	ld_err(L, "program header '%s' is not defined in PHDRS", name);
	return -1;
}

static LdStmt *ld_section(Ld *ld, LdLex *L, char *name) {
	LdStmt *st = ld_stmt(L, LS_SECTION);
	st->name = name;
	if (!ld_at(L, ":")) {
		const char *save = L->p;
		int sl = L->line;
		char *t = NULL;
		if (ld_accept(L, "(")) {
			t = ld_word(L, LD_SYMCH);
			if (!t || !ld_accept(L, ")")) { L->p = save; L->line = sl; t = NULL; }
		}
		if (!t) {
			st->addr = ld_expr(L);
			if (ld_accept(L, "(")) {
				t = ld_need_word(L, LD_SYMCH, "a section type");
				ld_expect(L, ")");
			}
		}
		if (t && !strcmp(t, "NOLOAD")) st->noload = 1;
		else if (t && strcmp(t, "READONLY")) ld_err(L, "section type '%s' is not supported", t);
	}
	ld_expect(L, ":");
	for (;;) {
		if (ld_accept(L, "AT(") || (ld_at(L, "AT") && (ld_accept(L, "AT"), ld_accept(L, "(")))) {
			st->at = ld_expr(L);
			ld_expect(L, ")");
		} else if (ld_at(L, "ALIGN") && !ld_at(L, "ALIGN_WITH_INPUT")) {
			ld_accept(L, "ALIGN");
			ld_expect(L, "(");
			st->align = ld_expr(L);
			ld_expect(L, ")");
		} else if (ld_at(L, "SUBALIGN") || ld_at(L, "ONLY_IF") || ld_at(L, "ALIGN_WITH_INPUT")) {
			ld_err(L, "'%s' is not supported", ld_word(L, LD_SYMCH));
		} else break;
	}
	ld_expect(L, "{");
	st->body = ld_body(L);
	for (;;) {
		if (ld_at(L, ">") || ld_at(L, "AT>")) ld_err(L, "MEMORY regions are not supported");
		ld_ws(L);
		if (L->p[0] == ':' && L->p[1] != '=') {
			L->p++;
			char *ph = ld_need_word(L, LD_SECCH, "a program header name");
			st->has_phdr = 1;
			if (!strcmp(ph, "NONE")) continue;
			if (st->nphdr == 8) ld_err(L, "too many program headers for one section");
			st->phdr[st->nphdr++] = ld_phdr_index(ld, L, ph);
		} else if (L->p[0] == '=' && L->p[1] != '=') {
			L->p++;
			ld_fill_pattern(st, ld_expr(L), L);
		} else break;
	}
	ld_accept(L, ",");
	return st;
}

static void ld_add(Ld *ld, LdStmt *st) {
	*ld->tail = st;
	ld->tail = &st->next;
}

static void ld_sections(Ld *ld, LdLex *L) {
	ld_expect(L, "{");
	while (!ld_accept(L, "}")) {
		if (!*L->p) ld_err(L, "expected '}'");
		if (ld_accept(L, ";")) continue;
		int line = L->line, op;
		char *w = ld_need_word(L, LD_SECCH, "an output section or a command");
		LdStmt *st;
		if (!strcmp(w, "ENTRY")) {
			ld_expect(L, "(");
			ld->entry = ld_need_word(L, LD_SECCH, "a symbol");
			ld_expect(L, ")");
			continue;
		}
		if (!strcmp(w, "PROVIDE") || !strcmp(w, "PROVIDE_HIDDEN")) st = ld_wrapped(L, 1);
		else if (!strcmp(w, "HIDDEN")) st = ld_wrapped(L, 0);
		else if (!strcmp(w, "ASSERT")) st = ld_assert(L);
		else if (!strcmp(w, "OVERLAY") || !strcmp(w, "INCLUDE") || !strcmp(w, "INSERT")) ld_err(L, "'%s' is not supported", w);
		else if ((op = ld_assign_op(L, &w)) != 0) st = ld_assignment(L, w, op, 0);
		else st = ld_section(ld, L, w);
		st->line = line;
		ld_add(ld, st);
	}
}

static const struct { const char *name; uint32_t type; } ld_ptypes[] = {
	{ "PT_NULL", 0 }, { "PT_LOAD", 1 }, { "PT_DYNAMIC", 2 }, { "PT_INTERP", 3 }, { "PT_NOTE", 4 },
	{ "PT_SHLIB", 5 }, { "PT_PHDR", 6 }, { "PT_TLS", 7 }, { "PT_GNU_EH_FRAME", 0x6474e550 },
	{ "PT_GNU_STACK", 0x6474e551 }, { "PT_GNU_RELRO", 0x6474e552 }, { "PT_GNU_PROPERTY", 0x6474e553 },
	{ NULL, 0 }
};

static void ld_phdrs(Ld *ld, LdLex *L) {
	ld_expect(L, "{");
	while (!ld_accept(L, "}")) {
		if (!*L->p) ld_err(L, "expected '}'");
		if (ld_accept(L, ";")) continue;
		if (ld->nsegs == ld->capsegs) {
			ld->capsegs = ld->capsegs ? ld->capsegs * 2 : 8;
			ld->segs = (LdSeg *)realloc(ld->segs, sizeof(LdSeg) * ld->capsegs);
			if (!ld->segs) { perror("realloc"); exit(1); }
		}
		LdSeg *g = &ld->segs[ld->nsegs];
		memset(g, 0, sizeof(LdSeg));
		g->name = ld_need_word(L, LD_SECCH, "a program header name");
		ld_ws(L);
		if (isdigit((unsigned char)*L->p)) {
			LdExpr *e = ld_primary(L);
			g->type = (uint32_t)e->v;
		} else {
			char *t = ld_need_word(L, LD_SYMCH, "a segment type");
			int i;
			for (i = 0; ld_ptypes[i].name && strcmp(ld_ptypes[i].name, t); i++) {}
			if (!ld_ptypes[i].name) ld_err(L, "unknown segment type '%s'", t);
			g->type = ld_ptypes[i].type;
		}
		for (;;) {
			if (ld_accept(L, "FILEHDR") || ld_accept(L, "PHDRS")) continue;
			if (ld_at(L, "AT")) {
				ld_accept(L, "AT");
				ld_expect(L, "(");
				g->at = ld_expr(L);
				ld_expect(L, ")");
			} else if (ld_at(L, "FLAGS")) {
				ld_accept(L, "FLAGS");
				ld_expect(L, "(");
				g->flags = (uint32_t)ld_expr(L)->v;
				g->has_flags = 1;
				ld_expect(L, ")");
			} else break;
		}
		ld_expect(L, ";");
		ld->nsegs++;
	}
	ld->nphdrs = ld->nsegs;
}

static Ld *ld_read(const char *path) {
	Ld *ld = (Ld *)calloc(1, sizeof(Ld));
	ld->path = path;
	ld->tail = &ld->top;
	LdLex lx, *L = &lx;
	L->p = read_file(path);
	L->file = path;
	L->line = 1;
	for (;;) {
		ld_ws(L);
		if (!*L->p) break;
		if (ld_accept(L, ";")) continue;
		int line = L->line, op;
		char *w = ld_need_word(L, LD_SECCH, "a command");
		if (!strcmp(w, "SECTIONS")) ld_sections(ld, L);
		else if (!strcmp(w, "PHDRS")) ld_phdrs(ld, L);
		else if (!strcmp(w, "ENTRY")) {
			ld_expect(L, "(");
			ld->entry = ld_need_word(L, LD_SECCH, "a symbol");
			ld_expect(L, ")");
		} else if (!strcmp(w, "OUTPUT_FORMAT")) {
			ld_expect(L, "(");
			char *f = ld_need_word(L, LD_SECCH, "a format");
			while (ld_accept(L, ",")) ld_need_word(L, LD_SECCH, "a format");
			ld_expect(L, ")");
			if (!strcmp(f, "elf64-x86-64")) ld->format = 1;
			else if (!strcmp(f, "binary")) ld->format = 2;
			else ld_err(L, "output format '%s' is not supported", f);
		} else if (!strcmp(w, "OUTPUT_ARCH")) {
			ld_expect(L, "(");
			char *a = ld_need_word(L, "_.$-:", "an architecture");
			ld_expect(L, ")");
			if (strcmp(a, "i386:x86-64")) ld_err(L, "architecture '%s' is not supported", a);
		} else if (!strcmp(w, "SEARCH_DIR")) {
			ld_expect(L, "(");
			ld_need_word(L, LD_PATCH, "a directory");
			ld_expect(L, ")");
		} else if (!strcmp(w, "PROVIDE") || !strcmp(w, "PROVIDE_HIDDEN")) { LdStmt *st = ld_wrapped(L, 1); st->line = line; ld_add(ld, st); }
		else if (!strcmp(w, "HIDDEN")) { LdStmt *st = ld_wrapped(L, 0); st->line = line; ld_add(ld, st); }
		else if (!strcmp(w, "ASSERT")) { LdStmt *st = ld_assert(L); st->line = line; ld_add(ld, st); }
		else if ((op = ld_assign_op(L, &w)) != 0) { LdStmt *st = ld_assignment(L, w, op, 0); st->line = line; ld_add(ld, st); }
		else ld_err(L, "'%s' is not supported", w);
	}
	return ld;
}

static int ld_glob(const char *p, const char *s) {
	for (;; p++, s++) {
		if (*p == '*') {
			while (*p == '*') p++;
			if (!*p) return 1;
			for (; *s; s++) if (ld_glob(p, s)) return 1;
			return 0;
		}
		if (!*s) return !*p;
		if (*p == '?') continue;
		if (*p == '[') {
			const char *q = p + 1;
			int neg = *q == '!' || *q == '^', hit = 0;
			if (neg) q++;
			for (; *q && (*q != ']' || q == p + 1 + neg); q++) {
				if (q[1] == '-' && q[2] && q[2] != ']') { if (*s >= *q && *s <= q[2]) hit = 1; q += 2; }
				else if (*q == *s) hit = 1;
			}
			if (!*q || hit == neg) return 0;
			p = q;
			continue;
		}
		if (*p != *s) return 0;
	}
}

static int ld_file_match(LdPat *f, Piece *pc) {
	for (LdPat *x = f->excl; x; x = x->next) if (ld_file_match(x, pc)) return 0;
	const char *colon = strchr(f->pat, ':');
	if (!colon) return ld_glob(f->pat, pc->file);
	char a[512];
	snprintf(a, sizeof(a), "%.*s", (int)(colon - f->pat), f->pat);
	if (!pc->arch) return !a[0] && (!colon[1] || ld_glob(colon + 1, pc->file));
	return (!a[0] || ld_glob(a, pc->arch)) && (!colon[1] || ld_glob(colon + 1, pc->file));
}

static int ld_piece_kind(Ctx *ctx, Piece *p) {
	if (p->sect == SEC_BSS) return 3;
	int f = sec_flags(ctx, p->sect);
	return f & 4 ? 0 : f & 1 ? 2 : 1;
}

static int ld_sort_less(Ctx *ctx, LdStmt *in, int sort, int a, int b) {
	Piece *x = &ctx->pieces[a], *y = &ctx->pieces[b];
	if (in->sortfile) { int c = strcmp(x->file, y->file); if (c) return c < 0; }
	if (sort == LD_SORT_NAME) return strcmp(x->name, y->name) < 0;
	if (sort == LD_SORT_ALIGN) return x->align > y->align;
	return 0;
}

static void ld_take(Ctx *ctx, Ld *ld, LdStmt *in, int i, int out, int sort) {
	if (in->npc == in->cappc) {
		in->cappc = in->cappc ? in->cappc * 2 : 8;
		in->pc = (int *)realloc(in->pc, sizeof(int) * in->cappc);
		if (!in->pc) { perror("realloc"); exit(1); }
	}
	int at = in->npc;
	if (sort || in->sortfile)
		for (at = 0; at < in->npc && !ld_sort_less(ctx, in, sort, i, in->pc[at]); at++) {}
	memmove(in->pc + at + 1, in->pc + at, sizeof(int) * (size_t)(in->npc - at));
	in->pc[at] = i;
	in->npc++;
	ctx->pieces[i].out = out;
	if (out < 0) return;
	LdOut *o = &ld->outs[out];
	Piece *p = &ctx->pieces[i];
	if ((uint64_t)p->align > o->align) o->align = (uint64_t)p->align;
	if (p->end > p->start) o->kept = 1;
	int k = ld_piece_kind(ctx, p);
	o->flags |= 2 | (k == 0 ? 4 : 0) | (k >= 2 ? 1 : 0) | (k == 3 ? 0 : p->note ? 0x200 : 0x100);
	if (k != 3) o->nobits = 0;
}

static void ld_match_input(Ctx *ctx, Ld *ld, LdStmt *in, int out) {
	for (int i = 0; i < ctx->npieces; i++) {
		Piece *p = &ctx->pieces[i];
		if (p->out != -2) continue;
		if (!ld_file_match(in->file, p)) continue;
		LdPat *s = in->secs;
		if (s) {
			for (; s; s = s->next) {
				if (s->excl) {
					int ex = 0;
					for (LdPat *x = s->excl; x; x = x->next) if (ld_file_match(x, p)) ex = 1;
					if (ex) continue;
				}
				if (ld_glob(s->pat, p->name)) break;
			}
			if (!s) continue;
		}
		ld_take(ctx, ld, in, i, out, s ? s->sort : 0);
	}
}

static int ld_new_out(Ld *ld, LdStmt *st) {
	if (ld->nouts == ld->capouts) {
		ld->capouts = ld->capouts ? ld->capouts * 2 : 16;
		ld->outs = (LdOut *)realloc(ld->outs, sizeof(LdOut) * ld->capouts);
		if (!ld->outs) { perror("realloc"); exit(1); }
	}
	LdOut *o = &ld->outs[ld->nouts];
	memset(o, 0, sizeof(LdOut));
	o->name = st->name;
	o->st = st;
	o->align = 1;
	o->nobits = 1;
	o->discard = !strcmp(st->name, "/DISCARD/");
	o->seg = -1;
	st->out = ld->nouts;
	return ld->nouts++;
}

static void ld_scan_body(LdOut *o, LdStmt *b) {
	for (; b; b = b->next) {
		if (b->kind == LS_DATA) { o->kept = 1; o->nobits = 0; }
		if (b->kind == LS_ASSIGN && !strcmp(b->name, ".")) o->kept = 1;
		if (b->kind == LS_ASSIGN && strcmp(b->name, ".")) o->syms = 1;
	}
}

static LdOut *ld_find_out(Ld *ld, const char *name) {
	for (int i = 0; i < ld->nouts; i++) if (!strcmp(ld->outs[i].name, name) && !ld->outs[i].discard) return &ld->outs[i];
	return NULL;
}

static int ld_fits(LdOut *o, int k) {
	if (o->discard || !(o->flags & 2)) return 0;
	switch (k) {
	case 0: return (o->flags & 4) != 0;
	case 1: return !(o->flags & 5) && (o->flags & 0x100);
	case 2: return (o->flags & 0x101) == 0x101;
	case 3: return o->nobits;
	}
	return !(o->flags & 1) && (o->flags & 0x100);
}

static LdStmt *ld_named(Ld *ld, int k) {
	static const char *names[] = { ".text", ".rodata", ".data", ".bss" };
	for (LdStmt *st = ld->top; st; st = st->next)
		if (st->kind == LS_SECTION && !ld->outs[st->out].discard && !strcmp(st->name, names[k])) return st;
	return NULL;
}

static LdStmt *ld_anchor(Ld *ld, int k, LdStmt *recent, int *exact) {
	static const int order[4][4] = { { 0, -1 }, { 1, 4, -1 }, { 2, 1, 4, -1 }, { 3, -1 } };
	LdStmt *after = NULL;
	int f;
	for (f = 0; order[k][f] >= 0 && !after; f++)
		for (LdStmt *st = ld->top; st; st = st->next)
			if (st->kind == LS_SECTION && ld_fits(&ld->outs[st->out], order[k][f])) after = st;
	*exact = after && f == 1;
	if (!after && k == 3) for (after = ld->top; after && after->next; after = after->next) {}
	return after || k == 0 ? after : recent;
}

static void ld_orphans(Ctx *ctx, Ld *ld) {
	LdStmt *recent = NULL, *tail[4] = { NULL, NULL, NULL, NULL };
	for (int i = 0; i < ctx->npieces; i++) {
		Piece *p = &ctx->pieces[i];
		if (p->out != -2) continue;
		const char *name = p->name;
		if (!strcmp(name, "COMMON") && ld_find_out(ld, ".bss")) name = ".bss";
		LdOut *o = ld_find_out(ld, name);
		if (!o) {
			int k = ld_piece_kind(ctx, p);
			LdStmt *st = (LdStmt *)calloc(1, sizeof(LdStmt));
			st->kind = LS_SECTION;
			st->name = (char *)name;
			st->orphan = 1;
			st->anchor = ld_named(ld, k);
			st->byname = st->anchor != NULL;
			if (!st->anchor) st->anchor = ld_anchor(ld, k, recent, &st->exact);
			LdStmt **at, *a = st->anchor;
			if (st->byname && tail[k]) at = &tail[k]->next;
			else if (a) {
				while (a->next && (st->byname ? a->next->kind != LS_SECTION || a->next->orphan :
					a->next->kind == LS_SECTION && a->next->orphan && !a->next->byname && a->next->exact && a->next->anchor == st->anchor)) a = a->next;
				at = &a->next;
			} else for (at = &ld->top; *at && (*at)->kind != LS_SECTION; at = &(*at)->next) {}
			if (st->byname) tail[k] = st;
			st->next = *at;
			*at = st;
			if (ld->tail == at) ld->tail = &st->next;
			int oi = ld_new_out(ld, st);
			o = &ld->outs[oi];
			recent = st;

		}
		LdStmt *b = o->st->body, *last = NULL;
		for (; b; b = b->next) last = b;
		if (!last || last->kind != LS_INPUT || !last->orphan) {
			LdStmt *in = (LdStmt *)calloc(1, sizeof(LdStmt));
			in->kind = LS_INPUT;
			in->orphan = 1;
			if (last) last->next = in;
			else o->st->body = in;
			last = in;
		}
		ld_take(ctx, ld, last, i, (int)(o - ld->outs), 0);
	}
}

static void ld_match(Ctx *ctx, Ld *ld) {
	for (LdStmt *st = ld->top; st; st = st->next) {
		if (st->kind != LS_SECTION) continue;
		int out = ld_new_out(ld, st);
		ld_scan_body(&ld->outs[out], st->body);
		for (LdStmt *b = st->body; b; b = b->next)
			if (b->kind == LS_INPUT) ld_match_input(ctx, ld, b, ld->outs[out].discard ? -1 : out);
	}
	ld_orphans(ctx, ld);
	int ns = SEC_NAMED + ctx->nsecs;
	ld->bysect = (int **)calloc(ns, sizeof(int *));
	ld->nbysect = (int *)calloc(ns, sizeof(int));
	for (int i = 0; i < ctx->npieces; i++) ld->nbysect[ctx->pieces[i].sect]++;
	for (int s = 0; s < ns; s++) {
		ld->bysect[s] = (int *)malloc(sizeof(int) * (ld->nbysect[s] + 1));
		ld->nbysect[s] = 0;
	}
	for (int i = 0; i < ctx->npieces; i++) {
		int s = ctx->pieces[i].sect;
		ld->bysect[s][ld->nbysect[s]++] = i;
	}
}

static Piece *ld_piece_at(Ctx *ctx, int sect, long off) {
	Ld *ld = ctx->ld;
	int lo = 0, hi = ld->nbysect[sect] - 1, best = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (ctx->pieces[ld->bysect[sect][mid]].start <= off) { best = mid; lo = mid + 1; }
		else hi = mid - 1;
	}
	if (best < 0) return NULL;
	Piece *p = &ctx->pieces[ld->bysect[sect][best]];
	return off <= p->end ? p : NULL;
}

static Piece *ld_sym_piece(Ctx *ctx, Sym *s) {
	if (s->piece) return &ctx->pieces[s->piece - 1];
	return ld_piece_at(ctx, s->sect, s->off);
}

static uint64_t ld_addr(Ctx *ctx, int sect, long off) {
	Piece *p = ld_piece_at(ctx, sect, off);
	return p ? p->va + (uint64_t)(off - p->start) : 0;
}

static uint64_t ld_sym_addr(Ctx *ctx, Sym *s) {
	if (s->sect == SEC_ABS) return (uint64_t)s->off;
	Piece *p = ld_sym_piece(ctx, s);
	return p ? p->va + (uint64_t)(s->off - p->start) : 0;
}

static LdSym *ld_sym(Ld *ld, char *name, int create) {
	for (int i = 0; i < ld->nsyms; i++) if (ld->syms[i].name == name) return &ld->syms[i];
	if (!create) return NULL;
	if (ld->nsyms == ld->capsyms) {
		ld->capsyms = ld->capsyms ? ld->capsyms * 2 : 32;
		ld->syms = (LdSym *)realloc(ld->syms, sizeof(LdSym) * ld->capsyms);
		if (!ld->syms) { perror("realloc"); exit(1); }
	}
	LdSym *y = &ld->syms[ld->nsyms++];
	memset(y, 0, sizeof(LdSym));
	y->name = name;
	return y;
}

static Sym *ld_global(Ctx *ctx, char *name) {
	for (Sym *g = ctx->globals; g; g = g->gnext)
		if (g->name == name && !g->is_static && (g->kind == SYM_FUNC || g->kind == SYM_VAR)) return g;
	return NULL;
}

static LdOut *ld_out_named(Ld *ld, const char *name) {
	LdOut *o = ld_find_out(ld, name);
	if (!o && ld->final) ld_fail(ld, "undefined section '%s' referenced in expression", name);
	return o;
}

static uint64_t ld_alignup(uint64_t x, uint64_t a) { return a > 1 ? (x + a - 1) / a * a : x; }

enum { LV_NUM = -2, LV_ABS = -1 };

static LdVal ld_val(uint64_t v, int sec) { LdVal r; r.v = v; r.sec = sec; return r; }

static uint64_t ld_abs(Ld *ld, LdVal x) { return x.sec >= 0 ? ld->outs[x.sec].vma + x.v : x.v; }

static LdVal ld_dot(Ld *ld) {
	if (ld->cur) return ld_val(ld->dot - ld->cur->vma, (int)(ld->cur - ld->outs));
	return ld_val(ld->dot, LV_ABS);
}

static LdVal ld_sym_val(Ctx *ctx, Ld *ld, Sym *g) {
	uint64_t a = ld_sym_addr(ctx, g);
	Piece *p = g->sect == SEC_ABS ? NULL : ld_sym_piece(ctx, g);
	if (!p || p->out < 0) return ld_val(a, LV_ABS);
	return ld_val(a - ld->outs[p->out].vma, p->out);
}

static uint64_t ld_apply(Ld *ld, int op, uint64_t a, uint64_t b) {
	switch (op) {
	case '+': return a + b;
	case '-': return a - b;
	case '*': return a * b;
	case '/': case '%':
		if (!b) ld_fail(ld, "division by zero in expression");
		return op == '/' ? (uint64_t)((int64_t)a / (int64_t)b) : (uint64_t)((int64_t)a % (int64_t)b);
	case '&': return a & b;
	case '|': return a | b;
	case '^': return a ^ b;
	case 'l': return a << (b & 63);
	case 'r': return a >> (b & 63);
	case '<': return a < b;
	case '>': return a > b;
	case 'L': return a <= b;
	case 'G': return a >= b;
	case 'E': return a == b;
	case 'N': return a != b;
	case 'A': return a && b;
	case 'O': return a || b;
	}
	return 0;
}

static LdVal ld_binop_val(Ld *ld, int op, LdVal a, LdVal b) {
	if (strchr("<>LGENAO", op)) return ld_val(ld_apply(ld, op, ld_abs(ld, a), ld_abs(ld, b)), LV_NUM);
	if (a.sec >= 0 && a.sec == b.sec) return ld_val(ld_apply(ld, op, a.v, b.v), LV_NUM);
	if (!ld->cur) return ld_val(ld_apply(ld, op, ld_abs(ld, a), ld_abs(ld, b)), a.sec == LV_NUM && b.sec == LV_NUM ? LV_NUM : LV_ABS);
	if (a.sec >= 0 && b.sec == LV_NUM) return ld_val(ld_apply(ld, op, a.v, b.v), a.sec);
	if (b.sec >= 0 && a.sec == LV_NUM) return ld_val(ld_apply(ld, op, a.v, b.v), b.sec);
	if (a.sec == LV_NUM && b.sec == LV_NUM) return ld_val(ld_apply(ld, op, a.v, b.v), LV_NUM);
	if (a.sec >= 0 && b.sec >= 0) return ld_val(ld_apply(ld, op, ld_abs(ld, a), ld_abs(ld, b)), LV_NUM);
	return ld_val(ld_apply(ld, op, ld_abs(ld, a), ld_abs(ld, b)), op == '-' && a.sec != LV_NUM && b.sec != LV_NUM ? LV_NUM : LV_ABS);
}

static LdVal ld_eval(Ctx *ctx, Ld *ld, LdExpr *e) {
	LdVal a, b;
	LdOut *o;
	switch (e->op) {
	case '#': return ld_val(e->v, LV_NUM);
	case '.': return ld_dot(ld);
	case 's': {
		char *name = intern(e->name);
		LdSym *y = ld_sym(ld, name, 0);
		Sym *g = ld_global(ctx, name);
		LdVal v;
		if (y && y->pass && !(y->provide && g && g->defined)) v = y->val;
		else if (g && g->defined) v = ld_sym_val(ctx, ld, g);
		else v = ld_val(0, LV_NUM);
		if (ld->cur && v.sec == LV_ABS) v.sec = LV_NUM;
		if ((y && y->pass) || (g && g->defined)) return v;
		if (ld->final) ld_fail(ld, "undefined symbol '%s' referenced in expression", e->name);
		return ld_val(0, LV_NUM);
	}
	case 'n': case '~':
		a = ld_eval(ctx, ld, e->a);
		if (ld->cur && a.sec >= 0) return ld_val(e->op == 'n' ? -a.v : ~a.v, a.sec);
		return ld_val(e->op == 'n' ? -ld_abs(ld, a) : ~ld_abs(ld, a), a.sec == LV_NUM ? LV_NUM : LV_ABS);
	case '!': return ld_val(!ld_abs(ld, ld_eval(ctx, ld, e->a)), LV_NUM);
	case '?': return ld_eval(ctx, ld, ld_abs(ld, ld_eval(ctx, ld, e->a)) ? e->b : e->c);
	case 'a': {
		LdVal d = ld_dot(ld);
		uint64_t x = ld_alignup(ld->dot, ld_abs(ld, ld_eval(ctx, ld, e->a)));
		return d.sec >= 0 ? ld_val(x - ld->outs[d.sec].vma, d.sec) : ld_val(x, LV_ABS);
	}
	case 'b': {
		a = ld_eval(ctx, ld, e->a);
		uint64_t x = ld_alignup(ld_abs(ld, a), ld_abs(ld, ld_eval(ctx, ld, e->b)));
		return a.sec >= 0 ? ld_val(x - ld->outs[a.sec].vma, a.sec) : ld_val(x, a.sec);
	}
	case 'D':
		o = ld_out_named(ld, e->name);
		return o && o->kept ? ld_val(0, (int)(o - ld->outs)) : ld_val(o ? o->vma : 0, LV_ABS);
	case 'M': o = ld_out_named(ld, e->name); return ld_val(o ? o->lma : 0, LV_ABS);
	case 'Z': o = ld_out_named(ld, e->name); return ld_val(o ? o->size : 0, LV_NUM);
	case 'Y': o = ld_out_named(ld, e->name); return ld_val(o ? o->align : 0, LV_NUM);
	case 'B': return ld_val(ld_abs(ld, ld_eval(ctx, ld, e->a)), LV_ABS);
	case 'R': return ld_eval(ctx, ld, e->b ? e->b : e->a);
	case 'd': {
		char *name = intern(e->name);
		LdSym *y = ld_sym(ld, name, 0);
		Sym *g = ld_global(ctx, name);
		return ld_val((y && y->pass == ld->pass) || (g && g->defined), LV_NUM);
	}
	case 'P': return ld_val(0x1000, LV_NUM);
	case 'H': return ld_val(64 + 56 * (uint64_t)(ld->nphdrs ? ld->nphdrs : ld->nsegs ? ld->nsegs : 2), LV_NUM);
	case 'g': {
		uint64_t x = ld_abs(ld, ld_eval(ctx, ld, e->a)), n = 0;
		while (n < 64 && ((uint64_t)1 << n) < x) n++;
		return ld_val(n, LV_NUM);
	}
	case 'S': {
		uint64_t m = ld_abs(ld, ld_eval(ctx, ld, e->a));
		return ld_val(ld_alignup(ld->dot, m) + (ld->dot & (m - 1)), LV_ABS);
	}
	}
	a = ld_eval(ctx, ld, e->a);
	b = ld_eval(ctx, ld, e->b);
	if (e->op == 'x') return ld_abs(ld, a) > ld_abs(ld, b) ? a : b;
	if (e->op == 'm') return ld_abs(ld, a) < ld_abs(ld, b) ? a : b;
	return ld_binop_val(ld, e->op, a, b);
}

static void ld_exec(Ctx *ctx, Ld *ld, LdStmt *st);

static void ld_assign(Ctx *ctx, Ld *ld, LdStmt *st) {
	LdVal v = ld_eval(ctx, ld, st->e);
	if (!strcmp(st->name, ".")) {
		if (st->op != '=') v = ld_binop_val(ld, st->op, ld_dot(ld), v);
		uint64_t t = ld->cur && v.sec == LV_NUM ? ld->start + v.v : ld_abs(ld, v);
		if (ld->cur && t < ld->dot && ld->final) ld_fail(ld, "cannot move the location counter backwards in '%s'", ld->cur->name);
		st->dva = ld->dot;
		st->dval = t;
		ld->dot = t;
		return;
	}
	char *name = intern(st->name);
	if (st->op != '=') {
		LdExpr x;
		memset(&x, 0, sizeof(x));
		x.op = 's';
		x.name = name;
		v = ld_binop_val(ld, st->op, ld_eval(ctx, ld, &x), v);
	}
	if (ld->cur && v.sec == LV_NUM) v = ld_val(v.v, (int)(ld->cur - ld->outs));
	LdSym *y = ld_sym(ld, name, 1);
	y->val = v;
	y->provide = st->provide;
	y->pass = ld->pass;
}

static void ld_section_exec(Ctx *ctx, Ld *ld, LdStmt *st) {
	LdOut *o = &ld->outs[st->out];
	if (o->discard || (!o->kept && !o->syms)) return;
	uint64_t al = o->align, attr = 0, before = ld->dot;
	if (st->align) {
		attr = ld_abs(ld, ld_eval(ctx, ld, st->align));
		if (attr > al) al = attr;
	}
	uint64_t vma = st->addr ? ld_alignup(ld_abs(ld, ld_eval(ctx, ld, st->addr)), attr) : ld_alignup(ld->dot, al);
	uint64_t lma = st->at ? ld_abs(ld, ld_eval(ctx, ld, st->at)) : !ld->has_delta ? vma :
		st->addr ? vma + ld->delta : ld_alignup(vma + ld->delta, al);
	ld->dot = ld->start = vma;
	ld->cur = o;
	o->vma = vma;
	o->lma = lma;
	for (LdStmt *b = st->body; b; b = b->next) ld_exec(ctx, ld, b);
	o->size = ld->dot - vma;
	o->align = al;
	ld->cur = NULL;
	if (!o->size) ld->dot = o->kept ? before : vma;
	if (st->addr && vma) {
		uint64_t low = vma & -vma;
		if (low < o->align) o->align = low;
	}
	if (o->size) {
		ld->delta = lma - vma;
		ld->has_delta = 1;
	}
}

static void ld_exec(Ctx *ctx, Ld *ld, LdStmt *st) {
	ld->line = st->line;
	switch (st->kind) {
	case LS_ASSIGN:
		ld_assign(ctx, ld, st);
		break;
	case LS_ASSERT:
		if (ld->final && !ld_abs(ld, ld_eval(ctx, ld, st->e))) ld_fail(ld, "%s", st->msg);
		break;
	case LS_SECTION:
		ld_section_exec(ctx, ld, st);
		break;
	case LS_INPUT:
		for (int i = 0; i < st->npc; i++) {
			Piece *p = &ctx->pieces[st->pc[i]];
			if (p->out < 0) continue;
			uint64_t a = (uint64_t)p->align;
			uint64_t r = ((uint64_t)p->start % a + a - ld->dot % a) % a;
			p->va = ld->dot + r;
			ld->dot = p->va + (uint64_t)(p->end - p->start);
		}
		break;
	case LS_DATA: {
		LdOut *cur = ld->cur;
		st->dva = ld->dot;
		ld->cur = NULL;
		st->dval = ld->final ? ld_abs(ld, ld_eval(ctx, ld, st->e)) : 0;
		ld->cur = cur;
		ld->dot += (uint64_t)st->size;
		break;
	}
	}
}

static void ld_segments(Ld *ld) {
	if (ld->nphdrs) {
		ld->nsegs = ld->nphdrs;
		int *cur = NULL;
		int ncur = 0;
		for (LdStmt *st = ld->top; st; st = st->next) {
			if (st->kind != LS_SECTION) continue;
			LdOut *o = &ld->outs[st->out];
			if (st->has_phdr) { cur = st->phdr; ncur = st->nphdr; }
			o->seg = -1;
			if (o->discard || !o->kept) continue;
			for (int i = 0; i < ncur; i++) if (ld->segs[cur[i]].type == 1 && o->seg < 0) o->seg = cur[i];
		}
		return;
	}
	int *ord = (int *)malloc(sizeof(int) * (ld->nouts + 1));
	int n = 0;
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		o->seg = -1;
		if (o->discard || !o->kept) continue;
		int j = n++;
		while (j > 0 && ld->outs[ord[j - 1]].lma > o->lma) { ord[j] = ord[j - 1]; j--; }
		ord[j] = i;
	}
	ld->nsegs = 0;
	LdOut *prev = NULL;
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		if (o->size == 0) continue;
		int same = 0;
		if (prev) {
			uint64_t end = prev->vma + prev->size;
			uint64_t lastpage = (end ? end - 1 : 0) & ~(uint64_t)0xFFF;
			same = o->vma - o->lma == prev->vma - prev->lma && o->vma >= end && !(prev->nobits && !o->nobits && o->size) &&
				((o->vma & ~(uint64_t)0xFFF) == lastpage ||
				 ((o->vma & ~(uint64_t)0xFFF) == lastpage + 0x1000 && (o->flags & 1) == (prev->flags & 1)));
		}
		if (!same) {
			if (ld->nsegs == ld->capsegs) {
				ld->capsegs = ld->capsegs ? ld->capsegs * 2 : 8;
				ld->segs = (LdSeg *)realloc(ld->segs, sizeof(LdSeg) * ld->capsegs);
				if (!ld->segs) { perror("realloc"); exit(1); }
			}
			memset(&ld->segs[ld->nsegs], 0, sizeof(LdSeg));
			ld->segs[ld->nsegs].type = 1;
			ld->nsegs++;
		}
		o->seg = ld->nsegs - 1;
		prev = o;
	}
	free(ord);
}

static uint64_t ld_signature(Ld *ld) {
	uint64_t h = 1469598103934665603ULL;
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		h = (h ^ o->vma) * 1099511628211ULL;
		h = (h ^ o->lma) * 1099511628211ULL;
		h = (h ^ o->size) * 1099511628211ULL;
	}
	for (int i = 0; i < ld->nsyms; i++) h = (h ^ ld_abs(ld, ld->syms[i].val)) * 1099511628211ULL;
	h = (h ^ (uint64_t)ld->nsegs) * 1099511628211ULL;
	return h;
}

static void ld_layout(Ctx *ctx, Ld *ld) {
	uint64_t prev = 0;
	for (ld->pass = 1;; ld->pass++) {
		ld->dot = 0;
		ld->cur = NULL;
		ld->has_delta = 0;
		for (LdStmt *st = ld->top; st; st = st->next) ld_exec(ctx, ld, st);
		ld_segments(ld);
		uint64_t sig = ld_signature(ld);
		if (ld->final) break;
		if (ld->pass > 1 && sig == prev) ld->final = 1;
		else if (ld->pass > 30) { ld->line = 0; ld_fail(ld, "the addresses in the script do not settle"); }
		prev = sig;
	}
	ld->line = 0;
}

static int ld_c_ident(const char *s) {
	if (!isalpha((unsigned char)*s) && *s != '_') return 0;
	for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '_') return 0;
	return 1;
}

static void ld_define(Ctx *ctx, Ld *ld) {
	for (int i = 0; i < ld->nsyms; i++) {
		LdSym *y = &ld->syms[i];
		Sym *g = ld_global(ctx, y->name);
		if (y->provide && (!g || g->defined)) continue;
		int created;
		if (!g) g = link_global(ctx, y->name, &created);
		g->defined = 1;
		g->sect = SEC_ABS;
		g->off = (long)ld_abs(ld, y->val);
		g->size = 0;
		g->weak = g->weak_def = 0;
		g->common = 0;
		g->tentative = 0;
		g->piece = 0;
		g->ld_out = y->val.sec >= 0 ? y->val.sec + 1 : 0;
	}
	for (Sym *g = ctx->globals; g; g = g->gnext) {
		if (g->defined || g->is_static) continue;
		int stop = strncmp(g->name, "__stop_", 7) == 0;
		if (!stop && strncmp(g->name, "__start_", 8) != 0) continue;
		LdOut *o = ld_find_out(ld, g->name + (stop ? 7 : 8));
		if (!o || !o->kept || !ld_c_ident(o->name)) continue;
		g->defined = 1;
		g->sect = SEC_ABS;
		g->off = (long)(o->vma + (stop ? o->size : 0));
		g->ld_out = (int)(o - ld->outs) + 1;
	}
}

static void ld_nops(uint8_t *p, size_t n) {
	static const char *nops[] = { "", "\x90", "\x66\x90", "\x0f\x1f\x00", "\x0f\x1f\x40\x00", "\x0f\x1f\x44\x00\x00",
		"\x66\x0f\x1f\x44\x00\x00", "\x0f\x1f\x80\x00\x00\x00\x00", "\x0f\x1f\x84\x00\x00\x00\x00\x00",
		"\x66\x0f\x1f\x84\x00\x00\x00\x00\x00", "\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00" };
	while (n) {
		size_t k = n > 10 ? 10 : n;
		memcpy(p, nops[k], k);
		p += k;
		n -= k;
	}
}

static void ld_gap(LdOut *o, LdStmt *f, uint8_t *p, size_t n) {
	uint8_t *fill = f->fill;
	int nfill = f->nfill;
	if (nfill) for (size_t i = 0; i < n; i++) p[i] = fill[i % (size_t)nfill];
	else if (o->flags & 4) ld_nops(p, n);
	else memset(p, 0, n);
}

static uint8_t *ld_contents(Ctx *ctx, LdOut *o) {
	uint8_t *d = (uint8_t *)malloc(o->size + 1);
	uint64_t at = o->vma;
	LdStmt *f = o->st;
	for (LdStmt *b = o->st->body; b; b = b->next) {
		if (b->kind == LS_FILL) f = b;
		else if (b->kind == LS_ASSIGN && !strcmp(b->name, ".") && b->dval > b->dva) {
			ld_gap(o, f, d + (at - o->vma), (size_t)(b->dva - at));
			ld_gap(o, f, d + (b->dva - o->vma), (size_t)(b->dval - b->dva));
			at = b->dval;
		} else if (b->kind == LS_INPUT) {
			for (int i = 0; i < b->npc; i++) {
				Piece *p = &ctx->pieces[b->pc[i]];
				size_t n = (size_t)(p->end - p->start);
				if (p->out < 0) continue;
				ld_gap(o, f, d + (at - o->vma), (size_t)(p->va - at));
				if (p->sect == SEC_BSS) memset(d + (p->va - o->vma), 0, n);
				else memcpy(d + (p->va - o->vma), sec_buf(ctx, p->sect)->data + p->start, n);
				at = p->va + n;
			}
		} else if (b->kind == LS_DATA) {
			ld_gap(o, f, d + (at - o->vma), (size_t)(b->dva - at));
			for (int k = 0; k < b->size; k++) d[b->dva - o->vma + (uint64_t)k] = (uint8_t)(b->dval >> (8 * k));
			at = b->dva + (uint64_t)b->size;
		}
	}
	ld_gap(o, f, d + (at - o->vma), (size_t)(o->vma + o->size - at));
	return d;
}

static int ld_loaded(LdOut *o) { return !o->discard && o->kept && !o->nobits && !o->st->noload && o->size; }

static Buf ld_image(Ctx *ctx, Ld *ld, uint64_t *base, uint64_t *end) {
	Buf file;
	buf_init(&file);
	uint64_t lo = UINT64_MAX, hi = 0;
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		if (o->discard || !o->kept) continue;
		if (o->lma + o->size > hi) hi = o->lma + o->size;
		if (ld_loaded(o) && o->lma < lo) lo = o->lma;
	}
	if (lo == UINT64_MAX) lo = hi;
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		if (!ld_loaded(o)) continue;
		if (o->lma - lo > 0x40000000) ld_fail(ld, "the image would be larger than 1 GB");
		uint8_t *d = ld_contents(ctx, o);
		buf_zero_to(&file, (size_t)(o->lma - lo + o->size));
		memcpy(file.data + (o->lma - lo), d, o->size);
		free(d);
	}
	*base = lo;
	*end = hi;
	return file;
}

static uint64_t sect_addr(Ctx *ctx, uint64_t *sect_va, int sect, long off) {
	if (sect == SEC_ABS) return (uint64_t)off;
	if (ctx->ld) return ld_addr(ctx, sect, off);
	return sect_va[sect] + (uint64_t)off;
}

static uint64_t sym_addr(Ctx *ctx, uint64_t *sect_va, Sym *s) {
	if (ctx->ld) return ld_sym_addr(ctx, s);
	return sect_addr(ctx, sect_va, s->sect, s->off);
}

static void apply_relocs(Ctx *ctx, uint64_t *sect_va) {
	for (int i = 0; i < ctx->nrelocs; i++) {
		Reloc *r = &ctx->relocs[i];
		uint64_t target;
		if (is_noalloc(ctx, r->in)) continue;
		if (ctx->ld) {
			Piece *p = ld_piece_at(ctx, r->in, (long)r->pos);
			if (!p || p->out < 0) continue;
			Piece *t = r->sym && r->sym->defined && r->sym->sect != SEC_ABS ? ld_sym_piece(ctx, r->sym) :
				!r->sym && r->sect != SEC_ABS ? (r->piece ? &ctx->pieces[r->piece - 1] : ld_piece_at(ctx, r->sect, (long)r->addend)) : NULL;
			if (t && t->out < 0) {
				if (r->sym) { fprintf(stderr, "error: '%s' is defined in discarded section '%s' of %s\n", r->sym->name, t->name, t->file); exit(1); }
				fprintf(stderr, "error: a relocation refers to discarded section '%s' of %s\n", t->name, t->file);
				exit(1);
			}
		}
		if (r->sym && !r->sym->defined) {
			if (!r->sym->weak) { fprintf(stderr, "error: undefined reference to '%s'\n", r->sym->name); exit(1); }
			target = (uint64_t)r->addend;
		} else if (r->sym) target = sym_addr(ctx, sect_va, r->sym) + r->addend;
		else if (r->piece && ctx->ld) target = ctx->pieces[r->piece - 1].va + (uint64_t)(r->addend - ctx->pieces[r->piece - 1].start);
		else target = sect_addr(ctx, sect_va, r->sect, (long)r->addend);
		Buf *b = sec_buf(ctx, r->in);
		int n = rel_size[r->kind];
		int pc = rel_pc(r->kind);
		int64_t v = pc ? (int64_t)target - (int64_t)(sect_addr(ctx, sect_va, r->in, (long)r->pos) + n) : (int64_t)target;
		if (n < 8) {
			int64_t lo = -((int64_t)1 << (8 * n - 1));
			int64_t hi = pc || r->kind == R_ABS32S ? ((int64_t)1 << (8 * n - 1)) - 1 : ((int64_t)1 << (8 * n)) - 1;
			if (v < lo || v > hi) {
				if (r->sym && !r->sym->defined) { fprintf(stderr, "error: weak symbol '%s' is undefined and address 0 is out of reach\n", r->sym->name); exit(1); }
				fprintf(stderr, "error: relocation against '%s' is out of range\n", r->sym ? r->sym->name : "a section");
				exit(1);
			}
		}
		for (int k = 0; k < n; k++) b->data[r->pos + k] = (uint8_t)((uint64_t)v >> (8 * k));
	}
}

static void ld_write_elf(Ctx *ctx, Ld *ld, const char *out_path, uint64_t entry) {
	int nsegs = ld->nsegs;
	for (int i = 0; i < nsegs; i++) {
		LdSeg *g = &ld->segs[i];
		g->filesz = g->memsz = 0;
	}
	int *ord = (int *)malloc(sizeof(int) * (ld->nouts + 1));
	int n = 0;
	for (LdStmt *st = ld->top; st; st = st->next)
		if (st->kind == LS_SECTION && !ld->outs[st->out].discard && ld->outs[st->out].kept) ord[n++] = st->out;
	for (int k = 0; k < n; k++) ld->outs[ord[k]].shndx = k + 1;

	size_t pos = 64 + 56 * (size_t)nsegs;
	for (int i = 0; i < nsegs; i++) {
		LdSeg *g = &ld->segs[i];
		if (g->type != 1) continue;
		uint64_t lo = UINT64_MAX, hi = 0, plo = 0, fhi = 0;
		for (int k = 0; k < n; k++) {
			LdOut *o = &ld->outs[ord[k]];
			if (o->seg != i) continue;
			if (o->vma < lo) { lo = o->vma; plo = o->lma; }
			if (o->vma + o->size > hi) hi = o->vma + o->size;
			if (!o->nobits && !o->st->noload && o->vma + o->size > fhi) fhi = o->vma + o->size;
		}
		if (lo == UINT64_MAX) { g->vaddr = g->paddr = 0; g->off = pos; continue; }
		pos += (size_t)((lo - pos) & 0xFFF);
		g->off = pos;
		g->vaddr = lo;
		g->paddr = g->at ? ld_abs(ld, ld_eval(ctx, ld, g->at)) : plo;
		g->memsz = hi - lo;
		g->filesz = fhi > lo ? fhi - lo : 0;
		g->align = 0x1000;
		g->flags = g->has_flags ? g->flags : 4;
		for (int k = 0; k < n; k++) {
			LdOut *o = &ld->outs[ord[k]];
			if (o->seg != i) continue;
			o->off = g->off + (o->vma - lo);
			if (o->nobits && o->vma < fhi) o->nobits = 0;
			if (!g->has_flags) g->flags |= (o->flags & 1 ? 2 : 0) | (o->flags & 4 ? 1 : 0);
		}
		pos = (size_t)(g->off + g->filesz);
	}
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		if (o->seg >= 0 && ld->segs[o->seg].type == 1) continue;
		pos = align_up(pos, o->align > 0 ? (size_t)o->align : 1);
		o->off = pos;
		if (!o->nobits && !o->st->noload) pos += (size_t)o->size;
	}
	for (int i = 0; i < nsegs && ld->nphdrs; i++) {
		LdSeg *g = &ld->segs[i];
		if (g->type == 1) continue;
		uint64_t lo = UINT64_MAX, hi = 0, off = 0, plo = 0, al = 1;
		for (LdStmt *st = ld->top, *cur = NULL; st; st = st->next) {
			if (st->kind != LS_SECTION) continue;
			if (st->has_phdr) cur = st;
			LdOut *o = &ld->outs[st->out];
			if (!cur || o->discard || !o->kept) continue;
			int in = 0;
			for (int j = 0; j < cur->nphdr; j++) if (cur->phdr[j] == i) in = 1;
			if (!in) continue;
			if (o->vma < lo) { lo = o->vma; off = o->off; plo = o->lma; }
			if (o->vma + o->size > hi) hi = o->vma + o->size;
			if (o->align > al) al = o->align;
		}
		if (lo == UINT64_MAX) continue;
		g->off = off;
		g->vaddr = lo;
		g->paddr = g->at ? ld_abs(ld, ld_eval(ctx, ld, g->at)) : plo;
		g->filesz = g->memsz = hi - lo;
		g->align = al;
		if (!g->has_flags) g->flags = 4;
	}

	Buf file;
	buf_init(&file);
	buf_zero_to(&file, pos);
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		if (o->nobits || o->st->noload || !o->size) continue;
		uint8_t *d = ld_contents(ctx, o);
		buf_zero_to(&file, (size_t)(o->off + o->size));
		memcpy(file.data + o->off, d, o->size);
		free(d);
	}

	Buf symtab, strtab, shstr;
	buf_init(&symtab); buf_init(&strtab); buf_init(&shstr);
	buf_append_byte(&strtab, 0);
	buf_zero_to(&symtab, 24);
	int nlocal = 1;
	uint8_t e[24];
	if (!ctx->freestanding) {
		Piece *p = ld_piece_at(ctx, SEC_TEXT, 0);
		memset(e, 0, 24);
		le32(e, strtab_add(&strtab, "_start"));
		e[4] = 2;
		le16(e+6, (uint16_t)(p && p->out >= 0 ? ld->outs[p->out].shndx : 0xFFF1));
		le64(e+8, ld_addr(ctx, SEC_TEXT, 0));
		buf_append(&symtab, e, 24);
		nlocal++;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (Sym *s = ctx->globals; s; s = s->gnext) {
			if (!s->defined || (s->kind != SYM_FUNC && s->kind != SYM_VAR) || is_noalloc(ctx, s->sect)) continue;
			if (s->is_static != (pass == 0)) continue;
			int shn = 0xFFF1;
			if (s->sect != SEC_ABS) {
				Piece *p = ld_sym_piece(ctx, s);
				if (!p || p->out < 0) continue;
				shn = ld->outs[p->out].shndx;
			} else if (s->ld_out && ld->outs[s->ld_out - 1].shndx) shn = ld->outs[s->ld_out - 1].shndx;
			memset(e, 0, 24);
			le32(e, strtab_add(&strtab, s->name));
			e[4] = (uint8_t)(((pass == 0 ? 0 : s->weak ? 2 : 1) << 4) | (s->kind == SYM_FUNC ? 2 : s->ld_out || s->sect != SEC_ABS ? 1 : 0));
			le16(e+6, (uint16_t)shn);
			le64(e+8, ld_sym_addr(ctx, s));
			le64(e+16, (uint64_t)s->size);
			buf_append(&symtab, e, 24);
			if (pass == 0) nlocal++;
		}
	}

	buf_append_byte(&shstr, 0);
	uint32_t *names = (uint32_t *)malloc(sizeof(uint32_t) * (n + 1));
	for (int k = 0; k < n; k++) names[k] = strtab_add(&shstr, ld->outs[ord[k]].name);
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
	int nsh = n + 4;
	buf_zero_to(&file, sh_off + (size_t)nsh * 64);
	uint8_t *S = file.data + sh_off;
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		int nob = o->nobits || o->st->noload;
		int type = nob ? 8 : (o->flags & 0x300) == 0x200 ? 7 : 1;
		shdr(S + (k + 1) * 64, names[k], type, (uint64_t)((o->flags & 7) | 2), o->vma, o->off, o->size, 0, 0, o->align, 0);
	}
	shdr(S + (n + 1) * 64, n_sym, 2, 0, 0, sym_off, symtab.len, (uint32_t)(n + 2), (uint32_t)nlocal, 8, 24);
	shdr(S + (n + 2) * 64, n_str, 3, 0, 0, str_off, strtab.len, 0, 0, 1, 0);
	shdr(S + (n + 3) * 64, n_shs, 3, 0, 0, shs_off, shstr.len, 0, 0, 1, 0);

	uint8_t *E = file.data;
	E[0]=0x7F; E[1]='E'; E[2]='L'; E[3]='F'; E[4]=2; E[5]=1; E[6]=1;
	le16(E+0x10,2); le16(E+0x12,62); le32(E+0x14,1);
	le64(E+0x18,entry);
	le64(E+0x20,nsegs ? 64 : 0); le64(E+0x28,sh_off);
	le16(E+0x34,64); le16(E+0x36,56); le16(E+0x38,(uint16_t)nsegs);
	le16(E+0x3A,64); le16(E+0x3C,(uint16_t)nsh); le16(E+0x3E,(uint16_t)(nsh - 1));
	for (int i = 0; i < nsegs; i++) {
		LdSeg *g = &ld->segs[i];
		uint8_t *P = file.data + 64 + 56 * i;
		le32(P+0x00,g->type); le32(P+0x04,g->flags);
		le64(P+0x08,g->off); le64(P+0x10,g->vaddr); le64(P+0x18,g->paddr);
		le64(P+0x20,g->filesz); le64(P+0x28,g->memsz); le64(P+0x30,g->align);
	}
	write_out(out_path, &file, 0755);
	free(file.data); free(symtab.data); free(strtab.data); free(shstr.data); free(names); free(ord);
}

static uint64_t ld_entry(Ctx *ctx, Ld *ld) {
	const char *name = ld->entry_cmd ? ld->entry_cmd : ld->entry;
	if (name) {
		Sym *g = ld_global(ctx, intern(name));
		if (g && g->defined) return ld_sym_addr(ctx, g);
		if (!strcmp(name, "_start") && !ctx->freestanding) return ld_addr(ctx, SEC_TEXT, 0);
		ld_fail(ld, "undefined entry symbol '%s'", name);
	}
	if (!ctx->freestanding) return ld_addr(ctx, SEC_TEXT, 0);
	Sym *g = ld_global(ctx, intern("_start"));
	if (g && g->defined) return ld_sym_addr(ctx, g);
	LdOut *o = ld_find_out(ld, ".text");
	if (o && o->kept) return o->vma;
	for (int i = 0; i < ld->nouts; i++) if (!ld->outs[i].discard && ld->outs[i].kept && (ld->outs[i].flags & 4)) return ld->outs[i].vma;
	return 0;
}

static void ld_link(Ctx *ctx, const char *out_path) {
	Ld *ld = ctx->ld;
	resolve_alias_syms(ctx);
	ld_match(ctx, ld);
	ld_layout(ctx, ld);
	ld_define(ctx, ld);
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *a = &ld->outs[i];
		if (a->discard || !a->kept || !a->size || a->st->noload) continue;
		for (int j = i + 1; j < ld->nouts; j++) {
			LdOut *b = &ld->outs[j];
			if (b->discard || !b->kept || !b->size || b->st->noload) continue;
			if (a->vma < b->vma + b->size && b->vma < a->vma + a->size)
				ld_fail(ld, "section '%s' overlaps section '%s'", a->name, b->name);
			if (!a->nobits && !b->nobits && a->lma < b->lma + b->size && b->lma < a->lma + a->size)
				ld_fail(ld, "the load addresses of '%s' and '%s' overlap", a->name, b->name);
		}
	}
	apply_relocs(ctx, NULL);
	uint64_t entry = ld_entry(ctx, ld);
	if (!ctx->binary) {
		ld_write_elf(ctx, ld, out_path, entry);
		return;
	}
	uint64_t base, end;
	Buf file = ld_image(ctx, ld, &base, &end);
	if (ctx->image || ctx->multiboot) {
		int high = entry >= 0xFFFFFFFF80000000ULL;
		for (int i = 0; i < ld->nouts; i++) {
			LdOut *o = &ld->outs[i];
			if (o->discard || !o->kept) continue;
			if (o->vma != o->lma && o->vma - o->lma != 0xFFFFFFFF80000000ULL)
				ld_fail(ld, "'%s' must be loaded at its address, or at its address minus 0xffffffff80000000", o->name);
			if (o->vma != o->lma) high = 1;
		}
		if (ctx->image) write_image(out_path, &file, base, end, entry, high);
		else {
			if (base & 0xFFF) ld_fail(ld, "a multiboot kernel must start on a page boundary");
			write_multiboot(out_path, &file, base, end, entry, high);
		}
	} else write_out(out_path, &file, 0);
	free(file.data);
}

static void write_elf(Ctx *ctx, const char *out_path) {
	if (ctx->ld) {
		ld_link(ctx, out_path);
		return;
	}
	const size_t headers = ctx->binary ? 0 : 64 + 2*56;
	if (ctx->data.len == 0 && !ctx->binary) buf_zero_to(&ctx->data, 8);
	resolve_alias_syms(ctx);
	define_bounds(ctx);

	int ns = SEC_NAMED + ctx->nsecs;
	int *order = (int *)malloc(sizeof(int) * ns);
	int norder = 0;
	order[norder++] = SEC_TEXT;
	for (int k = SK_CODE; k <= SK_DATA; k++) {
		if (k == SK_RO) order[norder++] = SEC_RODATA;
		if (k == SK_DATA) order[norder++] = SEC_DATA;
		for (int i = 0; i < ctx->nsecs; i++) if (ctx->secs[i].kind == k) order[norder++] = SEC_NAMED + i;
	}
	order[norder++] = SEC_BSS;

	size_t text_a = ctx->text_align > 16 ? (size_t)ctx->text_align : 16;
	size_t data_a = ctx->binary ? 16 : 0x1000;
	if ((size_t)ctx->data_align > data_a) data_a = (size_t)ctx->data_align;
	size_t bss_a = ctx->bss_align > 16 ? (size_t)ctx->bss_align : 16;
	size_t text_off = align_up(headers, text_a);
	uint64_t base = ctx->binary ? 0 : ELF_BASE;
	if (ctx->has_ttext) {
		if (ctx->ttext % text_a) { fprintf(stderr, "error: -Ttext address is not aligned to %d bytes\n", (int)text_a); exit(1); }
		if (!ctx->binary) {
			text_off = (size_t)(ctx->ttext & 0xFFF);
			if (text_off < headers) text_off += 0x1000;
		}
		base = ctx->ttext - text_off;
	}
	size_t seg_off = ctx->has_ttext ? text_off : 0;
	uint64_t *sect_va = (uint64_t *)calloc(ns, sizeof(uint64_t));
	size_t *sect_align = (size_t *)calloc(ns, sizeof(size_t));
	int *shndx = (int *)calloc(ns, sizeof(int));
	uint64_t va = base + text_off;
	size_t text_end = 0, data_end = 0;
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		size_t a = x == SEC_TEXT ? text_a : x == SEC_RODATA ? (ctx->rodata_align > 16 ? (size_t)ctx->rodata_align : 16) : x == SEC_DATA ? data_a : x == SEC_BSS ? bss_a :
			(size_t)ctx->secs[x - SEC_NAMED].align;
		sect_align[x] = a;
		shndx[x] = j + 1;
		if (x == SEC_DATA) text_end = (size_t)(va - base);
		if (x == SEC_BSS) data_end = (size_t)(va - base);
		va = align_up(va, a);
		sect_va[x] = va;
		if (x != SEC_BSS) va += sec_buf(ctx, x)->len;
	}
	sect_align[SEC_DATA] = ctx->data_align > 16 ? (size_t)ctx->data_align : 16;
	uint64_t text_va = sect_va[SEC_TEXT];
	uint64_t data_va = sect_va[SEC_DATA];
	uint64_t bss_va = sect_va[SEC_BSS];
	size_t data_off = (size_t)(data_va - base);

	apply_relocs(ctx, sect_va);

	uint64_t entry = text_va;
	if (ctx->entry_sym) entry = (ctx->entry_sym->sect == SEC_ABS ? 0 : sect_va[ctx->entry_sym->sect]) + ctx->entry_sym->off;

	Buf file; buf_init(&file);
	for (int j = 0; j < norder - 1; j++) {
		Buf *b = sec_buf(ctx, order[j]);
		buf_zero_to(&file, (size_t)(sect_va[order[j]] - base));
		buf_append(&file, b->data, b->len);
	}
	if (ctx->binary) {
		if (ctx->image) write_image(out_path, &file, text_va, bss_va + (uint64_t)ctx->bss_size, entry, 0);
		else if (ctx->multiboot) write_multiboot(out_path, &file, text_va, bss_va + (uint64_t)ctx->bss_size, entry, 0);
		else if (entry != text_va) { fprintf(stderr, "error: entry point '%s' is not at the start of the binary\n", ctx->entry_sym->name); exit(1); }
		else write_out(out_path, &file, 0);
		free(file.data); free(order); free(sect_va); free(sect_align); free(shndx);
		return;
	}

	Buf symtab, strtab, shstr;
	buf_init(&symtab); buf_init(&strtab); buf_init(&shstr);
	buf_append_byte(&strtab, 0);
	buf_zero_to(&symtab, 24);
	int nlocal = 1;
	if (!ctx->freestanding) {
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
			if (!s->defined || (s->kind != SYM_FUNC && s->kind != SYM_VAR) || is_noalloc(ctx, s->sect)) continue;
			if (s->is_static != (pass == 0)) continue;
			add_elf_sym(&symtab, &strtab, s, sect_va, shndx, pass == 0 ? 0 : s->weak ? 2 : 1);
			if (pass == 0) nlocal++;
		}
	}

	buf_append_byte(&shstr, 0);
	uint32_t *names = (uint32_t *)malloc(sizeof(uint32_t) * ns);
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		names[x] = strtab_add(&shstr, x == SEC_TEXT ? ".text" : x == SEC_RODATA ? ".rodata" : x == SEC_DATA ? ".data" :
			x == SEC_BSS ? ".bss" : ctx->secs[x - SEC_NAMED].name);
	}
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
	int nsh = norder + 4;
	buf_zero_to(&file, sh_off + (size_t)nsh * 64);
	uint8_t *S = file.data + sh_off;
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		uint64_t size = x == SEC_BSS ? (uint64_t)ctx->bss_size : sec_buf(ctx, x)->len;
		shdr(S+(j+1)*64, names[x], x == SEC_BSS ? 8 : 1, (uint64_t)sec_flags(ctx, x), sect_va[x], (uint64_t)(sect_va[x] - base),
			size, 0, 0, sect_align[x], 0);
	}
	shdr(S+(norder+1)*64, n_sym, 2, 0, 0, sym_off, symtab.len, (uint32_t)(norder + 2), (uint32_t)nlocal, 8, 24);
	shdr(S+(norder+2)*64, n_str, 3, 0, 0, str_off, strtab.len, 0, 0, 1, 0);
	shdr(S+(norder+3)*64, n_shs, 3, 0, 0, shs_off, shstr.len, 0, 0, 1, 0);

	uint8_t *E=file.data;
	E[0]=0x7F;E[1]='E';E[2]='L';E[3]='F';E[4]=2;E[5]=1;E[6]=1;E[7]=0;
	le16(E+0x10,2); le16(E+0x12,62); le32(E+0x14,1);
	le64(E+0x18,entry);
	le64(E+0x20,64); le64(E+0x28,sh_off); le32(E+0x30,0);
	le16(E+0x34,64); le16(E+0x36,56); le16(E+0x38,2);
	le16(E+0x3A,64); le16(E+0x3C,(uint16_t)nsh); le16(E+0x3E,(uint16_t)(nsh - 1));

	uint8_t *P=file.data+64;
	le32(P+0x00,1); le32(P+0x04,5);
	le64(P+0x08,seg_off); le64(P+0x10,base + seg_off); le64(P+0x18,base + seg_off);
	le64(P+0x20,text_end - seg_off); le64(P+0x28,text_end - seg_off); le64(P+0x30,0x1000);
	P += 56;
	le32(P+0x00,1); le32(P+0x04,6);
	le64(P+0x08,data_off); le64(P+0x10,data_va); le64(P+0x18,data_va);
	le64(P+0x20,data_end - data_off); le64(P+0x28,(bss_va - data_va) + (uint64_t)ctx->bss_size); le64(P+0x30,0x1000);

	write_out(out_path, &file, 0755);

	free(file.data); free(symtab.data); free(strtab.data); free(shstr.data);
	free(order); free(sect_va); free(sect_align); free(shndx); free(names);
}

static void link_error(const char *file, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s: error: ", file);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

typedef struct {
	int n;
	char **name;
	long *size;
	int *align, *part;
	long *off;
} BssSplit;

static void bss_split(Ctx *ctx, BssSplit *b) {
	memset(b, 0, sizeof(*b));
	int named = 0;
	for (int i = 0; i < ctx->npieces; i++)
		if (ctx->pieces[i].sect == SEC_BSS && strcmp(ctx->pieces[i].name, ".bss")) named = 1;
	if (!named) return;
	int np = ctx->npieces;
	b->name = (char **)calloc(np, sizeof(char *));
	b->size = (long *)calloc(np, sizeof(long));
	b->align = (int *)calloc(np, sizeof(int));
	b->part = (int *)calloc(np, sizeof(int));
	b->off = (long *)calloc(np, sizeof(long));
	for (int i = 0; i < np; i++) {
		Piece *p = &ctx->pieces[i];
		if (p->sect != SEC_BSS) continue;
		int k;
		for (k = 0; k < b->n && b->name[k] != p->name; k++) {}
		if (k == b->n) { b->name[k] = p->name; b->align[k] = 1; b->n++; }
		long a = p->align, cur = b->size[k];
		b->off[i] = cur + ((p->start % a) - (cur % a) + a) % a;
		b->part[i] = k;
		b->size[k] = b->off[i] + (p->end - p->start);
		if (p->align > b->align[k]) b->align[k] = p->align;
	}
}

static int obj_place(Ctx *ctx, BssSplit *b, int ns, int sect, int piece, long off, long *where) {
	*where = off;
	if (!b->n || sect != SEC_BSS) return sect;
	int best = -1;
	if (piece && ctx->pieces[piece - 1].sect == SEC_BSS) best = piece - 1;
	else for (int i = 0; i < ctx->npieces; i++) {
		Piece *p = &ctx->pieces[i];
		if (p->sect != SEC_BSS || p->start > off || off > p->end) continue;
		if (best < 0 || off < p->end) best = i;
		if (off < p->end) break;
	}
	if (best < 0) return sect;
	*where = b->off[best] + (off - ctx->pieces[best].start);
	return ns + b->part[best];
}

static void write_object(Ctx *ctx, const char *out_path) {
	int ns = SEC_NAMED + ctx->nsecs;
	BssSplit bs;
	bss_split(ctx, &bs);
	int nx = ns + bs.n;
	int *order = (int *)malloc(sizeof(int) * nx);
	int *shndx = (int *)calloc(nx, sizeof(int));
	int *symsec = (int *)calloc(nx, sizeof(int));
	int *rela = (int *)calloc(nx, sizeof(int));
	int *nrel = (int *)calloc(nx, sizeof(int));
	int norder = 0, nsh = 1;
	long where;
	resolve_alias_syms(ctx);
	order[norder++] = SEC_TEXT;
	if (ctx->rodata.len) order[norder++] = SEC_RODATA;
	if (ctx->data.len) order[norder++] = SEC_DATA;
	if (bs.n) for (int k = 0; k < bs.n; k++) order[norder++] = ns + k;
	else if (ctx->bss_size) order[norder++] = SEC_BSS;
	for (int i = 0; i < ctx->nsecs; i++)
		if (ctx->secs[i].kind != SK_NOALLOC) order[norder++] = SEC_NAMED + i;
	for (int j = 0; j < norder; j++) shndx[order[j]] = nsh++;
	for (int i = 0; i < ctx->nrelocs; i++) {
		Reloc *r = &ctx->relocs[i];
		if (shndx[r->in]) nrel[r->in]++;
		if (r->sym) r->sym->ref = 1;
	}
	for (int j = 0; j < norder; j++) if (nrel[order[j]]) rela[order[j]] = nsh++;
	int sh_note = nsh++, sh_sym = nsh++, sh_str = nsh++, sh_shs = nsh++;

	Buf symtab, strtab, shstr;
	buf_init(&symtab); buf_init(&strtab); buf_init(&shstr);
	buf_append_byte(&strtab, 0);
	buf_zero_to(&symtab, 24);
	int nsym = 1;
	uint8_t e[24];
	for (int j = 0; j < norder; j++) {
		memset(e, 0, 24);
		e[4] = 3;
		le16(e+6, (uint16_t)shndx[order[j]]);
		buf_append(&symtab, e, 24);
		symsec[order[j]] = nsym++;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (Sym *s = ctx->globals; s; s = s->gnext) {
			if (s->kind != SYM_FUNC && s->kind != SYM_VAR) continue;
			if (s->is_static != (pass == 0)) continue;
			int sx = s->defined ? obj_place(ctx, &bs, ns, s->sect, s->piece, s->off, &where) : 0;
			if (s->defined && s->sect != SEC_ABS && !shndx[sx]) continue;
			if (!s->defined && (pass == 0 || !s->ref)) continue;
			memset(e, 0, 24);
			le32(e, strtab_add(&strtab, s->name));
			e[4] = (uint8_t)(((pass == 0 ? 0 : s->weak ? 2 : 1) << 4) | (!s->defined ? 0 : s->kind == SYM_FUNC ? 2 : 1));
			if (s->defined && s->tentative && s->sect == SEC_BSS) {
				le16(e+6, 0xFFF2);
				int al = s->ty && s->ty->align > 0 ? s->ty->align : 8;
				le64(e+8, (uint64_t)(s->align > al ? s->align : al));
				le64(e+16, (uint64_t)s->size);
			} else if (s->defined) {
				le16(e+6, (uint16_t)(s->sect == SEC_ABS ? 0xFFF1 : shndx[sx]));
				le64(e+8, (uint64_t)where);
				le64(e+16, (uint64_t)s->size);
			}
			buf_append(&symtab, e, 24);
			s->elf_index = nsym++;
		}
	}
	int nlocal = 1 + norder;
	for (Sym *s = ctx->globals; s; s = s->gnext)
		if (s->is_static && s->elf_index) nlocal++;

	Buf *rb = (Buf *)calloc(nx, sizeof(Buf));
	for (int i = 0; i < ctx->nrelocs; i++) {
		Reloc *r = &ctx->relocs[i];
		if (!shndx[r->in]) continue;
		long long add = r->addend;
		int idx;
		Sym *y = r->sym;
		if (y && y->defined && (y->is_static || !y->elf_index)) {
			if (y->sect == SEC_ABS) { idx = 0; add += y->off; }
			else { idx = symsec[obj_place(ctx, &bs, ns, y->sect, y->piece, y->off, &where)]; add += where; }
		} else if (y) {
			if (!y->elf_index) link_error(out_path, "undefined reference to '%s'", y->name);
			idx = y->elf_index;
		} else if (r->sect == SEC_ABS) idx = 0;
		else {
			idx = symsec[obj_place(ctx, &bs, ns, r->sect, r->piece, (long)add, &where)];
			add = where;
		}
		if (rel_pc(r->kind)) add -= rel_size[r->kind];
		uint8_t x[24];
		le64(x, (uint64_t)r->pos);
		le64(x+8, ((uint64_t)idx << 32) | (uint64_t)rel_elf[r->kind]);
		le64(x+16, (uint64_t)add);
		buf_append(&rb[r->in], x, 24);
	}

	Buf file; buf_init(&file);
	buf_zero_to(&file, 64);
	size_t *offs = (size_t *)calloc(nsh, sizeof(size_t));
	size_t *sizes = (size_t *)calloc(nsh, sizeof(size_t));
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		buf_zero_to(&file, align_up(file.len, 16));
		offs[shndx[x]] = file.len;
		if (x >= ns) sizes[shndx[x]] = (size_t)bs.size[x - ns];
		else if (x == SEC_BSS) sizes[shndx[x]] = (size_t)ctx->bss_size;
		else {
			Buf *b = sec_buf(ctx, x);
			buf_append(&file, b->data, b->len);
			sizes[shndx[x]] = b->len;
		}
	}
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		if (!rela[x]) continue;
		buf_zero_to(&file, align_up(file.len, 8));
		offs[rela[x]] = file.len;
		sizes[rela[x]] = rb[x].len;
		buf_append(&file, rb[x].data, rb[x].len);
	}
	offs[sh_note] = file.len;
	buf_zero_to(&file, align_up(file.len, 8));
	offs[sh_sym] = file.len;
	sizes[sh_sym] = symtab.len;
	buf_append(&file, symtab.data, symtab.len);
	offs[sh_str] = file.len;
	sizes[sh_str] = strtab.len;
	buf_append(&file, strtab.data, strtab.len);

	buf_append_byte(&shstr, 0);
	uint32_t *names = (uint32_t *)calloc(nsh, sizeof(uint32_t));
	char nb[300];
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		const char *nm = x == SEC_TEXT ? ".text" : x == SEC_RODATA ? ".rodata" : x == SEC_DATA ? ".data" :
			x == SEC_BSS ? ".bss" : x >= ns ? bs.name[x - ns] : ctx->secs[x - SEC_NAMED].name;
		names[shndx[x]] = strtab_add(&shstr, nm);
		if (rela[x]) {
			snprintf(nb, sizeof(nb), ".rela%s", nm);
			names[rela[x]] = strtab_add(&shstr, nb);
		}
	}
	names[sh_note] = strtab_add(&shstr, ".note.GNU-stack");
	names[sh_sym] = strtab_add(&shstr, ".symtab");
	names[sh_str] = strtab_add(&shstr, ".strtab");
	names[sh_shs] = strtab_add(&shstr, ".shstrtab");
	offs[sh_shs] = file.len;
	sizes[sh_shs] = shstr.len;
	buf_append(&file, shstr.data, shstr.len);
	buf_zero_to(&file, align_up(file.len, 8));
	size_t sh_off = file.len;
	buf_zero_to(&file, sh_off + (size_t)nsh * 64);
	uint8_t *S = file.data + sh_off;
	for (int j = 0; j < norder; j++) {
		int x = order[j];
		int a = x >= ns ? bs.align[x - ns] : x == SEC_TEXT ? (ctx->text_align > 16 ? ctx->text_align : 16) :
			x == SEC_RODATA ? (ctx->rodata_align > 16 ? ctx->rodata_align : 16) :
			x == SEC_DATA ? (ctx->data_align > 8 ? ctx->data_align : 8) :
			x == SEC_BSS ? (ctx->bss_align > 8 ? ctx->bss_align : 8) :
			(ctx->secs[x - SEC_NAMED].align > 1 ? ctx->secs[x - SEC_NAMED].align : 1);
		int i = shndx[x];
		int type = x == SEC_BSS || x >= ns ? 8 : x >= SEC_NAMED && !strncmp(ctx->secs[x - SEC_NAMED].name, ".note", 5) ? 7 : 1;
		shdr(S + i * 64, names[i], type, (uint64_t)(x >= ns ? 3 : sec_flags(ctx, x)), 0, offs[i], sizes[i], 0, 0, (uint64_t)a, 0);
		if (rela[x]) {
			i = rela[x];
			shdr(S + i * 64, names[i], 4, 0x40, 0, offs[i], sizes[i], (uint32_t)sh_sym, (uint32_t)shndx[x], 8, 24);
		}
	}
	shdr(S + sh_note * 64, names[sh_note], 1, 0, 0, offs[sh_note], 0, 0, 0, 1, 0);
	shdr(S + sh_sym * 64, names[sh_sym], 2, 0, 0, offs[sh_sym], sizes[sh_sym], (uint32_t)sh_str, (uint32_t)nlocal, 8, 24);
	shdr(S + sh_str * 64, names[sh_str], 3, 0, 0, offs[sh_str], sizes[sh_str], 0, 0, 1, 0);
	shdr(S + sh_shs * 64, names[sh_shs], 3, 0, 0, offs[sh_shs], sizes[sh_shs], 0, 0, 1, 0);
	uint8_t *E = file.data;
	E[0]=0x7F; E[1]='E'; E[2]='L'; E[3]='F'; E[4]=2; E[5]=1; E[6]=1;
	le16(E+0x10,1); le16(E+0x12,62); le32(E+0x14,1);
	le64(E+0x28,sh_off);
	le16(E+0x34,64); le16(E+0x3A,64); le16(E+0x3C,(uint16_t)nsh); le16(E+0x3E,(uint16_t)sh_shs);
	write_out(out_path, &file, 0);
	free(file.data); free(symtab.data); free(strtab.data); free(shstr.data);
	for (int i = 0; i < nx; i++) free(rb[i].data);
	if (bs.n) { free(bs.name); free(bs.size); free(bs.align); free(bs.part); free(bs.off); }
	free(rb); free(order); free(shndx); free(symsec); free(rela); free(nrel); free(offs); free(sizes); free(names);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static Sym *link_global(Ctx *ctx, char *name, int *created) {
	for (Sym *g = ctx->globals; g; g = g->gnext)
		if (g->name == name && !g->is_static && (g->kind == SYM_FUNC || g->kind == SYM_VAR)) { *created = 0; return g; }
	Sym *g = (Sym *)calloc(1, sizeof(Sym));
	g->name = name;
	g->kind = SYM_VAR;
	g->ty = ty_char;
	g->is_asm = 1;
	g->gnext = ctx->globals;
	ctx->globals = g;
	*created = 1;
	return g;
}

static long link_got(Ctx *ctx, Sym *y) {
	if (y->got_off) return y->got_off - 1;
	int g = section_index(ctx, intern(".got"), SK_DATA);
	Buf *b = sec_buf(ctx, g);
	raise_align(ctx, g, 8);
	buf_zero_to(b, align_up(b->len, 8));
	y->got_off = (long)b->len + 1;
	buf_zero_to(b, b->len + 8);
	add_reloc(ctx, R_ABS64, (size_t)(y->got_off - 1), y, 0, 0);
	ctx->relocs[ctx->nrelocs - 1].in = g;
	return y->got_off - 1;
}

static int elf_ok(const uint8_t *d, size_t n) {
	return n >= 64 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F' && d[4] == 2 && d[5] == 1 &&
		rd16(d + 0x10) == 1 && rd16(d + 0x12) == 62 && rd64(d + 0x28) + (uint64_t)rd16(d + 0x3C) * 64 <= n;
}

static const uint8_t *elf_sh(const uint8_t *d, int i) { return d + rd64(d + 0x28) + (size_t)i * 64; }

static void load_object(Ctx *ctx, const uint8_t *d, size_t n, const char *name) {
	if (!elf_ok(d, n)) link_error(name, "not an x86-64 relocatable ELF object");
	int shnum = rd16(d + 0x3C);
	const char *shstr = (const char *)d + rd64(elf_sh(d, rd16(d + 0x3E)) + 0x18);
	int *map = (int *)malloc(sizeof(int) * shnum);
	long *base = (long *)calloc(shnum, sizeof(long));
	int *pidx = (int *)calloc(shnum, sizeof(int));
	int symsh = 0;
	ctx->tok.file = name;
	ctx->tok.line = 0;
	piece_begin(ctx);
	for (int i = 0; i < shnum; i++) {
		const uint8_t *sh = elf_sh(d, i);
		uint32_t type = rd32(sh + 4);
		uint64_t flags = rd64(sh + 8), off = rd64(sh + 0x18), size = rd64(sh + 0x20), align = rd64(sh + 0x30);
		const char *nm = shstr + rd32(sh);
		map[i] = -3;
		if (type == 2) symsh = i;
		if (i == 0 || !(flags & 2) || (type == 7 && !ctx->ld) || !strcmp(nm, ".eh_frame")) continue;
		if (type != 1 && type != 7 && type != 8 && type != 14 && type != 15 && type != 16) continue;
		if (flags & 0x400) link_error(name, "thread-local storage is not supported (section %s)", nm);
		if (type != 8 && off + size > n) link_error(name, "section %s lies outside the file", nm);
		int sect;
		if (type == 8) sect = SEC_BSS;
		else if (!strcmp(nm, ".text")) sect = SEC_TEXT;
		else if (!strcmp(nm, ".rodata")) sect = SEC_RODATA;
		else if (!strcmp(nm, ".data")) sect = SEC_DATA;
		else sect = section_index(ctx, intern(nm), flags & 4 ? SK_CODE : flags & 1 ? SK_DATA : SK_RO);
		if (align < 1) align = 1;
		raise_align(ctx, sect, (int)align);
		if (sect == SEC_BSS) {
			ctx->bss_size = (long)align_up((size_t)ctx->bss_size, (size_t)align);
			base[i] = ctx->bss_size;
			ctx->bss_size += (long)size;
		} else {
			Buf *b = sec_buf(ctx, sect);
			buf_zero_to(b, align_up(b->len, (size_t)align));
			base[i] = (long)b->len;
			buf_append(b, d + off, (size_t)size);
		}
		add_piece(ctx, sect, base[i], base[i] + (long)size, intern(nm), name, (int)align);
		if (ctx->track) ctx->pieces[ctx->npieces - 1].note = type == 7;
		pidx[i] = ctx->track ? ctx->npieces : 0;
		map[i] = sect;
	}
	if (!symsh) { piece_end(ctx, name); free(map); free(base); free(pidx); return; }
	const uint8_t *ssh = elf_sh(d, symsh);
	const uint8_t *symtab = d + rd64(ssh + 0x18);
	int nsyms = (int)(rd64(ssh + 0x20) / 24);
	const char *strtab = (const char *)d + rd64(elf_sh(d, (int)rd32(ssh + 0x28)) + 0x18);
	Sym **syms = (Sym **)calloc(nsyms, sizeof(Sym *));
	int *ssec = (int *)calloc(nsyms, sizeof(int));
	for (int k = 1; k < nsyms; k++) {
		const uint8_t *e = symtab + 24 * k;
		const char *nm = strtab + rd32(e);
		int bind = e[4] >> 4, type = e[4] & 15, shn = rd16(e + 6);
		uint64_t val = rd64(e + 8), sz = rd64(e + 16);
		int created;
		if (type == 4) continue;
		if (type == 3) { ssec[k] = shn; continue; }
		if (type == 6) link_error(name, "thread-local symbol '%s' is not supported", nm);
		if (shn != 0 && shn < 0xFF00 && (shn >= shnum || map[shn] == -3)) continue;
		if (bind == 0) {
			if (!shn || !*nm) continue;
			Sym *y = (Sym *)calloc(1, sizeof(Sym));
			y->name = intern(nm);
			y->kind = type == 2 ? SYM_FUNC : SYM_VAR;
			y->ty = ty_char;
			y->is_static = 1;
			y->defined = 1;
			y->sect = shn == 0xFFF1 ? SEC_ABS : map[shn];
			y->off = shn == 0xFFF1 ? (long)val : base[shn] + (long)val;
			y->piece = shn == 0xFFF1 ? 0 : pidx[shn];
			y->size = (long)sz;
			y->gnext = ctx->globals;
			ctx->globals = y;
			syms[k] = y;
			continue;
		}
		Sym *g = link_global(ctx, intern(nm), &created);
		syms[k] = g;
		if (!shn) { if (bind == 2 && created) g->weak = 1; continue; }
		if (shn == 0xFFF2) {
			if (!g->defined) {
				if ((long)sz > g->common) g->common = (long)sz;
				if ((int)val > g->common_align) g->common_align = (int)val;
			}
			continue;
		}
		if (g->defined) {
			if (bind == 2) continue;
			if (!g->weak_def && !g->tentative) link_error(name, "multiple definition of '%s'", nm);
		}
		g->tentative = 0;
		g->defined = 1;
		g->sect = shn == 0xFFF1 ? SEC_ABS : map[shn];
		g->off = shn == 0xFFF1 ? (long)val : base[shn] + (long)val;
		g->piece = shn == 0xFFF1 ? 0 : pidx[shn];
		g->size = (long)sz;
		g->weak = g->weak_def = bind == 2;
		g->common = 0;
		if (g->is_asm) g->kind = type == 2 ? SYM_FUNC : SYM_VAR;
	}
	for (int i = 0; i < shnum; i++) {
		const uint8_t *sh = elf_sh(d, i);
		if (rd32(sh + 4) != 4) continue;
		int tgt = (int)rd32(sh + 0x2C);
		if (tgt >= shnum || map[tgt] == -3) continue;
		const uint8_t *r = d + rd64(sh + 0x18);
		int nr = (int)(rd64(sh + 0x20) / 24);
		for (int j = 0; j < nr; j++, r += 24) {
			uint64_t off = rd64(r), info = rd64(r + 8);
			long long a = (long long)rd64(r + 16);
			int type = (int)(info & 0xFFFFFFFF), k = (int)(info >> 32);
			Sym *y = NULL;
			int sect = 0, piece = 0;
			RelKind kind;
			if (type == 0) continue;
			if (k == 0) sect = SEC_ABS;
			else if (k >= nsyms) link_error(name, "bad symbol in relocation");
			else if (ssec[k]) {
				if (ssec[k] >= shnum || map[ssec[k]] == -3) link_error(name, "relocation against a discarded section");
				sect = map[ssec[k]];
				a += base[ssec[k]];
				piece = pidx[ssec[k]];
			} else if (syms[k]) y = syms[k];
			else link_error(name, "relocation against a symbol in a discarded section");
			switch (type) {
			case 1: kind = R_ABS64; break;
			case 2: kind = R_PC32; a += 4; break;
			case 4: kind = R_PLT32; a += 4; break;
			case 10: kind = R_ABS32; break;
			case 11: kind = R_ABS32S; break;
			case 12: kind = R_ABS16; break;
			case 13: kind = R_PC16; a += 2; break;
			case 14: kind = R_ABS8; break;
			case 15: kind = R_PC8; a += 1; break;
			case 24: kind = R_PC64; a += 8; break;
			case 9: case 41: case 42:
				if (!y) link_error(name, "GOT relocation against a section");
				kind = R_PC32;
				sect = section_index(ctx, intern(".got"), SK_DATA);
				a += link_got(ctx, y) + 4;
				y = NULL;
				piece = 0;
				break;
			default:
				link_error(name, "unsupported relocation type %d", type);
				kind = R_ABS64;
			}
			add_reloc(ctx, kind, (size_t)(base[tgt] + (long)off), y, sect, a);
			ctx->relocs[ctx->nrelocs - 1].in = map[tgt];
			ctx->relocs[ctx->nrelocs - 1].piece = piece;
		}
	}
	piece_end(ctx, name);
	free(map); free(base); free(pidx); free(syms); free(ssec);
}

typedef struct {
	const uint8_t *data;
	size_t size;
	char name[64];
	int loaded;
} ArMember;

static int member_needed(Ctx *ctx, const ArMember *m) {
	const uint8_t *d = m->data;
	if (!elf_ok(d, m->size)) return 0;
	int shnum = rd16(d + 0x3C);
	for (int i = 0; i < shnum; i++) {
		const uint8_t *sh = elf_sh(d, i);
		if (rd32(sh + 4) != 2) continue;
		const uint8_t *symtab = d + rd64(sh + 0x18);
		int nsyms = (int)(rd64(sh + 0x20) / 24);
		const char *strtab = (const char *)d + rd64(elf_sh(d, (int)rd32(sh + 0x28)) + 0x18);
		for (int k = 1; k < nsyms; k++) {
			const uint8_t *e = symtab + 24 * k;
			int bind = e[4] >> 4, shn = rd16(e + 6);
			if (bind == 0 || shn == 0 || shn == 0xFFF2) continue;
			char *id = intern(strtab + rd32(e));
			for (Sym *g = ctx->globals; g; g = g->gnext)
				if (g->name == id && !g->is_static && !g->defined && (g->ref || g->common)) return 1;
		}
	}
	return 0;
}

static void load_archive(Ctx *ctx, const uint8_t *d, size_t n, const char *name) {
	ArMember *ms = NULL;
	int nm = 0, cap = 0;
	const char *longnames = NULL;
	size_t p = 8;
	while (p + 60 <= n) {
		const uint8_t *h = d + p;
		char nb[17], sb[11];
		memcpy(nb, h, 16); nb[16] = 0;
		memcpy(sb, h + 48, 10); sb[10] = 0;
		size_t size = (size_t)strtoul(sb, NULL, 10);
		const uint8_t *data = h + 60;
		if (p + 60 + size > n) link_error(name, "truncated archive");
		if (!strncmp(nb, "//", 2)) longnames = (const char *)data;
		else if (nb[0] != '/' || isdigit((unsigned char)nb[1])) {
			if (nm == cap) {
				cap = cap ? cap * 2 : 16;
				ms = (ArMember *)realloc(ms, sizeof(ArMember) * cap);
			}
			ArMember *m = &ms[nm++];
			const char *src = nb;
			if (nb[0] == '/' && longnames) src = longnames + strtoul(nb + 1, NULL, 10);
			int k = 0;
			while (k < 63 && src[k] && src[k] != '/' && src[k] != '\n' && !(src == nb && src[k] == ' ')) { m->name[k] = src[k]; k++; }
			m->name[k] = 0;
			m->data = data;
			m->size = size;
			m->loaded = 0;
		}
		p += 60 + size + (size & 1);
	}
	char full[512];
	for (int changed = 1; changed;) {
		changed = 0;
		for (Sym *g = ctx->globals; g; g = g->gnext) g->ref = 0;
		for (int i = 0; i < ctx->nrelocs; i++) if (ctx->relocs[i].sym) ctx->relocs[i].sym->ref = 1;
		for (int i = 0; i < nm; i++) {
			if (ms[i].loaded || !member_needed(ctx, &ms[i])) continue;
			ms[i].loaded = 1;
			snprintf(full, sizeof(full), "%s(%s)", name, ms[i].name);
			ctx->arch = name;
			ctx->member = intern(ms[i].name);
			load_object(ctx, ms[i].data, ms[i].size, intern(full));
			ctx->arch = ctx->member = NULL;
			changed = 1;
		}
	}
	free(ms);
}

static void alloc_commons(Ctx *ctx) {
	static const char *file = "";
	long start = ctx->bss_size;
	piece_begin(ctx);
	for (Sym *g = ctx->globals; g; g = g->gnext) {
		if (g->defined || !g->common) continue;
		int a = g->common_align > 0 ? g->common_align : 8;
		raise_align(ctx, SEC_BSS, a);
		ctx->bss_size = (long)align_up((size_t)ctx->bss_size, (size_t)a);
		g->sect = SEC_BSS;
		g->off = ctx->bss_size;
		g->size = g->common;
		g->defined = 1;
		ctx->bss_size += g->common;
	}
	if (ctx->bss_size > start) add_piece(ctx, SEC_BSS, start, ctx->bss_size, intern("COMMON"), file, ctx->bss_align);
	piece_end(ctx, file);
}

static uint8_t *read_binary(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = (uint8_t *)malloc((size_t)len + 1);
	if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { perror(path); exit(1); }
	fclose(f);
	*n = (size_t)len;
	return d;
}

static int input_kind(const char *path) {
	size_t n = strlen(path);
	if (n > 2 && path[n-2] == '.') {
		if (path[n-1] == 's') return 's';
		if (path[n-1] == 'S') return 'S';
		if (path[n-1] == 'o') return 'o';
		if (path[n-1] == 'a') return 'a';
	}
	return 'c';
}

static void compile_input(Ctx *ctx, const char *path) {
	int k = input_kind(path);
	if (k == 'o' || k == 'a') {
		size_t n;
		uint8_t *d = read_binary(path, &n);
		if (k == 'a') {
			if (n < 8 || memcmp(d, "!<arch>\n", 8)) link_error(path, "not an archive");
			load_archive(ctx, d, n, path);
		} else load_object(ctx, d, n, path);
		return;
	}
	char *src = read_file(path);
	piece_begin(ctx);
	if (k == 's' || k == 'S') assemble_unit(ctx, src, path, k == 'S');
	else compile_unit(ctx, src, path, 0);
	piece_end(ctx, path);
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
	ctx->cur_text = SEC_TEXT;
	ctx->cur_data = SEC_DATA;
}

int main(int argc, char **argv) {
	const char *out = "a.out";
	const char *inputs[64];
	const char *entry = NULL, *script = NULL;
	int ninputs = 0, compile_only = 0, out_given = 0, oformat_given = 0;
	Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
	ctx_init(ctx);
	for (int i=1;i<argc;i++) {
		const char *a = argv[i];
		if (strcmp(a,"-o")==0&&i+1<argc) { out=argv[++i]; out_given = 1; }
		else if (strcmp(a,"-c")==0) compile_only = 1;
		else if (a[0]=='-' && a[1]=='I' && ctx->nincdirs < 64) ctx->incdirs[ctx->nincdirs++] = a[2] ? a+2 : (i+1<argc ? argv[++i] : ".");
		else if (a[0]=='-' && a[1]=='D' && ctx->ndefines < 64) ctx->defines[ctx->ndefines++] = a[2] ? a+2 : (i+1<argc ? argv[++i] : "");
		else if (strcmp(a,"-nostdlib")==0) ctx->nostdlib = 1;
		else if (strcmp(a,"-ffreestanding")==0) ctx->freestanding = ctx->nostdlib = 1;
		else if (strcmp(a,"-mgeneral-regs-only")==0) ctx->no_float = 1;
		else if (strcmp(a,"-e")==0&&i+1<argc) entry=argv[++i];
		else if (strcmp(a,"-T")==0&&i+1<argc) script=argv[++i];
		else if (strcmp(a,"-Ttext")==0&&i+1<argc) {
			char *end;
			a = argv[++i];
			ctx->ttext = strtoull(a, &end, 0);
			ctx->has_ttext = 1;
			if (!*a || *end) { fprintf(stderr,"bad address: %s\n",a); return 1; }
		}
		else if (strcmp(a,"-oformat")==0&&i+1<argc) {
			a = argv[++i];
			oformat_given = 1;
			ctx->binary = strcmp(a,"elf") != 0;
			ctx->image = strcmp(a,"img") == 0;
			ctx->multiboot = strcmp(a,"multiboot") == 0;
			if (ctx->binary && !ctx->image && !ctx->multiboot && strcmp(a,"bin") != 0) { fprintf(stderr,"unknown output format: %s\n",a); return 1; }
		}
		else if (a[0]=='-') { fprintf(stderr,"unknown arg: %s\n",a); return 1; }
		else if (ninputs < 64) inputs[ninputs++] = a;
	}
	if (script) {
		if (compile_only) { fprintf(stderr,"-T needs a link, not -c\n"); return 1; }
		if (ctx->has_ttext) { fprintf(stderr,"-Ttext cannot be used with -T\n"); return 1; }
		ctx->ld = ld_read(script);
		ctx->track = 1;
		if (!oformat_given) ctx->binary = ctx->ld->format == 2;
		ctx->ld->entry_cmd = entry;
	}
	if (ctx->image && !ctx->has_ttext && !script) { ctx->ttext = 0x100000; ctx->has_ttext = 1; }
	if (ctx->multiboot && !ctx->has_ttext && !script) { ctx->ttext = 0x101000; ctx->has_ttext = 1; }
	if (ctx->multiboot && (ctx->ttext & 0xFFF)) { fprintf(stderr,"-Ttext must be page aligned for multiboot\n"); return 1; }
	if (ctx->no_float && !ctx->nostdlib) { fprintf(stderr,"-mgeneral-regs-only needs -nostdlib or -ffreestanding\n"); return 1; }
	if (ninputs == 0) { fprintf(stderr,"usage: %s <file.c|.s|.S|.o|.a>... [-c] [-o <out>] [-I dir] [-D name[=val]] [-nostdlib]\n"
			"       [-ffreestanding] [-e sym] [-Ttext addr] [-T script] [-oformat elf|bin|img|multiboot]\n"
			"       [-mgeneral-regs-only]\n",argv[0]); return 1; }

	keywords_init();
	types_init();
	id_main = intern("main");
	id_syscall = intern("__syscall");
	id_va_start = intern("__builtin_va_start");
	id_va_arg = intern("__builtin_va_arg");
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
	id_expect = intern("__builtin_expect");
	id_constant_p = intern("__builtin_constant_p");
	id_types_compatible_p = intern("__builtin_types_compatible_p");
	id_choose_expr = intern("__builtin_choose_expr");
	id_offsetof = intern("__builtin_offsetof");
	id_unreachable = intern("__builtin_unreachable");
	id_trap = intern("__builtin_trap");
	id_attribute = intern("__attribute__");
	id_attribute2 = intern("__attribute");
	id_interrupt = intern("interrupt");
	id_packed = intern("packed");
	id_aligned = intern("aligned");
	id_weak = intern("weak");
	id_alias = intern("alias");
	id_section = intern("section");
	id_asm = intern("asm");
	id_asm2 = intern("__asm__");
	id_asm3 = intern("__asm");
	id_naked = intern("naked");
	for (int i = 0; mach_builtins[i].name; i++) mach_ids[i] = intern(mach_builtins[i].name);

	if (compile_only) {
		if (out_given && ninputs > 1) { fprintf(stderr,"-o with -c needs a single input file\n"); return 1; }
		for (int i = 0; i < ninputs; i++) {
			int k = input_kind(inputs[i]);
			if (k == 'o' || k == 'a') { fprintf(stderr,"%s: error: -c needs a C or assembly file\n", inputs[i]); return 1; }
			Ctx *u = (Ctx *)malloc(sizeof(Ctx));
			ctx_init(u);
			memcpy(u->incdirs, ctx->incdirs, sizeof(ctx->incdirs));
			u->nincdirs = ctx->nincdirs;
			memcpy(u->defines, ctx->defines, sizeof(ctx->defines));
			u->ndefines = ctx->ndefines;
			u->nostdlib = ctx->nostdlib;
			u->freestanding = ctx->freestanding;
			u->no_float = ctx->no_float;
			u->track = 1;
			compile_input(u, inputs[i]);
			char obj[1024];
			if (out_given) snprintf(obj, sizeof(obj), "%s", out);
			else {
				const char *b = strrchr(inputs[i], '/');
				b = b ? b + 1 : inputs[i];
				snprintf(obj, sizeof(obj), "%s", b);
				char *dot = strrchr(obj, '.');
				if (dot) *dot = 0;
				if (strlen(obj) + 3 < sizeof(obj)) strcat(obj, ".o");
			}
			write_object(u, obj);
		}
		return 0;
	}

	Type *mt = func_type(ty_int);
	mt->oldstyle = 1;
	Sym *main_sym = ctx->freestanding ? NULL : get_global(ctx, id_main, SYM_FUNC, mt, 0);
	Sym *exit_sym = NULL, *init_sym = NULL, *environ_sym = NULL;
	if (!ctx->nostdlib) {
		Type *vt = func_type(ty_void);
		vt->oldstyle = 1;
		exit_sym = get_global(ctx, id_exit, SYM_FUNC, vt, 0);
		init_sym = get_global(ctx, id_libc_init, SYM_FUNC, vt, 0);
		environ_sym = get_global(ctx, id_environ, SYM_VAR, ptr_to(ptr_to(ty_char)), 0);
	}
	piece_begin(ctx);
	if (main_sym) emit_start(ctx, main_sym, exit_sym, init_sym, environ_sym);
	piece_end(ctx, "<start>");

	for (int i = 0; i < ninputs; i++) compile_input(ctx, inputs[i]);
	if (!ctx->nostdlib) {
		ctx->in_libc = 1;
		piece_begin(ctx);
		compile_unit(ctx, builtin_libc_src(), "<libc>", 1);
		piece_end(ctx, "<libc>");
		ctx->in_libc = 0;
	}
	alloc_commons(ctx);
	if (main_sym && !main_sym->defined) { fprintf(stderr, "error: undefined reference to 'main'\n"); return 1; }
	if (!entry && ctx->freestanding) entry = "_start";
	if (entry && !ctx->ld) {
		char *id = intern(entry);
		for (Sym *g = ctx->globals; g; g = g->gnext)
			if (g->name == id && !g->is_static && g->defined) { ctx->entry_sym = g; break; }
		if (!ctx->entry_sym) { fprintf(stderr, "error: undefined reference to '%s'\n", entry); return 1; }
	}

	write_elf(ctx, out);
	return 0;
}

#include "builtin.h"

static const char *find_builtin_header(const char *name) {
	for (int i = 0; builtin_names[i]; i++)
		if (strcmp(builtin_names[i], name) == 0) return builtin_srcs[i];
	return NULL;
}

static const char *find_builtin_boot(const char *name) {
	for (int i = 0; builtin_boot_names[i]; i++)
		if (strcmp(builtin_boot_names[i], name) == 0) return builtin_boot_srcs[i];
	return NULL;
}

static const char *builtin_libc_src(void) {
	Buf b; buf_init(&b);
	for (int i = 0; builtin_libc[i]; i++) buf_append(&b, builtin_libc[i], strlen(builtin_libc[i]));
	buf_append_byte(&b, 0);
	return (const char *)b.data;
}
