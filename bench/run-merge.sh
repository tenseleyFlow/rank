#!/bin/sh
set -eu

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/merge-$stamp.txt"
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

if test -z "$gnu_sort"; then
    printf 'GNU sort oracle not found\n' >&2
    exit 77
fi

tmp=${TMPDIR:-/tmp}/rank-merge-bench-$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

r=0
files=
key_files=
while test "$r" -lt 16; do
    file="$tmp/run-$r.in"
    key_file="$tmp/key-run-$r.in"
    files="$files $file"
    key_files="$key_files $key_file"
    : > "$file"
    : > "$key_file"
    i="$r"
    while test "$i" -lt 160000; do
        printf '%06d\n' "$i" >> "$file"
        printf 'row-%06d %06d\n' "$i" "$i" >> "$key_file"
        i=$((i + 16))
    done
    r=$((r + 1))
done

{
    printf 'kind=merge\n'
    printf 'rank_version='
    ./rank --version
    printf 'gnu_sort=%s\n' "$gnu_sort"
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

rank_time=$({ /usr/bin/time -p ./rank -m $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_time=$({ /usr/bin/time -p "$gnu_sort" -m $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'many-runs rank_real=%s gnu_real=%s args=-m files=16 records=160000\n' "$rank_time" "$gnu_time" >> "$out"

rank_unique_time=$({ /usr/bin/time -p ./rank -mu $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_unique_time=$({ /usr/bin/time -p "$gnu_sort" -mu $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'many-runs-unique rank_real=%s gnu_real=%s args=-mu files=16 records=160000\n' "$rank_unique_time" "$gnu_unique_time" >> "$out"

rank_key_time=$({ /usr/bin/time -p ./rank -m -k2,2n $key_files >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_key_time=$({ /usr/bin/time -p "$gnu_sort" -m -k2,2n $key_files >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'many-runs-keyed-numeric rank_real=%s gnu_real=%s args=-m -k2,2n files=16 records=160000\n' "$rank_key_time" "$gnu_key_time" >> "$out"

rank_version_time=$({ /usr/bin/time -p ./rank -m -V $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_version_time=$({ /usr/bin/time -p "$gnu_sort" -m -V $files >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'many-runs-version rank_real=%s gnu_real=%s args=-m -V files=16 records=160000\n' "$rank_version_time" "$gnu_version_time" >> "$out"

rank_key_version_time=$({ /usr/bin/time -p ./rank -m -k2,2V $key_files >/dev/null; } 2>&1 | sed -n 's/^real //p')
gnu_key_version_time=$({ /usr/bin/time -p "$gnu_sort" -m -k2,2V $key_files >/dev/null; } 2>&1 | sed -n 's/^real //p')
printf 'many-runs-key-version rank_real=%s gnu_real=%s args=-m -k2,2V files=16 records=160000\n' "$rank_key_version_time" "$gnu_key_version_time" >> "$out"

printf 'merge smoke wrote %s\n' "$out"
