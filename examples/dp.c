/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

static int min3(int a, int b, int c) {
	int m = a < b ? a : b;
	return m < c ? m : c;
}

static int edit_distance(const char *a, const char *b) {
	int la = (int)strlen(a), lb = (int)strlen(b);
	static int d[64][64];
	for (int i = 0; i <= la; i++) d[i][0] = i;
	for (int j = 0; j <= lb; j++) d[0][j] = j;
	for (int i = 1; i <= la; i++)
		for (int j = 1; j <= lb; j++)
			d[i][j] = min3(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] != b[j - 1]));
	return d[la][lb];
}

static void lcs(const char *a, const char *b, char *out) {
	int la = (int)strlen(a), lb = (int)strlen(b);
	static int t[64][64];
	for (int i = la; i >= 0; i--)
		for (int j = lb; j >= 0; j--) {
			if (i == la || j == lb) t[i][j] = 0;
			else if (a[i] == b[j]) t[i][j] = 1 + t[i + 1][j + 1];
			else t[i][j] = t[i + 1][j] > t[i][j + 1] ? t[i + 1][j] : t[i][j + 1];
		}
	int i = 0, j = 0, n = 0;
	while (i < la && j < lb) {
		if (a[i] == b[j]) {
			out[n++] = a[i];
			i++;
			j++;
		} else if (t[i + 1][j] >= t[i][j + 1]) i++;
		else j++;
	}
	out[n] = 0;
}

static int knapsack(const int *w, const int *v, int n, int cap, int *take) {
	static int best[16][128];
	for (int i = 0; i <= n; i++)
		for (int c = 0; c <= cap; c++) {
			if (i == 0) best[i][c] = 0;
			else {
				best[i][c] = best[i - 1][c];
				if (w[i - 1] <= c && best[i - 1][c - w[i - 1]] + v[i - 1] > best[i][c]) best[i][c] = best[i - 1][c - w[i - 1]] + v[i - 1];
			}
		}
	for (int i = n, c = cap; i > 0; i--) {
		take[i - 1] = best[i][c] != best[i - 1][c];
		if (take[i - 1]) c -= w[i - 1];
	}
	return best[n][cap];
}

static long coin_ways(const int *coins, int n, int amount) {
	long ways[1024] = { 1 };
	for (int i = 0; i < n; i++)
		for (int a = coins[i]; a <= amount; a++) ways[a] += ways[a - coins[i]];
	return ways[amount];
}

int main(void) {
	const char *pairs[][2] = { { "kitten", "sitting" }, { "flaw", "lawn" }, { "compiler", "complier" }, { "", "abc" }, { "intention", "execution" } };
	char buf[64];
	for (int i = 0; i < 5; i++) {
		lcs(pairs[i][0], pairs[i][1], buf);
		printf("%-10s %-10s edit distance %d, lcs \"%s\"\n", pairs[i][0], pairs[i][1], edit_distance(pairs[i][0], pairs[i][1]), buf);
	}
	int w[] = { 12, 7, 11, 8, 9, 6, 14, 5 }, v[] = { 24, 13, 23, 15, 16, 11, 28, 9 }, take[8];
	int best = knapsack(w, v, 8, 40, take);
	printf("knapsack capacity 40: best value %d, items", best);
	for (int i = 0; i < 8; i++)
		if (take[i]) printf(" %d(w%d,v%d)", i, w[i], v[i]);
	printf("\n");
	int coins[] = { 1, 2, 5, 10, 20, 50, 100, 200 };
	printf("ways to make 200 from uk coins: %ld\n", coin_ways(coins, 8, 200));
	printf("ways to make 100 from 1,5,10,25,50: %ld\n", coin_ways((int[]){ 1, 5, 10, 25, 50 }, 5, 100));
	return 0;
}
