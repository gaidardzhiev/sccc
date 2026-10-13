# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

	.set ORG, 0x7c00
	.set GDT, 0x1c0
	.set GDTR, 0x1d8
	.set DAP, 0x1e0
	.set DRIVE, 0x1f0
	.set NSECTS, 0x1f2

	.code16
start:
	cli
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov $ORG, %sp
	ljmp $0, $ORG + 1f - start
1:	sti
	mov %dl, ORG + DRIVE
	mov $0x5008, %di
	xor %ebx, %ebx
	xor %bp, %bp
e820:
	mov $0xe820, %eax
	mov $0x534d4150, %edx
	mov $24, %ecx
	movl $1, 20(%di)
	int $0x15
	jc 2f
	cmp $0x534d4150, %eax
	jne 2f
	add $24, %di
	inc %bp
	cmp $64, %bp
	je 2f
	test %ebx, %ebx
	jnz e820
2:	mov %bp, 0x5000
	movw $0, 0x5002
	movl $0, 0x5004
read:
	mov ORG + NSECTS, %cx
	jcxz loaded
	mov $64, %ax
	cmp %ax, %cx
	jae 3f
	mov %cx, %ax
3:	mov %ax, ORG + DAP + 2
	sub %ax, ORG + NSECTS
	mov $ORG + DAP, %si
	mov ORG + DRIVE, %dl
	mov $0x42, %ah
	int $0x13
	jc fail
	mov ORG + DAP + 2, %ax
	add %ax, ORG + DAP + 8
	shl $5, %ax
	add %ax, ORG + DAP + 6
	jmp read
fail:
	mov $0x0e45, %ax
	int $0x10
4:	hlt
	jmp 4b
loaded:
	in $0x92, %al
	or $2, %al
	and $0xfe, %al
	out %al, $0x92
	cli
	cld
	mov $0x1000, %di
	xor %eax, %eax
	mov $0xc00, %cx
	rep; stosl
	movw $0x2003, 0x1000
	movw $0x3003, 0x2000
	.if HIGH
	movw $0x2003, 0x1ff8
	movw $0x3003, 0x2ff0
	.endif
	mov $0x3000, %di
	mov $0x83, %eax
	mov $512, %cx
5:	mov %eax, (%di)
	add $0x200000, %eax
	add $8, %di
	loop 5b
	mov $0x1000, %eax
	mov %eax, %cr3
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4
	mov $0xc0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr
	lgdtl ORG + GDTR
	mov %cr0, %eax
	or $0x80000001, %eax
	mov %eax, %cr0
	ljmp $0x08, $ORG + 6f - start

	.code64
6:	mov $0x10, %eax
	mov %eax, %ds
	mov %eax, %es
	mov %eax, %ss
	mov $0x10000, %esi
	mov $DEST, %edi
	mov $SIZE, %ecx
	rep movsb
	mov $ZERO, %ecx
	xor %eax, %eax
	rep stosb
	mov $0x80000, %esp
	mov $0x5000, %edi
	.if HIGH
	mov $ENTRY, %rax
	.else
	mov $ENTRY, %eax
	.endif
	call *%rax
7:	cli
	hlt
	jmp 7b

	.org GDT
	.quad 0, 0x00209a0000000000, 0x0000920000000000
	.word 23
	.long ORG + GDT
	.org DAP
	.byte 16, 0
	.word 0, 0, 0x1000
	.quad 1
	.byte 0
	.org NSECTS
	.word NSECT
	.org 510
	.word 0xaa55
