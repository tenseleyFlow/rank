#!/bin/sh
set -eu

export LC_ALL=C

gnu_sort=$(sh scripts/find-gnu-sort.sh || true)
if test -n "$gnu_sort"; then
    tmp_in=${TMPDIR:-/tmp}/rank-golden-in.$$
    tmp_a=${TMPDIR:-/tmp}/rank-golden-a.$$
    tmp_b=${TMPDIR:-/tmp}/rank-golden-b.$$
    printf 'b\na\n' > "$tmp_in"
    "$gnu_sort" "$tmp_in" > "$tmp_a"
    "$gnu_sort" "$tmp_in" > "$tmp_b"
    cmp "$tmp_a" "$tmp_b"
    rm -f "$tmp_in" "$tmp_a" "$tmp_b"
    printf 'gnu sort self-test ok: %s\n' "$gnu_sort"
else
    printf 'gnu sort self-test skipped: no GNU coreutils sort found\n'
fi

run_case() {
    name=$1
    input=$2
    shift 2
    in=${TMPDIR:-/tmp}/rank-$name-in.$$
    got=${TMPDIR:-/tmp}/rank-$name-got.$$
    want=${TMPDIR:-/tmp}/rank-$name-want.$$

    printf '%s' "$input" > "$in"
    ./rank "$@" "$in" > "$got"
    if test -n "$gnu_sort"; then
        "$gnu_sort" "$@" "$in" > "$want"
    else
        sort "$@" "$in" > "$want" 2>/dev/null || ./rank "$@" "$in" > "$want"
    fi
    cmp "$got" "$want"
    rm -f "$in" "$got" "$want"
}

run_check_case() {
    name=$1
    input=$2
    shift 2
    in=${TMPDIR:-/tmp}/rank-$name-in.$$
    got_out=${TMPDIR:-/tmp}/rank-$name-got.out.$$
    got_err=${TMPDIR:-/tmp}/rank-$name-got.err.$$
    want_out=${TMPDIR:-/tmp}/rank-$name-want.out.$$
    want_err=${TMPDIR:-/tmp}/rank-$name-want.err.$$
    got_status=0
    want_status=0

    printf '%s' "$input" > "$in"
    ./rank "$@" "$in" > "$got_out" 2>"$got_err" || got_status=$?
    if test -n "$gnu_sort"; then
        "$gnu_sort" "$@" "$in" > "$want_out" 2>"$want_err" || want_status=$?
        sed 's/^[^:]*sort:/rank:/' "$want_err" > "$want_err.norm"
        mv "$want_err.norm" "$want_err"
    else
        sort "$@" "$in" > "$want_out" 2>"$want_err" || want_status=$?
        sed 's/^[^:]*sort:/rank:/' "$want_err" > "$want_err.norm"
        mv "$want_err.norm" "$want_err"
    fi
    test "$got_status" -eq "$want_status"
    cmp "$got_out" "$want_out"
    cmp "$got_err" "$want_err"
    rm -f "$in" "$got_out" "$got_err" "$want_out" "$want_err"
}

run_merge_case() {
    name=$1
    input_a=$2
    input_b=$3
    shift 3
    in_a=${TMPDIR:-/tmp}/rank-$name-a.$$
    in_b=${TMPDIR:-/tmp}/rank-$name-b.$$
    got=${TMPDIR:-/tmp}/rank-$name-got.$$
    want=${TMPDIR:-/tmp}/rank-$name-want.$$

    printf '%b' "$input_a" > "$in_a"
    printf '%b' "$input_b" > "$in_b"
    ./rank -m "$@" "$in_a" "$in_b" > "$got"
    if test -n "$gnu_sort"; then
        "$gnu_sort" -m "$@" "$in_a" "$in_b" > "$want"
    else
        sort -m "$@" "$in_a" "$in_b" > "$want"
    fi
    cmp "$got" "$want"
    rm -f "$in_a" "$in_b" "$got" "$want"
}

locale_available() {
    alt=$(printf '%s' "$1" | sed 's/UTF-8/utf8/')
    locale -a 2>/dev/null | grep -Fx -e "$1" -e "$alt" >/dev/null
}

run_locale_case() {
    locale_name=$1
    name=$2
    input=$3
    shift 3
    in=${TMPDIR:-/tmp}/rank-locale-$name-in.$$
    got=${TMPDIR:-/tmp}/rank-locale-$name-got.$$
    want=${TMPDIR:-/tmp}/rank-locale-$name-want.$$

    printf '%b' "$input" > "$in"
    LC_ALL=$locale_name ./rank "$@" "$in" > "$got"
    if test -n "$gnu_sort"; then
        LC_ALL=$locale_name "$gnu_sort" "$@" "$in" > "$want"
    else
        LC_ALL=$locale_name sort "$@" "$in" > "$want" 2>/dev/null || LC_ALL=$locale_name ./rank "$@" "$in" > "$want"
    fi
    cmp "$got" "$want"
    rm -f "$in" "$got" "$want"
}

run_case default 'b\na\n'
run_case reverse 'a\nb\n' -r
run_case unique 'b\na\nb\n' -u
run_case radix-prefix 'aaa\naa\na\n'
run_case radix-long-prefix 'prefix-0002\nprefix-0001\nprefix-0003\n'
run_case radix-duplicates 'b\na\nb\na\n' -u
if locale_available C.UTF-8; then
    run_locale_case C.UTF-8 cutf8-whole 'éclair\neagle\nÉclair\nábaco\n'
    run_locale_case C.UTF-8 cutf8-key 'x éclair\ny eagle\nz Éclair\nw ábaco\n' -k2,2
    run_locale_case C.UTF-8 cutf8-nul 'b\000z\nb\000a\na\000z\nb\nab\n'
