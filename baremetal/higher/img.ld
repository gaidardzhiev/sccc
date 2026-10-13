/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

ENTRY(kmain)
KERNEL_VMA = 0xffffffff80000000;

SECTIONS
{
	. = KERNEL_VMA + 0x101000;
	_phys_start = . - KERNEL_VMA;
	.text : AT(0x101000) { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.initcall : {
		__initcall_start = .;
		KEEP(*(SORT_BY_NAME(.initcall.*)))
		__initcall_end = .;
	}
	.data : { *(.data) }
	.bss : ALIGN(16) { __bss_start = .; *(.bss .bss.*) *(COMMON) __bss_end = .; }
	_kernel_end = ALIGN(4K);
}
