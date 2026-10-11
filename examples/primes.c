/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	int n = argc > 1 ? atoi(argv[1]) : 1000;
	char *composite = malloc(n + 1);
	int count = 0, col = 0;
	memset(composite, 0, n + 1);
	for (int i = 2; (long)i * i <= n; i++)
		if (!composite[i])
			for (int j = i * i; j <= n; j += i) composite[j] = 1;
	for (int i = 2; i <= n; i++) {
		if (composite[i]) continue;
		count++;
		printf("%6d", i);
		if (++col == 10) {
			printf("\n");
			col = 0;
		}
	}
	if (col) printf("\n");
	printf("%d primes up to %d\n", count, n);
	free(composite);
	return 0;
}
