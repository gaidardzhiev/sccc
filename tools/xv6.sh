#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

XV6=${1:-$(cd "$(dirname "$0")/.." && pwd)/xv6}
CC=${2:-$(cd "$(dirname "$0")/.." && pwd)/sccc}
case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac
WAIT=${WAIT:-600}

if [ ! -f "$XV6/kernel/kernel64.ld" ]; then echo "xv6: no xv6-x86_64 source in $XV6, run tools/xv6run.sh first; skipped"; exit 0; fi
for t in qemu-system-x86_64 objcopy objdump perl gcc; do
	if ! command -v $t >/dev/null 2>&1; then echo "xv6: $t not found, skipped"; exit 0; fi
done

TMP=${TMPDIR:-/tmp}/sccc-xv6.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
LIMIT="timeout -s KILL $((WAIT + 30))"
if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true >/dev/null 2>&1; then
	LIMIT="systemd-run --user --scope -q -p MemoryMax=1G -p RuntimeMaxSec=$((WAIT + 60)) $LIMIT"
fi

build() {
	d=$1
	shift
	rm -rf "$TMP/$d"
	cp -r "$XV6" "$TMP/$d"
	rm -rf "$TMP/$d/.git"
	(cd "$TMP/$d" && make clean >/dev/null && make "$@" out/kernel.elf fs.img) > "$TMP/$d.build" 2>&1
}

boot() {
	out=$TMP/$1.serial
	: > "$out"
	mkfifo "$TMP/$1.in"
	$LIMIT qemu-system-x86_64 -display none -no-reboot -net none -smp 2 -m 512 -monitor none \
		-kernel "$TMP/$1/out/kernel.elf" -drive "file=$TMP/$1/fs.img,index=1,media=disk,format=raw" \
		-serial stdio < "$TMP/$1.in" > "$out" 2>&1 &
	pid=$!
	exec 3> "$TMP/$1.in"
	n=0
	while [ $n -lt 60 ] && ! grep -q '^\$ ' "$out" && kill -0 $pid 2>/dev/null; do sleep 1; n=$((n + 1)); done
	printf 'usertests\n' >&3
	n=0
	while [ $n -lt "$WAIT" ] && ! grep -q 'ALL TESTS PASSED\|FAILED\|panic' "$out" && kill -0 $pid 2>/dev/null; do sleep 1; n=$((n + 1)); done
	sleep 1
	kill $pid 2>/dev/null
	wait $pid 2>/dev/null
	exec 3>&-
	echo $n > "$TMP/$1.seconds"
}

if ! build sccc CC="$CC" AS="$CC" LD="$CC"; then
	echo "FAIL xv6: build with sccc"; tail -20 "$TMP/sccc.build" | sed 's/^/	/'; exit 1
fi
build gcc LDFLAGS="-m elf_x86_64" || { echo "xv6: the gcc build failed, no reference"; }
boot sccc
fail=0
if grep -q 'ALL TESTS PASSED' "$TMP/sccc.serial"; then
	echo "ok   xv6 built by sccc: usertests passed in $(cat "$TMP/sccc.seconds") s"
else
	echo "FAIL xv6 built by sccc"; tail -20 "$TMP/sccc.serial" | sed 's/^/	/'; fail=1
fi
if [ -f "$TMP/gcc/out/kernel.elf" ]; then
	boot gcc
	sed '/terminating on signal/d; s/ on cpu [0-9] eip 0x[0-9a-f]* / /; s/\r//' "$TMP/gcc.serial" > "$TMP/gcc.txt"
	sed '/terminating on signal/d; s/ on cpu [0-9] eip 0x[0-9a-f]* / /; s/\r//' "$TMP/sccc.serial" > "$TMP/sccc.txt"
	if cmp -s "$TMP/gcc.txt" "$TMP/sccc.txt"; then
		echo "ok   xv6 serial output matches the gcc build"
	else
		echo "FAIL xv6 serial output differs from the gcc build"; diff "$TMP/gcc.txt" "$TMP/sccc.txt" | head -20 | sed 's/^/	/'; fail=1
	fi
fi
exit $fail
