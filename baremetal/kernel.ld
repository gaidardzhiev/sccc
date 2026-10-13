/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

ENTRY(kmain)

SECTIONS
{
	. = 0x100000;
	.text : { *(.text) } =0
	.rodata : ALIGN(16) { *(.rodata) . = ALIGN(16); }
	.data : { *(.data) }
	.bss : ALIGN(16) { *(.bss) }
}
