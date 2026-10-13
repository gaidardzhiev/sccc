/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386:x86-64)
ENTRY(boot32)

SECTIONS
{
	. = 0x100000;
	.boot : { KEEP(*(.text.boot)) *(.rodata.boot) }
	.text : ALIGN(16) { *(.text .text.*) _etext = .; }
	.rodata : { *(.rodata .rodata.*) }
	.init : {
		__init_start = .;
		KEEP(*(SORT_BY_NAME(.init_array.*)))
		KEEP(*(.init_array))
		__init_end = .;
	}
	.data ALIGN(0x1000) : {
		*(.data)
		*(EXCLUDE_FILE(*util.o) .kdata)
		BYTE(0x5a) SHORT(0x1234) LONG(ADDR(.text)) QUAD(SIZEOF(.text))
		FILL(0xee)
		. = ALIGN(64);
		table_bytes = .;
		*(.kdata)
		. += 3;
	}
	.bss : { __bss_start = .; *(.bss .bss.*) *(COMMON) __bss_end = .; }
	_end = .;
	marker_value = 0x1234;
	PROVIDE(provided_used = _end + 16);
	PROVIDE(provided_unused = 1);
	text_size = SIZEOF(.text);
	data_lma = LOADADDR(.data);
	kernel_phys_end = _end;
	/DISCARD/ : { *(.note.*) *(.comment) *(.discard.*) }
	ASSERT(SIZEOF(.boot) > 0, "no boot code")
}
