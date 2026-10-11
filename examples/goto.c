/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

static void duff_copy(char *to, const char *from, int count) {
	int n = (count + 7) / 8;
	switch (count % 8) {
	case 0: do { *to++ = *from++;
	case 7: *to++ = *from++;
	case 6: *to++ = *from++;
	case 5: *to++ = *from++;
	case 4: *to++ = *from++;
	case 3: *to++ = *from++;
	case 2: *to++ = *from++;
	case 1: *to++ = *from++;
		} while (--n > 0);
	}
}

static int find(int m[4][4], int target, int *row, int *col) {
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			if (m[i][j] == target) {
				*row = i;
				*col = j;
				goto found;
			}
	return 0;
found:
	return 1;
}

static int count_numbers(const char *s) {
	int count = 0;
start:
	if (!*s) goto done;
	if (*s >= '0' && *s <= '9') goto digits;
	s++;
	goto start;
digits:
	count++;
	while (*s >= '0' && *s <= '9') s++;
	goto start;
done:
	return count;
}

static const char *grade(int score) {
	switch (score / 10) {
	case 10:
	case 9: return "A";
	case 8: return "B";
	case 7: return "C";
	case 6: return "D";
	default: return "F";
	}
}

int main(void) {
	char src[64] = "Duff's device copies eight bytes per loop.", dst[64];
	for (int n = 0; n <= 17; n += 5) {
		memset(dst, '_', sizeof dst);
		dst[n] = 0;
		duff_copy(dst, src, n);
		printf("copy %2d: \"%s\"\n", n, dst);
	}
	int m[4][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 }, { 13, 14, 15, 16 } };
	int r, c;
	for (int t = 7; t <= 21; t += 7)
		if (find(m, t, &r, &c)) printf("%d found at [%d][%d]\n", t, r, c);
		else printf("%d not found\n", t);
	printf("numbers in \"a1 b22 333c d 4e55\": %d\n", count_numbers("a1 b22 333c d 4e55"));
	int scores[] = { 100, 93, 85, 71, 64, 12 };
	for (int i = 0; i < 6; i++) printf("%d -> %s\n", scores[i], grade(scores[i]));
	return 0;
}
