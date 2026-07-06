#!/bin/sh
set -eu

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/smoke-$stamp.txt"
host=$(hostname 2>/dev/null || printf unknown)
os=$(uname -srm 2>/dev/null || printf unknown)
cc=$(${CC:-cc} --version 2>/dev/null | sed -n '1p' || printf unknown)
gnu_sort=$(sh scripts/find-gnu-sort.sh || true)

{
    printf 'kind=smoke\n'
    printf 'host=%s\n' "$host"
    printf 'os=%s\n' "$os"
    printf 'cc=%s\n' "$cc"
    printf 'locale=%s\n' "${LC_ALL:-${LANG:-unknown}}"
    printf 'rank_version='
    ./rank --version
    if test -n "$gnu_sort"; then
        printf 'gnu_sort=%s\n' "$gnu_sort"
        "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
    else
        printf 'gnu_sort=missing\n'
    fi
    printf 'command=./rank --version\n'
} > "$out"

if command -v hyperfine >/dev/null 2>&1; then
    hyperfine --warmup 1 --runs 1 --export-json "bench/results/smoke-$stamp.json" './rank --version' >/dev/null
    printf 'hyperfine_json=bench/results/smoke-%s.json\n' "$stamp" >> "$out"
else
    start=$(date '+%s')
    ./rank --version >/dev/null
    end=$(date '+%s')
    printf 'elapsed_seconds=%s\n' "$((end - start))" >> "$out"
fi

printf 'perf smoke wrote %s\n' "$out"
