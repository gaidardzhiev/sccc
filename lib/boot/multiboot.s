# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

	.code32
header:
	.long 0x1badb002, 0x00010002, -(0x1badb002 + 0x00010002)
	.long LOAD, LOAD, END, TOP, LOAD + start - header
start:
	cli
	mov $TOP, %esp
	mov %ebx, %ebp
	mov %eax, %esi
	cld
	mov $PT, %edi
	xor %eax, %eax
	mov $0xc00, %ecx
	rep stosl
	movl $PT + 0x1003, PT
	movl $PT + 0x2003, PT + 0x1000
	.if HIGH
	movl $PT + 0x1003, PT + 0xff8
	movl $PT + 0x2003, PT + 0x1ff0
	.endif
	mov $PT + 0x2000, %edi
	mov $0x83, %eax
	mov $512, %ecx
1:	mov %eax, (%edi)
	add $0x200000, %eax
	add $8, %edi
	loop 1b
	mov $PT, %eax
	mov %eax, %cr3
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4
	mov $0xc0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr
	lgdt LOAD + gdt + 24 - header
	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0
	ljmp $0x08, $LOAD + 2f - header

	.code64
2:	mov $0x10, %eax
	mov %eax, %ds
	mov %eax, %es
	mov %eax, %ss
	mov $TOP, %esp
	mov %ebp, %edi
	mov %esi, %esi
	.if HIGH
	mov $ENTRY, %rax
	.else
	mov $ENTRY, %eax
	.endif
	call *%rax
3:	cli
	hlt
	jmp 3b

	.org 0x100
gdt:
	.quad 0, 0x00209a0000000000, 0x0000920000000000
	.word 23
	.long LOAD + gdt - header
	.org 0x1000
