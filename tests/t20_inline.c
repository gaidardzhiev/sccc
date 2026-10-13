/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

struct pair { int a, b; };

static int counter = 5;
static long table[4] = { 10, 20, 30, 40 };
int renamed __asm__("real_name") = 9;
int real_name_twice(void) __asm__("twice_real");

static int helper(int x) { return x * 3; }

__asm__(".globl from_top\n"
	"from_top:\n"
	"\tmov $77, %eax\n"
	"\tret\n"
	".globl twice_real\n"
	"twice_real:\n"
	"\tmov real_name(%rip), %eax\n"
	"\tadd %eax, %eax\n"
	"\tret\n");
int from_top(void);

__attribute__((naked)) int naked_seven(void) {
	__asm__("mov $7, %eax");
	__asm__("ret");
}

__attribute__((section(".text.other"))) int other_section(int x) {
	int r;
	__asm__("lea 1(%1), %0\n\t.pushsection .text\n\tnop\n\t.popsection" : "=r"(r) : "r"(x));
	return r;
}

static int add(int a, int b) {
	int r;
	__asm__("lea (%1,%2), %0" : "=r"(r) : "r"(a), "r"(b));
	return r;
}

static long plus(long a) {
	__asm__ __volatile__("addq $10, %0" : "+r"(a));
	return a;
}

static int cpu_ok(void) {
	unsigned a, b, c, d;
	__asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
	return a > 0 && (b == 0x756e6547 || b == 0x68747541);
}

static int memory_ops(void) {
	struct pair p = { 1, 2 };
	int arr[3] = { 4, 5, 6 };
	int i = 2;
	__asm__("incl %0" : "+m"(counter));
	__asm__("addl $10, %0" : "+m"(p.b));
	__asm__("shll $1, %0" : "+m"(arr[i]));
	__asm__("addq %1, %0" : "+m"(table[1]) : "ri"(5L));
	__asm__("movl %1, %%eax\n\taddl %%eax, %0" : "+rm"(i) : "m"(arr[0]) : "eax");
	return counter * 1000 + p.b * 100 + arr[2] + (int)table[1] + i;
}

static int immediates(void) {
	int r, s;
	long q;
	__asm__("mov %1, %0" : "=r"(r) : "i"(1234));
	__asm__("mov $%c1 + 1, %0" : "=r"(s) : "n"(99));
	__asm__("lea %c1(%%rip), %0" : "=r"(q) : "i"(&counter));
	return r + s + (*(int *)q == counter);
}

static int sizes(void) {
	long v = 0x1122334455667788;
	unsigned char b, h;
	unsigned short w;
	unsigned k;
	long z = 0;
	__asm__("mov %b1, %0" : "=r"(b) : "r"(v));
	__asm__("mov %h1, %0" : "=q"(h) : "a"(v));
	__asm__("mov %w1, %0" : "=r"(w) : "r"(v));
	__asm__("mov %k1, %0" : "=r"(k) : "r"(v));
	__asm__("mov%z0 %q1, %0" : "=r"(z) : "r"(v));
	return b + h + w + (k == 0x55667788) + (z == v);
}

static int named(int x) {
	int y;
	__asm__("imul %[in], %[in]\n\tmov %[in], %[out]" : [out] "=&r"(y) : [in] "r"(x));
	return y;
}

static int matching(int x) {
	__asm__("shl $2, %0" : "=r"(x) : "0"(x));
	return x;
}

static int call_static(int x) {
	int r;
	__asm__("mov %1, %%edi\n\tcall helper\n\tmov %%eax, %0" : "=r"(r) : "r"(x)
		: "rdi", "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory");
	return r;
}

static int classify(int x) {
	__asm__ goto("cmp $5, %0\n\tje %l1\n\tcmp $7, %0\n\tje %l[seven]" : : "r"(x) : "cc" : five, seven);
	return 0;
five:
	return 5;
seven:
	return 7;
}

static int back_jump(void) {
	int n = 0;
again:
	n++;
	__asm__ goto("cmpl $3, %0\n\tjl %l1" : : "m"(n) : "cc" : again);
	return n;
}

static int uses_rbx(int x) {
	int r;
	__asm__("mov %1, %%ebx\n\tadd %%ebx, %%ebx\n\tmov %%ebx, %0" : "=r"(r) : "r"(x) : "rbx");
	return r;
}

static long syscall3(long n, long a, long b, long c) {
	register long r10 __asm__("r10") = 0;
	long ret;
	__asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
	return ret;
}

static int unique(void) {
	int r = 0;
	__asm__("jmp 1f\n1:\tinc %0\n.Lu%=:\tinc %0" : "+r"(r));
	__asm__("jmp 1f\n1:\tinc %0\n.Lu%=:\tinc %0" : "+r"(r));
	return r;
}

static int many(void) {
	int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6;
	int s;
	__asm__("mov %1, %0\n\tadd %2, %0\n\tadd %3, %0\n\tadd %4, %0\n\tadd %5, %0\n\tadd %6, %0"
		: "=&r"(s) : "r"(a), "r"(b), "g"(c), "rm"(d), "r"(e), "r"(f));
	return s;
}

static int stmt_expr(int x) {
	return ({ int t; __asm__("lea 100(%1), %0" : "=r"(t) : "r"(x)); t; }) + 1;
}

int main(void) {
	printf("top %d naked %d section %d\n", from_top(), naked_seven(), other_section(41));
	printf("add %d plus %ld cpu %d\n", add(3, 4), plus(5), cpu_ok());
	printf("memory %d\n", memory_ops());
	printf("imm %d sizes %d\n", immediates(), sizes());
	printf("named %d matching %d static %d\n", named(6), matching(3), call_static(5));
	printf("goto %d %d %d back %d\n", classify(5), classify(7), classify(1), back_jump());
	printf("rbx %d unique %d many %d stmt %d\n", uses_rbx(21), unique(), many(), stmt_expr(1));
	printf("label %d %d\n", renamed, real_name_twice());
	fflush(stdout);
	long w = syscall3(1, 1, (long)"syscall\n", 8);
	printf("wrote %ld\n", w);
	return 0;
}
