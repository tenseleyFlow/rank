#!/bin/sh
set -eu

export LC_ALL=C

mkdir -p bench/results build/bench
gnu_sort=$(sh scripts/find-gnu-sort.sh || sh scripts/build-gnu-sort.sh)
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/keyed-$stamp.txt"

csv=build/bench/keyed-csv.csv
dups=build/bench/keyed-dups.csv
multi=build/bench/keyed-multi.csv
nkey=build/bench/keyed-nkey.csv
tsv=build/bench/keyed-tsv.tsv
logs=build/bench/keyed-logs.txt
paths=build/bench/keyed-paths.txt
urls=build/bench/keyed-urls.txt
prefix=build/bench/keyed-prefix.csv

: > "$csv"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d,%06d,payload-%06d\n' "$i" $((i % 10000)) "$i" >> "$csv"
    i=$((i - 1))
done

: > "$dups"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d,key%03d,payload-%06d\n' "$i" $((i % 100)) "$i" >> "$dups"
    i=$((i - 1))
done

: > "$multi"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d,group%03d,%06d,payload-%06d\n' "$i" $((i % 100)) $((100000 - i)) "$i" >> "$multi"
    i=$((i - 1))
done

: > "$nkey"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d,group%03d,sub%02d,%06d,payload-%06d\n' "$i" $((i % 100)) $((i % 10)) $((100000 - i)) "$i" >> "$nkey"
    i=$((i - 1))
done

: > "$tsv"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d\tgroup%03d\tuser%05d\tpayload-%06d\n' "$i" $((i % 250)) $((100000 - i)) "$i" >> "$tsv"
    i=$((i - 1))
done

: > "$logs"
i=100000
while test "$i" -gt 0; do
    printf '2026-07-06T12:%02d:%02dZ level%01d service%02d tenant%03d request%06d status%03d\n' \
        $((i % 60)) $(((i / 60) % 60)) $((i % 4)) $((i % 32)) $((i % 503)) "$i" $((200 + (i % 5))) >> "$logs"
    i=$((i - 1))
done

: > "$paths"
i=100000
while test "$i" -gt 0; do
    printf '/srv/app/tenant%03d/bucket%03d/object%06d.dat\n' $((i % 251)) $((i % 997)) "$i" >> "$paths"
    i=$((i - 1))
done

: > "$urls"
i=100000
while test "$i" -gt 0; do
    printf 'host%03d.example.test /api/v1/tenant%03d/item%06d status%03d\n' $((i % 89)) $((i % 503)) "$i" $((200 + (i % 5))) >> "$urls"
    i=$((i - 1))
done

: > "$prefix"
i=100000
while test "$i" -gt 0; do
    printf 'row%06d,shared-prefix-shared-prefix-shared-prefix-%06d,payload-%06d\n' "$i" "$i" "$i" >> "$prefix"
    i=$((i - 1))
done

{
    printf 'kind=keyed-radix\n'
    printf 'gnu_sort=%s\n' "$gnu_sort"
    printf 'rank_version='
    ./rank --version
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

run_pair() {
    name=$1
    file=$2
    shift 2
    rank_out="build/bench/$name.rank.out"
    gnu_out="build/bench/$name.gnu.out"

    ./rank "$@" "$file" > "$rank_out"
    "$gnu_sort" "$@" "$file" > "$gnu_out"
    cmp "$rank_out" "$gnu_out"

    if command -v hyperfine >/dev/null 2>&1; then
        hyperfine --warmup 2 --runs 5 --export-json "bench/results/keyed-$name-$stamp.json" \
            "./rank $* $file" \
            "$gnu_sort $* $file" >> "$out"
    else
        printf 'hyperfine=missing workload=%s\n' "$name" >> "$out"
    fi
}

run_pair csv-stable "$csv" -s -t, -k2,2
run_pair csv "$csv" -t, -k2,2
run_pair dups-stable "$dups" -s -t, -k2,2
run_pair dups "$dups" -t, -k2,2
run_pair multi-stable "$multi" -s -t, -k2,2 -k3,3
run_pair nkey-stable "$nkey" -s -t, -k2,2 -k3,3 -k4,4
run_pair nkey-stable-reverse "$nkey" -s -r -t, -k2,2 -k3,3 -k4,4
run_pair nkey-stable-mixed-reverse "$nkey" -s -t, -k2,2 -k3,3r -k4,4
run_pair dups-unique "$dups" -u -t, -k2,2
run_pair dups-unique-reverse "$dups" -u -r -t, -k2,2
run_pair nkey-unique "$nkey" -u -t, -k2,2 -k3,3 -k4,4
run_pair tsv-multi "$tsv" -s -k2,2 -k3,3
run_pair logs-service "$logs" -s -k4,4
run_pair logs-tenant-request "$logs" -s -k5,5 -k6,6
run_pair paths-components "$paths" -s -t/ -k4,4 -k5,5
run_pair urls-path "$urls" -s -k2,2
run_pair prefix-key "$prefix" -s -t, -k2,2

printf 'keyed perf wrote %s\n' "$out"
