/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

static const char keymap[0x3B] = "\0\033" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static volatile unsigned char queue[64];
static volatile unsigned qhead, qtail;

__attribute__((interrupt)) void on_kbd(struct frame *f) {
	(void)f;
	queue[qtail & 63] = __inb(0x60);
	qtail++;
	__outb(0x20, 0x20);
}

#ifdef INJECT
static const unsigned char keys[] = {
	0x2A, 0x23, 0xA3, 0xAA, 0x12, 0x92, 0x26, 0xA6, 0x26, 0xA6, 0x18, 0x98, 0x39, 0xB9,
	0x1F, 0x9F, 0x2E, 0xAE, 0x2E, 0xAE, 0x2E, 0xAE, 0x1C, 0x9C
};

static void inject(void) {
	for (unsigned i = 0; i < sizeof(keys); i++) {
		unsigned before = qtail;
		while (__inb(0x64) & 2) __pause();
		__outb(0x64, 0xD2);
		while (__inb(0x64) & 2) __pause();
		__outb(0x60, keys[i]);
		while (qtail == before) __pause();
	}
}
#endif

void kmain(void) {
	char line[80];
	int n = 0, shift = 0;
	serial_init();
	idt_set(33, on_kbd);
	idt_load();
	while (__inb(0x64) & 1) __inb(0x60);
	pic_init(0xFD, 0xFF);
	__sti();
	kputs("keyboard ready\n");
#ifdef INJECT
	inject();
#endif
	for (;;) {
		unsigned char sc;
		int c;
		while (qhead == qtail) __pause();
		sc = queue[qhead & 63];
		qhead++;
		if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
		if (sc == 0xAA || sc == 0xB6) { shift = 0; continue; }
		if (sc >= sizeof(keymap) || !keymap[sc]) continue;
		c = keymap[sc];
		if (shift && c >= 'a' && c <= 'z') c -= 'a' - 'A';
		kputc(c);
		if (c == '\n') break;
		if (n < 79) line[n++] = (char)c;
	}
	__cli();
	line[n] = 0;
	kputs("got: ");
	kputs(line);
	kputc('\n');
	check("keyboard", n == 10 && line[0] == 'H' && line[6] == 's');
	finish();
}
