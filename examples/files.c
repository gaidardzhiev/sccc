#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
	const char *path = "files_demo.txt";
	FILE *f = fopen(path, "w");
	if (!f) {
		perror(path);
		return 1;
	}
	for (int i = 1; i <= 5; i++) fprintf(f, "line %d: %d squared is %d\n", i, i, i * i);
	fputs("last line without number\n", f);
	fclose(f);

	f = fopen(path, "r");
	char line[128];
	int n = 0;
	while (fgets(line, sizeof line, f)) printf("%d| %s", ++n, line);
	printf("read %d lines\n", n);

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	printf("file size %ld bytes\n", size);
	fseek(f, 8, SEEK_SET);
	int c = fgetc(f);
	printf("byte at offset 8: '%c'\n", c);
	ungetc('#', f);
	printf("after ungetc: '%c'\n", fgetc(f));
	rewind(f);
	char word[16];
	size_t got = fread(word, 1, 6, f);
	word[got] = 0;
	printf("first %zu bytes: \"%s\"\n", got, word);
	while (fgetc(f) != EOF);
	printf("feof = %d, ferror = %d\n", feof(f) != 0, ferror(f) != 0);
	fclose(f);

	f = fopen(path, "a");
	fprintf(f, "appended\n");
	fclose(f);
	f = fopen(path, "rb");
	char *all = malloc(size + 64);
	size_t total = fread(all, 1, size + 63, f);
	all[total] = 0;
	fclose(f);
	printf("after append %zu bytes, ends with \"%s\"\n", total, strrchr(all, ':') + 2);
	free(all);
	printf("remove: %d\n", remove(path));
	printf("open removed file: %s\n", fopen(path, "r") ? "succeeded" : "failed");
	return 0;
}
