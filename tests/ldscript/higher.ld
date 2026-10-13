/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

OUTPUT_FORMAT("binary")
ENTRY(boot32)
KERNEL_VMA = 0xffffffff80000000;

SECTIONS
{
	. = 1M;
	.boot : { KEEP(*(.text.boot)) *(.rodata.boot) }
	.stack (NOLOAD) : ALIGN(16) { *(.bss.stack) }
	. += KERNEL_VMA;
	.text ALIGN(0x1000) : AT(ADDR(.text) - KERNEL_VMA) {
		*(.text)
		*(.text.*)
		_etext = .;
	} =0x90909090
	.rodata : { *(.rodata*) }
	.init : { __init_start = .; KEEP(*(SORT(.init_array.*) .init_array)) __init_end = .; }
	.data : ALIGN(4K) {
		*(.data .kdata)
		table_bytes = . - 8;
		QUAD(MAX(ADDR(.text), ADDR(.rodata)))
		QUAD(MIN(LOADADDR(.text), LOADADDR(.rodata)))
		LONG(DEFINED(provided_used) ? 1 : 2)
		LONG(DEFINED(nothing_like_this) ? 3 : 4)
	}
	.bss : { __bss_start = .; *(.bss) *(COMMON) __bss_end = ALIGN(16); }
	_end = ALIGN(., 4096);
	kernel_phys_end = _end - KERNEL_VMA;
	marker_value = ((1 << 12) | 0x34) * 2 - 6 / 3 % 4;
	provided_used = ~0 >> 60;
	text_size = SIZEOF(.text);
	data_lma = LOADADDR(.data);
	/DISCARD/ : { *(.note*) *(.comment) *(.discard.*) }
}
