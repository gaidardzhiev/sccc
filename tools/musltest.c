/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static jmp_buf jb;

static int cmp(const void *a, const void *b) {
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static int scmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void jump(int n) { if (n > 3) longjmp(jb, n); jump(n + 1); }

int main(int argc, char **argv) {
	printf("argc=%d argv0=%s\n", argc, strrchr(argv[0], '/') ? "path" : argv[0]);
	printf("ints: %d %i %u %ld %lld %x %X %o %#x %+d % d %05d %-5d| %hhd %hd %zu %jd\n",
		-42, 42, 4000000000u, -1234567890123L, LLONG_MIN, 0xbeefu, 0xbeefu, 0755u, 255, 7, 7, 42, 42, (signed char)200, (short)70000, (size_t)-1, (intmax_t)INT64_MAX);
	printf("doubles: %f %e %g %.3f %.10g %g %g %g %a\n", 3.14159265358979, 1e-300, 123456789.0, -0.0005, 1.0 / 3, 1e300 * 1e10, -1e300 * 1e10, 0.0 / 0.0 * 0.0, 1.5);
	printf("long doubles: %Lf %Le %.20Lg %Lg\n", 3.14159265358979323846L, 1e-4000L, 1.0L / 3, (long double)LLONG_MAX);
	printf("math: %.17g %.17g %.17g %.17g %.17g %.17g\n", sqrt(2.0), sin(1.0), exp(1.0), log(10.0), pow(2.0, 0.5), atan2(1.0, 2.0));
	printf("mathl: %.20Lg %.20Lg %.20Lg\n", sqrtl(2.0L), expl(1.0L), logl(10.0L));
	char buf[128];
	int n = snprintf(buf, sizeof buf, "%s-%c-%5.2s-%*d", "abc", 'x', "hello", 6, 9);
	printf("snprintf: %d [%s]\n", n, buf);
	char *p = malloc(1000);
	for (int i = 0; i < 1000; i++) p[i] = (char)i;
	p = realloc(p, 100000);
	long sum = 0;
	for (int i = 0; i < 1000; i++) sum += (unsigned char)p[i];
	free(p);
	void *blocks[200];
	for (int i = 0; i < 200; i++) blocks[i] = calloc(i + 1, 33);
	for (int i = 0; i < 200; i += 2) free(blocks[i]);
	for (int i = 1; i < 200; i += 2) free(blocks[i]);
	printf("malloc: %ld\n", sum);
	int a[] = { 5, -3, 99, 0, 42, 7, -100, 13 };
	qsort(a, 8, sizeof a[0], cmp);
	for (int i = 0; i < 8; i++) printf("%d ", a[i]);
	char *words[] = { "pear", "apple", "fig", "banana", "cherry" };
	qsort(words, 5, sizeof words[0], scmp);
	for (int i = 0; i < 5; i++) printf("%s ", words[i]);
	printf("\n");
	printf("strings: %zu %d %s %s %d %ld %lu %.6f\n", strlen("hello world"), strncmp("abcd", "abce", 3), strstr("haystack needle", "need"),
		strchr("path/to/file", '/'), toupper('q'), strtol("-0x1f", NULL, 16), strtoul("777", NULL, 8), strtod("2.5e-3", NULL));
	char tok[] = "a,b,,c";
	for (char *t = strtok(tok, ","); t; t = strtok(NULL, ",")) printf("<%s>", t);
	printf("\n");
	setenv("SCCC_TEST", "yes", 1);
	printf("env: %s %s\n", getenv("SCCC_TEST"), getenv("SCCC_NOPE") ? "set" : "unset");
	errno = 0;
	int fd = open("/nonexistent/sccc", O_RDONLY);
	printf("errno: %d %d %s\n", fd, errno == ENOENT, strerror(ENOENT));
	int r = setjmp(jb);
	if (!r) jump(0);
	printf("setjmp: %d\n", r);
	struct tm tm;
	time_t t0 = 1700000000;
	gmtime_r(&t0, &tm);
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
	printf("time: %s\n", buf);
	FILE *f = tmpfile();
	fprintf(f, "line one\nline %d\n", 2);
	rewind(f);
	while (fgets(buf, sizeof buf, f)) fputs(buf, stdout);
	fclose(f);
	int x, y;
	char w[16];
	printf("sscanf: %d", sscanf("17 -4 word", "%d %d %15s", &x, &y, w));
	printf(" %d %d %s\n", x, y, w);
	return 3;
}
