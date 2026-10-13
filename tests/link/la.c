/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

int lib_b(int x);
int lib_a(void) { return lib_b(20) + 1; }
