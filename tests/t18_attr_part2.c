/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

struct cmd { const char *name; int (*fn)(int); };

static int sq(int x) { return x * x; }
static const struct cmd cmd_square __attribute__((section("cmds"))) = { "square", sq };
static int hidden(void) { return 5; }

int strong_over = 99;
int wg(void) { return 20; }
__attribute__((weak)) int wf(void) { return 10; }
int other_unit(void) __attribute__((alias("hidden")));
