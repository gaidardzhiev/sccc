/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

struct entry { const char *name; int (*fn)(void); };
static int one(void) { return 1; }
struct entry e1 __attribute__((section("mytab"), used)) = { "one", one };
