/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

struct entry { const char *name; int (*fn)(void); };
static int two(void) { return 2; }
struct entry e2 __attribute__((section("mytab"), used)) = { "two", two };
double scale(double x, float f) { return x * f; }
