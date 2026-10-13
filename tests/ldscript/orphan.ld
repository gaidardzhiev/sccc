/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

SECTIONS
{
	. = 0x200000;
	.text : { *(.text) }
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	.bss : { *(.bss) }
	__bss_start = ADDR(.bss);
	__bss_end = .;
	_end = .;
	_etext = 0;
	__init_start = 0;
	__init_end = 0;
	marker_value = 0;
	provided_used = 0;
	table_bytes = 0;
	text_size = 0;
	data_lma = 0;
	kernel_phys_end = 0;
}
