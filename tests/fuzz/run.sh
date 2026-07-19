#!/bin/sh
# Differential fuzzer: random inputs x random accepted option sets,
# rank vs GNU sort, with RANK_DEBUG_VERIFY so optimized plans are also
# checked against the scalar oracle. Deterministic per FUZZ_SEED.
set -eu

export LC_ALL=C
export _POSIX2_VERSION=199209

trials=${FUZZ_TRIALS:-100}
seed=${FUZZ_SEED:-42}

gnu_sort=$(sh scripts/find-gnu-sort.sh || true)
if test -z "$gnu_sort"; then
    printf 'fuzz skipped: no GNU coreutils sort found\n'
    exit 77
fi

work=${TMPDIR:-/tmp}/rank-fuzz-$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT HUP INT TERM

gen_input() {
    awk -v seed="$1" 'BEGIN {
        srand(seed)
        lines = int(rand() * 200)
        for (l = 0; l < lines; l++) {
            class = int(rand() * 8)
            len = int(rand() * 40)
            line = ""
            if (class == 0) {
                for (i = 0; i < len; i++) line = line sprintf("%c", 97 + int(rand() * 26))
            } else if (class == 1) {
                if (rand() < 0.3) line = "-"
                line = line int(rand() * 100000)
                if (rand() < 0.4) line = line "." int(rand() * 1000)
            } else if (class == 2) {
                fields = 1 + int(rand() * 4)
                for (f = 0; f < fields; f++) {
                    if (f > 0) line = line (rand() < 0.3 ? "\t" : " ")
                    for (i = 0; i < 1 + int(rand() * 6); i++) line = line sprintf("%c", 97 + int(rand() * 26))
                }
                if (rand() < 0.2) line = "  " line
            } else if (class == 3) {
                for (i = 0; i < len; i++) line = line sprintf("%c", 128 + int(rand() * 128))
            } else if (class == 4) {
                line = ""
            } else if (class == 5) {
                line = "shared-prefix-shared-prefix-" int(rand() * 100)
            } else if (class == 6) {
                line = "pkg-" int(rand() * 10) "." int(rand() * 20) "." int(rand() * 10)
            } else {
                m = int(rand() * 14)
                split("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec foo ", months, " ")
                line = months[m + 1] " " int(rand() * 100)
            }
            print line
        }
    }'
}

gen_args() {
    awk -v seed="$1" 'BEGIN {
        srand(seed)
        args = ""
        mode = int(rand() * 10)
        if (mode == 1) args = args " -n"
        else if (mode == 2) args = args " -g"
        else if (mode == 3) args = args " -h"
        else if (mode == 4) args = args " -M"
        else if (mode == 5) args = args " -V"
        else if (mode == 6) args = args " -R --random-source=tests/fixtures/random/seed-a.bin"
        else if (mode == 7) args = args " -f"
        else if (mode == 8) args = args (rand() < 0.5 ? " -d" : " -i")
        if (rand() < 0.3) args = args " -r"
        if (rand() < 0.3) args = args " -s"
        if (rand() < 0.2) args = args " -u"
        if (rand() < 0.2) args = args " -b"
        sep = ""
        if (rand() < 0.3) {
            sep = (rand() < 0.5 ? "," : ":")
            args = args " -t" sep
        }
        keys = int(rand() * 3)
        for (k = 0; k < keys; k++) {
            def = "" (1 + int(rand() * 4))
            if (rand() < 0.3) def = def "." (1 + int(rand() * 3))
            if (rand() < 0.7) {
                def = def "," (1 + int(rand() * 4))
                if (rand() < 0.3) def = def "." int(rand() * 3)
            }
            kmods = ""
            km = int(rand() * 8)
            if (km == 1) kmods = "n"
            else if (km == 2) kmods = "V"
            else if (km == 3) kmods = "r"
            else if (km == 4) kmods = "b"
            else if (km == 5) kmods = "f"
            args = args " -k" def kmods
        }
        if (rand() < 0.15) args = args " -S 1K"
        if (rand() < 0.15) args = args " --parallel=2"
        print substr(args, 2)
    }'
}

failures=0
trial=0
while test "$trial" -lt "$trials"; do
    in="$work/in"
    gen_input "$((seed + trial))" > "$in"
    args=$(gen_args "$((seed + trial + 100000))")
    rank_status=0
    gnu_status=0

    # shellcheck disable=SC2086
    RANK_DEBUG_VERIFY=1 RANK_PARALLEL_MIN=1 ./rank $args "$in" > "$work/rank.out" 2>"$work/rank.err" || rank_status=$?
    # shellcheck disable=SC2086
    "$gnu_sort" $args "$in" > "$work/gnu.out" 2>"$work/gnu.err" || gnu_status=$?

    if test "$rank_status" -ne "$gnu_status" || ! cmp -s "$work/rank.out" "$work/gnu.out"; then
        mkdir -p tests/fuzz/failures
        cp "$in" "tests/fuzz/failures/trial-$trial.in"
        printf '%s\n' "$args" > "tests/fuzz/failures/trial-$trial.args"
        printf 'fuzz FAIL trial=%s seed=%s args=[%s] rank_status=%s gnu_status=%s\n' \
            "$trial" "$((seed + trial))" "$args" "$rank_status" "$gnu_status" >&2
        cat "$work/rank.err" >&2
        failures=$((failures + 1))
        if test "$failures" -ge 3; then
            exit 1
        fi
    fi
    trial=$((trial + 1))
done

if test "$failures" -gt 0; then
    exit 1
fi
printf 'fuzz ok: %s trials seed=%s\n' "$trials" "$seed"
