/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

__thread int shared;
__thread long table[4] = { 1, 2, 3, 4 };
extern __thread long seeded;

int bump(void) { return ++shared + (int)(seeded - seeded); }

long sum_table(void) {
	long s = 0;
	for (int i = 0; i < 4; i++) s += table[i];
	return s;
}
