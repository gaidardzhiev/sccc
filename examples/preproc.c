/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SQUARE(x) ((x) * (x))
#define LOG(fmt, ...) printf("%s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG(fmt, ...) printf("debug: " fmt "\n", ##__VA_ARGS__)
#define VERSION 2
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define COLORS(X) X(RED, 0xff0000) X(GREEN, 0x00ff00) X(BLUE, 0x0000ff) X(WHITE, 0xffffff)
#define AS_ENUM(name, rgb) name,
#define AS_NAME(name, rgb) #name,
#define AS_RGB(name, rgb) rgb,

enum Color { COLORS(AS_ENUM) NCOLORS };
static const char *color_names[] = { COLORS(AS_NAME) };
static const int color_rgb[] = { COLORS(AS_RGB) };

static int CAT(get, _answer)(void) { return 42; }

int main(void) {
	int CAT(x, 1) = 10, CAT(x, 2) = 20;
	printf("STR(a + b) = %s\n", STR(a + b));
	printf("XSTR(VERSION) = %s, STR(VERSION) = %s\n", XSTR(VERSION), STR(VERSION));
	printf("MAX(x1, x2) = %d, SQUARE(x1 + 1) = %d\n", MAX(x1, x2), SQUARE(x1 + 1));
	printf("get_answer() = %d\n", get_answer());
	LOG("x1=%d x2=%d", x1, x2);
	DEBUG("no extra arguments");
	DEBUG("with %s", "arguments");
	for (int c = 0; c < NCOLORS; c++) printf("%-5s = #%06x\n", color_names[c], color_rgb[c]);
	printf("ARRAY_LEN(color_rgb) = %d\n", (int)ARRAY_LEN(color_rgb));
#if VERSION >= 2 && defined(MAX)
	printf("version %d features on\n", VERSION);
#elif VERSION == 1
	printf("version 1\n");
#else
	printf("unknown version\n");
#endif
#ifdef __x86_64__
	printf("target is x86-64\n");
#endif
#undef VERSION
#ifndef VERSION
	printf("VERSION undefined\n");
#endif
	printf("__func__ = %s, __STDC_VERSION__ = %ldL\n", __func__, (long)__STDC_VERSION__);
	return 0;
}
