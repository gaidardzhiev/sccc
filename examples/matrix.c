/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <math.h>

#define N 4

typedef struct {
	double m[N][N];
} Mat;

static Mat mul(Mat a, Mat b) {
	Mat r;
	for (int i = 0; i < N; i++)
		for (int j = 0; j < N; j++) {
			double s = 0;
			for (int k = 0; k < N; k++) s += a.m[i][k] * b.m[k][j];
			r.m[i][j] = s;
		}
	return r;
}

static double det(Mat a) {
	double d = 1;
	for (int c = 0; c < N; c++) {
		int p = c;
		for (int r = c + 1; r < N; r++)
			if (fabs(a.m[r][c]) > fabs(a.m[p][c])) p = r;
		if (a.m[p][c] == 0) return 0;
		if (p != c) {
			for (int k = 0; k < N; k++) {
				double t = a.m[c][k];
				a.m[c][k] = a.m[p][k];
				a.m[p][k] = t;
			}
			d = -d;
		}
		d *= a.m[c][c];
		for (int r = c + 1; r < N; r++) {
			double f = a.m[r][c] / a.m[c][c];
			for (int k = c; k < N; k++) a.m[r][k] -= f * a.m[c][k];
		}
	}
	return d;
}

static int inverse(Mat a, Mat *inv) {
	Mat r = { { { 0 } } };
	for (int i = 0; i < N; i++) r.m[i][i] = 1;
	for (int c = 0; c < N; c++) {
		int p = c;
		for (int i = c + 1; i < N; i++)
			if (fabs(a.m[i][c]) > fabs(a.m[p][c])) p = i;
		if (fabs(a.m[p][c]) < 1e-12) return 0;
		for (int k = 0; k < N; k++) {
			double t = a.m[c][k]; a.m[c][k] = a.m[p][k]; a.m[p][k] = t;
			t = r.m[c][k]; r.m[c][k] = r.m[p][k]; r.m[p][k] = t;
		}
		double piv = a.m[c][c];
		for (int k = 0; k < N; k++) {
			a.m[c][k] /= piv;
			r.m[c][k] /= piv;
		}
		for (int i = 0; i < N; i++) {
			if (i == c) continue;
			double f = a.m[i][c];
			for (int k = 0; k < N; k++) {
				a.m[i][k] -= f * a.m[c][k];
				r.m[i][k] -= f * r.m[c][k];
			}
		}
	}
	*inv = r;
	return 1;
}

static void show(const char *name, Mat a) {
	printf("%s:\n", name);
	for (int i = 0; i < N; i++) {
		for (int j = 0; j < N; j++) printf(" %9.4f", fabs(a.m[i][j]) < 5e-10 ? 0.0 : a.m[i][j]);
		printf("\n");
	}
}

int main(void) {
	Mat a = { {
		{ 4, -2, 1, 3 },
		{ 3, 6, -4, 2 },
		{ 2, 1, 8, -5 },
		{ 1, -3, 2, 7 },
	} };
	Mat inv;
	show("A", a);
	printf("det(A) = %.6f\n", det(a));
	show("A*A", mul(a, a));
	if (inverse(a, &inv)) {
		show("inverse(A)", inv);
		show("A*inverse(A)", mul(a, inv));
	}
	return 0;
}
