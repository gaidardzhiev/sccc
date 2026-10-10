#include <stdio.h>

struct flags {
	unsigned a : 1;
	unsigned b : 3;
	int s : 5;
	unsigned : 0;
	unsigned c : 7;
	unsigned char small : 2;
	long big : 40;
	unsigned long top : 24;
};

struct packed { unsigned char x : 4, y : 4; unsigned short z : 9; };
union ub { unsigned v; struct { unsigned lo : 16, hi : 16; } p; };
struct mixed { char c; int f : 3; char d; };

struct flags gf = { 1, 5, -3, 100, 3, -12345678901L, 0xABCDEF };

int main(void) {
	struct flags f = { 0 };
	printf("%zu %zu %zu %zu\n", sizeof(struct flags), sizeof(struct packed), sizeof(union ub), sizeof(struct mixed));
	f.a = 1; f.b = 6; f.s = -7; f.c = 127; f.small = 5; f.big = -1099511627775L; f.top = 0xFFFFFFF;
	printf("%u %u %d %u %u %ld %lx\n", f.a, f.b, f.s, f.c, f.small, f.big, (unsigned long)f.top);
	f.b++;
	f.b += 3;
	f.s -= 20;
	int old = f.c++;
	printf("%u %d %d %u\n", f.b, f.s, old, f.c);
	printf("%u %u %d %u %u %ld %lx\n", gf.a, gf.b, gf.s, gf.c, gf.small, gf.big, (unsigned long)gf.top);
	struct packed p;
	p.x = 9; p.y = 15; p.z = 300;
	unsigned char *raw = (unsigned char *)&p;
	printf("%u %u %u %02x\n", p.x, p.y, p.z, raw[0]);
	union ub u;
	u.v = 0x12345678;
	printf("%x %x\n", u.p.lo, u.p.hi);
	u.p.hi = 0xBEEF;
	printf("%x\n", u.v);
	struct mixed m = { 'a', -2, 'b' };
	printf("%c %d %c\n", m.c, m.f, m.d);
	int x = (f.a = 3);
	printf("%d %d\n", x, f.s = 100);
	struct flags g = f;
	printf("%u %d\n", g.b, g.s);
	return 0;
}
