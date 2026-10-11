/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 2000

static unsigned seed = 12345;

static int rnd(void) {
	seed = seed * 1103515245u + 12345u;
	return (int)((seed >> 8) % 100000);
}

static void insertion(int *a, int n) {
	for (int i = 1; i < n; i++) {
		int v = a[i], j = i - 1;
		while (j >= 0 && a[j] > v) {
			a[j + 1] = a[j];
			j--;
		}
		a[j + 1] = v;
	}
}

static void quick(int *a, int lo, int hi) {
	while (lo < hi) {
		int p = a[(lo + hi) / 2], i = lo, j = hi;
		while (i <= j) {
			while (a[i] < p) i++;
			while (a[j] > p) j--;
			if (i <= j) {
				int t = a[i];
				a[i++] = a[j];
				a[j--] = t;
			}
		}
		if (j - lo < hi - i) {
			quick(a, lo, j);
			lo = i;
		} else {
			quick(a, i, hi);
			hi = j;
		}
	}
}

static int tmp[N];

static void merge(int *a, int n) {
	if (n < 2) return;
	int m = n / 2, i = 0, j = m, k = 0;
	merge(a, m);
	merge(a + m, n - m);
	while (i < m && j < n) tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
	while (i < m) tmp[k++] = a[i++];
	while (j < n) tmp[k++] = a[j++];
	memcpy(a, tmp, n * sizeof *a);
}

static int cmp(const void *x, const void *y) {
	int a = *(const int *)x, b = *(const int *)y;
	return (a > b) - (a < b);
}

static int sorted(const int *a, int n) {
	for (int i = 1; i < n; i++)
		if (a[i - 1] > a[i]) return 0;
	return 1;
}

int main(void) {
	static int orig[N], a[N], ref[N];
	for (int i = 0; i < N; i++) orig[i] = rnd();
	memcpy(ref, orig, sizeof ref);
	qsort(ref, N, sizeof *ref, cmp);
	const char *names[] = { "insertion", "quick", "merge", "qsort" };
	for (int s = 0; s < 4; s++) {
		memcpy(a, orig, sizeof a);
		switch (s) {
		case 0: insertion(a, N); break;
		case 1: quick(a, 0, N - 1); break;
		case 2: merge(a, N); break;
		default: qsort(a, N, sizeof *a, cmp); break;
		}
		printf("%-10s sorted=%d same=%d\n", names[s], sorted(a, N), memcmp(a, ref, sizeof a) == 0);
	}
	printf("min %d  median %d  max %d\n", ref[0], ref[N / 2], ref[N - 1]);
	for (int i = 0; i < 10; i++) printf("%d ", ref[i * N / 10]);
	printf("\n");
	return 0;
}
