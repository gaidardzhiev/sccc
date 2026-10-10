#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct Entry Entry;
struct Entry {
	char *word;
	int count;
	Entry *next;
};

#define NBUCKET 64

static Entry *table[NBUCKET];
static int nentries;

static unsigned hash(const char *s) {
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

static void add(const char *w) {
	unsigned b = hash(w) % NBUCKET;
	for (Entry *e = table[b]; e; e = e->next)
		if (strcmp(e->word, w) == 0) {
			e->count++;
			return;
		}
	Entry *e = malloc(sizeof *e);
	e->word = malloc(strlen(w) + 1);
	strcpy(e->word, w);
	e->count = 1;
	e->next = table[b];
	table[b] = e;
	nentries++;
}

static int by_count(const void *x, const void *y) {
	const Entry *a = *(Entry *const *)x, *b = *(Entry *const *)y;
	if (a->count != b->count) return b->count - a->count;
	return strcmp(a->word, b->word);
}

int main(void) {
	char word[64];
	int n = 0, c, total = 0;
	while ((c = getchar()) != EOF) {
		if (isalpha(c) || c == '\'') {
			if (n < 63) word[n++] = (char)tolower(c);
		} else if (n) {
			word[n] = 0;
			add(word);
			total++;
			n = 0;
		}
	}
	Entry **all = malloc(nentries * sizeof *all);
	int k = 0;
	for (int i = 0; i < NBUCKET; i++)
		for (Entry *e = table[i]; e; e = e->next) all[k++] = e;
	qsort(all, nentries, sizeof *all, by_count);
	printf("%d words, %d distinct\n", total, nentries);
	for (int i = 0; i < nentries && i < 15; i++) printf("%3d  %s\n", all[i]->count, all[i]->word);
	return 0;
}
