#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

ROOT=$(cd "$(dirname "$0")/.." && pwd)
XV6=${XV6:-$ROOT/xv6}
XV6_URL=${XV6_URL:-https://github.com/swetland/xv6}
XV6_REV=${XV6_REV:-fa7208ce8ed31932a8738fbbecd46e29f55a3c6c}
CC=${CC:-$ROOT/sccc}
CMD=${1:-ls}

for t in git make perl gcc objcopy objdump qemu-system-x86_64; do
	if ! command -v $t >/dev/null 2>&1; then echo "xv6run: $t not found"; exit 1; fi
done

if [ ! -f "$XV6/kernel/kernel64.ld" ]; then
	echo "xv6run: fetching $XV6_URL into $XV6"
	git clone -q "$XV6_URL" "$XV6" || exit 1
	git -C "$XV6" checkout -q "$XV6_REV" || exit 1
fi

[ -x "$CC" ] || make -C "$ROOT" sccc >/dev/null || exit 1

echo "xv6run: building xv6 with $CC"
if ! make -C "$XV6" CC="$CC" AS="$CC" LD="$CC" out/kernel.elf fs.img > "$XV6/build.log" 2>&1; then
	tail -20 "$XV6/build.log"
	echo "xv6run: the build failed, see $XV6/build.log"
	exit 1
fi

CAP=
if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true >/dev/null 2>&1; then
	CAP="systemd-run --user --scope -q -p MemoryMax=1G"
fi
QEMU="qemu-system-x86_64 -display none -no-reboot -net none -smp 2 -m 512 -kernel $XV6/out/kernel.elf"
QEMU="$QEMU -drive file=$XV6/fs.img,index=1,media=disk,format=raw"

if [ -t 0 ] && [ -t 1 ]; then
	echo "xv6run: booting; the xv6 shell is on this terminal, Ctrl-a x quits QEMU"
	exec $CAP $QEMU -serial mon:stdio
fi

TMP=${TMPDIR:-/tmp}/sccc-xv6run.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
mkfifo "$TMP/in"
$CAP timeout -s KILL 120 $QEMU -monitor none -serial stdio < "$TMP/in" > "$TMP/out" 2>&1 &
pid=$!
exec 3> "$TMP/in"
n=0
while [ $n -lt 60 ] && ! grep -q '^\$ ' "$TMP/out" && kill -0 $pid 2>/dev/null; do sleep 1; n=$((n + 1)); done
ok=0
grep -q '^\$ ' "$TMP/out" && ok=1
if [ $ok = 1 ]; then
	printf '%s\n' "$CMD" >&3
	sleep 3
fi
kill $pid 2>/dev/null
wait $pid 2>/dev/null
exec 3>&-
sed '/terminating on signal/d' "$TMP/out"
echo
if [ $ok = 1 ]; then echo "xv6run: reached the shell in $n s"; else echo "xv6run: no shell prompt"; exit 1; fi