fi
if locale_available en_US.UTF-8; then
    run_locale_case en_US.UTF-8 en-us-whole 'éclair\neagle\nÉclair\nábaco\n'
    run_locale_case en_US.UTF-8 en-us-key 'x éclair\ny eagle\nz Éclair\nw ábaco\n' -k2,2
    run_locale_case en_US.UTF-8 en-us-nul 'b\000z\nb\000a\na\000z\nb\nab\n'
    run_locale_case en_US.UTF-8 en-us-nul-eclair 'éclair\000z\néclair\000a\neagle\000z\néclair\n'
    run_locale_case en_US.UTF-8 en-us-random 'éclair\neagle\nÉclair\nábaco\ndelta\n' -R --random-source=tests/fixtures/random/seed-a.bin
fi
run_case missing-newline 'b\na'
run_case empty ''
run_case single 'only\n'
run_case equal 'x\nx\nx\n'
run_case already-sorted 'a\nb\nc\n'
run_case reverse-sorted 'c\nb\na\n'
run_case key-second 'z 2\na 1\n' -k2,2
run_case key-char 'xa\nyb\n' -k1.2,1.2
run_case key-open 'p z\np a\n' -k2
run_case key-end-zero 'ab\naa\n' -k1.1,1.0
run_case key-missing-field 'b\na x\n' -k2,2
run_case key-stable 'b 1\na 1\n' -s -k2,2
run_case key-unique 'b 1\na 1\nc 2\n' -u -k2,2
run_case key-unique-first-survivor 'z a\na a\nb b\n' -u -k2,2
run_case key-unique-reverse 'z a\na a\nb b\n' -u -r -k2,2
run_case key-unique-multi 'x a 1\ny a 1\nz a 2\nw b 1\n' -u -k2,2 -k3,3
run_case key-multi 'b 2\na 2\nc 1\n' -k2,2 -k1,1
run_case key-reverse 'a 1\nb 2\n' -k2,2r
run_case key-global-reverse 'a 1\nb 2\n' -r -k2,2
run_case key-last-resort 'b 1\na 1\nc 2\n' -k2,2
run_case key-last-resort-reverse 'b 1\na 1\nc 2\n' -r -k2,2
run_case key-mixed-reverse-last-resort 'b 1\na 1\nc 2\n' -r -k2,2r
run_case key-multi-nonstable 'b 2 1\na 2 1\nc 1 2\n' -k2,2 -k3,3
run_case key-multi-last-resort 'b 1 x\na 1 x\nc 2 y\n' -k2,2 -k3,3
run_case key-multi-global-reverse 'b 1 x\na 1 x\nc 2 y\n' -r -k2,2 -k3,3
run_case key-multi-mixed-reverse 'b 1 x\na 1 x\nc 2 y\n' -r -k2,2 -k3,3r
run_case ignore-case 'b\nA\na\nB\n' -f
run_case ignore-case-last-resort 'a\nA\nb\nB\n' -f
run_case ignore-case-reverse 'a\nA\nb\nB\n' -fr
run_case ignore-case-unique 'a\nA\nb\nB\n' -fu
run_case ignore-case-reverse-unique 'a\nA\nb\nB\n' -fru
run_case ignore-case-reverse-unique-upper-first 'A\na\nB\nb\n' -fru
run_case dictionary-reverse-unique 'a-b\nab\na b\naa\n' -dru
run_case nonprinting-reverse-unique 'a\001b\nab\na\177b\naa\n' -iru
run_case dictionary-order 'a-b\nab\na b\naa\n' -d
run_case ignore-nonprinting 'a\001b\nab\na\177b\naa\n' -i
run_case key-ignore-case 'x b\ny A\nz a\nw B\n' -k2,2f
run_case key-ignore-case-stable 'x b\ny A\nz a\nw B\n' -s -k2,2f
run_case key-dictionary-order 'x a-b\ny ab\nz a b\nw aa\n' -k2,2d
run_case key-ignore-nonprinting 'x a\001b\ny ab\nz a\177b\nw aa\n' -k2,2i
run_case numeric-basic '10\n2\n-1\n1.5\n' -n
run_case numeric-sort-word '10\n2\n-1\n1.5\n' --sort=numeric
run_case numeric-zeros '001\n1.0\n0.9\n000\n' -n
run_case numeric-blanks '  3\n\t2\n1\n' -n
run_case numeric-nonnumbers 'x\n-2\n1\ny\n' -n
run_case numeric-decimals '1.10\n1.02\n1.2\n1\n' -n
run_case numeric-signs '+2\n-0\n+0\n-2\n' -n
run_case numeric-bare-decimals '.5\n-.7\n0.2\n' -n
run_case numeric-trailing-dot '2.\n1.\n1.5\n' -n
run_case numeric-equal-last-resort '01\n1\n1.0\n' -n
run_case numeric-key 'a 10\nb 2\nc -1\n' -k2,2n
run_case numeric-global-key 'a 10\nb 2\nc -1\n' -n -k2,2
run_case numeric-reverse '10\n2\n-1\n1.5\n' -n -r
run_case numeric-unique 'a 1\nb 1.0\nc 2\n' -u -k2,2n
run_case general-numeric-basic '1e2\n10\n-inf\ninf\nnan\nx\n' -g
run_case general-numeric-sort-word '1e2\n10\n-inf\ninf\nnan\nx\n' --sort=general-numeric
run_case general-numeric-sort-short '1e2\n10\n-inf\ninf\nnan\nx\n' --sort=g
run_case general-numeric-nans '-nan\nnan\n+nan\nNaN\n' -g
run_case general-numeric-hex '0x10\n15\n0x1p4\n16\n' -g
run_case general-numeric-overflow '1e9999\n1e-9999\n-1e9999\n0\n' -g
run_case general-numeric-prefix 'abc\n12abc\n12\nabc12\n-x\n' -g
run_case general-numeric-key 'a 1e2\nb 10\nc -inf\nd inf\ne nan\nf x\n' -k2,2g
run_case general-numeric-reverse '1e2\n10\n-inf\ninf\nnan\nx\n' -g -r
run_case human-numeric-basic '2K\n100\n1K\n1M\n999K\n' -h
run_case human-numeric-sign '-1K\n0\n1\n-2\n' -h
run_case human-numeric-suffix-variants '1k\n1K\n1Ki\n1M\n1Mi\n' -h
run_case human-numeric-units '1\n1K\n1M\n1G\n1T\n1P\n1E\n1Z\n1Y\n1R\n1Q\n' -h
run_case human-numeric-kinds '1024\n1K\n1Ki\n1KB\n1Kib\n1k\n1kB\n' -h
run_case human-numeric-decimals '1.5K\n1536\n1.4K\n2K\n1.5M\n' -h
run_case human-numeric-unknown-suffix '1B\n1b\n1Q\n1R\n1\n2\n' -h
run_case human-numeric-rq-lowercase '1r\n1R\n1q\n1Q\n1Y\n' -h
run_case human-numeric-invalid 'x\n1K\nnan\n0\n' -h
run_case human-numeric-key 'a 2K\nb 100\nc 1K\nd 1M\n' -k2,2h
run_case human-numeric-sort-word '2K\n100\n1K\n' --sort=human-numeric
run_case month-basic 'Jan\nDec\nFeb\nfoo\njan\nJanuary\n' -M
run_case month-case 'march\nMar\nMAR\nmaRch\nMay\nmay\n' -M
run_case month-key 'x Jan\nx Dec\nx foo\nx Feb\n' -k2,2M
run_case month-reverse 'Jan\nDec\nFeb\nfoo\n' -M -r
run_case month-key-reverse 'x Jan\nx Dec\nx foo\nx Feb\n' -r -k2,2M
run_case month-unique-key 'x Jan\ny Jan\nz Feb\nw foo\nv foo\n' -u -k2,2M
run_case month-sort-word 'Jan\nDec\nFeb\nfoo\n' --sort=month
run_case version-basic 'v1.10\nv1.2\nv1.02\nv1.0\nv1\n' -V
run_case version-digits 'file9\nfile10\nfile02\nfile1\n' -V
run_case version-punct 'a-1\na.1\na~1\na1\n' -V
run_case version-leading-zeros 'a001\na01\na1\na0001\na0\n' -V
run_case version-release '1.0a\n1.0~rc1\n1.0\n1.0-rc1\n1.0_rc1\n' -V
run_case version-case 'a\nA\nb\nB\na1\nA1\n' -V
run_case version-empty-components '1..2\n1.0.2\n1.0\n1.\n1\n' -V
run_case version-alnum-punct 'a1\naa\na-\na.\n' -V
run_case version-tilde-chain '1.0~~\n1.0~\n1.0\n1.0~~a\n1.0~a\n' -V
run_case version-punct-context '1+git\n1.git\n1-git\n1_git\n1git\n' -V
run_case version-big-digits '1.0000000000000000002\n1.10\n1.2\n1.00000000000000000010\n' -V
run_case version-leading-punct '.1\n0.1\n..1\n-1\n_1\n' -V
run_case version-dot-special '.\n..\n.a\na\n' -V
run_case version-dot-spectrum '.0\n.9\n.A\n.Z\n.a~\n.a\n.b~\n.b\n.zz~\n.zz\n' -V
run_case version-suffix-pass 'a\na.tar\na.1\na~rc\n' -V
run_case version-file-suffixes 'gcc-c++-10.fc9.tar.gz\ngcc-c++-10.fc9.tar.gz.~1~\ngcc-c++-10.fc9.tar.gz.~2~\ngcc-c++-10.8.12-0.7rc2.fc9.tar.bz2\ngcc-c++-10.8.12-0.7rc2.fc9.tar.bz2.~1~\n' -V
run_case version-debianish 'pkg-1.0-1\npkg-1.0-01\npkg-1.0-001\npkg-1.0-1a\n' -V
run_case version-suffix-restore '1.0.tar\n1.0\n1.0a\n1.0~rc1\n1.0-rc1\n' -V
run_case version-key 'pkg v1.10\npkg v1.2\npkg v1.0\n' -k2,2V
run_case version-sort-word 'v1.10\nv1.2\nv1.0\n' --sort=version
run_case key-blanks 'x   b\ny   a\n' -b -k2,2
run_case key-blanks-late 'x   b\ny   a\n' -k2,2 -b
run_case obsolete-key 'b 1\na 2\n' +1
run_case obsolete-key-range 'z 2\na 1\n' +0 -1
run_case random-seeded 'delta\nalpha\nbravo\ncharlie\necho\nfox\n' -R --random-source=tests/fixtures/random/seed-a.bin
run_case random-seeded-b 'delta\nalpha\nbravo\ncharlie\necho\nfox\n' -R --random-source=tests/fixtures/random/seed-b.bin
run_case random-seeded-reverse 'delta\nalpha\nbravo\ncharlie\n' -R -r --random-source=tests/fixtures/random/seed-a.bin
run_case random-seeded-unique 'b\na\nb\nc\na\n' -R -u --random-source=tests/fixtures/random/seed-a.bin
run_case random-seeded-key 'x b\ny a\nz c\nw a\n' -k2,2R --random-source=tests/fixtures/random/seed-a.bin
run_case random-seeded-fold 'a\nA\nb\nB\nc\n' -R -f --random-source=tests/fixtures/random/seed-a.bin
run_check_case check-sorted 'a\nb\nc\n' -c
run_check_case check-unsorted 'b\na\n' -c
run_check_case check-quiet 'b\na\n' -C
run_check_case check-diagnose-first 'b\na\n' --check=diagnose-first
run_check_case check-silent 'b\na\n' --check=silent
run_check_case check-reverse 'c\nb\na\n' -cr
run_check_case check-key 'x 1\ny 2\nz 3\n' -c -k2,2n
run_check_case check-key-disorder 'x 2\ny 1\n' -c -k2,2n
run_check_case check-unique-dupe 'a\na\n' -cu
run_check_case check-unique-key-dupe 'x 1\ny 1\n' -cu -k2,2n
run_check_case check-version 'pkg-1.0\npkg-1.1\npkg-1.10\n' -cV
run_merge_case merge-basic 'a\nc\n' 'b\nd\n'
run_merge_case merge-missing-newline 'a\nc' 'b\nd'
run_merge_case merge-empty-record '\na\n' '\nb\n'
run_merge_case merge-reverse 'd\nb\n' 'c\na\n' -r
run_merge_case merge-unique 'a\nc\n' 'a\nb\nc\n' -u
run_merge_case merge-stable 'a 2\na 1\n' 'a 3\nb 1\n' -s -k1,1
run_merge_case merge-unique-key 'x 1\nz 2\n' 'y 1\nw 3\n' -u -k2,2n
run_merge_case merge-key-numeric 'x 1\nx 3\n' 'x 2\nx 4\n' -k2,2n
run_merge_case merge-numeric '1\n3\n' '2\n4\n' -n
run_merge_case merge-general-numeric '1e1\n3e1\n' '2e1\n4e1\n' -g
run_merge_case merge-human '1K\n3K\n' '2K\n4K\n' -h
run_merge_case merge-month 'Jan\nMar\n' 'Feb\nApr\n' -M
run_merge_case merge-version 'pkg-1.0\npkg-1.10\n' 'pkg-1.2\npkg-2.0\n' -V
run_merge_case merge-key-general 'x 1e1\nx 3e1\n' 'x 2e1\nx 4e1\n' -k2,2g
run_merge_case merge-key-human 'x 1K\nx 3K\n' 'x 2K\nx 4K\n' -k2,2h
run_merge_case merge-key-month 'x Jan\nx Mar\n' 'x Feb\nx Apr\n' -k2,2M
run_merge_case merge-key-version 'x pkg-1.0\nx pkg-1.10\n' 'x pkg-1.2\nx pkg-2.0\n' -k2,2V

