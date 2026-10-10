#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "t14_multi.h"

static int helper(int x) { return x * 100; }
int shared_counter = 5;

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n + 1000; }

int main(void) {
	struct point p = make_point(3, 4);
	printf("%d %d %d\n", p.x, p.y, helper(1));
	bump_counter();
	bump_counter();
	printf("%d %d\n", shared_counter, get_calls());
	printf("%s\n", greeting);
	printf("%zu\n", strlen("abc"));
	const char *path = "/tmp/cc_t14_test.txt";
	FILE *f = fopen(path, "w");
	if (!f) { perror("fopen"); return 1; }
	for (int i = 0; i < 1000; i++) fprintf(f, "line %d\n", i);
	fclose(f);
	f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(size + 1);
	size_t got = fread(buf, 1, size, f);
	buf[got] = 0;
	fseek(f, 5, SEEK_SET);
	int c = fgetc(f);
	char line[64];
	fseek(f, -9, SEEK_END);
	fgets(line, sizeof line, f);
	fclose(f);
	remove(path);
	int lines = 0;
	for (char *q = buf; *q; q++) if (*q == '\n') lines++;
	printf("%ld %zu %d %c %s", size, got, lines, c, line);
	if (fopen("/nonexistent/x", "r") == NULL) printf("missing ok\n");
	return 0;
}
