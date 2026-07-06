#!/bin/sh
set -eu

src=.docs/refs/gnu-coreutils
dst=build/gnu-sort
make_cmd=${MAKE:-}
bootstrap_path=$PATH
root=$(pwd)

if test -x "$dst/src/sort"; then
    printf '%s/src/sort\n' "$dst"
    exit 0
fi

if test -z "$make_cmd"; then
    if command -v gmake >/dev/null 2>&1; then
        make_cmd=gmake
    else
        make_cmd=make
    fi
fi

mkdir -p build/bootstrap-bin
mkdir -p build/gnu-include
printf '%s\n' '#ifndef RANK_GNU_ORACLE_ALLOCA_H' '#define RANK_GNU_ORACLE_ALLOCA_H' '#include <stdlib.h>' '#endif' > build/gnu-include/alloca.h
if ! command -v wget >/dev/null 2>&1 && command -v curl >/dev/null 2>&1; then
    printf '%s\n' '#!/bin/sh' 'exec curl -L -O "$@"' > build/bootstrap-bin/wget
    chmod +x build/bootstrap-bin/wget
    bootstrap_path=$(pwd)/build/bootstrap-bin:$PATH
fi

if test ! -d "$src"; then
    printf 'GNU coreutils reference tree is missing: %s\n' "$src" >&2
    printf 'Clone or restore .docs/refs/gnu-coreutils before building the pinned reference.\n' >&2
    exit 0
fi

if test ! -x "$src/configure"; then
    if test ! -x "$src/bootstrap"; then
        printf 'GNU coreutils reference tree is incomplete: %s/bootstrap missing\n' "$src" >&2
        exit 0
    fi
    printf 'bootstrapping GNU coreutils reference in %s\n' "$src" >&2
    (cd "$src" && PATH=$bootstrap_path ./bootstrap --no-git --skip-po --gnulib-srcdir=gnulib)
fi

if test ! -d "$dst"; then
    cp -R "$src" "$dst"
fi

(cd "$dst" && CPPFLAGS="${CPPFLAGS:-} -I$root/build/gnu-include" ./configure --quiet --disable-nls && "$make_cmd" -s all || true && "$make_cmd" -s src/sort)
printf '%s/src/sort\n' "$dst"
