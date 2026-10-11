/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "t14_multi.h"
static int calls;
static int helper(int x) { return x + 1; }
const char greeting[] = "hello from part 2";
struct point make_point(int x, int y) { struct point p; p.x = helper(x) - 1; p.y = y; return p; }
void bump_counter(void) { shared_counter = helper(shared_counter); calls++; }
int get_calls(void) { return calls; }
