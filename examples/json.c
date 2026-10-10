#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

static const char *p;
static int ok = 1;
static int nvalues, maxdepth;

static void indent(int d) {
	for (int i = 0; i < d; i++) printf("  ");
}

static void ws(void) {
	while (isspace((unsigned char)*p)) p++;
}

static void value(int depth);

static void string(void) {
	putchar(*p++);
	while (*p && *p != '"') {
		if (*p == '\\') putchar(*p++);
		putchar(*p++);
	}
	if (*p != '"') ok = 0;
	else putchar(*p++);
}

static void container(int depth, char open, char close) {
	if (depth > maxdepth) maxdepth = depth;
	putchar(*p++);
	ws();
	if (*p == close) {
		putchar(*p++);
		return;
	}
	putchar('\n');
	for (;;) {
		indent(depth + 1);
		ws();
		if (open == '{') {
			if (*p != '"') { ok = 0; return; }
			string();
			ws();
			if (*p != ':') { ok = 0; return; }
			p++;
			printf(": ");
		}
		value(depth + 1);
		ws();
		if (*p == ',') {
			p++;
			printf(",\n");
			continue;
		}
		if (*p != close) { ok = 0; return; }
		putchar('\n');
		indent(depth);
		putchar(*p++);
		return;
	}
}

static void value(int depth) {
	ws();
	nvalues++;
	if (*p == '{') container(depth, '{', '}');
	else if (*p == '[') container(depth, '[', ']');
	else if (*p == '"') string();
	else if (*p == '-' || isdigit((unsigned char)*p)) {
		char *end;
		double v = strtod(p, &end);
		printf("%.15g", v);
		p = end;
	} else if (isalpha((unsigned char)*p)) {
		while (isalpha((unsigned char)*p)) putchar(*p++);
	} else ok = 0;
}

int main(void) {
	static char buf[16384];
	size_t n = fread(buf, 1, sizeof buf - 1, stdin);
	buf[n] = 0;
	p = buf;
	value(0);
	ws();
	putchar('\n');
	if (*p) ok = 0;
	printf("%s: %d values, depth %d\n", ok ? "valid" : "invalid", nvalues, maxdepth);
	return 0;
}
