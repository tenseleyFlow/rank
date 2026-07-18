#!/bin/sh
set -eu

export LC_ALL=C

mkdir -p bench/results build/bench
gnu_sort=$(sh scripts/find-gnu-sort.sh || sh scripts/build-gnu-sort.sh)
stamp=$(date '+%Y%m%d%H%M%S')
out="bench/results/default-$stamp.txt"

words=build/bench/default-words.txt
sorted=build/bench/default-sorted.txt
prefix=build/bench/default-prefix.txt
tiny=build/bench/default-tiny.txt
shuffled=build/bench/default-shuffled.txt
paths=build/bench/default-paths.txt
urls=build/bench/default-urls.txt
equal=build/bench/default-equal.txt
variable=build/bench/default-variable.txt

: > "$words"
i=100000
while test "$i" -gt 0; do
    printf 'word-%06d\n' "$i" >> "$words"
    i=$((i - 1))
done

: > "$sorted"
i=1
while test "$i" -le 100000; do
    printf 'word-%06d\n' "$i" >> "$sorted"
    i=$((i + 1))
done

: > "$prefix"
i=100000
while test "$i" -gt 0; do
    printf 'common-prefix-common-prefix-%06d\n' "$i" >> "$prefix"
    i=$((i - 1))
done

: > "$tiny"
i=1000
while test "$i" -gt 0; do
    printf 'tiny-%04d\n' "$i" >> "$tiny"
    i=$((i - 1))
done

: > "$shuffled"
i=0
x=17
while test "$i" -lt 100000; do
    x=$(((x * 1103515245 + 12345) & 2147483647))
    printf 'word-%010d\n' "$x" >> "$shuffled"
    i=$((i + 1))
done

: > "$paths"
i=100000
while test "$i" -gt 0; do
    printf '/srv/app/%03d/%03d/%06d.dat\n' $((i % 251)) $((i % 997)) "$i" >> "$paths"
    i=$((i - 1))
done

: > "$urls"
i=100000
while test "$i" -gt 0; do
    printf '2026-07-06T12:%02d:%02dZ GET /api/v1/items/%06d?tenant=%03d status=%03d\n' \
        $((i % 60)) $(((i / 60) % 60)) "$i" $((i % 503)) $((200 + (i % 5))) >> "$urls"
    i=$((i - 1))
done

: > "$equal"
i=0
while test "$i" -lt 100000; do
    case $((i % 10)) in
        0) printf 'alpha\n' >> "$equal" ;;
        1) printf 'beta\n' >> "$equal" ;;
        2) printf 'gamma\n' >> "$equal" ;;
        3) printf 'delta\n' >> "$equal" ;;
        4) printf 'epsilon\n' >> "$equal" ;;
        5) printf 'alpha\n' >> "$equal" ;;
        6) printf 'beta\n' >> "$equal" ;;
        7) printf 'gamma\n' >> "$equal" ;;
        8) printf 'delta\n' >> "$equal" ;;
        *) printf 'epsilon\n' >> "$equal" ;;
    esac
    i=$((i + 1))
done

: > "$variable"
i=100000
while test "$i" -gt 0; do
    case $((i % 6)) in
        0) printf 'x%06d\n' "$i" >> "$variable" ;;
        1) printf 'medium-key-%06d-tail\n' "$i" >> "$variable" ;;
        2) printf 'long-key-common-prefix-%06d-suffix-with-more-bytes\n' "$i" >> "$variable" ;;
        3) printf '%06d\n' "$i" >> "$variable" ;;
        4) printf 'zz/%06d/item/name\n' "$i" >> "$variable" ;;
        *) printf 'a-very-very-long-record-with-shared-front-%06d-and-trailing-data\n' "$i" >> "$variable" ;;
    esac
    i=$((i - 1))
done

{
    printf 'kind=default-radix\n'
    printf 'gnu_sort=%s\n' "$gnu_sort"
    printf 'rank_version='
    ./rank --version
    "$gnu_sort" --version | sed -n '1p' | sed 's/^/gnu_sort_version=/'
} > "$out"

run_pair() {
    name=$1
    file=$2
    rank_out="build/bench/$name.rank.out"
    gnu_out="build/bench/$name.gnu.out"

    ./rank "$file" > "$rank_out"
    "$gnu_sort" "$file" > "$gnu_out"
    cmp "$rank_out" "$gnu_out"

    if command -v hyperfine >/dev/null 2>&1; then
        hyperfine --warmup 2 --runs 5 --export-json "bench/results/default-$name-$stamp.json" \
            "./rank $file" \
            "$gnu_sort $file" >> "$out"
    else
        printf 'hyperfine=missing workload=%s\n' "$name" >> "$out"
    fi
}

run_pair words "$words"
run_pair sorted "$sorted"
run_pair prefix "$prefix"
run_pair shuffled "$shuffled"
run_pair paths "$paths"
run_pair urls "$urls"
run_pair equal "$equal"
run_pair variable "$variable"
run_pair tiny "$tiny"

printf 'default perf wrote %s\n' "$out"
