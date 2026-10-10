#ifndef _MATH_H
#define _MATH_H
#define HUGE_VAL (1e300 * 1e300)
#define INFINITY (1e300 * 1e300)
#define NAN (-(0.0 / 0.0))
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define isnan(x) ((x) != (x))
#define isinf(x) (!isnan(x) && isnan((x) - (x)))
#define isfinite(x) (!isnan((x) - (x)))
double fabs(double x);
float fabsf(float x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double fmod(double x, double y);
double sqrt(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double ldexp(double x, int e);
double frexp(double x, int *e);
double modf(double x, double *ip);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
#endif
