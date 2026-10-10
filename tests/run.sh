#!/bin/sh
CC=${1:-./sccc}
DIR=$(dirname "$0")
TMP=${TMPDIR:-/tmp}/sccc-tests.$$
mkdir -p "$TMP"
pass=0; fail=0
for t in "$DIR"/*.c; do
	n=$(basename "$t" .c)
	case "$n" in *_part*) continue;; esac
	extra=""
	for x in "$DIR/${n}"_part*.c; do [ -f "$x" ] && extra="$extra $x"; done
	if ! $CC "$t" $extra -o "$TMP/$n" 2>"$TMP/$n.err"; then
		echo "FAIL $n (compile)"; sed 's/^/    /' "$TMP/$n.err" | head -5; fail=$((fail+1)); continue
	fi
	"$TMP/$n" > "$TMP/$n.stdout" 2>&1; rc=$?
	if [ $rc -ne 0 ]; then echo "FAIL $n (exit $rc)"; fail=$((fail+1)); continue; fi
	if [ -f "$DIR/$n.out" ] && ! cmp -s "$TMP/$n.stdout" "$DIR/$n.out"; then
		echo "FAIL $n (output)"; diff "$DIR/$n.out" "$TMP/$n.stdout" | head -10; fail=$((fail+1)); continue
	fi
	pass=$((pass+1))
done
rm -rf "$TMP"
echo "tests: $pass passed, $fail failed"
[ $fail -eq 0 ]
