/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

int classify(int x) {
	switch (x) {
	case 0: return 10;
	case 1:
	case 2: return 20;
	case -5: return 30;
	default: return 40;
	case 100: return 50;
	}
}
int fallthrough(int x) {
	int r = 0;
	switch (x) {
	case 1: r += 1;
	case 2: r += 2; break;
	case 3: r += 3;
	default: r += 100;
	}
	return r;
}
int duff(int n) {
	int count = 0;
	int k = (n + 3) / 4;
	switch (n % 4) {
	case 0: do { count++;
	case 3: count++;
	case 2: count++;
	case 1: count++;
	} while (--k > 0);
	}
	return count;
}
int main() {
	if (classify(0) != 10 || classify(2) != 20 || classify(-5) != 30 || classify(7) != 40 || classify(100) != 50) return 1;
	if (fallthrough(1) != 3 || fallthrough(2) != 2 || fallthrough(3) != 103 || fallthrough(9) != 100) return 2;
	if (duff(7) != 7 || duff(8) != 8) return 3;
	int i = 0, s = 0;
	do { s += i; i++; } while (i < 5);
	if (s != 10) return 4;
	i = 0; s = 0;
	do { i++; if (i == 2) continue; if (i == 5) break; s += i; } while (i < 10);
	if (s != 8) return 5;
	s = 0;
	for (int k = 0; k < 10; k++) { if (k % 2) continue; s += k; }
	if (s != 20) return 6;
	s = 0;
	for (;;) { s++; if (s == 7) break; }
	if (s != 7) return 7;
	i = 0;
again:
	i++;
	if (i < 5) goto again;
	if (i != 5) return 8;
	goto skip;
	return 9;
skip:
	s = 0;
	for (int a = 0; a < 3; a++)
		for (int b = 0; b < 3; b++) {
			if (b == 2) break;
			s++;
		}
	if (s != 6) return 10;
	while (1) {
		switch (s) { case 6: s = 100; continue; default: break; }
		break;
	}
	if (s != 100) return 11;
	if (s == 100) s = 1; else s = 2;
	if (s != 1) return 12;
	int x = 0;
	{ int x = 5; if (x != 5) return 13; }
	if (x != 0) return 14;
	return 0;
}
