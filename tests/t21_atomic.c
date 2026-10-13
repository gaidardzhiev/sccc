/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#define barrier() asm volatile("" ::: "memory")
typedef struct { int counter; } atomic_t;
static inline void atomic_add(int i, atomic_t *v) { asm volatile("lock; addl %1,%0" : "+m"(v->counter) : "ir"(i)); }
static inline int cmpxchg(int *p, int old, int new) {
	int prev;
	asm volatile("lock; cmpxchgl %2,%1" : "=a"(prev), "+m"(*p) : "r"(new), "0"(old) : "memory");
	return prev;
}
static inline int test_and_set_bit(long nr, volatile unsigned long *addr) {
	unsigned char c;
	asm volatile("lock; btsq %2,%1\n\tsetc %0" : "=qm"(c), "+m"(*addr) : "Ir"(nr) : "memory");
	return c;
}
static inline unsigned long save_fl(void) { unsigned long f; asm volatile("pushf ; pop %0" : "=rm"(f) : : "memory"); return f; }
static inline long xchg(long *p, long v) { asm volatile("xchgq %0, %1" : "+r"(v), "+m"(*p) : : "memory"); return v; }
static inline unsigned long ffs_(unsigned long x) { unsigned long r; asm("bsf %1,%0" : "=r"(r) : "rm"(x)); return r; }
static inline void rep_movs(void *d, const void *s, unsigned long n) {
	asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
int main(void) {
	atomic_t a = { 1 };
	int x = 5;
	unsigned long bits = 0;
	long l = 3;
	char buf[8] = "abcdefg", out[8] = { 0 };
	atomic_add(41, &a);
	atomic_add(a.counter, &a);
	int p1 = cmpxchg(&x, 5, 9), p2 = cmpxchg(&x, 5, 11);
	int b1 = test_and_set_bit(3, &bits), b2 = test_and_set_bit(3, &bits), b3 = test_and_set_bit(40, &bits);
	barrier();
	long old = xchg(&l, 77);
	rep_movs(out, buf, 7);
	printf("%d %d %d %d %d %d %d %lx %ld %ld %lu %s %d\n", a.counter, p1, p2, x, b1, b2, b3, bits, old, l, ffs_(0x50), out, (int)((save_fl() >> 1) & 1));
	return 0;
}
