#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int sum(int count, ...) {
	va_list ap;
	int total = 0;
	va_start(ap, count);
	for (int i = 0; i < count; i++) total += va_arg(ap, int);
	va_end(ap);
	return total;
}

static double average(int count, ...) {
	va_list ap;
	double total = 0;
	va_start(ap, count);
	for (int i = 0; i < count; i++) total += va_arg(ap, double);
	va_end(ap);
	return count ? total / count : 0;
}

static char *concat(char *dst, ...) {
	va_list ap;
	const char *s;
	dst[0] = 0;
	va_start(ap, dst);
	while ((s = va_arg(ap, const char *)) != NULL) strcat(dst, s);
	va_end(ap);
	return dst;
}

static void mini_printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	for (const char *p = fmt; *p; p++) {
		if (*p != '%') {
			putchar(*p);
			continue;
		}
		switch (*++p) {
		case 'd': printf("%d", va_arg(ap, int)); break;
		case 's': fputs(va_arg(ap, const char *), stdout); break;
		case 'c': putchar(va_arg(ap, int)); break;
		case 'f': printf("%.2f", va_arg(ap, double)); break;
		default: putchar(*p); break;
		}
	}
	va_end(ap);
}

static void log_line(const char *level, const char *fmt, ...) {
	va_list ap;
	char buf[128];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	printf("[%s] %s\n", level, buf);
}

int main(void) {
	char buf[100];
	printf("sum() = %d\n", sum(0));
	printf("sum(1..10) = %d\n", sum(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	printf("average = %.3f\n", average(4, 1.5, 2.5, 3.0, 10.0));
	printf("concat = %s\n", concat(buf, "self", "-", "hosting", " ", "compiler", (char *)NULL));
	mini_printf("mini: %d %s %c %f %%\n", -42, "str", 'x', 3.14159);
	log_line("info", "%d files, %s", 3, "no errors");
	log_line("warn", "%5.1f%% done", 87.25);
	return 0;
}
