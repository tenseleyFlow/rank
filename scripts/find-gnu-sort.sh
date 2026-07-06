#!/bin/sh
set -eu

is_gnu_sort() {
    candidate=$1
    "$candidate" --version 2>/dev/null | grep 'GNU coreutils' >/dev/null 2>&1
}

for candidate in ./build/gnu-sort/src/sort gsort sort; do
    if command -v "$candidate" >/dev/null 2>&1 || test -x "$candidate"; then
        if is_gnu_sort "$candidate"; then
            printf '%s\n' "$candidate"
            exit 0
        fi
    fi
done

exit 1
