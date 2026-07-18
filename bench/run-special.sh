#!/bin/sh
set -eu

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/special-$stamp.txt"
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

if test -z "$gnu_sort"; then
    printf 'GNU sort oracle not found\n' >&2
    exit 77
fi

tmp=${TMPDIR:-/tmp}/rank-special-bench-$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

i=0
: > "$tmp/numeric.in"
: > "$tmp/general.in"
: > "$tmp/human.in"
: > "$tmp/month.in"
: > "$tmp/version.in"
: > "$tmp/random.in"
while test "$i" -lt 100000; do
    n=$((100000 - i))
    printf '%d.%03d\n' "$n" "$i" >> "$tmp/numeric.in"
    printf '%de%d\n' "$n" "$((i % 7))" >> "$tmp/general.in"
    printf '%dK\n' "$n" >> "$tmp/human.in"
    case $((i % 12)) in
        0) m=Jan ;;
        1) m=Feb ;;
        2) m=Mar ;;
        3) m=Apr ;;
        4) m=May ;;
        5) m=Jun ;;
        6) m=Jul ;;
        7) m=Aug ;;
        8) m=Sep ;;
        9) m=Oct ;;
        10) m=Nov ;;
        *) m=Dec ;;
    esac
    printf '%s item-%05d\n' "$m" "$n" >> "$tmp/month.in"
    printf 'pkg-1.%d.%03d%s\n' "$((i % 100))" "$n" "$m" >> "$tmp/version.in"
    printf 'row-%05d group-%03d\n' "$n" "$((i % 997))" >> "$tmp/random.in"
    i=$((i + 1))
done

{
    printf 'kind=special\n'
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

run_case numeric "$tmp/numeric.in" -n
run_case general "$tmp/general.in" -g
run_case human "$tmp/human.in" -h
run_case month "$tmp/month.in" -k1,1M
run_case version "$tmp/version.in" -V
run_case random "$tmp/random.in" -R

printf 'special comparator smoke wrote %s\n' "$out"
