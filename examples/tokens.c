/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

static const char *keywords[] = { "int", "char", "void", "return", "if", "else", "while", "for", "static", "const", "struct", "sizeof", "unsigned", "long", NULL };

static int is_keyword(const char *s, int n) {
	for (int i = 0; keywords[i]; i++)
		if ((int)strlen(keywords[i]) == n && strncmp(keywords[i], s, n) == 0) return 1;
	return 0;
}

int main(void) {
	static char src[8192];
	size_t len = fread(src, 1, sizeof src - 1, stdin);
	src[len] = 0;
	int counts[6] = { 0 }, line = 1;
	const char *kinds[] = { "keyword", "ident", "number", "string", "punct", "comment" };
	const char *p = src;
	while (*p) {
		if (*p == '\n') { line++; p++; continue; }
		if (isspace((unsigned char)*p)) { p++; continue; }
		const char *start = p;
		int kind;
		if (p[0] == '/' && p[1] == '*') {
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) line += *p++ == '\n';
			if (*p) p += 2;
			kind = 5;
		} else if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n') p++;
			kind = 5;
		} else if (isalpha((unsigned char)*p) || *p == '_') {
			while (isalnum((unsigned char)*p) || *p == '_') p++;
			kind = is_keyword(start, (int)(p - start)) ? 0 : 1;
		} else if (isdigit((unsigned char)*p)) {
			while (isalnum((unsigned char)*p) || *p == '.') p++;
			kind = 2;
		} else if (*p == '"' || *p == '\'') {
			char q = *p++;
			while (*p && *p != q) p += *p == '\\' && p[1] ? 2 : 1;
			if (*p) p++;
			kind = 3;
		} else {
			static const char *ops[] = { "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", NULL };
			int n = 1;
			for (int i = 0; ops[i]; i++)
				if (strncmp(p, ops[i], strlen(ops[i])) == 0) { n = (int)strlen(ops[i]); break; }
			p += n;
			kind = 4;
		}
		counts[kind]++;
		if (kind != 5) printf("%3d %-7s %.*s\n", line, kinds[kind], (int)(p - start), start);
	}
	printf("\n");
	for (int i = 0; i < 6; i++) printf("%-8s %d\n", kinds[i], counts[i]);
	return 0;
}
