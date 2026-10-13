/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef int wchar_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
