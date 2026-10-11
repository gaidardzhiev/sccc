/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

static int cols[16];
static int solutions;
static int first[16];

static int safe(int row, int col) {
	for (int r = 0; r < row; r++) {
		int c = cols[r];
		if (c == col || c - col == r - row || c - col == row - r) return 0;
	}
	return 1;
}

static void place(int row, int n) {
	if (row == n) {
		if (solutions++ == 0)
			for (int i = 0; i < n; i++) first[i] = cols[i];
		return;
	}
	for (int c = 0; c < n; c++) {
		if (!safe(row, c)) continue;
		cols[row] = c;
		place(row + 1, n);
	}
}

int main(void) {
	for (int n = 1; n <= 10; n++) {
		solutions = 0;
		place(0, n);
		printf("%2d queens: %4d solutions\n", n, solutions);
	}
	solutions = 0;
	place(0, 8);
	printf("\nfirst solution for 8:\n");
	for (int r = 0; r < 8; r++) {
		for (int c = 0; c < 8; c++) printf(" %c", first[r] == c ? 'Q' : '.');
		printf("\n");
	}
	return 0;
}
