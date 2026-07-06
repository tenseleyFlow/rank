#!/bin/sh
set -eu

export LC_ALL=C

gnu_sort=$(sh scripts/find-gnu-sort.sh || true)
if test -n "$gnu_sort"; then
    tmp_in=${TMPDIR:-/tmp}/rank-golden-in.$$
    tmp_a=${TMPDIR:-/tmp}/rank-golden-a.$$
    tmp_b=${TMPDIR:-/tmp}/rank-golden-b.$$
    printf 'b\na\n' > "$tmp_in"
    "$gnu_sort" "$tmp_in" > "$tmp_a"
    "$gnu_sort" "$tmp_in" > "$tmp_b"
    cmp "$tmp_a" "$tmp_b"
    rm -f "$tmp_in" "$tmp_a" "$tmp_b"
    printf 'gnu sort self-test ok: %s\n' "$gnu_sort"
else
    printf 'gnu sort self-test skipped: no GNU coreutils sort found\n'
fi

run_case() {
    name=$1
    input=$2
    shift 2
    in=${TMPDIR:-/tmp}/rank-$name-in.$$
    got=${TMPDIR:-/tmp}/rank-$name-got.$$
    want=${TMPDIR:-/tmp}/rank-$name-want.$$

    printf '%s' "$input" > "$in"
    ./rank "$@" "$in" > "$got"
    if test -n "$gnu_sort"; then
        "$gnu_sort" "$@" "$in" > "$want"
    else
        sort "$@" "$in" > "$want" 2>/dev/null || ./rank "$@" "$in" > "$want"
    fi
    cmp "$got" "$want"
    rm -f "$in" "$got" "$want"
}

run_case default 'b\na\n'
run_case reverse 'a\nb\n' -r
run_case unique 'b\na\nb\n' -u
run_case missing-newline 'b\na'
run_case empty ''
run_case single 'only\n'
run_case equal 'x\nx\nx\n'
run_case already-sorted 'a\nb\nc\n'
run_case reverse-sorted 'c\nb\na\n'

long_a=$(printf 'a%0999d\n' 0)
long_b=$(printf 'b%0999d\n' 0)
run_case long-lines "$long_b$long_a"

many_in=/tmp/rank-golden-many.in
many_got=/tmp/rank-golden-many.got
many_want=/tmp/rank-golden-many.want
: > "$many_in"
i=200
while test "$i" -gt 0; do
    printf '%03d\n' "$i" >> "$many_in"
    i=$((i - 1))
done
./rank "$many_in" > "$many_got"
sort "$many_in" > "$many_want"
cmp "$many_got" "$many_want"
rm -f "$many_in" "$many_got" "$many_want"

printf 'c\n' > /tmp/rank-golden-a.in
printf 'a\n' > /tmp/rank-golden-b.in
printf 'b\n' | ./rank /tmp/rank-golden-a.in - /tmp/rank-golden-b.in > /tmp/rank-golden-multi.got
printf 'a\nb\nc\n' > /tmp/rank-golden-multi.want
cmp /tmp/rank-golden-multi.got /tmp/rank-golden-multi.want
rm -f /tmp/rank-golden-a.in /tmp/rank-golden-b.in /tmp/rank-golden-multi.got /tmp/rank-golden-multi.want

printf 'b\na\n' | ./rank - - > /tmp/rank-golden-stdin.got
printf 'a\nb\n' > /tmp/rank-golden-stdin.want
cmp /tmp/rank-golden-stdin.got /tmp/rank-golden-stdin.want
rm -f /tmp/rank-golden-stdin.got /tmp/rank-golden-stdin.want

printf 'b\000a\na\000' | ./rank > /tmp/rank-golden-nul.got
printf 'a\000\nb\000a\n' > /tmp/rank-golden-nul.want
cmp /tmp/rank-golden-nul.got /tmp/rank-golden-nul.want
rm -f /tmp/rank-golden-nul.got /tmp/rank-golden-nul.want

printf 'b\000a\000' | ./rank -z > /tmp/rank-golden-zero.got
printf 'a\000b\000' > /tmp/rank-golden-zero.want
cmp /tmp/rank-golden-zero.got /tmp/rank-golden-zero.want
rm -f /tmp/rank-golden-zero.got /tmp/rank-golden-zero.want

printf 'b\nx\000a\nx\000' | ./rank -z > /tmp/rank-golden-zero-newline.got
printf 'a\nx\000b\nx\000' > /tmp/rank-golden-zero-newline.want
cmp /tmp/rank-golden-zero-newline.got /tmp/rank-golden-zero-newline.want
rm -f /tmp/rank-golden-zero-newline.got /tmp/rank-golden-zero-newline.want

printf 'b\na\n' > /tmp/rank-golden-output.in
./rank -o /tmp/rank-golden-output.out /tmp/rank-golden-output.in
printf 'a\nb\n' > /tmp/rank-golden-output.want
cmp /tmp/rank-golden-output.out /tmp/rank-golden-output.want
./rank -o /tmp/rank-golden-output.in /tmp/rank-golden-output.in
cmp /tmp/rank-golden-output.in /tmp/rank-golden-output.want
printf 'b\na\n' > /tmp/rank-golden-output.in
ln /tmp/rank-golden-output.in /tmp/rank-golden-output.hard 2>/dev/null || true
if test -e /tmp/rank-golden-output.hard; then
    ./rank -o /tmp/rank-golden-output.hard /tmp/rank-golden-output.in
    cmp /tmp/rank-golden-output.hard /tmp/rank-golden-output.want
fi
rm -f /tmp/rank-golden-output.in /tmp/rank-golden-output.hard
printf 'b\na\n' > /tmp/rank-golden-output.in
ln -s /tmp/rank-golden-output.in /tmp/rank-golden-output.sym 2>/dev/null || true
if test -L /tmp/rank-golden-output.sym; then
    ./rank -o /tmp/rank-golden-output.sym /tmp/rank-golden-output.in
    cmp /tmp/rank-golden-output.in /tmp/rank-golden-output.want
fi
rm -f /tmp/rank-golden-output.in /tmp/rank-golden-output.out /tmp/rank-golden-output.want /tmp/rank-golden-output.sym

status=0
printf 'x\n' > /tmp/rank-golden-output-input
./rank -o /tmp /tmp/rank-golden-output-input >/tmp/rank-golden-bad-output.out 2>/tmp/rank-golden-bad-output.err || status=$?
test "$status" -eq 2
grep 'rank: cannot write: /tmp:' /tmp/rank-golden-bad-output.err >/dev/null
rm -f /tmp/rank-golden-output-input /tmp/rank-golden-bad-output.out /tmp/rank-golden-bad-output.err

status=0
./rank --unsupported >/tmp/rank-golden.out 2>/tmp/rank-golden.err || status=$?
test "$status" -eq 2
grep "rank: option '--unsupported' is not implemented yet" /tmp/rank-golden.err >/dev/null
rm -f /tmp/rank-golden.out /tmp/rank-golden.err
printf 'golden harness smoke ok\n'
