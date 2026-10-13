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
	exp=$DIR/$1.out
	[ -f "$exp" ] || exp=$DIR/$1/kernel.out
	if [ "$3" = ok ] && cmp -s "$exp" "$4"; then
		echo "ok   $1 ($2)"
		pass=$((pass + 1))
	else
		why=$3
		[ "$why" = ok ] && why="output differs"
		echo "FAIL $1 ($2): $why"
		[ -f "$4" ] && diff "$exp" "$4" | head -10 | sed 's/^/	/'
		fail=$((fail + 1))
	fi
}

run_qemu() {
	out=$TMP/$1.qemu
	rm -f "$out"
	media="-drive file=$2,format=raw"
	[ "$3" = kernel ] && media="-kernel $2"
	mon="-monitor none"
	if [ -f "$DIR/$1.keys" ]; then
		mkfifo "$TMP/mon.in" "$TMP/mon.out"
		mon="-monitor pipe:$TMP/mon"
	fi
	$LIMIT $QEMU -display none -no-reboot -m 64 -serial "file:$out" $mon \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		$media > "$TMP/$1.qlog" 2>&1 &
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
	if [ $status -eq 33 ]; then result "$1" "qemu$TAG" ok "$out"; else result "$1" "qemu$TAG" "exit status $status" "$out"; fi
}

bochs_display() {
	printf 'megs: 16\nromimage: file=%s\nvgaromimage: file=%s\ndisplay_library: nogui\nlog: %s\n' \
		"$BXSHARE/BIOS-bochs-latest" "$VGABIOS" "$TMP/probe.log" > "$TMP/probe.rc"
	$LIMIT bochs -q -f "$TMP/probe.rc" > "$TMP/probe.out" 2>&1 < /dev/null
	if grep -q "display library 'nogui' not available" "$TMP/probe.out"; then echo sdl2; else echo nogui; fi
}

run_bochs() {
	out=$TMP/$1.bochs
	rm -f "$out"
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
	if grep -q 'shutdown requested' "$TMP/$1.bout" "$TMP/$1.blog" 2>/dev/null; then result "$1" "bochs$TAG" ok "$out"
	else result "$1" "bochs$TAG" "no shutdown" "$out"; fi
}

