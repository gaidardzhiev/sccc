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

static char *read_stream(FILE *f) {
	Buf b; buf_init(&b);
	char tmp[65536];
	size_t n;
	while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf_append(&b, tmp, n);
	buf_append_byte(&b, 0);
	return (char *)b.data;
}

static char *read_file(const char *path) {
	if (!strcmp(path, "-")) return read_stream(stdin);
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	struct stat st;
	if (stat(path, &st) == 0 && !S_ISREG(st.st_mode)) { char *s = read_stream(f); fclose(f); return s; }
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
	T_BSLASH = '\\',
	T_INT = 256, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE, T_TYPEOF, T_ALIGNOF, T_ALIGNAS, T_NORETURN, T_GENERIC, T_THREAD, T_COMPLEX,
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
	const char *raw;
	int rawlen;
	int badnum;
	int wide;
	int imag;
} Tok;

typedef struct {
	const char *s;
	size_t i, n;
	int line;
	const char *file;
	int bol, space;
	int as;
	size_t tstart;
} Lex;

static const char *kw_names[] = {
	"int", "return", "if", "else", "while", "break", "continue",
	"char", "short", "long", "signed", "unsigned", "void", "_Bool",
	"struct", "union", "enum", "typedef", "static", "extern",
	"const", "volatile", "inline", "register", "auto", "restrict",
	"sizeof", "for", "do", "switch", "case", "default", "goto",
	"float", "double", "typeof", "_Alignof", "_Alignas", "_Noreturn", "_Generic", "_Thread_local",
	"__inline", "__restrict", "__const", "__signed__", "__inline__", "__restrict__",
	"__typeof__", "__typeof", "__alignof__", "__alignof", "__signed", "__volatile", "__thread",
	"_Complex", "__complex__",
	NULL
};
static const int kw_kinds[] = {
	T_INT, T_RETURN, T_IF, T_ELSE, T_WHILE, T_BREAK, T_CONTINUE,
	T_CHAR, T_SHORT, T_LONG, T_SIGNED, T_UNSIGNED, T_VOID, T_BOOL,
	T_STRUCT, T_UNION, T_ENUM, T_TYPEDEF, T_STATIC, T_EXTERN,
	T_CONST, T_VOLATILE, T_INLINE, T_REGISTER, T_AUTO, T_RESTRICT,
	T_SIZEOF, T_FOR, T_DO, T_SWITCH, T_CASE, T_DEFAULT, T_GOTO,
	T_FLOAT, T_DOUBLE, T_TYPEOF, T_ALIGNOF, T_ALIGNAS, T_NORETURN, T_GENERIC, T_THREAD,
	T_INLINE, T_RESTRICT, T_CONST, T_SIGNED, T_INLINE, T_RESTRICT,
	T_TYPEOF, T_TYPEOF, T_ALIGNOF, T_ALIGNOF, T_SIGNED, T_VOLATILE, T_THREAD,
	T_COMPLEX, T_COMPLEX,
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
		if (c=='#' && (L->as == 2 || ((L->as == 1 || L->as == 3) && !L->bol))) {
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

static int lex_ucn(Lex *L) {
	int n = L->s[L->i++] == 'u' ? 4 : 8, v = 0;
	for (int k = 0; k < n && L->i < L->n && isxdigit((unsigned char)L->s[L->i]); k++) v = v * 16 + hexval(L->s[L->i++]);
	return v;
}

static int lex_utf8(Lex *L) {
	int c = (unsigned char)L->s[L->i++], n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	int v = n ? c & (0x3F >> n) : c;
	for (int k = 0; k < n && L->i < L->n && ((unsigned char)L->s[L->i] & 0xC0) == 0x80; k++) v = v << 6 | (L->s[L->i++] & 0x3F);
	return v;
}

static void put_utf8(Buf *b, int v) {
	if (v < 0x80) { buf_append_byte(b, (uint8_t)v); return; }
	if (v < 0x800) { buf_append_byte(b, (uint8_t)(0xC0 | v >> 6)); }
	else {
		if (v < 0x10000) buf_append_byte(b, (uint8_t)(0xE0 | v >> 12));
		else { buf_append_byte(b, (uint8_t)(0xF0 | v >> 18)); buf_append_byte(b, (uint8_t)(0x80 | (v >> 12 & 0x3F))); }
		buf_append_byte(b, (uint8_t)(0x80 | (v >> 6 & 0x3F)));
	}
	buf_append_byte(b, (uint8_t)(0x80 | (v & 0x3F)));
}

static int wide_size(int w) { return w == 'L' || w == 'U' ? 4 : w == 'u' ? 2 : 1; }

static void put_wide(Buf *b, int w, int v) {
	int n = wide_size(w);
	if (n == 1) { buf_append_byte(b, (uint8_t)v); return; }
	if (n == 2 && v >= 0x10000) { put_wide(b, w, 0xD800 + ((v - 0x10000) >> 10)); v = 0xDC00 + ((v - 0x10000) & 0x3FF); }
	for (int i = 0; i < n; i++) buf_append_byte(b, (uint8_t)(v >> (8 * i)));
}

static int lex_wide(Lex *L, Tok *t, int w) {
	char q = L->s[L->i];
	L->i++;
	if (q == '\'') {
		int v = 0;
		while (L->i < L->n && L->s[L->i] != '\'') {
			if (L->s[L->i] == '\\' && L->i + 1 < L->n && (L->s[L->i+1] == 'u' || L->s[L->i+1] == 'U')) { L->i++; v = lex_ucn(L); }
			else if (L->s[L->i] == '\\') { L->i++; v = lex_escape(L); }
			else v = lex_utf8(L);
		}
		if (L->i >= L->n) lex_error(L, "unterminated character literal");
		L->i++;
		t->kind = T_NUM;
		t->val = w == 'u' ? (v & 0xFFFF) : w == '8' ? (signed char)v : v;
		t->is_unsigned = w == 'U';
		return 1;
	}
	Buf b; buf_init(&b);
	while (L->i < L->n && L->s[L->i] != '"') {
		if (L->s[L->i] == '\n') lex_error(L, "unterminated string literal");
		int v, raw = 0;
		if (L->s[L->i] == '\\' && L->i + 1 < L->n && (L->s[L->i+1] == 'u' || L->s[L->i+1] == 'U')) { L->i++; v = lex_ucn(L); }
		else if (L->s[L->i] == '\\') { L->i++; v = lex_escape(L); raw = 1; }
		else v = wide_size(w) == 1 ? (unsigned char)L->s[L->i++] : lex_utf8(L);
		if (wide_size(w) == 1 && !raw && v >= 0x80 && L->s[L->i - 1] != (char)v) put_utf8(&b, v);
		else put_wide(&b, w, v);
	}
	if (L->i >= L->n) lex_error(L, "unterminated string literal");
	L->i++;
	for (int i = 0; i < wide_size(w); i++) buf_append_byte(&b, 0);
	t->kind = T_STR; t->str = (char *)b.data; t->len = (int)b.len - wide_size(w);
	t->wide = w == '8' ? 0 : w;
	return 1;
}

static int lex_dots(Lex *L, size_t i) {
	return i + 2 < L->n && L->s[i] == '.' && L->s[i+1] == '.' && L->s[i+2] == '.';
}

static Tok lex_next1(Lex *L);

static Tok lex_next(Lex *L) {
	Tok t = lex_next1(L);
	if (t.kind != T_EOF) { t.raw = L->s + L->tstart; t.rawlen = (int)(L->i - L->tstart); }
	return t;
}

static Tok lex_next1(Lex *L) {
	L->space = 0;
	skip_ws(L);
	L->tstart = L->i;
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

	if (!L->as && (c == 'L' || c == 'u' || c == 'U') && L->i + 1 < L->n) {
		char c1 = L->s[L->i + 1];
		if (c1 == '\'' || c1 == '"') { L->i++; lex_wide(L, &t, c); return t; }
		if (c == 'u' && c1 == '8' && L->i + 2 < L->n && L->s[L->i + 2] == '"') { L->i += 2; lex_wide(L, &t, '8'); return t; }
	}

	if (isalpha((unsigned char)c) || c=='_' || (c=='$' && !L->as) ||
		(c=='.' && L->as && !(L->i+1<L->n && isdigit((unsigned char)L->s[L->i+1])) && !lex_dots(L, L->i))) {
		size_t j=L->i;
		while (j<L->n && (isalnum((unsigned char)L->s[j]) || L->s[j]=='_' || (L->s[j]=='$' && !L->as) || (L->s[j]=='.' && L->as && !lex_dots(L, j)))) j++;
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
			if (*end == 'i' || *end == 'I' || *end == 'j' || *end == 'J') { t.imag = 1; end++; }
			if (*end == 'f' || *end == 'F') { t.is_float = 1; end++; }
			else if (*end == 'l' || *end == 'L') { t.is_long = 1; end++; }
			if (!t.imag && (*end == 'i' || *end == 'I' || *end == 'j' || *end == 'J')) { t.imag = 1; end++; }
			if (*end) t.badnum = 1;
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
		if (L->i != j) { L->i = j; t.kind = T_NUM; t.badnum = 1; return t; }
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

	if (strchr("(){}[];,.+-*/%=<>!~&|^?:#\\", c) || (L->as && (c=='$' || c=='@'))) {
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
	TY_VOID, TY_BOOL, TY_CHAR, TY_SHORT, TY_INT, TY_LONG, TY_FLOAT, TY_DOUBLE, TY_LDOUBLE, TY_COMPLEX,
	TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT
} TyKind;

typedef struct Type Type;
typedef struct Member Member;
typedef struct Param Param;

struct Member {
	char *name;
	Type *ty;
	int off, flat;
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
	int vla;
	Type *cplx;
};

static Type *ty_void, *ty_bool, *ty_char, *ty_uchar, *ty_short, *ty_ushort;
static Type *ty_int, *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double, *ty_ldouble;
static Type *ty_schar, *ty_llong, *ty_ullong, *ty_va_list;

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
	ty_ldouble = new_type(TY_LDOUBLE, 16, 16);
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
static int is_flonum(Type *t) { return t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE; }
static int is_arith(Type *t) { return is_integer(t) || is_flonum(t); }
static int is_complex(Type *t) { return t->kind == TY_COMPLEX; }

static Type *complex_of(Type *b) {
	if (b->origin) b = b->origin;
	if (b->cplx) return b->cplx;
	Type *t = new_type(TY_COMPLEX, 2 * b->size, b->align);
	t->base = b;
	t->complete = 1;
	b->cplx = t;
	return t;
}
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
	if (a->kind == TY_COMPLEX || b->kind == TY_COMPLEX) return complex_of(arith_conv(a->kind == TY_COMPLEX ? a->base : a, b->kind == TY_COMPLEX ? b->base : b));
	if (a->kind == TY_LDOUBLE || b->kind == TY_LDOUBLE) return ty_ldouble;
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
	int weak, weak_unit, weak_def, strong_ref, strong_unit, strong_multi, twice;
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
	long got_off, gottp_off;
	int ref;
	int tentative;
	int ld_out;
	int piece;
	int tls;
	Tok *body;
	int nbody, always;
	Sym *gsame;
	int asm_alias, indirect, inline_only, gregvar, small, kv_valid, addr_taken, noinline, is_inline, used_attr, live, bound;
	long long kv;
	Sym *gnext;
};

typedef struct {
	char **k;
	Sym **v;
	int cap, n;
} SymMap;

static unsigned symmap_hash(char *k) {
	uint64_t x = (uint64_t)(uintptr_t)k;
	x ^= x >> 29;
	x *= 0x9E3779B97F4A7C15ULL;
	return (unsigned)(x >> 32);
}

static Sym *symmap_get(SymMap *m, char *k) {
	if (!m->cap) return NULL;
	for (unsigned i = symmap_hash(k) & (unsigned)(m->cap - 1);; i = (i + 1) & (unsigned)(m->cap - 1)) {
		if (!m->k[i]) return NULL;
		if (m->k[i] == k) return m->v[i];
	}
}

static void symmap_put(SymMap *m, char *k, Sym *v) {
	if ((m->n + 1) * 2 > m->cap) {
		SymMap o = *m;
		m->cap = o.cap ? o.cap * 2 : 1024;
		m->k = (char **)calloc((size_t)m->cap, sizeof(char *));
		m->v = (Sym **)calloc((size_t)m->cap, sizeof(Sym *));
		m->n = 0;
		if (!m->k || !m->v) { perror("calloc"); exit(1); }
		for (int i = 0; i < o.cap; i++) if (o.k[i]) symmap_put(m, o.k[i], o.v[i]);
		free(o.k);
		free(o.v);
	}
	unsigned i = symmap_hash(k) & (unsigned)(m->cap - 1);
	while (m->k[i] && m->k[i] != k) i = (i + 1) & (unsigned)(m->cap - 1);
	if (!m->k[i]) { m->k[i] = k; m->n++; }
	m->v[i] = v;
}

static void symmap_clear(SymMap *m) {
	if (m->cap) { memset(m->k, 0, sizeof(char *) * (size_t)m->cap); m->n = 0; }
}

typedef struct Tag Tag;
struct Tag {
	char *name;
	Type *ty;
	int depth;
	Tag *next;
};

typedef struct {
	size_t pos;
	int nrel, sp;
} Mark;

typedef struct LoopCtx LoopCtx;
struct LoopCtx {
	int is_switch, kconst, matched, kdead, tentative, depth;
	long long kval;
	Mark tmark;
	long cont_target;
	Patches breaks;
	Patches conts;
	Type *sw_type;
	long long *case_vals;
	long long *case_hi;
	size_t *case_pos;
	int ncase, capcase;
	long default_pos;
	size_t body;
	int vla_mark;
	LoopCtx *parent;
};

typedef struct LLab LLab;
struct LLab {
	char *name, *real;
	int depth;
	LLab *next;
};

typedef struct AsMacro AsMacro;
struct AsMacro {
	char *name;
	int n;
	char **pname, **pdef;
	int *req, *vararg;
	char *body;
	AsMacro *next;
};

typedef struct Inl Inl;
struct Inl {
	Sym *fn;
	Type *ret;
	Patches rets;
	int id, tmp, dead_base;
	Inl *up;
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

static void emit_scale(Code *c, int reg, int n);

static void scale_by(Code *c, int reg, Type *t) {
	if (!t->vla) { emit_scale(c, reg, t->size); return; }
	enc_rex(c, 1, reg, 0, RBP, 0); C8(c,0x0F); C8(c,0xAF); enc_mem(c, reg, RBP, -1, 1, t->vla, 1, 0);
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

typedef enum { R_PC32, R_ABS64, R_ABS32, R_ABS32S, R_ABS16, R_ABS8, R_PC16, R_PC8, R_PC64, R_PLT32, R_TPOFF32, R_TPOFF64 } RelKind;

static const int rel_size[] = { 4, 8, 4, 4, 2, 1, 2, 1, 8, 4, 4, 8 };
static const int rel_elf[] = { 2, 1, 10, 11, 12, 14, 13, 15, 24, 4, 23, 18 };

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
	int tls;
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
	Patches xrel;
	int seen;
	Label *next;
};

typedef struct {
	Sym *sym;
	Type *ty;
	int frame, frame_max;
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
	int nostart, nostdinc, no_common, omagic, gnu, fold;
	int sizeof_vla, nvla, vla_slots[64], vla_block, dead_jdepth, next_jdepth, pack, npack, packs[16];
	size_t k_start, k_end;
	long long k_val;
	int k_nrel, dead, cond_depth, revived;
	Inl *inl;
	LLab *llabs;
	AsMacro *as_macros;
	int as_mcount;
	SymMap fsmap, gmap;
	Sym *gidx_head;
	int inl_count, inl_depth, decl_inline, nllab, decl_register, name_attrs, xcond, noasm, dead_x, sx_depth, dead_sx, fn_depth, sidefx, jumped, swbody;
	Sym *lv_local;
	size_t lv_start;
	struct Attr *name_attr;
	size_t dk_start, dk_end;
	long long dk_val;
	int dk_nrel, k_ice, dk_ice;
	size_t revive_jmp[64];
	int revive_at[64], nrevive;
	Mark dead_mark;
	const char *includes[64];
	int nincludes;
	const char *undefs[64];
	int nundefs;
	const char **deps;
	int ndeps, capdeps, track_deps;
	Buf *asm_text;
	int binary;
	int image;
	int multiboot;
	int no_float;
	int decl_attrs, decl_thread;
	Attr decl_attr;
	Sym *tls_sym;
	size_t tls_end;
	Mark tls_mark;
	long long tls_tp, tls_lo;
	int has_tls;
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
	int in_pp_if, counter, ofs_rt, ofs_have, ginit, ncl, emit_relocs, m32, elf32, shared, gnu89, kmodel, no_sse;
	const char *soname, *map;
	struct DynSym { Sym *s; int ver; uint32_t gh; uint32_t name; } *dyn;
	int ndyn, dsec[7];
	uint32_t dyn_nbucket, dyn_gnb, dyn_soname, *dyn_vname;
	Buf dynstr;
	uint64_t max_page;
	struct PendCL { Sym *g; TokVec v; } *cl;
	int capcl;
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

static char *id_va_arg, *id_va_end, *id_va_copy, *id_va_list;
static char *id_main, *id_syscall, *id_va_start, *id_exit, *id_libc_init, *id_environ;
enum { ATTR_INTERRUPT = 1, ATTR_PACKED = 2, ATTR_WEAK = 4, ATTR_NAKED = 8, ATTR_ALWAYS_INLINE = 16, ATTR_USED = 32, ATTR_GNU_INLINE = 64, ATTR_NOINLINE = 128, ATTR_TWICE = 256 };
static char *id_attribute, *id_attribute2, *id_interrupt, *id_packed, *id_aligned, *id_weak, *id_alias, *id_section;
static char *id_defined, *id_file, *id_line, *id_counter, *id_va_args, *id_func, *id_function, *id_static_assert;
static char *id_asm, *id_asm2, *id_asm3, *id_naked;
static char *id_expect, *id_constant_p, *id_types_compatible_p, *id_choose_expr, *id_offsetof, *id_unreachable, *id_trap;
static char *id_return_address, *id_frame_address;

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
	if (t->kind >= T_INT && t->kind <= T_THREAD) return kw_names[t->kind - T_INT];
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
	if (ctx->track_deps && !is_builtin) {
		char *ip = intern(path);
		int seen = 0;
		for (int i = 0; i < ctx->ndeps && !seen; i++) seen = ctx->deps[i] == ip;
		if (!seen) {
			if (ctx->ndeps == ctx->capdeps) {
				ctx->capdeps = ctx->capdeps ? ctx->capdeps * 2 : 64;
				ctx->deps = (const char **)realloc(ctx->deps, sizeof(char *) * ctx->capdeps);
			}
			ctx->deps[ctx->ndeps++] = ip;
		}
	}
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
				if (t.kind == T_ELLIPSIS) {
					m->variadic = 1;
					t = lex_next(sub);
					break;
				}
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
	if (sub.as) sub.as = 4;
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
			int def = a.id->macro != NULL || a.str == id_file || a.str == id_line || a.str == id_counter;
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

static Tok pp_next(Ctx *ctx);
static void tok_spell(Buf *b, Tok *t);

static char *pp_expand_line(Ctx *ctx, Lex *sub) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	for (;;) {
		Tok t = lex_next(sub);
		if (t.kind == T_EOF) break;
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
	Buf b; buf_init(&b);
	for (;;) {
		Tok t = pp_next(ctx);
		if (is_sentinel(&t) || t.kind == T_EOF) break;
		if (t.space && b.len) buf_append_byte(&b, ' ');
		tok_spell(&b, &t);
	}
	buf_append_byte(&b, 0);
	if (ctx->src == frame) ctx->src = frame->parent;
	ctx->tok = saved_tok; ctx->pk = saved_pk; ctx->pp_back = saved_back;
	ctx->has_pk = saved_has_pk; ctx->has_pp_back = saved_has_back;
	return (char *)b.data;
}

static void pp_include(Ctx *ctx, Src *s, Lex *sub) {
	const char *p = sub->s + sub->i;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '"' && *p != '<') p = pp_expand_line(ctx, sub);
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
	const char *b = ctx->nostdinc && !ctx->in_libc ? NULL : find_builtin_header(name);
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
	if (sub.as) sub.as = 4;
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
		if (t.id && !strcmp(t.str, "pack") && !L->as) {
			Tok u = lex_next(&sub);
			if (u.kind != T_LP) return;
			u = lex_next(&sub);
			if (u.id && !strcmp(u.str, "push")) {
				if (ctx->npack < 16) ctx->packs[ctx->npack++] = ctx->pack;
				u = lex_next(&sub);
				if (u.kind == T_COMMA) { u = lex_next(&sub); if (u.kind == T_NUM) ctx->pack = (int)u.val; }
			} else if (u.id && !strcmp(u.str, "pop")) {
				ctx->pack = ctx->npack ? ctx->packs[--ctx->npack] : 0;
			} else if (u.kind == T_NUM) ctx->pack = (int)u.val;
			else if (u.kind == T_RP) ctx->pack = 0;
			return;
		}
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
		if (t.kind == T_HASHHASH && t.bol && s->L.as) { free(read_line(&s->L)); continue; }
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
	if (t->raw && t->kind != T_STR) { buf_append(b, t->raw, (size_t)t->rawlen); return; }
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
	if (res.n) res.v[0].space = t->space;
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
		if (t.str == id_line || t.str == id_counter) {
			t.kind = T_NUM; t.val = t.str == id_line ? t.line : ctx->counter++; t.id = NULL; t.str = NULL;
			t.is_unsigned = 0; t.is_long = 0; t.raw = NULL; t.rawlen = 0;
			return t;
		}
		if (t.str == id_file) {
			t.kind = T_STR; t.str = (char *)t.file; t.len = (int)strlen(t.file); t.id = NULL; t.raw = NULL; t.rawlen = 0;
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
		"__extension__", "__volatile__ volatile", "__SCCC__ 1",
		NULL
	};
	ctx->counter = 0;
	for (int i = 0; predef[i]; i++) pp_define_str(ctx, predef[i]);
	if (ctx->freestanding) pp_define_str(ctx, "__STDC_HOSTED__ 0");
	if (ctx->m32) {
		static const char *un[] = { "__x86_64__", "__x86_64", "__amd64__", "__LP64__", "_LP64", NULL };
		for (int i = 0; un[i]; i++) intern_str(un[i], (int)strlen(un[i]))->macro = NULL;
		pp_define_str(ctx, "__i386__ 1");
		pp_define_str(ctx, "__i386 1");
		pp_define_str(ctx, "__SIZEOF_LONG__ 4");
		pp_define_str(ctx, "__SIZEOF_POINTER__ 4");
		if (ctx->gnu) pp_define_str(ctx, "i386 1");
	}
	if (ctx->gnu) {
		pp_define_str(ctx, "__GNUC__ 4");
		pp_define_str(ctx, "__GNUC_MINOR__ 9");
		pp_define_str(ctx, "__GNUC_PATCHLEVEL__ 0");
	}
	for (int i = 0; i < ctx->ndefines; i++) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", ctx->defines[i]);
		char *eq = strchr(buf, '=');
		if (eq) *eq = ' ';
		else if (strlen(buf) < sizeof(buf) - 3) strcat(buf, " 1");
		pp_define_str(ctx, intern(buf));
	}
	for (int i = 0; i < ctx->nundefs; i++) {
		Str *id = intern_str(ctx->undefs[i], (int)strlen(ctx->undefs[i]));
		id->macro = NULL;
	}
}

static void push_includes(Ctx *ctx, int as) {
	for (int i = ctx->nincludes - 1; i >= 0; i--) {
		push_file(ctx, read_file(ctx->includes[i]), ctx->includes[i], 0);
		ctx->src->L.as = as;
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
	if (!strcmp(name, "always_inline")) return ATTR_ALWAYS_INLINE;
	if (!strcmp(name, "used")) return ATTR_USED;
	if (!strcmp(name, "gnu_inline")) return ATTR_GNU_INLINE;
	if (!strcmp(name, "noinline")) return ATTR_NOINLINE;
	if (!strcmp(name, "returns_twice")) return ATTR_TWICE;
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
		if (t.kind == T_IDENT && !strcmp(t.str, "_Pragma")) {
			Tok u = pp_next(ctx);
			if (u.kind != T_LP) error_at(ctx, "expected '(' after _Pragma");
			do u = pp_next(ctx); while (u.kind == T_STR);
			if (u.kind != T_RP) error_at(ctx, "_Pragma takes a string literal");
			continue;
		}
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


static void capture_body(Ctx *ctx, Sym *g) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0;
	do {
		Tok t = ctx->tok;
		if (t.kind == T_EOF) error_at(ctx, "unexpected EOF in function body");
		if (t.kind == T_LB) depth++;
		else if (t.kind == T_RB) depth--;
		t.id = NULL;
		tv_push(&v, t);
		next(ctx);
	} while (depth > 0);
	g->body = v.v;
	g->nbody = v.n;
	g->small = v.n <= (g->is_inline ? 512 : 64) && !g->noinline;
	for (int i = 0; i < v.n && g->small; i++) {
		Tok *t = &v.v[i];
		if (t->kind == T_STATIC) g->small = 0;
		else if (t->kind == T_IDENT && (!strcmp(t->str, "alloca") || !strcmp(t->str, "__builtin_alloca"))) g->small = 0;
		else if (t->kind == T_LBRACKET && i >= 2 && v.v[i-1].kind == T_IDENT) {
			int k = v.v[i-2].kind;
			if (!(k == T_IDENT || k == T_INT || k == T_CHAR || k == T_SHORT || k == T_LONG || k == T_UNSIGNED || k == T_SIGNED ||
				k == T_FLOAT || k == T_DOUBLE || k == T_BOOL)) continue;
			for (int j = i + 1, d = 0; j < v.n && (d || v.v[j].kind != T_RBRACKET); j++) {
				if (v.v[j].kind == T_LBRACKET) d++;
				else if (v.v[j].kind == T_RBRACKET) d--;
				else if (v.v[j].kind == T_IDENT && strcmp(v.v[j].str, "sizeof")) { g->small = 0; break; }
			}
		}
	}
}

static int toks_write(Tok *b, int n, char *name) {
	for (int i = 0; i < n; i++) {
		if (b[i].kind != T_IDENT || b[i].str != name) continue;
		int j = i + 1, q = i - 1;
		while (j < n && b[j].kind == T_RP) j++;
		while (q >= 0 && b[q].kind == T_LP) q--;
		int nj = j < n ? b[j].kind : T_EOF, pq = q >= 0 ? b[q].kind : T_EOF;
		if (nj == T_ASSIGN || nj == T_INC || nj == T_DEC || (nj >= T_ADD_ASSIGN && nj <= T_SHR_ASSIGN)) return 1;
		if (pq == T_AMP || pq == T_INC || pq == T_DEC) return 1;
		int nk = i + 1 < n ? b[i+1].kind : T_EOF;
		if (nk == T_ASSIGN || nk == T_INC || nk == T_DEC || (nk >= T_ADD_ASSIGN && nk <= T_SHR_ASSIGN)) return 1;
		int pk = i > 0 ? b[i-1].kind : T_EOF;
		if (pk == T_INC || pk == T_DEC) return 1;
		if (pk == T_AMP) {
			int ppk = i > 1 ? b[i-2].kind : T_EOF;
			if (ppk != T_IDENT && ppk != T_NUM && ppk != T_FNUM && ppk != T_RP && ppk != T_RBRACKET && ppk != T_STR) return 1;
		}
		if (pk == T_LP && i > 1 && b[i-2].kind == T_STR) {
			int j = i - 2;
			while (j > 0 && b[j-1].kind == T_STR) j--;
			if (b[j].len > 0 && (b[j].str[0] == '=' || b[j].str[0] == '+')) return 1;
		}
	}
	return 0;
}

static int param_written(Sym *g, char *name) { return toks_write(g->body, g->nbody, name); }

static int rest_written(Ctx *ctx, char *name) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_EOF) break;
		if (k == T_LB) depth++;
		else if (k == T_RB && !depth--) break;
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	int w = toks_write(v.v, v.n, name);
	replay(ctx, &v);
	free(v.v);
	return w;
}

static Sym *lookup(Ctx *ctx, char *name) {
	Sym *s = ctx->scope;
	for (; s && s->depth > 0; s = s->next)
		if (s->name == name) return s;
	return s ? symmap_get(&ctx->fsmap, name) : NULL;
}

static Sym *global_chain(Ctx *ctx, char *name) {
	Sym *h = ctx->globals;
	if (h != ctx->gidx_head) {
		int n = 0, cap = 64;
		Sym **v = (Sym **)malloc(sizeof(Sym *) * cap);
		Sym *p = h;
		for (; p && p != ctx->gidx_head; p = p->gnext) {
			if (n == cap) v = (Sym **)realloc(v, sizeof(Sym *) * (cap *= 2));
			v[n++] = p;
		}
		if (!p && ctx->gidx_head) {
			symmap_clear(&ctx->gmap);
			ctx->gidx_head = NULL;
			free(v);
			return global_chain(ctx, name);
		}
		for (int i = n - 1; i >= 0; i--) {
			v[i]->gsame = symmap_get(&ctx->gmap, v[i]->name);
			symmap_put(&ctx->gmap, v[i]->name, v[i]);
		}
		free(v);
		ctx->gidx_head = h;
	}
	return symmap_get(&ctx->gmap, name);
}

static Sym *push_sym(Ctx *ctx, char *name, SymKind kind, Type *ty) {
	Sym *s = (Sym *)calloc(1, sizeof(Sym));
	s->name = name; s->kind = kind; s->ty = ty;
	s->depth = ctx->depth;
	s->next = ctx->scope;
	ctx->scope = s;
	if (!s->depth) symmap_put(&ctx->fsmap, name, s);
	return s;
}

static void enter_scope(Ctx *ctx) { ctx->depth++; }

static void leave_scope(Ctx *ctx) {
	while (ctx->llabs && ctx->llabs->depth >= ctx->depth) ctx->llabs = ctx->llabs->next;
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
	return ctx->scope ? symmap_get(&ctx->fsmap, name) : NULL;
}

static Sym *get_global(Ctx *ctx, char *name, SymKind kind, Type *ty, int is_static) {
	Sym *s = lookup_file_scope(ctx, name);
	if (s && s->global) s = s->global;
	if (!s && !is_static) {
		for (Sym *g = global_chain(ctx, name); g; g = g->gsame)
			if (!g->is_static && (g->kind == SYM_VAR || g->kind == SYM_FUNC)) { s = g; break; }
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
		Sym **pp = &ctx->scope;
		while (*pp && (*pp)->depth > 0) pp = &(*pp)->next;
		g->next = *pp;
		*pp = g;
		symmap_put(&ctx->fsmap, g->name, g);
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
	r->in = kind == R_PC32 || kind == R_PLT32 || kind == R_TPOFF32 ? ctx->cur_text : ctx->cur_data;
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
	sec->align = kind == SK_CODE && strcmp(name, ".init") && strcmp(name, ".fini") ? 16 : 1;
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
	if (ctx->kmodel) {
		C8(c, (uint8_t)(0x48 | (reg >= 8 ? 1 : 0)));
		C8(c, 0xC7);
		C8(c, (uint8_t)(0xC0 | (reg & 7)));
		add_reloc(ctx, R_ABS32S, Cpos(c), sym, sect, off);
		ctx->relocs[ctx->nrelocs - 1].in = ctx->cur_text;
		C32(c, 0);
		return;
	}
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
			k = 0;
			for (int i = 0; i < l->xrel.n; i++) if ((int)l->xrel.v[i] < m.nrel) l->xrel.v[k++] = l->xrel.v[i];
			l->xrel.n = k;
		}
	}
	ctx->text.b.len = m.pos;
	ctx->nrelocs = m.nrel;
	ctx->sp = m.sp;
	ctx->lv = 0;
	ctx->fn_lea_sym = NULL;
	if (ctx->k_end > m.pos) ctx->k_end = (size_t)-1;
}

static void emit_const(Ctx *ctx, long long v) {
	ctx->k_start = Cpos(&ctx->text);
	emit_mov_rax_imm(&ctx->text, v);
	ctx->k_end = Cpos(&ctx->text);
	ctx->k_val = v;
	ctx->k_nrel = ctx->nrelocs;
	ctx->k_ice = 1;
}

static int expr_const(Ctx *ctx, long long *v) {
	if (!ctx->fold || ctx->lv || ctx->k_end != Cpos(&ctx->text) || ctx->k_nrel != ctx->nrelocs) return 0;
	*v = ctx->k_val;
	return 1;
}

static void drop_to(Ctx *ctx, size_t pos, int sp) {
	Mark m;
	m.pos = pos;
	m.nrel = ctx->nrelocs;
	while (m.nrel > 0 && ctx->relocs[m.nrel - 1].in == ctx->cur_text && ctx->relocs[m.nrel - 1].pos >= pos) m.nrel--;
	m.sp = sp;
	drop_code(ctx, m);
}

static void filter_patches(Patches *p, size_t pos) {
	int k = 0;
	for (int i = 0; i < p->n; i++) if (p->v[i] < pos) p->v[k++] = p->v[i];
	p->n = k;
}

static void cut_code(Ctx *ctx, Mark m) {
	for (LoopCtx *l = ctx->loops.top; l; l = l->parent) {
		filter_patches(&l->breaks, m.pos);
		filter_patches(&l->conts, m.pos);
		int k = 0;
		for (int i = 0; i < l->ncase; i++)
			if (l->case_pos[i] <= m.pos) { l->case_vals[k] = l->case_vals[i]; l->case_hi[k] = l->case_hi[i]; l->case_pos[k++] = l->case_pos[i]; }
		l->ncase = k;
		if (l->default_pos > (long)m.pos) l->default_pos = -1;
	}
	if (ctx->fn)
		for (Label *l = ctx->fn->labels; l; l = l->next)
			if (l->pos > (long)m.pos) {
				for (int i = 0; i < l->patches.n; i++)
					if (l->patches.v[i] < m.pos) error_at(ctx, "a jump to '%s' goes into code left out for a constant condition", l->name);
				for (int i = 0; i < l->xrel.n; i++)
					if ((int)l->xrel.v[i] < m.nrel) error_at(ctx, "a jump to '%s' goes into code left out for a constant condition", l->name);
				l->pos = -1;
			}
	filter_patches(&ctx->init_patches, m.pos);
	for (Inl *in = ctx->inl; in; in = in->up) filter_patches(&in->rets, m.pos);
	drop_code(ctx, m);
}

static void dead_begin(Ctx *ctx) {
	int jd = ctx->next_jdepth;
	ctx->next_jdepth = 0;
	if (ctx->dead++) return;
	ctx->dead_jdepth = jd;
	ctx->dead_sx = ctx->sx_depth;
	ctx->dead_mark = mark_code(ctx);
	ctx->dk_start = ctx->k_start;
	ctx->dk_end = ctx->k_end;
	ctx->dk_val = ctx->k_val;
	ctx->dk_nrel = ctx->k_nrel;
	ctx->dk_ice = ctx->k_ice;
}

static void dead_end(Ctx *ctx) {
	if (!ctx->dead) {
		ctx->revived--;
		while (ctx->nrevive && ctx->revive_at[ctx->nrevive - 1] == ctx->revived) {
			ctx->nrevive--;
			patch_jmp_to(&ctx->text.b, ctx->revive_jmp[ctx->nrevive], Cpos(&ctx->text));
		}
		return;
	}
	if (--ctx->dead) return;
	ctx->dead_jdepth = 0;
	cut_code(ctx, ctx->dead_mark);
	ctx->k_start = ctx->dk_start;
	ctx->k_end = ctx->dk_end;
	ctx->k_val = ctx->dk_val;
	ctx->k_nrel = ctx->dk_nrel;
	ctx->k_ice = ctx->dk_ice;
}

static void dead_xbegin(Ctx *ctx) {
	ctx->dead_x++;
	dead_begin(ctx);
}

static void dead_xend(Ctx *ctx) {
	dead_end(ctx);
	ctx->dead_x--;
}

static void revive(Ctx *ctx) {
	if (!ctx->dead || ctx->dead_x || ctx->dead_sx != ctx->sx_depth || (ctx->inl && ctx->inl->dead_base)) return;
	cut_code(ctx, ctx->dead_mark);
	if (ctx->nrevive == 64) error_at(ctx, "too many labels in code left out for constant conditions");
	ctx->revive_at[ctx->nrevive] = ctx->revived;
	ctx->revive_jmp[ctx->nrevive++] = emit_jmp_placeholder(&ctx->text);
	ctx->revived += ctx->dead;
	ctx->dead = 0;
}

static void case_revive(Ctx *ctx, LoopCtx *lc) {
	if (!ctx->dead || ctx->dead_mark.pos < lc->body) return;
	revive(ctx);
	if (ctx->dead) error_at(ctx, "a case label is inside code left out for a constant condition");
}

static Label *find_label(Ctx *ctx, char *name) {
	for (LLab *ll = ctx->llabs; ll; ll = ll->next)
		if (ll->name == name) { name = ll->real; break; }
	if (ctx->inl) {
		char b[256];
		snprintf(b, sizeof(b), "%s.inl%d", name, ctx->inl->id);
		name = intern(b);
	}
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
	if (f->frame > f->frame_max) f->frame_max = f->frame;
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
	if (ty->kind == TY_LDOUBLE || ty->kind == TY_COMPLEX) return;
	if (ty->bit_width) emit_bf_load(c, ty);
	else emit_load(c, ty->size, ty->is_unsigned);
}

static void emit_store_type(Code *c, Type *ty) {
	if (ty->kind == TY_COMPLEX) {
		emit_mov_reg_reg(c, RDX, RAX);
		emit_mov_reg_reg(c, RSI, RAX);
		emit_mov_reg_reg(c, RDI, RCX);
		emit_mov_ecx_imm(c, (uint32_t)ty->size);
		emit_rep_movsb(c);
		emit_mov_reg_reg(c, RAX, RDX);
		return;
	}
	if (ty->kind == TY_LDOUBLE) {
		C8(c,0x48); C8(c,0x8B); C8(c,0x10);
		C8(c,0x48); C8(c,0x89); C8(c,0x11);
		C8(c,0x0F); C8(c,0xB7); C8(c,0x50); C8(c,0x08);
		C8(c,0x66); C8(c,0x89); C8(c,0x51); C8(c,0x08);
		return;
	}
	if (ty->bit_width) emit_bf_store(c, ty);
	else emit_store(c, ty->size);
}

static void emit_tls_access(Ctx *ctx, Sym *g, int size, int is_unsigned, int store) {
	Code t, *c = &ctx->text;
	code_init(&t);
	if (store) emit_store(&t, size);
	else emit_load(&t, size, is_unsigned);
	C8(c,0x64);
	for (size_t i = 0; i + 1 < t.b.len; i++) C8(c,t.b.data[i]);
	C8(c,0x04); C8(c,0x25);
	add_reloc(ctx, R_TPOFF32, Cpos(c), g, SEC_TEXT, 0);
	C32(c,0);
	free(t.b.data);
}

static void emit_tls_addr(Ctx *ctx, Sym *g) {
	Code *c = &ctx->text;
	ctx->tls_mark = mark_code(ctx);
	C8(c,0x64); C8(c,0x48); C8(c,0x8B); C8(c,0x04); C8(c,0x25); C32(c,0);
	C8(c,0x48); C8(c,0x8D); C8(c,0x80);
	add_reloc(ctx, R_TPOFF32, Cpos(c), g, SEC_TEXT, 0);
	C32(c,0);
	ctx->tls_sym = g;
	ctx->tls_end = Cpos(c);
}

static Sym *tls_lvalue(Ctx *ctx, Type *ty) {
	if (!ctx->lv || !ctx->tls_sym || ctx->tls_end != Cpos(&ctx->text) || !is_scalar(ty) || ty->bit_width || ty->kind == TY_LDOUBLE) return NULL;
	Sym *g = ctx->tls_sym;
	drop_code(ctx, ctx->tls_mark);
	ctx->tls_sym = NULL;
	return g;
}

static Type *xcond_end(Ctx *ctx, Type *t) {
	ctx->xcond--;
	return t;
}

static Sym *simple_local(Ctx *ctx) {
	Sym *s = ctx->lv_local;
	if (!s || !ctx->lv || ctx->lv_end != Cpos(&ctx->text) || ctx->lv_start > ctx->lv_end) return NULL;
	Code t;
	code_init(&t);
	emit_lea_rbp(&t, RAX, s->stack_off);
	int ok = ctx->lv_end - ctx->lv_start == t.b.len && !memcmp(ctx->text.b.data + ctx->lv_start, t.b.data, t.b.len);
	free(t.b.data);
	return ok ? s : NULL;
}

static void kv_forget_all(Ctx *ctx) {
	for (Sym *s = ctx->scope; s && s->depth > 0; s = s->next) s->kv_valid = 0;
}

static int toks_write(Tok *b, int n, char *name);

static void kv_forget_scan(Ctx *ctx, int levels) {
	int any = 0;
	for (Sym *s = ctx->scope; s && s->depth > 0 && !any; s = s->next) any = s->kv_valid;
	if (!any) return;
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_EOF) break;
		if (k == T_LB) depth++;
		else if (k == T_RB) {
			if (depth) depth--;
			else if (--levels <= 0) break;
		}
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	for (Sym *s = ctx->scope; s && s->depth > 0; s = s->next)
		if (s->kv_valid && (toks_write(v.v, v.n, s->name) || s->addr_taken)) s->kv_valid = 0;
	replay(ctx, &v);
	free(v.v);
}

static void kv_forget_loop(Ctx *ctx) { kv_forget_scan(ctx, 1); }

static void kv_forget(Ctx *ctx, int addr) {
	Sym *s = simple_local(ctx);
	if (!s || ctx->dead) return;
	s->kv_valid = 0;
	if (addr) s->addr_taken = 1;
}

static Type *rval(Ctx *ctx, Type *ty) {
	if (ctx->fold && ctx->lv && (is_integer(ty) || is_ptr(ty)) && !ty->bit_width) {
		Sym *s = simple_local(ctx);
		if (s && s->kv_valid && !s->addr_taken) {
			drop_to(ctx, ctx->lv_start, ctx->sp);
			emit_const(ctx, s->kv);
			ctx->k_ice = 0;
			ctx->lv = 0;
			return ty->origin ? ty->origin : ty;
		}
	}
	if (ty->kind == TY_ARRAY) { ctx->lv = 0; return ptr_to(ty->base); }
	if (ty->kind == TY_FUNC) { ctx->lv = 0; return ptr_to(ty); }
	Sym *tg = tls_lvalue(ctx, ty);
	if (tg) {
		emit_tls_access(ctx, tg, ty->size, ty->is_unsigned, 0);
		ctx->lv = 0;
	}
	if (ctx->lv) {
		if (ty->kind != TY_STRUCT && ty->kind != TY_VOID) emit_load_type(&ctx->text, ty);
		ctx->lv = 0;
	}
	if (ty->origin) ty = ty->origin;
	return ty;
}

typedef struct { uint32_t *d; int n, cap; } Big;

static void big_fit(Big *b, int n) {
	if (n <= b->cap) return;
	b->d = (uint32_t *)realloc(b->d, sizeof(uint32_t) * (size_t)n);
	memset(b->d + b->cap, 0, sizeof(uint32_t) * (size_t)(n - b->cap));
	b->cap = n;
}

static void big_trim(Big *b) { while (b->n && !b->d[b->n - 1]) b->n--; }

static void big_muladd(Big *b, uint32_t m, uint32_t a) {
	uint64_t carry = a;
	for (int i = 0; i < b->n; i++) {
		uint64_t v = (uint64_t)b->d[i] * m + carry;
		b->d[i] = (uint32_t)v;
		carry = v >> 32;
	}
	if (carry) { big_fit(b, b->n + 1); b->d[b->n++] = (uint32_t)carry; }
}

static int big_bits(Big *b) {
	big_trim(b);
	if (!b->n) return 0;
	int k = 32;
	while (!(b->d[b->n - 1] >> (k - 1))) k--;
	return 32 * (b->n - 1) + k;
}

static int big_bit(Big *b, int i) { return i >= 0 && i / 32 < b->n ? (int)(b->d[i / 32] >> (i % 32) & 1) : 0; }

static void big_shl(Big *b, int k) {
	if (k <= 0 || !b->n) return;
	int w = k / 32, r = k % 32;
	big_fit(b, b->n + w + 1);
	for (int i = b->n + w; i >= 0; i--) {
		uint64_t hi = i - w >= 0 && i - w < b->n ? b->d[i - w] : 0, lo = i - w - 1 >= 0 && i - w - 1 < b->n ? b->d[i - w - 1] : 0;
		b->d[i] = r ? (uint32_t)(hi << r | lo >> (32 - r)) : (uint32_t)hi;
	}
	b->n += w + 1;
	big_trim(b);
}

static int big_cmp(Big *a, Big *b) {
	big_trim(a); big_trim(b);
	if (a->n != b->n) return a->n < b->n ? -1 : 1;
	for (int i = a->n - 1; i >= 0; i--) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
	return 0;
}

static void big_sub(Big *a, Big *b) {
	int64_t br = 0;
	for (int i = 0; i < a->n; i++) {
		int64_t v = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - br;
		br = v < 0;
		a->d[i] = (uint32_t)(v + (br ? ((int64_t)1 << 32) : 0));
	}
	big_trim(a);
}

static void big_div(Big *num, Big *den, Big *q) {
	int nb = big_bits(num);
	q->n = 0;
	big_fit(q, nb / 32 + 2);
	memset(q->d, 0, sizeof(uint32_t) * (size_t)q->cap);
	q->n = nb / 32 + 1;
	Big r = { NULL, 0, 0 };
	for (int i = nb - 1; i >= 0; i--) {
		big_shl(&r, 1);
		if (big_bit(num, i)) { if (!r.n) { big_fit(&r, 1); r.n = 1; r.d[0] = 0; } r.d[0] |= 1; }
		if (big_cmp(&r, den) >= 0) { big_sub(&r, den); q->d[i / 32] |= 1u << (i % 32); }
	}
	big_trim(q);
	free(num->d);
	*num = r;
}

static void ld_round(Big *m, int sticky, uint8_t out[10], int exp2) {
	int nb = big_bits(m);
	int e = exp2 + nb - 1;
	int be = e + 16383, prec = be > 0 ? 64 : 64 - (1 - be);
	uint64_t mant = 0;
	if (prec > 0) for (int i = 0; i < prec; i++) mant |= (uint64_t)big_bit(m, nb - 1 - i) << (prec - 1 - i);
	int rb = prec >= 0 ? big_bit(m, nb - 1 - prec) : 0;
	if (prec < 0) sticky = 1;
	for (int i = 0; i < nb - 1 - prec && !sticky; i++) sticky = big_bit(m, i);
	if (rb && (sticky || (mant & 1))) {
		mant++;
		if (prec == 64 && !mant) { mant = 1ULL << 63; be++; }
		else if (prec < 64 && (mant >> 63)) be = 1;
	}
	if (be <= 0) be = 0;
	if (be >= 0x7FFF) { mant = 1ULL << 63; be = 0x7FFF; }
	for (int i = 0; i < 8; i++) out[i] = (uint8_t)(mant >> (8 * i));
	out[8] = (uint8_t)be;
	out[9] = (uint8_t)(be >> 8);
}

static void ld_from_text(const char *p, uint8_t out[10]) {
	memset(out, 0, 10);
	Big m = { NULL, 0, 0 };
	int neg = 0, exp = 0, any = 0;
	if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		int frac = 0, dot = 0;
		for (;; p++) {
			if (*p == '.') { dot = 1; continue; }
			if (!isxdigit((unsigned char)*p)) break;
			if (!m.n) { big_fit(&m, 1); m.n = 1; m.d[0] = 0; }
			big_muladd(&m, 16, (uint32_t)hexval(*p));
			any = 1;
			if (dot) frac += 4;
		}
		if (*p == 'p' || *p == 'P') {
			p++;
			int es = 1, ev = 0;
			if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
			while (isdigit((unsigned char)*p)) ev = ev * 10 + (*p++ - '0');
			exp = es * ev;
		}
		if (any && big_bits(&m)) ld_round(&m, 0, out, exp - frac);
	} else {
		int frac = 0, dot = 0;
		for (;; p++) {
			if (*p == '.') { dot = 1; continue; }
			if (!isdigit((unsigned char)*p)) break;
			if (!m.n) { big_fit(&m, 1); m.n = 1; m.d[0] = 0; }
			big_muladd(&m, 10, (uint32_t)(*p - '0'));
			if (dot) frac++;
		}
		if (*p == 'e' || *p == 'E') {
			p++;
			int es = 1, ev = 0;
			if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
			while (isdigit((unsigned char)*p)) ev = ev * 10 + (*p++ - '0');
			exp = es * ev;
		}
		exp -= frac;
		if (big_bits(&m)) {
			if (exp >= 0) {
				for (int i = 0; i < exp; i++) big_muladd(&m, 10, 0);
				ld_round(&m, 0, out, 0);
			} else {
				Big den = { NULL, 0, 0 };
				big_fit(&den, 1); den.n = 1; den.d[0] = 1;
				for (int i = 0; i < -exp; i++) big_muladd(&den, 10, 0);
				int k = 70 + big_bits(&den) - big_bits(&m);
				if (k < 0) k = 0;
				big_shl(&m, k);
				Big q = { NULL, 0, 0 };
				big_div(&m, &den, &q);
				ld_round(&q, big_bits(&m) != 0, out, -k);
				free(den.d); free(q.d);
			}
		}
	}
	if (neg) out[9] |= 0x80;
	free(m.d);
}

static void ld_tok(Tok *t, uint8_t out[10]) {
	char buf[256];
	int n = t->raw && t->rawlen < (int)sizeof(buf) ? t->rawlen : 0;
	if (n) { memcpy(buf, t->raw, (size_t)n); buf[n] = 0; }
	else snprintf(buf, sizeof(buf), "%.17g", t->fval);
	ld_from_text(buf, out);
}

static void emit_x87(Code *c, uint8_t op, int ext, int base, int disp) { enc_rex(c, 0, 0, 0, base, 0); C8(c,op); enc_mem(c, ext, base, -1, 1, disp, 1, 0); }

static void ld_store_temp(Ctx *ctx) {
	Code *c = &ctx->text;
	int t = alloc_local(ctx, 16, 16);
	emit_x87(c, 0xDB, 7, RBP, t);
	emit_lea_rbp(c, RAX, t);
}

static void ld_flags_setcc(Code *c, int op) {
	switch (op) {
	case T_EQ: emit_setcc_al(c, 0x94); emit_setcc_cl(c, 0x9B); C8(c,0x20); C8(c,0xC8); break;
	case T_NEQ: emit_setcc_al(c, 0x95); emit_setcc_cl(c, 0x9A); C8(c,0x08); C8(c,0xC8); break;
	case T_GEQ: emit_setcc_al(c, 0x93); break;
	default: emit_setcc_al(c, 0x97); break;
	}
	emit_movzx_eax_al(c);
}

static void ld_nonzero(Code *c) {
	emit_x87(c, 0xDB, 5, RAX, 0);
	C8(c,0xD9); C8(c,0xEE);
	C8(c,0xDF); C8(c,0xE9);
	C8(c,0xDD); C8(c,0xD8);
	ld_flags_setcc(c, T_NEQ);
}

static void ld_push_double(Code *c, uint64_t bits) {
	emit_mov_rax_imm64(c, bits);
	emit_push(c, RAX);
	emit_x87(c, 0xDD, 0, RSP, 0);
	emit_pop(c, RAX);
}

static void gen_ldcast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (from->kind == TY_LDOUBLE && to->kind == TY_LDOUBLE) return;
	if (to->kind == TY_LDOUBLE) {
		emit_push(c, RAX);
		if (from->kind == TY_DOUBLE) emit_x87(c, 0xDD, 0, RSP, 0);
		else if (from->kind == TY_FLOAT) emit_x87(c, 0xD9, 0, RSP, 0);
		else {
			emit_x87(c, 0xDF, 5, RSP, 0);
			if (from->size == 8 && from->is_unsigned) {
				C8(c,0x48); C8(c,0x85); C8(c,0xC0);
				size_t js = jfwd(c, 0x79);
				ld_push_double(c, 0x43F0000000000000ULL);
				C8(c,0xDE); C8(c,0xC1);
				jland(c, js);
			}
		}
		emit_pop(c, RAX);
		ld_store_temp(ctx);
		return;
	}
	if (to->kind == TY_BOOL) { ld_nonzero(c); return; }
	if (is_flonum(to)) {
		int t = alloc_local(ctx, 8, 8);
		emit_x87(c, 0xDB, 5, RAX, 0);
		if (to->kind == TY_DOUBLE) { emit_x87(c, 0xDD, 3, RBP, t); emit_load_rbp(c, RAX, t); }
		else { emit_x87(c, 0xD9, 3, RBP, t); C8(c,0x8B); enc_mem(c, RAX, RBP, -1, 1, t, 1, 0); }
		return;
	}
	int t = alloc_local(ctx, 16, 16);
	emit_x87(c, 0xDB, 5, RAX, 0);
	emit_x87(c, 0xD9, 7, RBP, t);
	C8(c,0x0F); C8(c,0xB7); enc_mem(c, RAX, RBP, -1, 1, t, 1, 0);
	C8(c,0x0D); C32(c,0x0C00);
	C8(c,0x66); C8(c,0x89); enc_mem(c, RAX, RBP, -1, 1, t + 2, 1, 0);
	emit_x87(c, 0xD9, 5, RBP, t + 2);
	size_t jbig = 0, jdone = 0;
	if (to->size == 8 && to->is_unsigned) {
		ld_push_double(c, 0x43E0000000000000ULL);
		C8(c,0xDF); C8(c,0xE9);
		jbig = jfwd(c, 0x76);
	}
	emit_x87(c, 0xDF, 7, RBP, t + 8);
	if (jbig) {
		jdone = jfwd(c, 0xEB);
		jland(c, jbig);
		ld_push_double(c, 0x43E0000000000000ULL);
		C8(c,0xDE); C8(c,0xE9);
		emit_x87(c, 0xDF, 7, RBP, t + 8);
		C8(c,0x80); enc_mem(c, 6, RBP, -1, 1, t + 15, 1, 0); C8(c,0x80);
		jland(c, jdone);
	}
	emit_x87(c, 0xD9, 5, RBP, t);
	emit_load_rbp(c, RAX, t + 8);
	if (is_integer(to) && to->size < 8) emit_extend(c, to->size, to->is_unsigned);
}

static void gen_fnonzero(Ctx *ctx, Type *ty) {
	Code *c = &ctx->text;
	if (ty->kind == TY_LDOUBLE) { ld_nonzero(c); return; }
	emit_movq_xmm0_rax(c);
	emit_xorpd_xmm1(c);
	emit_ucomi(c, ty->kind == TY_DOUBLE, 0);
	emit_setcc_al(c, 0x95);
	emit_setcc_cl(c, 0x9A);
	C8(c,0x08); C8(c,0xC8);
	emit_movzx_eax_al(c);
}

static void gen_cast(Ctx *ctx, Type *from, Type *to);

static void gen_truth(Ctx *ctx, Type *ty) {
	if (is_complex(ty)) gen_cast(ctx, ty, ty_bool);
	else if (is_flonum(ty)) gen_fnonzero(ctx, ty);
}

static void gen_fcast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (from->kind == to->kind) return;
	if (from->kind == TY_LDOUBLE || to->kind == TY_LDOUBLE) { gen_ldcast(ctx, from, to); return; }
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

static void gen_ccast(Ctx *ctx, Type *from, Type *to);

static void gen_cast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (to->kind == TY_VOID) return;
	if (is_complex(from) || is_complex(to)) {
		if (!(is_complex(from) || is_arith(from)) || !(is_complex(to) || is_arith(to))) error_at(ctx, "invalid conversion involving a complex type");
		gen_ccast(ctx, from, to);
		return;
	}
	long long kv;
	if ((is_integer(from) || is_ptr(from)) && (is_integer(to) || is_ptr(to)) && expr_const(ctx, &kv)) {
		int ice = ctx->k_ice;
		drop_to(ctx, ctx->k_start, ctx->sp);
		emit_const(ctx, const_cast_val(kv, to));
		ctx->k_ice = ice;
		return;
	}
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

static int read_strs(Ctx *ctx, Buf *s) {
	int w = 0;
	TokVec v;
	memset(&v, 0, sizeof(v));
	while (ctx->tok.kind == T_STR) {
		if (ctx->tok.wide) {
			if (w && w != ctx->tok.wide) error_at(ctx, "concatenation of different wide string literals");
			w = ctx->tok.wide;
		}
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	for (int i = 0; i < v.n; i++) {
		Tok *t = &v.v[i];
		if (t->wide || !w) { buf_append(s, t->str, t->len); continue; }
		Lex L;
		memset(&L, 0, sizeof(L));
		L.s = t->str;
		L.n = (size_t)t->len;
		while (L.i < L.n) put_wide(s, w, lex_utf8(&L));
	}
	for (int i = 0; i < wide_size(w); i++) buf_append_byte(s, 0);
	free(v.v);
	return w;
}

static Type *wide_type(int w) { return w == 'L' ? ty_int : w == 'U' ? ty_uint : w == 'u' ? ty_ushort : ty_char; }

static int is_typename(Ctx *ctx, Tok *t) {
	switch (t->kind) {
	case T_INT: case T_CHAR: case T_SHORT: case T_LONG: case T_SIGNED: case T_UNSIGNED:
	case T_VOID: case T_BOOL: case T_FLOAT: case T_DOUBLE: case T_COMPLEX:
	case T_STRUCT: case T_UNION: case T_ENUM: case T_TYPEDEF:
	case T_STATIC: case T_EXTERN: case T_CONST: case T_VOLATILE: case T_INLINE:
	case T_REGISTER: case T_AUTO: case T_RESTRICT: case T_TYPEOF: case T_ALIGNAS: case T_NORETURN: case T_THREAD:
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
	int nlong = 0, nshort = 0, nint = 0, nchar = 0, nvoid = 0, nbool = 0, nfloat = 0, ndouble = 0, ncomplex = 0;
	int is_signed = 0, is_unsigned = 0;
	Type *user = NULL;
	int attrs = 0;
	Attr da;
	memset(&da, 0, sizeof(da));
	if (storage) *storage = ST_NONE;
	ctx->decl_thread = 0;
	ctx->decl_inline = 0;
	ctx->decl_register = 0;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_INLINE) ctx->decl_inline = 1;
		if (k == T_REGISTER) ctx->decl_register = 1;
		if (k == T_THREAD) {
			if (!storage) error_at(ctx, "storage class specifier not allowed here");
			ctx->decl_thread = 1;
			next(ctx); continue;
		}
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
		if (k == T_COMPLEX) { ncomplex++; next(ctx); continue; }
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
	if (ncomplex) {
		if (user || nvoid || nbool || nchar || nshort || nint || is_signed || is_unsigned) error_at(ctx, "only floating complex types are supported");
		return complex_of(nfloat ? ty_float : nlong ? ty_ldouble : ty_double);
	}
	if (user) return user;
	if (nvoid) return ty_void;
	if (nbool) return ty_bool;
	if (nfloat) return ty_float;
	if (ndouble) return nlong ? ty_ldouble : ty_double;
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

static int tokens_const(Ctx *ctx, TokVec *v);

static Type *type_suffix(Ctx *ctx, Type *ty) {
	if (ctx->tok.kind == T_LP) {
		next(ctx);
		return func_params(ctx, ty);
	}
	if (ctx->tok.kind == T_LBRACKET) {
		next(ctx);
		while (is_qualifier(ctx->tok.kind) || ctx->tok.kind == T_STATIC) next(ctx);
		int len = -1;
		if (ctx->tok.kind == T_MUL && peek(ctx)->kind == T_RBRACKET) next(ctx);
		else if (ctx->tok.kind != T_RBRACKET) {
			TokVec v;
			memset(&v, 0, sizeof(v));
			for (int d = 0; ctx->tok.kind != T_EOF && (d || ctx->tok.kind != T_RBRACKET); next(ctx)) {
				if (ctx->tok.kind == T_LBRACKET || ctx->tok.kind == T_LP) d++;
				else if (ctx->tok.kind == T_RBRACKET || ctx->tok.kind == T_RP) d--;
				tv_push(&v, ctx->tok);
			}
			int k = tokens_const(ctx, &v);
			replay(ctx, &v);
			free(v.v);
			if (k || !ctx->fn || ctx->noasm) len = (int)const_expr(ctx);
			else {
				Code *c = &ctx->text;
				int cnt = alloc_local(ctx, 8, 8);
				Type *nt = rval(ctx, parse_assign(ctx));
				if (!is_integer(nt)) error_at(ctx, "size of array has non-integer type");
				gen_cast(ctx, nt, ty_long);
				emit_store_rbp(c, RAX, cnt);
				expect(ctx, T_RBRACKET);
				ty = type_suffix(ctx, ty);
				if (ty->kind == TY_FUNC || (ty->kind == TY_STRUCT && !ty->complete) || ty->kind == TY_VOID) error_at(ctx, "invalid element type of a variable length array");
				int sz = alloc_local(ctx, 8, 8);
				emit_load_rbp(c, RAX, cnt);
				if (ty->vla) { C8(c,0x48); C8(c,0x0F); C8(c,0xAF); enc_mem(c, RAX, RBP, -1, 1, ty->vla, 1, 0); }
				else emit_scale(c, RAX, ty->size);
				emit_store_rbp(c, RAX, sz);
				Type *t = new_type(TY_ARRAY, 0, ty->align);
				t->base = ty;
				t->len = 0;
				t->complete = 1;
				t->vla = sz;
				return t;
			}
		}
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
		int na = ctx->name_attrs;
		Attr *nat = ctx->name_attr;
		expect(ctx, T_RP);
		ty = type_suffix(ctx, ty);
		*ph = *ty;
		if (name) { ctx->name_attrs = na; ctx->name_attr = nat; }
		return inner;
	}
	if (ctx->tok.kind == T_IDENT) {
		int na = ctx->tok.attrs;
		Attr *nat = ctx->tok.attr;
		if (name) *name = ctx->tok.str;
		next(ctx);
		Type *r = type_suffix(ctx, ty);
		if (name) { ctx->name_attrs = na; ctx->name_attr = nat; }
		return r;
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
		int a = packed ? 1 : ctx->pack && ctx->pack < mt->align ? ctx->pack : mt->align;
		if (fs[i].align > a) a = fs[i].align;
		if (fs[i].width < -1) {
			int moff = is_union ? 0 : (int)align_up((size_t)((bits + 7) / 8), (size_t)a);
			Member *am = (Member *)calloc(1, sizeof(Member));
			am->ty = mt; am->off = moff;
			cur->next = am; cur = am;
			for (Member *bm = mt->members; bm; bm = bm->next) {
				Member *m = (Member *)calloc(1, sizeof(Member));
				m->name = bm->name; m->ty = bm->ty; m->off = moff + bm->off; m->flat = 1;
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
	struct_layout(ctx, ty, fs, nf, (attrs & ATTR_PACKED) != 0 || ctx->pack == 1, sa.align);
	free(fs);
	return ty;
}

static Type *enum_decl(Ctx *ctx) {
	next(ctx);
	char *tag = NULL;
	if (ctx->tok.kind == T_IDENT) { tag = ctx->tok.str; next(ctx); }
	if (ctx->tok.kind != T_LB) {
		Tag *tg = tag ? find_tag(ctx, tag) : NULL;
		return tg && is_integer(tg->ty) ? tg->ty : ty_int;
	}
	next(ctx);
	long long v = 0, lo = 0, hi = 0;
	int n = 0, cap = 16;
	Sym **cs = (Sym **)malloc(sizeof(Sym *) * (size_t)cap);
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected enumerator name");
		char *name = ctx->tok.str;
		next(ctx);
		if (consume(ctx, T_ASSIGN)) v = const_expr(ctx);
		Sym *s = push_sym(ctx, name, SYM_ENUMC, ty_int);
		if (!n || v < lo) lo = v;
		if (!n || v > hi) hi = v;
		if (n == cap) cs = (Sym **)realloc(cs, sizeof(Sym *) * (size_t)(cap *= 2));
		cs[n++] = s;
		s->val = v++;
		if (!consume(ctx, T_COMMA)) break;
	}
	expect(ctx, T_RB);
	Type *t = ty_int;
	if (lo < -2147483647LL - 1 || hi > 2147483647LL)
		t = lo >= 0 && hi <= 0xFFFFFFFFLL ? ty_uint : lo >= 0 ? ty_ulong : ty_long;
	for (int i = 0; i < n; i++) cs[i]->ty = t;
	free(cs);
	if (tag) push_tag(ctx, tag, t);
	return t;
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

static int is_builtin_const(char *name);

static int tokens_const(Ctx *ctx, TokVec *v) {
	for (int i = 0; i < v->n; i++) {
		Tok *t = &v->v[i];
		int k = t->kind;
		if (k == T_IDENT && is_builtin_const(t->str) && i + 1 < v->n && v->v[i+1].kind == T_LP) {
			int d = 0;
			do {
				i++;
				if (v->v[i].kind == T_LP) d++;
				else if (v->v[i].kind == T_RP) d--;
			} while (d > 0 && i + 1 < v->n);
		} else if (k == T_IDENT) {
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
			TokVec v;
			memset(&v, 0, sizeof(v));
			for (int d = 0; d || ctx->tok.kind != T_RBRACKET; next(ctx)) {
				if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected end of file");
				if (ctx->tok.kind == T_LBRACKET || ctx->tok.kind == T_LP) d++;
				else if (ctx->tok.kind == T_RBRACKET || ctx->tok.kind == T_RP) d--;
				tv_push(&v, ctx->tok);
			}
			int k = tokens_const(ctx, &v);
			replay(ctx, &v);
			free(v.v);
			if (k || !ctx->ofs_rt) off += const_expr(ctx) * ty->base->size;
			else {
				Code *c = &ctx->text;
				if (ctx->ofs_have) gen_push(ctx);
				Type *it = rval(ctx, parse_expr(ctx));
				if (!is_integer(it)) error_at(ctx, "array subscript is not an integer");
				gen_cast(ctx, it, ty_long);
				C8(c,0x48); C8(c,0x69); C8(c,0xC0); C32(c,(uint32_t)ty->base->size);
				if (ctx->ofs_have) { gen_pop(ctx, RCX); C8(c,0x48); C8(c,0x01); C8(c,0xC8); }
				ctx->ofs_have = 1;
			}
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
		if (!v && ctx->fn && ctx->fold) {
			replay(ctx, &t);
			Mark m = mark_code(ctx);
			size_t ro = ctx->rodata.len;
			int frame = ctx->fn->frame;
			ctx->noasm++;
			Type *ty = rval(ctx, parse_assign(ctx));
			long long kv;
			v = !is_flonum(ty) && expr_const(ctx, &kv);
			ctx->noasm--;
			drop_code(ctx, m);
			ctx->rodata.len = ro;
			ctx->fn->frame = frame;
		}
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

typedef struct {
	long long v;
	long double f;
	Type *ty;
	Sym *sym;
	int sect, lv;
} CV;

static int fold_binop(int op, Type *lt, Type *rt, long long a, long long b, long long *res, Type **rty);
static CV ce_cast(Ctx *ctx);
static CV ce_cond(Ctx *ctx);

static CV cv_int(long long v, Type *t) {
	CV c;
	memset(&c, 0, sizeof(c));
	c.ty = t;
	c.v = const_cast_val(v, t);
	c.sect = -1;
	return c;
}

static int cv_reloc(CV *c) { return c->sym || c->sect >= 0; }

static long double flt_round(long double f, Type *t) {
	if (t->kind == TY_FLOAT) return (float)f;
	if (t->kind == TY_DOUBLE) return (double)f;
	return f;
}

static CV cv_flt(long double f, Type *t) {
	CV c = cv_int(0, t);
	c.f = flt_round(f, t);
	return c;
}

static long double cv_f(CV *c) {
	if (is_flonum(c->ty)) return c->f;
	if (c->ty->is_unsigned) return (long double)(unsigned long long)c->v;
	return (long double)c->v;
}

static long long flt_to_int(long double f, Type *t) {
	if (t->kind == TY_BOOL) return f != 0;
	if (t->size == 8 && t->is_unsigned) return (long long)(unsigned long long)f;
	return const_cast_val((long long)f, t);
}

static CV cv_convert(CV c, Type *t) {
	if (is_flonum(t)) return cv_flt(cv_f(&c), t);
	if (is_flonum(c.ty)) return cv_int(flt_to_int(c.f, t), t);
	return cv_int(c.v, t);
}

static int cv_truth(CV *c) { return cv_reloc(c) ? 1 : is_flonum(c->ty) ? c->f != 0 : c->v != 0; }

static CV cv_rval(Ctx *ctx, CV c) {
	if (c.ty->kind == TY_FUNC) { c.ty = ptr_to(c.ty); c.lv = 0; return c; }
	if (!c.lv) return c;
	if (c.ty->kind == TY_ARRAY) { c.ty = ptr_to(c.ty->base); c.lv = 0; return c; }
	error_at(ctx, "expression must be constant here");
	return c;
}

static long long cv_value(Ctx *ctx, CV c) {
	c = cv_rval(ctx, c);
	if (cv_reloc(&c)) error_at(ctx, "expression must be constant here");
	if (is_flonum(c.ty)) return (long long)c.f;
	return c.v;
}

static Type *num_type(Ctx *ctx, Tok *t) {
	if (ctx->in_pp_if) return t->is_unsigned || (t->is_long && t->val < 0) ? ty_ulong : ty_long;
	return t->is_long > 1 ? (t->is_unsigned ? ty_ullong : ty_llong)
		: t->is_long ? (t->is_unsigned ? ty_ulong : ty_long)
		: (t->is_unsigned ? ty_uint : ty_int);
}

static int bit_builtin(const char *n, long long a, long long *r, Type **t) {
	unsigned long long u = (unsigned long long)a;
	int w = 32;
	char b[32];
	if (strncmp(n, "__builtin_", 10)) return 0;
	n += 10;
	if (!strcmp(n, "bswap16")) { *r = (long long)(((u & 0xFF) << 8) | ((u >> 8) & 0xFF)); *t = ty_ushort; return 1; }
	if (!strcmp(n, "bswap32") || !strcmp(n, "bswap64")) {
		int nb = n[5] == '3' ? 4 : 8;
		unsigned long long v = 0;
		for (int i = 0; i < nb; i++) v |= ((u >> (8 * i)) & 0xFF) << (8 * (nb - 1 - i));
		*r = (long long)v;
		*t = nb == 4 ? ty_uint : ty_ulong;
		return 1;
	}
	size_t l = strlen(n);
	if (l >= 2 && !strcmp(n + l - 2, "ll")) { w = 64; l -= 2; }
	else if (l >= 1 && n[l - 1] == 'l') { w = 64; l -= 1; }
	if (l >= sizeof(b)) return 0;
	memcpy(b, n, l);
	b[l] = 0;
	if (w == 32) u &= 0xFFFFFFFFULL;
	int k = 0;
	*t = ty_int;
	if (!strcmp(b, "popcount")) { while (u) { k += (int)(u & 1); u >>= 1; } *r = k; return 1; }
	if (!strcmp(b, "parity")) { while (u) { k ^= (int)(u & 1); u >>= 1; } *r = k; return 1; }
	if (!strcmp(b, "ffs")) { if (!u) { *r = 0; return 1; } while (!(u & 1)) { u >>= 1; k++; } *r = k + 1; return 1; }
	if (!u) return 0;
	if (!strcmp(b, "ctz")) { while (!(u & 1)) { u >>= 1; k++; } *r = k; return 1; }
	if (!strcmp(b, "clz")) { while (!(u >> (w - 1))) { u <<= 1; k++; } *r = k; return 1; }
	return 0;
}

static CV ce_primary(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_SIZEOF) { next(ctx); return cv_int(sizeof_operand(ctx), ty_ulong); }
	if (k == T_ALIGNOF) { next(ctx); return cv_int(alignof_operand(ctx), ty_ulong); }
	if (k == T_LP) {
		next(ctx);
		CV c = ce_cond(ctx);
		expect(ctx, T_RP);
		return c;
	}
	if (k == T_NUM) {
		if (ctx->tok.badnum) error_at(ctx, "invalid number literal");
		Type *t = num_type(ctx, &ctx->tok);
		long long v = ctx->tok.val;
		next(ctx);
		return cv_int(v, t);
	}
	if (k == T_FNUM) {
		if (ctx->tok.badnum) error_at(ctx, "invalid floating constant");
		if (ctx->tok.imag) error_at(ctx, "imaginary constants are not supported in constant expressions");
		CV c;
		if (ctx->tok.is_long) {
			uint8_t b[16];
			long double f;
			memset(b, 0, sizeof(b));
			ld_tok(&ctx->tok, b);
			memcpy(&f, b, sizeof(f) < 16 ? sizeof(f) : 16);
			c = cv_flt(f, ty_ldouble);
		} else c = cv_flt(ctx->tok.fval, ctx->tok.is_float ? ty_float : ty_double);
		next(ctx);
		return c;
	}
	if (k == T_STR && !ctx->in_pp_if) {
		Buf s; buf_init(&s);
		int w = read_strs(ctx, &s);
		CV c = cv_int(0, array_of(wide_type(w), (int)s.len / wide_size(w)));
		c.v = add_rodata(ctx, s.data, s.len);
		c.sect = SEC_RODATA;
		c.lv = 1;
		free(s.data);
		return c;
	}
	if (k == T_IDENT) {
		char *name = ctx->tok.str;
		if (is_builtin_const(name)) return cv_int(builtin_const(ctx, name), name == id_offsetof ? ty_ulong : ty_int);
		if (name == id_expect || name == id_choose_expr) {
			next(ctx);
			expect(ctx, T_LP);
			CV c = ce_cond(ctx);
			expect(ctx, T_COMMA);
			if (name == id_choose_expr) {
				long long v = cv_value(ctx, c);
				if (v) { c = ce_cond(ctx); expect(ctx, T_COMMA); free(capture_expr(ctx).v); }
				else { free(capture_expr(ctx).v); expect(ctx, T_COMMA); c = ce_cond(ctx); }
			} else ce_cond(ctx);
			expect(ctx, T_RP);
			return c;
		}
		if (ctx->in_pp_if) { next(ctx); return cv_int(0, ty_long); }
		Sym *s = lookup(ctx, name);
		long long br;
		Type *bt;
		if (!s && peek(ctx)->kind == T_LP && bit_builtin(name, 1, &br, &bt)) {
			next(ctx);
			next(ctx);
			long long a = cv_value(ctx, ce_cond(ctx));
			expect(ctx, T_RP);
			if (!bit_builtin(name, a, &br, &bt)) error_at(ctx, "%s of zero is undefined", name);
			return cv_int(br, bt);
		}
		if (s && s->kind == SYM_ENUMC) { next(ctx); return cv_int(s->val, s->ty); }
		if (s && (s->kind == SYM_VAR || s->kind == SYM_FUNC) && !s->is_local) {
			Sym *g = s->global ? s->global : s;
			if (!g->tls) {
				next(ctx);
				CV c = cv_int(0, s->ty);
				c.sym = g;
				c.lv = 1;
				return c;
			}
		}
		error_at(ctx, "'%s' is not a constant", name);
	}
	error_at(ctx, "expected constant expression, got '%s'", tok_desc(&ctx->tok));
	return cv_int(0, ty_int);
}

static CV ce_postfix(Ctx *ctx) {
	CV c = ce_primary(ctx);
	for (;;) {
		if (ctx->tok.kind == T_LBRACKET) {
			next(ctx);
			c = cv_rval(ctx, c);
			CV i = cv_rval(ctx, ce_cond(ctx));
			expect(ctx, T_RBRACKET);
			if (!is_ptr(c.ty) || !is_integer(i.ty) || cv_reloc(&i)) error_at(ctx, "expression must be constant here");
			c.v += i.v * c.ty->base->size;
			c.ty = c.ty->base;
			c.lv = 1;
		} else if (ctx->tok.kind == T_DOT || ctx->tok.kind == T_ARROW) {
			int arrow = ctx->tok.kind == T_ARROW;
			next(ctx);
			if (arrow) {
				c = cv_rval(ctx, c);
				if (!is_ptr(c.ty)) error_at(ctx, "member access on non-pointer");
				c.ty = c.ty->base;
			} else if (!c.lv) error_at(ctx, "expression must be constant here");
			if (c.ty->kind != TY_STRUCT) error_at(ctx, "member access on non-struct");
			if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected member name");
			Member *m = find_member(c.ty, ctx->tok.str);
			if (!m) error_at(ctx, "no member named '%s'", ctx->tok.str);
			next(ctx);
			c.v += m->off;
			c.ty = m->ty;
			c.lv = 1;
		} else return c;
	}
}

static CV ce_unary(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_AMP) {
		next(ctx);
		CV c = ce_cast(ctx);
		if (!c.lv && c.ty->kind != TY_FUNC) error_at(ctx, "lvalue required as unary '&' operand");
		c.ty = ptr_to(c.ty);
		c.lv = 0;
		return c;
	}
	if (k == T_MUL) {
		next(ctx);
		CV c = cv_rval(ctx, ce_cast(ctx));
		if (!is_ptr(c.ty)) error_at(ctx, "invalid type argument of unary '*'");
		c.ty = c.ty->base;
		c.lv = 1;
		return c;
	}
	if (k == T_MINUS || k == T_PLUS || k == T_TILDE || k == T_NOT) {
		next(ctx);
		CV c = cv_rval(ctx, ce_cast(ctx));
		if (k == T_NOT) return cv_int(!cv_truth(&c), ty_int);
		if (!cv_reloc(&c) && is_flonum(c.ty) && k != T_TILDE) return k == T_MINUS ? cv_flt(-c.f, c.ty) : c;
		if (cv_reloc(&c) || !is_integer(c.ty)) error_at(ctx, "expression must be constant here");
		Type *t = int_promote(c.ty);
		long long v = const_cast_val(c.v, t);
		if (k == T_MINUS) v = (long long)(0 - (unsigned long long)v);
		else if (k == T_TILDE) v = ~v;
		return cv_int(v, t);
	}
	return ce_postfix(ctx);
}

static CV ce_cast(Ctx *ctx) {
	if (ctx->tok.kind == T_LP && !ctx->in_pp_if && is_typename(ctx, peek(ctx))) {
		next(ctx);
		Type *t = parse_typename(ctx);
		expect(ctx, T_RP);
		if (ctx->tok.kind == T_LB) {
			if (!ctx->ginit) error_at(ctx, "expression must be constant here");
			static int ncl;
			char nb[64];
			snprintf(nb, sizeof(nb), "__compound_literal.%d", ncl++);
			Sym *g = (Sym *)calloc(1, sizeof(Sym));
			g->name = intern(nb);
			g->kind = SYM_VAR;
			g->ty = t;
			g->is_static = 1;
			g->gnext = ctx->globals;
			ctx->globals = g;
			TokVec v;
			memset(&v, 0, sizeof(v));
			int d = 0;
			do {
				if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected end of file");
				if (ctx->tok.kind == T_LB) d++;
				else if (ctx->tok.kind == T_RB) d--;
				tv_push(&v, ctx->tok);
				next(ctx);
			} while (d);
			if (ctx->ncl == ctx->capcl) {
				ctx->capcl = ctx->capcl ? ctx->capcl * 2 : 16;
				ctx->cl = realloc(ctx->cl, sizeof(*ctx->cl) * (size_t)ctx->capcl);
			}
			ctx->cl[ctx->ncl].g = g;
			ctx->cl[ctx->ncl++].v = v;
			CV c = cv_int(0, t);
			c.sym = g;
			c.lv = 1;
			return c;
		}
		CV c = cv_rval(ctx, ce_cast(ctx));
		if (t->kind == TY_VOID) { c.ty = t; return c; }
		if (cv_reloc(&c)) {
			if (t->size != 8 || !(is_integer(t) || is_ptr(t))) error_at(ctx, "expression must be constant here");
			c.ty = t;
			return c;
		}
		if (t->kind == TY_STRUCT) error_at(ctx, "cast to struct type");
		return cv_convert(c, t);
	}
	return ce_unary(ctx);
}

static CV ce_binary(Ctx *ctx, int minprec) {
	CV l = ce_cast(ctx);
	for (;;) {
		int k = ctx->tok.kind;
		int p = binop_prec(k);
		if (p == 0 || p < minprec) return l;
		next(ctx);
		l = cv_rval(ctx, l);
		if (k == T_AND || k == T_OR) {
			int lv = cv_truth(&l);
			if (k == T_AND ? !lv : lv) { ce_binary(ctx, p + 1); l = cv_int(k == T_OR, ty_int); continue; }
			CV r = cv_rval(ctx, ce_binary(ctx, p + 1));
			l = cv_int(cv_truth(&r), ty_int);
			continue;
		}
		CV r = cv_rval(ctx, ce_binary(ctx, p + 1));
		if ((is_flonum(l.ty) || is_flonum(r.ty)) && !cv_reloc(&l) && !cv_reloc(&r)) {
			if (!is_arith(l.ty) || !is_arith(r.ty)) error_at(ctx, "invalid operands to binary operator");
			Type *t = arith_conv(l.ty, r.ty);
			long double a = cv_convert(l, t).f, b = cv_convert(r, t).f, f;
			switch (k) {
			case T_PLUS: f = t->kind == TY_FLOAT ? (float)a + (float)b : t->kind == TY_DOUBLE ? (double)a + (double)b : a + b; break;
			case T_MINUS: f = t->kind == TY_FLOAT ? (float)a - (float)b : t->kind == TY_DOUBLE ? (double)a - (double)b : a - b; break;
			case T_MUL: f = t->kind == TY_FLOAT ? (float)a * (float)b : t->kind == TY_DOUBLE ? (double)a * (double)b : a * b; break;
			case T_DIV: f = t->kind == TY_FLOAT ? (float)a / (float)b : t->kind == TY_DOUBLE ? (double)a / (double)b : a / b; break;
			case T_LT: l = cv_int(a < b, ty_int); continue;
			case T_GT: l = cv_int(a > b, ty_int); continue;
			case T_LEQ: l = cv_int(a <= b, ty_int); continue;
			case T_GEQ: l = cv_int(a >= b, ty_int); continue;
			case T_EQ: l = cv_int(a == b, ty_int); continue;
			case T_NEQ: l = cv_int(a != b, ty_int); continue;
			default: error_at(ctx, "invalid operands to binary operator"); f = 0;
			}
			l = cv_flt(f, t);
			continue;
		}
		if ((k == T_PLUS || k == T_MINUS) && is_ptr(l.ty) && is_integer(r.ty) && !cv_reloc(&r)) {
			long long sz = l.ty->base->kind == TY_VOID || l.ty->base->kind == TY_FUNC ? 1 : l.ty->base->size;
			l.v += (k == T_PLUS ? r.v : -r.v) * sz;
			continue;
		}
		if (k == T_PLUS && is_integer(l.ty) && is_ptr(r.ty) && !cv_reloc(&l)) {
			long long sz = r.ty->base->kind == TY_VOID || r.ty->base->kind == TY_FUNC ? 1 : r.ty->base->size;
			r.v += l.v * sz;
			l = r;
			continue;
		}
		if (cv_reloc(&l) || cv_reloc(&r)) {
			int same = l.sym == r.sym && l.sect == r.sect;
			if (k == T_MINUS && same) {
				long long sz = is_ptr(l.ty) && l.ty->base->size > 0 ? l.ty->base->size : 1;
				l = cv_int((l.v - r.v) / sz, ty_long);
				continue;
			}
			if ((k == T_PLUS || k == T_MINUS) && cv_reloc(&l) && !cv_reloc(&r) && is_integer(r.ty)) {
				l.v += k == T_PLUS ? r.v : -r.v;
				continue;
			}
			if ((k == T_EQ || k == T_NEQ) && (same || (!cv_reloc(&r) && !r.v) || (!cv_reloc(&l) && !l.v))) {
				int eq = same && l.v == r.v;
				l = cv_int(k == T_EQ ? eq : !eq, ty_int);
				continue;
			}
			error_at(ctx, "expression must be constant here");
		}
		Type *lt = is_ptr(l.ty) ? ty_ulong : l.ty, *rt = is_ptr(r.ty) ? ty_ulong : r.ty;
		long long res;
		Type *rty;
		if (fold_binop(k, lt, rt, l.v, r.v, &res, &rty)) { l = cv_int(res, rty); continue; }
		if ((k == T_DIV || k == T_MOD) && !r.v) error_at(ctx, "division by zero in a constant expression");
		if (k == T_SHL || k == T_SHR) { l = cv_int(k == T_SHL ? (long long)((unsigned long long)l.v << (r.v & 63)) : l.v >> (r.v & 63), int_promote(lt)); continue; }
		l = cv_int(k == T_DIV ? l.v / r.v : l.v % r.v, arith_conv(lt, rt));
	}
}

static void ce_skip(Ctx *ctx) {
	int d = 0, q = 0;
	for (;; next(ctx)) {
		int k = ctx->tok.kind;
		if (k == T_EOF) error_at(ctx, "unexpected end of expression");
		if (k == T_LP || k == T_LBRACKET || k == T_LB) d++;
		else if (k == T_RP || k == T_RBRACKET || k == T_RB) { if (!d) return; d--; }
		else if (!d && (k == T_COMMA || k == T_SEMI)) return;
		else if (!d && k == T_QUESTION) q++;
		else if (!d && k == T_COLON) { if (!q) return; q--; }
	}
}

static CV ce_cond(Ctx *ctx) {
	CV c = ce_binary(ctx, 1);
	if (!consume(ctx, T_QUESTION)) return c;
	c = cv_rval(ctx, c);
	int t = cv_truth(&c);
	if (ctx->tok.kind == T_COLON) {
		next(ctx);
		CV b = ce_cond(ctx);
		return t ? c : cv_rval(ctx, b);
	}
	CV r;
	if (t) {
		r = cv_rval(ctx, ce_cond(ctx));
		expect(ctx, T_COLON);
		ce_skip(ctx);
	} else {
		ce_skip(ctx);
		expect(ctx, T_COLON);
		r = cv_rval(ctx, ce_cond(ctx));
	}
	return r;
}

static long long const_expr(Ctx *ctx) { return cv_value(ctx, ce_cond(ctx)); }

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
	ctx->sizeof_vla = t->vla;
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
	int fold = ctx->fold;
	ctx->fold = 1;
	Type *t = parse_unary(ctx);
	ctx->fold = fold;
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
			if (!m->flat && !classify_at(m->ty, off + m->off, cls)) return 0;
		return 1;
	}
	if (t->kind == TY_ARRAY) {
		for (int i = 0; i < t->len; i++)
			if (!classify_at(t->base, off + i * t->base->size, cls)) return 0;
		return 1;
	}
	if (t->bit_width) { cls[off / 8] |= CL_INT; return 1; }
	if (t->kind == TY_LDOUBLE) return 0;
	if (t->kind == TY_COMPLEX) return classify_at(t->base, off, cls) && classify_at(t->base, off + t->base->size, cls);
	if (t->size == 0) return 1;
	if (off % t->size || off / 8 > 1) return 0;
	cls[off / 8] |= is_flonum(t) ? CL_SSE : CL_INT;
	return 1;
}

static int classify(Type *t, int *cls) {
	cls[0] = cls[1] = 0;
	if (t->kind == TY_VOID) return 0;
	if (t->kind == TY_LDOUBLE || (t->kind == TY_COMPLEX && t->base->kind == TY_LDOUBLE)) return -1;
	if (t->kind != TY_STRUCT && t->kind != TY_COMPLEX) { cls[0] = is_flonum(t) ? CL_SSE : CL_INT; return 1; }
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
	{ "__builtin_bswap16", 1, { RAX }, &ty_ushort, 7, "\x66\xC1\xC0\x08\x0F\xB7\xC0" },
	{ "__builtin_bswap32", 1, { RAX }, &ty_uint, 2, "\x0F\xC8" },
	{ "__builtin_bswap64", 1, { RAX }, &ty_ulong, 3, "\x48\x0F\xC8" },
	{ "__invlpg", 1, { RAX }, &ty_void, 3, "\x0F\x01\x38" },
	{ "__cpuid", 3, { RAX, RCX, RDI }, &ty_void, 15, "\x53\x0F\xA2\x89\x07\x89\x5F\x04\x89\x4F\x08\x89\x57\x0C\x5B" },
	{ "__rdtsc", 0, { 0 }, &ty_ulong, 9, "\x0F\x31\x48\xC1\xE2\x20\x48\x09\xD0" },
	{ NULL, 0, { 0 }, NULL, 0, NULL }
};
static char *mach_ids[sizeof(mach_builtins) / sizeof(mach_builtins[0])];

static Type *parse_builtin_mach(Ctx *ctx, const MachBuiltin *b) {
	ctx->sidefx++;
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

static Type *parse_builtin_va_end(Ctx *ctx, int copy) {
	Code *c = &ctx->text;
	next(ctx);
	expect(ctx, T_LP);
	rval(ctx, parse_assign(ctx));
	if (copy) {
		gen_push(ctx);
		expect(ctx, T_COMMA);
		rval(ctx, parse_assign(ctx));
		emit_mov_reg_reg(c, RSI, RAX);
		gen_pop(ctx, RDI);
		emit_memcpy(c, 24);
	}
	expect(ctx, T_RP);
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
		int tmp = t->kind == TY_STRUCT || t->kind == TY_COMPLEX ? alloc_local(ctx, 16, 16) : 0;
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

static int returns_twice(Sym *g) {
	static const char *names[] = { "setjmp", "_setjmp", "sigsetjmp", "__sigsetjmp", "savectx", "vfork", "getcontext", NULL };
	if (g->twice || (g->global && g->global->twice)) return 1;
	for (int i = 0; names[i]; i++) if (!strcmp(g->name, names[i])) return 1;
	return 0;
}

static Type *parse_call(Ctx *ctx, Type *fty) {
	Code *c = &ctx->text;
	ctx->sidefx++;
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
			if ((ats[a]->kind == TY_STRUCT || ats[a]->kind == TY_COMPLEX) && n > 0) tmp[a] = alloc_local(ctx, 16, 16);
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
			if (ats[a]->kind == TY_STRUCT || ats[a]->kind == TY_LDOUBLE || ats[a]->kind == TY_COMPLEX) {
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
		if (ats[a]->kind != TY_STRUCT && ats[a]->kind != TY_COMPLEX) {
			if (cls[0] == CL_SSE) emit_movq_xmm_load(c, sse++, RSP, disp);
			else emit_load_rsp(c, argregs[gp++], disp);
			continue;
		}
		for (int i = 0; i < n; i++) {
			if (cls[i] == CL_INT) emit_load_rbp(c, argregs[gp++], tmp[a] + 8 * i);
			else if (cls[i] == CL_SSE) emit_movq_xmm_load(c, sse++, RBP, tmp[a] + 8 * i);
		}
	}
	int outer = ctx->sp - nargs - (direct ? 0 : 1), save = 0;
	if (direct && outer > 0 && returns_twice(direct)) {
		save = alloc_local(ctx, 8 * outer, 8);
		for (int j = 0; j < outer; j++) {
			emit_load_rsp(c, R11, area + 8 * (nargs + j));
			emit_store_rbp(c, R11, save + 8 * j);
		}
	}
	if (!direct) emit_load_rsp(c, R11, area + 8 * nargs);
	if (sse) emit_mov_rax_imm(c, sse);
	else emit_xor_eax_eax(c);
	if (direct) emit_call_sym(ctx, direct);
	else emit_call_r11(c);
	int drop = extra + nargs + (direct ? 0 : 1);
	if (drop) emit_add_rsp(c, drop * 8);
	for (int j = 0; save && j < outer; j++) {
		emit_load_rbp(c, R11, save + 8 * j);
		C8(c,0x4C); C8(c,0x89); C8(c,0x9C); C8(c,0x24); C32(c,(uint32_t)(8 * j));
	}
	ctx->sp -= nargs + (direct ? 0 : 1);
	if (ret->kind == TY_COMPLEX && rn < 0) {
		int t = alloc_local(ctx, 32, 16);
		emit_x87(c, 0xDB, 7, RBP, t);
		emit_x87(c, 0xDB, 7, RBP, t + 16);
		emit_lea_rbp(c, RAX, t);
	} else if ((ret->kind == TY_STRUCT || ret->kind == TY_COMPLEX) && rn >= 0) {
		int t = alloc_local(ctx, 16, 16), g = 0, x = 0;
		static const int retregs[2] = { RAX, RDX };
		for (int i = 0; i < rn; i++) {
			if (rcls[i] == CL_INT) emit_store_rbp(c, retregs[g++], t + 8 * i);
			else if (rcls[i] == CL_SSE) emit_movq_xmm_store(c, x++, RBP, t + 8 * i);
		}
		emit_lea_rbp(c, RAX, t);
	} else if (ret->kind == TY_LDOUBLE) ld_store_temp(ctx);
	else if (ret->kind == TY_DOUBLE) emit_movq_rax_xmm0(c);
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

static int local_labels(Ctx *ctx);

static Type *parse_stmt_expr(Ctx *ctx) {
	if (!ctx->fn) error_at(ctx, "statement expression outside a function");
	next(ctx);
	next(ctx);
	enter_scope(ctx);
	ctx->sx_depth++;
	Type *ty = ty_void;
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in statement expression");
		ty = ty_void;
		if (local_labels(ctx)) continue;
		if (ctx->tok.kind == T_IDENT && peek(ctx)->kind == T_COLON) { define_label(ctx); continue; }
		if (is_expr_stmt(ctx)) {
			Type *t = parse_expr(ctx);
			expect(ctx, T_SEMI);
			if (ctx->tok.kind == T_RB) ty = rval(ctx, t);
			ctx->lv = 0;
		} else parse_stmt(ctx);
	}
	ctx->sx_depth--;
	ctx->jumped = 0;
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
	size_t ks = ctx->k_start, ke = ctx->k_end;
	long long kv = ctx->k_val;
	int kn = ctx->k_nrel, ki = ctx->k_ice;
	parse_assign(ctx);
	drop_code(ctx, m);
	ctx->k_start = ks; ctx->k_end = ke; ctx->k_val = kv; ctx->k_nrel = kn; ctx->k_ice = ki;
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

static int libc_builtin(const char *n) {
	static const char *names[] = {
		"memcpy", "memmove", "memset", "memchr", "strcpy", "strncpy", "strcat", "strncat", "strchr", "strrchr",
		"strstr", "malloc", "calloc", NULL,
		"strlen", "strnlen", NULL,
		"memcmp", "strcmp", "strncmp", "abort", "abs", "labs", "printf", "sprintf", "snprintf", "puts",
		"putchar", "free", NULL
	};
	int k = 2;
	for (int i = 0; k < 5; i++) {
		if (!names[i]) { k++; continue; }
		if (!strcmp(n, names[i])) return k == 4 ? 1 : k;
	}
	return 0;
}

static Type *inline_call(Ctx *ctx, Sym *g) {
	Code *c = &ctx->text;
	Type *fty = g->ty;
	int n = fty->nparams;
	Sym *ps = (Sym *)calloc((size_t)n + 1, sizeof(Sym));
	next(ctx);
	int i = 0;
	for (Param *p = fty->params; p; p = p->next, i++) {
		if (i) expect(ctx, T_COMMA);
		if (ctx->tok.kind == T_RP) error_at(ctx, "wrong number of arguments (expected %d, got %d)", n, i);
		Type *pt = p->ty;
		size_t pos = Cpos(c);
		int sp = ctx->sp, fx = ctx->sidefx, used = 0;
		for (int b = 0; b < g->nbody && !used; b++) used = g->body[b].kind == T_IDENT && g->body[b].str == p->name;
		int off = alloc_local(ctx, pt->size > 0 ? pt->size : 8, pt->align > 0 ? pt->align : 8);
		emit_lea_rbp(c, RAX, off);
		gen_push(ctx);
		Type *at = rval(ctx, parse_assign(ctx));
		long long kv;
		ps[i].name = p->name;
		ps[i].ty = pt;
		if (p->name && !used && fx == ctx->sidefx) {
			drop_to(ctx, pos, sp);
			ps[i].kind = SYM_ENUMC;
			ps[i].val = 0;
			continue;
		}
		if ((is_integer(pt) || is_ptr(pt)) && (is_integer(at) || is_ptr(at)) && p->name && !param_written(g, p->name) && expr_const(ctx, &kv)) {
			drop_to(ctx, pos, sp);
			ps[i].kind = SYM_ENUMC;
			ps[i].val = const_cast_val(kv, pt);
			continue;
		}
		ps[i].kind = SYM_VAR;
		ps[i].stack_off = off;
		if (pt->kind == TY_STRUCT) {
			gen_pop(ctx, RDI);
			gen_struct_copy_to_rdi(ctx, pt->size);
		} else {
			if (is_scalar(pt) && is_scalar(at)) gen_cast(ctx, at, pt);
			gen_pop(ctx, RCX);
			emit_store_type(c, pt);
		}
	}
	if (i != n) error_at(ctx, "wrong number of arguments (expected %d, got %d)", n, i);
	if (ctx->tok.kind == T_COMMA) error_at(ctx, "too many arguments to '%s'", g->name);
	expect(ctx, T_RP);
	Inl in;
	memset(&in, 0, sizeof(in));
	in.fn = g;
	in.ret = fty->base;
	in.id = ++ctx->inl_count;
	in.dead_base = ctx->dead;
	in.up = ctx->inl;
	if (in.ret->kind == TY_STRUCT || in.ret->kind == TY_COMPLEX) in.tmp = alloc_local(ctx, in.ret->size, in.ret->align);
	Sym *scope = ctx->scope;
	Tag *tags = ctx->tags;
	LoopCtx *loops = ctx->loops.top;
	int cond_depth = ctx->cond_depth;
	while (ctx->scope && ctx->scope->depth > 0) ctx->scope = ctx->scope->next;
	while (ctx->tags && ctx->tags->depth > 0) ctx->tags = ctx->tags->next;
	ctx->loops.top = NULL;
	ctx->inl = &in;
	ctx->inl_depth++;
	enter_scope(ctx);
	for (i = 0; i < n; i++) {
		if (!ps[i].name) continue;
		Sym *s = push_sym(ctx, ps[i].name, (SymKind)ps[i].kind, ps[i].ty);
		s->bound = 1;
		s->is_local = ps[i].kind == SYM_VAR;
		s->stack_off = ps[i].stack_off;
		s->val = ps[i].val;
	}
	free(ps);
	TokVec v;
	v.v = g->body;
	v.n = v.cap = g->nbody;
	replay(ctx, &v);
	parse_stmt(ctx);
	leave_scope(ctx);
	if (in.rets.n && in.rets.v[in.rets.n - 1] + 4 == Cpos(c)) {
		c->b.len -= 5;
		in.rets.n--;
	}
	if (in.rets.n) ctx->k_end = (size_t)-1;
	for (i = 0; i < in.rets.n; i++) patch_jmp_to(&c->b, in.rets.v[i], Cpos(c));
	ctx->k_ice = 0;
	free(in.rets.v);
	ctx->inl_depth--;
	ctx->jumped = 0;
	ctx->inl = in.up;
	ctx->scope = scope;
	ctx->tags = tags;
	ctx->loops.top = loops;
	ctx->cond_depth = cond_depth;
	ctx->lv = 0;
	ctx->fn_lea_sym = NULL;
	return in.ret;
}

static Type *parse_primary(Ctx *ctx) {
	Code *c = &ctx->text;
	ctx->lv = 0;
	if (ctx->tok.kind == T_GENERIC) return parse_generic(ctx);
	if (ctx->tok.kind == T_NUM) {
		if (ctx->tok.badnum) error_at(ctx, "invalid number literal");
		Type *ty = ctx->tok.is_long > 1 ? (ctx->tok.is_unsigned ? ty_ullong : ty_llong)
			: ctx->tok.is_long ? (ctx->tok.is_unsigned ? ty_ulong : ty_long)
			: (ctx->tok.is_unsigned ? ty_uint : ty_int);
		emit_const(ctx, ctx->tok.val);
		next(ctx);
		return ty;
	}
	if (ctx->tok.kind == T_FNUM) {
		if (ctx->tok.badnum) error_at(ctx, "invalid floating constant");
		if (ctx->no_float) error_at(ctx, "floating point is disabled by -mgeneral-regs-only");
		if (ctx->tok.imag) {
			Type *bt = ctx->tok.is_long ? ty_ldouble : ctx->tok.is_float ? ty_float : ty_double;
			uint8_t b[32];
			memset(b, 0, sizeof(b));
			if (bt == ty_ldouble) ld_tok(&ctx->tok, b + 16);
			else {
				uint64_t bits = float_bits(ctx->tok.fval, bt);
				memcpy(b + bt->size, &bits, (size_t)bt->size);
			}
			emit_lea_rodata(ctx, RAX, add_rodata(ctx, b, (size_t)(2 * bt->size)));
			next(ctx);
			ctx->lv = 0;
			return complex_of(bt);
		}
		if (ctx->tok.is_long) {
			uint8_t b[16];
			memset(b, 0, sizeof(b));
			ld_tok(&ctx->tok, b);
			emit_lea_rodata(ctx, RAX, add_rodata(ctx, b, 16));
			next(ctx);
			ctx->lv = 0;
			return ty_ldouble;
		}
		Type *ty = ctx->tok.is_float ? ty_float : ty_double;
		uint64_t bits = float_bits(ctx->tok.fval, ty);
		if (bits <= 0xFFFFFFFFULL) emit_mov_rax_imm(c, (long long)bits);
		else emit_mov_rax_imm64(c, bits);
		next(ctx);
		return ty;
	}
	if (ctx->tok.kind == T_STR) {
		Buf s; buf_init(&s);
		int w = read_strs(ctx, &s);
		long off = add_rodata(ctx, s.data, s.len);
		emit_lea_rodata(ctx, RAX, off);
		int n = (int)s.len / wide_size(w);
		free(s.data);
		ctx->lv = 1;
		return array_of(wide_type(w), n);
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
		if (name == id_va_end || name == id_va_copy) return parse_builtin_va_end(ctx, name == id_va_copy);
		if (!strcmp(name, "__builtin_alloca") && peek(ctx)->kind == T_LP && !lookup(ctx, name)) {
			if (!ctx->fn) error_at(ctx, "alloca outside a function");
			next(ctx);
			next(ctx);
			Type *nt = rval(ctx, parse_assign(ctx));
			expect(ctx, T_RP);
			if (!is_integer(nt)) error_at(ctx, "alloca needs an integer size");
			emit_add_rax_imm(c, 15);
			C8(c,0x48); C8(c,0x83); C8(c,0xE0); C8(c,0xF0);
			emit_mov_reg_reg(c, RSI, RSP);
			C8(c,0x48); C8(c,0x29); C8(c,0xC4);
			emit_mov_reg_reg(c, RDI, RSP);
			emit_mov_ecx_imm(c, (uint32_t)ctx->sp);
			C8(c,0xF3); C8(c,0x48); C8(c,0xA5);
			emit_mov_reg_reg(c, RAX, RDI);
			ctx->sidefx++;
			ctx->lv = 0;
			return ptr_to(ty_void);
		}
		if (name == id_offsetof && ctx->fn) {
			ctx->ofs_rt = 1;
			ctx->ofs_have = 0;
			long long off = builtin_const(ctx, name);
			ctx->ofs_rt = 0;
			if (ctx->ofs_have) emit_add_rax_imm(c, off);
			else emit_const(ctx, off);
			ctx->lv = 0;
			return ty_ulong;
		}
		if (is_builtin_const(name)) {
			emit_const(ctx, builtin_const(ctx, name));
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
		if (name == id_return_address || name == id_frame_address) {
			next(ctx);
			expect(ctx, T_LP);
			long long n = const_expr(ctx);
			expect(ctx, T_RP);
			if (n < 0) error_at(ctx, "the level of %s must not be negative", name);
			emit_mov_reg_reg(c, RAX, RBP);
			for (long long i = 0; i < n; i++) emit_load(c, 8, 0);
			if (name == id_return_address) { C8(c,0x48); C8(c,0x8B); C8(c,0x40); C8(c,0x08); }
			return ptr_to(ty_void);
		}
		if ((!strcmp(name, "__builtin_prefetch") || !strcmp(name, "__builtin_extract_return_addr")) && !lookup(ctx, name)) {
			int pf = name[10] == 'p';
			next(ctx);
			expect(ctx, T_LP);
			Type *at = rval(ctx, parse_assign(ctx));
			while (consume(ctx, T_COMMA)) {
				gen_push(ctx);
				rval(ctx, parse_assign(ctx));
				gen_pop(ctx, RAX);
			}
			expect(ctx, T_RP);
			ctx->lv = 0;
			return pf ? ty_void : at;
		}
		if (!strcmp(name, "__builtin_object_size") && !lookup(ctx, name)) {
			next(ctx);
			expect(ctx, T_LP);
			Mark m = mark_code(ctx);
			rval(ctx, parse_assign(ctx));
			drop_code(ctx, m);
			expect(ctx, T_COMMA);
			long long kind = const_expr(ctx);
			expect(ctx, T_RP);
			emit_const(ctx, kind < 2 ? -1 : 0);
			return ty_ulong;
		}
		long long br;
		Type *bt;
		if (name[0] == '_' && name[1] == '_' && !lookup(ctx, name) && peek(ctx)->kind == T_LP && bit_builtin(name, 1, &br, &bt)) {
			next(ctx);
			next(ctx);
			Type *at = rval(ctx, parse_assign(ctx));
			expect(ctx, T_RP);
			if (!is_integer(at)) error_at(ctx, "%s needs an integer argument", name);
			long long kv;
			if (expr_const(ctx, &kv) && bit_builtin(name, kv, &br, &bt)) {
				drop_to(ctx, ctx->k_start, ctx->sp);
				emit_const(ctx, br);
				return bt;
			}
			const char *b = name + 10;
			int w64 = b[strlen(b) - 1] == 'l';
			if (!strncmp(b, "bswap", 5)) {
				w64 = b[5] == '6';
				if (b[5] == '1') { C8(c,0x66); C8(c,0xC1); C8(c,0xC0); C8(c,0x08); C8(c,0x0F); C8(c,0xB7); C8(c,0xC0); }
				else { if (w64) C8(c,0x48); C8(c,0x0F); C8(c,0xC8); }
				ctx->lv = 0;
				return bt;
			}
			if (!w64) { C8(c,0x89); C8(c,0xC0); }
			if (!strncmp(b, "ctz", 3)) { C8(c,0x48); C8(c,0x0F); C8(c,0xBC); C8(c,0xC0); }
			else if (!strncmp(b, "clz", 3)) {
				C8(c,0x48); C8(c,0x0F); C8(c,0xBD); C8(c,0xC0);
				C8(c,0x83); C8(c,0xF0); C8(c,w64 ? 63 : 31);
			} else if (!strncmp(b, "ffs", 3)) {
				C8(c,0x48); C8(c,0x0F); C8(c,0xBC); C8(c,0xC8);
				C8(c,0xBA); C32(c,0xFFFFFFFFu);
				C8(c,0x0F); C8(c,0x44); C8(c,0xCA);
				C8(c,0x8D); C8(c,0x41); C8(c,0x01);
			} else error_at(ctx, "%s is only supported on constants", name);
			ctx->lv = 0;
			return ty_int;
		}
		if (name[0] == '_' && name[1] == '_' && !lookup(ctx, name))
			for (int i = 0; mach_ids[i]; i++)
				if (name == mach_ids[i]) return parse_builtin_mach(ctx, &mach_builtins[i]);
		if ((name == id_func || name == id_function) && ctx->fn) {
			const char *fname = ctx->inl ? ctx->inl->fn->name : ctx->fn->sym->name;
			long off = add_rodata(ctx, fname, strlen(fname) + 1);
			emit_lea_rodata(ctx, RAX, off);
			next(ctx);
			ctx->lv = 1;
			return array_of(ty_char, (int)strlen(fname) + 1);
		}
		Sym *s = lookup(ctx, name);
		int bi = 0;
		if (!s && !strncmp(name, "__builtin_", 10) && (bi = libc_builtin(name + 10))) {
			name = intern(name + 10);
			s = lookup(ctx, name);
		}
		next(ctx);
		if (!s) {
			if (ctx->tok.kind != T_LP) error_at(ctx, "undeclared variable '%s'", name);
			if (!bi) warn_at(ctx, "implicit declaration of function '%s'", name);
			Type *ft = func_type(bi == 2 ? ptr_to(ty_void) : bi == 3 ? ty_ulong : ty_int);
			ft->oldstyle = 1;
			Sym *g = get_global(ctx, name, SYM_FUNC, ft, 0);
			int d = ctx->depth;
			ctx->depth = 0;
			bind_global(ctx, g);
			ctx->depth = d;
			s = g;
		}
		if (s->kind == SYM_ENUMC) { emit_const(ctx, s->val); if (s->bound) ctx->k_ice = 0; return s->ty; }
		if (s->kind == SYM_TYPEDEF) error_at(ctx, "unexpected type name '%s'", name);
		if (s->kind == SYM_FUNC) {
			Sym *g = s->global ? s->global : s;
			if ((g->always || g->small) && g->body && ctx->fn && ctx->tok.kind == T_LP && ctx->inl_depth < 16 && !g->ty->variadic && !g->ty->oldstyle)
				return inline_call(ctx, g);
			emit_lea_sym(ctx, g);
			ctx->fn_lea_end = Cpos(c);
			ctx->fn_lea_sym = g;
			return g->ty;
		}
		Sym *gs = s->global ? s->global : s;
		if (s->is_local && s->indirect) { emit_load_rbp(c, RAX, s->stack_off); ctx->lv = 1; return s->ty; }
		if (s->gregvar) { emit_mov_reg_reg(c, RAX, s->gregvar - 1); ctx->lv = 0; return s->ty; }
		ctx->lv_start = Cpos(c);
		ctx->lv_local = s->is_local ? s : NULL;
		if (s->is_local) emit_lea_rbp(c, RAX, s->stack_off);
		else if (gs->tls) emit_tls_addr(ctx, gs);
		else emit_lea_sym(ctx, gs);
		ctx->lv_end = Cpos(c);
		ctx->lv_sym = s->is_local || gs->tls ? NULL : gs;
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
	ctx->sidefx++;
	if (!ctx->lv) error_at(ctx, "lvalue required as increment/decrement operand");
	kv_forget(ctx, 0);
	if (!is_scalar(ty)) error_at(ctx, "invalid operand to increment/decrement");
	ctx->lv = 0;
	int step = is_ptr(ty) ? ty->base->size : 1;
	gen_push(ctx);
	emit_load_type(c, ty);
	if (post && ty->kind == TY_LDOUBLE) { emit_x87(c, 0xDB, 5, RAX, 0); ld_store_temp(ctx); }
	if (post) gen_push(ctx);
	if (ty->kind == TY_BOOL) emit_mov_rax_imm(c, delta > 0 ? 1 : 0);
	else if (is_ptr(ty) && ty->base->vla) {
		C8(c,0x48); C8(c,(uint8_t)(delta > 0 ? 0x03 : 0x2B)); enc_mem(c, RAX, RBP, -1, 1, ty->base->vla, 1, 0);
	} else if (ty->kind == TY_LDOUBLE) {
		emit_mov_reg_reg(c, RCX, RAX);
		C8(c,0xD9); C8(c,0xE8);
		ld_store_temp(ctx);
		gen_binop(ctx, delta > 0 ? T_PLUS : T_MINUS, ty, ty);
	} else if (is_flonum(ty)) {
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

static Type *postfix_ops(Ctx *ctx, Type *ty);

static Type *parse_postfix(Ctx *ctx) { return postfix_ops(ctx, parse_primary(ctx)); }

static Type *postfix_ops(Ctx *ctx, Type *ty) {
	Code *c = &ctx->text;
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
				scale_by(c, RAX, ty->base);
				ty = ty->base;
			} else if (is_integer(ty) && is_ptr(it)) {
				scale_by(c, RCX, it->base);
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

static void c_part(Ctx *ctx, int slot, Type *t, int p);
static void c_neg(Ctx *ctx, Type *bt);
static void c_store(Ctx *ctx, int tmp, Type *bt, int p);

static Type *parse_unary(Ctx *ctx) {
	Code *c = &ctx->text;
	int k = ctx->tok.kind;
	if (k == T_PLUS || k == T_MINUS || k == T_TILDE) {
		next(ctx);
		Type *ty = rval(ctx, parse_cast(ctx));
		if (is_complex(ty)) {
			if (k == T_PLUS) return ty;
			int tmp = alloc_local(ctx, ty->size, ty->align);
			gen_push(ctx);
			int slot = ctx->sp;
			for (int p = 0; p < 2; p++) {
				c_part(ctx, slot, ty, p);
				if (k == T_MINUS || p) c_neg(ctx, ty->base);
				c_store(ctx, tmp, ty->base, p);
			}
			gen_pop(ctx, RCX);
			emit_lea_rbp(c, RAX, tmp);
			return ty;
		}
		if (is_flonum(ty) && k != T_TILDE) {
			if (k == T_MINUS) {
				if (ty->kind == TY_LDOUBLE) { emit_x87(c, 0xDB, 5, RAX, 0); C8(c,0xD9); C8(c,0xE0); ld_store_temp(ctx); }
				else if (ty->kind == TY_DOUBLE) { emit_mov_rcx_imm64(c, 0x8000000000000000ULL); emit_xor_rax_rcx(c); }
				else { C8(c,0x35); C32(c,0x80000000u); }
			}
			return ty;
		}
		if (!is_integer(ty)) error_at(ctx, "invalid operand to unary operator");
		ty = int_promote(ty);
		long long kv;
		if (expr_const(ctx, &kv)) {
			int ice = ctx->k_ice;
			drop_to(ctx, ctx->k_start, ctx->sp);
			emit_const(ctx, const_cast_val(k == T_MINUS ? (long long)(0 - (unsigned long long)kv) : k == T_TILDE ? ~kv : kv, ty));
			ctx->k_ice = ice;
			return ty;
		}
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
		Type *ty = rval(ctx, parse_cast(ctx));
		long long kv;
		if (!is_flonum(ty) && expr_const(ctx, &kv)) {
			int ice = ctx->k_ice;
			drop_to(ctx, ctx->k_start, ctx->sp);
			emit_const(ctx, !kv);
			ctx->k_ice = ice;
			return ty_int;
		}
		gen_truth(ctx, ty);
		emit_logical_not(c);
		return ty_int;
	}
	if (k == T_AMP) {
		next(ctx);
		Type *ty = parse_cast(ctx);
		if (ty->kind == TY_FUNC) { ctx->lv = 0; ctx->fn_lea_sym = NULL; return ptr_to(ty); }
		if (!ctx->lv) error_at(ctx, "lvalue required as unary '&' operand");
		if (ty->bit_width) error_at(ctx, "cannot take address of bit-field");
		kv_forget(ctx, 1);
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
	if (k == T_IDENT && (!strcmp(ctx->tok.str, "__real__") || !strcmp(ctx->tok.str, "__imag__") || !strcmp(ctx->tok.str, "__real") || !strcmp(ctx->tok.str, "__imag"))) {
		int im = ctx->tok.str[2] == 'i';
		next(ctx);
		Type *ty = parse_cast(ctx);
		if (is_complex(ty)) {
			if (!ctx->lv) ctx->lv = 1;
			if (im) emit_add_rax_imm(c, ty->base->size);
			return ty->base;
		}
		ty = rval(ctx, ty);
		if (!is_arith(ty)) error_at(ctx, "invalid operand to __real__ or __imag__");
		if (im) {
			emit_const(ctx, 0);
			gen_cast(ctx, ty_int, ty);
		}
		return ty;
	}
	if (k == T_SIZEOF || k == T_ALIGNOF) {
		next(ctx);
		ctx->sizeof_vla = 0;
		long long n = k == T_SIZEOF ? sizeof_operand(ctx) : alignof_operand(ctx);
		if (k == T_SIZEOF && ctx->sizeof_vla) emit_load_rbp(&ctx->text, RAX, ctx->sizeof_vla);
		else emit_const(ctx, n);
		ctx->sizeof_vla = 0;
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
			return postfix_ops(ctx, t);
		}
		if (t->kind == TY_VOID && ctx->fold && ctx->fn) {
			Mark m = mark_code(ctx);
			int fx = ctx->sidefx;
			rval(ctx, parse_cast(ctx));
			if (fx == ctx->sidefx) drop_code(ctx, m);
			ctx->lv = 0;
			return t;
		}
		Type *from = rval(ctx, parse_cast(ctx));
		if (t->kind == TY_STRUCT) {
			if (!same_type(from, t)) error_at(ctx, "cast to struct type");
			ctx->lv = 0;
			return t;
		}
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
	if (t->kind == TY_LDOUBLE) {
		int cmp = op == T_GT || op == T_GEQ || op == T_LT || op == T_LEQ || op == T_EQ || op == T_NEQ;
		if (cmp) {
			int sw = op == T_LT || op == T_LEQ;
			emit_x87(c, 0xDB, 5, sw ? RCX : RAX, 0);
			emit_x87(c, 0xDB, 5, sw ? RAX : RCX, 0);
			C8(c,0xDF); C8(c,0xE9);
			C8(c,0xDD); C8(c,0xD8);
			ld_flags_setcc(c, op == T_LT ? T_GT : op == T_LEQ ? T_GEQ : op);
			return ty_int;
		}
		emit_x87(c, 0xDB, 5, RCX, 0);
		emit_x87(c, 0xDB, 5, RAX, 0);
		switch (op) {
		case T_PLUS: C8(c,0xDE); C8(c,0xC1); break;
		case T_MINUS: C8(c,0xDE); C8(c,0xE9); break;
		case T_MUL: C8(c,0xDE); C8(c,0xC9); break;
		case T_DIV: C8(c,0xDE); C8(c,0xF9); break;
		default: error_at(ctx, "invalid operands to binary operator");
		}
		ld_store_temp(ctx);
		return t;
	}
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

static Type *gen_binop(Ctx *ctx, int op, Type *lt, Type *rt);

static void c_part(Ctx *ctx, int slot, Type *t, int p) {
	Code *c = &ctx->text;
	emit_load_rsp(c, RAX, (ctx->sp - slot) * 8);
	if (t->kind == TY_COMPLEX) {
		if (p) emit_add_rax_imm(c, t->base->size);
		emit_load_type(c, t->base);
	}
}

static void c_neg(Ctx *ctx, Type *bt) {
	Code *c = &ctx->text;
	if (bt->kind == TY_LDOUBLE) { emit_x87(c, 0xDB, 5, RAX, 0); C8(c,0xD9); C8(c,0xE0); ld_store_temp(ctx); }
	else if (bt->kind == TY_DOUBLE) { emit_mov_rcx_imm64(c, 0x8000000000000000ULL); emit_xor_rax_rcx(c); }
	else { C8(c,0x35); C32(c,0x80000000u); }
}

static void c_op(Ctx *ctx, int op, int ls, Type *lt, int lp, int rs, Type *rtt, int rp, Type *bt) {
	c_part(ctx, ls, lt, lp);
	gen_push(ctx);
	c_part(ctx, rs, rtt, rp);
	gen_pop(ctx, RCX);
	gen_binop(ctx, op, bt, bt);
}

static void c_store(Ctx *ctx, int tmp, Type *bt, int p) {
	Code *c = &ctx->text;
	emit_lea_rbp(c, RCX, tmp + p * bt->size);
	emit_store_type(c, bt);
}

static void c_zero(Ctx *ctx, int tmp, Type *bt, int p) {
	Code *c = &ctx->text;
	emit_xor_eax_eax(c);
	emit_store_rbp(c, RAX, tmp + p * bt->size);
	if (bt->size > 8) emit_store_rbp(c, RAX, tmp + p * bt->size + 8);
}

static void gen_ccast(Ctx *ctx, Type *from, Type *to) {
	Code *c = &ctx->text;
	if (from->kind == TY_COMPLEX && to->kind == TY_COMPLEX && from->base->kind == to->base->kind) return;
	if (to->kind == TY_COMPLEX) {
		int tmp = alloc_local(ctx, to->size, to->align);
		gen_push(ctx);
		int slot = ctx->sp;
		for (int p = 0; p < 2; p++) {
			if (from->kind != TY_COMPLEX && p) { c_zero(ctx, tmp, to->base, 1); break; }
			if (from->kind == TY_COMPLEX) c_part(ctx, slot, from, p);
			else emit_load_rsp(c, RAX, (ctx->sp - slot) * 8);
			gen_cast(ctx, from->kind == TY_COMPLEX ? from->base : from, to->base);
			c_store(ctx, tmp, to->base, p);
		}
		gen_pop(ctx, RCX);
		emit_lea_rbp(c, RAX, tmp);
		return;
	}
	if (to->kind == TY_BOOL) {
		gen_push(ctx);
		int slot = ctx->sp;
		c_part(ctx, slot, from, 0);
		gen_cast(ctx, from->base, ty_bool);
		gen_push(ctx);
		c_part(ctx, slot, from, 1);
		gen_cast(ctx, from->base, ty_bool);
		gen_pop(ctx, RCX);
		C8(c,0x09); C8(c,0xC8);
		gen_pop(ctx, RCX);
		return;
	}
	emit_load_type(c, from->base);
	gen_cast(ctx, from->base, to);
}

static Type *gen_cbinop(Ctx *ctx, int op, Type *lt, Type *rt) {
	Code *c = &ctx->text;
	Type *t = arith_conv(lt, rt), *bt = t->base;
	int lc = lt->kind == TY_COMPLEX, rc = rt->kind == TY_COMPLEX;
	gen_push(ctx);
	int rs = ctx->sp;
	emit_mov_reg_reg(c, RAX, RCX);
	gen_cast(ctx, lt, lc ? t : bt);
	gen_push(ctx);
	int ls = ctx->sp;
	emit_load_rsp(c, RAX, (ctx->sp - rs) * 8);
	gen_cast(ctx, rt, rc ? t : bt);
	emit_store_rsp_rax(c, (ctx->sp - rs) * 8);
	Type *L = lc ? t : bt, *R = rc ? t : bt;
	if (op == T_EQ || op == T_NEQ) {
		c_op(ctx, T_EQ, ls, L, 0, rs, R, 0, bt);
		gen_push(ctx);
		if (lc && rc) c_op(ctx, T_EQ, ls, L, 1, rs, R, 1, bt);
		else {
			c_part(ctx, lc ? ls : rs, t, 1);
			gen_cast(ctx, bt, ty_bool);
			C8(c,0x83); C8(c,0xF0); C8(c,0x01);
		}
		gen_pop(ctx, RCX);
		C8(c,0x21); C8(c,0xC8);
		if (op == T_NEQ) { C8(c,0x83); C8(c,0xF0); C8(c,0x01); }
		gen_pop(ctx, RCX);
		gen_pop(ctx, RCX);
		return ty_int;
	}
	int tmp = alloc_local(ctx, t->size, t->align);
	if (op == T_PLUS || op == T_MINUS) {
		c_op(ctx, op, ls, L, 0, rs, R, 0, bt);
		c_store(ctx, tmp, bt, 0);
		if (lc && rc) c_op(ctx, op, ls, L, 1, rs, R, 1, bt);
		else if (lc) c_part(ctx, ls, L, 1);
		else { c_part(ctx, rs, R, 1); if (op == T_MINUS) c_neg(ctx, bt); }
		c_store(ctx, tmp, bt, 1);
	} else if (op == T_MUL) {
		if (lc && rc) {
			c_op(ctx, T_MUL, ls, L, 0, rs, R, 0, bt);
			gen_push(ctx);
			c_op(ctx, T_MUL, ls, L, 1, rs, R, 1, bt);
			gen_pop(ctx, RCX);
			gen_binop(ctx, T_MINUS, bt, bt);
			c_store(ctx, tmp, bt, 0);
			c_op(ctx, T_MUL, ls, L, 0, rs, R, 1, bt);
			gen_push(ctx);
			c_op(ctx, T_MUL, ls, L, 1, rs, R, 0, bt);
			gen_pop(ctx, RCX);
			gen_binop(ctx, T_PLUS, bt, bt);
			c_store(ctx, tmp, bt, 1);
		} else {
			for (int p = 0; p < 2; p++) {
				c_op(ctx, T_MUL, ls, L, lc ? p : 0, rs, R, rc ? p : 0, bt);
				c_store(ctx, tmp, bt, p);
			}
		}
	} else if (op == T_DIV) {
		if (!rc) {
			for (int p = 0; p < 2; p++) {
				c_op(ctx, T_DIV, ls, L, p, rs, R, 0, bt);
				c_store(ctx, tmp, bt, p);
			}
		} else {
			int den = alloc_local(ctx, 16, 16);
			c_op(ctx, T_MUL, rs, R, 0, rs, R, 0, bt);
			gen_push(ctx);
			c_op(ctx, T_MUL, rs, R, 1, rs, R, 1, bt);
			gen_pop(ctx, RCX);
			gen_binop(ctx, T_PLUS, bt, bt);
			emit_lea_rbp(c, RCX, den);
			emit_store_type(c, bt);
			for (int p = 0; p < 2; p++) {
				c_op(ctx, T_MUL, ls, L, lc ? p : 0, rs, R, 0, bt);
				gen_push(ctx);
				if (lc) {
					c_op(ctx, T_MUL, ls, L, 1 - p, rs, R, 1, bt);
					if (p) c_neg(ctx, bt);
					gen_pop(ctx, RCX);
					gen_binop(ctx, T_PLUS, bt, bt);
				} else {
					gen_pop(ctx, RCX);
					emit_mov_reg_reg(c, RAX, RCX);
					if (p) {
						c_op(ctx, T_MUL, ls, L, 0, rs, R, 1, bt);
						c_neg(ctx, bt);
					}
				}
				gen_push(ctx);
				emit_lea_rbp(c, RAX, den);
				emit_load_type(c, bt);
				gen_pop(ctx, RCX);
				gen_binop(ctx, T_DIV, bt, bt);
				c_store(ctx, tmp, bt, p);
			}
		}
	} else error_at(ctx, "invalid operands to binary operator");
	gen_pop(ctx, RCX);
	gen_pop(ctx, RCX);
	emit_lea_rbp(c, RAX, tmp);
	return t;
}

static Type *gen_binop(Ctx *ctx, int op, Type *lt, Type *rt) {
	Code *c = &ctx->text;
	if (is_complex(lt) || is_complex(rt)) return gen_cbinop(ctx, op, lt, rt);
	if (is_flonum(lt) || is_flonum(rt)) return gen_fbinop(ctx, op, lt, rt);
	if (op == T_PLUS || op == T_MINUS) {
		if (is_ptr(lt) && is_integer(rt)) {
			scale_by(c, RAX, lt->base);
			if (op == T_PLUS) emit_add_rax_rcx(c);
			else { emit_xchg_rax_rcx(c); emit_sub_rax_rcx(c); }
			return lt;
		}
		if (is_integer(lt) && is_ptr(rt) && op == T_PLUS) {
			scale_by(c, RCX, rt->base);
			emit_add_rax_rcx(c);
			return rt;
		}
		if (is_ptr(lt) && is_ptr(rt) && op == T_MINUS) {
			emit_xchg_rax_rcx(c);
			emit_sub_rax_rcx(c);
			int sz = lt->base->size;
			if (lt->base->vla) { emit_load_rbp(c, RCX, lt->base->vla); emit_cqo(c); emit_idiv_rcx(c); }
			else if (sz > 1) { emit_mov_ecx_imm(c, (uint32_t)sz); emit_cqo(c); emit_idiv_rcx(c); }
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

static int fold_binop(int op, Type *lt, Type *rt, long long a, long long b, long long *res, Type **rty) {
	if (!is_integer(lt) || !is_integer(rt)) return 0;
	if (op == T_EQ || op == T_NEQ || op == T_LT || op == T_GT || op == T_LEQ || op == T_GEQ) {
		Type *ct = arith_conv(lt, rt);
		a = const_cast_val(a, ct);
		b = const_cast_val(b, ct);
		int u = ct->is_unsigned;
		unsigned long long ua = (unsigned long long)a, ub = (unsigned long long)b;
		int r = op == T_EQ ? a == b : op == T_NEQ ? a != b :
			op == T_LT ? (u ? ua < ub : a < b) : op == T_GT ? (u ? ua > ub : a > b) :
			op == T_LEQ ? (u ? ua <= ub : a <= b) : (u ? ua >= ub : a >= b);
		*res = r;
		*rty = ty_int;
		return 1;
	}
	if (op == T_SHL || op == T_SHR) {
		Type *t = int_promote(lt);
		if (b < 0 || b >= t->size * 8) return 0;
		a = const_cast_val(a, t);
		if (op == T_SHL) *res = const_cast_val((long long)((unsigned long long)a << b), t);
		else if (t->is_unsigned) *res = const_cast_val((long long)((t->size == 4 ? (unsigned long long)(uint32_t)a : (unsigned long long)a) >> b), t);
		else *res = a >> b;
		*rty = t;
		return 1;
	}
	Type *t = arith_conv(lt, rt);
	a = const_cast_val(a, t);
	b = const_cast_val(b, t);
	unsigned long long ua = (unsigned long long)a, ub = (unsigned long long)b, r;
	if (t->size == 4 && t->is_unsigned) { ua = (uint32_t)ua; ub = (uint32_t)ub; }
	switch (op) {
	case T_PLUS: r = ua + ub; break;
	case T_MINUS: r = ua - ub; break;
	case T_MUL: r = ua * ub; break;
	case T_DIV: case T_MOD:
		if (!b) return 0;
		if (t->is_unsigned) r = op == T_DIV ? ua / ub : ua % ub;
		else {
			if (b == -1 && a == (t->size == 4 ? (long long)INT32_MIN : (long long)INT64_MIN)) return 0;
			r = (unsigned long long)(op == T_DIV ? a / b : a % b);
		}
		break;
	case T_AMP: r = ua & ub; break;
	case T_PIPE: r = ua | ub; break;
	case T_CARET: r = ua ^ ub; break;
	default: return 0;
	}
	*res = const_cast_val((long long)r, t);
	*rty = t;
	return 1;
}

static Type *parse_binary(Ctx *ctx, int minprec) {
	Code *c = &ctx->text;
	size_t m0 = Cpos(c);
	int sp0 = ctx->sp, fx0 = ctx->sidefx;
	Type *lt = parse_cast(ctx);
	for (;;) {
		int k = ctx->tok.kind;
		int p = binop_prec(k);
		if (p == 0 || p < minprec) return lt;
		next(ctx);
		lt = rval(ctx, lt);
		long long lv = 0;
		int lc = !is_flonum(lt) && expr_const(ctx, &lv);
		int lice = lc && ctx->k_ice;
		size_t ls = ctx->k_start;
		if (lc && (k == T_AND || k == T_OR)) {
			drop_to(ctx, ls, ctx->sp);
			if (k == T_AND ? !lv : lv != 0) {
				dead_xbegin(ctx);
				rval(ctx, parse_binary(ctx, p + 1));
				dead_xend(ctx);
				emit_const(ctx, k == T_OR);
				ctx->k_ice = lice;
			} else {
				size_t rs = Cpos(c);
				Type *rt = rval(ctx, parse_binary(ctx, p + 1));
				long long rv;
				if (!is_flonum(rt) && expr_const(ctx, &rv)) { int rice = ctx->k_ice; drop_to(ctx, rs, ctx->sp); emit_const(ctx, rv != 0); ctx->k_ice = lice && rice; }
				else { gen_truth(ctx, rt); emit_to_bool(c); }
			}
			lt = ty_int;
			continue;
		}
		if (k == T_AND || k == T_OR) {
			size_t lpos = Cpos(c);
			int lsp = ctx->sp;
			gen_truth(ctx, lt);
			emit_test_rax_rax(c);
			size_t sc = k == T_AND ? emit_jz_placeholder(c) : emit_jnz_placeholder(c);
			size_t rs = Cpos(c);
			Type *rt = (ctx->xcond++, xcond_end(ctx, rval(ctx, parse_binary(ctx, p + 1))));
			long long rv;
			if (ctx->fold && !is_flonum(rt) && rs > m0 && expr_const(ctx, &rv) && (k == T_AND ? !rv : rv != 0)) {
				if (fx0 == ctx->sidefx) drop_to(ctx, m0, sp0);
				else drop_to(ctx, lpos, lsp);
				emit_const(ctx, k == T_OR);
				ctx->k_ice = 0;
				lt = ty_int;
				continue;
			}
			gen_truth(ctx, rt);
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
		long long rv, res;
		Type *rty;
		int rc = expr_const(ctx, &rv);
		if (ctx->fold && (k == T_AMP || k == T_MUL) && is_integer(lt) && is_integer(rt) && fx0 == ctx->sidefx &&
			!(lc && rc) && ((rc && !rv) || (lc && !lv))) {
			drop_to(ctx, m0, sp0);
			emit_const(ctx, 0);
			ctx->k_ice = 0;
			lt = arith_conv(lt, rt);
			continue;
		}
		if (lc && expr_const(ctx, &rv) && fold_binop(k, lt, rt, lv, rv, &res, &rty)) {
			int rice = ctx->k_ice;
			drop_to(ctx, ls, ctx->sp - 1);
			emit_const(ctx, res);
			ctx->k_ice = lice && rice;
			lt = rty;
			continue;
		}
		gen_pop(ctx, RCX);
		lt = gen_binop(ctx, k, lt, rt);
	}
}

static int null_const(Ctx *ctx, Type *t) {
	long long v;
	return is_ptr(t) && t->base->kind == TY_VOID && expr_const(ctx, &v) && !v && ctx->k_ice;
}

static Type *parse_cond(Ctx *ctx) {
	Code *c = &ctx->text;
	Type *ty = parse_binary(ctx, 1);
	if (ctx->tok.kind != T_QUESTION) return ty;
	next(ctx);
	ty = rval(ctx, ty);
	long long kv;
	if (consume(ctx, T_COLON)) {
		if (!is_scalar(ty)) error_at(ctx, "scalar required before '?:'");
		if (!is_flonum(ty) && expr_const(ctx, &kv)) {
			if (kv) {
				dead_xbegin(ctx);
				rval(ctx, parse_cond(ctx));
				dead_xend(ctx);
				return ty;
			}
			drop_to(ctx, ctx->k_start, ctx->sp);
			return rval(ctx, parse_cond(ctx));
		}
		gen_push(ctx);
		gen_truth(ctx, ty);
		emit_test_rax_rax(c);
		size_t jelse = emit_jz_placeholder(c);
		gen_pop(ctx, RAX);
		size_t jend = emit_jmp_placeholder(c);
		patch_jmp_to(&c->b, jelse, Cpos(c));
		ctx->sp++;
		gen_pop(ctx, RCX);
		Type *b = (ctx->xcond++, xcond_end(ctx, rval(ctx, parse_cond(ctx))));
		ctx->lv = 0;
		Type *t = ty;
		if (is_arith(ty) && is_arith(b)) t = arith_conv(ty, b);
		if (is_arith(t)) gen_cast(ctx, b, t);
		if (is_arith(t) && ty != t) {
			size_t jskip = emit_jmp_placeholder(c);
			patch_jmp_to(&c->b, jend, Cpos(c));
			gen_cast(ctx, ty, t);
			patch_jmp_to(&c->b, jskip, Cpos(c));
		} else patch_jmp_to(&c->b, jend, Cpos(c));
		ctx->k_end = (size_t)-1;
		return t;
	}
	if (!is_flonum(ty) && expr_const(ctx, &kv)) {
		drop_to(ctx, ctx->k_start, ctx->sp);
		if (!kv) dead_xbegin(ctx);
		Type *a = rval(ctx, parse_expr(ctx));
		int an = null_const(ctx, a);
		if (!kv) dead_xend(ctx);
		expect(ctx, T_COLON);
		if (kv) dead_xbegin(ctx);
		Type *b = rval(ctx, parse_cond(ctx));
		if (kv) dead_xend(ctx);
		ctx->lv = 0;
		Type *t = a;
		if (is_arith(a) && is_arith(b)) t = arith_conv(a, b);
		else if (is_ptr(a) && is_ptr(b)) t = an ? b : a;
		else if (is_ptr(a)) t = a;
		else if (is_ptr(b)) t = b;
		if (is_arith(t)) gen_cast(ctx, kv ? a : b, t);
		return t;
	}
	gen_truth(ctx, ty);
	emit_test_rax_rax(c);
	size_t jelse = emit_jz_placeholder(c);
	Type *a = (ctx->xcond++, xcond_end(ctx, rval(ctx, parse_expr(ctx))));
	int an = null_const(ctx, a);
	expect(ctx, T_COLON);
	size_t jend = emit_jmp_placeholder(c);
	patch_jmp_to(&c->b, jelse, Cpos(c));
	Type *b = (ctx->xcond++, xcond_end(ctx, rval(ctx, parse_cond(ctx))));
	ctx->lv = 0;
	Type *t = a;
	if (is_arith(a) && is_arith(b)) t = arith_conv(a, b);
	else if (is_ptr(a) && is_ptr(b) && an) t = b;
	else if (is_ptr(a)) t = a;
	else if (is_ptr(b)) t = b;
	if (is_arith(t)) gen_cast(ctx, b, t);
	if (is_arith(t) && a != t) {
		size_t jskip = emit_jmp_placeholder(c);
		patch_jmp_to(&c->b, jend, Cpos(c));
		gen_cast(ctx, a, t);
		patch_jmp_to(&c->b, jskip, Cpos(c));
	} else patch_jmp_to(&c->b, jend, Cpos(c));
	ctx->k_end = (size_t)-1;
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
	if (k == T_ASSIGN || compound_op(k)) ctx->sidefx++;
	Sym *tg = k == T_ASSIGN || compound_op(k) ? tls_lvalue(ctx, lt) : NULL;
	if (tg) {
		int op = compound_op(k);
		Type *vt = lt->origin ? lt->origin : lt;
		next(ctx);
		ctx->lv = 0;
		if (op) {
			emit_tls_access(ctx, tg, lt->size, lt->is_unsigned, 0);
			gen_push(ctx);
		}
		Type *rt = rval(ctx, parse_assign(ctx));
		if (op) {
			gen_pop(ctx, RCX);
			rt = gen_binop(ctx, op, vt, rt);
		}
		gen_cast(ctx, rt, lt);
		emit_tls_access(ctx, tg, lt->size, 0, 1);
		ctx->lv = 0;
		return vt;
	}
	if (k == T_ASSIGN) {
		if (!ctx->lv) error_at(ctx, "lvalue required as left operand of assignment");
		if (lt->kind == TY_ARRAY || lt->kind == TY_FUNC) error_at(ctx, "assignment to array or function");
		Sym *tl = ctx->fold ? simple_local(ctx) : NULL;
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
		long long kv;
		int isk = tl && (is_integer(lt) || is_ptr(lt)) && !lt->bit_width && expr_const(ctx, &kv);
		gen_pop(ctx, RCX);
		emit_store_type(c, lt);
		if (tl && !ctx->dead) {
			tl->kv_valid = isk && !ctx->cond_depth && !ctx->xcond && !tl->addr_taken;
			if (tl->kv_valid) tl->kv = const_cast_val(kv, lt);
		}
		ctx->lv = 0;
		return lt->origin ? lt->origin : lt;
	}
	int op = compound_op(k);
	if (op) {
		if (!ctx->lv) error_at(ctx, "lvalue required as left operand of assignment");
		if (!is_scalar(lt) && !is_complex(lt)) error_at(ctx, "invalid compound assignment");
		Sym *tl = ctx->fold ? simple_local(ctx) : NULL;
		kv_forget(ctx, 0);
		next(ctx);
		ctx->lv = 0;
		gen_push(ctx);
		emit_load_type(c, lt);
		gen_push(ctx);
		Type *rt = rval(ctx, parse_assign(ctx));
		long long rv;
		int zero = tl && is_integer(lt) && !lt->bit_width && (op == T_AMP || op == T_MUL) && is_integer(rt) && expr_const(ctx, &rv) && !rv;
		gen_pop(ctx, RCX);
		Type *vt = lt->origin ? lt->origin : lt;
		Type *res = gen_binop(ctx, op, vt, rt);
		gen_cast(ctx, res, vt);
		gen_pop(ctx, RCX);
		emit_store_type(c, lt);
		if (zero && !ctx->cond_depth && !ctx->xcond && !tl->addr_taken) { tl->kv_valid = 1; tl->kv = 0; }
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
	if (!is_scalar(ty) && !is_complex(ty)) error_at(ctx, "scalar required in condition");
	gen_truth(ctx, ty);
}

static void emit_lea_init(Ctx *ctx, int reg, int rel) {
	Code *c = &ctx->text;
	emit_lea_rbp(c, reg, rel);
	patches_push(&ctx->init_patches, Cpos(c) - 4);
}

static int is_string_init(Ctx *ctx, Type *ty) {
	if (ty->kind != TY_ARRAY || !is_integer(ty->base)) return 0;
	Tok *t = ctx->tok.kind == T_STR ? &ctx->tok : ctx->tok.kind == T_LB && peek(ctx)->kind == T_STR ? peek(ctx) : NULL;
	return t && ty->base->size == wide_size(t->wide);
}

static Buf read_string_lit(Ctx *ctx) {
	Buf s; buf_init(&s);
	int braced = consume(ctx, T_LB);
	read_strs(ctx, &s);
	if (braced) { consume(ctx, T_COMMA); expect(ctx, T_RB); }
	return s;
}

static void skip_designator_eq(Ctx *ctx) { consume(ctx, T_ASSIGN); }

static void data_write(Ctx *ctx, long off, long long v, int size) {
	buf_zero_to(&ctx->data, (size_t)(off + size));
	for (int i = 0; i < size; i++) ctx->data.data[off + i] = (uint8_t)(v >> (8 * i));
}

static int global_addr_init(Ctx *ctx, long off) {
	if (ctx->tok.kind != T_AND || !ctx->fn || peek(ctx)->kind != T_IDENT) return 0;
	next(ctx);
	Label *l = find_label(ctx, ctx->tok.str);
	next(ctx);
	data_write(ctx, off, 0, 8);
	if (l->pos < 0) patches_push(&l->abs, (size_t)ctx->nrelocs);
	add_reloc(ctx, R_ABS64, (size_t)off, NULL, ctx->cur_text, l->pos);
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
	if (is_flonum(ty)) {
		CV r = cv_rval(ctx, ce_cond(ctx));
		if (cv_reloc(&r) || !is_arith(r.ty)) error_at(ctx, "initializer element is not a constant");
		long double f = cv_convert(r, ty).f;
		if (ty->kind == TY_LDOUBLE) {
			uint8_t b[16];
			memset(b, 0, sizeof(b));
			memcpy(b, &f, 10);
			buf_zero_to(&ctx->data, (size_t)(off + 16));
			memcpy(ctx->data.data + off, b, 16);
		} else data_write(ctx, off, (long long)float_bits((double)f, ty), ty->size);
	} else if (ty->bit_width) {
		uint64_t mask = ty->bit_width >= 64 ? ~0ULL : (1ULL << ty->bit_width) - 1;
		uint64_t v = ((uint64_t)const_expr(ctx) & mask) << ty->bit_off;
		buf_zero_to(&ctx->data, (size_t)(off + ty->bit_unit));
		uint64_t cur = 0;
		for (int i = 0; i < ty->bit_unit; i++) cur |= (uint64_t)ctx->data.data[off + i] << (8 * i);
		cur = (cur & ~(mask << ty->bit_off)) | v;
		data_write(ctx, off, (long long)cur, ty->bit_unit);
	} else if (!global_addr_init(ctx, off)) {
		CV r = cv_rval(ctx, ce_cond(ctx));
		if (cv_reloc(&r)) {
			if (ty->size != 8) error_at(ctx, "initializer element is not computable at load time");
			data_write(ctx, off, 0, 8);
			add_reloc(ctx, R_ABS64, (size_t)off, r.sym, r.sym ? SEC_TEXT : r.sect, r.v);
		} else data_write(ctx, off, const_cast_val(r.v, ty), ty->size);
	}
}

static Type *init_string(Ctx *ctx, Type *ty, long off, int global) {
	Buf s = read_string_lit(ctx);
	if (ty->len < 0) ty = array_of(ty->base, (int)s.len / ty->base->size);
	int n = (int)s.len < ty->size ? (int)s.len : ty->size;
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
	while (m && m->flat) m = m->next;
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
		if (ty->is_union) m = NULL;
		else do m = m->next; while (m && m->flat);
		if (!braced && !m) break;
		if (!init_continue(ctx, braced)) break;
	}
	return 0;
}

static Type *init_value(Ctx *ctx, Type *ty, long off, int global, int top) {
	if (ty->kind == TY_COMPLEX) {
		int braced = consume(ctx, T_LB);
		if (global) {
			CV r = cv_rval(ctx, ce_cond(ctx));
			if (cv_reloc(&r) || !is_arith(r.ty)) error_at(ctx, "initializer element is not a constant");
			long double f = cv_convert(r, ty->base).f;
			buf_zero_to(&ctx->data, (size_t)(off + ty->size));
			if (ty->base->kind == TY_LDOUBLE) memcpy(ctx->data.data + off, &f, 10);
			else data_write(ctx, off, (long long)float_bits((double)f, ty->base), ty->base->size);
		} else {
			emit_lea_init(ctx, RAX, (int)off);
			gen_push(ctx);
			Type *rt = rval(ctx, parse_assign(ctx));
			gen_cast(ctx, rt, ty);
			gen_pop(ctx, RCX);
			emit_store_type(&ctx->text, ty);
		}
		if (braced) { consume(ctx, T_COMMA); expect(ctx, T_RB); }
		return ty;
	}
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
		if (ctx->tok.kind != T_LB && ctx->tok.kind != T_STR && ctx->tok.kind != T_NUM && ctx->tok.kind != T_FNUM && !global) {
			TokVec v = capture_expr(ctx);
			replay(ctx, &v);
			Mark m = mark_code(ctx);
			size_t ro = ctx->rodata.len;
			int frame = ctx->fn ? ctx->fn->frame : 0;
			Type *rt = parse_assign(ctx);
			if (rt->kind == TY_STRUCT && same_type(rt, ty)) {
				rval(ctx, rt);
				emit_lea_init(ctx, RDI, (int)off);
				gen_struct_copy_to_rdi(ctx, ty->size);
				free(v.v);
				return ty;
			}
			drop_code(ctx, m);
			ctx->rodata.len = ro;
			if (ctx->fn) ctx->fn->frame = frame;
			ctx->lv = 0;
			replay(ctx, &v);
			free(v.v);
		}
		if (global && ctx->tok.kind == T_LP && peek(ctx)->kind == T_LP) {
			TokVec v = capture_expr(ctx);
			int a = 0, b = v.n;
			for (;;) {
				if (b - a < 2 || v.v[a].kind != T_LP || v.v[b-1].kind != T_RP) break;
				int d = 0, i = a;
				for (; i < b; i++) {
					if (v.v[i].kind == T_LP) d++;
					else if (v.v[i].kind == T_RP && !--d) break;
				}
				if (i != b - 1 || v.v[a+1].kind != T_LP) break;
				a++;
				b--;
			}
			TokVec r = { v.v + a, b - a, b - a };
			replay(ctx, &r);
			free(v.v);
		}
		if (global && ctx->tok.kind == T_LP && is_typename(ctx, peek(ctx))) {
			next(ctx);
			Type *ct = parse_typename(ctx);
			expect(ctx, T_RP);
			if (!same_type(ct, ty) || ctx->tok.kind != T_LB) error_at(ctx, "incompatible struct initializer");
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
	ctx->ginit++;
	ty = init_value(ctx, ty, off, 1, 1);
	ctx->ginit--;
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
	if (attrs & ATTR_TWICE) g->twice = 1;
	if (attrs & ATTR_WEAK) {
		g->weak = 1;
		g->weak_unit = ctx->unit;
		if (g->strong_ref && g->strong_unit == ctx->unit && !g->strong_multi) g->strong_ref = 0;
	} else if (!weak_here(ctx, g, 0)) {
		if (g->strong_ref && g->strong_unit != ctx->unit) g->strong_multi = 1;
		g->strong_ref = 1;
		g->strong_unit = ctx->unit;
	}
	if (attrs & ATTR_USED) g->used_attr = 1;
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
	int sect = SEC_DATA, named = 0;
	if (g->tls) {
		if (!has_init && g->defined) return;
		sect = section_index(ctx, intern(has_init ? ".tdata" : ".tbss"), SK_DATA);
		ctx->secs[sect - SEC_NAMED].tls = has_init ? 1 : 2;
	} else if (g->section && (has_init || strcmp(g->section, ".bss") != 0)) {
		sect = section_index(ctx, g->section, strncmp(g->section, ".rodata", 7) == 0 ? SK_RO : SK_DATA);
		named = 1;
	}
	if (has_init || named || sect != SEC_DATA) {
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
		buf_zero_to(&ctx->data, (size_t)(off + (ty->size ? ty->size : 1)));
		if (sect != SEC_DATA) { *sec_buf(ctx, sect) = ctx->data; ctx->data = saved; ctx->cur_data = SEC_DATA; }
		while (ctx->ncl && !ctx->ginit) {
			struct PendCL p = ctx->cl[--ctx->ncl];
			replay(ctx, &p.v);
			free(p.v.v);
			define_global_var(ctx, p.g, p.g->ty, 1);
		}
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
	ctx->bss_size += ty->size ? ty->size : 1;
}

static void parse_block(Ctx *ctx);

static int label_needed(Ctx *ctx, TokVec *v, char *raw);

static int label_used_later(Ctx *ctx, char *name, int levels) {
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0, used = 0;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_EOF) break;
		if (k == T_LB) depth++;
		else if (k == T_RB) {
			if (depth) depth--;
			else if (--levels <= 0) break;
		}
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	used = label_needed(ctx, &v, name);
	replay(ctx, &v);
	free(v.v);
	return used;
}

static void define_label(Ctx *ctx) {
	Code *c = &ctx->text;
	char *raw = ctx->tok.str;
	Label *l = find_label(ctx, raw);
	if (l->patches.n || l->xrel.n || l->abs.n || ctx->inl) kv_forget_all(ctx);
	int live = !ctx->dead || l->abs.n;
	for (int i = 0; i < l->patches.n && !live; i++) live = l->patches.v[i] < ctx->dead_mark.pos;
	for (int i = 0; i < l->xrel.n && !live; i++) live = (int)l->xrel.v[i] < ctx->dead_mark.nrel;
	if (!live && ctx->dead && !ctx->dead_x) {
		next(ctx);
		Tok colon = ctx->tok;
		next(ctx);
		live = label_used_later(ctx, raw, ctx->depth - ctx->fn_depth + 1);
		TokVec b;
		memset(&b, 0, sizeof(b));
		Tok id = colon;
		id.kind = T_IDENT;
		id.str = raw;
		tv_push(&b, id);
		tv_push(&b, colon);
		replay(ctx, &b);
		free(b.v);
	}
	if (live) revive(ctx);
	if (l->pos >= 0) error_at(ctx, "duplicate label '%s'", ctx->tok.str);
	next(ctx);
	next(ctx);
	if (!(l->patches.n || l->xrel.n || l->abs.n || ctx->inl)) kv_forget_scan(ctx, ctx->depth - ctx->fn_depth + 1);
	l->seen = 1;
	l->pos = (long)Cpos(c);
	for (int i = 0; i < l->patches.n; i++)
		patch_jmp_to(&c->b, l->patches.v[i], (size_t)l->pos);
	for (int i = 0; i < l->abs.n; i++)
		ctx->relocs[l->abs.v[i]].addend = l->pos;
	for (int i = 0; i < l->xrel.n; i++)
		ctx->relocs[l->xrel.v[i]].addend += l->pos;
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
	int battrs = ctx->decl_attrs, thread = ctx->decl_thread;
	Attr battr = ctx->decl_attr;
	if (thread && storage != ST_STATIC && storage != ST_EXTERN) error_at(ctx, "a thread-local variable must be static or extern");
	if (ctx->tok.kind == T_SEMI) { next(ctx); return; }
	for (;;) {
		char *name = NULL;
		ctx->name_attrs = 0;
		ctx->name_attr = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		int nattrs = ctx->name_attrs;
		Attr *nattr = ctx->name_attr;
		char *label = parse_asm_label(ctx);
		int attrs = battrs | ctx->tok.attrs | nattrs;
		Attr da = battr;
		attr_merge(&da, nattr);
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
			if (thread) g->tls = 1;
		} else if (storage == ST_STATIC) {
			Sym *g = (Sym *)calloc(1, sizeof(Sym));
			g->name = name; g->kind = SYM_VAR; g->ty = ty; g->is_static = 1;
			g->tls = thread;
			g->align = da.align;
			g->section = da.section;
			if (attrs & ATTR_USED) g->used_attr = 1;
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
			Sym *s = push_sym(ctx, name, SYM_VAR, ty);
			s->is_local = 1;
			if (ty->vla) {
				Code *c = &ctx->text;
				if (ctx->tok.kind == T_ASSIGN) error_at(ctx, "variable length array '%s' cannot have an initializer", name);
				if (ctx->sp) error_at(ctx, "variable length array '%s' inside an expression is not supported", name);
				if (!ctx->vla_block) {
					if (ctx->nvla == 64) error_at(ctx, "too many nested blocks with variable length arrays");
					ctx->vla_block = alloc_local(ctx, 8, 8);
					emit_store_rbp(c, RSP, ctx->vla_block);
					ctx->vla_slots[ctx->nvla++] = ctx->vla_block;
				}
				s->stack_off = alloc_local(ctx, 8, 8);
				s->indirect = 1;
				emit_load_rbp(c, RAX, ty->vla);
				emit_add_rax_imm(c, 15);
				C8(c,0x48); C8(c,0x83); C8(c,0xE0); C8(c,0xF0);
				C8(c,0x48); C8(c,0x29); C8(c,0xC4);
				emit_store_rbp(c, RSP, s->stack_off);
				if (!consume(ctx, T_COMMA)) break;
				continue;
			}
			if (ty->align > 16) {
				if (ty->kind == TY_ARRAY && ty->len < 0) error_at(ctx, "local variable '%s' needs a size", name);
				if (ctx->tok.kind == T_ASSIGN) error_at(ctx, "local variable '%s' aligned to more than 16 bytes cannot have an initializer", name);
				Code *c = &ctx->text;
				int base = alloc_local(ctx, ty->size + ty->align, 16);
				s->stack_off = alloc_local(ctx, 8, 8);
				s->indirect = 1;
				emit_lea_rbp(c, RAX, base);
				emit_add_rax_imm(c, ty->align - 1);
				C8(c,0x48); C8(c,0x25); C32(c,(uint32_t)-ty->align);
				emit_store_rbp(c, RAX, s->stack_off);
				if (!consume(ctx, T_COMMA)) break;
				continue;
			}
			if (label) {
				int r = asm_regnum(label);
				if (r < 0 || r == RSP || r == RBP) error_at(ctx, "invalid register '%s' for '%s'", label, name);
				s->asm_reg = r + 1;
			}
			if (ctx->tok.kind == T_ASSIGN && ctx->fold && (is_integer(ty) || is_ptr(ty)) && !ty->bit_width && !s->asm_reg && peek(ctx)->kind != T_LB) {
				next(ctx);
				size_t pos = Cpos(&ctx->text);
				int sp = ctx->sp;
				Type *rt = rval(ctx, parse_assign(ctx));
				long long kv;
				if ((is_integer(rt) || is_ptr(rt)) && expr_const(ctx, &kv)) {
					drop_to(ctx, pos, sp);
					kv = const_cast_val(kv, ty);
					if (!rest_written(ctx, name)) {
						s->bound = 1;
						s->kind = SYM_ENUMC;
						s->is_local = 0;
						s->val = kv;
					} else {
						s->stack_off = alloc_local(ctx, ty->size, ty->align);
						emit_mov_rax_imm(&ctx->text, kv);
						emit_lea_rbp(&ctx->text, RCX, s->stack_off);
						emit_store_type(&ctx->text, ty);
						if (!ctx->cond_depth && !ctx->xcond) { s->kv_valid = 1; s->kv = kv; }
					}
				} else {
					if (is_scalar(rt)) gen_cast(ctx, rt, ty);
					else if (!(is_ptr(ty) && (rt->kind == TY_ARRAY || rt->kind == TY_FUNC))) error_at(ctx, "invalid initializer for '%s'", name);
					s->stack_off = alloc_local(ctx, ty->size, ty->align);
					emit_lea_rbp(&ctx->text, RCX, s->stack_off);
					emit_store_type(&ctx->text, ty);
				}
			} else if (consume(ctx, T_ASSIGN)) {
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

static void after_jump(Ctx *ctx) {
	LoopCtx *lc = loopstack_switch(&ctx->loops);
	if (lc && lc->kconst && !lc->kdead && ctx->cond_depth == lc->depth) {
		dead_begin(ctx);
		lc->kdead = 1;
	}
}

static void parse_body(Ctx *ctx) {
	ctx->cond_depth++;
	parse_stmt(ctx);
	ctx->cond_depth--;
}

static void parse_stmt1(Ctx *ctx);

static int tok_group_end(TokVec *v, int i) {
	int d = 0;
	for (; i < v->n; i++) {
		int k = v->v[i].kind;
		if (k == T_LP || k == T_LBRACKET || k == T_LB) d++;
		else if ((k == T_RP || k == T_RBRACKET || k == T_RB) && --d == 0) return i + 1;
	}
	return v->n;
}

static int tok_stmt_end(TokVec *v, int i) {
	if (i >= v->n) return v->n;
	int k = v->v[i].kind;
	if (k == T_LB) return tok_group_end(v, i);
	if (k == T_IF) {
		i = tok_stmt_end(v, tok_group_end(v, i + 1));
		if (i < v->n && v->v[i].kind == T_ELSE) i = tok_stmt_end(v, i + 1);
		return i;
	}
	if (k == T_WHILE || k == T_SWITCH || k == T_FOR) return tok_stmt_end(v, tok_group_end(v, i + 1));
	if (k == T_DO) {
		i = tok_stmt_end(v, i + 1);
		if (i < v->n && v->v[i].kind == T_WHILE) i = tok_group_end(v, i + 1);
		return i < v->n ? i + 1 : v->n;
	}
	if (k == T_IDENT && i + 1 < v->n && v->v[i+1].kind == T_COLON) return tok_stmt_end(v, i + 2);
	if (k == T_DEFAULT) return tok_stmt_end(v, i + 2);
	if (k == T_CASE) {
		while (i < v->n && v->v[i].kind != T_COLON) i++;
		return tok_stmt_end(v, i + 1);
	}
	int d = 0;
	for (; i < v->n; i++) {
		k = v->v[i].kind;
		if (k == T_LP || k == T_LBRACKET || k == T_LB) d++;
		else if (k == T_RP || k == T_RBRACKET || k == T_RB) d--;
		else if (k == T_SEMI && !d) return i + 1;
	}
	return v->n;
}

static int label_defined_at(TokVec *v, int i) {
	if (v->v[i].kind != T_IDENT || i + 1 >= v->n || v->v[i+1].kind != T_COLON) return 0;
	int pk = i ? v->v[i-1].kind : T_SEMI;
	return pk == T_SEMI || pk == T_LB || pk == T_RB || pk == T_COLON || pk == T_RP || pk == T_ELSE || pk == T_DO;
}

static int label_refd_live(Ctx *ctx, char *raw);

static int label_needed(Ctx *ctx, TokVec *v, char *raw) {
	int re = 0;
	if (ctx->dead_jdepth) {
		int cur = ctx->depth;
		re = v->n;
		for (int i = 0; i < v->n; i++) {
			if (v->v[i].kind == T_LB) cur++;
			else if (v->v[i].kind == T_RB) {
				if (cur == ctx->dead_jdepth - 1) { re = i; break; }
				cur--;
			}
		}
	}
	int cap = 16, n = 0;
	char **need = (char **)malloc(sizeof(char *) * (size_t)cap);
	for (int j = re; j + 1 < v->n; j++)
		if ((v->v[j].kind == T_GOTO || v->v[j].kind == T_AND) && v->v[j+1].kind == T_IDENT) {
			if (n == cap) need = (char **)realloc(need, sizeof(char *) * (size_t)(cap *= 2));
			need[n++] = v->v[j+1].str;
		}
	for (int i = 0; i < re; i++) if (label_defined_at(v, i) && label_refd_live(ctx, v->v[i].str)) {
		if (n == cap) need = (char **)realloc(need, sizeof(char *) * (size_t)(cap *= 2));
		need[n++] = v->v[i].str;
	}
	for (int changed = 1; changed;) {
		changed = 0;
		for (int i = 0; i < re; i++) {
			if (!label_defined_at(v, i)) continue;
			int in = 0;
			for (int k = 0; k < n && !in; k++) in = need[k] == v->v[i].str;
			if (!in) continue;
			for (int j = i; j + 1 < re; j++) {
				if (!((v->v[j].kind == T_GOTO || v->v[j].kind == T_AND) && v->v[j+1].kind == T_IDENT)) continue;
				int have = 0;
				for (int k = 0; k < n && !have; k++) have = need[k] == v->v[j+1].str;
				if (have) continue;
				if (n == cap) need = (char **)realloc(need, sizeof(char *) * (size_t)(cap *= 2));
				need[n++] = v->v[j+1].str;
				changed = 1;
			}
		}
	}
	int r = 0;
	for (int k = 0; k < n && !r; k++) r = need[k] == raw;
	free(need);
	return r;
}

static int label_refd_live(Ctx *ctx, char *raw) {
	char *name = raw;
	for (LLab *ll = ctx->llabs; ll; ll = ll->next) if (ll->name == raw) { name = ll->real; break; }
	if (ctx->inl) {
		char b[256];
		snprintf(b, sizeof(b), "%s.inl%d", name, ctx->inl->id);
		name = intern(b);
	}
	for (Label *l = ctx->fn->labels; l; l = l->next) {
		if (l->name != name) continue;
		if (l->abs.n) return 1;
		for (int j = 0; j < l->patches.n; j++) if (l->patches.v[j] < ctx->dead_mark.pos) return 1;
		for (int j = 0; j < l->xrel.n; j++) if ((int)l->xrel.v[j] < ctx->dead_mark.nrel) return 1;
	}
	return 0;
}

static int label_tok_live(Ctx *ctx, TokVec *v, int i) {
	char *raw = v->v[i].str, *name = raw;
	for (LLab *ll = ctx->llabs; ll; ll = ll->next) if (ll->name == raw) { name = ll->real; break; }
	if (ctx->inl) {
		char b[256];
		snprintf(b, sizeof(b), "%s.inl%d", name, ctx->inl->id);
		name = intern(b);
	}
	for (Label *l = ctx->fn->labels; l; l = l->next) {
		if (l->name != name) continue;
		if (l->abs.n) return 1;
		for (int j = 0; j < l->patches.n; j++) if (l->patches.v[j] < ctx->dead_mark.pos) return 1;
		for (int j = 0; j < l->xrel.n; j++) if ((int)l->xrel.v[j] < ctx->dead_mark.nrel) return 1;
	}
	return label_needed(ctx, v, raw);
}

static void stmt_revive(Ctx *ctx) {
	if (!ctx->dead || !ctx->fn || ctx->dead_x || ctx->dead_sx != ctx->sx_depth) return;
	TokVec v;
	memset(&v, 0, sizeof(v));
	int depth = 0, levels = ctx->depth - ctx->fn_depth + 1;
	for (;;) {
		int k = ctx->tok.kind;
		if (k == T_EOF) break;
		if (k == T_LB) depth++;
		else if (k == T_RB) {
			if (depth) depth--;
			else if (--levels <= 0) break;
		}
		tv_push(&v, ctx->tok);
		next(ctx);
	}
	int e = tok_stmt_end(&v, 0), live = 0;
	for (int i = 0; i + 1 < e && !live; i++) {
		if (v.v[i].kind != T_IDENT || v.v[i+1].kind != T_COLON) continue;
		int pk = i ? v.v[i-1].kind : T_SEMI;
		if (pk != T_SEMI && pk != T_LB && pk != T_RB && pk != T_COLON && pk != T_RP && pk != T_ELSE && pk != T_DO) continue;
		live = label_tok_live(ctx, &v, i);
	}
	replay(ctx, &v);
	free(v.v);
	if (live) revive(ctx);
}

static void parse_stmt(Ctx *ctx) {
	int k = ctx->tok.kind;
	if (k == T_WHILE || k == T_DO || k == T_FOR || k == T_IF || k == T_SWITCH || k == T_LB) stmt_revive(ctx);
	parse_stmt1(ctx);
	if (k == T_WHILE || k == T_DO || k == T_FOR || k == T_SWITCH) ctx->jumped = 0;
}

static void parse_stmt1(Ctx *ctx) {
	Code *c = &ctx->text;
	int k = ctx->tok.kind;

	if (k == T_LB) { parse_block(ctx); return; }
	if (k == T_SEMI) { next(ctx); return; }
	if (local_labels(ctx)) return;
	if (is_asm_kw(ctx)) { asm_statement(ctx, 0); return; }

	if (k == T_RETURN && ctx->inl) {
		Inl *in = ctx->inl;
		next(ctx);
		if (ctx->tok.kind != T_SEMI) {
			Type *t = rval(ctx, parse_expr(ctx));
			if (in->ret->kind == TY_COMPLEX) gen_cast(ctx, t, in->ret);
			if (in->ret->kind == TY_STRUCT || in->ret->kind == TY_COMPLEX) {
				if (!in->tmp) in->tmp = alloc_local(ctx, in->ret->size, in->ret->align);
				emit_copy_to_rbp(c, RAX, in->tmp, in->ret->size);
				emit_lea_rbp(c, RAX, in->tmp);
			} else if (in->ret->kind != TY_VOID) gen_cast(ctx, t, in->ret);
		}
		expect(ctx, T_SEMI);
		patches_push(&in->rets, emit_jmp_placeholder(c));
		after_jump(ctx);
		ctx->jumped = 1;
		return;
	}
	if (k == T_RETURN) {
		next(ctx);
		if (ctx->tok.kind != T_SEMI) {
			Type *t = rval(ctx, parse_expr(ctx));
			Type *rt = ctx->fn->ty->base;
			int cls[2];
			int n = classify(rt, cls);
			if (rt->kind == TY_COMPLEX) gen_cast(ctx, t, rt);
			if (rt->kind == TY_COMPLEX && n < 0) {
				emit_x87(c, 0xDB, 5, RAX, 16);
				emit_x87(c, 0xDB, 5, RAX, 0);
			} else if (rt->kind == TY_STRUCT && n < 0) {
				emit_load_rbp(c, RDI, ctx->fn->ret_off);
				gen_struct_copy_to_rdi(ctx, rt->size);
			} else if (rt->kind == TY_STRUCT || rt->kind == TY_COMPLEX) {
				int tmp = alloc_local(ctx, 16, 16), g = 0, x = 0;
				static const int retregs[2] = { RAX, RDX };
				emit_copy_to_rbp(c, RAX, tmp, rt->size);
				for (int i = 0; i < n; i++) {
					if (cls[i] == CL_SSE) emit_movq_xmm_load(c, x++, RBP, tmp + 8 * i);
					else if (cls[i] == CL_INT) emit_load_rbp(c, retregs[g++], tmp + 8 * i);
				}
			} else if (rt->kind != TY_VOID) {
				gen_cast(ctx, t, rt);
				if (rt->kind == TY_LDOUBLE) emit_x87(c, 0xDB, 5, RAX, 0);
				if (rt->kind == TY_DOUBLE) emit_movq_xmm0_rax(c);
				else if (rt->kind == TY_FLOAT) { C8(c,0x66); C8(c,0x0F); C8(c,0x6E); C8(c,0xC0); }
			}
		}
		expect(ctx, T_SEMI);
		emit_leave(c);
		emit_ret(c);
		after_jump(ctx);
		ctx->jumped = 1;
		return;
	}

	if (k == T_IF) {
		next(ctx);
		expect(ctx, T_LP);
		Type *ct = rval(ctx, parse_expr(ctx));
		if (!is_scalar(ct) && !is_complex(ct)) error_at(ctx, "scalar required in condition");
		long long kv;
		if (!is_flonum(ct) && expr_const(ctx, &kv)) {
			drop_to(ctx, ctx->k_start, ctx->sp);
			expect(ctx, T_RP);
			if (!kv) { ctx->next_jdepth = ctx->tok.kind == T_LB ? ctx->depth + 2 : 0; dead_begin(ctx); }
			ctx->jumped = 0;
			parse_body(ctx);
			if (!kv) dead_end(ctx);
			int jt = kv ? ctx->jumped : 0;
			ctx->jumped = 0;
			if (ctx->tok.kind == T_ELSE) {
				next(ctx);
				if (kv) { ctx->next_jdepth = ctx->tok.kind == T_LB ? ctx->depth + 2 : 0; dead_begin(ctx); }
				parse_body(ctx);
				if (kv) dead_end(ctx);
				if (!kv) jt = ctx->jumped;
			}
			ctx->jumped = jt;
			return;
		}
		gen_truth(ctx, ct);
		expect(ctx, T_RP);
		emit_test_rax_rax(c);
		size_t jz_patch = emit_jz_placeholder(c);
		parse_body(ctx);
		if (ctx->tok.kind == T_ELSE) {
			next(ctx);
			size_t jmp_patch = emit_jmp_placeholder(c);
			patch_jmp_to(&c->b, jz_patch, Cpos(c));
			parse_body(ctx);
			patch_jmp_to(&c->b, jmp_patch, Cpos(c));
		} else {
			patch_jmp_to(&c->b, jz_patch, Cpos(c));
		}
		ctx->jumped = 0;
		return;
	}

	if (k == T_BREAK) {
		next(ctx);
		expect(ctx, T_SEMI);
		LoopCtx *lc = loopstack_cur(&ctx->loops);
		if (!lc) error_at(ctx, "'break' outside loop");
		if (ctx->nvla > lc->vla_mark) emit_load_rbp(c, RSP, ctx->vla_slots[lc->vla_mark]);
		patches_push(&lc->breaks, emit_jmp_placeholder(c));
		after_jump(ctx);
		if (!lc->is_switch) ctx->jumped = 1;
		return;
	}

	if (k == T_CONTINUE) {
		next(ctx);
		expect(ctx, T_SEMI);
		LoopCtx *lc = loopstack_loop(&ctx->loops);
		if (!lc) error_at(ctx, "'continue' outside loop");
		if (ctx->nvla > lc->vla_mark) emit_load_rbp(c, RSP, ctx->vla_slots[lc->vla_mark]);
		if (lc->cont_target >= 0) emit_jmp_to(c, (size_t)lc->cont_target);
		else patches_push(&lc->conts, emit_jmp_placeholder(c));
		after_jump(ctx);
		ctx->jumped = 1;
		return;
	}

	if (k == T_WHILE) {
		kv_forget_loop(ctx);
		next(ctx);
		size_t loop_top = Cpos(c);
		expect(ctx, T_LP);
		Type *ct = rval(ctx, parse_expr(ctx));
		if (!is_scalar(ct) && !is_complex(ct)) error_at(ctx, "scalar required in condition");
		long long kv;
		if (!is_flonum(ct) && expr_const(ctx, &kv) && !kv) {
			drop_to(ctx, ctx->k_start, ctx->sp);
			expect(ctx, T_RP);
			loopstack_push(&ctx->loops, (long)loop_top, 0)->vla_mark = ctx->nvla;
			dead_begin(ctx);
			parse_body(ctx);
			dead_end(ctx);
			loopstack_pop(&ctx->loops);
			return;
		}
		gen_truth(ctx, ct);
		expect(ctx, T_RP);
		emit_test_rax_rax(c);
		size_t jz_patch = emit_jz_placeholder(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, (long)loop_top, 0);
		lc->vla_mark = ctx->nvla;
		parse_body(ctx);
		loopstack_pop(&ctx->loops);
		emit_jmp_to(c, loop_top);
		size_t loop_end = Cpos(c);
		patch_jmp_to(&c->b, jz_patch, loop_end);
		for (int i = 0; i < lc->breaks.n; i++)
			patch_jmp_to(&c->b, lc->breaks.v[i], loop_end);
		return;
	}

	if (k == T_DO) {
		kv_forget_loop(ctx);
		next(ctx);
		size_t loop_top = Cpos(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, -1, 0);
		lc->vla_mark = ctx->nvla;
		parse_body(ctx);
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
		kv_forget_loop(ctx);
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
		int nkv = 0;
		for (Sym *s = ctx->scope; s && s->depth > 0; s = s->next) nkv++;
		Sym **unk = calloc(nkv + 1, sizeof(Sym *));
		nkv = 0;
		for (Sym *s = ctx->scope; s && s->depth > 0; s = s->next) if (!s->kv_valid) unk[nkv++] = s;
		if (ctx->tok.kind != T_RP) { parse_expr(ctx); ctx->lv = 0; }
		for (int i = 0; i < nkv; i++) unk[i]->kv_valid = 0;
		free(unk);
		expect(ctx, T_RP);
		emit_jmp_to(c, top);
		patch_jmp_to(&c->b, jbody, Cpos(c));
		LoopCtx *lc = loopstack_push(&ctx->loops, (long)step, 0);
		lc->vla_mark = ctx->nvla;
		parse_body(ctx);
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
		long long kv;
		if (expr_const(ctx, &kv)) {
			drop_to(ctx, ctx->k_start, ctx->sp);
			LoopCtx *lc = loopstack_push(&ctx->loops, -1, 1);
			lc->vla_mark = ctx->nvla;
			lc->sw_type = int_promote(t);
			lc->kconst = 1;
			lc->kval = const_cast_val(kv, lc->sw_type);
			lc->depth = ctx->cond_depth;
			dead_begin(ctx);
			lc->kdead = 1;
			ctx->swbody = 1;
			parse_stmt(ctx);
			ctx->swbody = 0;
			if (lc->kdead) dead_end(ctx);
			loopstack_pop(&ctx->loops);
			kv_forget_all(ctx);
			for (int i = 0; i < lc->breaks.n; i++)
				patch_jmp_to(&c->b, lc->breaks.v[i], Cpos(c));
			return;
		}
		int tmp = alloc_local(ctx, 8, 8);
		emit_store_rbp(c, RAX, tmp);
		size_t jdispatch = emit_jmp_placeholder(c);
		LoopCtx *lc = loopstack_push(&ctx->loops, -1, 1);
		lc->vla_mark = ctx->nvla;
		lc->sw_type = int_promote(t);
		lc->body = Cpos(c);
		ctx->swbody = 1;
		parse_stmt(ctx);
		ctx->swbody = 0;
		loopstack_pop(&ctx->loops);
		kv_forget_all(ctx);
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
		kv_forget_all(ctx);
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
		if (lc->kconst) {
			int u = lc->sw_type->is_unsigned;
			int hit = u ? (unsigned long long)v <= (unsigned long long)lc->kval && (unsigned long long)lc->kval <= (unsigned long long)hi :
				v <= lc->kval && lc->kval <= hi;
			if (hit) {
				if (lc->kdead) { dead_end(ctx); lc->kdead = 0; }
				if (lc->tentative) { cut_code(ctx, lc->tmark); lc->tentative = 0; }
				lc->matched = 1;
			}
		} else if (lc->sw_type->is_unsigned ? (unsigned long long)hi < (unsigned long long)v : hi < v)
			warn_at(ctx, "empty range specified");
		else {
			case_revive(ctx, lc);
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
		kv_forget_all(ctx);
		LoopCtx *lc = loopstack_switch(&ctx->loops);
		if (!lc) error_at(ctx, "'default' outside switch");
		if (lc->kconst && lc->kdead && !lc->matched) {
			dead_end(ctx);
			lc->kdead = 0;
			lc->tentative = 1;
			lc->tmark = mark_code(ctx);
		}
		if (!lc->kconst) case_revive(ctx, lc);
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
		after_jump(ctx);
		ctx->jumped = 1;
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

static int local_labels(Ctx *ctx) {
	if (ctx->tok.kind != T_IDENT || strcmp(ctx->tok.str, "__label__")) return 0;
	next(ctx);
	do {
		if (ctx->tok.kind != T_IDENT) error_at(ctx, "expected a label name after __label__");
		LLab *ll = (LLab *)calloc(1, sizeof(LLab));
		char b[256];
		snprintf(b, sizeof(b), "%s.ll%d", ctx->tok.str, ++ctx->nllab);
		ll->name = ctx->tok.str;
		ll->real = intern(b);
		ll->depth = ctx->depth;
		ll->next = ctx->llabs;
		ctx->llabs = ll;
		next(ctx);
	} while (consume(ctx, T_COMMA));
	expect(ctx, T_SEMI);
	return 1;
}

static void parse_stmts(Ctx *ctx, const char *what) {
	int sw = ctx->swbody, started = 0, d0 = 0, jumped = 0;
	ctx->swbody = 0;
	while (ctx->tok.kind != T_RB) {
		if (ctx->tok.kind == T_EOF) error_at(ctx, "unexpected EOF in %s", what);
		ctx->jumped = 0;
		parse_stmt(ctx);
		if (ctx->jumped && !started && !sw && ctx->fold) {
			d0 = ctx->dead;
			ctx->next_jdepth = ctx->depth + 1;
			dead_begin(ctx);
			started = 1;
		}
	}
	if (started) {
		jumped = ctx->dead > d0;
		dead_end(ctx);
	}
	ctx->jumped = jumped;
}

static void parse_block(Ctx *ctx) {
	expect(ctx, T_LB);
	enter_scope(ctx);
	int vb = ctx->vla_block, nv = ctx->nvla;
	int frame = ctx->fn ? ctx->fn->frame : 0;
	ctx->vla_block = 0;
	parse_stmts(ctx, "block");
	if (ctx->vla_block) emit_load_rbp(&ctx->text, RSP, ctx->vla_block);
	ctx->vla_block = vb;
	ctx->nvla = nv;
	if (ctx->fn) ctx->fn->frame = frame;
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
	ctx->fn_depth = ctx->depth;
	int cls[2];
	int sret = fty->base->kind == TY_STRUCT && classify(fty->base, cls) < 0;
	int gp = 0, sse = 0, stk = 16;
	if (fty->variadic) {
		f.save_off = alloc_local(ctx, 176, 16);
		for (int i = 0; i < 6; i++) emit_store_rbp(c, argregs[i], f.save_off + 8 * i);
		if (!ctx->no_float && !ctx->no_sse) {
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
	parse_stmts(ctx, "function body");
	next(ctx);
	leave_scope(ctx);

	emit_xor_eax_eax(c);
	emit_leave(c);
	emit_ret(c);

	for (Label *l = f.labels; l; l = l->next)
		if (l->pos < 0 && (!l->seen || l->patches.n || l->xrel.n || l->abs.n)) error_at(ctx, "label '%s' used but not defined", l->name);

	uint32_t frame_size = (uint32_t)align_up((size_t)f.frame_max, 16);
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
	int attrs = ctx->decl_attrs, thread = ctx->decl_thread, inl = ctx->decl_inline, reg = ctx->decl_register;
	Attr battr = ctx->decl_attr;
	if (consume(ctx, T_SEMI)) return;
	for (;;) {
		char *name = NULL;
		ctx->name_attrs = 0;
		ctx->name_attr = NULL;
		Type *ty = declarator(ctx, base, &name);
		if (!name) error_at(ctx, "expected identifier in declaration");
		int nattrs = ctx->name_attrs;
		Attr *nattr = ctx->name_attr;
		char *label = parse_asm_label(ctx);
		attrs |= ctx->tok.attrs | nattrs;
		Attr da = battr;
		attr_merge(&da, nattr);
		attr_merge(&da, ctx->tok.attr);
		if ((attrs & ATTR_INTERRUPT) && (ty->kind != TY_FUNC || storage == ST_TYPEDEF)) error_at(ctx, "interrupt attribute applies only to functions");
		if (storage == ST_TYPEDEF) {
			push_sym(ctx, name, SYM_TYPEDEF, aligned_type(ty, da.align));
		} else if (reg && label && ty->kind != TY_FUNC) {
			int r = asm_regnum(label);
			if (r < 0) error_at(ctx, "invalid register '%s' for '%s'", label, name);
			push_sym(ctx, name, SYM_VAR, ty)->gregvar = r + 1;
		} else if (ty->kind == TY_FUNC) {
			if (thread) error_at(ctx, "function '%s' declared thread-local", name);
			Sym *g = get_global(ctx, label ? label : name, SYM_FUNC, ty, storage == ST_STATIC);
			if (label) push_sym(ctx, name, SYM_FUNC, ty)->global = g;
			else bind_global(ctx, g);
			decl_attrs(ctx, g, attrs, &da);
			if (attrs & ATTR_INTERRUPT) g->interrupt = 1;
			if (attrs & ATTR_NAKED) g->naked = 1;
			if (ctx->tok.kind == T_LB) {
				int weak = weak_here(ctx, g, attrs);
				if (g->body && !g->inline_only) error_at(ctx, "redefinition of '%s'", name);
				if (g->inline_only) { g->body = NULL; g->inline_only = 0; }
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
				if (attrs & ATTR_ALWAYS_INLINE) g->always = 1;
				if (storage == ST_EXTERN && inl && (ctx->gnu89 || (attrs & ATTR_GNU_INLINE)) && !g->body && !g->defined) {
					g->is_inline = 1;
					capture_body(ctx, g);
					g->inline_only = 1;
					return;
				}
				if (attrs & ATTR_NOINLINE) g->noinline = 1;
				if (inl) g->is_inline = 1;
				if (ctx->fold && storage == ST_STATIC && !g->body &&
					!(attrs & (ATTR_USED | ATTR_NAKED | ATTR_INTERRUPT)) && !g->alias_name) {
					capture_body(ctx, g);
					return;
				}
				parse_func_in(ctx, g, ty);
				return;
			}
		} else {
			Sym *g = get_global(ctx, label ? label : name, SYM_VAR, ty, storage == ST_STATIC);
			if (label) push_sym(ctx, name, SYM_VAR, ty)->global = g;
			else bind_global(ctx, g);
			decl_attrs(ctx, g, attrs, &da);
			if (thread) g->tls = 1;
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
		if (g->asm_alias) g->kind = t->kind;
	}
}

static void prune_statics(Ctx *ctx, Sym *g0, int rel0) {
	int n = 0, cap = 0;
	Sym **cand = NULL;
	for (Sym *g = ctx->globals; g && g != g0; g = g->gnext) {
		if (!g->is_static || g->kind != SYM_VAR || !g->defined || g->used_attr || g->tls || g->sect == SEC_ABS || g->sect == SEC_BSS || g->size <= 0) continue;
		if (n == cap) cand = (Sym **)realloc(cand, sizeof(Sym *) * (size_t)(cap = cap ? cap * 2 : 32));
		g->live = 0;
		cand[n++] = g;
	}
	if (!n) return;
	int nr = ctx->nrelocs - rel0;
	int *in = (int *)malloc(sizeof(int) * (size_t)(nr + 1)), *tg = (int *)malloc(sizeof(int) * (size_t)(nr + 1));
	for (int i = 0; i < nr; i++) {
		Reloc *r = &ctx->relocs[rel0 + i];
		in[i] = tg[i] = -1;
		for (int k = 0; k < n; k++) {
			Sym *g = cand[k];
			if (r->in == g->sect && (long)r->pos >= g->off && (long)r->pos < g->off + g->size) in[i] = k;
			if (r->sym == g || (!r->sym && r->sect == g->sect && r->addend >= g->off && r->addend < g->off + g->size)) tg[i] = k;
		}
	}
	for (Sym *a = ctx->globals; a; a = a->gnext) {
		if (!a->alias_name || a->alias) continue;
		Sym *t = lookup_file_scope(ctx, a->alias_name);
		if (t) t->live = 1;
	}
	for (int changed = 1; changed;) {
		changed = 0;
		for (int i = 0; i < nr; i++)
			if (tg[i] >= 0 && !cand[tg[i]]->live && (in[i] < 0 || cand[in[i]]->live)) { cand[tg[i]]->live = 1; changed = 1; }
	}
	for (int i = 0; i < nr; i++)
		if (in[i] >= 0 && !cand[in[i]]->live) {
			Reloc *r = &ctx->relocs[rel0 + i];
			r->sym = NULL;
			r->sect = r->in;
			r->addend = 0;
		}
	free(in);
	free(tg);
	free(cand);
}

static void compile_lazy(Ctx *ctx) {
	for (;;) {
		Sym *g = NULL;
		for (int i = 0; i < ctx->nrelocs && !g; i++) {
			Sym *s = ctx->relocs[i].sym;
			if (s && s->body && !s->defined && !s->inline_only) g = s;
		}
		for (Sym *a = ctx->globals; a && !g; a = a->gnext) {
			if (!a->alias_name || a->alias) continue;
			Sym *t = lookup_file_scope(ctx, a->alias_name);
			if (t && t->body && !t->defined && !t->inline_only) g = t;
		}
		if (!g) return;
		TokVec v;
		v.v = g->body;
		v.n = v.cap = g->nbody;
		replay(ctx, &v);
		parse_func_in(ctx, g, g->ty);
	}
}

static Type *va_list_type(void) {
	static const char *names[4] = { "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area" };
	Type *st = new_type(TY_STRUCT, 24, 8);
	Member **tail = &st->members;
	for (int i = 0; i < 4; i++) {
		Member *m = (Member *)calloc(1, sizeof(Member));
		m->name = intern(names[i]);
		m->ty = i < 2 ? ty_uint : ptr_to(ty_void);
		m->off = i < 2 ? 4 * i : 8 * (i - 1);
		*tail = m;
		tail = &m->next;
	}
	st->complete = 1;
	return array_of(st, 1);
}

static void compile_unit(Ctx *ctx, const char *src, const char *file, int is_builtin) {
	if (ctx->m32 && !is_builtin) { fprintf(stderr, "%s: error: C code can only be compiled for x86-64, not with -m32 or -m16\n", file); exit(1); }
	ctx->unit++;
	ctx->as_macros = NULL;
	ctx->as_mcount = 0;
	pp_begin_unit(ctx);
	if (!ty_va_list) ty_va_list = va_list_type();
	push_sym(ctx, id_va_list, SYM_TYPEDEF, ty_va_list);
	push_file(ctx, src, file, is_builtin);
	if (!is_builtin) push_includes(ctx, 0);
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	next(ctx);
	Sym *g0 = ctx->globals;
	int rel0 = ctx->nrelocs;
	while (ctx->tok.kind != T_EOF) parse_toplevel(ctx);
	compile_lazy(ctx);
	if (ctx->fold) prune_statics(ctx, g0, rel0);
	resolve_aliases(ctx, file);
	pp_end_unit(ctx);
	ctx->scope = NULL;
	symmap_clear(&ctx->fsmap);
	ctx->tags = NULL;
}

enum { RC_GP, RC_SEG, RC_CR, RC_DR, RC_XMM, RC_IP, RC_ST };
enum { OP_REG, OP_IMM, OP_MEM };

typedef struct {
	char *name;
	int rc, num, size, hi;
} AsReg;

static AsReg as_regs[200];
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
	char *alias;
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
	int stack[16], pstack[16], nstack;
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
	int done, final;
	Lex *lx;
	int snip;
	const char *sfile;
	int sline;
	int cond[16], ncond;
	AsVal *szexpr;
	long long *szval, *szprev;
	int nsz, capsz, nszprev;
	char *bss_name;
	long *bpos;
	char **bname;
	int nbm, capbm;
	struct { Lex *L; Tok q[2]; int nq, iq, first, macro; } xs[64];
	int nxs;
} As;

enum {
	AI_ALU, AI_SHIFT, AI_UNARY, AI_INCDEC, AI_IMUL, AI_MOV, AI_MOVABS, AI_LEA, AI_TEST, AI_XCHG,
	AI_MOVX, AI_FIXED, AI_FIXSZ, AI_STR, AI_PUSH, AI_POP, AI_PUSHF, AI_PUSHA, AI_JMP, AI_CALL,
	AI_JCC, AI_LOOP, AI_LJMP, AI_RET, AI_INT, AI_IN, AI_OUT, AI_PREFIX, AI_SETCC, AI_CMOV,
	AI_X87, AI_X87R, AI_X87A, AI_SSESH, AI_0F, AI_BT, AI_BSWAP, AI_SHD, AI_GRP, AI_NOP, AI_SSE, AI_MOVD, AI_CVTI, AI_CVTF
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
	{ "lsl", AI_0F, 0x03, 1, 0, F_SUF, NULL }, { "lar", AI_0F, 0x02, 1, 0, F_SUF, NULL },
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
	{ "rdpid", AI_GRP, 0, 7, 0, 0, "\xF3\x0F\xC7" },
	{ "fnsave", AI_X87, 0xDD, 6, 0, 0, NULL }, { "fsave", AI_X87, 0xDD, 6, 1, 0, NULL },
	{ "frstor", AI_X87, 0xDD, 4, 0, 0, NULL }, { "fldenv", AI_X87, 0xD9, 4, 0, 0, NULL },
	{ "fnstenv", AI_X87, 0xD9, 6, 0, 0, NULL }, { "fstenv", AI_X87, 0xD9, 6, 1, 0, NULL },
	{ "fldcw", AI_X87, 0xD9, 5, 0, 0, NULL }, { "fnstcw", AI_X87, 0xD9, 7, 0, 0, NULL },
	{ "fstcw", AI_X87, 0xD9, 7, 1, 0, NULL }, { "fnstsw", AI_X87, 0xDD, 7, 0, 0, NULL },
	{ "fstsw", AI_X87, 0xDD, 7, 1, 0, NULL },
	{ "filds", AI_X87, 0xDF, 0, 0, 0, NULL }, { "fildl", AI_X87, 0xDB, 0, 0, 0, NULL },
	{ "fildll", AI_X87, 0xDF, 5, 0, 0, NULL }, { "fildq", AI_X87, 0xDF, 5, 0, 0, NULL },
	{ "fninit", AI_FIXED, 0, 0, 0, 0, "\xDB\xE3" }, { "finit", AI_FIXED, 0, 0, 0, 0, "\x9B\xDB\xE3" },
	{ "fnclex", AI_FIXED, 0, 0, 0, 0, "\xDB\xE2" }, { "fclex", AI_FIXED, 0, 0, 0, 0, "\x9B\xDB\xE2" },
	{ "fwait", AI_FIXED, 0, 0, 0, 0, "\x9B" }, { "wait", AI_FIXED, 0, 0, 0, 0, "\x9B" },
	{ "emms", AI_FIXED, 0, 0, 0, 0, "\x0F\x77" },
	{ "vmcall", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC1" }, { "vmmcall", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xD9" },
	{ "vmlaunch", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC2" }, { "vmresume", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC3" },
	{ "vmxoff", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xC4" },
	{ "stgi", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xDC" }, { "clgi", AI_FIXED, 0, 0, 0, 0, "\x0F\x01\xDD" },
	{ "flds", AI_X87, 0xD9, 0, 0, 0, NULL }, { "fldl", AI_X87, 0xDD, 0, 0, 0, NULL },
	{ "fadds", AI_X87, 0xD8, 0, 0, 0, NULL }, { "faddl", AI_X87, 0xDC, 0, 0, 0, NULL },
	{ "fmuls", AI_X87, 0xD8, 1, 0, 0, NULL }, { "fmull", AI_X87, 0xDC, 1, 0, 0, NULL },
	{ "fcoms", AI_X87, 0xD8, 2, 0, 0, NULL }, { "fcoml", AI_X87, 0xDC, 2, 0, 0, NULL },
	{ "fcomps", AI_X87, 0xD8, 3, 0, 0, NULL }, { "fcompl", AI_X87, 0xDC, 3, 0, 0, NULL },
	{ "fsubs", AI_X87, 0xD8, 4, 0, 0, NULL }, { "fsubl", AI_X87, 0xDC, 4, 0, 0, NULL },
	{ "fsubrs", AI_X87, 0xD8, 5, 0, 0, NULL }, { "fsubrl", AI_X87, 0xDC, 5, 0, 0, NULL },
	{ "fdivs", AI_X87, 0xD8, 6, 0, 0, NULL }, { "fdivl", AI_X87, 0xDC, 6, 0, 0, NULL },
	{ "fdivrs", AI_X87, 0xD8, 7, 0, 0, NULL }, { "fdivrl", AI_X87, 0xDC, 7, 0, 0, NULL },
	{ "faddp", AI_X87R, 0xDE, 0xC0, 2, 0, NULL },
	{ "fmulp", AI_X87R, 0xDE, 0xC8, 2, 0, NULL },
	{ "fsubp", AI_X87R, 0xDE, 0xE0, 2, 0, NULL },
	{ "fsubrp", AI_X87R, 0xDE, 0xE8, 2, 0, NULL },
	{ "fdivp", AI_X87R, 0xDE, 0xF0, 2, 0, NULL },
	{ "fdivrp", AI_X87R, 0xDE, 0xF8, 2, 0, NULL },
	{ "fld", AI_X87R, 0xD9, 0xC0, 0, 0, NULL }, { "fstp", AI_X87R, 0xDD, 0xD8, 0, 0, NULL },
	{ "fxch", AI_X87R, 0xD9, 0xC8, 1, 0, NULL },
	{ "f2xm1", AI_FIXED, 0, 0, 0, 0, "\xD9\xF0" },
	{ "fabs", AI_FIXED, 0, 0, 0, 0, "\xD9\xE1" },
	{ "fchs", AI_FIXED, 0, 0, 0, 0, "\xD9\xE0" },
	{ "fcompp", AI_FIXED, 0, 0, 0, 0, "\xDE\xD9" },
	{ "fcos", AI_FIXED, 0, 0, 0, 0, "\xD9\xFF" },
	{ "fdecstp", AI_FIXED, 0, 0, 0, 0, "\xD9\xF6" },
	{ "fincstp", AI_FIXED, 0, 0, 0, 0, "\xD9\xF7" },
	{ "fldl2t", AI_FIXED, 0, 0, 0, 0, "\xD9\xE9" },
	{ "fldl2e", AI_FIXED, 0, 0, 0, 0, "\xD9\xEA" },
	{ "fldpi", AI_FIXED, 0, 0, 0, 0, "\xD9\xEB" },
	{ "fldlg2", AI_FIXED, 0, 0, 0, 0, "\xD9\xEC" },
	{ "fldln2", AI_FIXED, 0, 0, 0, 0, "\xD9\xED" },
	{ "fnop", AI_FIXED, 0, 0, 0, 0, "\xD9\xD0" },
	{ "fpatan", AI_FIXED, 0, 0, 0, 0, "\xD9\xF3" },
	{ "fprem", AI_FIXED, 0, 0, 0, 0, "\xD9\xF8" },
	{ "fprem1", AI_FIXED, 0, 0, 0, 0, "\xD9\xF5" },
	{ "fptan", AI_FIXED, 0, 0, 0, 0, "\xD9\xF2" },
	{ "frndint", AI_FIXED, 0, 0, 0, 0, "\xD9\xFC" },
	{ "fscale", AI_FIXED, 0, 0, 0, 0, "\xD9\xFD" },
	{ "fsin", AI_FIXED, 0, 0, 0, 0, "\xD9\xFE" },
	{ "fsincos", AI_FIXED, 0, 0, 0, 0, "\xD9\xFB" },
	{ "fsqrt", AI_FIXED, 0, 0, 0, 0, "\xD9\xFA" },
	{ "ftst", AI_FIXED, 0, 0, 0, 0, "\xD9\xE4" },
	{ "fucompp", AI_FIXED, 0, 0, 0, 0, "\xDA\xE9" },
	{ "fxam", AI_FIXED, 0, 0, 0, 0, "\xD9\xE5" },
	{ "fxtract", AI_FIXED, 0, 0, 0, 0, "\xD9\xF4" },
	{ "fyl2x", AI_FIXED, 0, 0, 0, 0, "\xD9\xF1" },
	{ "fyl2xp1", AI_FIXED, 0, 0, 0, 0, "\xD9\xF9" },
	{ "fadd", AI_X87A, 0xC0, 0, 0, 0, NULL },
	{ "fmul", AI_X87A, 0xC8, 0, 0, 0, NULL },
	{ "fsub", AI_X87A, 0xE0, 0, 0, 0, NULL },
	{ "fsubr", AI_X87A, 0xE8, 0, 0, 0, NULL },
	{ "fdiv", AI_X87A, 0xF0, 0, 0, 0, NULL },
	{ "fdivr", AI_X87A, 0xF8, 0, 0, 0, NULL },
	{ "fst", AI_X87R, 0xDD, 0xD0, 0, 0, NULL },
	{ "ffree", AI_X87R, 0xDD, 0xC0, 0, 0, NULL },
	{ "fucom", AI_X87R, 0xDD, 0xE0, 1, 0, NULL },
	{ "fucomp", AI_X87R, 0xDD, 0xE8, 1, 0, NULL },
	{ "fcom", AI_X87R, 0xD8, 0xD0, 1, 0, NULL },
	{ "fcomp", AI_X87R, 0xD8, 0xD8, 1, 0, NULL },
	{ "fcomi", AI_X87R, 0xDB, 0xF0, 2, 0, NULL },
	{ "fucomi", AI_X87R, 0xDB, 0xE8, 2, 0, NULL },
	{ "fcomip", AI_X87R, 0xDF, 0xF0, 2, 0, NULL },
	{ "fucomip", AI_X87R, 0xDF, 0xE8, 2, 0, NULL },
	{ "fcmovb", AI_X87R, 0xDA, 0xC0, 2, 0, NULL },
	{ "fcmove", AI_X87R, 0xDA, 0xC8, 2, 0, NULL },
	{ "fcmovbe", AI_X87R, 0xDA, 0xD0, 2, 0, NULL },
	{ "fcmovu", AI_X87R, 0xDA, 0xD8, 2, 0, NULL },
	{ "fcmovnb", AI_X87R, 0xDB, 0xC0, 2, 0, NULL },
	{ "fcmovne", AI_X87R, 0xDB, 0xC8, 2, 0, NULL },
	{ "fcmovnbe", AI_X87R, 0xDB, 0xD0, 2, 0, NULL },
	{ "fcmovnu", AI_X87R, 0xDB, 0xD8, 2, 0, NULL },
	{ "fiadds", AI_X87, 0xDE, 0, 0, 0, NULL },
	{ "fiaddl", AI_X87, 0xDA, 0, 0, 0, NULL },
	{ "fimuls", AI_X87, 0xDE, 1, 0, 0, NULL },
	{ "fimull", AI_X87, 0xDA, 1, 0, 0, NULL },
	{ "ficoms", AI_X87, 0xDE, 2, 0, 0, NULL },
	{ "ficoml", AI_X87, 0xDA, 2, 0, 0, NULL },
	{ "ficomps", AI_X87, 0xDE, 3, 0, 0, NULL },
	{ "ficompl", AI_X87, 0xDA, 3, 0, 0, NULL },
	{ "fisubs", AI_X87, 0xDE, 4, 0, 0, NULL },
	{ "fisubl", AI_X87, 0xDA, 4, 0, 0, NULL },
	{ "fisubrs", AI_X87, 0xDE, 5, 0, 0, NULL },
	{ "fisubrl", AI_X87, 0xDA, 5, 0, 0, NULL },
	{ "fidivs", AI_X87, 0xDE, 6, 0, 0, NULL },
	{ "fidivl", AI_X87, 0xDA, 6, 0, 0, NULL },
	{ "fidivrs", AI_X87, 0xDE, 7, 0, 0, NULL },
	{ "fidivrl", AI_X87, 0xDA, 7, 0, 0, NULL },
	{ "fists", AI_X87, 0xDF, 2, 0, 0, NULL },
	{ "fistps", AI_X87, 0xDF, 3, 0, 0, NULL },
	{ "fisttps", AI_X87, 0xDF, 1, 0, 0, NULL },
	{ "fisttpl", AI_X87, 0xDB, 1, 0, 0, NULL },
	{ "fisttpll", AI_X87, 0xDD, 1, 0, 0, NULL },
	{ "fisttpq", AI_X87, 0xDD, 1, 0, 0, NULL },
	{ "fbld", AI_X87, 0xDF, 4, 0, 0, NULL },
	{ "fbstp", AI_X87, 0xDF, 6, 0, 0, NULL },
	{ "fild", AI_X87, 0xDF, 0, 0, 0, NULL },
	{ "fist", AI_X87, 0xDF, 2, 0, 0, NULL },
	{ "fistp", AI_X87, 0xDF, 3, 0, 0, NULL },
	{ "fiadd", AI_X87, 0xDE, 0, 0, 0, NULL },
	{ "fimul", AI_X87, 0xDE, 1, 0, 0, NULL },
	{ "ficom", AI_X87, 0xDE, 2, 0, 0, NULL },
	{ "ficomp", AI_X87, 0xDE, 3, 0, 0, NULL },
	{ "fisub", AI_X87, 0xDE, 4, 0, 0, NULL },
	{ "fisubr", AI_X87, 0xDE, 5, 0, 0, NULL },
	{ "fidiv", AI_X87, 0xDE, 6, 0, 0, NULL },
	{ "fidivr", AI_X87, 0xDE, 7, 0, 0, NULL },
	{ "fisttp", AI_X87, 0xDF, 1, 0, 0, NULL },
	{ "fldz", AI_FIXED, 0, 0, 0, 0, "\xD9\xEE" }, { "fld1", AI_FIXED, 0, 0, 0, 0, "\xD9\xE8" },
	{ "fldt", AI_X87, 0xDB, 5, 0, 0, NULL }, { "fsts", AI_X87, 0xD9, 2, 0, 0, NULL },
	{ "fstl", AI_X87, 0xDD, 2, 0, 0, NULL }, { "fstps", AI_X87, 0xD9, 3, 0, 0, NULL },
	{ "fstpl", AI_X87, 0xDD, 3, 0, 0, NULL }, { "fstpt", AI_X87, 0xDB, 7, 0, 0, NULL },
	{ "fistl", AI_X87, 0xDB, 2, 0, 0, NULL }, { "fistpl", AI_X87, 0xDB, 3, 0, 0, NULL },
	{ "fistpll", AI_X87, 0xDF, 7, 0, 0, NULL }, { "fistpq", AI_X87, 0xDF, 7, 0, 0, NULL },
	{ "movnti", AI_0F, 0xC3, 0, 0, F_SUF | F_REGSRC, NULL },
	{ "prefetchnta", AI_GRP, 0, 0, 0, F_MEM, "\x0F\x18" }, { "prefetcht0", AI_GRP, 0, 1, 0, F_MEM, "\x0F\x18" },
	{ "prefetcht1", AI_GRP, 0, 2, 0, F_MEM, "\x0F\x18" }, { "prefetcht2", AI_GRP, 0, 3, 0, F_MEM, "\x0F\x18" },
	{ "prefetch", AI_GRP, 0, 0, 0, F_MEM, "\x0F\x0D" }, { "prefetchw", AI_GRP, 0, 1, 0, F_MEM, "\x0F\x0D" },
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
	{ "pcmpeqb", AI_SSE, 0x74, 0, 0, 0, "\x66" }, { "pcmpeqw", AI_SSE, 0x75, 0, 0, 0, "\x66" },
	{ "pcmpeqd", AI_SSE, 0x76, 0, 0, 0, "\x66" }, { "pcmpgtb", AI_SSE, 0x64, 0, 0, 0, "\x66" },
	{ "pcmpgtw", AI_SSE, 0x65, 0, 0, 0, "\x66" }, { "pcmpgtd", AI_SSE, 0x66, 0, 0, 0, "\x66" },
	{ "psrlw", AI_SSESH, 0xD1, 0x71, 2, 0, "\x66" }, { "psrld", AI_SSESH, 0xD2, 0x72, 2, 0, "\x66" },
	{ "psrlq", AI_SSESH, 0xD3, 0x73, 2, 0, "\x66" }, { "psraw", AI_SSESH, 0xE1, 0x71, 4, 0, "\x66" },
	{ "psrad", AI_SSESH, 0xE2, 0x72, 4, 0, "\x66" }, { "psllw", AI_SSESH, 0xF1, 0x71, 6, 0, "\x66" },
	{ "pslld", AI_SSESH, 0xF2, 0x72, 6, 0, "\x66" }, { "psllq", AI_SSESH, 0xF3, 0x73, 6, 0, "\x66" },
	{ "psrldq", AI_SSESH, 0, 0x73, 3, 0, "\x66" }, { "pslldq", AI_SSESH, 0, 0x73, 7, 0, "\x66" },
	{ "pmovmskb", AI_CVTF, 0xD7, 0, 0, 0, "\x66" },
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
		if (i < 8) { snprintf(b, sizeof(b), "db%d", i); as_reg_add(b, RC_DR, i, 8, 0); }
		if (i < 8) { snprintf(b, sizeof(b), "st(%d)", i); as_reg_add(b, RC_ST, i, 10, 0); }
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

static char *as_lower(char *name) {
	char b[64];
	int n = (int)strlen(name), up = 0;
	if (n >= (int)sizeof(b)) return name;
	for (int i = 0; i < n; i++) { b[i] = (char)tolower((unsigned char)name[i]); up |= b[i] != name[i]; }
	return up ? intern_str(b, n)->s : name;
}

static const AsIns *as_find1(char *name, int *suffix);

static const AsIns *as_find(char *name, int *suffix) {
	const AsIns *in = as_find1(name, suffix);
	char *l = as_lower(name);
	if (!in && l != name) in = as_find1(l, suffix);
	return in;
}

static const AsIns *as_find1(char *name, int *suffix) {
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

static Tok as_raw(As *A) {
	while (A->nxs) {
		int i = A->nxs - 1;
		if (A->xs[i].L) {
			Tok t = lex_next(A->xs[i].L);
			if (A->xs[i].first) { t.bol = 1; A->xs[i].first = 0; }
			if (t.kind != T_EOF) return t;
			free((char *)A->xs[i].L->s);
			free(A->xs[i].L);
			A->xs[i].L = NULL;
		}
		if (A->xs[i].iq < A->xs[i].nq) return A->xs[i].q[A->xs[i].iq++];
		A->nxs--;
	}
	return A->lx ? lex_next(A->lx) : pp_next(A->ctx);
}

static void as_next(As *A) {
	if (A->has_pk) { A->tok = A->pk; A->has_pk = 0; }
	else A->tok = as_raw(A);
	A->nl = A->tok.bol;
	if (A->snip) {
		A->ctx->tok = A->tok;
		A->ctx->tok.file = A->sfile;
		A->ctx->tok.line = A->sline;
	} else if (!A->nl) A->ctx->tok = A->tok;
}

static Tok *as_peek(As *A) {
	if (!A->has_pk) { A->pk = as_raw(A); A->has_pk = 1; }
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
	int left = 0, later = 0;
	for (int i = 0; i < na; i++) if (adds[i]) { left++; later |= !as_defined(A, adds[i]); }
	for (int i = 0; i < ns; i++) if (subs[i]) { left += 4; later |= !as_defined(A, subs[i]); }
	if ((left & 3) > 1 && later && !A->final) return as_tree(T_PLUS, a, b);
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
	case T_LT: a = -(a < b); break;
	case T_GT: a = -(a > b); break;
	case T_LEQ: a = -(a <= b); break;
	case T_GEQ: a = -(a >= b); break;
	case T_EQ: a = -(a == b); break;
	case T_NEQ: a = -(a != b); break;
	case T_AND: a = a && b; break;
	case T_OR: a = a || b; break;
	default: error_at(A->ctx, "invalid operator in expression");
	}
	return as_const(a);
}

static AsVal as_binary(As *A, int minprec);

static const AsReg *as_reg(As *A);

static AsVal as_primary(As *A) {
	Tok t = A->tok;
	if (as_at_end(A)) error_at(A->ctx, "expected an expression");
	if (t.kind == T_NUM) { as_next(A); return as_const(t.val); }
	if (t.kind == '%' && as_peek(A)->kind == T_IDENT) {
		const AsReg *r = as_reg(A);
		return as_const(((long long)1 << 40) | ((long long)r->rc << 24) | (r->num << 16) | (r->size << 8) | r->hi);
	}
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
		if (as_is(A, T_AT) && !A->nl && as_peek(A)->kind == T_IDENT && (!strcmp(as_peek(A)->str, "PLT") || !strcmp(as_peek(A)->str, "plt"))) { as_next(A); as_next(A); }
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

static long long as_sizeexpr(As *A) {
	AsVal v = as_expr(A);
	as_fold(A, &v);
	if (A->nsz == A->capsz) {
		A->capsz = A->capsz ? A->capsz * 2 : 16;
		A->szexpr = (AsVal *)realloc(A->szexpr, sizeof(AsVal) * A->capsz);
		A->szval = (long long *)realloc(A->szval, sizeof(long long) * A->capsz);
		A->szprev = (long long *)realloc(A->szprev, sizeof(long long) * A->capsz);
		if (!A->szexpr || !A->szval || !A->szprev) { perror("realloc"); exit(1); }
	}
	int i = A->nsz++;
	A->szexpr[i] = v;
	A->szval[i] = as_known(&v) ? v.v : i < A->nszprev ? A->szprev[i] : 0;
	return A->szval[i];
}

static void as_settle(As *A) {
	for (int i = 0; i < A->nsz; i++) {
		AsVal v = A->szexpr[i];
		as_fold(A, &v);
		if (!as_known(&v)) error_at(A->ctx, "expression must be constant here");
		if (v.v != A->szval[i]) A->changed = 1;
		A->szprev[i] = v.v;
	}
	A->nszprev = A->nsz;
	if (A->changed && A->pass > 64) error_at(A->ctx, "the sizes in this assembly do not settle");
}

static long long as_absexpr(As *A) {
	AsVal v = as_expr(A);
	if (!as_known(&v)) error_at(A->ctx, "expression must be constant here");
	return v.v;
}

static const AsReg *as_reg(As *A) {
	as_expect(A, '%');
	if (!as_is(A, T_IDENT)) error_at(A->ctx, "expected a register name after '%%'");
	char *l = as_lower(A->tok.str);
	if (!strcmp(l, "st")) {
		int n = 0;
		as_next(A);
		if (as_is(A, T_LP) && as_peek(A)->kind == T_NUM) {
			as_next(A);
			n = (int)A->tok.val;
			as_next(A);
			as_expect(A, T_RP);
		}
		if (n < 0 || n > 7) error_at(A->ctx, "invalid x87 register");
		char b[8];
		snprintf(b, sizeof(b), "st(%d)", n);
		char *id = intern(b);
		for (int i = 0; i < nas_regs; i++) if (as_regs[i].name == id) return &as_regs[i];
	}
	for (int i = 0; i < nas_regs; i++)
		if (as_regs[i].name == A->tok.str || as_regs[i].name == l) { as_next(A); return &as_regs[i]; }
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
	if (size == 4 && A->mode != 64) { lo = -0xFFFFFFFFLL; hi = 0xFFFFFFFFLL; }
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
	if (s->pass && s->pass == A->pass - 1) return s->sect == A->sect;
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
	case AI_X87A: {
		int b = (uint8_t)in->op;
		if (n == 1 && o[0].kind == OP_MEM) {
			op[0] = (char)0xD8;
			as_rm(A, 0, 0, op, 1, (b - 0xC0) >> 3, NULL, &o[0], 0);
			break;
		}
		int pre = 0xDE, r = 1;
		if (n == 2 && as_isreg(&o[0], RC_ST) && as_isreg(&o[1], RC_ST) && (!o[0].reg || !o[1].reg)) {
			pre = o[1].reg ? 0xDC : 0xD8;
			r = o[1].reg ? o[1].reg : o[0].reg;
		} else if (n == 1 && as_isreg(&o[0], RC_ST)) {
			pre = 0xD8;
			r = o[0].reg;
		} else if (n != 0) error_at(ctx, "invalid operands for %s", name);
		as_head(A, 0, NULL);
		C8(c, (uint8_t)pre);
		C8(c, (uint8_t)(b + r));
		break;
	}
	case AI_X87R: {
		int r = in->sz ? 1 : -1;
		if (n == 1 && o[0].kind == OP_MEM && ((in->op == 0xD9 && in->ext == 0xC0) || (in->op == 0xDD && (in->ext == 0xD0 || in->ext == 0xD8)))) {
			op[0] = (char)0xD9;
			as_rm(A, 0, 0, op, 1, in->ext == 0xC0 ? 0 : in->ext == 0xD0 ? 2 : 3, NULL, &o[0], 0);
			break;
		}
		if (n == 2 && in->sz == 2 && as_isreg(&o[0], RC_ST) && as_isreg(&o[1], RC_ST) && (!o[0].reg || !o[1].reg)) r = o[0].reg ? o[0].reg : o[1].reg;
		else if (n == 1 && as_isreg(&o[0], RC_ST)) r = o[0].reg;
		else if (n != 0 || r < 0) error_at(ctx, "invalid operands for %s", name);
		as_head(A, 0, NULL);
		C8(c, in->op);
		C8(c, (uint8_t)(in->ext + r));
		break;
	}
	case AI_X87:
		as_nops(A, name, n, 1);
		if (in->ext == 7 && in->op == 0xDD && as_isacc(&o[0]) && o[0].size == 2) {
			if (in->sz) C8(c, 0x9B);
			C8(c, 0xDF); C8(c, 0xE0);
			break;
		}
		if (o[0].kind != OP_MEM) error_at(ctx, "invalid operand for %s", name);
		if (in->sz) C8(c, 0x9B);
		op[0] = (char)in->op;
		as_rm(A, 0, 0, op, 1, in->ext, NULL, &o[0], 0);
		break;
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
		if (in->ext && as_isreg(m, RC_GP) && m->size == 2 && r->size > 2 && (!suf || suf == r->size)) sz = r->size;
		else sz = as_sz(A, suf, o, 2);
		if (sz == 1 && !(in->flags & F_BYTE)) error_at(ctx, "%s has no byte form", name);
		op[0] = 0x0F; op[1] = (char)(in->op - (sz == 1));
		if (in->ext && sz == 8) sz = 4;
		if (in->op == 0xC3 && sz == 4) sz = 0;
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
		if ((uint8_t)in->code[0] == 0xF3) {
			if (o[0].kind != OP_REG || (o[0].size != 8 && !(o[0].size == 4 && A->mode != 64))) error_at(ctx, "invalid operand for %s", name);
			as_rm(A, 0, 0xF3, in->code + 1, 2, in->ext, NULL, &o[0], 0);
			break;
		}
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
	case AI_SSESH:
		as_nops(A, name, n, 2);
		if (!as_isreg(&o[1], RC_XMM)) error_at(ctx, "invalid operands for %s", name);
		op[0] = 0x0F;
		if (o[0].kind == OP_IMM) {
			op[1] = (char)in->ext;
			as_rm(A, 0, 0x66, op, 2, in->sz, NULL, &o[1], 1);
			as_imm(A, &o[0].val, 1, 0);
		} else if (in->op && (as_isreg(&o[0], RC_XMM) || o[0].kind == OP_MEM)) {
			op[1] = (char)in->op;
			as_rm(A, 0, 0x66, op, 2, 0, &o[1], &o[0], 0);
		} else error_at(ctx, "invalid operands for %s", name);
		break;
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
	int kind = strncmp(name, ".text", 5) == 0 || !strcmp(name, ".init") || !strcmp(name, ".fini") ? SK_CODE : strncmp(name, ".rodata", 7) == 0 ? SK_RO :
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
	if (f == 0x90 && A->mode == 64 && as_is_code(A->ctx, A->sect)) {
		static const char *nops[12] = { "", "\x90", "\x66\x90", "\x0F\x1F\x00", "\x0F\x1F\x40\x00",
			"\x0F\x1F\x44\x00\x00", "\x66\x0F\x1F\x44\x00\x00", "\x0F\x1F\x80\x00\x00\x00\x00",
			"\x0F\x1F\x84\x00\x00\x00\x00\x00", "\x66\x0F\x1F\x84\x00\x00\x00\x00\x00",
			"\x66\x2E\x0F\x1F\x84\x00\x00\x00\x00\x00", "\x66\x66\x2E\x0F\x1F\x84\x00\x00\x00\x00\x00" };
		long r = pad % 11, q = pad / 11;
		for (long i = 0; i < r; i++) as_byte(A, (uint8_t)nops[r][i]);
		while (q-- > 0)
			for (int i = 0; i < 11; i++) as_byte(A, (uint8_t)nops[11][i]);
		return;
	}
	if (f == 0x90 && pad > 0 && as_is_code(A->ctx, A->sect)) {
		static const char *n16[6] = { "", "\x90", "\x89\xF6", "\x8D\x74\x00", "\x8D\xB4\x00\x00", "\x2E\x8D\xB4\x00\x00" };
		static const char *n32[9] = { "", "\x90", "\x66\x90", "\x8D\x76\x00", "\x8D\x74\x26\x00", "\x2E\x8D\x74\x26\x00",
			"\x8D\xB6\x00\x00\x00\x00", "\x8D\xB4\x26\x00\x00\x00\x00", "\x2E\x8D\xB4\x26\x00\x00\x00\x00" };
		const char **t = A->mode == 16 ? n16 : n32;
		long m = A->mode == 16 ? 5 : 8;
		if (pad >= 3 * m && pad - 2 <= 127) { as_byte(A, 0xEB); as_byte(A, (int)(pad - 2)); pad -= 2; }
		long r = pad % m, q = pad / m;
		for (long i = 0; i < r; i++) as_byte(A, (uint8_t)t[r][i]);
		while (q-- > 0)
			for (long i = 0; i < m; i++) as_byte(A, (uint8_t)t[m][i]);
		return;
	}
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
	} else if (A->snip && v.add && !v.sub && !v.t && !v.v && !as_defined(A, v.add)) {
		s->alias = v.add->name;
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

typedef struct { int a, b; } AsRange;

static void as_tok_text(Buf *b, Tok *t) {
	if (t->raw && t->rawlen) buf_append(b, t->raw, (size_t)t->rawlen);
	else tok_spell(b, t);
}

static int as_line_toks(As *A, Tok **out) {
	int n = 0, cap = 16;
	Tok *v = (Tok *)malloc(sizeof(Tok) * cap);
	while (!as_at_end(A)) {
		if (n == cap) v = (Tok *)realloc(v, sizeof(Tok) * (cap *= 2));
		v[n++] = A->tok;
		as_next(A);
	}
	*out = v;
	return n;
}

static int as_joins(int k) {
	return k == '+' || k == '*' || k == '/' || k == '|' || k == '&' || k == '^' || k == '=' || k == '<' || k == '>' ||
		k == T_SHL || k == T_SHR || k == T_EQ || k == T_NEQ || k == T_LEQ || k == T_GEQ || k == T_AND || k == T_OR || k == ':';
}

static int as_split(Tok *v, int n, AsRange **out) {
	int na = 0, cap = 8, depth = 0, a = 0;
	AsRange *r = (AsRange *)malloc(sizeof(AsRange) * cap);
	if (!n) { *out = r; return 0; }
	for (int i = 0; i <= n; i++) {
		int end = i == n;
		if (!end && depth == 0 && v[i].kind == T_COMMA) end = 1;
		else if (!end && depth == 0 && i > a && v[i].space) {
			int pk = v[i-1].kind, k = v[i].kind;
			int nospace_after = i + 1 < n && !v[i+1].space;
			if (pk != ')' && !as_joins(pk) && !as_joins(k) && (k != '-' || nospace_after)) end = 2;
		}
		if (end) {
			if (na == cap) r = (AsRange *)realloc(r, sizeof(AsRange) * (cap *= 2));
			r[na].a = a; r[na].b = i; na++;
			if (end == 2) { a = i; } else a = i + 1;
			if (end == 2) { if (v[i].kind == '(') depth++; continue; }
			continue;
		}
		if (v[i].kind == '(' || v[i].kind == '[') depth++;
		else if (v[i].kind == ')' || v[i].kind == ']') depth--;
	}
	*out = r;
	return na;
}

static int as_wordy(int k) { return k == T_IDENT || k == T_NUM || k == T_FNUM || k == T_STR; }

static char *as_range_text(Tok *v, AsRange r, int unquote) {
	Buf b; buf_init(&b);
	if (unquote && r.b == r.a + 1 && v[r.a].kind == T_STR) {
		Tok *t = &v[r.a];
		if (t->raw && t->rawlen >= 2 && t->raw[0] == '"') buf_append(&b, t->raw + 1, (size_t)t->rawlen - 2);
		else buf_append(&b, t->str, (size_t)t->len);
	} else
		for (int i = r.a; i < r.b; i++) {
			if (i > r.a && v[i].space && as_wordy(v[i-1].kind) && as_wordy(v[i].kind)) buf_append_byte(&b, ' ');
			as_tok_text(&b, &v[i]);
		}
	buf_append_byte(&b, 0);
	return (char *)b.data;
}

static char *as_capture(As *A, int endr) {
	Buf b; buf_init(&b);
	int depth = 0, semi = 0;
	for (;;) {
		Tok t = A->tok;
		if (t.kind == T_EOF) error_at(A->ctx, endr ? "missing .endr" : "missing .endm");
		int start = A->nl || !b.len || semi;
		semi = t.kind == T_SEMI;
		if (start && t.kind == T_IDENT) {
			const char *d = t.str;
			if (endr ? !strcmp(d, ".rept") || !strcmp(d, ".irp") || !strcmp(d, ".irpc") : !strcmp(d, ".macro")) depth++;
			else if (!strcmp(d, endr ? ".endr" : ".endm") && !depth--) {
				as_next(A);
				buf_append_byte(&b, '\n');
				buf_append_byte(&b, 0);
				return (char *)b.data;
			}
		}
		if (t.bol && b.len) buf_append_byte(&b, '\n');
		else if (t.space) buf_append_byte(&b, ' ');
		as_tok_text(&b, &t);
		as_next(A);
	}
}

static void as_subst(Buf *b, const char *body, int n, char **names, char **vals, int count) {
	for (const char *p = body; *p; ) {
		if (*p == '\\') {
			if (p[1] == '@') { char t[24]; snprintf(t, sizeof(t), "%d", count); buf_append(b, t, strlen(t)); p += 2; continue; }
			if (p[1] == '(' && p[2] == ')') { p += 3; continue; }
			int len = 0;
			while (isalnum((unsigned char)p[1 + len]) || p[1 + len] == '_') len++;
			int hit = -1;
			for (int i = 0; i < n && hit < 0; i++)
				if ((int)strlen(names[i]) == len && !strncmp(names[i], p + 1, (size_t)len)) hit = i;
			if (hit >= 0) { buf_append(b, vals[hit], strlen(vals[hit])); p += 1 + len; continue; }
		}
		buf_append_byte(b, (uint8_t)*p++);
	}
}

static void as_push_text(As *A, Buf *b, int macro) {
	if (A->nxs == 64) error_at(A->ctx, "macros nested too deeply");
	buf_append_byte(b, 0);
	int i = A->nxs++;
	Lex *L = (Lex *)calloc(1, sizeof(Lex));
	lex_init(L, (char *)b->data, A->ctx->tok.file ? A->ctx->tok.file : "<macro>");
	L->as = 2;
	L->line = A->ctx->tok.line;
	A->xs[i].L = L;
	A->xs[i].first = 1;
	A->xs[i].macro = macro;
	A->xs[i].nq = A->xs[i].iq = 0;
	A->xs[i].q[A->xs[i].nq++] = A->tok;
	if (A->has_pk) A->xs[i].q[A->xs[i].nq++] = A->pk;
	A->has_pk = 0;
	as_next(A);
}

static AsMacro *as_find_macro(Ctx *ctx, char *name) {
	for (AsMacro *m = ctx->as_macros; m; m = m->next)
		if (m->name == name) return m;
	return NULL;
}

static void as_macro_def(As *A) {
	Ctx *ctx = A->ctx;
	if (A->tok.kind != T_IDENT || as_at_end(A)) error_at(ctx, "expected a macro name");
	char *name = A->tok.str;
	as_next(A);
	if (as_is(A, T_COMMA)) as_next(A);
	Tok *v;
	int n = as_line_toks(A, &v);
	AsRange *r;
	int na = as_split(v, n, &r);
	AsMacro *m = (AsMacro *)calloc(1, sizeof(AsMacro));
	m->name = name;
	m->pname = (char **)calloc((size_t)na + 1, sizeof(char *));
	m->pdef = (char **)calloc((size_t)na + 1, sizeof(char *));
	m->req = (int *)calloc((size_t)na + 1, sizeof(int));
	m->vararg = (int *)calloc((size_t)na + 1, sizeof(int));
	for (int i = 0; i < na; i++) {
		int a = r[i].a, b = r[i].b;
		if (a == b) continue;
		if (v[a].kind != T_IDENT) error_at(ctx, "expected a macro parameter name");
		int k = m->n++;
		m->pname[k] = v[a].str;
		a++;
		if (a < b && v[a].kind == T_COLON && a + 1 < b && v[a+1].kind == T_IDENT) {
			if (!strcmp(v[a+1].str, "req")) m->req[k] = 1;
			else if (!strcmp(v[a+1].str, "vararg")) m->vararg[k] = 1;
			else error_at(ctx, "unknown macro parameter qualifier '%s'", v[a+1].str);
			a += 2;
		}
		if (a < b && v[a].kind == T_ASSIGN) { AsRange d = { a + 1, b }; m->pdef[k] = as_range_text(v, d, 1); }
		else if (a < b) error_at(ctx, "bad macro parameter '%s'", m->pname[k]);
		else m->pdef[k] = "";
	}
	free(v);
	free(r);
	m->body = as_capture(A, 0);
	AsMacro **pp = &ctx->as_macros;
	while (*pp && (*pp)->name != name) pp = &(*pp)->next;
	if (*pp) { m->next = (*pp)->next; *pp = m; }
	else { m->next = ctx->as_macros; ctx->as_macros = m; }
}

static void as_macro_call(As *A, AsMacro *m) {
	Ctx *ctx = A->ctx;
	Tok *v;
	int n = as_line_toks(A, &v);
	AsRange *r;
	int na = as_split(v, n, &r);
	char **vals = (char **)calloc((size_t)m->n + 1, sizeof(char *));
	int pos = 0;
	for (int i = 0; i < na; i++) {
		int a = r[i].a, b = r[i].b;
		if (b > a + 1 && v[a].kind == T_IDENT && v[a+1].kind == T_ASSIGN) {
			int k = -1;
			for (int j = 0; j < m->n; j++) if (m->pname[j] == v[a].str) k = j;
			if (k >= 0) { AsRange d = { a + 2, b }; vals[k] = as_range_text(v, d, 1); continue; }
		}
		if (pos >= m->n) error_at(ctx, "too many arguments for macro '%s'", m->name);
		if (m->vararg[pos]) {
			AsRange d = { a, r[na-1].b };
			vals[pos++] = as_range_text(v, d, 0);
			break;
		}
		if (a < b) vals[pos] = as_range_text(v, r[i], 1);
		pos++;
	}
	for (int k = 0; k < m->n; k++)
		if (!vals[k]) {
			if (m->req[k]) error_at(ctx, "missing value for required parameter '%s' of macro '%s'", m->pname[k], m->name);
			vals[k] = m->pdef[k];
		}
	Buf b; buf_init(&b);
	as_subst(&b, m->body, m->n, m->pname, vals, ctx->as_mcount++);
	free(v);
	free(r);
	free(vals);
	as_push_text(A, &b, 1);
}

static void as_repeat(As *A, char *d) {
	Ctx *ctx = A->ctx;
	Buf b; buf_init(&b);
	if (!strcmp(d, ".rept")) {
		long long cnt = as_absexpr(A);
		if (!as_at_end(A)) error_at(ctx, "junk after .rept count");
		char *body = as_capture(A, 1);
		for (long long i = 0; i < cnt; i++) buf_append(&b, body, strlen(body));
		free(body);
	} else {
		if (A->tok.kind != T_IDENT || as_at_end(A)) error_at(ctx, "expected a symbol after %s", d);
		char *name = A->tok.str;
		as_next(A);
		if (as_is(A, T_COMMA)) as_next(A);
		Tok *v;
		int n = as_line_toks(A, &v);
		AsRange *r;
		int na = as_split(v, n, &r);
		char *body = as_capture(A, 1);
		if (!strcmp(d, ".irp")) {
			if (!na) { char *e = ""; as_subst(&b, body, 1, &name, &e, ctx->as_mcount); }
			for (int i = 0; i < na; i++) {
				char *val = as_range_text(v, r[i], 1);
				as_subst(&b, body, 1, &name, &val, ctx->as_mcount);
				free(val);
			}
		} else {
			AsRange all = { 0, n };
			char *str = as_range_text(v, all, 1);
			for (char *p = str; *p; p++) {
				char one[2] = { *p, 0 };
				char *val = one;
				as_subst(&b, body, 1, &name, &val, ctx->as_mcount);
			}
			free(str);
		}
		free(v);
		free(r);
		free(body);
	}
	if (!b.len) { free(b.data); return; }
	as_push_text(A, &b, 0);
}

static void as_exitm(As *A) {
	int i = A->nxs - 1;
	while (i >= 0 && !A->xs[i].macro) i--;
	if (i < 0) error_at(A->ctx, ".exitm outside a macro");
	for (int j = A->nxs - 1; j >= i; j--) if (A->xs[j].L) A->xs[j].L->i = A->xs[j].L->n;
	while (!as_at_end(A)) as_next(A);
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
		A->stack[A->nstack] = A->sect;
		A->pstack[A->nstack++] = A->prev;
		as_enter(A, as_section(A));
	} else if (!strcmp(d, ".popsection")) {
		if (!A->nstack) error_at(ctx, ".popsection without .pushsection");
		as_enter(A, A->stack[--A->nstack]);
		A->prev = A->pstack[A->nstack];
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
		if (as_is(A, T_COMMA)) as_next(A);
		if (as_is(A, T_AT) || as_is(A, '%')) as_next(A);
		if (!as_is(A, T_IDENT) && !as_is(A, T_STR)) error_at(ctx, "expected a symbol type");
		if (!strcmp(A->tok.str, "function") || !strcmp(A->tok.str, "STT_FUNC")) s->type = 1;
		else if (!strcmp(A->tok.str, "object") || !strcmp(A->tok.str, "STT_OBJECT")) s->type = 2;
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
	else if (!strcmp(d, ".zero")) as_fill(A, as_sizeexpr(A), 1, 0);
	else if (!strcmp(d, ".skip") || !strcmp(d, ".space")) {
		a = as_sizeexpr(A);
		b = 0;
		if (as_is(A, T_COMMA)) { as_next(A); b = as_absexpr(A); }
		as_fill(A, a, 1, b);
	} else if (!strcmp(d, ".fill")) {
		a = as_sizeexpr(A);
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
	else if (!strcmp(d, ".macro")) as_macro_def(A);
	else if (!strcmp(d, ".rept") || !strcmp(d, ".irp") || !strcmp(d, ".irpc")) as_repeat(A, d);
	else if (!strcmp(d, ".exitm")) as_exitm(A);
	else if (!strcmp(d, ".purgem")) {
		char *name = as_name(A);
		AsMacro **pp = &A->ctx->as_macros;
		while (*pp && (*pp)->name != name) pp = &(*pp)->next;
		if (!*pp) error_at(A->ctx, "macro '%s' is not defined", name);
		*pp = (*pp)->next;
	} else if (!strcmp(d, ".endm") || !strcmp(d, ".endr")) error_at(A->ctx, "%s without a matching start", d);
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
		} else if (!strcmp(k, "b") || !strcmp(k, "nb")) {
			v = as_at_end(A) == (k[0] == 'b');
			while (!as_at_end(A)) as_next(A);
		} else if (!strcmp(k, "c") || !strcmp(k, "nc") || !strcmp(k, "eqs") || !strcmp(k, "nes")) {
			Tok *tv;
			int n = as_line_toks(A, &tv);
			int i = 0, d = 0;
			for (; i < n; i++) {
				if (tv[i].kind == T_LP) d++;
				else if (tv[i].kind == T_RP) d--;
				else if (tv[i].kind == T_COMMA && !d) break;
			}
			if (i == n) error_at(ctx, "%s needs two operands separated by a comma", d);
			AsRange ra = { 0, i }, rb = { i + 1, n };
			char *x = as_range_text(tv, ra, 1), *y = as_range_text(tv, rb, 1);
			v = !strcmp(x, y) == (k[0] == 'c' || k[0] == 'e');
			free(x);
			free(y);
			free(tv);
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
	AsMacro *m = as_find_macro(ctx, t.str);
	if (m) { as_macro_call(A, m); return 1; }
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
	A->final = 1;
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
			if (!f->pc || f->size != 4) error_at(ctx, "a C label can only be the target of a jump");
			if (f->sect == ctx->cur_text && f->end == f->pos + 4 && !v.v) {
				patches_push(&s->clab->patches, (size_t)f->pos);
				continue;
			}
			patches_push(&s->clab->xrel, (size_t)ctx->nrelocs);
			add_reloc(ctx, R_PC32, (size_t)f->pos, NULL, ctx->cur_text, v.v + 4 - (long long)(f->end - f->pos));
			ctx->relocs[ctx->nrelocs - 1].in = f->sect;
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
			for (Sym *p = global_chain(ctx, y->name); p; p = p->gsame)
				if (!p->is_static && (p->kind == SYM_FUNC || p->kind == SYM_VAR)) { g = p; break; }
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
		if (cpp) push_includes(ctx, 1);
		ctx->has_pk = 0;
		ctx->has_pp_back = 0;
		A->has_pk = 0;
		A->mode = ctx->m32 ? 32 : 64;
		A->sect = A->prev = SEC_TEXT;
		A->c.b = ctx->text.b;
		A->nfix = A->njmp = A->nj = A->nstack = A->npend = A->nbm = A->ncond = A->nsz = 0;
		A->changed = A->done = 0;
		A->bss_name = NULL;
		A->nxs = 0;
		ctx->as_macros = NULL;
		ctx->as_mcount = 0;
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
		as_settle(A);
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
			if (def && s->alias) {
				if (y->defined) error_at(ctx, "redefinition of '%s'", s->name);
				y->defined = 1;
				y->alias_name = s->alias;
				y->asm_alias = 1;
				y->weak_def = s->weak;
			} else if (def) {
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
	int mcount = ctx->as_mcount;
	for (A->pass = 1;; A->pass++) {
		if (A->pass > 1) as_restore(A);
		ctx->as_mcount = mcount;
		A->nxs = 0;
		lex_init(&L, text, file);
		L.as = 2;
		L.line = line;
		A->lx = &L;
		A->has_pk = 0;
		A->mode = 64;
		A->sect = A->prev = ctx->cur_text;
		A->c.b = *sec_buf(ctx, A->sect);
		A->nfix = A->njmp = A->nj = A->nstack = A->npend = A->nsz = 0;
		A->changed = A->done = 0;
		as_next(A);
		while (A->tok.kind != T_EOF && !A->done) {
			if (A->tok.kind == T_SEMI) { as_next(A); continue; }
			if (as_stmt(A)) continue;
			if (!A->done && !as_at_end(A)) error_at(ctx, "unexpected '%s' at the end of an asm statement", tok_desc(&A->tok));
		}
		as_flush(A);
		as_relax(A);
		as_settle(A);
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
	int creg, cmem, cimm, n8, immset, fp;
	int reg, fixed, match;
	Type *ty;
	int slot, vslot, pslot;
	int mem, greg;
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

static int asm_imm_ok(AsmOp *o, long long k) {
	int m = o->immset;
	if (m & (1 << 26)) return 1;
	if ((m & (1 << ('I' - 'A'))) && k >= 0 && k <= 31) return 1;
	if ((m & (1 << ('J' - 'A'))) && k >= 0 && k <= 63) return 1;
	if ((m & (1 << ('K' - 'A'))) && k >= -128 && k <= 127) return 1;
	if ((m & (1 << ('L' - 'A'))) && (k == 0xFF || k == 0xFFFF || k == 0xFFFFFFFFLL)) return 1;
	if ((m & (1 << ('M' - 'A'))) && k >= 0 && k <= 3) return 1;
	if ((m & (1 << ('N' - 'A'))) && k >= 0 && k <= 255) return 1;
	if ((m & (1 << ('O' - 'A'))) && k >= 0 && k <= 127) return 1;
	if ((m & (1 << 28)) && k >= -2147483648LL && k <= 2147483647LL) return 1;
	if ((m & (1 << ('Z' - 'A'))) && k >= 0 && k <= 0xFFFFFFFFLL) return 1;
	return 0;
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
		case 'g': case 'X': o->creg = o->cmem = o->cimm = 1; o->immset |= 1 << 26; break;
		case 'p': o->creg = o->cimm = 1; o->immset |= 1 << 26; break;
		case 'm': case 'o': case 'V': case '<': case '>': o->cmem = 1; break;
		case 'N': o->n8 = 1; o->cimm = 1; o->immset |= 1 << ('N' - 'A'); break;
		case 'i': case 'n': case 's': case 'E': case 'F': o->cimm = 1; o->immset |= 1 << 26; break;
		case 'I': case 'J': case 'K': case 'L': case 'M': case 'O': case 'e': case 'Z':
			o->cimm = 1; o->immset |= 1 << (*c == 'e' ? 28 : *c - 'A'); break;
		case 'x': o->fp = 1; break;
		case 't': case 'f': o->fp = 2; break;
		case 'u': o->fp = 3; break;
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
	if (o->fp) o->creg = o->cmem = o->cimm = 0;
	if (!o->fp && !o->creg && !o->cmem && !o->cimm && o->fixed < 0 && o->match < 0) error_at(ctx, "invalid asm constraint");
}

static void asm_regvar(Ctx *ctx, AsmOp *o) {
	if (!ctx->lv || Cpos(&ctx->text) != ctx->lv_end || ctx->lv_sym) return;
	for (Sym *s = ctx->scope; s; s = s->next)
		if (s->is_local && s->stack_off == ctx->lv_off && s->asm_reg) { o->fixed = s->asm_reg - 1; return; }
}

static Type *asm_scalar(Ctx *ctx, Type *ty, int load) {
	if (ty->kind == TY_STRUCT && (ty->size == 1 || ty->size == 2 || ty->size == 4 || ty->size == 8)) {
		Type *t = ty->size == 1 ? ty_uchar : ty->size == 2 ? ty_ushort : ty->size == 4 ? ty_uint : ty_ulong;
		if (load) emit_load_type(&ctx->text, t);
		return t;
	}
	if (!is_scalar(ty) || ty->size > 8) error_at(ctx, "asm register operand must be a scalar");
	return ty;
}

static int asm_greg(Ctx *ctx, AsmOp *o) {
	if (ctx->tok.kind != T_IDENT || peek(ctx)->kind != T_RP) return 0;
	Sym *s = lookup(ctx, ctx->tok.str);
	if (!s || !s->gregvar) return 0;
	next(ctx);
	o->kind = AK_REG;
	o->greg = 1;
	o->reg = o->fixed = s->gregvar - 1;
	o->ty = s->ty;
	return 1;
}

static void asm_fp_value(Ctx *ctx, AsmOp *o, Type *ty) {
	if (o->fp == 1) {
		if (!is_flonum(ty) || ty->kind == TY_LDOUBLE) error_at(ctx, "an \"x\" asm operand must be float or double");
		o->ty = ty;
	} else {
		if (!is_arith(ty)) error_at(ctx, "an x87 asm operand must be arithmetic");
		gen_cast(ctx, ty, ty_ldouble);
		o->ty = ty_ldouble;
	}
}

static void asm_output(Ctx *ctx, AsmOp *o) {
	Code *c = &ctx->text;
	if (asm_greg(ctx, o)) return;
	if (o->fp) {
		Type *ty = parse_assign(ctx);
		if (!ctx->lv) error_at(ctx, "asm output operand is not an lvalue");
		if (o->fp == 1 ? !is_flonum(ty) || ty->kind == TY_LDOUBLE : ty->kind != TY_LDOUBLE) error_at(ctx, "asm output operand has the wrong type for its constraint");
		kv_forget(ctx, 0);
		o->ty = ty;
		o->kind = AK_REG;
		gen_push(ctx);
		o->slot = ctx->sp;
		if (o->plus) {
			emit_load_type(c, ty);
			gen_push(ctx);
			o->vslot = ctx->sp;
		}
		ctx->lv = 0;
		return;
	}
	Mark m = mark_code(ctx);
	Type *ty = parse_assign(ctx);
	if (!ctx->lv) error_at(ctx, "asm output operand is not an lvalue");
	kv_forget(ctx, 0);
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
	ty = o->ty = asm_scalar(ctx, ty, 0);
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
	if (asm_greg(ctx, o)) return;
	if (o->match >= 0 && outs[o->match].fp) {
		o->fp = outs[o->match].fp;
		asm_fp_value(ctx, o, rval(ctx, parse_assign(ctx)));
		o->kind = AK_REG;
		gen_push(ctx);
		o->slot = ctx->sp;
		return;
	}
	if (o->fp) {
		asm_fp_value(ctx, o, rval(ctx, parse_assign(ctx)));
		o->kind = AK_REG;
		gen_push(ctx);
		o->slot = ctx->sp;
		return;
	}
	if (o->match >= 0) {
		AsmOp *t = &outs[o->match];
		Type *ty = rval(ctx, parse_assign(ctx));
		if (t->kind == AK_MEM) { o->kind = AK_MEM; o->mem = t->mem; o->sym = t->sym; o->off = t->off; o->slot = t->slot; o->match = -1; return; }
		ty = asm_scalar(ctx, ty, 1);
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
			if (asm_imm_ok(o, k)) {
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
	long long kv;
	if (o->cimm && !is_flonum(ty) && expr_const(ctx, &kv) && asm_imm_ok(o, kv)) {
		drop_code(ctx, m);
		o->kind = AK_IMM;
		o->imm = kv;
		o->ty = ty_long;
		return;
	}
	ty = asm_scalar(ctx, ty, 1);
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
	} else if (o->kind == AK_REG && o->fp) {
		if (o->fp == 1) snprintf(t, sizeof(t), "%%xmm%d", o->reg);
		else snprintf(t, sizeof(t), o->reg ? "%%st(1)" : "%%st");
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
	ctx->sidefx++;
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
		if (ctx->asm_text) { buf_append(ctx->asm_text, tmpl, strlen(tmpl)); buf_append_byte(ctx->asm_text, '\n'); }
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
	int stpop = 0;
	if (consume(ctx, T_COLON) && ctx->tok.kind == T_STR) {
		for (;;) {
			char *cl = asm_string(ctx);
			const char *cn = *cl == '%' ? cl + 1 : cl;
			if (!strcmp(cn, "st") || !strcmp(cn, "st(0)")) stpop |= 1;
			else if (!strcmp(cn, "st(1)")) stpop |= 2;
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
	int usedx = 0;
	for (int i = 0; i < n; i++) {
		AsmOp *o = &ops[i];
		if (!o->fp) continue;
		if (o->fp >= 2) { o->reg = o->fp - 2; continue; }
		if (!o->out && o->match >= 0) { o->reg = ops[o->match].reg; continue; }
		int x = 0;
		while (x < 16 && (usedx & (1 << x))) x++;
		if (x == 16) error_at(ctx, "asm statement needs too many SSE registers");
		usedx |= 1 << x;
		o->reg = x;
	}
	for (int i = 0; i < nout; i++) {
		AsmOp *o = &ops[i];
		if (o->kind != AK_REG || o->fixed >= 0 || o->fp) continue;
		int u = used_out | used_in;
		o->reg = asm_take(ctx, &u);
		used_out |= 1 << o->reg;
		if (o->plus) used_in |= 1 << o->reg;
	}
	for (int i = nout; i < n; i++) {
		AsmOp *o = &ops[i];
		if (o->fp) continue;
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
	int nst = 0, tout = -1;
	for (int st = 1; st >= 0; st--)
		for (int i = 0; i < n; i++) {
			AsmOp *o = &ops[i];
			if (o->fp < 2 || o->reg != st || (o->out && !o->plus)) continue;
			emit_load_rsp(c, R11, (ctx->sp - (o->plus ? o->vslot : o->slot)) * 8);
			emit_x87(c, 0xDB, 5, R11, 0);
			nst++;
			break;
		}
	for (int i = 0; i < nout; i++) if (ops[i].fp >= 2 && ops[i].reg == 0) tout = i;
	for (int i = 0; i < n; i++) {
		AsmOp *o = &ops[i];
		if (o->greg) continue;
		if (o->fp == 1) {
			if (!o->out || o->plus) emit_movq_xmm_load(c, o->reg, RSP, (ctx->sp - (o->plus ? o->vslot : o->slot)) * 8);
			continue;
		}
		if (o->fp) continue;
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
	if (ctx->asm_text && !ctx->dead && !ctx->noasm) { buf_append(ctx->asm_text, text, strlen(text)); buf_append_byte(ctx->asm_text, '\n'); }
	if (!ctx->dead && !ctx->noasm) assemble_snippet(ctx, text, hid, nh, file, line);
	free(text);

	int npush = 0;
	if (tout >= 0) {
		emit_sub_rsp(c, 16);
		emit_x87(c, 0xDB, 7, RSP, 0);
		ctx->sp += 2;
		npush += 2;
		ops[tout].pslot = ctx->sp;
		if (nst) nst--;
	}
	if (stpop & 1 && nst) nst--;
	if (stpop & 2 && nst) nst--;
	while (nst-- > 0) { C8(c,0xDD); C8(c,0xD8); }
	for (int i = 0; i < nout; i++) {
		if (ops[i].kind != AK_REG || ops[i].greg || ops[i].fp >= 2) continue;
		if (ops[i].fp == 1) { emit_sub_rsp(c, 8); emit_movq_xmm_store(c, ops[i].reg, RSP, 0); }
		else emit_push(c, ops[i].reg);
		ctx->sp++;
		ops[i].pslot = ctx->sp;
		npush++;
	}
	for (int i = 0; i < nout; i++) {
		AsmOp *o = &ops[i];
		if (o->kind != AK_REG || o->greg || (o->fp >= 2 && i != tout)) continue;
		emit_load_rsp(c, RCX, (ctx->sp - o->slot) * 8);
		if (o->fp >= 2) emit_rm_wide(c, 0x8D, RAX, RSP, (ctx->sp - o->pslot) * 8);
		else emit_load_rsp(c, RAX, (ctx->sp - o->pslot) * 8);
		emit_store_type(c, o->ty);
	}
	if (npush) { emit_add_rsp(c, npush * 8); ctx->sp -= npush; }
	for (int i = nsave - 1; i >= 0; i--) { emit_pop(c, saves[i]); ctx->sp--; }
	if (ctx->sp > sp0) { emit_add_rsp(c, (ctx->sp - sp0) * 8); ctx->sp = sp0; }
	ctx->lv = 0;
}

#define ELF_BASE 0x400000ULL

static int array_sec_type(const char *n) {
	const char *k[3] = { ".init_array", ".fini_array", ".preinit_array" };
	for (int i = 0; i < 3; i++) {
		size_t l = strlen(k[i]);
		if (!strncmp(n, k[i], l) && (!n[l] || n[l] == '.')) return 14 + i;
	}
	return 1;
}

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

static void add_elf_sym(Buf *symtab, Buf *strtab, Sym *s, uint64_t *sect_va, int *shndx, int bind, long long tls_lo) {
	uint8_t e[24];
	memset(e, 0, 24);
	le32(e, strtab_add(strtab, s->name));
	e[4] = (uint8_t)((bind << 4) | (s->tls ? 6 : s->kind == SYM_FUNC ? 2 : 1));
	le16(e+6, (uint16_t)(s->sect == SEC_ABS ? 0xFFF1 : shndx[s->sect]));
	le64(e+8, (s->sect == SEC_ABS ? (uint64_t)s->off : sect_va[s->sect] + s->off) - (s->tls ? (uint64_t)tls_lo : 0));
	le64(e+16, (uint64_t)s->size);
	buf_append(symtab, e, 24);
}

static void write_out(const char *out_path, Buf *file, int mode) {
	struct stat st;
	int regular = stat(out_path, &st) != 0 || S_ISREG(st.st_mode);
	if (!strcmp(out_path, "-")) { fwrite(file->data, 1, file->len, stdout); return; }
	if (regular) remove(out_path);
	FILE *out=fopen(out_path,"wb");
	if (!out) { perror(out_path); exit(1); }
	if (fwrite(file->data,1,file->len,out)!=file->len) { perror("fwrite"); exit(1); }
	fclose(out);
	if (mode && regular) chmod(out_path, mode);
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
	return (k == SK_CODE ? 6 : k == SK_RO ? 2 : 3) | (ctx->secs[sect - SEC_NAMED].tls ? 0x400 : 0);
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
	char *phname[8];
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
	int filehdr;
} LdSeg;

typedef struct {
	char *name;
	char **glob;
	int nglob;
} LdVer;

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
	LdVer *vers;
	int nvers;
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
	if (!strncmp(p, "/DISCARD/", 9)) return 0;
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

static LdStmt *ld_assert(LdLex *L);

static LdStmt *ld_assignment(LdLex *L, char *name, int op, int provide) {
	ld_ws(L);
	if (!strcmp(name, ".") && op == '=' && !strncmp(L->p, "ASSERT", 6) && !isalnum((unsigned char)L->p[6]) && L->p[6] != '_') {
		L->p += 6;
		return ld_assert(L);
	}
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

static void ld_resolve_phdrs(Ld *ld, LdLex *L) {
	for (LdStmt *st = ld->top; st; st = st->next)
		if (st->kind == LS_SECTION)
			for (int i = 0; i < st->nphdr; i++) {
				L->line = st->line;
				st->phdr[i] = ld_phdr_index(ld, L, st->phname[i]);
			}
}

static LdStmt *ld_section(Ld *ld, LdLex *L, char *name) {
	(void)ld;
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
			st->phname[st->nphdr] = ph;
			st->phdr[st->nphdr++] = -1;
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
			if (ld_accept(L, "FILEHDR")) { g->filehdr = 1; continue; }
			if (ld_accept(L, "PHDRS")) continue;
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

static void ld_version(Ld *ld, LdLex *L) {
	ld_expect(L, "{");
	while (!ld_accept(L, "}")) {
		if (!*L->p) ld_err(L, "expected '}'");
		ld->vers = (LdVer *)realloc(ld->vers, sizeof(LdVer) * (size_t)(ld->nvers + 1));
		LdVer *v = &ld->vers[ld->nvers++];
		memset(v, 0, sizeof(*v));
		v->name = ld_need_word(L, LD_SECCH, "a version name");
		ld_expect(L, "{");
		int global = 1;
		while (!ld_accept(L, "}")) {
			if (!*L->p) ld_err(L, "expected '}'");
			if (ld_accept(L, ";")) continue;
			if (ld_accept(L, "global:")) { global = 1; continue; }
			if (ld_accept(L, "local:")) { global = 0; continue; }
			char *w = ld_need_word(L, LD_PATCH, "a symbol");
			size_t n = strlen(w);
			if (n && w[n - 1] == ':') {
				if (!strcmp(w, "global:")) { global = 1; continue; }
				if (!strcmp(w, "local:")) { global = 0; continue; }
			}
			if (global) {
				v->glob = (char **)realloc(v->glob, sizeof(char *) * (size_t)(v->nglob + 1));
				v->glob[v->nglob++] = w;
			}
			ld_accept(L, ";");
		}
		while (!ld_accept(L, ";")) {
			if (!*L->p || ld_at(L, "}")) break;
			ld_need_word(L, LD_SECCH, "a version name");
		}
	}
}

static Ld *ld_parse(const char *path, char *text) {
	Ld *ld = (Ld *)calloc(1, sizeof(Ld));
	ld->path = path;
	ld->tail = &ld->top;
	LdLex lx, *L = &lx;
	L->p = text;
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
		else if (!strcmp(w, "VERSION")) ld_version(ld, L);
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
			else if (!strcmp(f, "elf32-i386")) ld->format = 3;
			else ld_err(L, "output format '%s' is not supported", f);
		} else if (!strcmp(w, "OUTPUT_ARCH")) {
			ld_expect(L, "(");
			char *a = ld_need_word(L, "_.$-:", "an architecture");
			ld_expect(L, ")");
			if (strcmp(a, "i386:x86-64") && strcmp(a, "i386")) ld_err(L, "architecture '%s' is not supported", a);
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
	ld_resolve_phdrs(ld, L);
	return ld;
}

static Ld *ld_read(const char *path) { return ld_parse(path, read_file(path)); }

static Ld *ld_default(Ctx *ctx) {
	char *b = (char *)malloc(1024);
	char base[64];
	if (ctx->shared && !ctx->has_ttext) {
		snprintf(b, 1024, "PHDRS { text PT_LOAD FILEHDR PHDRS FLAGS(5); data PT_LOAD FLAGS(6); dynamic PT_DYNAMIC FLAGS(6); } "
			"SECTIONS { . = SIZEOF_HEADERS; .hash : { *(.hash) } :text .gnu.hash : { *(.gnu.hash) } .dynsym : { *(.dynsym) } "
			".dynstr : { *(.dynstr) } .text : { *(.text .text.*) } .rodata : { *(.rodata .rodata.*) } "
			". = ALIGN(0x1000) + (. & 0xfff); .dynamic : { *(.dynamic) } :data :dynamic "
			".data : { *(.data .data.*) } :data .bss : { *(.bss .bss.*) *(COMMON) } }");
		return ld_parse("<default script>", b);
	}
	if (ctx->has_ttext) snprintf(base, sizeof(base), "0x%llx", (unsigned long long)ctx->ttext);
	else snprintf(base, sizeof(base), "%sSIZEOF_HEADERS", ctx->shared ? "" : ctx->elf32 ? "0x8048000 + " : "0x400000 + ");
	snprintf(b, 512, "SECTIONS { . = %s; .text : { *(.text .text.*) } .rodata : { *(.rodata .rodata.*) } "
		".data : { *(.data .data.*) } .bss : { *(.bss .bss.*) *(COMMON) } }", base);
	ctx->has_ttext = 0;
	return ld_parse("<default script>", b);
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
	int tls = p->sect >= SEC_NAMED ? ctx->secs[p->sect - SEC_NAMED].tls : 0;
	o->flags |= 2 | (k == 0 ? 4 : 0) | (k >= 2 ? 1 : 0) | (k == 3 || tls == 2 ? 0 : p->note ? 0x200 : 0x100) | (tls ? 0x400 : 0);
	if (k != 3 && tls != 2) o->nobits = 0;
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
			st->anchor = p->sect >= SEC_NAMED && ctx->secs[p->sect - SEC_NAMED].tls ? NULL : ld_named(ld, k);
			st->byname = st->anchor != NULL;
			if (!st->anchor) st->anchor = ld_anchor(ld, k, recent, &st->exact);
			LdStmt **at, *a = st->anchor;
			if (st->byname && tail[k]) at = &tail[k]->next;
			else if (a) {
				while (a->next && (st->byname ? (a->next->kind != LS_SECTION && !(a->next->kind == LS_ASSIGN && !strcmp(a->next->name, "."))) || a->next->orphan :
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
	else if ((o->flags & 0x400) && o->nobits) ld->dot = vma;
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
		if (o->size == 0 || ((o->flags & 0x400) && o->nobits)) continue;
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
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		if (o->discard || !o->kept || !(o->flags & 0x400)) continue;
		if (ld->nsegs == ld->capsegs) {
			ld->capsegs = ld->capsegs ? ld->capsegs * 2 : 8;
			ld->segs = (LdSeg *)realloc(ld->segs, sizeof(LdSeg) * ld->capsegs);
			if (!ld->segs) { perror("realloc"); exit(1); }
		}
		memset(&ld->segs[ld->nsegs], 0, sizeof(LdSeg));
		ld->segs[ld->nsegs++].type = 7;
		break;
	}
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

static int ld_i386;

static void ld_nops(uint8_t *p, size_t n) {
	if (ld_i386) {
		for (size_t i = 0; i < n; i++) p[i] = i + 1 == n && (n & 1) ? 0x90 : i & 1 ? 0x90 : 0x66;
		return;
	}
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
		if (ctx->shared && !pc && r->kind != R_TPOFF32 && r->kind != R_TPOFF64 && !(r->sym && (!r->sym->defined || r->sym->sect == SEC_ABS))) {
			fprintf(stderr, "error: -shared cannot make dynamic relocations, and one is needed for %s\n", r->sym ? r->sym->name : "a section address");
			exit(1);
		}
		int64_t v = pc ? (int64_t)target - (int64_t)(sect_addr(ctx, sect_va, r->in, (long)r->pos) + n) : (int64_t)target;
		if (r->kind == R_TPOFF32 || r->kind == R_TPOFF64) {
			if (!ctx->has_tls) { fprintf(stderr, "error: '%s' is thread-local but there is no TLS section\n", r->sym ? r->sym->name : "?"); exit(1); }
			v = (int64_t)(target - (uint64_t)ctx->tls_tp);
		}
		if (n < 8) {
			int64_t lo = -((int64_t)1 << (8 * n - 1));
			int64_t hi = pc || r->kind == R_ABS32S || r->kind == R_TPOFF32 ? ((int64_t)1 << (8 * n - 1)) - 1 : ((int64_t)1 << (8 * n)) - 1;
			if (v < lo || v > hi) {
				if (r->sym && !r->sym->defined) { fprintf(stderr, "error: weak symbol '%s' is undefined and address 0 is out of reach\n", r->sym->name); exit(1); }
				fprintf(stderr, "error: relocation against '%s' is out of range\n", r->sym ? r->sym->name : "a section");
				exit(1);
			}
		}
		for (int k = 0; k < n; k++) b->data[r->pos + k] = (uint8_t)((uint64_t)v >> (8 * k));
	}
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static int rel386(int t) {
	switch (t) {
	case 2: case 4: return 2;
	case 10: case 11: return 1;
	case 12: return 20;
	case 13: return 21;
	case 14: return 22;
	case 15: return 23;
	}
	return -1;
}

static void elf_to32(Ctx *ctx, Buf *f) {
	(void)ctx;
	uint8_t *E = f->data;
	uint64_t phoff = rd64(E + 0x20), shoff = rd64(E + 0x28), entry = rd64(E + 0x18);
	int phnum = rd16(E + 0x38), shnum = rd16(E + 0x3C), shstrndx = rd16(E + 0x3E);
	uint8_t *old = (uint8_t *)malloc(f->len);
	memcpy(old, f->data, f->len);
	const uint8_t *osh = old + shoff, *oshs = old + rd64(osh + (size_t)shstrndx * 64 + 0x18);
	Buf shs;
	buf_init(&shs);
	buf_append_byte(&shs, 0);
	uint8_t *nsh = (uint8_t *)calloc((size_t)shnum, 40);
	for (int i = 1; i < shnum; i++) {
		const uint8_t *h = osh + (size_t)i * 64;
		const char *nm = (const char *)oshs + rd32(h);
		uint32_t type = rd32(h + 4);
		uint64_t off = rd64(h + 0x18), size = rd64(h + 0x20), ent = rd64(h + 0x38), al = rd64(h + 0x30);
		char nb[300];
		if (type == 4 && !strncmp(nm, ".rela", 5)) { snprintf(nb, sizeof(nb), ".rel%s", nm + 5); nm = nb; }
		uint32_t name = strtab_add(&shs, nm);
		if (type == 2) {
			buf_zero_to(f, align_up(f->len, 4));
			size_t no = f->len;
			for (uint64_t k = 0; k < size / 24; k++) {
				const uint8_t *s = old + off + k * 24;
				uint8_t e[16];
				le32(e, rd32(s));
				le32(e + 4, (uint32_t)rd64(s + 8));
				le32(e + 8, (uint32_t)rd64(s + 16));
				e[12] = s[4];
				e[13] = s[5];
				le16(e + 14, rd16(s + 6));
				buf_append(f, e, 16);
			}
			off = no;
			size = size / 24 * 16;
			ent = 16;
			al = 4;
		} else if (type == 4) {
			buf_zero_to(f, align_up(f->len, 4));
			size_t no = f->len;
			for (uint64_t k = 0; k < size / 24; k++) {
				const uint8_t *s = old + off + k * 24;
				uint64_t info = rd64(s + 8);
				int t = rel386((int)(info & 0xFFFFFFFF));
				if (t < 0) { fprintf(stderr, "error: relocation type %d cannot be written to a 32-bit file\n", (int)(info & 0xFFFFFFFF)); exit(1); }
				uint8_t e[8];
				le32(e, (uint32_t)rd64(s));
				le32(e + 4, (uint32_t)((info >> 32) << 8) | (uint32_t)t);
				buf_append(f, e, 8);
			}
			type = 9;
			off = no;
			size = size / 24 * 8;
			ent = 8;
			al = 4;
		}
		uint8_t *n = nsh + (size_t)i * 40;
		le32(n, name);
		le32(n + 4, type);
		le32(n + 8, (uint32_t)rd64(h + 8));
		le32(n + 12, (uint32_t)rd64(h + 0x10));
		le32(n + 16, (uint32_t)off);
		le32(n + 20, (uint32_t)size);
		le32(n + 24, rd32(h + 0x28));
		le32(n + 28, rd32(h + 0x2C));
		le32(n + 32, (uint32_t)al);
		le32(n + 36, (uint32_t)ent);
	}
	size_t shs_off = f->len;
	buf_append(f, shs.data, shs.len);
	le32(nsh + (size_t)shstrndx * 40 + 16, (uint32_t)shs_off);
	le32(nsh + (size_t)shstrndx * 40 + 20, (uint32_t)shs.len);
	buf_zero_to(f, align_up(f->len, 4));
	size_t sh32 = f->len;
	buf_append(f, nsh, (size_t)shnum * 40);
	E = f->data;
	memset(E, 0, 52);
	E[0] = 0x7F; E[1] = 'E'; E[2] = 'L'; E[3] = 'F'; E[4] = 1; E[5] = 1; E[6] = 1;
	le16(E + 0x10, 2); le16(E + 0x12, 3); le32(E + 0x14, 1);
	le32(E + 0x18, (uint32_t)entry);
	le32(E + 0x1C, phnum ? 52 : 0);
	le32(E + 0x20, (uint32_t)sh32);
	le16(E + 0x28, 52); le16(E + 0x2A, 32); le16(E + 0x2C, (uint16_t)phnum);
	le16(E + 0x2E, 40); le16(E + 0x30, (uint16_t)shnum); le16(E + 0x32, (uint16_t)shstrndx);
	for (int i = 0; i < phnum; i++) {
		const uint8_t *p = old + phoff + (size_t)i * 56;
		uint8_t *q = E + 52 + (size_t)i * 32;
		le32(q, rd32(p));
		le32(q + 4, (uint32_t)rd64(p + 8));
		le32(q + 8, (uint32_t)rd64(p + 0x10));
		le32(q + 12, (uint32_t)rd64(p + 0x18));
		le32(q + 16, (uint32_t)rd64(p + 0x20));
		le32(q + 20, (uint32_t)rd64(p + 0x28));
		le32(q + 24, rd32(p + 4));
		le32(q + 28, (uint32_t)rd64(p + 0x30));
	}
	memset(E + 52 + (size_t)phnum * 32, 0, 64 + (size_t)phnum * 56 - 52 - (size_t)phnum * 32);
	free(old);
	free(nsh);
	free(shs.data);
}


static uint32_t elf_hash(const char *n) {
	uint32_t h = 0, g;
	while (*n) {
		h = (h << 4) + (uint8_t)*n++;
		g = h & 0xF0000000u;
		if (g) h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

static uint32_t gnu_hash(const char *n) {
	uint32_t h = 5381;
	while (*n) h = h * 33 + (uint8_t)*n++;
	return h;
}

static int dyn_cmp_gnb;

static int dyn_cmp(const void *a, const void *b) {
	const struct DynSym *x = (const struct DynSym *)a, *y = (const struct DynSym *)b;
	uint32_t bx = x->gh % (uint32_t)dyn_cmp_gnb, by = y->gh % (uint32_t)dyn_cmp_gnb;
	if (bx != by) return bx < by ? -1 : 1;
	return strcmp(x->s->name, y->s->name);
}

static void ld_shared_prep(Ctx *ctx, Ld *ld) {
	int cap = 0;
	for (Sym *g = ctx->globals; g; g = g->gnext) {
		if (!g->defined || g->is_static || (g->kind != SYM_FUNC && g->kind != SYM_VAR)) continue;
		int ver = ld->nvers ? 0 : 1;
		for (int i = 0; i < ld->nvers && !ver; i++)
			for (int j = 0; j < ld->vers[i].nglob && !ver; j++)
				if (ld_glob(ld->vers[i].glob[j], g->name)) ver = i + 2;
		if (!ver) continue;
		int dup = 0;
		for (int i = 0; i < ctx->ndyn && !dup; i++) dup = ctx->dyn[i].s->name == g->name;
		if (dup) continue;
		if (ctx->ndyn == cap) ctx->dyn = realloc(ctx->dyn, sizeof(*ctx->dyn) * (size_t)(cap = cap ? cap * 2 : 16));
		ctx->dyn[ctx->ndyn].s = g;
		ctx->dyn[ctx->ndyn].ver = ver;
		ctx->dyn[ctx->ndyn++].gh = gnu_hash(g->name);
	}
	int n = ctx->ndyn;
	ctx->dyn_gnb = n < 4 ? 1 : (uint32_t)n / 2;
	ctx->dyn_nbucket = n < 3 ? 1 : n < 17 ? 3 : 17;
	dyn_cmp_gnb = (int)ctx->dyn_gnb;
	if (n) qsort(ctx->dyn, (size_t)n, sizeof(*ctx->dyn), dyn_cmp);
	buf_init(&ctx->dynstr);
	buf_append_byte(&ctx->dynstr, 0);
	ctx->dyn_soname = ctx->soname ? strtab_add(&ctx->dynstr, ctx->soname) : 0;
	for (int i = 0; i < n; i++) ctx->dyn[i].name = strtab_add(&ctx->dynstr, ctx->dyn[i].s->name);
	ctx->dyn_vname = (uint32_t *)calloc((size_t)ld->nvers + 1, sizeof(uint32_t));
	for (int i = 0; i < ld->nvers; i++) ctx->dyn_vname[i] = strtab_add(&ctx->dynstr, ld->vers[i].name);
	static const char *names[7] = { ".hash", ".gnu.hash", ".dynsym", ".dynstr", ".gnu.version", ".gnu.version_d", ".dynamic" };
	size_t size[7];
	size[0] = (2 + ctx->dyn_nbucket + (size_t)n + 1) * 4;
	size[1] = 16 + 8 + ctx->dyn_gnb * 4 + (size_t)n * 4;
	size[2] = ((size_t)n + 1) * 24;
	size[3] = ctx->dynstr.len;
	size[4] = ld->nvers ? ((size_t)n + 1) * 2 : 0;
	size[5] = ld->nvers ? (size_t)(ld->nvers + 1) * 28 : 0;
	size[6] = (size_t)(8 + (ctx->soname ? 1 : 0) + (ld->nvers ? 3 : 0)) * 16;
	piece_begin(ctx);
	for (int i = 0; i < 7; i++) {
		ctx->dsec[i] = 0;
		if (!size[i]) continue;
		ctx->dsec[i] = section_index(ctx, intern(names[i]), SK_RO);
		ctx->secs[ctx->dsec[i] - SEC_NAMED].align = i == 4 ? 2 : i == 3 ? 1 : 8;
		Buf *b = sec_buf(ctx, ctx->dsec[i]);
		buf_zero_to(b, b->len + size[i]);
	}
	piece_end(ctx, "<dynamic>");
}

static void ld_shared_fill(Ctx *ctx, Ld *ld) {
	int n = ctx->ndyn;
	uint8_t *hash = sec_buf(ctx, ctx->dsec[0])->data, *gh = sec_buf(ctx, ctx->dsec[1])->data, *sym = sec_buf(ctx, ctx->dsec[2])->data;
	memcpy(sec_buf(ctx, ctx->dsec[3])->data, ctx->dynstr.data, ctx->dynstr.len);
	for (int i = 0; i < n; i++) {
		Sym *g = ctx->dyn[i].s;
		uint8_t *e = sym + (size_t)(i + 1) * 24;
		int shn = 0xFFF1;
		if (g->sect != SEC_ABS) {
			Piece *p = ld_sym_piece(ctx, g);
			shn = p && p->out >= 0 ? ld->outs[p->out].shndx : 0;
		}
		le32(e, ctx->dyn[i].name);
		e[4] = (uint8_t)(((g->weak ? 2 : 1) << 4) | (g->kind == SYM_FUNC ? 2 : 1));
		le16(e + 6, (uint16_t)shn);
		le64(e + 8, ld_sym_addr(ctx, g));
		le64(e + 16, (uint64_t)g->size);
	}
	uint32_t nb = ctx->dyn_nbucket;
	le32(hash, nb);
	le32(hash + 4, (uint32_t)n + 1);
	for (int i = n; i >= 1; i--) {
		uint32_t b = elf_hash(ctx->dyn[i - 1].s->name) % nb;
		le32(hash + 8 + 4 * ((size_t)nb + (size_t)i), rd32(hash + 8 + 4 * b));
		le32(hash + 8 + 4 * b, (uint32_t)i);
	}
	uint32_t gnb = ctx->dyn_gnb;
	le32(gh, gnb);
	le32(gh + 4, 1);
	le32(gh + 8, 1);
	le32(gh + 12, 6);
	uint64_t bloom = 0;
	for (int i = 0; i < n; i++) {
		uint32_t h = ctx->dyn[i].gh;
		bloom |= 1ULL << (h % 64) | 1ULL << ((h >> 6) % 64);
	}
	le64(gh + 16, bloom);
	for (int i = n - 1; i >= 0; i--) le32(gh + 24 + 4 * (size_t)(ctx->dyn[i].gh % gnb), (uint32_t)i + 1);
	for (int i = 0; i < n; i++) {
		uint32_t h = ctx->dyn[i].gh & ~1u;
		if (i + 1 == n || ctx->dyn[i + 1].gh % gnb != ctx->dyn[i].gh % gnb) h |= 1;
		le32(gh + 24 + 4 * (size_t)gnb + 4 * (size_t)i, h);
	}
	if (ld->nvers) {
		uint8_t *vs = sec_buf(ctx, ctx->dsec[4])->data, *vd = sec_buf(ctx, ctx->dsec[5])->data;
		for (int i = 0; i < n; i++) le16(vs + 2 * (size_t)(i + 1), (uint16_t)ctx->dyn[i].ver);
		uint32_t vname = ctx->dyn_soname;
		for (int i = 0; i <= ld->nvers; i++) {
			const char *nm = i ? ld->vers[i - 1].name : ctx->soname ? ctx->soname : "";
			uint8_t *e = vd + (size_t)i * 28;
			le16(e, 1);
			le16(e + 2, i ? 0 : 1);
			le16(e + 4, (uint16_t)(i + 1));
			le16(e + 6, 1);
			le32(e + 8, elf_hash(nm));
			le32(e + 12, 20);
			le32(e + 16, i < ld->nvers ? 28 : 0);
			le32(e + 20, i ? ctx->dyn_vname[i - 1] : vname);
			le32(e + 24, 0);
		}
	}
	uint8_t *d = sec_buf(ctx, ctx->dsec[6])->data;
	uint64_t tags[16][2];
	int nt = 0;
	if (ctx->soname) { tags[nt][0] = 14; tags[nt++][1] = ctx->dyn_soname; }
	tags[nt][0] = 4; tags[nt++][1] = ld_addr(ctx, ctx->dsec[0], 0);
	tags[nt][0] = 0x6ffffef5; tags[nt++][1] = ld_addr(ctx, ctx->dsec[1], 0);
	tags[nt][0] = 5; tags[nt++][1] = ld_addr(ctx, ctx->dsec[3], 0);
	tags[nt][0] = 6; tags[nt++][1] = ld_addr(ctx, ctx->dsec[2], 0);
	tags[nt][0] = 10; tags[nt++][1] = ctx->dynstr.len;
	tags[nt][0] = 11; tags[nt++][1] = 24;
	if (ld->nvers) {
		tags[nt][0] = 0x6ffffff0; tags[nt++][1] = ld_addr(ctx, ctx->dsec[4], 0);
		tags[nt][0] = 0x6ffffffc; tags[nt++][1] = ld_addr(ctx, ctx->dsec[5], 0);
		tags[nt][0] = 0x6ffffffd; tags[nt++][1] = (uint64_t)ld->nvers + 1;
	}
	for (int i = 0; i < nt; i++) { le64(d + 16 * (size_t)i, tags[i][0]); le64(d + 16 * (size_t)i + 8, tags[i][1]); }
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
	if (ctx->shared) ld_shared_fill(ctx, ld);

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
		if (g->filehdr && g->vaddr >= g->off) {
			uint64_t d = g->off;
			g->vaddr -= d;
			g->paddr -= d;
			g->memsz += d;
			g->filesz += d;
			g->off = 0;
		}
	}
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		if (o->seg >= 0 && ld->segs[o->seg].type == 1) continue;
		pos = align_up(pos, o->align > 0 ? (size_t)o->align : 1);
		o->off = pos;
		if (!o->nobits && !o->st->noload) pos += (size_t)o->size;
	}
	for (int i = 0; i < nsegs; i++) {
		LdSeg *g = &ld->segs[i];
		if (g->type != 7 || ld->nphdrs) continue;
		uint64_t lo = UINT64_MAX, hi = 0, fhi = 0, plo = 0, al = 1, off = 0;
		for (int k = 0; k < n; k++) {
			LdOut *o = &ld->outs[ord[k]];
			if (!(o->flags & 0x400)) continue;
			if (o->vma < lo) { lo = o->vma; plo = o->lma; off = o->off; }
			if (o->vma + o->size > hi) hi = o->vma + o->size;
			if (!o->nobits && o->vma + o->size > fhi) fhi = o->vma + o->size;
			if (o->align > al) al = o->align;
		}
		if (lo == UINT64_MAX) continue;
		g->off = off;
		g->vaddr = lo;
		g->paddr = plo;
		g->filesz = fhi > lo ? fhi - lo : 0;
		g->memsz = hi - lo;
		g->align = al;
		g->flags = 4;
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
	for (Sym *s = ctx->globals; s; s = s->gnext) s->elf_index = 0;
	if (ctx->emit_relocs)
		for (int k = 0; k < n; k++) {
			memset(e, 0, 24);
			e[4] = 3;
			le16(e+6, (uint16_t)(k + 1));
			le64(e+8, ld->outs[ord[k]].vma);
			buf_append(&symtab, e, 24);
			nlocal++;
		}
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
			e[4] = (uint8_t)(((pass == 0 ? 0 : s->weak ? 2 : 1) << 4) | (s->tls ? 6 : s->kind == SYM_FUNC ? 2 : s->ld_out || s->sect != SEC_ABS ? 1 : 0));
			le16(e+6, (uint16_t)shn);
			le64(e+8, ld_sym_addr(ctx, s) - (s->tls ? (uint64_t)ctx->tls_lo : 0));
			le64(e+16, (uint64_t)s->size);
			s->elf_index = (int)(symtab.len / 24);
			buf_append(&symtab, e, 24);
			if (pass == 0) nlocal++;
		}
	}

	Buf *rel = (Buf *)calloc((size_t)n + 1, sizeof(Buf));
	if (ctx->emit_relocs)
		for (int i = 0; i < ctx->nrelocs; i++) {
			Reloc *r = &ctx->relocs[i];
			Piece *p = ld_piece_at(ctx, r->in, (long)r->pos);
			if (!p || p->out < 0 || !ld->outs[p->out].shndx) continue;
			int k = ld->outs[p->out].shndx - 1, symi = 0, pc = r->kind == R_PC32 || r->kind == R_PLT32 || r->kind == R_PC16 || r->kind == R_PC8 || r->kind == R_PC64;
			uint64_t P = ld_addr(ctx, r->in, (long)r->pos), S = 0;
			if (r->sym) {
				Sym *g = r->sym;
				if (g->defined && g->elf_index) { symi = g->elf_index; S = ld_sym_addr(ctx, g); }
			} else {
				Piece *tp = ld_piece_at(ctx, r->sect, (long)r->addend);
				if (!tp || tp->out < 0 || !ld->outs[tp->out].shndx) continue;
				symi = ld->outs[tp->out].shndx;
				S = ld->outs[tp->out].vma;
			}
			int sz = rel_size[r->kind];
			const uint8_t *d = sec_buf(ctx, r->in)->data + r->pos;
			uint64_t v = 0;
			for (int b = 0; b < sz; b++) v |= (uint64_t)d[b] << (8 * b);
			if (sz < 8 && (pc || r->kind == R_ABS32S) && (v >> (8 * sz - 1)) & 1) v |= ~0ULL << (8 * sz);
			uint8_t re[24];
			le64(re, P);
			le64(re + 8, ((uint64_t)symi << 32) | (uint64_t)rel_elf[r->kind]);
			le64(re + 16, pc ? v - S + P : v - S);
			buf_append(&rel[k], re, 24);
		}

	buf_append_byte(&shstr, 0);
	uint32_t *names = (uint32_t *)malloc(sizeof(uint32_t) * (2 * n + 1));
	for (int k = 0; k < n; k++) names[k] = strtab_add(&shstr, ld->outs[ord[k]].name);
	int nrel = 0;
	for (int k = 0; k < n; k++)
		if (rel[k].len) {
			char rn[300];
			snprintf(rn, sizeof(rn), ".rela%s", ld->outs[ord[k]].name);
			names[n + k] = strtab_add(&shstr, rn);
			nrel++;
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
	size_t *rel_off = (size_t *)calloc((size_t)n + 1, sizeof(size_t));
	for (int k = 0; k < n; k++)
		if (rel[k].len) {
			buf_zero_to(&file, align_up(file.len, 8));
			rel_off[k] = file.len;
			buf_append(&file, rel[k].data, rel[k].len);
		}
	buf_zero_to(&file, align_up(file.len, 8));
	size_t sh_off = file.len;
	int nsh = n + 4 + nrel;
	buf_zero_to(&file, sh_off + (size_t)nsh * 64);
	uint8_t *S = file.data + sh_off;
	for (int k = 0, j = n + 4; k < n; k++)
		if (rel[k].len) {
			shdr(S + j * 64, names[n + k], 4, 0x40, 0, rel_off[k], rel[k].len, (uint32_t)(n + 1), (uint32_t)(k + 1), 8, 24);
			j++;
		}
	for (int k = 0; k < n; k++) free(rel[k].data);
	free(rel);
	free(rel_off);
	for (int k = 0; k < n; k++) {
		LdOut *o = &ld->outs[ord[k]];
		int nob = o->nobits || o->st->noload;
		int type = nob ? 8 : (o->flags & 0x300) == 0x200 ? 7 : 1;
		shdr(S + (k + 1) * 64, names[k], type, (uint64_t)((o->flags & 0x407) | 2), o->vma, o->off, o->size, 0, 0, o->align, 0);
	}
	shdr(S + (n + 1) * 64, n_sym, 2, 0, 0, sym_off, symtab.len, (uint32_t)(n + 2), (uint32_t)nlocal, 8, 24);
	shdr(S + (n + 2) * 64, n_str, 3, 0, 0, str_off, strtab.len, 0, 0, 1, 0);
	shdr(S + (n + 3) * 64, n_shs, 3, 0, 0, shs_off, shstr.len, 0, 0, 1, 0);

	uint8_t *E = file.data;
	E[0]=0x7F; E[1]='E'; E[2]='L'; E[3]='F'; E[4]=2; E[5]=1; E[6]=1;
	le16(E+0x10,ctx->shared ? 3 : 2); le16(E+0x12,62); le32(E+0x14,1);
	le64(E+0x18,entry);
	le64(E+0x20,nsegs ? 64 : 0); le64(E+0x28,sh_off);
	le16(E+0x34,64); le16(E+0x36,56); le16(E+0x38,(uint16_t)nsegs);
	le16(E+0x3A,64); le16(E+0x3C,(uint16_t)nsh); le16(E+0x3E,(uint16_t)(n + 3));
	for (int i = 0; i < nsegs; i++) {
		LdSeg *g = &ld->segs[i];
		uint8_t *P = file.data + 64 + 56 * i;
		le32(P+0x00,g->type); le32(P+0x04,g->flags);
		le64(P+0x08,g->off); le64(P+0x10,g->vaddr); le64(P+0x18,g->paddr);
		le64(P+0x20,g->filesz); le64(P+0x28,g->memsz); le64(P+0x30,g->align);
	}
	if (ctx->elf32) elf_to32(ctx, &file);
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
	ld_i386 = ctx->elf32;
	resolve_alias_syms(ctx);
	if (ctx->shared) ld_shared_prep(ctx, ld);
	ld_match(ctx, ld);
	ld_layout(ctx, ld);
	ld_define(ctx, ld);
	uint64_t tls_lo = UINT64_MAX, tls_hi = 0, tls_a = 1;
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *o = &ld->outs[i];
		if (o->discard || !o->kept || !(o->flags & 0x400)) continue;
		if (o->vma < tls_lo) tls_lo = o->vma;
		if (o->vma + o->size > tls_hi) tls_hi = o->vma + o->size;
		if (o->align > tls_a) tls_a = o->align;
	}
	ctx->has_tls = tls_hi > 0;
	ctx->tls_lo = (long long)tls_lo;
	if (ctx->has_tls) ctx->tls_tp = (long long)(tls_lo + ld_alignup(tls_hi - tls_lo, tls_a));
	if (ctx->map) {
		Buf m;
		buf_init(&m);
		char line[600];
		for (int i = 0; i < ld->nouts; i++) {
			LdOut *o = &ld->outs[i];
			if (o->discard || !o->kept) continue;
			int k = snprintf(line, sizeof(line), "%-24s 0x%016llx 0x%016llx 0x%llx\n", o->name, (unsigned long long)o->vma,
				(unsigned long long)o->lma, (unsigned long long)o->size);
			buf_append(&m, line, (size_t)k);
		}
		write_out(ctx->map, &m, 0);
		free(m.data);
	}
	for (int i = 0; i < ld->nouts; i++) {
		LdOut *a = &ld->outs[i];
		if (a->discard || !a->kept || !a->size || a->st->noload || ((a->flags & 0x400) && a->nobits)) continue;
		for (int j = i + 1; j < ld->nouts; j++) {
			LdOut *b = &ld->outs[j];
			if (b->discard || !b->kept || !b->size || b->st->noload || ((b->flags & 0x400) && b->nobits)) continue;
			if (a->vma < b->vma + b->size && b->vma < a->vma + a->size)
				ld_fail(ld, "section '%s' (0x%llx-0x%llx) overlaps section '%s' (0x%llx-0x%llx)", a->name, (unsigned long long)a->vma,
					(unsigned long long)(a->vma + a->size), b->name, (unsigned long long)b->vma, (unsigned long long)(b->vma + b->size));
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
	int tls = 0;
	for (int i = 0; i < ctx->nsecs; i++) if (ctx->secs[i].tls) tls = 1;
	int nph = (ctx->omagic ? 1 : 2) + tls;
	const size_t headers = ctx->binary ? 0 : 64 + (size_t)nph * 56;
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
	size_t data_a = ctx->binary || ctx->omagic ? 16 : 0x1000;
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

	uint64_t tls_lo = UINT64_MAX, tls_hi = 0, tls_a = 1;
	for (int i = 0; i < ctx->nsecs; i++) {
		if (!ctx->secs[i].tls) continue;
		uint64_t v = sect_va[SEC_NAMED + i];
		if (v < tls_lo) tls_lo = v;
		if (v + ctx->secs[i].b.len > tls_hi) tls_hi = v + ctx->secs[i].b.len;
		if ((uint64_t)ctx->secs[i].align > tls_a) tls_a = (uint64_t)ctx->secs[i].align;
	}
	ctx->has_tls = tls;
	ctx->tls_lo = (long long)tls_lo;
	if (tls) ctx->tls_tp = (long long)(tls_lo + align_up((size_t)(tls_hi - tls_lo), (size_t)tls_a));

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
			add_elf_sym(&symtab, &strtab, s, sect_va, shndx, pass == 0 ? 0 : s->weak ? 2 : 1, ctx->tls_lo);
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
	le16(E+0x34,64); le16(E+0x36,56); le16(E+0x38,(uint16_t)nph);
	le16(E+0x3A,64); le16(E+0x3C,(uint16_t)nsh); le16(E+0x3E,(uint16_t)(nsh - 1));

	uint8_t *P=file.data+64;
	if (ctx->omagic) {
		le32(P+0x00,1); le32(P+0x04,7);
		le64(P+0x08,seg_off); le64(P+0x10,base + seg_off); le64(P+0x18,base + seg_off);
		le64(P+0x20,data_end - seg_off); le64(P+0x28,(bss_va + (uint64_t)ctx->bss_size) - (base + seg_off)); le64(P+0x30,0x1000);
	} else {
		le32(P+0x00,1); le32(P+0x04,5);
		le64(P+0x08,seg_off); le64(P+0x10,base + seg_off); le64(P+0x18,base + seg_off);
		le64(P+0x20,text_end - seg_off); le64(P+0x28,text_end - seg_off); le64(P+0x30,0x1000);
		P += 56;
		le32(P+0x00,1); le32(P+0x04,6);
		le64(P+0x08,data_off); le64(P+0x10,data_va); le64(P+0x18,data_va);
		le64(P+0x20,data_end - data_off); le64(P+0x28,(bss_va - data_va) + (uint64_t)ctx->bss_size); le64(P+0x30,0x1000);
	}
	if (tls) {
		P += 56;
		le32(P+0x00,7); le32(P+0x04,4);
		le64(P+0x08,tls_lo - base); le64(P+0x10,tls_lo); le64(P+0x18,tls_lo);
		le64(P+0x20,tls_hi - tls_lo); le64(P+0x28,tls_hi - tls_lo); le64(P+0x30,tls_a);
	}

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
			e[4] = (uint8_t)(((pass == 0 ? 0 : s->weak ? 2 : 1) << 4) | (s->tls ? 6 : !s->defined ? 0 : s->kind == SYM_FUNC ? 2 : 1));
			if (s->defined && s->tentative && s->sect == SEC_BSS && !ctx->no_common) {
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
		else if (x >= SEC_NAMED && ctx->secs[x - SEC_NAMED].tls == 2) sizes[shndx[x]] = ctx->secs[x - SEC_NAMED].b.len;
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
		int type = x == SEC_BSS || x >= ns || (x >= SEC_NAMED && ctx->secs[x - SEC_NAMED].tls == 2) ? 8 :
			x >= SEC_NAMED && !strncmp(ctx->secs[x - SEC_NAMED].name, ".note", 5) ? 7 :
			x >= SEC_NAMED ? array_sec_type(ctx->secs[x - SEC_NAMED].name) : 1;
		shdr(S + i * 64, names[i], type, (uint64_t)(x >= ns ? 3 : sec_flags(ctx, x)), 0, offs[i], sizes[i], 0, 0, (uint64_t)a, type > 1 && type != 7 && type != 8 ? 8 : 0);
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


static Sym *link_global(Ctx *ctx, char *name, int *created) {
	for (Sym *g = global_chain(ctx, name); g; g = g->gsame)
		if (!g->is_static && (g->kind == SYM_FUNC || g->kind == SYM_VAR)) { *created = 0; return g; }
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

static long link_got(Ctx *ctx, Sym *y, int tp) {
	long *slot = tp ? &y->gottp_off : &y->got_off;
	if (*slot) return *slot - 1;
	int g = section_index(ctx, intern(".got"), SK_DATA);
	Buf *b = sec_buf(ctx, g);
	raise_align(ctx, g, 8);
	buf_zero_to(b, align_up(b->len, 8));
	*slot = (long)b->len + 1;
	buf_zero_to(b, b->len + 8);
	add_reloc(ctx, tp ? R_TPOFF64 : R_ABS64, (size_t)(*slot - 1), y, 0, 0);
	ctx->relocs[ctx->nrelocs - 1].in = g;
	return *slot - 1;
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
		if (type != 8 && off + size > n) link_error(name, "section %s lies outside the file", nm);
		int sect;
		if (flags & 0x400) {
			sect = section_index(ctx, intern(nm), SK_DATA);
			ctx->secs[sect - SEC_NAMED].tls = type == 8 ? 2 : 1;
		} else if (type == 8) sect = SEC_BSS;
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
			if (type == 8) buf_zero_to(b, b->len + (size_t)size);
			else buf_append(b, d + off, (size_t)size);
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
			y->tls = type == 6;
			y->gnext = ctx->globals;
			ctx->globals = y;
			syms[k] = y;
			continue;
		}
		Sym *g = link_global(ctx, intern(nm), &created);
		syms[k] = g;
		if (type == 6) g->tls = 1;
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
			case 9: case 41: case 42: case 22:
				if (!y) link_error(name, "GOT relocation against a section");
				kind = R_PC32;
				sect = section_index(ctx, intern(".got"), SK_DATA);
				a += link_got(ctx, y, type == 22) + 4;
				y = NULL;
				piece = 0;
				break;
			case 23: kind = R_TPOFF32; break;
			case 18: kind = R_TPOFF64; break;
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
	char name[256];
	const char *path;
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
			if (bind == 0 || shn == 0) continue;
			char *id = intern(strtab + rd32(e));
			for (Sym *g = global_chain(ctx, id); g; g = g->gsame)
				if (!g->is_static && !g->defined && (!g->weak || g->strong_ref) && (shn == 0xFFF2 ? g->ref && !g->common : g->ref || g->common)) return 1;
		}
	}
	return 0;
}

typedef struct {
	const char *name;
	ArMember *ms;
	int nm;
} Ar;

static uint8_t *read_binary(const char *path, size_t *n);

static Ar *ar_open(const uint8_t *d, size_t n, const char *name) {
	Ar *ar = (Ar *)calloc(1, sizeof(Ar));
	int cap = 0, thin = n >= 8 && !memcmp(d, "!<thin>\n", 8);
	const char *longnames = NULL;
	size_t p = 8;
	ar->name = name;
	char *dir = dir_of(name);
	while (p + 60 <= n) {
		const uint8_t *h = d + p;
		char nb[17], sb[11];
		memcpy(nb, h, 16); nb[16] = 0;
		memcpy(sb, h + 48, 10); sb[10] = 0;
		size_t size = (size_t)strtoul(sb, NULL, 10);
		const uint8_t *data = h + 60;
		int special = !strncmp(nb, "//", 2) || (nb[0] == '/' && !isdigit((unsigned char)nb[1]));
		size_t step = thin && !special ? 0 : size;
		if (p + 60 + step > n) link_error(name, "truncated archive");
		if (!strncmp(nb, "//", 2)) longnames = (const char *)data;
		else if (!special) {
			if (ar->nm == cap) {
				cap = cap ? cap * 2 : 16;
				ar->ms = (ArMember *)realloc(ar->ms, sizeof(ArMember) * cap);
			}
			ArMember *m = &ar->ms[ar->nm++];
			const char *src = nb;
			if (nb[0] == '/' && longnames) src = longnames + strtoul(nb + 1, NULL, 10);
			char full[1024];
			int k = 0;
			while (k < 1000 && src[k] && src[k] != '\n' && !(src[k] == '/' && (src[k+1] == '\n' || src[k+1] == 0 || src == nb)) && !(src == nb && src[k] == ' ')) { full[k] = src[k]; k++; }
			full[k] = 0;
			snprintf(m->name, sizeof(m->name), "%.255s", full);
			if (thin) {
				char path[2048];
				if (full[0] == '/') snprintf(path, sizeof(path), "%s", full);
				else snprintf(path, sizeof(path), "%s/%s", dir, full);
				m->data = read_binary(path, &m->size);
				m->path = intern(path);
			} else {
				m->data = data;
				m->size = size;
				m->path = NULL;
			}
			m->loaded = 0;
		}
		p += 60 + step + (step & 1);
	}
	return ar;
}

static int ar_pull(Ctx *ctx, Ar *ar, int whole) {
	char full[1200];
	int any = 0;
	for (int changed = 1; changed;) {
		changed = 0;
		for (Sym *g = ctx->globals; g; g = g->gnext) g->ref = 0;
		for (int i = 0; i < ctx->nrelocs; i++) if (ctx->relocs[i].sym) ctx->relocs[i].sym->ref = 1;
		for (int i = 0; i < ar->nm; i++) {
			ArMember *m = &ar->ms[i];
			if (m->loaded || !(whole || member_needed(ctx, m))) continue;
			m->loaded = 1;
			if (m->size >= 8 && (!memcmp(m->data, "!<arch>\n", 8) || !memcmp(m->data, "!<thin>\n", 8))) {
				Ar *sub = ar_open(m->data, m->size, m->path ? m->path : ar->name);
				if (ar_pull(ctx, sub, whole)) changed = any = 1;
				continue;
			}
			if (m->path) snprintf(full, sizeof(full), "%s", m->path);
			else snprintf(full, sizeof(full), "%s(%s)", ar->name, m->name);
			ctx->arch = ar->name;
			ctx->member = intern(m->name);
			load_object(ctx, m->data, m->size, intern(full));
			ctx->arch = ctx->member = NULL;
			changed = any = 1;
		}
		if (whole) break;
	}
	return any;
}

static void load_archive(Ctx *ctx, const uint8_t *d, size_t n, const char *name) {
	ar_pull(ctx, ar_open(d, n, name), 0);
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

static void load_binary(Ctx *ctx, const char *path) {
	size_t n;
	uint8_t *d = read_binary(path, &n);
	char base[512], name[600];
	int k = 0;
	for (const char *p = path; *p && k < 511; p++) base[k++] = isalnum((unsigned char)*p) ? *p : '_';
	base[k] = 0;
	piece_begin(ctx);
	long off = (long)ctx->data.len;
	buf_append(&ctx->data, d, n);
	add_piece(ctx, SEC_DATA, off, off + (long)n, intern(".data"), path, 1);
	static const char *suffix[] = { "start", "end", "size" };
	for (int i = 0; i < 3; i++) {
		int created;
		snprintf(name, sizeof(name), "_binary_%s_%s", base, suffix[i]);
		Sym *g = link_global(ctx, intern(name), &created);
		if (g->defined) link_error(path, "multiple definition of '%s'", name);
		g->defined = 1;
		g->sect = i == 2 ? SEC_ABS : SEC_DATA;
		g->off = i == 0 ? off : i == 1 ? off + (long)n : (long)n;
		g->piece = i == 2 || !ctx->track ? 0 : ctx->npieces;
	}
	piece_end(ctx, path);
	free(d);
}

static void compile_input(Ctx *ctx, const char *path, int k) {
	if (!k) k = input_kind(path);
	if (k == 'o' || k == 'a') {
		size_t n;
		uint8_t *d = read_binary(path, &n);
		if (k == 'a') {
			if (n < 8 || (memcmp(d, "!<arch>\n", 8) && memcmp(d, "!<thin>\n", 8))) link_error(path, "not an archive");
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

static void link_inputs(Ctx *ctx, const char **inputs, int *kinds, int *binary, int n) {
	int whole = 0, group = 0, ng = 0, capg = 0;
	Ar **gs = NULL;
	for (int i = 0; i < n; i++) {
		int k = kinds[i];
		if (k == 'W' || k == 'w') { whole = k == 'W'; continue; }
		if (k == 'G') { group = 1; ng = 0; continue; }
		if (k == 'g') {
			for (int changed = 1; changed;) {
				changed = 0;
				for (int j = 0; j < ng; j++) changed |= ar_pull(ctx, gs[j], 0);
			}
			group = 0;
			continue;
		}
		if (binary[i]) { load_binary(ctx, inputs[i]); continue; }
		if (!k) k = input_kind(inputs[i]);
		if (k == 'o') {
			char m[8];
			FILE *f = fopen(inputs[i], "rb");
			if (f) {
				if (fread(m, 1, 8, f) == 8 && (!memcmp(m, "!<arch>\n", 8) || !memcmp(m, "!<thin>\n", 8))) k = 'a';
				fclose(f);
			}
		}
		if (k != 'a') { compile_input(ctx, inputs[i], k); continue; }
		size_t sz;
		uint8_t *d = read_binary(inputs[i], &sz);
		if (sz < 8 || (memcmp(d, "!<arch>\n", 8) && memcmp(d, "!<thin>\n", 8))) link_error(inputs[i], "not an archive");
		Ar *ar = ar_open(d, sz, inputs[i]);
		ar_pull(ctx, ar, whole);
		if (group) {
			if (ng == capg) gs = (Ar **)realloc(gs, sizeof(Ar *) * (size_t)(capg = capg ? capg * 2 : 16));
			gs[ng++] = ar;
		}
	}
	free(gs);
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
	ctx->k_end = (size_t)-1;
	code_init(&ctx->text);
	buf_init(&ctx->rodata);
	buf_init(&ctx->data);
	loopstack_init(&ctx->loops);
	ctx->cur_text = SEC_TEXT;
	ctx->cur_data = SEC_DATA;
}

static int ignored_flag(const char *a) {
	static const char *names[] = { "-static", "-pipe", "-P", "-C", "-no-pie", "-w", "-pedantic", NULL };
	static const char *refused[] = { "-funsigned-char", "-fshort-enums", NULL };
	for (int i = 0; refused[i]; i++) if (!strcmp(a, refused[i])) return 0;
	for (int i = 0; names[i]; i++) if (!strcmp(a, names[i])) return 1;
	if (a[1] == 'O' && (!a[2] || (strchr("0123sgz", a[2]) && !a[3]))) return 1;
	if (a[1] == 'g') return 1;
	if (a[1] == 'f' || (a[1] == 'm' && a[2])) return 1;
	if (a[1] == 'W' && a[2] && strncmp(a, "-Wl,", 4) && strncmp(a, "-Wp,", 4)) return 1;
	return 0;
}

static void preprocess_input(Ctx *ctx, const char *path, int k, Buf *out) {
	if (!k) k = input_kind(path);
	int as = k == 'S' || k == 's';
	char *src = read_file(path);
	lex_asm = as;
	pp_begin_unit(ctx);
	if (as) pp_define_str(ctx, "__ASSEMBLER__ 1");
	push_file(ctx, src, path, 0);
	ctx->src->L.as = as;
	push_includes(ctx, as);
	ctx->has_pk = 0;
	ctx->has_pp_back = 0;
	int first = 1;
	for (;;) {
		Tok t = pp_next(ctx);
		if (t.kind == T_EOF) break;
		if (!first && t.bol) buf_append_byte(out, '\n');
		else if (!first && t.space) buf_append_byte(out, ' ');
		if (t.raw) buf_append(out, t.raw, (size_t)t.rawlen);
		else tok_spell(out, &t);
		first = 0;
	}
	buf_append_byte(out, '\n');
	pp_end_unit(ctx);
	lex_asm = 0;
}

static void write_deps(Ctx *ctx, const char *depfile, const char *target) {
	Buf b; buf_init(&b);
	buf_append(&b, target, strlen(target));
	buf_append_byte(&b, ':');
	for (int i = 0; i < ctx->ndeps; i++) {
		buf_append(&b, " \\\n  ", 5);
		buf_append(&b, ctx->deps[i], strlen(ctx->deps[i]));
	}
	buf_append_byte(&b, '\n');
	write_out(depfile, &b, 0);
	free(b.data);
}

static const char *include_dir(const char *argv0) {
	static char path[4096];
	const char *slash = strrchr(argv0, '/');
	if (!slash) return "include";
	snprintf(path, sizeof(path), "%.*s/lib/include", (int)(slash - argv0), argv0);
	return path;
}

static Ctx *unit_ctx(Ctx *base) {
	Ctx *u = (Ctx *)malloc(sizeof(Ctx));
	ctx_init(u);
	memcpy(u->incdirs, base->incdirs, sizeof(base->incdirs));
	u->nincdirs = base->nincdirs;
	memcpy(u->defines, base->defines, sizeof(base->defines));
	u->ndefines = base->ndefines;
	memcpy(u->includes, base->includes, sizeof(base->includes));
	u->nincludes = base->nincludes;
	memcpy(u->undefs, base->undefs, sizeof(base->undefs));
	u->nundefs = base->nundefs;
	u->nostdlib = base->nostdlib;
	u->freestanding = base->freestanding;
	u->nostdinc = base->nostdinc;
	u->no_common = base->no_common;
	u->no_float = base->no_float;
	u->gnu = base->gnu;
	u->fold = base->fold;
	u->gnu89 = base->gnu89;
	u->m32 = base->m32;
	u->kmodel = base->kmodel;
	u->no_sse = base->no_sse;
	u->track_deps = base->track_deps;
	u->track = 1;
	return u;
}

static char *derived_name(const char *path, const char *ext) {
	static char name[1024];
	const char *b = strrchr(path, '/');
	b = b ? b + 1 : path;
	snprintf(name, sizeof(name), "%s", b);
	char *dot = strrchr(name, '.');
	if (dot) *dot = 0;
	if (strlen(name) + strlen(ext) < sizeof(name)) strcat(name, ext);
	return name;
}

static int lang_kind(Ctx *ctx, const char *l) {
	(void)ctx;
	if (!strcmp(l, "c")) return 'c';
	if (!strcmp(l, "assembler")) return 's';
	if (!strcmp(l, "assembler-with-cpp")) return 'S';
	if (!strcmp(l, "none")) return 0;
	fprintf(stderr, "unknown language: %s\n", l);
	exit(1);
}

int main(int argc, char **argv) {
	const char *out = NULL;
	char **args = (char **)malloc(sizeof(char *) * (size_t)(argc * 2 + 64));
	int nargs = 0;
	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "-Wl,", 4)) {
			char *p = argv[i] + 4;
			for (;;) {
				char *c = strchr(p, ',');
				if (c) *c = 0;
				args = (char **)realloc(args, sizeof(char *) * (size_t)(nargs + 2));
				args[nargs++] = p;
				if (!c) break;
				p = c + 1;
			}
		} else {
			args = (char **)realloc(args, sizeof(char *) * (size_t)(nargs + 2));
			args[nargs++] = argv[i];
		}
	}
	const char **inputs = (const char **)malloc(sizeof(char *) * (size_t)(nargs + 1));
	int *binary_in = (int *)malloc(sizeof(int) * (size_t)(nargs + 1));
	int *in_kind = (int *)malloc(sizeof(int) * (size_t)(nargs + 1));
	int as_binary = 0, lang = 0, mode = 0, version = 0, want_deps = 0, ld_style = 0;
	const char *entry = NULL, *script = NULL, *depfile = NULL, *deptarget = NULL;
	int ninputs = 0, oformat_given = 0, nlibdirs = 0;
	const char **libdirs = (const char **)malloc(sizeof(char *) * (size_t)(nargs + 1));
	Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
	ctx_init(ctx);
	for (int i=0;i<nargs;i++) {
		const char *a = args[i];
		int more = i + 1 < nargs;
		if (strcmp(a,"-o")==0&&more) out=args[++i];
		else if (strcmp(a,"-c")==0) mode = 'c';
		else if (strcmp(a,"-E")==0) mode = 'E';
		else if (strcmp(a,"-S")==0) mode = 'S';
		else if (strcmp(a,"-x")==0&&more) lang = lang_kind(ctx, args[++i]);
		else if (!strncmp(a,"-x",2)&&a[2]) lang = lang_kind(ctx, a+2);
		else if (!strcmp(a,"-v") || !strcmp(a,"--version") || !strcmp(a,"-V")) version = 1;
		else if (!strcmp(a,"-dumpversion")) { printf("4.9.0\n"); return 0; }
		else if (!strcmp(a,"-dumpmachine")) { printf("x86_64-linux-gnu\n"); return 0; }
		else if (!strncmp(a,"-print-file-name=",17)) {
			printf("%s\n", strcmp(a+17,"include") ? a+17 : include_dir(argv[0]));
			return 0;
		}
		else if (!strncmp(a,"-std=",5)) { ctx->gnu = !strncmp(a+5,"gnu",3); ctx->gnu89 = !strcmp(a+5,"gnu89") || !strcmp(a+5,"gnu90"); }
		else if (!strcmp(a,"-ansi")) ctx->gnu = 0;
		else if (strcmp(a,"-include")==0&&more) { if (ctx->nincludes < 64) ctx->includes[ctx->nincludes++] = args[++i]; else i++; }
		else if ((!strcmp(a,"-isystem") || !strcmp(a,"-idirafter") || !strcmp(a,"-iquote"))&&more) {
			if (ctx->nincdirs < 64) ctx->incdirs[ctx->nincdirs++] = args[++i]; else i++;
		}
		else if (a[0]=='-' && a[1]=='U' && ctx->nundefs < 64) ctx->undefs[ctx->nundefs++] = a[2] ? a+2 : (more ? args[++i] : "");
		else if (!strcmp(a,"-MD") || !strcmp(a,"-MMD")) want_deps = 1;
		else if (!strcmp(a,"-MF")&&more) depfile = args[++i];
		else if ((!strcmp(a,"-MT") || !strcmp(a,"-MQ"))&&more) deptarget = args[++i];
		else if (!strcmp(a,"-MP") || !strcmp(a,"-M") || !strcmp(a,"-MM")) {}
		else if (!strncmp(a,"-Wp,",4)) {
			char buf[1024];
			snprintf(buf, sizeof(buf), "%s", a+4);
			char *c = strchr(buf, ',');
			if (c && (!strncmp(buf,"-MD,",4) || !strncmp(buf,"-MMD,",5))) { want_deps = 1; depfile = intern(c+1); }
			else if (buf[0]=='-' && buf[1]=='D' && ctx->ndefines < 64) ctx->defines[ctx->ndefines++] = intern(buf+2);
			else if (buf[0]=='-' && buf[1]=='U' && ctx->nundefs < 64) ctx->undefs[ctx->nundefs++] = intern(buf+2);
		}
		else if (!strncmp(a,"-Wa,",4)) {}
		else if (a[0]=='-' && a[1]=='I' && ctx->nincdirs < 64) ctx->incdirs[ctx->nincdirs++] = a[2] ? a+2 : (more ? args[++i] : ".");
		else if (a[0]=='-' && a[1]=='D' && ctx->ndefines < 64) ctx->defines[ctx->ndefines++] = a[2] ? a+2 : (more ? args[++i] : "");
		else if (strcmp(a,"-nostdlib")==0) ctx->nostdlib = 1;
		else if (strcmp(a,"-nostartfiles")==0) ctx->nostart = 1;
		else if (a[0]=='-' && a[1]=='L' && (a[2] || more)) libdirs[nlibdirs++] = a[2] ? a+2 : args[++i];
		else if (a[0]=='-' && a[1]=='l' && (a[2] || more)) {
			const char *n = a[2] ? a+2 : args[++i];
			char *found = NULL;
			for (int d = 0; d < nlibdirs && !found; d++) {
				char path[4096];
				if (n[0] == ':') snprintf(path, sizeof(path), "%s/%s", libdirs[d], n + 1);
				else snprintf(path, sizeof(path), "%s/lib%s.a", libdirs[d], n);
				FILE *lf = fopen(path, "rb");
				if (lf) { fclose(lf); found = intern(path); }
			}
			if (!found) { fprintf(stderr, "error: cannot find -l%s\n", n); return 1; }
			binary_in[ninputs] = as_binary;
			in_kind[ninputs] = 0;
			inputs[ninputs++] = found;
		}
		else if (strcmp(a,"-ffreestanding")==0) ctx->freestanding = ctx->nostdlib = 1;
		else if (strcmp(a,"-nodefaultlibs")==0) ctx->nostart = ctx->nostdlib = 1;
		else if (strcmp(a,"-nostdinc")==0) ctx->nostdinc = 1;
		else if (strcmp(a,"-fno-common")==0) ctx->no_common = 1;
		else if (strcmp(a,"-fcommon")==0) ctx->no_common = 0;
		else if (strcmp(a,"-N")==0) ctx->omagic = 1;
		else if ((strcmp(a,"-m")==0&&more) || strncmp(a,"-melf",5)==0) {
			const char *m = a[2] ? a+2 : args[++i];
			if (!strcmp(m,"elf_i386")) ctx->elf32 = 1;
			else if (strcmp(m,"elf_x86_64")) { fprintf(stderr,"unsupported emulation: %s\n",m); return 1; }
			if (!ld_style) ld_style = 2;
		}
		else if (!strcmp(a,"-r") || !strcmp(a,"--relocatable")) { mode = 'r'; ld_style = 1; }
		else if (!strcmp(a,"-shared")) { ctx->shared = 1; ld_style = 1; }
		else if (!strcmp(a,"-Map")&&more) ctx->map = args[++i];
		else if (!strncmp(a,"-Map=",5)) ctx->map = a + 5;
		else if ((!strcmp(a,"-soname") || !strcmp(a,"-h"))&&more) ctx->soname = args[++i];
		else if (!strncmp(a,"-soname=",8)) ctx->soname = a + 8;
		else if (!strcmp(a,"-Bsymbolic")) {}
		else if (!strcmp(a,"-z")&&more) { a = args[++i]; if (!strncmp(a,"max-page-size=",14)) ctx->max_page = strtoull(a+14, NULL, 0); }
		else if (!strncmp(a,"-z",2)&&a[2]) { if (!strncmp(a+2,"max-page-size=",14)) ctx->max_page = strtoull(a+16, NULL, 0); }
		else if (!strcmp(a,"--emit-relocs") || !strcmp(a,"-q")) ctx->emit_relocs = 1;
		else if (!strcmp(a,"--build-id") || !strncmp(a,"--build-id=",11) || !strcmp(a,"--discard-none") || !strcmp(a,"--strip-debug") ||
			!strcmp(a,"--gc-sections") || !strcmp(a,"--no-undefined") || !strncmp(a,"--hash-style=",13) || !strcmp(a,"-EL") || !strcmp(a,"--eh-frame-hdr") ||
			!strcmp(a,"--warn-common") || !strcmp(a,"--verbose") || !strcmp(a,"--sort-common") || !strncmp(a,"--sort-section=",15)) {}
		else if (!strcmp(a,"--sort-section")&&more) i++;
		else if (!strcmp(a,"--whole-archive") || !strcmp(a,"--no-whole-archive") || !strcmp(a,"--start-group") || !strcmp(a,"--end-group") ||
			!strcmp(a,"-(") || !strcmp(a,"-)")) {
			binary_in[ninputs] = 0;
			in_kind[ninputs] = a[2] == 'w' ? 'W' : a[2] == 'n' ? 'w' : a[2] == 's' || a[1] == '(' ? 'G' : 'g';
			inputs[ninputs++] = a;
		}
		else if (strcmp(a,"-mgeneral-regs-only")==0) ctx->no_float = 1;
		else if (!strcmp(a,"-m32") || !strcmp(a,"-m16")) ctx->m32 = 1;
		else if (!strncmp(a,"-mcmodel=",9)) ctx->kmodel = !strcmp(a+9,"kernel");
		else if (!strcmp(a,"-mno-sse")) ctx->no_sse = 1;
		else if (!strcmp(a,"-msse")) ctx->no_sse = 0;
		else if (!strcmp(a,"-m64")) ctx->m32 = 0;
		else if (a[0]=='-' && a[1]=='O' && (!a[2] || (strchr("0123sgz", a[2]) && !a[3]))) ctx->fold = strcmp(a, "-O0") != 0;
		else if (a[0]=='-' && ignored_flag(a)) {}
		else if (strcmp(a,"-e")==0&&more) entry=args[++i];
		else if (strcmp(a,"-T")==0&&more) script=args[++i];
		else if (strcmp(a,"-Ttext")==0&&more) {
			char *end;
			a = args[++i];
			ctx->ttext = strtoull(a, &end, 0);
			ctx->has_ttext = 1;
			if (!*a || *end) { fprintf(stderr,"bad address: %s\n",a); return 1; }
		}
		else if (strcmp(a,"-oformat")==0&&more) {
			a = args[++i];
			oformat_given = 1;
			ctx->binary = strcmp(a,"elf") != 0;
			ctx->image = strcmp(a,"img") == 0;
			ctx->multiboot = strcmp(a,"multiboot") == 0;
			if (ctx->binary && !ctx->image && !ctx->multiboot && strcmp(a,"bin") != 0) { fprintf(stderr,"unknown output format: %s\n",a); return 1; }
		}
		else if (strcmp(a,"-b")==0&&more) {
			a = args[++i];
			if (strcmp(a,"binary") && strcmp(a,"elf64-x86-64") && strcmp(a,"default")) { fprintf(stderr,"unknown input format: %s\n",a); return 1; }
			as_binary = strcmp(a,"binary") == 0;
		}
		else if (a[0]=='-' && a[1]) { fprintf(stderr,"unknown arg: %s\n",a); return 1; }
		else { binary_in[ninputs] = as_binary; in_kind[ninputs] = lang; inputs[ninputs++] = a; }
	}
	if (version && ninputs == 0) { printf("sccc version 1.0 (x86-64)\n"); return 0; }
	int sources = 0;
	for (int i = 0; i < ninputs; i++) {
		int k = in_kind[i] ? in_kind[i] : input_kind(inputs[i]);
		if (!binary_in[i] && (k == 'c' || k == 's' || k == 'S')) sources = 1;
	}
	if (ctx->elf32 && sources) { fprintf(stderr,"-m elf_i386 links objects assembled with -m32 or -m16, not sources\n"); return 1; }
	if (ld_style == 1 || (ld_style == 2 && !sources)) ctx->nostdlib = ctx->nostart = 1;
	ctx->track_deps = want_deps;
	if (script && mode == 'r') script = NULL;
	if (script) {
		if (mode) { fprintf(stderr,"-T needs a link, not -c, -E or -S\n"); return 1; }
		if (ctx->has_ttext) { fprintf(stderr,"-Ttext cannot be used with -T\n"); return 1; }
		ctx->ld = ld_read(script);
		ctx->track = 1;
		if (!oformat_given) ctx->binary = ctx->ld->format == 2;
		if (ctx->ld->format == 3) ctx->elf32 = 1;
		ctx->ld->entry_cmd = entry;
	} else if ((ctx->shared || ctx->elf32) && !mode && !sources) {
		ctx->ld = ld_default(ctx);
		ctx->track = 1;
		ctx->ld->entry_cmd = entry;
	}
	if (ctx->image && !ctx->has_ttext && !script) { ctx->ttext = 0x100000; ctx->has_ttext = 1; }
	if (ctx->multiboot && !ctx->has_ttext && !script) { ctx->ttext = 0x101000; ctx->has_ttext = 1; }
	if (ctx->multiboot && (ctx->ttext & 0xFFF)) { fprintf(stderr,"-Ttext must be page aligned for multiboot\n"); return 1; }
	if (ctx->no_float && !ctx->nostdlib) { fprintf(stderr,"-mgeneral-regs-only needs -nostdlib or -ffreestanding\n"); return 1; }
	if (ninputs == 0) { fprintf(stderr,"usage: %s <file.c|.s|.S|.o|.a>... [-c|-E|-S] [-o <out>] [-I dir] [-D name[=val]] [-nostdlib]\n"
			"       [-ffreestanding] [-e sym] [-Ttext addr] [-T script] [-oformat elf|bin|img|multiboot]\n"
			"       [-mgeneral-regs-only]\n",argv[0]); return 1; }

	keywords_init();
	types_init();
	id_main = intern("main");
	id_syscall = intern("__syscall");
	id_va_start = intern("__builtin_va_start");
	id_va_arg = intern("__builtin_va_arg");
	id_va_end = intern("__builtin_va_end");
	id_va_copy = intern("__builtin_va_copy");
	id_va_list = intern("__builtin_va_list");
	id_exit = intern("exit");
	id_libc_init = intern("__libc_init");
	id_environ = intern("environ");
	id_defined = intern("defined");
	id_file = intern("__FILE__");
	id_line = intern("__LINE__");
	id_counter = intern("__COUNTER__");
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
	id_return_address = intern("__builtin_return_address");
	id_frame_address = intern("__builtin_frame_address");
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

	if (mode == 'E') {
		Buf b; buf_init(&b);
		for (int i = 0; i < ninputs; i++) {
			Ctx *u = unit_ctx(ctx);
			preprocess_input(u, inputs[i], in_kind[i], &b);
			if (want_deps) write_deps(u, depfile ? depfile : derived_name(inputs[i], ".d"), deptarget ? deptarget : out ? out : derived_name(inputs[i], ".o"));
		}
		write_out(out ? out : "-", &b, 0);
		return 0;
	}
	if (mode == 'r') {
		if (!out) out = "a.out";
		link_inputs(ctx, inputs, in_kind, binary_in, ninputs);
		write_object(ctx, out);
		return 0;
	}
	if (mode) {
		if (out && ninputs > 1) { fprintf(stderr,"-o with -c or -S needs a single input file\n"); return 1; }
		if (as_binary) { fprintf(stderr,"-b binary needs a link, not -c\n"); return 1; }
		for (int i = 0; i < ninputs; i++) {
			int k = in_kind[i] ? in_kind[i] : input_kind(inputs[i]);
			if (k == 'o' || k == 'a') { fprintf(stderr,"%s: error: -c needs a C or assembly file\n", inputs[i]); return 1; }
			Ctx *u = unit_ctx(ctx);
			Buf text; buf_init(&text);
			if (mode == 'S') u->asm_text = &text;
			compile_input(u, inputs[i], k);
			const char *o = out ? out : derived_name(inputs[i], mode == 'S' ? ".s" : ".o");
			if (mode == 'S') write_out(o, &text, 0);
			else write_object(u, o);
			if (want_deps) write_deps(u, depfile ? depfile : derived_name(o, ".d"), deptarget ? deptarget : o);
		}
		return 0;
	}
	if (!out) out = "a.out";

	Type *mt = func_type(ty_int);
	mt->oldstyle = 1;
	if (ctx->nostart) ctx->freestanding = 1;
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

	link_inputs(ctx, inputs, in_kind, binary_in, ninputs);
	if (!ctx->nostdlib) {
		ctx->in_libc = 1;
		for (int i = 0; i < ctx->nsecs; i++)
			if (ctx->secs[i].tls && ctx->ndefines < 64) { ctx->defines[ctx->ndefines++] = "__SCCC_TLS__"; break; }
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
