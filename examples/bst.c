/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>

typedef struct Tree Tree;
struct Tree {
	int key;
	Tree *left, *right;
};

static Tree *insert(Tree *t, int key) {
	if (!t) {
		t = calloc(1, sizeof *t);
		t->key = key;
	} else if (key < t->key) t->left = insert(t->left, key);
	else if (key > t->key) t->right = insert(t->right, key);
	return t;
}

static Tree *erase(Tree *t, int key) {
	if (!t) return NULL;
	if (key < t->key) t->left = erase(t->left, key);
	else if (key > t->key) t->right = erase(t->right, key);
	else if (!t->left || !t->right) {
		Tree *child = t->left ? t->left : t->right;
		free(t);
		return child;
	} else {
		Tree *m = t->right;
		while (m->left) m = m->left;
		t->key = m->key;
		t->right = erase(t->right, m->key);
	}
	return t;
}

static int height(Tree *t) {
	if (!t) return 0;
	int l = height(t->left), r = height(t->right);
	return 1 + (l > r ? l : r);
}

static void inorder(Tree *t) {
	if (!t) return;
	inorder(t->left);
	printf(" %d", t->key);
	inorder(t->right);
}

static void draw(Tree *t, int depth) {
	if (!t) return;
	draw(t->right, depth + 1);
	printf("%*s%d\n", depth * 4, "", t->key);
	draw(t->left, depth + 1);
}

int main(void) {
	int keys[] = { 50, 30, 70, 20, 40, 60, 80, 35, 45, 65, 10, 75, 85, 33 };
	Tree *t = NULL;
	for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) t = insert(t, keys[i]);
	printf("inorder:");
	inorder(t);
	printf("\nheight %d\n", height(t));
	draw(t, 0);
	t = erase(t, 30);
	t = erase(t, 50);
	t = erase(t, 85);
	printf("after erasing 30 50 85:");
	inorder(t);
	printf("\nheight %d\n", height(t));
	draw(t, 0);
	return 0;
}
