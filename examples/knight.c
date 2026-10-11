/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

#define N 8

static int board[N][N];
static const int dy[] = { 2, 1, -1, -2, -2, -1, 1, 2 };
static const int dx[] = { 1, 2, 2, 1, -1, -2, -2, -1 };

static int valid(int y, int x) {
	return y >= 0 && y < N && x >= 0 && x < N && !board[y][x];
}

static int degree(int y, int x) {
	int n = 0;
	for (int k = 0; k < 8; k++) n += valid(y + dy[k], x + dx[k]);
	return n;
}

int main(void) {
	int y = 0, x = 0;
	board[y][x] = 1;
	for (int step = 2; step <= N * N; step++) {
		int best = -1, best_deg = 9;
		for (int k = 0; k < 8; k++) {
			int ny = y + dy[k], nx = x + dx[k];
			if (!valid(ny, nx)) continue;
			int d = degree(ny, nx);
			if (d < best_deg) {
				best_deg = d;
				best = k;
			}
		}
		if (best < 0) {
			printf("stuck at step %d\n", step);
			return 1;
		}
		y += dy[best];
		x += dx[best];
		board[y][x] = step;
	}
	printf("knight's tour (Warnsdorff's rule):\n");
	for (int r = 0; r < N; r++) {
		for (int c = 0; c < N; c++) printf(" %2d", board[r][c]);
		printf("\n");
	}
	return 0;
}
