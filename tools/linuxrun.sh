#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VER=${LINUX_VER:-5.4.302}
DIR=${LINUX_DIR:-$ROOT/linux}
SRC=$DIR/linux-$VER
URL=https://cdn.kernel.org/pub/linux/kernel/v${VER%%.*}.x
CC=${CC:-$ROOT/sccc}
OUT=${LINUX_OUT:-$DIR/build-sccc}
STAGE=${1:-run}
HOSTCC=${HOSTCC:-gcc}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

need() {
	for t in "$@"; do
		if ! command -v $t >/dev/null 2>&1; then echo "linuxrun: $t not found"; exit 1; fi
	done
}

need curl sha256sum tar xz make gcc bc flex bison qemu-system-x86_64

mkdir -p "$DIR"
if [ ! -f "$SRC/Makefile" ]; then
	tarball=linux-$VER.tar.xz
	if [ ! -f "$DIR/$tarball" ]; then
		echo "linuxrun: downloading $URL/$tarball"
		curl -fsSL -o "$DIR/$tarball.part" "$URL/$tarball" || exit 1
		mv "$DIR/$tarball.part" "$DIR/$tarball"
	fi
	curl -fsSL -o "$DIR/sha256sums.asc" "$URL/sha256sums.asc" || exit 1
	want=$(grep " $tarball\$" "$DIR/sha256sums.asc" | cut -d' ' -f1)
	got=$(sha256sum "$DIR/$tarball" | cut -d' ' -f1)
	if [ -z "$want" ] || [ "$want" != "$got" ]; then echo "linuxrun: sha256 of $tarball does not match kernel.org"; exit 1; fi
	echo "linuxrun: sha256 ok, unpacking"
	tar -xJf "$DIR/$tarball" -C "$DIR" || exit 1
fi
[ "$STAGE" = fetch ] && exit 0

[ -x "$CC" ] || make -C "$ROOT" sccc >/dev/null || exit 1
KMAKE="make -C $SRC O=$OUT ARCH=x86_64 CC=$CC AS=$CC LD=$CC HOSTCC=$HOSTCC"
CAP=
if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true >/dev/null 2>&1; then
	CAP="systemd-run --user --scope -q -p MemoryMax=6G"
fi

mkdir -p "$OUT"
FRAG=$ROOT/tools/linux.config
if [ ! -f "$OUT/.config" ] || [ "$FRAG" -nt "$OUT/.config" ]; then
	echo "linuxrun: configuring (tinyconfig + tools/linux.config)"
	{ $KMAKE tinyconfig && "$SRC/scripts/kconfig/merge_config.sh" -m -O "$OUT" "$OUT/.config" "$FRAG" && $KMAKE olddefconfig; } > "$OUT/sccc-config.log" 2>&1 || {
		tail -20 "$OUT/sccc-config.log"; echo "linuxrun: configuration failed, see $OUT/sccc-config.log"; exit 1; }
	grep -v '^#' "$FRAG" | grep . | while read -r line; do
		grep -qx "$line" "$OUT/.config" || echo "linuxrun: warning: '$line' did not survive olddefconfig"
	done
	grep '^# CONFIG' "$FRAG" | sed 's/^# \(CONFIG_[A-Za-z0-9_]*\) is not set$/\1/' | while read -r sym; do
		grep -q "^$sym=" "$OUT/.config" && echo "linuxrun: warning: $sym is still enabled"
	done
fi
[ "$STAGE" = config ] && exit 0

echo "linuxrun: building vmlinux with $CC (log in $OUT/sccc-build.log)"
if ! $CAP $KMAKE -j"$JOBS" vmlinux > "$OUT/sccc-build.log" 2>&1; then
	grep -E "error|Error" "$OUT/sccc-build.log" | head -20
	echo "linuxrun: the build failed, see $OUT/sccc-build.log"
	exit 1
fi
[ -s "$OUT/vmlinux" ] || { echo "linuxrun: no vmlinux"; exit 1; }
[ "$STAGE" = build ] && exit 0

echo "linuxrun: building /init with $CC"
"$CC" -O2 -ffreestanding "$ROOT/tools/linuxinit.c" -o "$OUT/sccc-init" || exit 1
printf 'file /init %s 0755 0 0\n' "$OUT/sccc-init" > "$OUT/sccc-initrd.list"
"$OUT/usr/gen_init_cpio" "$OUT/sccc-initrd.list" > "$OUT/sccc-initrd.cpio" || exit 1
[ "$STAGE" = init ] && exit 0

QEMU="qemu-system-x86_64 -display none -no-reboot -net none -m 256 -kernel $OUT/vmlinux"
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then QEMU="$QEMU -enable-kvm -cpu host"; fi
QCAP=
[ -n "$CAP" ] && QCAP="systemd-run --user --scope -q -p MemoryMax=1G"
LOG=$OUT/sccc-boot.log
run_qemu() {
	$QCAP timeout -s KILL ${LINUX_TIMEOUT:-60} $QEMU -monitor none -serial stdio "$@" < /dev/null > "$LOG" 2>&1
	sed '/terminating on signal/d' "$LOG"
}

echo "linuxrun: booting without an initramfs"
run_qemu -append "console=ttyS0 panic=-1"
grep -aq "Linux version" "$LOG" || { echo "linuxrun: no console output"; exit 1; }
echo "linuxrun: ok: console output over serial"
grep -aq "No working init found" "$LOG" || { echo "linuxrun: did not reach 'No working init found'"; exit 1; }
echo "linuxrun: ok: panic: no init found"

echo "linuxrun: booting with the sccc-built /init"
run_qemu -initrd "$OUT/sccc-initrd.cpio" -append "console=ttyS0 panic=-1"
grep -aq "sccc init: hello from userspace" "$LOG" || { echo "linuxrun: /init did not print"; exit 1; }
echo "linuxrun: ok: sccc-built init printed from userspace"
