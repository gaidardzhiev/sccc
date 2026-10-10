#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static const char *p;
static double vars[26];
static int failed;

static double expr(void);

static void skip(void) {
	while (*p == ' ' || *p == '\t') p++;
}

static void fail(const char *msg) {
	if (!failed) printf("error: %s\n", msg);
	failed = 1;
}

static double primary(void) {
	skip();
	if (*p == '(') {
		p++;
		double v = expr();
		skip();
		if (*p != ')') fail("missing )");
		else p++;
		return v;
	}
	if (*p == '-') {
		p++;
		return -primary();
	}
	if (isdigit((unsigned char)*p) || *p == '.') {
		char *end;
		double v = strtod(p, &end);
		p = end;
		return v;
	}
	if (islower((unsigned char)*p)) {
		char name[16];
		int n = 0;
		while (isalpha((unsigned char)*p) && n < 15) name[n++] = *p++;
		name[n] = 0;
		skip();
		if (*p == '(') {
			p++;
			double v = expr();
			skip();
			if (*p != ')') fail("missing )");
			else p++;
			if (strcmp(name, "sqrt") == 0) return sqrt(v);
			if (strcmp(name, "abs") == 0) return fabs(v);
			if (strcmp(name, "floor") == 0) return floor(v);
			fail("unknown function");
			return 0;
		}
		if (n != 1) {
			fail("unknown variable");
			return 0;
		}
		return vars[name[0] - 'a'];
	}
	fail("syntax error");
	return 0;
}

static double power(void) {
	double v = primary();
	skip();
	if (*p == '^') {
		p++;
		return pow(v, power());
	}
	return v;
}

static double term(void) {
	double v = power();
	for (;;) {
		skip();
		if (*p == '*') {
			p++;
			v *= power();
		} else if (*p == '/') {
			p++;
			double d = power();
			if (d == 0) fail("division by zero");
			else v /= d;
		} else if (*p == '%') {
			p++;
			v = fmod(v, power());
		} else return v;
	}
}

static double expr(void) {
	double v = term();
	for (;;) {
		skip();
		if (*p == '+') {
			p++;
			v += term();
		} else if (*p == '-') {
			p++;
			v -= term();
		} else return v;
	}
}

int main(void) {
	char line[256];
	printf("calc: + - * / %% ^ ( ), sqrt abs floor, variables a-z (x = 1), q to quit\n");
	for (;;) {
		printf("> ");
		if (!fgets(line, sizeof line, stdin)) break;
		line[strcspn(line, "\n")] = 0;
		p = line;
		skip();
		if (*p == 0) continue;
		if (*p == 'q' && p[1] == 0) break;
		int target = -1;
		if (islower((unsigned char)p[0])) {
			const char *q = p + 1;
			while (*q == ' ') q++;
			if (*q == '=' && !isalpha((unsigned char)p[1])) {
				target = p[0] - 'a';
				p = q + 1;
			}
		}
		failed = 0;
		double v = expr();
		skip();
		if (*p) fail("unexpected input");
		if (failed) continue;
		if (target >= 0) vars[target] = v;
		printf("%.10g\n", v);
	}
	printf("\nbye\n");
	return 0;
}
