#include <stdio.h>
#include <string.h>

typedef struct {
	int freq;
	int sym;
	int left, right;
} Node;

static Node nodes[512];
static int nnodes;
static char codes[256][32];

static void assign(int n, char *buf, int depth) {
	if (nodes[n].sym >= 0) {
		buf[depth] = 0;
		strcpy(codes[nodes[n].sym], depth ? buf : "0");
		return;
	}
	buf[depth] = '0';
	assign(nodes[n].left, buf, depth + 1);
	buf[depth] = '1';
	assign(nodes[n].right, buf, depth + 1);
}

static int pop_min(int *heap, int *n) {
	int best = 0;
	for (int i = 1; i < *n; i++) {
		Node *a = &nodes[heap[i]], *b = &nodes[heap[best]];
		if (a->freq < b->freq || (a->freq == b->freq && heap[i] < heap[best])) best = i;
	}
	int r = heap[best];
	heap[best] = heap[--*n];
	return r;
}

int main(void) {
	const char *text = "this is an example of a huffman tree built by a self hosting c compiler";
	int freq[256] = { 0 }, heap[512], nheap = 0;
	for (const char *p = text; *p; p++) freq[(unsigned char)*p]++;
	for (int c = 0; c < 256; c++)
		if (freq[c]) {
			nodes[nnodes] = (Node){ freq[c], c, -1, -1 };
			heap[nheap++] = nnodes++;
		}
	while (nheap > 1) {
		int a = pop_min(heap, &nheap), b = pop_min(heap, &nheap);
		nodes[nnodes] = (Node){ nodes[a].freq + nodes[b].freq, -1, a, b };
		heap[nheap++] = nnodes++;
	}
	char buf[32];
	assign(heap[0], buf, 0);
	int bits = 0;
	for (int c = 0; c < 256; c++)
		if (freq[c]) {
			printf("'%c' %2d  %s\n", c, freq[c], codes[c]);
			bits += freq[c] * (int)strlen(codes[c]);
		}
	char encoded[1024] = "";
	for (const char *p = text; *p; p++) strcat(encoded, codes[(unsigned char)*p]);
	printf("%d chars, %d bits encoded vs %d bits plain (%.1f%%)\n", (int)strlen(text), bits, (int)strlen(text) * 8, 100.0 * bits / (strlen(text) * 8));
	char decoded[256];
	int n = 0, node = heap[0];
	for (const char *p = encoded; *p; p++) {
		node = *p == '0' ? nodes[node].left : nodes[node].right;
		if (nodes[node].sym >= 0) {
			decoded[n++] = (char)nodes[node].sym;
			node = heap[0];
		}
	}
	decoded[n] = 0;
	printf("decoded matches: %s\n", strcmp(decoded, text) == 0 ? "yes" : "no");
	return 0;
}
