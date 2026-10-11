/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

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
