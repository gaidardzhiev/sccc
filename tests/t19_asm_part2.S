/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#define ENTRY(name) .globl name; .type name, @function; name:
#define SCALE 3
#define NCASE 4

	.text
ENTRY(add3)
	lea (%rdi,%rsi), %rax
	add %rdx, %rax
	ret

ENTRY(copy_bytes)
	mov %rdi, %rax
	mov %rdx, %rcx
	rep movsb
	ret

ENTRY(sum_table)
	xor %eax, %eax
	test %rsi, %rsi
	jz 2f
1:	add (%rdi), %rax
	add $8, %rdi
	dec %rsi
	jnz 1b
2:	ret

ENTRY(classify)
	cmp $NCASE - 1, %rdi
	ja .Ldefault
	lea .Ltable(%rip), %rdx
	movslq (%rdx,%rdi,4), %rax
	add %rdx, %rax
	jmp *%rax
.Lc0:	mov $10, %eax
	ret
.Lc1:	mov $11, %eax
	ret
.Lc2:	mov $12, %eax
	ret
.Lc3:	mov $13, %eax
	ret
.Ldefault:
	mov $-1, %eax
	ret

	.section .rodata
	.balign 4
.Ltable:
	.long .Lc0 - .Ltable, .Lc1 - .Ltable, .Lc2 - .Ltable, .Lc3 - .Ltable
	.text

ENTRY(call_back)
	push %rbx
	mov %edi, %ebx
	call c_square
	imul $SCALE, %eax
	add c_counter(%rip), %eax
	add %ebx, %eax
	pop %rbx
	ret

ENTRY(say)
	sub $8, %rsp
	mov %edi, %edx
	lea greeting(%rip), %rsi
	lea fmt(%rip), %rdi
	xor %eax, %eax
	call printf
	add $8, %rsp
	ret

ENTRY(fill_buf)
	lea scratch(%rip), %rdi
	mov $'x', %al
	mov $16, %ecx
	rep stosb
	movb $0, scratch+16(%rip)
	lea scratch(%rip), %rax
	ret

ENTRY(bit_ops)
	bsr %rdi, %rax
	shl $8, %rax
	bt $0, %rdi
	setc %al
	movzbl %al, %ecx
	or %rcx, %rax
	ret

ENTRY(far_branch)
	xor %eax, %eax
	test %edi, %edi
	jz 3f
	.fill 200, 1, 0x90
	mov $1, %eax
3:	ret

	.weak overridable
ENTRY(overridable)
	mov $1, %eax
	ret

	.weak maybe_hook
ENTRY(use_hook)
	lea maybe_hook(%rip), %rax
	test %rax, %rax
	jz 1f
	jmp maybe_hook
1:	mov $-7, %eax
	ret

	.data
	.globl asm_values, asm_count, asm_ptr
	.balign 8
asm_values:
	.quad 1, 2, 3, 4, 5
asm_count:
	.long (asm_count - asm_values) / 8
	.balign 8
asm_ptr:
	.quad asm_values + 16

	.section .rodata
greeting:
	.asciz "hello from asm"
fmt:
	.asciz "%s %d\n"

	.comm scratch, 32, 16
