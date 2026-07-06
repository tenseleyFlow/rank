#!/bin/sh
set -eu

src=.docs/refs/gnu-coreutils
dst=build/gnu-sort

if test ! -d "$src"; then
    printf 'GNU coreutils reference tree is missing: %s\n' "$src" >&2
    printf 'Clone or restore .docs/refs/gnu-coreutils before building the pinned reference.\n' >&2
    exit 0
fi

if test ! -x "$src/configure"; then
    printf 'GNU coreutils reference tree is incomplete or not bootstrapped: %s/configure missing\n' "$src" >&2
    exit 0
fi

mkdir -p build
if test ! -d "$dst"; then
    cp -R "$src" "$dst"
fi

(cd "$dst" && ./configure --quiet && ${MAKE:-make} -s src/sort)
printf '%s/src/sort\n' "$dst"
