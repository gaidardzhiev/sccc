/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double gd = 2.5;
float gf = -1.25f;
double garr[] = { 1.0, 0.5, -3e2, 1e-3 };
struct pt { double x, y; float w; } gp = { 1.5, -2.0, 0.25f };

double avg(double a, double b) { return (a + b) / 2; }
float scale(float v, int k) { return v * k; }
double sumv(int n, ...) {
	return 0;
}
double poly(double x) { return 3 * x * x - 2 * x + 1; }
int cmpd(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}
struct pt mid(struct pt a, struct pt b) {
	struct pt r;
	r.x = avg(a.x, b.x); r.y = avg(a.y, b.y); r.w = a.w + b.w;
	return r;
}
double many(double a, double b, double c, double d, double e, double f, double g, double h) {
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}

int main(void) {
	double d = 1.5;
	float f = 2.25f;
	printf("%f %f %f\n", d, f, d * f);
	printf("%.2f %.0f %.3f %10.4f|%-10.2f|\n", 3.14159, 2.5, -0.0005, 1.0 / 3, 2.0 / 3);
	printf("%e %E %.2e\n", 12345.678, 0.000123, -9.999);
	printf("%g %g %g %g %g %g\n", 100000.0, 1000000.0, 0.0001, 0.00001, 1.5, 123456789.0);
	printf("%d %d %d %ld\n", (int)3.99, (int)-3.99, (int)f, (long)1e15);
	printf("%f %f\n", (double)7, (double)-2147483648);
	unsigned long big = 18446744073709551615UL;
	printf("%.0f %.1f\n", (double)big, (double)(unsigned)4000000000u);
	printf("%d %d %d %d %d %d\n", d < f, d > f, d == 1.5, d != 1.5, d <= 1.5, f >= 3);
	double nan = 0.0 / 0.0;
	printf("%d %d %d %d\n", nan == nan, nan != nan, nan < 1, !nan);
	printf("%f %f %f\n", gd, gf, garr[2]);
	printf("%g %g %g\n", gp.x, gp.y, gp.w);
	printf("%f %f\n", avg(1, 2), scale(1.5f, 3));
	printf("%f\n", poly(2.0));
	d += 1; d *= 2; d -= 0.5; d /= 4;
	printf("%f\n", d);
	int i = 10;
	i += 2.7;
	i *= 1.5;
	printf("%d\n", i);
	d = 0;
	for (int k = 0; k < 10; k++) d += 0.1;
	printf("%.15f %d\n", d, d == 1.0);
	double xs[] = { 3.5, -1.0, 2.25, 0.0, 10.0 };
	qsort(xs, 5, sizeof(double), cmpd);
	printf("%g %g %g %g %g\n", xs[0], xs[1], xs[2], xs[3], xs[4]);
	struct pt a = { 0, 0, 1 }, b = { 2, 4, 2 };
	struct pt m = mid(a, b);
	printf("%g %g %g\n", m.x, m.y, m.w);
	printf("%g\n", many(1, 2, 3, 4, 5, 6, 7, 8));
	printf("%g %g %g %g\n", sqrt(2.0), fabs(-3.5), floor(-2.5), ceil(2.1));
	printf("%g %g %.6f\n", pow(2, 10), pow(2.0, -2), fmod(7.5, 2.0));
	printf("%.6f %.6f %.6f\n", exp(1.0), log(10.0), sin(1.0));
	printf("%g %g\n", strtod("1.25e3", NULL), atof("-0.5"));
	double inc = 1.5;
	inc++;
	++inc;
	inc--;
	printf("%g %d\n", inc, d ? 1 : 0);
	float ff = 0.1f;
	double dd = ff;
	printf("%.10f %d\n", dd, ff == 0.1f);
	printf("%g %g\n", 1 ? 2 : 0.5, 0 ? 2 : 0.5);
	printf("%5.1f|%-6.1f|%06.2f|%+.1f\n", 2.25, 2.25, -2.5, 3.0);
	long double ld = 2.0;
	printf("%d\n", (int)(ld * 4));
	if (!(0.5 > 0) || !d) return 1;
	return 0;
}
