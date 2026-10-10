#include <stdio.h>
#include <limits.h>
#include <string.h>

#define MAXN 16

static const char *city[] = { "Amsterdam", "Berlin", "Copenhagen", "Dresden", "Essen", "Frankfurt", "Geneva", "Hamburg", "Innsbruck", "Jena" };
static int n = 10;
static int dist[MAXN][MAXN];

static void road(int a, int b, int km) {
	dist[a][b] = dist[b][a] = km;
}

static void shortest(int src) {
	int d[MAXN], prev[MAXN], done[MAXN] = { 0 };
	for (int i = 0; i < n; i++) {
		d[i] = INT_MAX;
		prev[i] = -1;
	}
	d[src] = 0;
	for (int it = 0; it < n; it++) {
		int u = -1;
		for (int i = 0; i < n; i++)
			if (!done[i] && d[i] != INT_MAX && (u < 0 || d[i] < d[u])) u = i;
		if (u < 0) break;
		done[u] = 1;
		for (int v = 0; v < n; v++)
			if (dist[u][v] && d[u] + dist[u][v] < d[v]) {
				d[v] = d[u] + dist[u][v];
				prev[v] = u;
			}
	}
	printf("from %s:\n", city[src]);
	for (int i = 0; i < n; i++) {
		if (i == src) continue;
		if (d[i] == INT_MAX) {
			printf("  %-10s unreachable\n", city[i]);
			continue;
		}
		int path[MAXN], len = 0;
		for (int v = i; v >= 0; v = prev[v]) path[len++] = v;
		printf("  %-10s %5d km via", city[i], d[i]);
		while (len--) printf(" %c", city[path[len]][0]);
		printf("\n");
	}
}

int main(void) {
	memset(dist, 0, sizeof dist);
	road(0, 4, 210); road(0, 7, 460); road(4, 5, 250); road(7, 1, 290);
	road(7, 2, 340); road(1, 3, 190); road(3, 9, 160); road(9, 5, 260);
	road(5, 6, 590); road(5, 8, 520); road(6, 8, 560); road(1, 9, 240);
	road(4, 7, 360); road(3, 8, 610);
	shortest(0);
	shortest(6);
	return 0;
}