merge_output_a=${TMPDIR:-/tmp}/rank-merge-output-a.$$
merge_output_b=${TMPDIR:-/tmp}/rank-merge-output-b.$$
merge_output_got=${TMPDIR:-/tmp}/rank-merge-output-got.$$
merge_output_want=${TMPDIR:-/tmp}/rank-merge-output-want.$$
printf 'a\nc\n' > "$merge_output_a"
printf 'b\nd\n' > "$merge_output_b"
./rank -m -o "$merge_output_got" "$merge_output_a" "$merge_output_b"
if test -n "$gnu_sort"; then
    "$gnu_sort" -m "$merge_output_a" "$merge_output_b" > "$merge_output_want"
else
    sort -m "$merge_output_a" "$merge_output_b" > "$merge_output_want"
fi
cmp "$merge_output_got" "$merge_output_want"
rm -f "$merge_output_a" "$merge_output_b" "$merge_output_got" "$merge_output_want"

merge_many_a=${TMPDIR:-/tmp}/rank-merge-many-a.$$
merge_many_b=${TMPDIR:-/tmp}/rank-merge-many-b.$$
merge_many_c=${TMPDIR:-/tmp}/rank-merge-many-c.$$
merge_many_got=${TMPDIR:-/tmp}/rank-merge-many-got.$$
merge_many_want=${TMPDIR:-/tmp}/rank-merge-many-want.$$
printf 'a\nd\n' > "$merge_many_a"
printf 'b\ne\n' > "$merge_many_b"
printf 'c\nf\n' > "$merge_many_c"
./rank -m "$merge_many_a" "$merge_many_b" "$merge_many_c" > "$merge_many_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" -m "$merge_many_a" "$merge_many_b" "$merge_many_c" > "$merge_many_want"
else
    sort -m "$merge_many_a" "$merge_many_b" "$merge_many_c" > "$merge_many_want"
