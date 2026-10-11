/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double stack[64];
static int sp;

static int need(int n) {
	if (sp >= n) return 1;
	printf("  stack underflow\n");
	return 0;
}

int main(void) {
	char tok[64];
	for (;;) {
		int c, n = 0;
		while ((c = getchar()) == ' ' || c == '\t');
		if (c == EOF) break;
		if (c == '\n') {
			if (sp) printf("= %g\n", stack[sp - 1]);
			sp = 0;
			continue;
		}
		while (c != EOF && c != ' ' && c != '\t' && c != '\n') {
			if (n < 63) tok[n++] = (char)c;
			c = getchar();
		}
		tok[n] = 0;
		if (c == '\n') ungetc(c, stdin);
		char *end;
		double v = strtod(tok, &end);
		if (*end == 0) {
			stack[sp++] = v;
		} else if (strlen(tok) == 1 && strchr("+-*/", tok[0])) {
			if (!need(2)) continue;
			double b = stack[--sp], a = stack[--sp];
			switch (tok[0]) {
			case '+': a += b; break;
			case '-': a -= b; break;
			case '*': a *= b; break;
			case '/': a /= b; break;
			}
			stack[sp++] = a;
		} else if (strcmp(tok, "dup") == 0) {
			if (need(1)) { stack[sp] = stack[sp - 1]; sp++; }
		} else if (strcmp(tok, "swap") == 0) {
			if (need(2)) { double t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; }
		} else printf("  unknown token '%s'\n", tok);
	}
	return 0;
}
