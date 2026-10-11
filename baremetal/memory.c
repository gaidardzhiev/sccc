/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define FRAMES (0x40000000ul / 4096)
#define LOW (0x200000ul / 4096)
#define HDR 32

struct block {
	unsigned long size;
	struct block *next;
	int free;
};

static unsigned long bitmap[FRAMES / 64];
static unsigned long nfree, hint;
static struct block *heap;
static unsigned long heap_size;
static unsigned long rng = 12345;
static unsigned char *ptrs[256];
static unsigned long sizes[256];
static unsigned long frames[16384];

static int used(unsigned long f) { return (bitmap[f / 64] >> (f % 64)) & 1; }
static void mark(unsigned long f) { bitmap[f / 64] |= 1ul << (f % 64); }
static void unmark(unsigned long f) { bitmap[f / 64] &= ~(1ul << (f % 64)); }

static unsigned long frame_init(unsigned *info) {
	struct e820 *e = (struct e820 *)(info + 2);
	unsigned long usable = 0;
	for (unsigned long f = 0; f < FRAMES; f++) mark(f);
	for (unsigned i = 0; i < info[0]; i++) {
		unsigned long lo, hi;
		if (e[i].type != 1) continue;
		usable += e[i].len;
		lo = (e[i].base + 4095) / 4096;
		hi = (e[i].base + e[i].len) / 4096;
		if (lo < LOW) lo = LOW;
		if (hi > FRAMES) hi = FRAMES;
		for (unsigned long f = lo; f < hi; f++) if (used(f)) { unmark(f); nfree++; }
	}
	hint = LOW;
	return usable;
}

static unsigned long frame_alloc(void) {
	for (unsigned long i = 0; i < FRAMES; i++) {
		unsigned long f = (hint + i) % FRAMES;
		if (!used(f)) {
			mark(f);
			nfree--;
			hint = f + 1;
			return f * 4096;
		}
	}
	return 0;
}

static void frame_free(unsigned long a) {
	unmark(a / 4096);
	nfree++;
}

static unsigned long frame_alloc_run(unsigned long n) {
	for (unsigned long f = LOW; f + n <= FRAMES; f++) {
		unsigned long k = 0;
		while (k < n && !used(f + k)) k++;
		if (k == n) {
			for (k = 0; k < n; k++) mark(f + k);
			nfree -= n;
			return f * 4096;
		}
		f += k;
	}
	return 0;
}

static unsigned long next_rand(void) {
	rng = rng * 6364136223846793005ul + 1442695040888963407ul;
	return rng >> 33;
}

static void heap_init(unsigned long base, unsigned long size) {
	heap = (struct block *)base;
	heap->size = size - HDR;
	heap->next = 0;
	heap->free = 1;
	heap_size = size;
}

static void *kmalloc(unsigned long n) {
	n = (n + 15) & ~15ul;
	for (struct block *b = heap; b; b = b->next) {
		if (!b->free || b->size < n) continue;
		if (b->size >= n + HDR + 16) {
			struct block *r = (struct block *)((unsigned char *)b + HDR + n);
			r->size = b->size - n - HDR;
			r->next = b->next;
			r->free = 1;
			b->next = r;
			b->size = n;
		}
		b->free = 0;
		return (unsigned char *)b + HDR;
	}
	return 0;
}

static void kfree(void *p) {
	struct block *b = (struct block *)((unsigned char *)p - HDR);
	b->free = 1;
	for (b = heap; b; b = b->next)
		while (b->free && b->next && b->next->free) {
			b->size += HDR + b->next->size;
			b->next = b->next->next;
		}
}

static int test_frames(void) {
	unsigned long n = 0, total = nfree, a;
	int ok = 1;
	while ((a = frame_alloc()) != 0) {
		if (n < 16384) frames[n] = a;
		*(unsigned long *)a = a ^ 0x5a5a5a5a5a5a5a5aul;
		n++;
	}
	if (n != total || nfree != 0 || n > 16384) return 0;
	for (unsigned long i = 0; i < n; i++)
		if (*(unsigned long *)frames[i] != (frames[i] ^ 0x5a5a5a5a5a5a5a5aul) || frames[i] < 0x200000) ok = 0;
	for (unsigned long i = 0; i < n; i += 2) frame_free(frames[i]);
	for (unsigned long i = 0; i < n; i += 2) if (frame_alloc() == 0) ok = 0;
	if (frame_alloc() != 0) ok = 0;
	for (unsigned long i = 0; i < n; i++) frame_free(frames[i]);
	return ok && nfree == total;
}

static int test_heap(void) {
	unsigned long base = frame_alloc_run(256);
	int ok = 1;
	if (!base) return 0;
	heap_init(base, 256 * 4096);
	for (int op = 0; op < 3000; op++) {
		unsigned long i = next_rand() % 256;
		if (ptrs[i]) {
			for (unsigned long k = 0; k < sizes[i]; k++) if (ptrs[i][k] != (unsigned char)(i + k)) ok = 0;
			kfree(ptrs[i]);
			ptrs[i] = 0;
		} else {
			sizes[i] = 1 + next_rand() % 4000;
			ptrs[i] = kmalloc(sizes[i]);
			if (!ptrs[i] || (unsigned long)ptrs[i] % 16) { ok = 0; ptrs[i] = 0; continue; }
			for (unsigned long k = 0; k < sizes[i]; k++) ptrs[i][k] = (unsigned char)(i + k);
		}
	}
	for (int i = 0; i < 256; i++) if (ptrs[i]) kfree(ptrs[i]);
	return ok && heap->free && heap->next == 0 && heap->size == heap_size - HDR;
}

void kmain(unsigned *info) {
	unsigned long usable;
	serial_init();
	kputs("memory\n");
	usable = frame_init(info);
	check("e820", info[0] > 2 && usable > 60ul * 1024 * 1024 && nfree > 14000);
	check("frames", test_frames());
	check("heap", test_heap());
	finish();
}
