#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

BB=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d "${TMPDIR:-/tmp}/sccc-bbtest.XXXXXX") || exit 1
trap 'chmod -R u+w "$T"; "$BB" rm -rf "$T"' EXIT
mkdir "$T/bin" "$T/work"
for a in $("$BB" --list); do ln -s "$BB" "$T/bin/$a"; done
cd "$T/work" || exit 1

cat > script.sh <<'EOF'
set -u
echo "== echo"
echo hello world
echo -n "no newline"; echo
echo -e 'tab\there\nnewline'
printf '%s|%5d|%-4s|%x|%o|%c|%%\n' str 42 ab 255 8 Z
printf '%.3s %05.1f %e\n' abcdef 3.14159 12345.678

echo "== arithmetic"
echo $((6*7)) $((7/2)) $((-7%3)) $((1<<10)) $((0x10+010)) $((2**10)) $(( (3+4)*(5-2) ))
i=0; while [ $i -lt 5 ]; do i=$((i+1)); done; echo "i=$i"
x=10; x=$((x*x-1)); echo "x=$x"

echo "== variables and expansion"
v="a b  c"; echo "$v" $v
unset nope; echo "${nope:-default} ${nope-dash} ${#v}"
p=/usr/local/lib/file.tar.gz
echo "${p##*/} ${p%%.*} ${p#*/} ${p%/*} ${p/lib/LIB}"
set -- one two three; echo "$# $1 $3 $*"; shift; echo "$@"
echo $(echo command substitution) `echo backquotes`
a=5; f() { local a=6; echo "in f a=$a args=$#"; return 3; }; f x y; echo "rc=$? a=$a"

echo "== control flow"
for w in alpha beta gamma; do case $w in a*) echo "A:$w";; b*) echo "B:$w";; *) echo "other:$w";; esac; done
if [ -z "" ] && [ -n x ] || false; then echo "and-or ok"; fi
n=0; until [ $n -ge 3 ]; do n=$((n+1)); [ $n -eq 2 ] && continue; echo "n=$n"; done
test 3 -gt 2 && echo "test ok"; [ "abc" = "abc" ] && echo "bracket ok"; [[ abc == a* ]] && echo "double bracket ok"
false; echo "false=$?"; true; echo "true=$?"
nosuchcommand 2>/dev/null; echo "missing=$?"

echo "== here-doc and pipes"
cat <<END | sort -r | head -n 2
line c
line a
line b
END
echo "one two three two one" | tr ' ' '\n' | sort | uniq -c | sort -rn | head -n 3
{ echo first; echo second; } > out.txt; wc -l < out.txt
echo appended >> out.txt; cat out.txt

echo "== background jobs"
sleep 0 & sleep 0 & wait; echo "waited"
( exit 7 ); echo "subshell=$?"

echo "== files"
mkdir -p d1/d2/d3; echo data > d1/d2/f.txt; cp d1/d2/f.txt d1/copy.txt; mv d1/copy.txt d1/moved.txt
ln -s d2/f.txt d1/link; ls d1; ls -R d1 | head -n 20; cat d1/link
rm d1/moved.txt; ls d1; touch empty; ls -la empty | cut -c1-10
chmod 600 empty; stat -c '%a %s %n' empty d1/d2/f.txt
rm -r d1; ls

echo "== text tools"
seq 1 20 > nums; seq 5 3 30 | tr '\n' ' '; echo
wc nums; wc -c nums; head -n 3 nums; tail -n 2 nums; tail -c 10 nums
grep -n 1 nums | head -n 4; grep -c 2 nums; grep -v '[0-5]' nums; grep -E '^(1|2)[05]$' nums
sed -n '3,5p' nums; sed 's/1/one/g' nums | head -n 12 | tail -n 3; sed -e '/5/d' -e 's/^/x/' nums | tail -n 3
printf 'b 2\na 10\nc 1\nb 1\n' > pairs; sort pairs; sort -k2 -n pairs; sort -u -k1,1 pairs
cut -d' ' -f2 pairs | sort -n | uniq; tr a-z A-Z < pairs; tr -d '0-9' < pairs | sort
expr 3 + 4 \* 5; expr length hello; basename /a/b/c.txt .txt; dirname /a/b/c.txt
env -i A=1 B=2 env | sort
echo "== done"
EOF
PATH="$T/bin" "$BB" sh script.sh 2>&1
echo "exit=$?"
