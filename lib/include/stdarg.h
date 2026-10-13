/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _STDARG_H
#define _STDARG_H
#ifdef __GNUC__
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)
#else
typedef struct {
	unsigned int gp_offset;
	unsigned int fp_offset;
	void *overflow_arg_area;
	void *reg_save_area;
} __va_elem;
typedef __va_elem va_list[1];
#define va_start(ap, last) __builtin_va_start(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)0)
#define va_copy(dst, src) ((dst)[0] = (src)[0])
#endif
#endif