fi
cmp "$merge_many_got" "$merge_many_want"
rm -f "$merge_many_a" "$merge_many_b" "$merge_many_c" "$merge_many_got" "$merge_many_want"

merge_stdin_file=${TMPDIR:-/tmp}/rank-merge-stdin-file.$$
merge_stdin_got=${TMPDIR:-/tmp}/rank-merge-stdin-got.$$
merge_stdin_want=${TMPDIR:-/tmp}/rank-merge-stdin-want.$$
printf 'b\nd\n' > "$merge_stdin_file"
printf 'a\nc\n' | ./rank -m - "$merge_stdin_file" > "$merge_stdin_got"
if test -n "$gnu_sort"; then
    printf 'a\nc\n' | "$gnu_sort" -m - "$merge_stdin_file" > "$merge_stdin_want"
else
    printf 'a\nc\n' | sort -m - "$merge_stdin_file" > "$merge_stdin_want"
fi
cmp "$merge_stdin_got" "$merge_stdin_want"
rm -f "$merge_stdin_file" "$merge_stdin_got" "$merge_stdin_want"

merge_z_a=${TMPDIR:-/tmp}/rank-merge-z-a.$$
merge_z_b=${TMPDIR:-/tmp}/rank-merge-z-b.$$
merge_z_got=${TMPDIR:-/tmp}/rank-merge-z-got.$$
merge_z_want=${TMPDIR:-/tmp}/rank-merge-z-want.$$
printf 'a\000c\000' > "$merge_z_a"
printf 'b\000d\000' > "$merge_z_b"
./rank -mz "$merge_z_a" "$merge_z_b" > "$merge_z_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" -mz "$merge_z_a" "$merge_z_b" > "$merge_z_want"
else
    sort -mz "$merge_z_a" "$merge_z_b" > "$merge_z_want"
