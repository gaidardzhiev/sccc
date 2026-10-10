#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

static void show(const char *s) {
	char *end;
	double v = strtod(s, &end);
	unsigned long long b;
	memcpy(&b, &v, 8);
	printf("%-28s %016llx %.17g rest \"%s\"\n", s, b, v, end);
}

int main(void) {
	const char *in[] = { "0x1p0", "0x1.8p3", "-0x.8p-1", "0X1P-1074", "0x1p-1075", "0x1.fffffffffffff8p1023", "0x1.fffffffffffff7ffp1023",
"0x123456789abcdef0123p0", "0x", "0xg", "0x1p", "  +0xAbC.dEfp-7z", "inf", "-Infinity", "infinit", "nan", "NaN(abc_1)", "-nan", "1e-400", "2.5e-324", NULL };
	for (int i = 0; in[i]; i++) show(in[i]);
	volatile double z = 0;
	printf("%f %f %+f %e %G %5.1f|%-6f|\n", NAN, -NAN, NAN, z / z, -(z / z), INFINITY, -INFINITY);
	printf("%.9g %.17g %d %d %.9g\n", (double)FLT_MAX, DBL_MAX, DBL_DIG, FLT_MANT_DIG, (double)FLT_EPSILON);
	printf("%g %g %g %g %.17g\n", 0x1p-2, 0X.8P1, 0xAp0, (double)0x1.8p1f, 0x1.fffffffffffffp1023);
	printf("%d\n", setvbuf(stdout, NULL, _IONBF, 0));
	printf("unbuffered ");
	fprintf(stderr, "stderr ");
	printf("line\n");
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
	printf("line buffered\n");
	setbuf(stdout, NULL);
	printf("%d\n", setvbuf(stdout, NULL, 7, 0) != 0);
	return 0;
}
