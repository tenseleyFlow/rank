#!/bin/sh
set -eu

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/locale-$stamp.txt"
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

if test -z "$gnu_sort"; then
    printf 'GNU sort oracle not found\n' >&2
    exit 77
fi

tmp=${TMPDIR:-/tmp}/rank-locale-bench-$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

locale_name=C.UTF-8
if ! locale -a 2>/dev/null | grep '^C.UTF-8$' >/dev/null; then
    locale_name=C
fi

whole_in="$tmp/whole.in"
key_in="$tmp/key.in"
mod_in="$tmp/mod.in"
: > "$whole_in"
: > "$key_in"
: > "$mod_in"
i=80000
while test "$i" -gt 0; do
    case $((i % 4)) in
        0) word='éclair' ;;
        1) word='eagle' ;;
        2) word='Éclair' ;;
        *) word='ábaco' ;;
    esac
    printf '%s-%06d\n' "$word" "$i" >> "$whole_in"
    printf 'row-%06d %s-%06d\n' "$i" "$word" "$i" >> "$key_in"
    printf 'item-%06d %s_%06d\n' "$i" "$word" "$((i % 997))" >> "$mod_in"
    i=$((i - 1))
done

{
    printf 'kind=locale\n'
    printf 'locale=%s\n' "$locale_name"
    printf 'rank_version='
    ./rank --version
    printf 'gnu_sort=%s\n' "$gnu_sort"
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

rank_whole_time=$({ /usr/bin/time -p env LC_ALL="$locale_name" ./rank "$whole_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_whole_time=$({ /usr/bin/time -p env LC_ALL="$locale_name" "$gnu_sort" "$whole_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'whole-locale rank_real=%s gnu_real=%s records=80000\n' "$rank_whole_time" "$gnu_whole_time" >> "$out"

rank_key_time=$({ /usr/bin/time -p env LC_ALL="$locale_name" ./rank -k2,2 "$key_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_key_time=$({ /usr/bin/time -p env LC_ALL="$locale_name" "$gnu_sort" -k2,2 "$key_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'key-locale rank_real=%s gnu_real=%s args=-k2,2 records=80000\n' "$rank_key_time" "$gnu_key_time" >> "$out"

rank_mod_time=$({ /usr/bin/time -p env LC_ALL=C ./rank -f -d "$mod_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_mod_time=$({ /usr/bin/time -p env LC_ALL=C "$gnu_sort" -f -d "$mod_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'ascii-fd rank_real=%s gnu_real=%s args=-f -d records=80000\n' "$rank_mod_time" "$gnu_mod_time" >> "$out"

printf 'locale smoke wrote %s\n' "$out"
