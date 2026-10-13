# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

	.text
	.globl helper
	.p2align 5
helper:
	ret
	.byte 0xcc
	.p2align 3
	.globl helper2
helper2:
	lea table(%rip), %rax
	ret

	.data
	.p2align 3
	.quad helper, kmain, msg
	.byte 9

	.section .kdata, "aw"
	.long 0x11223344

	.comm shared, 24, 8
	.lcomm local_buf, 40

	.section .rodata.str, "a"
	.asciz "abc"
