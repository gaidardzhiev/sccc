#include <stdio.h>

static long long fib_rec(int n) {
	return n < 2 ? n : fib_rec(n - 1) + fib_rec(n - 2);
}

static unsigned long long fib_iter(int n) {
	unsigned long long a = 0, b = 1;
	while (n-- > 0) {
		unsigned long long t = a + b;
		a = b;
		b = t;
	}
	return a;
}

int main(void) {
	for (int i = 0; i <= 25; i++) printf("fib(%2d) = %lld\n", i, fib_rec(i));
	printf("fib(90) = %llu\n", fib_iter(90));
	printf("fib(93) = %llu\n", fib_iter(93));
	return 0;
}
