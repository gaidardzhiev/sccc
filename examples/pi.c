#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	int digits = argc > 1 ? atoi(argv[1]) : 500;
	int len = digits * 10 / 3 + 2;
	int *a = malloc(len * sizeof *a);
	int nines = 0, predigit = 0, printed = 0;
	for (int i = 0; i < len; i++) a[i] = 2;
	for (int j = 0; j <= digits; j++) {
		int q = 0;
		for (int i = len; i > 0; i--) {
			int x = 10 * a[i - 1] + q * i;
			a[i - 1] = x % (2 * i - 1);
			q = x / (2 * i - 1);
		}
		a[0] = q % 10;
		q /= 10;
		if (q == 9) nines++;
		else if (q == 10) {
			printed += printf("%d", predigit + 1);
			for (; nines > 0; nines--) printed += printf("0");
			predigit = 0;
		} else {
			if (j) printed += printf("%d", predigit);
			if (j == 1) printed += printf(".");
			predigit = q;
			for (; nines > 0; nines--) printed += printf("9");
		}
	}
	printf("%d\n", predigit);
	free(a);
	return 0;
}
