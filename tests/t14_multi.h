/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef T14_MULTI_H
#define T14_MULTI_H
struct point { int x, y; };
struct point make_point(int x, int y);
void bump_counter(void);
int get_calls(void);
extern int shared_counter;
extern const char greeting[];
#endif
