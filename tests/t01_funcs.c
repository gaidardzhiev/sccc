int add(int a, int b) { return a + b; }
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
long many(long a, long b, long c, long d, long e, long f, long g, long h, long i) {
	return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 9*i;
}
int fwd(int x);
int twice(int x) { return fwd(x) * 2; }
int fwd(int x) { return x + 1; }
void nothing(void) { }
char tochar(int x) { return x; }
unsigned char touchar(int x) { return x; }
int main(void) {
	if (add(2, 3) != 5) return 1;
	if (fact(5) != 120) return 2;
	if (fib(10) != 55) return 3;
	if (many(1,2,3,4,5,6,7,8,9) != 285) return 4;
	if (twice(4) != 10) return 5;
	nothing();
	if (tochar(300) != 44) return 6;
	if (tochar(200) != -56) return 7;
	if (touchar(-1) != 255) return 8;
	if (add(many(1,1,1,1,1,1,1,1,1), fact(3)) != 51) return 9;
	return 0;
}
