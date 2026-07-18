#!/bin/sh
set -eu

mkdir -p bench/results
stamp=$(date '+%Y%m%d%H%M%S')
manifest="bench/results/all-$stamp.txt"

scripts='default keyed special check merge external locale'

{
    printf 'kind=all\n'
    printf 'stamp=%s\n' "$stamp"
    printf 'rank_version='
    ./rank --version
} > "$manifest"

for name in $scripts; do
    script="bench/run-$name.sh"
    before=$(ls bench/results 2>/dev/null || true)
    status=0

    "$script" || status=$?
    after=$(ls bench/results 2>/dev/null || true)
    result=$(printf '%s\n%s\n' "$before" "$after" | sort | uniq -u | sed -n "s#^$name-[0-9].*\.txt\$#bench/results/&#p")

    if test "$status" -eq 77; then
        printf '%s status=skipped\n' "$name" >> "$manifest"
        continue
    fi
    if test "$status" -ne 0; then
        printf '%s status=failed exit=%s\n' "$name" "$status" >> "$manifest"
        exit "$status"
    fi
    if test -n "$result"; then
        printf '%s status=ok result=%s\n' "$name" "$result" >> "$manifest"
    else
        printf '%s status=ok result=unknown\n' "$name" >> "$manifest"
    fi
done

printf 'perf suite wrote %s\n' "$manifest"
