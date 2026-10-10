#ifndef _STDARG_H
#define _STDARG_H
typedef struct {
	unsigned int gp_offset;
	unsigned int fp_offset;
	void *overflow_arg_area;
	void *reg_save_area;
} __va_elem;
typedef __va_elem va_list[1];
void *__va_arg_ptr(__va_elem *ap, unsigned long size);
#define va_start(ap, last) __builtin_va_start(ap)
#define va_arg(ap, type) (*(type *)__va_arg_ptr(ap, sizeof(type)))
#define va_end(ap) ((void)0)
#define va_copy(dst, src) ((dst)[0] = (src)[0])
#endif
