#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

DIR=$(cd "$(dirname "$0")" && pwd)
CC=${1:-$DIR/../../sccc}
case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac

for t in gcc as ld ar objdump nm; do
	if ! command -v $t >/dev/null 2>&1; then echo "ldscript: $t not found, skipped"; exit 0; fi
done

TMP=${TMPDIR:-/tmp}/sccc-ldscript.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
GCC="gcc -std=c99 -O1 -fno-pic -mcmodel=kernel -fno-stack-protector -fcf-protection=none -ffreestanding -fno-asynchronous-unwind-tables -c"
pass=0
fail=0

objects() {
	B=$TMP/$1
	mkdir -p "$B"
	if [ "$2" = sccc ]; then "$CC" -c "$DIR/kern.c" -o "$B/kern.o"; else $GCC "$DIR/kern.c" -o "$B/kern.o"; fi || return 1
	if [ "$3" = sccc ]; then "$CC" -c "$DIR/boot.s" -o "$B/boot.o"; else as "$DIR/boot.s" -o "$B/boot.o"; fi || return 1
	if [ "$4" = sccc ]; then "$CC" -c "$DIR/util.s" -o "$B/util.o"; else as "$DIR/util.s" -o "$B/util.o"; fi || return 1
	rm -f "$B/libu.a"
	ar rcs "$B/libu.a" "$B/util.o"
}

dump() {
	objdump -h "$1" | awk '$1 ~ /^[0-9]+$/ { name = $2; line = $2 " " $3 " " $4 " " $5 " " $7; getline; if ($0 ~ /ALLOC/) print line }'
	nm -n "$1" | awk 'NF == 3 { print $1, $3 }' | LC_ALL=C sort
	for sec in $(objdump -h "$1" | awk '$1 ~ /^[0-9]+$/ { name = $2; getline; if ($0 ~ /CONTENTS/ && $0 ~ /ALLOC/) print name }'); do
		objdump -s -j "$sec" "$1" | tail -n +4
	done
}

check() {
	name=$1
	script=$DIR/$2.ld
	shift 2
	if ! ld -T "$script" "$@" -o "$TMP/ref" 2> "$TMP/err"; then
		echo "FAIL $name: ld"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1)); return
	fi
	if ! "$CC" -ffreestanding -T "$script" "$@" -o "$TMP/out" 2> "$TMP/err"; then
		echo "FAIL $name: sccc"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1)); return
	fi
	if head -c 4 "$TMP/ref" | grep -q ELF; then
		dump "$TMP/ref" > "$TMP/ref.txt"
		dump "$TMP/out" > "$TMP/out.txt"
		same=0
		cmp -s "$TMP/ref.txt" "$TMP/out.txt" && same=1
	else
		same=0
		cmp -s "$TMP/ref" "$TMP/out" && same=1
		[ $same = 1 ] || { xxd "$TMP/ref" > "$TMP/ref.txt"; xxd "$TMP/out" > "$TMP/out.txt"; }
	fi
	if [ $same = 1 ]; then
		echo "ok   $name"
		pass=$((pass + 1))
	else
		echo "FAIL $name"
		diff "$TMP/ref.txt" "$TMP/out.txt" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	fi
}

for set in "sccc sccc sccc" "gnu gnu gnu" "sccc gnu sccc" "gnu sccc gnu"; do
	set -- $set
	tag=$1-$2-$3
	if ! objects "$tag" "$@" 2> "$TMP/err"; then
		echo "FAIL $tag: objects"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1)); continue
	fi
	B=$TMP/$tag
	for s in flat higher elf orphan; do
		check "$s ($tag)" $s "$B/boot.o" "$B/kern.o" "$B/util.o"
		check "$s ($tag, archive)" $s "$B/boot.o" "$B/kern.o" "$B/libu.a"
	done
done

as "$DIR/orphans.s" -o "$TMP/orphans.o"
"$CC" -c "$DIR/orphans.s" -o "$TMP/orphans_s.o"
n=0
while IFS= read -r body; do
	n=$((n + 1))
	for obj in orphans orphans_s; do
		printf 'SECTIONS { . = 0x1000; %s /DISCARD/ : { *(.text .data .bss .note*) } }\n' "$body" > "$TMP/o.ld"
		ld -T "$TMP/o.ld" "$TMP/$obj.o" -o "$TMP/o.ref" 2> /dev/null
		"$CC" -ffreestanding -T "$TMP/o.ld" "$TMP/$obj.o" -o "$TMP/o.out" 2> "$TMP/err"
		objdump -h "$TMP/o.ref" | awk '$1 ~ /^[0-9]+$/ { print $2, $4 }' > "$TMP/o.ref.txt"
		objdump -h "$TMP/o.out" 2> /dev/null | awk '$1 ~ /^[0-9]+$/ { print $2, $4 }' > "$TMP/o.out.txt"
		if cmp -s "$TMP/o.ref.txt" "$TMP/o.out.txt"; then
			pass=$((pass + 1))
		else
			echo "FAIL orphans $n ($obj): $body"
			diff "$TMP/o.ref.txt" "$TMP/o.out.txt" | head -6 | sed 's/^/	/'
			fail=$((fail + 1))
		fi
	done
done < "$DIR/orphans.txt"
echo "ok   orphans: $n scripts"

hosted() {
	if ! "$CC" -T "$DIR/hosted.ld" "$@" -o "$TMP/hosted" 2> "$TMP/err"; then
		echo "FAIL hosted ($how): link"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1)); return
	fi
	(ulimit -v 1048576; timeout 10 "$TMP/hosted") > "$TMP/hosted.txt" 2>&1
	if [ $? -eq 0 ] && cmp -s "$TMP/hosted.txt" "$DIR/hosted.out"; then
		echo "ok   hosted ($how)"
		pass=$((pass + 1))
	else
		echo "FAIL hosted ($how)"
		diff "$DIR/hosted.out" "$TMP/hosted.txt" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	fi
}
how=source; hosted "$DIR/hosted.c"
how="sccc object"; "$CC" -c "$DIR/hosted.c" -o "$TMP/hs.o" && hosted "$TMP/hs.o"
how="gcc object"
gcc -std=c99 -O1 -fno-pic -fno-stack-protector -fcf-protection=none -nostdinc -I"$DIR/../../lib/include" -c "$DIR/hosted.c" -o "$TMP/hg.o" && hosted "$TMP/hg.o"

echo "ldscript: $pass passed, $fail failed"
[ $fail -eq 0 ]
