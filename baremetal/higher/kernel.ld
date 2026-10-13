/* Copyright (C) 2026 Ivan Gaydardzhiev */
/* Licensed under the GPL-3.0-only */

OUTPUT_FORMAT(binary)
ENTRY(boot32)
KERNEL_VMA = 0xffffffff80000000;

SECTIONS
{
	. = 1M;
	_phys_start = .;
	.boot : {
		KEEP(*(.multiboot))
		*(.boot.text)
		*(.boot.rodata)
	}
	.boot.bss (NOLOAD) : ALIGN(4K) { *(.boot.bss) }

	. += KERNEL_VMA;
	.text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA) { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.initcall : {
		__initcall_start = .;
		KEEP(*(SORT_BY_NAME(.initcall.*)))
		__initcall_end = .;
	}
	.data : { *(.data) }
	_phys_load_end = LOADADDR(.data) + SIZEOF(.data);
	.bss : ALIGN(16) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		__bss_end = .;
	}
	_phys_bss_end = LOADADDR(.bss) + SIZEOF(.bss);
	.stack (NOLOAD) : ALIGN(16) { *(.stack) }
	_kernel_end = ALIGN(4K);
	/DISCARD/ : { *(.comment) *(.note.*) }
}
