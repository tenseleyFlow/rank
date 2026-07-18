#!/bin/sh
set -eu

export LC_ALL=C

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/external-$stamp.txt"
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

if test -z "$gnu_sort"; then
    printf 'GNU sort oracle not found\n' >&2
    exit 77
fi

tmp=${TMPDIR:-/tmp}/rank-external-bench-$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

byte_in="$tmp/byte.in"
key_in="$tmp/key.in"
: > "$byte_in"
: > "$key_in"
i=120000
while test "$i" -gt 0; do
    printf 'path/%06d/item/%06d\n' "$i" "$((i % 997))" >> "$byte_in"
    printf 'row-%06d %06d\n' "$i" "$((i % 997))" >> "$key_in"
    i=$((i - 1))
done

{
    printf 'kind=external\n'
    printf 'rank_version='
    ./rank --version
    printf 'gnu_sort=%s\n' "$gnu_sort"
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

rank_byte_time=$({ /usr/bin/time -p ./rank -S 64K -T "$tmp" "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_byte_time=$({ /usr/bin/time -p "$gnu_sort" -S 64K -T "$tmp" "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'byte rank_real=%s gnu_real=%s args=-S 64K records=120000\n' "$rank_byte_time" "$gnu_byte_time" >> "$out"

rank_key_time=$({ /usr/bin/time -p ./rank -S 64K -T "$tmp" -k2,2n "$key_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_key_time=$({ /usr/bin/time -p "$gnu_sort" -S 64K -T "$tmp" -k2,2n "$key_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'keyed-numeric rank_real=%s gnu_real=%s args=-S 64K -k2,2n records=120000\n' "$rank_key_time" "$gnu_key_time" >> "$out"

rank_batch_time=$({ /usr/bin/time -p ./rank -S 8K -T "$tmp" --batch-size=2 "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_batch_time=$({ /usr/bin/time -p "$gnu_sort" -S 8K -T "$tmp" --batch-size=2 "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'batch-size rank_real=%s gnu_real=%s args=-S 8K --batch-size=2 records=120000\n' "$rank_batch_time" "$gnu_batch_time" >> "$out"

compressor=$(command -v gzip || true)
if test -n "$compressor"; then
    rank_compress_time=$({ /usr/bin/time -p ./rank -S 8K -T "$tmp" --batch-size=2 --compress-program="$compressor" "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
    gnu_compress_time=$({ /usr/bin/time -p "$gnu_sort" -S 8K -T "$tmp" --batch-size=2 --compress-program="$compressor" "$byte_in" >/dev/null; } 2>&1 | sed -n 's/^real //p')
    printf 'compress rank_real=%s gnu_real=%s args=-S 8K --batch-size=2 --compress-program=gzip records=120000\n' "$rank_compress_time" "$gnu_compress_time" >> "$out"
fi

printf 'external smoke wrote %s\n' "$out"
