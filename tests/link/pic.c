/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

extern int pic_base;
int pic_value = 7;
int pic_call(int x) { return x + pic_base + pic_value; }
