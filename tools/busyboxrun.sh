#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BB=${BUSYBOX_DIR:-$ROOT/busybox}
DL=$BB/dl
MUSL_VER=1.2.5
MUSL_SHA=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
MUSL_KEY=836489290BB6B70F99FFDA0556BCDB593020450F
BB_VER=1.36.1
LINUX=$ROOT/linux/linux-5.4.302
KOUT=${LINUX_OUT:-$ROOT/linux/build-busybox}
SCCC=$ROOT/sccc
CC=$ROOT/tools/musl-sccc
STAGE=${1:-run}
CHECK=${BUSYBOX_CHECK:-0}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

say() { echo "busyboxrun: $*"; }
die() { echo "busyboxrun: $*"; exit 1; }
need() {
	for t in "$@"; do command -v $t >/dev/null 2>&1 || die "$t not found"; done
}
CAP=
if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope -q true >/dev/null 2>&1; then
	CAP="systemd-run --user --scope -q -p MemoryMax=6G"
fi
capped() { timeout -s KILL ${T:-60} ${CAP:+systemd-run --user --scope -q -p MemoryMax=1G} "$@"; }
done_if() { [ "$STAGE" = "$1" ] && exit 0; }

need curl sha256sum tar bzip2 gzip make gcc ar qemu-system-x86_64
[ -x "$SCCC" ] || make -C "$ROOT" sccc >/dev/null || exit 1
mkdir -p "$DL"

get() {
	[ -f "$DL/$2" ] && return 0
	say "downloading $1/$2"
	curl -fsSL -o "$DL/$2.part" "$1/$2" || exit 1
	mv "$DL/$2.part" "$DL/$2"
}
if [ ! -f "$BB/musl-$MUSL_VER/Makefile" ]; then
	get https://musl.libc.org/releases musl-$MUSL_VER.tar.gz
	[ "$(sha256sum "$DL/musl-$MUSL_VER.tar.gz" | cut -d' ' -f1)" = $MUSL_SHA ] || die "sha256 of musl-$MUSL_VER.tar.gz does not match"
	if command -v gpg >/dev/null 2>&1; then
		get https://musl.libc.org/releases musl-$MUSL_VER.tar.gz.asc
		get https://musl.libc.org musl.pub
		G=$(mktemp -d)
		gpg -q --homedir "$G" --import "$DL/musl.pub" 2>/dev/null
		fpr=$(gpg --homedir "$G" --with-colons --fingerprint 2>/dev/null | grep '^fpr' | head -1 | cut -d: -f10)
		gpg -q --homedir "$G" --verify "$DL/musl-$MUSL_VER.tar.gz.asc" "$DL/musl-$MUSL_VER.tar.gz" 2>/dev/null; ok=$?
		rm -rf "$G"
		[ $ok = 0 ] && [ "$fpr" = $MUSL_KEY ] || die "the signature of musl-$MUSL_VER.tar.gz does not verify"
	fi
	tar -xzf "$DL/musl-$MUSL_VER.tar.gz" -C "$BB" || exit 1
fi
if [ ! -f "$BB/busybox-$BB_VER/Makefile" ]; then
	get https://busybox.net/downloads busybox-$BB_VER.tar.bz2
	get https://busybox.net/downloads busybox-$BB_VER.tar.bz2.sha256
	[ "$(sha256sum "$DL/busybox-$BB_VER.tar.bz2" | cut -d' ' -f1)" = "$(cut -d' ' -f1 "$DL/busybox-$BB_VER.tar.bz2.sha256")" ] || die "sha256 of busybox-$BB_VER.tar.bz2 does not match"
	tar -xjf "$DL/busybox-$BB_VER.tar.bz2" -C "$BB" || exit 1
fi
"$ROOT/tools/linuxrun.sh" fetch || exit 1
done_if fetch

musl_build() {
	if [ ! -f "$BB/$2/lib/libc.a" ]; then
		say "building musl with $1"
		rm -rf "$BB/musl-build-$3"; mkdir -p "$BB/musl-build-$3"
		(cd "$BB/musl-build-$3" && "$BB/musl-$MUSL_VER/configure" CC="$1" AR=ar RANLIB=ranlib --disable-shared --prefix="$BB/$2" &&
			$CAP make -j"$JOBS" && make install) > "$BB/musl-build-$3.log" 2>&1 || { tail -20 "$BB/musl-build-$3.log"; die "musl build with $3 failed"; }
	fi
}
musl_build "$SCCC" musl sccc
if [ ! -f "$BB/kheaders/include/linux/vt.h" ]; then
	say "installing the Linux UAPI headers"
	mkdir -p "$BB/kheaders-build"
	make -C "$LINUX" O="$BB/kheaders-build" ARCH=x86_64 HOSTCC=gcc headers_install INSTALL_HDR_PATH="$BB/kheaders" > "$BB/kheaders.log" 2>&1 || die "headers_install failed"
