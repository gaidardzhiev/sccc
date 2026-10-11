/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

static unsigned long bp_count, bp_ip, de_count, de_ip, pf_count, pf_err, pf_addr;
static const unsigned char brk[] = { 0xCC, 0xC3 };

__attribute__((interrupt)) void on_de(struct frame *f) {
	unsigned char *p = (unsigned char *)f->ip;
	de_count++;
	de_ip = f->ip;
	if (p[0] == 0x48 && p[1] == 0xF7) f->ip += 3;
}

__attribute__((interrupt)) void on_bp(struct frame *f) {
	bp_count++;
	bp_ip = f->ip;
}

__attribute__((interrupt)) void on_pf(struct frame *f, unsigned long err) {
	unsigned long *pml4 = (unsigned long *)(__read_cr3() & ~0xFFFul);
	unsigned long *pdpt = (unsigned long *)(pml4[0] & ~0xFFFul);
	(void)f;
	pf_count++;
	pf_err = err;
	pf_addr = __read_cr2();
	pdpt[1] = pdpt[0];
	__invlpg((void *)pf_addr);
}

void kmain(void) {
	long keep = 0x123456789abcdefl;
	volatile long z = 0;
	volatile unsigned long *hi = (volatile unsigned long *)0x40300000ul;
	long q;
	serial_init();
	kputs("exceptions\n");
	idt_set(0, on_de);
	idt_set(3, on_bp);
	idt_set(14, on_pf);
	idt_load();
	((void (*)(void))brk)();
	check("#BP", bp_count == 1 && bp_ip == (unsigned long)brk + 1);
	q = 100 / z;
	(void)q;
	check("#DE", de_count == 1 && ((unsigned char *)de_ip)[0] == 0x48);
	((void (*)(void))brk)();
	check("#BP again", bp_count == 2);
	*hi = 0xfeedface;
	check("#PF", pf_count == 1 && pf_err == 2 && pf_addr == 0x40300000ul && *(unsigned long *)0x300000ul == 0xfeedface);
	kputs("pf err ");
	kputx(pf_err);
	kputs(" cr2 ");
	kputx(pf_addr);
	kputc('\n');
	check("locals", keep == 0x123456789abcdefl);
	finish();
}
