#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

DIR=$(cd "$(dirname "$0")" && pwd)
CC=${1:-$DIR/../sccc}

case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac

MODE=check

[ "$2" = "-update" ] && MODE=update

TMP=${TMPDIR:-/tmp}/sccc-examples.$$

mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
LIMIT=
command -v timeout >/dev/null 2>&1 && LIMIT="timeout 10"

cd "$DIR"

for src in *.c; do
	case $src in *_part*.c) continue ;; esac
	name=${src%.c}
	parts=$(ls "$name"_part*.c 2>/dev/null)
	args=
	[ -f "$name.args" ] && args=$(cat "$name.args")
	input=/dev/null
	[ -f "$name.in" ] && input=$DIR/$name.in
	if [ $MODE = update ]; then
		gcc -std=c99 -w -fno-builtin -o "$TMP/$name" "$src" $parts -lm || { echo "gcc failed: $src"; exit 1; }
		(cd "$TMP" && $LIMIT ./"$name" $args < "$input" > "$DIR/$name.out")
		echo "updated $name.out"
		continue
	fi
	if ! "$CC" "$src" $parts -o "$TMP/$name" 2> "$TMP/err"; then
		echo "FAIL $name: compile error"
		sed 's/^/	/' "$TMP/err"
		fail=$((fail + 1))
		continue
	fi
	(cd "$TMP" && $LIMIT ./"$name" $args < "$input" > "$TMP/$name.got")
	status=$?
	if [ $status -ne 0 ]; then
		echo "FAIL $name: exit status $status"
		fail=$((fail + 1))
	elif ! cmp -s "$TMP/$name.got" "$name.out"; then
		echo "FAIL $name: output differs"
		diff "$name.out" "$TMP/$name.got" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	else
		echo "ok   $name"
		pass=$((pass + 1))
	fi
done

[ $MODE = update ] && exit 0

echo "examples: $pass passed, $fail failed"

[ $fail -eq 0 ]