fi
GCCW=$BB/gcc-musl
printf '#!/bin/sh\nSCCC=gcc MUSL=%s MUSL_EXTRA=%s exec %s "$@"\n' "$BB/musl-gcc" "$(gcc -print-libgcc-file-name)" "$CC" > "$GCCW"
chmod +x "$GCCW"
if [ $CHECK = 1 ]; then
	musl_build gcc musl-gcc gcc
	"$CC" -O2 "$ROOT/tools/musltest.c" -o "$BB/musltest-sccc" || die "musltest does not build with sccc"
	"$GCCW" -O2 "$ROOT/tools/musltest.c" -o "$BB/musltest-gcc" || die "musltest does not build with gcc"
	T=20 capped "$BB/musltest-sccc" a b > "$BB/musltest-sccc.out" 2>&1; s=$?
	T=20 capped "$BB/musltest-gcc" a b > "$BB/musltest-gcc.out" 2>&1; g=$?
	[ $s = $g ] && cmp -s "$BB/musltest-sccc.out" "$BB/musltest-gcc.out" || { diff "$BB/musltest-sccc.out" "$BB/musltest-gcc.out" | head; die "musl test differs from gcc"; }
	say "ok: musl built by sccc matches gcc on tools/musltest.c"
fi
done_if musl

bb_config() {
	O=$1
	mkdir -p "$O"
	make -C "$BB/busybox-$BB_VER" O="$O" HOSTCC=gcc allnoconfig > "$O/config.log" 2>&1 || return 1
	F=$ROOT/tools/busybox.config
	syms=$(grep -v '^#' "$F" | grep . | sed 's/=.*//' | paste -sd'|' -)
	grep -v -E "^(# )?($syms)[= ]" "$O/.config" > "$O/.config.new"
	grep -v '^#' "$F" | grep . >> "$O/.config.new"
	mv "$O/.config.new" "$O/.config"
	yes "" | make -C "$BB/busybox-$BB_VER" O="$O" HOSTCC=gcc oldconfig >> "$O/config.log" 2>&1 || return 1
	grep -v '^#' "$F" | grep . | while read -r l; do grep -qx "$l" "$O/.config" || say "warning: '$l' did not survive oldconfig"; done
}
bb_build() {
	O=$BB/bb-$2
	if [ ! -x "$O/busybox" ] || [ "$ROOT/tools/busybox.config" -nt "$O/.config" ]; then
		say "building BusyBox with $2"
		bb_config "$O" || die "BusyBox configuration failed"
		$CAP make -C "$BB/busybox-$BB_VER" O="$O" HOSTCC=gcc CC="$1" -j"$JOBS" > "$O/build.log" 2>&1 || { grep -E "error|Error" "$O/build.log" | head -20; die "BusyBox build with $2 failed"; }
	fi
}
bb_build "$CC" sccc
if [ $CHECK = 1 ]; then
	bb_build "$GCCW" gcc
	T=120 capped sh "$ROOT/tools/bbtest.sh" "$BB/bb-sccc/busybox" > "$BB/bbtest-sccc.out" 2>&1
	T=120 capped sh "$ROOT/tools/bbtest.sh" "$BB/bb-gcc/busybox" > "$BB/bbtest-gcc.out" 2>&1
	grep -q "== done" "$BB/bbtest-sccc.out" && cmp -s "$BB/bbtest-sccc.out" "$BB/bbtest-gcc.out" || { diff "$BB/bbtest-sccc.out" "$BB/bbtest-gcc.out" | head -20; die "BusyBox test differs from gcc"; }
	say "ok: BusyBox built by sccc matches gcc on tools/bbtest.sh"
fi
done_if busybox

if [ ! -s "$KOUT/vmlinux" ] || [ "$ROOT/tools/linux-busybox.config" -nt "$KOUT/.config" ] || [ "$ROOT/tools/linux.config" -nt "$KOUT/.config" ]; then
	say "building Linux with sccc (log in $KOUT/build.log)"
	mkdir -p "$KOUT"
	K="make -C $LINUX O=$KOUT ARCH=x86_64 CC=$SCCC AS=$SCCC LD=$SCCC HOSTCC=gcc"
	{ $K tinyconfig && "$LINUX/scripts/kconfig/merge_config.sh" -m -O "$KOUT" "$KOUT/.config" "$ROOT/tools/linux.config" "$ROOT/tools/linux-busybox.config" && $K olddefconfig; } > "$KOUT/config.log" 2>&1 || die "kernel configuration failed"
	for f in "$ROOT/tools/linux.config" "$ROOT/tools/linux-busybox.config"; do
		grep -v '^#' "$f" | grep . | while read -r l; do grep -qx "$l" "$KOUT/.config" || say "warning: '$l' did not survive olddefconfig"; done
	done
	$CAP $K -j"$JOBS" vmlinux > "$KOUT/build.log" 2>&1 || { grep -E "error|Error" "$KOUT/build.log" | head -20; die "the kernel build failed"; }
