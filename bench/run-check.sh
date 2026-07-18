#!/bin/sh
set -eu

export LC_ALL=C

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/check-$stamp.txt"
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

if test -z "$gnu_sort"; then
    printf 'GNU sort oracle not found\n' >&2
    exit 77
fi

tmp=${TMPDIR:-/tmp}/rank-check-bench-$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

i=0
: > "$tmp/sorted.in"
: > "$tmp/early.in"
: > "$tmp/keyed.in"
while test "$i" -lt 200000; do
    printf '%06d\n' "$i" >> "$tmp/sorted.in"
    if test "$i" -eq 0; then
        printf '000001\n' >> "$tmp/early.in"
    elif test "$i" -eq 1; then
        printf '000000\n' >> "$tmp/early.in"
    else
        printf '%06d\n' "$i" >> "$tmp/early.in"
    fi
    printf 'row-%06d %06d\n' "$i" "$i" >> "$tmp/keyed.in"
    i=$((i + 1))
done

{
    printf 'kind=check\n'
    printf 'rank_version='
    ./rank --version
    printf 'gnu_sort=%s\n' "$gnu_sort"
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

run_case() {
    name=$1
    shift
    input=$1
    shift
    rank_time=$({ /usr/bin/time -p ./rank "$@" "$input" >/dev/null; } 2>&1 | sed -n 's/^real //p')
    gnu_time=$({ /usr/bin/time -p "$gnu_sort" "$@" "$input" >/dev/null; } 2>&1 | sed -n 's/^real //p')
    printf '%s rank_real=%s gnu_real=%s args=%s\n' "$name" "$rank_time" "$gnu_time" "$*" >> "$out"
}

run_case sorted "$tmp/sorted.in" -c
run_case early-disorder "$tmp/early.in" -c
run_case keyed-numeric "$tmp/keyed.in" -c -k2,2n

printf 'check smoke wrote %s\n' "$out"
