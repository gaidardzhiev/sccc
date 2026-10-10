#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/builtin.h}
quote() {
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/	/\\t/g' -e 's/^/	"/' -e 's/$/\\n"/' "$1"
}
headers() {
	(cd "$ROOT/lib/include" && find . -name '*.h' | sed 's|^\./||' | LC_ALL=C sort)
}
{
	echo "static const char *builtin_names[] = {"
	headers | while read -r h; do
		echo "	\"$h\","
	done
	echo "	NULL"
	echo "};"
	echo "static const char *builtin_srcs[] = {"
	headers | while read -r h; do
		quote "$ROOT/lib/include/$h"
		echo "	,"
	done
	echo "	NULL"
	echo "};"
	echo "static const char *builtin_libc[] = {"
	quote "$ROOT/lib/libc.c" | awk '{ print; if (NR % 40 == 0) print "\t," }'
	echo "	,"
	echo "	NULL"
	echo "};"
} > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
