#include <stdio.h>
#include <string.h>

static int match_here(const char *re, const char *text);

static int match_star(int c, const char *re, const char *text) {
	do {
		if (match_here(re, text)) return 1;
	} while (*text != '\0' && (*text++ == c || c == '.'));
	return 0;
}

static int match_here(const char *re, const char *text) {
	if (re[0] == '\0') return 1;
	if (re[1] == '*') return match_star(re[0], re + 2, text);
	if (re[0] == '$' && re[1] == '\0') return *text == '\0';
	if (*text != '\0' && (re[0] == '.' || re[0] == *text)) return match_here(re + 1, text + 1);
	return 0;
}

static int match(const char *re, const char *text) {
	if (re[0] == '^') return match_here(re + 1, text);
	do {
		if (match_here(re, text)) return 1;
	} while (*text++ != '\0');
	return 0;
}

int main(int argc, char **argv) {
	static char lines[64][128];
	int n = 0;
	while (n < 64 && fgets(lines[n], sizeof lines[n], stdin)) {
		lines[n][strcspn(lines[n], "\n")] = 0;
		n++;
	}
	for (int a = 1; a < argc; a++) {
		int hits = 0;
		printf("pattern \"%s\":\n", argv[a]);
		for (int i = 0; i < n; i++)
			if (match(argv[a], lines[i])) {
				printf("  %2d: %s\n", i + 1, lines[i]);
				hits++;
			}
		printf("  %d matching lines\n", hits);
	}
	return 0;
}