fi
cmp "$merge_z_got" "$merge_z_want"
rm -f "$merge_z_a" "$merge_z_b" "$merge_z_got" "$merge_z_want"

external_tmp=${TMPDIR:-/tmp}/rank-external-tmp.$$
mkdir "$external_tmp"
external_in=${TMPDIR:-/tmp}/rank-external-in.$$
external_got=${TMPDIR:-/tmp}/rank-external-got.$$
external_want=${TMPDIR:-/tmp}/rank-external-want.$$
: > "$external_in"
i=200
while test "$i" -gt 0; do
    printf '%03d\n' "$i" >> "$external_in"
    i=$((i - 1))
done
./rank -S 128 -T "$external_tmp" "$external_in" > "$external_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" "$external_in" > "$external_want"
else
    sort "$external_in" > "$external_want"
fi
cmp "$external_got" "$external_want"
test -z "$(ls -A "$external_tmp")"
rm -f "$external_in" "$external_got" "$external_want"
rmdir "$external_tmp"

external_key_in=${TMPDIR:-/tmp}/rank-external-key-in.$$
external_key_got=${TMPDIR:-/tmp}/rank-external-key-got.$$
external_key_want=${TMPDIR:-/tmp}/rank-external-key-want.$$
printf 'x 3\ny 1\nz 2\nw 1\n' > "$external_key_in"
./rank -S 8 -k2,2n "$external_key_in" > "$external_key_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" -k2,2n "$external_key_in" > "$external_key_want"
else
    sort -k2,2n "$external_key_in" > "$external_key_want"
fi
cmp "$external_key_got" "$external_key_want"
rm -f "$external_key_in" "$external_key_got" "$external_key_want"

external_unique_in=${TMPDIR:-/tmp}/rank-external-unique-in.$$
external_unique_got=${TMPDIR:-/tmp}/rank-external-unique-got.$$
external_unique_want=${TMPDIR:-/tmp}/rank-external-unique-want.$$
printf 'b\na\nb\na\nc\n' > "$external_unique_in"
./rank -S 4 -u "$external_unique_in" > "$external_unique_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" -u "$external_unique_in" > "$external_unique_want"
else
    sort -u "$external_unique_in" > "$external_unique_want"
fi
cmp "$external_unique_got" "$external_unique_want"
rm -f "$external_unique_in" "$external_unique_got" "$external_unique_want"

external_o_in=${TMPDIR:-/tmp}/rank-external-o-in.$$
external_o_want=${TMPDIR:-/tmp}/rank-external-o-want.$$
printf 'b\na\nc\n' > "$external_o_in"
if test -n "$gnu_sort"; then
    "$gnu_sort" "$external_o_in" > "$external_o_want"
else
    sort "$external_o_in" > "$external_o_want"
fi
./rank -S 2 -o "$external_o_in" "$external_o_in"
cmp "$external_o_in" "$external_o_want"
rm -f "$external_o_in" "$external_o_want"

external_z_in=${TMPDIR:-/tmp}/rank-external-z-in.$$
external_z_got=${TMPDIR:-/tmp}/rank-external-z-got.$$
external_z_want=${TMPDIR:-/tmp}/rank-external-z-want.$$
printf 'b\000a\000c\000' > "$external_z_in"
./rank -S 2 -z "$external_z_in" > "$external_z_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" -z "$external_z_in" > "$external_z_want"
else
    sort -z "$external_z_in" > "$external_z_want"
fi
cmp "$external_z_got" "$external_z_want"
rm -f "$external_z_in" "$external_z_got" "$external_z_want"

external_empty_in=${TMPDIR:-/tmp}/rank-external-empty-in.$$
external_empty_out=${TMPDIR:-/tmp}/rank-external-empty-out.$$
: > "$external_empty_in"
./rank -S 1 -o "$external_empty_out" "$external_empty_in"
test ! -s "$external_empty_out"
rm -f "$external_empty_in" "$external_empty_out"

external_long_in=${TMPDIR:-/tmp}/rank-external-long-in.$$
external_long_got=${TMPDIR:-/tmp}/rank-external-long-got.$$
external_long_want=${TMPDIR:-/tmp}/rank-external-long-want.$$
long_a=$(printf 'a%070000d' 0)
long_b=$(printf 'b%070000d' 0)
printf '%s\n%s\n' "$long_b" "$long_a" > "$external_long_in"
./rank -S 16 "$external_long_in" > "$external_long_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" "$external_long_in" > "$external_long_want"
else
    sort "$external_long_in" > "$external_long_want"
fi
cmp "$external_long_got" "$external_long_want"
rm -f "$external_long_in" "$external_long_got" "$external_long_want"

