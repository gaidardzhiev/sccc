#include <stdio.h>

static int g[9][9];
static long tries;

static int ok(int r, int c, int v) {
	for (int i = 0; i < 9; i++)
		if (g[r][i] == v || g[i][c] == v) return 0;
	int br = r / 3 * 3, bc = c / 3 * 3;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			if (g[br + i][bc + j] == v) return 0;
	return 1;
}

static int solve(int pos) {
	if (pos == 81) return 1;
	int r = pos / 9, c = pos % 9;
	if (g[r][c]) return solve(pos + 1);
	for (int v = 1; v <= 9; v++) {
		tries++;
		if (!ok(r, c, v)) continue;
		g[r][c] = v;
		if (solve(pos + 1)) return 1;
	}
	g[r][c] = 0;
	return 0;
}

static void show(void) {
	for (int r = 0; r < 9; r++) {
		if (r && r % 3 == 0) printf("------+-------+------\n");
		for (int c = 0; c < 9; c++) {
			if (c && c % 3 == 0) printf("| ");
			printf("%c ", g[r][c] ? '0' + g[r][c] : '.');
		}
		printf("\n");
	}
}

int main(void) {
	char line[64];
	int r = 0;
	while (r < 9 && fgets(line, sizeof line, stdin)) {
		int c = 0;
		for (char *p = line; *p && c < 9; p++) {
			if (*p >= '1' && *p <= '9') g[r][c++] = *p - '0';
			else if (*p == '.' || *p == '0') g[r][c++] = 0;
		}
		if (c == 9) r++;
	}
	printf("puzzle:\n");
	show();
	if (solve(0)) {
		printf("\nsolution (%ld tries):\n", tries);
		show();
	} else printf("no solution\n");
	return 0;
}
