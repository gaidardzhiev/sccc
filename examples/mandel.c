/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

int main(void) {
	const char *shades = " .:-=+*#%@";
	for (int y = 0; y < 24; y++) {
		double ci = -1.2 + y * 2.4 / 23;
		for (int x = 0; x < 78; x++) {
			double cr = -2.2 + x * 3.2 / 77;
			double zr = 0, zi = 0;
			int i = 0;
			while (i < 100 && zr * zr + zi * zi < 4.0) {
				double t = zr * zr - zi * zi + cr;
				zi = 2 * zr * zi + ci;
				zr = t;
				i++;
			}
			putchar(i == 100 ? '@' : shades[i % 9]);
		}
		putchar('\n');
	}
	return 0;
}