external_batch_tmp=${TMPDIR:-/tmp}/rank-external-batch-tmp.$$
mkdir "$external_batch_tmp"
external_batch_in=${TMPDIR:-/tmp}/rank-external-batch-in.$$
external_batch_got=${TMPDIR:-/tmp}/rank-external-batch-got.$$
external_batch_want=${TMPDIR:-/tmp}/rank-external-batch-want.$$
: > "$external_batch_in"
i=120
while test "$i" -gt 0; do
    printf '%03d\n' "$i" >> "$external_batch_in"
    i=$((i - 1))
done
./rank -S 16 -T "$external_batch_tmp" --batch-size=2 "$external_batch_in" > "$external_batch_got"
if test -n "$gnu_sort"; then
    "$gnu_sort" "$external_batch_in" > "$external_batch_want"
else
    sort "$external_batch_in" > "$external_batch_want"
fi
cmp "$external_batch_got" "$external_batch_want"
test -z "$(ls -A "$external_batch_tmp")"
rm -f "$external_batch_in" "$external_batch_got" "$external_batch_want"
rmdir "$external_batch_tmp"

compressor=$(command -v gzip || true)
if test -n "$compressor"; then
    external_compress_tmp=${TMPDIR:-/tmp}/rank-external-compress-tmp.$$
    mkdir "$external_compress_tmp"
    external_compress_in=${TMPDIR:-/tmp}/rank-external-compress-in.$$
    external_compress_got=${TMPDIR:-/tmp}/rank-external-compress-got.$$
    external_compress_want=${TMPDIR:-/tmp}/rank-external-compress-want.$$
    : > "$external_compress_in"
    i=160
    while test "$i" -gt 0; do
        printf 'row-%03d %03d\n' "$i" "$((i % 17))" >> "$external_compress_in"
        i=$((i - 1))
    done
    ./rank -S 24 -T "$external_compress_tmp" --batch-size=2 --compress-program="$compressor" -k2,2n "$external_compress_in" > "$external_compress_got"
    if test -n "$gnu_sort"; then
        "$gnu_sort" -k2,2n "$external_compress_in" > "$external_compress_want"
    else
        sort -k2,2n "$external_compress_in" > "$external_compress_want"
    fi
    cmp "$external_compress_got" "$external_compress_want"
    test -z "$(ls -A "$external_compress_tmp")"
    rm -f "$external_compress_in" "$external_compress_got" "$external_compress_want"
    rmdir "$external_compress_tmp"
fi

external_missing_in=${TMPDIR:-/tmp}/rank-external-missing-in.$$
external_missing_err=${TMPDIR:-/tmp}/rank-external-missing.err.$$
external_missing_status=0
printf 'b\na\n' > "$external_missing_in"
./rank -S 2 -T "${TMPDIR:-/tmp}/rank-missing-dir-$$" "$external_missing_in" >/tmp/rank-external-missing.out.$$ 2>"$external_missing_err" || external_missing_status=$?
test "$external_missing_status" -ne 0
test -s "$external_missing_err"
rm -f "$external_missing_in" "$external_missing_err" /tmp/rank-external-missing.out.$$

external_nowrite_tmp=${TMPDIR:-/tmp}/rank-external-nowrite-tmp.$$
external_nowrite_in=${TMPDIR:-/tmp}/rank-external-nowrite-in.$$
external_nowrite_err=${TMPDIR:-/tmp}/rank-external-nowrite.err.$$
external_nowrite_status=0
mkdir "$external_nowrite_tmp"
chmod 500 "$external_nowrite_tmp"
printf 'b\na\n' > "$external_nowrite_in"
./rank -S 2 -T "$external_nowrite_tmp" "$external_nowrite_in" >/tmp/rank-external-nowrite.out.$$ 2>"$external_nowrite_err" || external_nowrite_status=$?
chmod 700 "$external_nowrite_tmp"
test "$external_nowrite_status" -ne 0
test -s "$external_nowrite_err"
rm -f "$external_nowrite_in" "$external_nowrite_err" /tmp/rank-external-nowrite.out.$$
rmdir "$external_nowrite_tmp"

external_badcompress_in=${TMPDIR:-/tmp}/rank-external-badcompress-in.$$
external_badcompress_err=${TMPDIR:-/tmp}/rank-external-badcompress.err.$$
external_badcompress_status=0
printf 'b\na\n' > "$external_badcompress_in"
./rank -S 2 --compress-program="rank-no-such-compressor-$$" "$external_badcompress_in" >/tmp/rank-external-badcompress.out.$$ 2>"$external_badcompress_err" || external_badcompress_status=$?
test "$external_badcompress_status" -ne 0
test -s "$external_badcompress_err"
rm -f "$external_badcompress_in" "$external_badcompress_err" /tmp/rank-external-badcompress.out.$$

external_signal_tmp=${TMPDIR:-/tmp}/rank-external-signal-tmp.$$
external_signal_in=${TMPDIR:-/tmp}/rank-external-signal-in.$$
external_signal_prog=${TMPDIR:-/tmp}/rank-external-signal-compressor.$$
mkdir "$external_signal_tmp"
printf '#!/bin/sh\nsleep 5\ncat\n' > "$external_signal_prog"
chmod 700 "$external_signal_prog"
: > "$external_signal_in"
i=80
while test "$i" -gt 0; do
    printf '%03d\n' "$i" >> "$external_signal_in"
    i=$((i - 1))
done
./rank -S 8 -T "$external_signal_tmp" --compress-program="$external_signal_prog" "$external_signal_in" >/tmp/rank-external-signal.out.$$ 2>/tmp/rank-external-signal.err.$$ &
external_signal_pid=$!
sleep 1
kill -TERM "$external_signal_pid" 2>/dev/null || true
wait "$external_signal_pid" 2>/dev/null || true
test -z "$(ls -A "$external_signal_tmp")"
rm -f "$external_signal_in" "$external_signal_prog" /tmp/rank-external-signal.out.$$ /tmp/rank-external-signal.err.$$
rmdir "$external_signal_tmp"

