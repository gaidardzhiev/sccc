#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only
DIR=$(dirname "$0")
TMP=${TMPDIR:-/tmp}/sccc-gcc.$$
mkdir -p "$TMP"
for t in "$DIR"/*.c; do
	n=$(basename "$t" .c)
	case "$n" in *_part*) continue;; esac
	extra=""
	for x in "$DIR/${n}"_part*.c; do [ -f "$x" ] && extra="$extra $x"; done
	gcc -w -fno-builtin -o "$TMP/$n" "$t" $extra -I"$DIR" -lm || { echo "GCC-COMPILE-FAIL $n"; continue; }
	"$TMP/$n" > "$TMP/$n.stdout" 2>&1 || echo "GCC-FAIL $n exit $?"
	if [ -f "$DIR/$n.out" ] && ! cmp -s "$TMP/$n.stdout" "$DIR/$n.out"; then echo "GCC-OUTPUT-DIFF $n"; fi
done
rm -rf "$TMP"
