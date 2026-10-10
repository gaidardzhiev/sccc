#include <stdio.h>
#include <ctype.h>

int main(int argc, char **argv) {
	FILE *f = stdin;
	if (argc > 1 && !(f = fopen(argv[1], "rb"))) {
		perror(argv[1]);
		return 1;
	}
	unsigned char row[16];
	unsigned long off = 0;
	size_t n;
	unsigned sum = 0;
	while ((n = fread(row, 1, sizeof row, f)) > 0) {
		printf("%08lx ", off);
		for (size_t i = 0; i < 16; i++) {
			if (i == 8) putchar(' ');
			if (i < n) printf(" %02x", row[i]);
			else printf("   ");
		}
		printf("  |");
		for (size_t i = 0; i < n; i++) {
			putchar(isprint(row[i]) ? row[i] : '.');
			sum = sum * 31 + row[i];
		}
		printf("|\n");
		off += n;
	}
	printf("%08lx\n%lu bytes, checksum %08x\n", off, off, sum);
	if (f != stdin) fclose(f);
	return 0;
}