check_stdin_status=0
printf 'b\na\n' | ./rank -c >/tmp/rank-check-stdin.out.$$ 2>/tmp/rank-check-stdin.err.$$ || check_stdin_status=$?
test "$check_stdin_status" -eq 1
printf 'rank: -:2: disorder: a\n' > /tmp/rank-check-stdin.want.$$
cmp /tmp/rank-check-stdin.err.$$ /tmp/rank-check-stdin.want.$$
rm -f /tmp/rank-check-stdin.out.$$ /tmp/rank-check-stdin.err.$$ /tmp/rank-check-stdin.want.$$

check_long_a=$(printf 'a%070000d' 0)
check_long_b=$(printf 'b%070000d' 0)
run_check_case check-long-records "${check_long_a}\n${check_long_b}\n" -c

check_z_in=${TMPDIR:-/tmp}/rank-check-z-in.$$
check_z_got_err=${TMPDIR:-/tmp}/rank-check-z-got.err.$$
check_z_want_err=${TMPDIR:-/tmp}/rank-check-z-want.err.$$
check_z_status=0
printf 'b\000a\000' > "$check_z_in"
./rank -cz "$check_z_in" >/tmp/rank-check-z-got.out.$$ 2>"$check_z_got_err" || check_z_status=$?
test "$check_z_status" -eq 1
printf 'rank: %s:2: disorder: a\000' "$check_z_in" > "$check_z_want_err"
cmp "$check_z_got_err" "$check_z_want_err"
rm -f "$check_z_in" "$check_z_got_err" "$check_z_want_err" /tmp/rank-check-z-got.out.$$

check_multi_a=${TMPDIR:-/tmp}/rank-check-multi-a.$$
check_multi_b=${TMPDIR:-/tmp}/rank-check-multi-b.$$
check_multi_err=${TMPDIR:-/tmp}/rank-check-multi.err.$$
check_multi_want=${TMPDIR:-/tmp}/rank-check-multi.want.$$
check_multi_status=0
printf 'a\n' > "$check_multi_a"
printf 'b\n' > "$check_multi_b"
./rank -c "$check_multi_a" "$check_multi_b" >/tmp/rank-check-multi.out.$$ 2>"$check_multi_err" || check_multi_status=$?
test "$check_multi_status" -eq 2
printf "rank: extra operand '%s' not allowed with -c\n" "$check_multi_b" > "$check_multi_want"
cmp "$check_multi_err" "$check_multi_want"
rm -f "$check_multi_a" "$check_multi_b" "$check_multi_err" "$check_multi_want" /tmp/rank-check-multi.out.$$

printf 'delta\nalpha\ncharlie\nbravo\n' > /tmp/rank-golden-radix-verify.in
RANK_DEBUG_VERIFY=1 ./rank /tmp/rank-golden-radix-verify.in > /tmp/rank-golden-radix-verify.got
if test -n "$gnu_sort"; then
    "$gnu_sort" /tmp/rank-golden-radix-verify.in > /tmp/rank-golden-radix-verify.want
else
    sort /tmp/rank-golden-radix-verify.in > /tmp/rank-golden-radix-verify.want
fi
cmp /tmp/rank-golden-radix-verify.got /tmp/rank-golden-radix-verify.want
rm -f /tmp/rank-golden-radix-verify.in /tmp/rank-golden-radix-verify.got /tmp/rank-golden-radix-verify.want

printf 'z 2\na 1\n' > /tmp/rank-golden-debug.in
./rank --debug -b -k2,2 /tmp/rank-golden-debug.in > /tmp/rank-golden-debug.got 2>/tmp/rank-golden-debug.err
if test -n "$gnu_sort"; then
    "$gnu_sort" --debug -b -k2,2 /tmp/rank-golden-debug.in > /tmp/rank-golden-debug.want 2>/tmp/rank-golden-debug-sort.err
    sed 's/^sort:/rank:/' /tmp/rank-golden-debug-sort.err > /tmp/rank-golden-debug.err.want
    cmp /tmp/rank-golden-debug.err /tmp/rank-golden-debug.err.want
else
    printf 'a 1\n  _\n___\nz 2\n  _\n___\n' > /tmp/rank-golden-debug.want
fi
cmp /tmp/rank-golden-debug.got /tmp/rank-golden-debug.want
rm -f /tmp/rank-golden-debug.in /tmp/rank-golden-debug.got /tmp/rank-golden-debug.want \
    /tmp/rank-golden-debug.err /tmp/rank-golden-debug-sort.err /tmp/rank-golden-debug.err.want

printf 'b\na\n' | ./rank --debug -k2,2 > /tmp/rank-golden-debug-empty.got 2>/tmp/rank-golden-debug-empty.err
printf 'a\n ^\n_\nb\n ^\n_\n' > /tmp/rank-golden-debug-empty.want
cmp /tmp/rank-golden-debug-empty.got /tmp/rank-golden-debug-empty.want
grep 'rank: debug: key 1 is empty for record 1' /tmp/rank-golden-debug-empty.err >/dev/null
rm -f /tmp/rank-golden-debug-empty.got /tmp/rank-golden-debug-empty.want /tmp/rank-golden-debug-empty.err

