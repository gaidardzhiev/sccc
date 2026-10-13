/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#define ENTRY(name) .globl name; .type name, @function; name:
#define STUB(n) ENTRY(isr##n) push $0; push $n; jmp isr_common
#define STUB_ERR(n) ENTRY(isr##n) push $n; jmp isr_common

	.text
STUB(0)
STUB(3)
STUB(6)
STUB_ERR(13)
STUB_ERR(14)
STUB(32)
STUB(128)

isr_common:
	push %rax
	push %rcx
	push %rdx
	push %rbx
	push %rbp
	push %rsi
	push %rdi
	push %r8
	push %r9
	push %r10
	push %r11
	push %r12
	push %r13
	push %r14
	push %r15
	cld
	mov %rsp, %rdi
	call isr_dispatch
	mov %rax, %rsp
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rdi
	pop %rsi
	pop %rbp
	pop %rbx
	pop %rdx
	pop %rcx
	pop %rax
	add $16, %rsp
	iretq

ENTRY(switch_to)
	push %rbx
	push %rbp
	push %r12
	push %r13
	push %r14
	push %r15
	mov %rsp, (%rdi)
	mov %rsi, %rsp
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbp
	pop %rbx
	ret

ENTRY(co_start)
	mov %r13, %rdi
	call *%r12
	ud2

ENTRY(load_idt)
	lidt (%rdi)
	ret

ENTRY(read_flags)
	pushfq
	pop %rax
	ret

ENTRY(breakpoint)
	int3
	ret

ENTRY(yield)
	int $0x80
	ret

ENTRY(trigger_de)
	xor %ecx, %ecx
	mov $1, %eax
	cltd
	div %ecx
	ret

ENTRY(trigger_ud)
	ud2
	ret

ENTRY(trigger_gp)
	mov $0x1230, %ax
	mov %ax, %ds
	ret

ENTRY(checksum)
	push %rsi
	mov %rsi, %rcx
	mov %rdi, %rsi
	xor %edx, %edx
	jrcxz 2f
1:	lodsb
	movzbl %al, %eax
	rol $5, %edx
	add %eax, %edx
	loop 1b
2:	mov %edx, %eax
	pop %rsi
	ret

ENTRY(run_steps)
	push %rbx
	push %r12
	sub $8, %rsp
	lea steps(%rip), %rbx
	xor %r12d, %r12d
1:	mov (%rbx), %rax
	test %rax, %rax
	jz 2f
	call *%rax
	add %eax, %r12d
	add $8, %rbx
	jmp 1b
2:	mov %r12d, %eax
	add $8, %rsp
	pop %r12
	pop %rbx
	ret

	.data
	.globl vectors, stubs, nvectors
vectors:
	.byte 0, 3, 6, 13, 14, 32, 128
nvectors:
	.long nvectors - vectors
	.balign 8
stubs:
	.quad isr0, isr3, isr6, isr13, isr14, isr32, isr128
steps:
	.quad step_one, step_two, step_three, 0
