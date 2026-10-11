/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>

typedef struct Node Node;
struct Node {
	int value;
	Node *next;
};

static Node *push(Node *head, int v) {
	Node *n = malloc(sizeof *n);
	n->value = v;
	n->next = head;
	return n;
}

static Node *reverse(Node *head) {
	Node *prev = NULL;
	while (head) {
		Node *next = head->next;
		head->next = prev;
		prev = head;
		head = next;
	}
	return prev;
}

static Node *remove_if(Node *head, int (*pred)(int)) {
	Node **pp = &head;
	while (*pp) {
		if (pred((*pp)->value)) {
			Node *dead = *pp;
			*pp = dead->next;
			free(dead);
		} else pp = &(*pp)->next;
	}
	return head;
}

static Node *merge_sorted(Node *a, Node *b) {
	Node dummy, *t = &dummy;
	while (a && b) {
		if (a->value <= b->value) {
			t->next = a;
			a = a->next;
		} else {
			t->next = b;
			b = b->next;
		}
		t = t->next;
	}
	t->next = a ? a : b;
	return dummy.next;
}

static Node *sort(Node *head) {
	if (!head || !head->next) return head;
	Node *slow = head, *fast = head->next;
	while (fast && fast->next) {
		slow = slow->next;
		fast = fast->next->next;
	}
	Node *second = slow->next;
	slow->next = NULL;
	return merge_sorted(sort(head), sort(second));
}

static void print(const char *label, Node *n) {
	int count = 0;
	printf("%-9s", label);
	for (; n; n = n->next, count++) printf(" %d", n->value);
	printf("  (%d)\n", count);
}

static int is_even(int v) { return v % 2 == 0; }

int main(void) {
	Node *list = NULL;
	for (int i = 0; i < 15; i++) list = push(list, (i * 37 + 11) % 50);
	print("built", list);
	list = reverse(list);
	print("reversed", list);
	list = sort(list);
	print("sorted", list);
	list = remove_if(list, is_even);
	print("odd only", list);
	while (list) {
		Node *n = list->next;
		free(list);
		list = n;
	}
	print("freed", list);
	return 0;
}
