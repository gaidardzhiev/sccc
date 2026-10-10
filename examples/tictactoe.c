#include <stdio.h>

static char b[9];
static long nodes;

static int winner(void) {
	static const int lines[8][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 }, { 0, 3, 6 }, { 1, 4, 7 }, { 2, 5, 8 }, { 0, 4, 8 }, { 2, 4, 6 } };
	for (int i = 0; i < 8; i++) {
		char c = b[lines[i][0]];
		if (c != ' ' && c == b[lines[i][1]] && c == b[lines[i][2]]) return c;
	}
	return 0;
}

static int full(void) {
	for (int i = 0; i < 9; i++)
		if (b[i] == ' ') return 0;
	return 1;
}

static int minimax(char me, int depth) {
	nodes++;
	int w = winner();
	if (w) return w == 'X' ? 10 - depth : depth - 10;
	if (full()) return 0;
	int best = me == 'X' ? -100 : 100;
	for (int i = 0; i < 9; i++) {
		if (b[i] != ' ') continue;
		b[i] = me;
		int s = minimax(me == 'X' ? 'O' : 'X', depth + 1);
		b[i] = ' ';
		if (me == 'X' ? s > best : s < best) best = s;
	}
	return best;
}

static int best_move(char me) {
	int best = -1, score = me == 'X' ? -100 : 100;
	for (int i = 0; i < 9; i++) {
		if (b[i] != ' ') continue;
		b[i] = me;
		int s = minimax(me == 'X' ? 'O' : 'X', 1);
		b[i] = ' ';
		if (me == 'X' ? s > score : s < score) {
			score = s;
			best = i;
		}
	}
	return best;
}

static void show(void) {
	for (int r = 0; r < 3; r++) printf(" %c | %c | %c\n%s", b[r * 3], b[r * 3 + 1], b[r * 3 + 2], r < 2 ? "---+---+---\n" : "\n");
}

static void play(const char *title, int first_move) {
	char turn = 'X';
	for (int i = 0; i < 9; i++) b[i] = ' ';
	nodes = 0;
	printf("%s\n", title);
	if (first_move >= 0) {
		b[first_move] = 'X';
		turn = 'O';
	}
	while (!winner() && !full()) {
		b[best_move(turn)] = turn;
		turn = turn == 'X' ? 'O' : 'X';
	}
	show();
	int w = winner();
	if (w) printf("%c wins", w);
	else printf("draw");
	printf(" (%ld positions searched)\n\n", nodes);
}

int main(void) {
	play("perfect play from an empty board:", -1);
	play("X opens in a corner:", 0);
	play("X opens on an edge:", 1);
	b[0] = 'X'; b[1] = 'X'; b[2] = ' ';
	b[3] = 'O'; b[4] = 'O'; b[5] = ' ';
	b[6] = ' '; b[7] = ' '; b[8] = ' ';
	printf("O to move on\n");
	show();
	printf("O plays square %d\n", best_move('O'));
	return 0;
}