[ -n "$BOCHS" ] && DISPLAYLIB=$(bochs_display)
for src in "$DIR"/*.c; do
	name=$(basename "$src" .c)
	case $name in *_part*) continue ;; esac
	parts=
	for x in "$DIR/$name"_part*.c "$DIR/$name"_part*.S "$DIR/$name"_part*.s; do [ -f "$x" ] && parts="$parts $x"; done
	img=$TMP/$name.img
	for flags in "" -DASM_BUILTINS objects; do
		: > "$TMP/err"
		TAG=
		[ "$flags" = -DASM_BUILTINS ] && TAG=", asm builtins"
		[ "$flags" = objects ] && TAG=", objects"
		if [ "$flags" = objects ]; then
			objs=
			k=0
			for f in "$src" $parts; do
				k=$((k + 1))
				"$CC" -c -ffreestanding -mgeneral-regs-only "$f" -o "$TMP/$name.$k.o" 2>> "$TMP/err" && objs="$objs $TMP/$name.$k.o"
			done
			flags=
			build() { "$CC" -ffreestanding -oformat img -e kmain $objs -o "$img"; }
		else
			build() { "$CC" -ffreestanding -mgeneral-regs-only -oformat img -e kmain $flags "$src" $parts -o "$img"; }
		fi
		if ! build 2>> "$TMP/err"; then
			echo "FAIL $name$TAG: compile error"
			sed 's/^/	/' "$TMP/err"
			fail=$((fail + 1))
			continue
		fi
		if [ -n "$QEMU" ]; then run_qemu "$name" "$img"; else skip=$((skip + 1)); fi
		if [ -n "$BOCHS" ]; then
			if [ -f "$DIR/$name.keys" ]; then
				"$CC" -ffreestanding -mgeneral-regs-only -oformat img -e kmain -DINJECT $flags "$src" $parts -o "$img" || fail=$((fail + 1))
			fi
			run_bochs "$name" "$img"
		else
			skip=$((skip + 1))
		fi
	done
done

for src in "$DIR"/*.c; do
	name=$(basename "$src" .c)
	case $name in *_part*) continue ;; esac
	parts=
	for x in "$DIR/$name"_part*.c "$DIR/$name"_part*.S "$DIR/$name"_part*.s; do [ -f "$x" ] && parts="$parts $x"; done
	: > "$TMP/err"
	objs=
	k=0
	for f in "$src" $parts; do
		k=$((k + 1))
		"$CC" -c -ffreestanding -mgeneral-regs-only "$f" -o "$TMP/$name.$k.o" 2>> "$TMP/err" && objs="$objs $TMP/$name.$k.o"
	done
	for how in source objects; do
		if [ $how = source ]; then in="$src $parts"; else in=$objs; fi
		if "$CC" -ffreestanding -mgeneral-regs-only -oformat img -e kmain $in -o "$TMP/a.img" 2>> "$TMP/err" &&
			"$CC" -ffreestanding -mgeneral-regs-only -oformat img -T "$DIR/kernel.ld" $in -o "$TMP/b.img" 2>> "$TMP/err" &&
			cmp -s "$TMP/a.img" "$TMP/b.img"; then
			echo "ok   $name (kernel.ld, $how, same image)"
			pass=$((pass + 1))
		else
			echo "FAIL $name (kernel.ld, $how): the image differs"
			sed 's/^/	/' "$TMP/err"
			fail=$((fail + 1))
		fi
	done
done

H=$DIR/higher
hk() { "$CC" -ffreestanding -mgeneral-regs-only "$@" 2>> "$TMP/err"; }
: > "$TMP/err"
TAG=", own multiboot"
if hk -T "$H/kernel.ld" "$H/kernel.c" "$H/boot.S" -o "$TMP/higher.bin"; then
	if [ -n "$QEMU" ]; then run_qemu higher "$TMP/higher.bin" kernel; else skip=$((skip + 1)); fi
else
	echo "FAIL higher$TAG: compile error"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1))
fi
TAG=", objects"
if hk -c "$H/kernel.c" -o "$TMP/hk.o" && hk -c "$H/boot.S" -o "$TMP/hb.o" && hk -T "$H/kernel.ld" "$TMP/hk.o" "$TMP/hb.o" -o "$TMP/higher.obj.bin"; then
	if [ -n "$QEMU" ]; then run_qemu higher "$TMP/higher.obj.bin" kernel; else skip=$((skip + 1)); fi
	if command -v ld >/dev/null 2>&1; then
		if ld -T "$H/kernel.ld" "$TMP/hk.o" "$TMP/hb.o" -o "$TMP/higher.ld.bin" 2>> "$TMP/err" && cmp -s "$TMP/higher.ld.bin" "$TMP/higher.obj.bin"; then
			echo "ok   higher (GNU ld, same image)"
			pass=$((pass + 1))
		else
			echo "FAIL higher (GNU ld): the image differs"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1))
		fi
	fi
else
	echo "FAIL higher$TAG: compile error"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1))
fi
TAG=", img"
if hk -oformat img -T "$H/img.ld" "$H/kernel.c" -o "$TMP/higher.img"; then
	if [ -n "$QEMU" ]; then run_qemu higher "$TMP/higher.img"; else skip=$((skip + 1)); fi
	if [ -n "$BOCHS" ]; then run_bochs higher "$TMP/higher.img"; else skip=$((skip + 1)); fi
else
	echo "FAIL higher$TAG: compile error"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1))
fi
TAG=", multiboot"
if hk -oformat multiboot -T "$H/img.ld" "$H/kernel.c" -o "$TMP/higher.mb"; then
	if [ -n "$QEMU" ]; then run_qemu higher "$TMP/higher.mb" kernel; else skip=$((skip + 1)); fi
else
	echo "FAIL higher$TAG: compile error"; sed 's/^/	/' "$TMP/err"; fail=$((fail + 1))
fi

echo "baremetal: $pass passed, $fail failed, $skip skipped"

[ $fail -eq 0 ]
