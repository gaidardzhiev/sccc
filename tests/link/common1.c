/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

int counter;
int shared_buf[16];
int only_common[8];
int sum_commons(void) {
	shared_buf[3] = 4;
	only_common[7] = 30;
	return counter + shared_buf[3] + only_common[7] + (int)sizeof(only_common);
}