printf 'z,2\na,1\n' | ./rank -t, -k2,2 > /tmp/rank-golden-sep.got
printf 'a,1\nz,2\n' > /tmp/rank-golden-sep.want
cmp /tmp/rank-golden-sep.got /tmp/rank-golden-sep.want
rm -f /tmp/rank-golden-sep.got /tmp/rank-golden-sep.want

printf 'b,,2\na,,1\n' | ./rank -t, -k3,3 > /tmp/rank-golden-adjsep.got
printf 'a,,1\nb,,2\n' > /tmp/rank-golden-adjsep.want
cmp /tmp/rank-golden-adjsep.got /tmp/rank-golden-adjsep.want
rm -f /tmp/rank-golden-adjsep.got /tmp/rank-golden-adjsep.want

long_a=$(printf 'a%0999d\n' 0)
long_b=$(printf 'b%0999d\n' 0)
run_case long-lines "$long_b$long_a"

many_in=/tmp/rank-golden-many.in
many_got=/tmp/rank-golden-many.got
many_want=/tmp/rank-golden-many.want
: > "$many_in"
i=200
while test "$i" -gt 0; do
    printf '%03d\n' "$i" >> "$many_in"
    i=$((i - 1))
done
./rank "$many_in" > "$many_got"
sort "$many_in" > "$many_want"
cmp "$many_got" "$many_want"
rm -f "$many_in" "$many_got" "$many_want"

printf 'c\n' > /tmp/rank-golden-a.in
printf 'a\n' > /tmp/rank-golden-b.in
printf 'b\n' | ./rank /tmp/rank-golden-a.in - /tmp/rank-golden-b.in > /tmp/rank-golden-multi.got
printf 'a\nb\nc\n' > /tmp/rank-golden-multi.want
cmp /tmp/rank-golden-multi.got /tmp/rank-golden-multi.want
rm -f /tmp/rank-golden-a.in /tmp/rank-golden-b.in /tmp/rank-golden-multi.got /tmp/rank-golden-multi.want

printf 'b\na\n' | ./rank - - > /tmp/rank-golden-stdin.got
printf 'a\nb\n' > /tmp/rank-golden-stdin.want
cmp /tmp/rank-golden-stdin.got /tmp/rank-golden-stdin.want
rm -f /tmp/rank-golden-stdin.got /tmp/rank-golden-stdin.want

printf 'b\000a\na\000' | ./rank > /tmp/rank-golden-nul.got
printf 'a\000\nb\000a\n' > /tmp/rank-golden-nul.want
cmp /tmp/rank-golden-nul.got /tmp/rank-golden-nul.want
rm -f /tmp/rank-golden-nul.got /tmp/rank-golden-nul.want

printf 'b\000a\000' | ./rank -z > /tmp/rank-golden-zero.got
printf 'a\000b\000' > /tmp/rank-golden-zero.want
cmp /tmp/rank-golden-zero.got /tmp/rank-golden-zero.want
rm -f /tmp/rank-golden-zero.got /tmp/rank-golden-zero.want

printf 'b\nx\000a\nx\000' | ./rank -z > /tmp/rank-golden-zero-newline.got
printf 'a\nx\000b\nx\000' > /tmp/rank-golden-zero-newline.want
cmp /tmp/rank-golden-zero-newline.got /tmp/rank-golden-zero-newline.want
rm -f /tmp/rank-golden-zero-newline.got /tmp/rank-golden-zero-newline.want

printf 'b\na\n' > /tmp/rank-golden-output.in
./rank -o /tmp/rank-golden-output.out /tmp/rank-golden-output.in
printf 'a\nb\n' > /tmp/rank-golden-output.want
cmp /tmp/rank-golden-output.out /tmp/rank-golden-output.want
./rank -o /tmp/rank-golden-output.in /tmp/rank-golden-output.in
cmp /tmp/rank-golden-output.in /tmp/rank-golden-output.want
printf 'b\na\n' > /tmp/rank-golden-output.in
ln /tmp/rank-golden-output.in /tmp/rank-golden-output.hard 2>/dev/null || true
if test -e /tmp/rank-golden-output.hard; then
    ./rank -o /tmp/rank-golden-output.hard /tmp/rank-golden-output.in
    cmp /tmp/rank-golden-output.hard /tmp/rank-golden-output.want
fi
rm -f /tmp/rank-golden-output.in /tmp/rank-golden-output.hard
printf 'b\na\n' > /tmp/rank-golden-output.in
ln -s /tmp/rank-golden-output.in /tmp/rank-golden-output.sym 2>/dev/null || true
if test -L /tmp/rank-golden-output.sym; then
    ./rank -o /tmp/rank-golden-output.sym /tmp/rank-golden-output.in
    cmp /tmp/rank-golden-output.in /tmp/rank-golden-output.want
fi
rm -f /tmp/rank-golden-output.in /tmp/rank-golden-output.out /tmp/rank-golden-output.want /tmp/rank-golden-output.sym

status=0
printf 'x\n' > /tmp/rank-golden-output-input
./rank -o /tmp /tmp/rank-golden-output-input >/tmp/rank-golden-bad-output.out 2>/tmp/rank-golden-bad-output.err || status=$?
test "$status" -eq 2
grep 'rank: cannot write: /tmp:' /tmp/rank-golden-bad-output.err >/dev/null
rm -f /tmp/rank-golden-output-input /tmp/rank-golden-bad-output.out /tmp/rank-golden-bad-output.err

status=0
./rank --unsupported >/tmp/rank-golden.out 2>/tmp/rank-golden.err || status=$?
test "$status" -eq 2
grep "rank: option '--unsupported' is not implemented yet" /tmp/rank-golden.err >/dev/null
rm -f /tmp/rank-golden.out /tmp/rank-golden.err
printf 'golden harness smoke ok\n'
