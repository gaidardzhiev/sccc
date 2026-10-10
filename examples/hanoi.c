#include <stdio.h>

static long moves;

static void hanoi(int n, char from, char to, char via, int print) {
	if (n == 0) return;
	hanoi(n - 1, from, via, to, print);
	moves++;
	if (print) printf("move disk %d from %c to %c\n", n, from, to);
	hanoi(n - 1, via, to, from, print);
}

int main(void) {
	hanoi(4, 'A', 'C', 'B', 1);
	printf("%ld moves\n\n", moves);
	for (int n = 1; n <= 20; n++) {
		moves = 0;
		hanoi(n, 'A', 'C', 'B', 0);
		printf("%2d disks: %7ld moves\n", n, moves);
	}
	return 0;
}
