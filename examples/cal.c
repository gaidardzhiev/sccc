/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>

static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December" };

static int leap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in(int m, int y) {
	static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 1 && leap(y) ? 29 : d[m];
}

static int weekday(int d, int m, int y) {
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (m < 3) y--;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void month(int m, int y) {
	char title[32];
	int len = snprintf(title, sizeof title, "%s %d", months[m], y);
	printf("%*s%s\n", (20 - len) / 2, "", title);
	printf("Su Mo Tu We Th Fr Sa\n");
	int start = weekday(1, m + 1, y);
	printf("%*s", start * 3, "");
	for (int d = 1; d <= days_in(m, y); d++) {
		printf("%2d", d);
		if ((start + d) % 7 == 0) printf("\n");
		else if (d < days_in(m, y)) printf(" ");
	}
	printf("\n\n");
}

int main(int argc, char **argv) {
	int y = argc > 1 ? atoi(argv[1]) : 2026;
	int m = argc > 2 ? atoi(argv[2]) : 0;
	static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
	if (m >= 1 && m <= 12) month(m - 1, y);
	else
		for (int i = 0; i < 12; i++) month(i, y);
	printf("%d is %sa leap year\n", y, leap(y) ? "" : "not ");
	printf("1 January 2000 was a %s\n", days[weekday(1, 1, 2000)]);
	printf("20 July 1969 was a %s\n", days[weekday(20, 7, 1969)]);
	return 0;
}
