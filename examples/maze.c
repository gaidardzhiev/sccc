#include <stdio.h>
#include <string.h>

#define W 31
#define H 15

static char m[H][W + 1];
static unsigned seed = 2026;

static int rnd(int n) {
	seed = seed * 1664525u + 1013904223u;
	return (int)((seed >> 16) % (unsigned)n);
}

static void carve(int y, int x) {
	int dirs[4][2] = { { 0, 2 }, { 0, -2 }, { 2, 0 }, { -2, 0 } };
	for (int i = 3; i > 0; i--) {
		int j = rnd(i + 1), t0 = dirs[i][0], t1 = dirs[i][1];
		dirs[i][0] = dirs[j][0];
		dirs[i][1] = dirs[j][1];
		dirs[j][0] = t0;
		dirs[j][1] = t1;
	}
	for (int i = 0; i < 4; i++) {
		int ny = y + dirs[i][0], nx = x + dirs[i][1];
		if (ny > 0 && ny < H - 1 && nx > 0 && nx < W - 1 && m[ny][nx] == '#') {
			m[y + dirs[i][0] / 2][x + dirs[i][1] / 2] = ' ';
			m[ny][nx] = ' ';
			carve(ny, nx);
		}
	}
}

static int solve(void) {
	static int qy[W * H], qx[W * H], from[H][W];
	int head = 0, tail = 0;
	memset(from, -1, sizeof from);
	qy[tail] = 1;
	qx[tail++] = 1;
	from[1][1] = 1 * W + 1;
	while (head < tail) {
		int y = qy[head], x = qx[head++];
		if (y == H - 2 && x == W - 2) break;
		static const int dy[] = { 0, 0, 1, -1 }, dx[] = { 1, -1, 0, 0 };
		for (int d = 0; d < 4; d++) {
			int ny = y + dy[d], nx = x + dx[d];
			if (m[ny][nx] != '#' && from[ny][nx] < 0) {
				from[ny][nx] = y * W + x;
				qy[tail] = ny;
				qx[tail++] = nx;
			}
		}
	}
	int len = 0;
	for (int y = H - 2, x = W - 2; !(y == 1 && x == 1); len++) {
		int p = from[y][x];
		m[y][x] = '.';
		y = p / W;
		x = p % W;
	}
	m[1][1] = '.';
	return len + 1;
}

int main(void) {
	for (int y = 0; y < H; y++) {
		memset(m[y], '#', W);
		m[y][W] = 0;
	}
	m[1][1] = ' ';
	carve(1, 1);
	for (int y = 0; y < H; y++) puts(m[y]);
	int len = solve();
	printf("\nshortest path: %d cells\n", len);
	for (int y = 0; y < H; y++) puts(m[y]);
	return 0;
}