fi
done_if kernel

R=$BB/rootfs
rm -rf "$R"; mkdir -p "$R"
cat > "$R/inittab" <<'EOF'
::sysinit:/etc/init.d/rcS
::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
EOF
cat > "$R/rcS" <<'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs tmpfs /tmp
echo "sccc busybox: system ready"
EOF
{
	for d in /bin /sbin /usr /usr/bin /usr/sbin /dev /proc /sys /tmp /etc /etc/init.d /root; do echo "dir $d 0755 0 0"; done
	echo "nod /dev/console 0600 0 0 c 5 1"
	echo "file /bin/busybox $BB/bb-sccc/busybox 0755 0 0"
	echo "slink /init /bin/busybox 0777 0 0"
	echo "file /etc/inittab $R/inittab 0644 0 0"
	echo "file /etc/init.d/rcS $R/rcS 0755 0 0"
	T=10 capped "$BB/bb-sccc/busybox" --list-full | grep -v '^bin/busybox$' | while read -r p; do echo "slink /$p /bin/busybox 0777 0 0"; done
} > "$R/list"
"$KOUT/usr/gen_init_cpio" "$R/list" > "$BB/initramfs.cpio" || die "gen_init_cpio failed"
done_if initramfs

QEMU="qemu-system-x86_64 -display none -no-reboot -net none -m 256 -kernel $KOUT/vmlinux -initrd $BB/initramfs.cpio"
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then QEMU="$QEMU -enable-kvm -cpu host"; fi
APPEND="console=ttyS0 panic=-1"

if [ -t 0 ] && [ -t 1 ] && [ $CHECK = 0 ]; then
	say "booting; the BusyBox shell is on this terminal, Ctrl-a x quits QEMU, poweroff ends the session"
	exec ${CAP:+systemd-run --user --scope -q -p MemoryMax=1G} $QEMU -append "$APPEND" -serial mon:stdio
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/sccc-busyboxrun.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT
mkfifo "$TMP/in"
T=120 capped $QEMU -append "$APPEND" -monitor none -serial stdio < "$TMP/in" > "$TMP/out" 2>&1 &
pid=$!
exec 3> "$TMP/in"
waitfor() {
	n=0
	while [ $n -lt ${2:-60} ] && ! grep -aq "$1" "$TMP/out" && kill -0 $pid 2>/dev/null; do sleep 1; n=$((n + 1)); done
	grep -aq "$1" "$TMP/out"
}
waitfor '~ # ' 60 || { cat "$TMP/out"; die "no shell prompt"; }
i=0
while IFS= read -r cmd; do
	i=$((i + 1))
	printf '%s\necho __mark_%s__\n' "$cmd" $i >&3
	waitfor "^__mark_${i}__" 30 || { cat "$TMP/out"; die "no response to: $cmd"; }
done <<'EOF'
uname -a
ls /
cat /proc/meminfo
ps
mount
echo $((6*7))
echo one two three | tr ' ' '\n' | wc -l
sleep 1 & wait $!; echo "background=$?"
nosuchcommand; echo "missing=$?"
EOF
printf "cat <<END | sed 's/^/got: /'\nhello here-document\nEND\necho __mark_doc__\n" >&3
waitfor '^__mark_doc__' 30 || { cat "$TMP/out"; die "no response to the here-document"; }
printf 'poweroff\n' >&3
waitfor 'reboot: Power down' 30; off=$?
wait $pid; qrc=$?
exec 3>&-
sed '/terminating on signal/d' "$TMP/out"
echo
fail=0
for want in 'Linux (none) 5.4.302' 'proc' 'MemTotal:' 'PID' 'init' 'proc on /proc type proc' '^42' '^3' 'got: hello here-document' 'background=0' 'nosuchcommand: not found' 'missing=127'; do
	grep -aq -- "$want" "$TMP/out" || { say "missing in the output: $want"; fail=1; }
done
[ $off = 0 ] || { say "poweroff did not power down"; fail=1; }
[ $qrc = 0 ] || { say "QEMU did not exit cleanly ($qrc)"; fail=1; }
[ $fail = 0 ] || exit 1
say "ok: the sccc kernel booted the sccc BusyBox, ran the commands and powered off"
