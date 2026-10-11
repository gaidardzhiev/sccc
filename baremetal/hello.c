/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

void kmain(void) {
	volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
	const char *msg = "Hello from sccc";
	int ok = 1;
	serial_init();
	kputs("Hello from sccc on COM1\n");
	for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0720;
	for (int i = 0; msg[i]; i++) vga[i] = (unsigned short)(0x1F00 | msg[i]);
	for (int i = 0; msg[i]; i++) if (vga[i] != (0x1F00 | msg[i])) ok = 0;
	check("vga", ok);
	finish();
}
