/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifdef ASM_BUILTINS
static inline void k_outb(unsigned short port, unsigned char v) { __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void k_outw(unsigned short port, unsigned short v) { __asm__ __volatile__("outw %0, %1" : : "a"(v), "Nd"(port)); }
static inline void k_outl(unsigned short port, unsigned v) { __asm__ __volatile__("outl %0, %1" : : "a"(v), "Nd"(port)); }
static inline unsigned char k_inb(unsigned short port) { unsigned char v; __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline unsigned short k_inw(unsigned short port) { unsigned short v; __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline unsigned k_inl(unsigned short port) { unsigned v; __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline unsigned long k_read_cr(int n) {
	unsigned long v = 0;
	switch (n) {
	case 0: __asm__ __volatile__("mov %%cr0, %0" : "=r"(v)); break;
	case 2: __asm__ __volatile__("mov %%cr2, %0" : "=r"(v)); break;
	case 3: __asm__ __volatile__("mov %%cr3, %0" : "=r"(v)); break;
	case 4: __asm__ __volatile__("mov %%cr4, %0" : "=r"(v)); break;
	}
	return v;
}
static inline void k_write_cr(int n, unsigned long v) {
	switch (n) {
	case 0: __asm__ __volatile__("mov %0, %%cr0" : : "r"(v) : "memory"); break;
	case 2: __asm__ __volatile__("mov %0, %%cr2" : : "r"(v) : "memory"); break;
	case 3: __asm__ __volatile__("mov %0, %%cr3" : : "r"(v) : "memory"); break;
	case 4: __asm__ __volatile__("mov %0, %%cr4" : : "r"(v) : "memory"); break;
	}
}
static inline unsigned long k_rdmsr(unsigned msr) {
	unsigned lo, hi;
	__asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return (unsigned long)hi << 32 | lo;
}
static inline void k_wrmsr(unsigned msr, unsigned long v) {
	__asm__ __volatile__("wrmsr" : : "c"(msr), "a"((unsigned)v), "d"((unsigned)(v >> 32)));
}
static inline void k_cpuid(unsigned leaf, unsigned sub, unsigned *r) {
	__asm__ __volatile__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}
static inline unsigned long k_rdtsc(void) {
	unsigned lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return (unsigned long)hi << 32 | lo;
}
#define __outb(p, v) k_outb((p), (v))
#define __outw(p, v) k_outw((p), (v))
#define __outl(p, v) k_outl((p), (v))
#define __inb(p) k_inb(p)
#define __inw(p) k_inw(p)
#define __inl(p) k_inl(p)
#define __cli() __asm__ __volatile__("cli" : : : "memory")
#define __sti() __asm__ __volatile__("sti" : : : "memory")
#define __hlt() __asm__ __volatile__("hlt")
#define __pause() __asm__ __volatile__("pause")
#define __lgdt(p) __asm__ __volatile__("lgdt (%0)" : : "r"(p) : "memory")
#define __lidt(p) __asm__ __volatile__("lidt (%0)" : : "r"(p) : "memory")
#define __ltr(sel) __asm__ __volatile__("ltr %w0" : : "r"((unsigned)(sel)))
#define __read_cr0() k_read_cr(0)
#define __read_cr2() k_read_cr(2)
#define __read_cr3() k_read_cr(3)
#define __read_cr4() k_read_cr(4)
#define __write_cr0(v) k_write_cr(0, (v))
#define __write_cr2(v) k_write_cr(2, (v))
#define __write_cr3(v) k_write_cr(3, (v))
#define __write_cr4(v) k_write_cr(4, (v))
#define __rdmsr(m) k_rdmsr(m)
#define __wrmsr(m, v) k_wrmsr((m), (v))
#define __invlpg(p) __asm__ __volatile__("invlpg (%0)" : : "r"(p) : "memory")
#define __cpuid(l, s, r) k_cpuid((l), (s), (r))
#define __rdtsc() k_rdtsc()
#endif

struct frame { unsigned long ip, cs, flags, sp, ss; };
struct e820 { unsigned long base, len; unsigned type, ext; };

static unsigned long idt[512];

static void serial_init(void) {
	__outb(0x3F9, 0);
	__outb(0x3FB, 0x80);
	__outb(0x3F8, 1);
	__outb(0x3F9, 0);
	__outb(0x3FB, 3);
	__outb(0x3FA, 0xC7);
}

static void kputc(int c) {
	while (!(__inb(0x3FD) & 0x20)) __pause();
	__outb(0x3F8, c);
}

static void kputs(const char *s) {
	while (*s) kputc(*s++);
}

static void kputn(unsigned long v) {
	char b[24];
	int i = 0;
	do b[i++] = (char)('0' + v % 10); while (v /= 10);
	while (i) kputc(b[--i]);
}

static void kputx(unsigned long v) {
	kputs("0x");
	for (int i = 60; i >= 0; i -= 4) kputc("0123456789abcdef"[(v >> i) & 15]);
}

static void check(const char *what, int ok) {
	kputs(what);
	kputs(ok ? " ok\n" : " FAIL\n");
}

static void idt_set(int n, void (*h)()) {
	unsigned long a = (unsigned long)h;
	idt[2 * n] = (a & 0xFFFF) | (0x08ul << 16) | (0x8E00ul << 32) | (((a >> 16) & 0xFFFF) << 48);
	idt[2 * n + 1] = a >> 32;
}

static void idt_load(void) {
	unsigned short d[5];
	unsigned long base = (unsigned long)idt;
	d[0] = sizeof(idt) - 1;
	d[1] = base & 0xFFFF;
	d[2] = (base >> 16) & 0xFFFF;
	d[3] = (base >> 32) & 0xFFFF;
	d[4] = base >> 48;
	__lidt(d);
}

static void pic_init(int mask1, int mask2) {
	__outb(0x20, 0x11);
	__outb(0xA0, 0x11);
	__outb(0x21, 0x20);
	__outb(0xA1, 0x28);
	__outb(0x21, 4);
	__outb(0xA1, 2);
	__outb(0x21, 1);
	__outb(0xA1, 1);
	__outb(0x21, mask1);
	__outb(0xA1, mask2);
}

static void finish(void) {
	while (!(__inb(0x3FD) & 0x40)) __pause();
	__outb(0xF4, 0x10);
	for (const char *s = "Shutdown"; *s; s++) __outb(0x8900, *s);
	for (;;) {
		__cli();
		__hlt();
	}
}
