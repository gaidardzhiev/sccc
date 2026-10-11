/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
int main(int argc, char **argv) {
	printf("hello, world\n");
	printf("%d %i %u %x %X %o %c %s %%\n", -42, 7, 3000000000u, 255, 255, 8, 'z', "str");
	printf("[%5d] [%-5d] [%05d] [%+d] [%.3d] [%5s] [%-5s] [%.2s]\n", 42, 42, 42, 42, 7, "ab", "ab", "abcdef");
	printf("%ld %lu %lx %zu %p\n", -1234567890123L, 18446744073709551615UL, 0xdeadbeefcafeUL, sizeof(long), (void *)0x1000);
	printf("%#x %#o %hhd %hd\n", 255, 8, 300, 70000);
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "%s-%d", "abc", 123);
	printf("%s %d\n", buf, n);
	n = snprintf(buf, 5, "%s", "truncated");
	printf("%s %d\n", buf, n);
	sprintf(buf, "%08.3x|", 0x1f);
	puts(buf);
	char *s = malloc(100);
	strcpy(s, "Hello");
	strcat(s, " World");
	printf("%s %zu %d %d\n", s, strlen(s), strcmp(s, "Hello World"), strncmp(s, "Help", 3));
	printf("%s %s\n", strchr(s, 'o'), strrchr(s, 'o'));
	printf("%s\n", strstr(s, "Wor"));
	char *d = strdup(s);
	d[0] = tolower(d[0]);
	printf("%s %d %d\n", d, !!isalpha('a'), !!isdigit('a'));
	printf("%ld %ld %d\n", strtol("-123abc", NULL, 10), strtol("0x1F", NULL, 0), atoi("77"));
	int *arr = calloc(10, sizeof(int));
	for (int i = 0; i < 10; i++) arr[i] = i * i;
	arr = realloc(arr, 1000 * sizeof(int));
	printf("%d %d\n", arr[9], arr[3]);
	free(arr);
	fputs("to stdout\n", stdout);
	fprintf(stdout, "argc=%d\n", argc);
	putchar('x');
	putchar('\n');
	return 0;
}
