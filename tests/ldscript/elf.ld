/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

ENTRY(kmain)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	note PT_NOTE;
}

SECTIONS
{
	. = 0x400000 + SIZEOF_HEADERS;
	.text : { *(.text.boot) *(.text .text.*) _etext = .; } :text
	.rodata : { *(.rodata*) }
	.notes : { *(.note.*) } :text :note
	. = ALIGN(CONSTANT(MAXPAGESIZE));
	.data : {
		*(.data)
		__init_start = .;
		KEEP(*(SORT_BY_NAME(.init_array.*) .init_array))
		__init_end = .;
	} :data
	.kdata : { *(.kdata) }
	.bss : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
	_end = .;
	PROVIDE(marker_value = 5);
	PROVIDE(provided_used = 6);
	table_bytes = ADDR(.kdata);
	text_size = SIZEOF(.text);
	data_lma = LOADADDR(.data);
	kernel_phys_end = _end;
	/DISCARD/ : { *(.comment) *(.discard.*) }
}
