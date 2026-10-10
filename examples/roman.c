#include <stdio.h>
#include <string.h>

static const struct {
	int value;
	const char *sym;
} table[] = {
	{ 1000, "M" }, { 900, "CM" }, { 500, "D" }, { 400, "CD" }, { 100, "C" }, { 90, "XC" },
	{ 50, "L" }, { 40, "XL" }, { 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" },
};

static char *to_roman(int n, char *buf) {
	buf[0] = 0;
	for (int i = 0; n > 0; i++)
		while (n >= table[i].value) {
			strcat(buf, table[i].sym);
			n -= table[i].value;
		}
	return buf;
}

static int digit(char c) {
	switch (c) {
	case 'I': return 1;
	case 'V': return 5;
	case 'X': return 10;
	case 'L': return 50;
	case 'C': return 100;
	case 'D': return 500;
	case 'M': return 1000;
	}
	return 0;
}

static int from_roman(const char *s) {
	int total = 0;
	for (; *s; s++) {
		int v = digit(*s);
		total += digit(s[1]) > v ? -v : v;
	}
	return total;
}

int main(void) {
	char buf[32];
	int samples[] = { 1, 4, 9, 14, 40, 90, 400, 1066, 1984, 1999, 2026, 3888, 3999 };
	for (int i = 0; i < 13; i++) {
		to_roman(samples[i], buf);
		printf("%4d = %-15s -> %d\n", samples[i], buf, from_roman(buf));
	}
	int bad = 0;
	for (int n = 1; n < 4000; n++)
		if (from_roman(to_roman(n, buf)) != n) bad++;
	printf("round trip 1..3999: %d mismatches\n", bad);
	return 0;
}
