/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

static volatile unsigned long ticks;

__attribute__((interrupt)) void on_timer(struct frame *f) {
	(void)f;
	ticks++;
	__outb(0x20, 0x20);
}

void kmain(void) {
	unsigned long t0, t1;
	serial_init();
	kputs("timer start\n");
	idt_set(32, on_timer);
	idt_load();
	pic_init(0xFE, 0xFF);
	__outb(0x43, 0x34);
	__outb(0x40, 11932 & 0xFF);
	__outb(0x40, 11932 >> 8);
	t0 = __rdtsc();
	__sti();
	for (unsigned long next = 10; next <= 50; next += 10) {
		while (ticks < next) __hlt();
		kputs("tick ");
		kputn(next);
		kputc('\n');
	}
	__cli();
	t1 = __rdtsc();
	check("timer", ticks >= 50 && t1 > t0);
	finish();
}
