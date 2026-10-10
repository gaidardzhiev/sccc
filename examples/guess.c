#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	unsigned seed = argc > 1 ? (unsigned)atoi(argv[1]) : 7;
	char line[64];
	int games = 0;
	for (;;) {
		seed = seed * 1103515245u + 12345u;
		int secret = (int)((seed >> 16) % 100) + 1, tries = 0;
		printf("I am thinking of a number between 1 and 100.\n");
		for (;;) {
			printf("guess: ");
			if (!fgets(line, sizeof line, stdin)) {
				printf("\nthe number was %d. %d games played\n", secret, games);
				return 0;
			}
			char *end;
			long g = strtol(line, &end, 10);
			if (end == line) {
				printf("that is not a number\n");
				continue;
			}
			tries++;
			if (g < secret) printf("too low\n");
			else if (g > secret) printf("too high\n");
			else {
				printf("correct, %d tries\n\n", tries);
				games++;
				break;
			}
		}
	}
}
