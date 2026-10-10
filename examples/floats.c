#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

static double newton_sqrt(double x) {
	double g = x / 2;
	for (int i = 0; i < 30; i++) g = (g + x / g) / 2;
	return g;
}

static double integrate(double (*f)(double), double a, double b, int n) {
	double h = (b - a) / n, s = f(a) + f(b);
	for (int i = 1; i < n; i++) s += f(a + i * h) * (i % 2 ? 4 : 2);
	return s * h / 3;
}

static double parabola(double x) { return x * x; }

int main(void) {
	double values[] = { 0.0, 1.0, -2.5, 3.14159265358979, 1e-7, 123456789.0, 1.0 / 3, 6.02214076e23 };
	for (int i = 0; i < 8; i++) printf("%-22.17g %%f=%f %%e=%e %%g=%g\n", values[i], values[i], values[i], values[i]);
	printf("%8.3f|%-8.2f|%+.1f|%08.2f|% .2e\n", 3.14159, 2.71828, 1.5, -3.25, 12345.678);
	printf("0.1 + 0.2 = %.17g (== 0.3: %d)\n", 0.1 + 0.2, 0.1 + 0.2 == 0.3);
	printf("float 0.1 = %.10f, double 0.1 = %.20f\n", (double)0.1f, 0.1);
	printf("DBL_EPSILON = %g, 1 + eps > 1: %d, 1 + eps/2 > 1: %d\n", DBL_EPSILON, 1 + DBL_EPSILON > 1, 1 + DBL_EPSILON / 2 > 1);
	double inf = 1e308 * 10, nan = inf - inf;
	printf("inf = %f, -inf = %f, nan == nan: %d, isnan: %d, isinf: %d\n", inf, -inf, nan == nan, isnan(nan), isinf(inf));
	printf("sqrt(2) = %.15f, newton = %.15f\n", sqrt(2.0), newton_sqrt(2.0));
	printf("floor(-2.5) = %g, ceil(-2.5) = %g, round(2.5) = %g, trunc(-2.7) = %g\n", floor(-2.5), ceil(-2.5), round(2.5), trunc(-2.7));
	printf("fmod(10, 3) = %g, fabs(-4.5) = %g, pow(2, 0.5) = %.6f\n", fmod(10, 3), fabs(-4.5), pow(2, 0.5));
	printf("integral of x^2 on [0,3] = %.10f\n", integrate(parabola, 0, 3, 100));
	printf("(int)3.99 = %d, (int)-3.99 = %d, (double)7 / 2 = %g, 7 / 2 = %d\n", (int)3.99, (int)-3.99, (double)7 / 2, 7 / 2);
	printf("strtod(\"  -1.5e3xyz\") = %g, atof(\"0x10\") = %g\n", strtod("  -1.5e3xyz", NULL), atof("0x10"));
	float f = 16777216.0f;
	printf("float 2^24 + 1 = %.1f (precision lost)\n", (double)(f + 1.0f));
	double sum = 0;
	for (int i = 1; i <= 1000000; i++) sum += 1.0 / ((double)i * i);
	printf("sum 1/n^2 = %.12f, pi^2/6 = %.12f\n", sum, 3.14159265358979323846 * 3.14159265358979323846 / 6);
	return 0;
}
