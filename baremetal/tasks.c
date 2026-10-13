/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

struct regs {
	unsigned long r15, r14, r13, r12, r11, r10, r9, r8;
	unsigned long rdi, rsi, rbp, rbx, rdx, rcx, rax;
	unsigned long vec, err, rip, cs, rflags, rsp, ss;
};

extern unsigned char vectors[];
extern int nvectors;
extern unsigned long stubs[];
void switch_to(unsigned long *save, unsigned long sp);
void co_start(void);
void load_idt(void *p);
unsigned long read_flags(void);
void breakpoint(void);
void yield(void);
void trigger_de(void);
void trigger_ud(void);
void trigger_gp(void);
unsigned long checksum(const unsigned char *p, unsigned long n);
int run_steps(void);

#define NTASK 4
#define STACK 4096

static volatile unsigned long ticks;
static unsigned long counts[256], last_err[256];
static unsigned long saved[NTASK];
static int cur, done[NTASK];
static unsigned long stacks[NTASK][STACK / 8] __attribute__((aligned(16)));
static unsigned long co_stack[STACK / 8] __attribute__((aligned(16)));
static unsigned long main_sp, co_sp;

int step_one(void) { return 1; }
int step_two(void) { return 20; }
int step_three(void) { return 300; }

unsigned long isr_dispatch(struct regs *r) {
	counts[r->vec]++;
	last_err[r->vec] = r->err;
	switch (r->vec) {
	case 0: case 6: case 13:
		r->rip += 2;
		break;
	case 32:
		ticks++;
		__outb(0x20, 0x20);
		break;
	case 128:
		saved[cur] = (unsigned long)r;
		do cur = (cur + 1) % NTASK; while (done[cur] && cur);
		return saved[cur];
	}
	return (unsigned long)r;
}

static void task(void) {
	int me = cur;
	for (int i = 0; i < 3; i++) {
		kputc('A' + me - 1);
		kputn(i);
		kputc(' ');
		yield();
	}
	done[me] = 1;
	for (;;) yield();
}

static void spawn(int n, void (*fn)(void)) {
	unsigned long top = (unsigned long)&stacks[n][STACK / 8];
	struct regs *r = (struct regs *)(top - 64 - sizeof(struct regs));
	for (unsigned long *p = (unsigned long *)r; p < (unsigned long *)top; p++) *p = 0;
	r->rip = (unsigned long)fn;
	r->cs = 8;
	r->rflags = 0x202;
	r->rsp = top - 8;
	r->ss = 0x10;
	saved[n] = (unsigned long)r;
}

static void coroutine(unsigned long arg) {
	for (unsigned long i = 0;; i++) {
		kputs("co ");
		kputn(arg + i);
		kputc('\n');
		switch_to(&co_sp, main_sp);
	}
}

void kmain(void) {
	unsigned short d[5];
	unsigned long base = (unsigned long)idt;
	serial_init();
	kputs("tasks\n");
	for (int i = 0; i < nvectors; i++) idt_set(vectors[i], (void (*)())stubs[i]);
	d[0] = sizeof(idt) - 1;
	d[1] = base & 0xFFFF;
	d[2] = (base >> 16) & 0xFFFF;
	d[3] = (base >> 32) & 0xFFFF;
	d[4] = base >> 48;
	load_idt(d);

	breakpoint();
	trigger_de();
	trigger_ud();
	trigger_gp();
	check("exceptions", counts[3] == 1 && counts[0] == 1 && counts[6] == 1 && counts[13] == 1);
	kputs("gp err ");
	kputx(last_err[13]);
	kputc('\n');

	pic_init(0xFE, 0xFF);
	__outb(0x43, 0x34);
	__outb(0x40, 1193 & 0xFF);
	__outb(0x40, 1193 >> 8);
	__sti();
	check("interrupts on", (read_flags() & 0x200) != 0);
	while (ticks < 20) __hlt();
	check("timer", ticks >= 20);

	for (int i = 1; i < NTASK; i++) spawn(i, task);
	while (!(done[1] && done[2] && done[3])) yield();
	kputc('\n');
	check("tasks", counts[128] >= 9 && cur == 0);

	co_stack[STACK / 8 - 1] = 0;
	unsigned long *sp = &co_stack[STACK / 8 - 8];
	sp[0] = 0; sp[1] = 0;
	sp[2] = 1000; sp[3] = (unsigned long)coroutine;
	sp[4] = 0; sp[5] = 0;
	sp[6] = (unsigned long)co_start;
	co_sp = (unsigned long)sp;
	for (int i = 0; i < 3; i++) switch_to(&main_sp, co_sp);
	__cli();

	static const unsigned char text[] = "built-in assembler";
	unsigned long sum = 0;
	for (unsigned long i = 0; i < sizeof(text) - 1; i++) sum = ((sum << 5 | sum >> 27) & 0xFFFFFFFF) + text[i];
	check("checksum", checksum(text, sizeof(text) - 1) == (sum & 0xFFFFFFFF) && checksum(text, 0) == 0);
	check("steps", run_steps() == 321);
	finish();
}
