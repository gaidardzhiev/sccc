/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	int verbose = 0, total = 0;
	const char *name = "world";
	printf("argc = %d\n", argc);
	for (int i = 1; i < argc; i++) {
		printf("argv[%d] = \"%s\"\n", i, argv[i]);
		if (strcmp(argv[i], "-v") == 0) verbose++;
		else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) name = argv[++i];
		else total += atoi(argv[i]);
	}
	printf("argv[argc] is %s\n", argv[argc] ? "not NULL" : "NULL");
	printf("hello, %s; verbose level %d; sum of numbers %d\n", name, verbose, total);
	return 0;
}
