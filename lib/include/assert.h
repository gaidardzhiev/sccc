/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#undef assert
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __assert_fail(const char *expr, const char *file, int line);
#define assert(x) ((x) ? (void)0 : __assert_fail(#x, __FILE__, __LINE__))
#endif
