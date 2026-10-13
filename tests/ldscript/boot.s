# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

	.section .text.boot, "ax"
	.code32
	.globl boot32
boot32:
	mov $boot_stack_top, %esp
	mov $low_table, %eax
	movl $kernel_phys_end, (%eax)
	ljmp $8, $boot64
	.code64
boot64:
	call helper
	movabs $kmain, %rax
	call *%rax
1:	hlt
	jmp 1b

	.section .rodata.boot, "a"
low_table:
	.long text_size, data_lma

	.section .init_array.20, "aw"
	.quad init_b
	.section .init_array.10, "aw"
	.quad init_a
	.section .init_array, "aw"
	.quad init_c

	.section .bss.stack, "aw", @nobits
	.p2align 4
	.skip 256
	.globl boot_stack_top
boot_stack_top:

	.section .note.dropme, "a"
	.long 1, 2, 3

	.section .comment
	.string "boot"
