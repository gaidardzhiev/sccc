/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

__thread int tls_counter = 40;
__thread long tls_zero;

int tls_step(void) {
	tls_zero += 2;
	return ++tls_counter + (int)tls_zero;
}
