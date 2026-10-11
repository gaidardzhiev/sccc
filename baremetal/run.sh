#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

DIR=$(cd "$(dirname "$0")" && pwd)
CC=${1:-$DIR/../sccc}

case $CC in /*) ;; *) CC=$(pwd)/$CC ;; esac

BXSHARE=${BXSHARE:-/usr/share/bochs}
TMP=${TMPDIR:-/tmp}/sccc-baremetal.$$

mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
skip=0
LIMIT=

command -v timeout >/dev/null 2>&1 && LIMIT="timeout -s KILL 60"

if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true >/dev/null 2>&1; then
	LIMIT="systemd-run --user --scope -q -p MemoryMax=512M -p RuntimeMaxSec=90 $LIMIT"
fi

if [ -z "$LIMIT" ]; then
	echo "baremetal: no timeout or systemd-run, not starting emulators"
	exit 0
fi

QEMU=
command -v qemu-system-x86_64 >/dev/null 2>&1 && QEMU=qemu-system-x86_64

BOCHS=
command -v bochs >/dev/null 2>&1 && [ -f "$BXSHARE/BIOS-bochs-latest" ] && BOCHS=bochs

VGABIOS=$BXSHARE/VGABIOS-lgpl-latest.bin
[ -f "$VGABIOS" ] || VGABIOS=$BXSHARE/VGABIOS-lgpl-latest

result() {
	if [ "$3" = ok ] && cmp -s "$DIR/$1.out" "$4"; then
		echo "ok   $1 ($2)"
		pass=$((pass + 1))
	else
		why=$3
		[ "$why" = ok ] && why="output differs"
		echo "FAIL $1 ($2): $why"
		[ -f "$4" ] && diff "$DIR/$1.out" "$4" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	fi
}

run_qemu() {
	out=$TMP/$1.qemu
	mon="-monitor none"
	if [ -f "$DIR/$1.keys" ]; then
		mkfifo "$TMP/mon.in" "$TMP/mon.out"
		mon="-monitor pipe:$TMP/mon"
	fi
	$LIMIT $QEMU -display none -no-reboot -m 64 -serial "file:$out" $mon \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-drive "file=$2,format=raw" > "$TMP/$1.qlog" 2>&1 &
	pid=$!
	if [ -f "$DIR/$1.keys" ]; then
		exec 3<>"$TMP/mon.in" 4<>"$TMP/mon.out"
		n=0
		while [ $n -lt 30 ] && ! grep -q ready "$out" 2>/dev/null && kill -0 $pid 2>/dev/null; do
			sleep 1
			n=$((n + 1))
		done
		for k in $(cat "$DIR/$1.keys"); do printf 'sendkey %s\n' "$k" >&3; done
	fi
	wait $pid
	status=$?
	if [ -f "$DIR/$1.keys" ]; then
		exec 3>&- 4>&-
		rm -f "$TMP/mon.in" "$TMP/mon.out"
	fi
	if [ $status -eq 33 ]; then result "$1" qemu ok "$out"; else result "$1" qemu "exit status $status" "$out"; fi
}

bochs_display() {
	printf 'megs: 16\nromimage: file=%s\nvgaromimage: file=%s\ndisplay_library: nogui\nlog: %s\n' \
		"$BXSHARE/BIOS-bochs-latest" "$VGABIOS" "$TMP/probe.log" > "$TMP/probe.rc"
	$LIMIT bochs -q -f "$TMP/probe.rc" > "$TMP/probe.out" 2>&1 < /dev/null
	if grep -q "display library 'nogui' not available" "$TMP/probe.out"; then echo sdl2; else echo nogui; fi
}

run_bochs() {
	out=$TMP/$1.bochs
	cat > "$TMP/$1.bxrc" <<EOT
megs: 64
cpu: model=corei7_sandy_bridge_2600k, ips=50000000
romimage: file=$BXSHARE/BIOS-bochs-latest
vgaromimage: file=$VGABIOS
display_library: $DISPLAYLIB
ata0-master: type=disk, path=$2, mode=flat
boot: disk
com1: enabled=1, mode=file, dev=$out
log: $TMP/$1.blog
panic: action=fatal
error: action=report
info: action=ignore
debug: action=ignore
EOT
	$LIMIT env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy bochs -q -f "$TMP/$1.bxrc" > "$TMP/$1.bout" 2>&1 < /dev/null
	if grep -q 'shutdown requested' "$TMP/$1.bout" "$TMP/$1.blog" 2>/dev/null; then result "$1" bochs ok "$out"
	else result "$1" bochs "no shutdown" "$out"; fi
}

[ -n "$BOCHS" ] && DISPLAYLIB=$(bochs_display)
for src in "$DIR"/*.c; do
	name=$(basename "$src" .c)
	img=$TMP/$name.img
	if ! "$CC" -ffreestanding -mgeneral-regs-only -oformat img -e kmain "$src" -o "$img" 2> "$TMP/err"; then
		echo "FAIL $name: compile error"
		sed 's/^/	/' "$TMP/err"
		fail=$((fail + 1))
		continue
	fi
	if [ -n "$QEMU" ]; then run_qemu "$name" "$img"; else skip=$((skip + 1)); fi
	if [ -n "$BOCHS" ]; then
		if [ -f "$DIR/$name.keys" ]; then
			"$CC" -ffreestanding -mgeneral-regs-only -oformat img -e kmain -DINJECT "$src" -o "$img" || fail=$((fail + 1))
		fi
		run_bochs "$name" "$img"
	else
		skip=$((skip + 1))
	fi
done

echo "baremetal: $pass passed, $fail failed, $skip skipped"

[ $fail -eq 0 ]
