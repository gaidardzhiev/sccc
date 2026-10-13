/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#define MB_MAGIC 0x1badb002
#define MB_FLAGS 0x00010002

	.section .multiboot, "a"
	.p2align 2
mb_header:
	.long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
	.long mb_header, _phys_start, _phys_load_end, _phys_bss_end, boot32

	.section .boot.text, "ax"
	.code32
	.globl boot32
boot32:
	cli
	cld
	mov $boot_pml4, %edi
	xor %eax, %eax
	mov $3 * 1024, %ecx
	rep stosl
	movl $boot_pdpt + 3, boot_pml4
	movl $boot_pdpt + 3, boot_pml4 + 511 * 8
	movl $boot_pd + 3, boot_pdpt
	movl $boot_pd + 3, boot_pdpt + 510 * 8
	mov $boot_pd, %edi
	mov $0x83, %eax
	mov $512, %ecx
1:	mov %eax, (%edi)
	add $0x200000, %eax
	add $8, %edi
	loop 1b
	mov $boot_pml4, %eax
	mov %eax, %cr3
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4
	mov $0xc0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr
	lgdt boot_gdtr
	mov %cr0, %eax
	or $0x80000001, %eax
	mov %eax, %cr0
	ljmp $0x08, $boot64

	.code64
boot64:
	mov $0x10, %eax
	mov %eax, %ds
	mov %eax, %es
	mov %eax, %ss
	movabs $stack_top, %rsp
	movabs $kmain, %rax
	call *%rax
2:	cli
	hlt
	jmp 2b

	.section .boot.rodata, "a"
	.p2align 3
boot_gdt:
	.quad 0, 0x00209a0000000000, 0x0000920000000000
boot_gdtr:
	.word 23
	.long boot_gdt

	.section .boot.bss, "aw", @nobits
	.p2align 12
boot_pml4:
	.skip 4096
boot_pdpt:
	.skip 4096
boot_pd:
	.skip 4096

	.section .stack, "aw", @nobits
	.p2align 4
	.skip 16384
stack_top:
