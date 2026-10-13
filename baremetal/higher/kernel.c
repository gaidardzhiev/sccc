/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "../kernel.h"

#define KERNEL_VMA 0xffffffff80000000ul
#define initcall(n, fn) static void (*const fn##_call)(void) __attribute__((section(".initcall." #n), used)) = fn

extern char __bss_start[], __bss_end[], _kernel_end[], _phys_start[];
extern void (*const __initcall_start[])(void), (*const __initcall_end[])(void);

static unsigned long zeros[4096];
static int order[4], norder;
static unsigned long bp_count, bp_ip;
unsigned long magic = 0x5CCC;
static const char text[] = "higher half";

static void second(void) { order[norder++] = 2; }
static void first(void) { order[norder++] = 1; }
static void third(void) { order[norder++] = 3; }
initcall(2, second);
initcall(3, third);
initcall(1, first);

__attribute__((interrupt)) void on_bp(struct frame *f) {
	bp_count++;
	bp_ip = f->ip;
}

void kmain(void) {
	unsigned long k = (unsigned long)kmain;
	const char *low = (const char *)((unsigned long)text - KERNEL_VMA);
	int ok = 1;
	serial_init();
	kputs("higher half kernel\n");
	check("linked high", k >= KERNEL_VMA && (unsigned long)&magic >= KERNEL_VMA && (unsigned long)text >= KERNEL_VMA);
	for (int i = 0; text[i]; i++) if (low[i] != text[i]) ok = 0;
	check("loaded low", ok && *(unsigned char *)(k - KERNEL_VMA) == *(unsigned char *)k && (unsigned long)_phys_start < KERNEL_VMA);
	check("data", magic == 0x5CCC);
	ok = 1;
	for (unsigned long i = 0; i < 4096; i++) if (zeros[i]) ok = 0;
	for (char *p = __bss_start; p < __bss_end; p++) if (*p) ok = 0;
	check("bss", ok && (char *)zeros >= __bss_start && (char *)(zeros + 4096) <= __bss_end);
	for (void (*const *f)(void) = __initcall_start; f < __initcall_end; f++) (*f)();
	check("initcalls", norder == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3);
	check("end", _kernel_end >= __bss_end && !((unsigned long)_kernel_end & 0xFFF));
	idt_set(3, on_bp);
	idt_load();
	__asm__ __volatile__("int3");
	check("#BP", bp_count == 1 && bp_ip >= KERNEL_VMA);
	finish();
}
