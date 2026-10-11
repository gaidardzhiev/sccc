/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

#define W 40
#define H 20

static char grid[H][W], next[H][W];

static int neighbours(int y, int x) {
	int n = 0;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++) {
			if (!dy && !dx) continue;
			n += grid[(y + dy + H) % H][(x + dx + W) % W];
		}
	return n;
}

static void show(int gen) {
	int alive = 0;
	printf("generation %d\n", gen);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			putchar(grid[y][x] ? '#' : '.');
			alive += grid[y][x];
		}
		putchar('\n');
	}
	printf("%d alive\n\n", alive);
}

int main(void) {
	static const char *glider_gun[] = {
		"........................#...........",
		"......................#.#...........",
		"............##......##............##",
		"...........#...#....##............##",
		"##........#.....#...##..............",
		"##........#...#.##....#.#...........",
		"..........#.....#.......#...........",
		"...........#...#....................",
		"............##......................",
	};
	for (int y = 0; y < 9; y++)
		for (int x = 0; glider_gun[y][x]; x++) grid[y + 1][x + 1] = glider_gun[y][x] == '#';
	for (int gen = 0; gen <= 60; gen++) {
		if (gen % 30 == 0) show(gen);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int n = neighbours(y, x);
				next[y][x] = n == 3 || (n == 2 && grid[y][x]);
			}
		memcpy(grid, next, sizeof grid);
	}
	return 0;
}
