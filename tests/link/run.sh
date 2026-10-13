#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

DIR=$(cd "$(dirname "$0")" && pwd)
CC=${1:-$DIR/../../sccc}
case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac
INC=$DIR/../../lib/include

if ! command -v gcc >/dev/null 2>&1 || ! command -v ar >/dev/null 2>&1; then
	echo "link: gcc or ar not found, skipped"
	exit 0
fi

TMP=${TMPDIR:-/tmp}/sccc-link.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
GCC="gcc -std=c99 -O1 -fno-pic -fno-stack-protector -fcf-protection=none -nostdinc -I$INC"
FILES="main tab1 tab2 common1 common2 weak strong la lb lc tls"
pass=0
fail=0

build() {
	name=$1
	linker=$2
	shift 2
	B=$TMP/$name
	mkdir -p "$B"
	for f in $FILES; do
		flags=
		case $f in common*) flags=-fcommon ;; esac
		if [ "$1" = gcc ]; then $GCC $flags -c "$DIR/$f.c" -o "$B/$f.o"
		else "$CC" -c "$DIR/$f.c" -o "$B/$f.o"
		fi 2> "$B/err" || { echo "FAIL $name: compiling $f with $1"; sed 's/^/	/' "$B/err"; fail=$((fail + 1)); return; }
		shift
	done
	$GCC -fPIC -c "$DIR/pic.c" -o "$B/pic.o"
	ar rcs "$B/libx.a" "$B/la.o" "$B/lb.o" "$B/lc.o"
	objs="$B/main.o $B/tab1.o $B/tab2.o $B/common1.o $B/common2.o $B/weak.o $B/strong.o $B/pic.o $B/tls.o $B/libx.a"
	if [ "$linker" = gcc ]; then gcc -no-pie $objs -o "$B/prog" 2> "$B/err"
	else "$CC" $objs -o "$B/prog" 2> "$B/err"
	fi || { echo "FAIL $name: link"; sed 's/^/	/' "$B/err"; fail=$((fail + 1)); return; }
	timeout 10 "$B/prog" > "$B/out" 2>&1
	if [ $? -eq 0 ] && cmp -s "$B/out" "$DIR/link.out"; then
		echo "ok   $name"
		pass=$((pass + 1))
	else
		echo "FAIL $name"
		diff "$DIR/link.out" "$B/out" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	fi
}

build gcc-only gcc gcc gcc gcc gcc gcc gcc gcc gcc gcc gcc gcc
build sccc-objects sccc sccc sccc sccc sccc sccc sccc sccc sccc sccc sccc sccc
build mixed1 sccc sccc gcc sccc gcc gcc gcc sccc gcc sccc gcc sccc
build mixed2 sccc gcc sccc gcc sccc sccc sccc gcc sccc gcc sccc gcc
build gcc-objects sccc gcc gcc gcc gcc gcc gcc gcc gcc gcc gcc gcc
build mixed1-ld gcc sccc gcc sccc gcc gcc gcc sccc gcc sccc gcc sccc
build mixed2-ld gcc gcc sccc gcc sccc sccc sccc gcc sccc gcc sccc gcc
build sccc-ld gcc sccc sccc sccc sccc sccc sccc sccc sccc sccc sccc sccc

echo "link: $pass passed, $fail failed"
[ $fail -eq 0 ]
