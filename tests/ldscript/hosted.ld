/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

ENTRY(_start)

SECTIONS
{
	. = 0x500000 + SIZEOF_HEADERS;
	.text : { *(.text .text.*) }
	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(8);
		__cmds_start = .;
		KEEP(*(SORT(.cmds.*)))
		__cmds_end = .;
		banner = .;
		BYTE(0x6f) BYTE(0x6b) BYTE(0)
	}
	. = ALIGN(CONSTANT(MAXPAGESIZE)) + (. & (CONSTANT(MAXPAGESIZE) - 1));
	.data : { __data_start = .; *(.data .data.*) _edata = .; }
	data_page_offset = __data_start & 0xFFF;
	.bss : { __bss_begin = .; *(.bss .bss.*) *(COMMON) }
	_end = .;
}
