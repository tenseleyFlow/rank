#!/bin/sh
set -eu

export LC_ALL=C

${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Wconversion -Isrc -o /tmp/rank-scan-fuzz tests/unit/scan-fuzz.c src/sys/scan.c
/tmp/rank-scan-fuzz
rm -f /tmp/rank-scan-fuzz

./rank --version >/tmp/rank-version.out
grep 'rank 0.0.0-sprint01' /tmp/rank-version.out >/dev/null

./rank --help >/tmp/rank-help.out
grep 'Usage: rank' /tmp/rank-help.out >/dev/null

status=0
./rank --no-such-option >/tmp/rank-bad.out 2>/tmp/rank-bad.err || status=$?
test "$status" -eq 2
grep "rank: option '--no-such-option' is not implemented yet" /tmp/rank-bad.err >/dev/null

status=0
./rank -o >/tmp/rank-bad-o.out 2>/tmp/rank-bad-o.err || status=$?
test "$status" -eq 2
grep "rank: option '-o' requires an argument" /tmp/rank-bad-o.err >/dev/null

printf 'b\na\n' | ./rank > /tmp/rank-sort.out
printf 'a\nb\n' > /tmp/rank-sort.want
cmp /tmp/rank-sort.out /tmp/rank-sort.want

printf 'a\nb\na\n' | ./rank -u > /tmp/rank-unique.out
printf 'a\nb\n' > /tmp/rank-unique.want
cmp /tmp/rank-unique.out /tmp/rank-unique.want

printf 'b\000a\000' | ./rank -z > /tmp/rank-zero.out
printf 'a\000b\000' > /tmp/rank-zero.want
cmp /tmp/rank-zero.out /tmp/rank-zero.want

printf 'b\na\n' > /tmp/rank-file.in
./rank -o /tmp/rank-file.out /tmp/rank-file.in
printf 'a\nb\n' > /tmp/rank-file.want
cmp /tmp/rank-file.out /tmp/rank-file.want

RANK_FORCE_SCALAR=1 RANK_DEBUG_STATS=1 ./rank /tmp/rank-file.in >/tmp/rank-stats.out 2>/tmp/rank-stats.err
grep 'rank: comparator calls=' /tmp/rank-stats.err >/dev/null

RANK_DEBUG_VERIFY=1 ./rank /tmp/rank-file.in >/tmp/rank-verify.out
cmp /tmp/rank-verify.out /tmp/rank-file.want

RANK_FORCE_READ=1 ./rank /tmp/rank-file.in >/tmp/rank-forceread.out
cmp /tmp/rank-forceread.out /tmp/rank-file.want
RANK_FORCE_READ=1 ./rank -s -k1,1 /tmp/rank-file.in >/tmp/rank-forceread-key.out
cmp /tmp/rank-forceread-key.out /tmp/rank-file.want

RANK_DEBUG_PLAN=1 ./rank /tmp/rank-file.in >/tmp/rank-plan.out 2>/tmp/rank-plan.err
grep 'rank: plan=radix-bytes reason=whole-line byte radix' /tmp/rank-plan.err >/dev/null
cmp /tmp/rank-plan.out /tmp/rank-file.want

if locale -a 2>/dev/null | grep -Fx -e 'C.UTF-8' -e 'C.utf8' >/dev/null; then
    LC_ALL=C.UTF-8 RANK_DEBUG_PLAN=1 RANK_DEBUG_STATS=1 ./rank /tmp/rank-file.in >/tmp/rank-plan-cutf8.out 2>/tmp/rank-plan-cutf8.err
    grep 'rank: plan=radix-transformed reason=whole-line transformed radix' /tmp/rank-plan-cutf8.err >/dev/null
    grep 'rank: transformed radix ' /tmp/rank-plan-cutf8.err >/dev/null
    cmp /tmp/rank-plan-cutf8.out /tmp/rank-file.want
    LC_ALL=C.UTF-8 RANK_DEBUG_PLAN=1 RANK_DEBUG_STATS=1 ./rank -k1,1 /tmp/rank-file.in >/tmp/rank-plan-cutf8-key.out 2>/tmp/rank-plan-cutf8-key.err
    grep 'rank: plan=radix-transformed reason=single transformed key radix' /tmp/rank-plan-cutf8-key.err >/dev/null
    grep 'rank: transformed radix ' /tmp/rank-plan-cutf8-key.err >/dev/null
    cmp /tmp/rank-plan-cutf8-key.out /tmp/rank-file.want
fi

RANK_DEBUG_STATS=1 ./rank /tmp/rank-file.in >/tmp/rank-radix-stats.out 2>/tmp/rank-radix-stats.err
grep 'rank: radix ' /tmp/rank-radix-stats.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_STATS=1 ./rank -f /tmp/rank-file.in >/tmp/rank-plan-fold.out 2>/tmp/rank-plan-fold.err
grep 'rank: plan=radix-transformed reason=whole-line filtered radix' /tmp/rank-plan-fold.err >/dev/null
grep 'rank: transformed radix ' /tmp/rank-plan-fold.err >/dev/null
RANK_DEBUG_PLAN=1 ./rank -fr /tmp/rank-file.in >/tmp/rank-plan-fold-r.out 2>/tmp/rank-plan-fold-r.err
grep 'rank: plan=radix-transformed reason=whole-line filtered radix' /tmp/rank-plan-fold-r.err >/dev/null
RANK_DEBUG_PLAN=1 ./rank -fu /tmp/rank-file.in >/tmp/rank-plan-fold-u.out 2>/tmp/rank-plan-fold-u.err
grep 'rank: plan=radix-transformed reason=whole-line filtered radix' /tmp/rank-plan-fold-u.err >/dev/null
RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -fru /tmp/rank-file.in >/tmp/rank-plan-fold-ru.out 2>/tmp/rank-plan-fold-ru.err
grep 'rank: plan=radix-transformed reason=whole-line filtered radix' /tmp/rank-plan-fold-ru.err >/dev/null
printf 'b\na\n' > /tmp/rank-plan-fold-ru.want
cmp /tmp/rank-plan-fold-ru.out /tmp/rank-plan-fold-ru.want

RANK_DEBUG_PLAN=1 ./rank -k1,1 /tmp/rank-file.in >/tmp/rank-plan-key.out 2>/tmp/rank-plan-key.err
grep 'rank: plan=radix-keys reason=single key byte radix' /tmp/rank-plan-key.err >/dev/null

printf 'B x\na x\nb y\n' > /tmp/rank-keymod.in
RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -k2,2f /tmp/rank-keymod.in >/tmp/rank-keymod.out 2>/tmp/rank-keymod.err
grep 'rank: plan=radix-transformed reason=single filtered key radix' /tmp/rank-keymod.err >/dev/null
printf 'B x\na x\nb y\n' > /tmp/rank-keymod.want
cmp /tmp/rank-keymod.out /tmp/rank-keymod.want

printf '10\n2\n-1\n1.5\n' > /tmp/rank-numeric.in
RANK_DEBUG_PLAN=1 ./rank -n /tmp/rank-numeric.in >/tmp/rank-numeric.out 2>/tmp/rank-numeric.err
printf -- '-1\n1.5\n2\n10\n' > /tmp/rank-numeric.want
cmp /tmp/rank-numeric.out /tmp/rank-numeric.want
grep 'rank: plan=scalar reason=special comparator' /tmp/rank-numeric.err >/dev/null

printf 'a 10\nb 2\nc -1\n' > /tmp/rank-numeric-key.in
./rank -k2,2n /tmp/rank-numeric-key.in >/tmp/rank-numeric-key.out
printf 'c -1\nb 2\na 10\n' > /tmp/rank-numeric-key.want
cmp /tmp/rank-numeric-key.out /tmp/rank-numeric-key.want

./rank --sort=numeric /tmp/rank-numeric.in >/tmp/rank-numeric-sort-word.out
cmp /tmp/rank-numeric-sort-word.out /tmp/rank-numeric.want

printf '1e2\n10\n-inf\ninf\nnan\nx\n' > /tmp/rank-general-numeric.in
RANK_DEBUG_PLAN=1 ./rank -g /tmp/rank-general-numeric.in >/tmp/rank-general-numeric.out 2>/tmp/rank-general-numeric.err
printf 'x\nnan\n-inf\n10\n1e2\ninf\n' > /tmp/rank-general-numeric.want
cmp /tmp/rank-general-numeric.out /tmp/rank-general-numeric.want
grep 'rank: plan=scalar reason=special comparator' /tmp/rank-general-numeric.err >/dev/null

./rank --sort=g /tmp/rank-general-numeric.in >/tmp/rank-general-numeric-sort-g.out
cmp /tmp/rank-general-numeric-sort-g.out /tmp/rank-general-numeric.want

printf 'x,b\ny,a\nz,a\n' > /tmp/rank-key-radix.in
RANK_DEBUG_PLAN=1 RANK_DEBUG_STATS=1 ./rank -s -t, -k2,2 /tmp/rank-key-radix.in >/tmp/rank-key-radix.out 2>/tmp/rank-key-radix.err
printf 'y,a\nz,a\nx,b\n' > /tmp/rank-key-radix.want
cmp /tmp/rank-key-radix.out /tmp/rank-key-radix.want
grep 'rank: plan=radix-keys reason=single stable key byte radix' /tmp/rank-key-radix.err >/dev/null
grep 'rank: key radix passes=' /tmp/rank-key-radix.err >/dev/null

RANK_DEBUG_PLAN=1 ./rank -t, -k2,2 /tmp/rank-key-radix.in >/tmp/rank-key-radix-last.out 2>/tmp/rank-key-radix-last.err
printf 'y,a\nz,a\nx,b\n' > /tmp/rank-key-radix-last.want
cmp /tmp/rank-key-radix-last.out /tmp/rank-key-radix-last.want
grep 'rank: plan=radix-keys reason=single key byte radix' /tmp/rank-key-radix-last.err >/dev/null

printf 'b,2\na,2\nc,1\n' > /tmp/rank-key-radix-multi.in
RANK_DEBUG_PLAN=1 ./rank -s -t, -k2,2 -k1,1 /tmp/rank-key-radix-multi.in >/tmp/rank-key-radix-multi.out 2>/tmp/rank-key-radix-multi.err
printf 'c,1\na,2\nb,2\n' > /tmp/rank-key-radix-multi.want
cmp /tmp/rank-key-radix-multi.out /tmp/rank-key-radix-multi.want
grep 'rank: plan=radix-keys reason=multi-key byte radix' /tmp/rank-key-radix-multi.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -t, -k2,2 -k1,1 /tmp/rank-key-radix-multi.in >/tmp/rank-key-radix-multi-fallback.out 2>/tmp/rank-key-radix-multi-fallback.err
printf 'c,1\na,2\nb,2\n' > /tmp/rank-key-radix-multi-fallback.want
cmp /tmp/rank-key-radix-multi-fallback.out /tmp/rank-key-radix-multi-fallback.want
grep 'rank: plan=radix-keys reason=multi-key byte radix' /tmp/rank-key-radix-multi-fallback.err >/dev/null

printf 'a,x,2\nb,x,1\na,x,1\nb,x,2\n' > /tmp/rank-key-radix-nkey.in
RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -s -t, -k1,1 -k2,2 -k3,3 /tmp/rank-key-radix-nkey.in >/tmp/rank-key-radix-nkey.out 2>/tmp/rank-key-radix-nkey.err
printf 'a,x,1\na,x,2\nb,x,1\nb,x,2\n' > /tmp/rank-key-radix-nkey.want
cmp /tmp/rank-key-radix-nkey.out /tmp/rank-key-radix-nkey.want
grep 'rank: plan=radix-keys reason=multi-key byte radix' /tmp/rank-key-radix-nkey.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -t, -k2,2r /tmp/rank-key-radix.in >/tmp/rank-key-radix-rev.out 2>/tmp/rank-key-radix-rev.err
printf 'x,b\ny,a\nz,a\n' > /tmp/rank-key-radix-rev.want
cmp /tmp/rank-key-radix-rev.out /tmp/rank-key-radix-rev.want
grep 'rank: plan=radix-keys reason=single key byte radix' /tmp/rank-key-radix-rev.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -s -r -t, -k1,1 -k3,3 /tmp/rank-key-radix-nkey.in >/tmp/rank-key-radix-global-rev.out 2>/tmp/rank-key-radix-global-rev.err
printf 'b,x,2\nb,x,1\na,x,2\na,x,1\n' > /tmp/rank-key-radix-global-rev.want
cmp /tmp/rank-key-radix-global-rev.out /tmp/rank-key-radix-global-rev.want
grep 'rank: plan=radix-keys reason=multi-key byte radix' /tmp/rank-key-radix-global-rev.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -s -t, -k1,1 -k3,3r /tmp/rank-key-radix-nkey.in >/tmp/rank-key-radix-mixed-rev.out 2>/tmp/rank-key-radix-mixed-rev.err
printf 'a,x,2\na,x,1\nb,x,2\nb,x,1\n' > /tmp/rank-key-radix-mixed-rev.want
cmp /tmp/rank-key-radix-mixed-rev.out /tmp/rank-key-radix-mixed-rev.want
grep 'rank: plan=radix-keys reason=multi-key byte radix' /tmp/rank-key-radix-mixed-rev.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -r -t, -k2,2 /tmp/rank-key-radix.in >/tmp/rank-key-radix-global-rev-fallback.out 2>/tmp/rank-key-radix-global-rev-fallback.err
printf 'x,b\nz,a\ny,a\n' > /tmp/rank-key-radix-global-rev-fallback.want
cmp /tmp/rank-key-radix-global-rev-fallback.out /tmp/rank-key-radix-global-rev-fallback.want
grep 'rank: plan=radix-keys reason=single key byte radix' /tmp/rank-key-radix-global-rev-fallback.err >/dev/null

printf 'z,a\na,a\nb,b\n' > /tmp/rank-key-radix-unique.in
RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -u -t, -k2,2 /tmp/rank-key-radix-unique.in >/tmp/rank-key-radix-unique.out 2>/tmp/rank-key-radix-unique.err
printf 'z,a\nb,b\n' > /tmp/rank-key-radix-unique.want
cmp /tmp/rank-key-radix-unique.out /tmp/rank-key-radix-unique.want
grep 'rank: plan=radix-keys reason=unique key byte radix' /tmp/rank-key-radix-unique.err >/dev/null

RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -u -r -t, -k2,2 /tmp/rank-key-radix-unique.in >/tmp/rank-key-radix-unique-rev.out 2>/tmp/rank-key-radix-unique-rev.err
printf 'b,b\nz,a\n' > /tmp/rank-key-radix-unique-rev.want
cmp /tmp/rank-key-radix-unique-rev.out /tmp/rank-key-radix-unique-rev.want
grep 'rank: plan=radix-keys reason=unique key byte radix' /tmp/rank-key-radix-unique-rev.err >/dev/null

printf 'x,a,1\ny,a,1\nz,a,2\nw,b,1\n' > /tmp/rank-key-radix-unique-multi.in
RANK_DEBUG_PLAN=1 RANK_DEBUG_VERIFY=1 ./rank -u -t, -k2,2 -k3,3 /tmp/rank-key-radix-unique-multi.in >/tmp/rank-key-radix-unique-multi.out 2>/tmp/rank-key-radix-unique-multi.err
printf 'x,a,1\nz,a,2\nw,b,1\n' > /tmp/rank-key-radix-unique-multi.want
cmp /tmp/rank-key-radix-unique-multi.out /tmp/rank-key-radix-unique-multi.want
grep 'rank: plan=radix-keys reason=unique key byte radix' /tmp/rank-key-radix-unique-multi.err >/dev/null

printf 'c,3\nb,2\na,1\n' > /tmp/rank-key-radix-monotonic.in
RANK_DEBUG_STATS=1 RANK_DEBUG_VERIFY=1 ./rank -s -t, -k2,2 /tmp/rank-key-radix-monotonic.in >/tmp/rank-key-radix-monotonic.out 2>/tmp/rank-key-radix-monotonic.err
printf 'a,1\nb,2\nc,3\n' > /tmp/rank-key-radix-monotonic.want
cmp /tmp/rank-key-radix-monotonic.out /tmp/rank-key-radix-monotonic.want
grep 'rank: key radix passes=0 classified=0 insertion_sorts=0' /tmp/rank-key-radix-monotonic.err >/dev/null

RANK_DEBUG_PLAN=1 ./rank --debug /tmp/rank-file.in >/tmp/rank-plan-debug.out 2>/tmp/rank-plan-debug.err
grep 'rank: plan=scalar reason=debug output' /tmp/rank-plan-debug.err >/dev/null

./rank -S 1K --buffer-size=2M -T /tmp --temporary-directory=/tmp --batch-size=8 --compress-program=gzip /tmp/rank-file.in >/tmp/rank-external-options.out
cmp /tmp/rank-external-options.out /tmp/rank-file.want

status=0
./rank --buffer-size=bad /tmp/rank-file.in >/tmp/rank-bad-buffer.out 2>/tmp/rank-bad-buffer.err || status=$?
test "$status" -eq 2
grep "rank: invalid --buffer-size argument 'bad'" /tmp/rank-bad-buffer.err >/dev/null

status=0
./rank --batch-size=0 /tmp/rank-file.in >/tmp/rank-bad-batch.out 2>/tmp/rank-bad-batch.err || status=$?
test "$status" -eq 2
grep "rank: invalid --batch-size argument '0'" /tmp/rank-bad-batch.err >/dev/null

status=0
./rank -T '' /tmp/rank-file.in >/tmp/rank-bad-temp.out 2>/tmp/rank-bad-temp.err || status=$?
test "$status" -eq 2
grep 'rank: temporary directory name is empty' /tmp/rank-bad-temp.err >/dev/null

RANK_FORCE_SCALAR=1 RANK_DEBUG_PLAN=1 ./rank /tmp/rank-file.in >/tmp/rank-plan-force.out 2>/tmp/rank-plan-force.err
grep 'rank: plan=scalar reason=forced scalar' /tmp/rank-plan-force.err >/dev/null

RANK_DEBUG_KEYS=1 ./rank -k2.3,4.5r -t, /tmp/rank-file.in >/tmp/rank-key.out 2>/tmp/rank-key.err
grep 'rank: keys=1 field-separator=44 global-b=0 global-d=0 global-f=0 global-i=0 debug=0' /tmp/rank-key.err >/dev/null
grep 'rank: key\[0\] start=2.3 end=4.5 start_b=0 end_b=0 d=0 f=0 i=0 reverse=1' /tmp/rank-key.err >/dev/null

RANK_DEBUG_KEYS=1 ./rank -b --key=1b,2b /tmp/rank-file.in >/tmp/rank-key-b.out 2>/tmp/rank-key-b.err
grep 'rank: keys=1 field-separator=default global-b=1 global-d=0 global-f=0 global-i=0 debug=0' /tmp/rank-key-b.err >/dev/null
grep 'rank: key\[0\] start=1.0 end=2.0 start_b=1 end_b=1 d=0 f=0 i=0 reverse=0' /tmp/rank-key-b.err >/dev/null

RANK_DEBUG_KEYS=1 ./rank --debug -k1 /tmp/rank-file.in >/tmp/rank-debug-key.out 2>/tmp/rank-debug-key.err
grep 'rank: keys=1 field-separator=default global-b=0 global-d=0 global-f=0 global-i=0 debug=1' /tmp/rank-debug-key.err >/dev/null

RANK_DEBUG_KEYS=1 ./rank +0 -1 /tmp/rank-file.in >/tmp/rank-old-key.out 2>/tmp/rank-old-key.err
grep 'rank: key\[0\] start=1.0 end=1.0 start_b=0 end_b=0 d=0 f=0 i=0 reverse=0' /tmp/rank-old-key.err >/dev/null

RANK_DEBUG_PLAN=1 ./rank -R tests/fixtures/random/basic.in >/tmp/rank-random.out 2>/tmp/rank-random.err
grep 'rank: plan=scalar reason=special comparator' /tmp/rank-random.err >/dev/null
sort /tmp/rank-random.out >/tmp/rank-random.content
sort tests/fixtures/random/basic.in >/tmp/rank-random.content.want
cmp /tmp/rank-random.content /tmp/rank-random.content.want

./rank -R --random-source=tests/fixtures/random/seed-a.bin tests/fixtures/random/basic.in >/tmp/rank-random-seed-a.out
./rank -R --random-source tests/fixtures/random/seed-a.bin tests/fixtures/random/basic.in >/tmp/rank-random-seed-a-repeat.out
./rank -R --random-source=tests/fixtures/random/seed-b.bin tests/fixtures/random/basic.in >/tmp/rank-random-seed-b.out
cmp /tmp/rank-random-seed-a.out /tmp/rank-random-seed-a-repeat.out
if cmp -s /tmp/rank-random-seed-a.out /tmp/rank-random-seed-b.out; then
    printf 'random source did not affect output\n' >&2
    exit 1
fi

./rank --sort=random --random-source=tests/fixtures/random/seed-a.bin tests/fixtures/random/basic.in >/tmp/rank-random-sort-word.out
cmp /tmp/rank-random-sort-word.out /tmp/rank-random-seed-a.out

status=0
./rank -R --random-source=tests/fixtures/random/seed-short.bin tests/fixtures/random/basic.in >/tmp/rank-random-short.out 2>/tmp/rank-random-short.err || status=$?
test "$status" -eq 2
grep "rank: 'tests/fixtures/random/seed-short.bin': end of file" /tmp/rank-random-short.err >/dev/null

status=0
./rank --random-source >/tmp/rank-random-bad-source.out 2>/tmp/rank-random-bad-source.err || status=$?
test "$status" -eq 2
grep "rank: option '--random-source' requires an argument" /tmp/rank-random-bad-source.err >/dev/null

./rank -t, -k2,2R --random-source=tests/fixtures/random/seed-a.bin tests/fixtures/random/keyed.csv >/tmp/rank-random-key.out
./rank -t, -k2,2R --random-source=tests/fixtures/random/seed-a.bin tests/fixtures/random/keyed.csv >/tmp/rank-random-key-repeat.out
cmp /tmp/rank-random-key.out /tmp/rank-random-key-repeat.out
./rank -u -t, -k2,2R --random-source=tests/fixtures/random/seed-a.bin tests/fixtures/random/keyed-dups.csv >/tmp/rank-random-key-unique.out
test "$(wc -l </tmp/rank-random-key-unique.out)" -eq 2

status=0
./rank -t,, /tmp/rank-file.in >/tmp/rank-bad-sep.out 2>/tmp/rank-bad-sep.err || status=$?
test "$status" -eq 2
grep "rank: field separator must be a single character: ',,'" /tmp/rank-bad-sep.err >/dev/null

./rank /tmp/rank-file.in -r > /tmp/rank-after-operand.out
printf 'b\na\n' > /tmp/rank-after-operand.want
cmp /tmp/rank-after-operand.out /tmp/rank-after-operand.want

./rank -o /tmp/rank-file.in /tmp/rank-file.in
cmp /tmp/rank-file.in /tmp/rank-file.want

status=0
./rank /tmp/rank-missing-file >/tmp/rank-missing.out 2>/tmp/rank-missing.err || status=$?
test "$status" -eq 2
grep 'rank: cannot read: /tmp/rank-missing-file:' /tmp/rank-missing.err >/dev/null

rm -f /tmp/rank-version.out /tmp/rank-help.out /tmp/rank-bad.out /tmp/rank-bad.err \
    /tmp/rank-bad-o.out /tmp/rank-bad-o.err \
    /tmp/rank-sort.out /tmp/rank-sort.want /tmp/rank-unique.out /tmp/rank-unique.want \
    /tmp/rank-zero.out /tmp/rank-zero.want /tmp/rank-file.in /tmp/rank-file.out \
    /tmp/rank-file.want /tmp/rank-stats.out /tmp/rank-stats.err /tmp/rank-missing.out \
    /tmp/rank-missing.err /tmp/rank-verify.out /tmp/rank-after-operand.out \
    /tmp/rank-forceread.out /tmp/rank-forceread-key.out \
    /tmp/rank-plan.out /tmp/rank-plan.err /tmp/rank-plan-cutf8.out \
    /tmp/rank-plan-cutf8.err /tmp/rank-plan-cutf8-key.out \
    /tmp/rank-plan-cutf8-key.err /tmp/rank-plan-fold.out \
    /tmp/rank-plan-fold.err /tmp/rank-plan-fold-r.out \
    /tmp/rank-plan-fold-r.err /tmp/rank-plan-fold-u.out \
    /tmp/rank-plan-fold-u.err /tmp/rank-plan-fold-ru.out \
    /tmp/rank-plan-fold-ru.err /tmp/rank-plan-fold-ru.want /tmp/rank-plan-key.out \
    /tmp/rank-keymod.in /tmp/rank-keymod.out /tmp/rank-keymod.err /tmp/rank-keymod.want \
    /tmp/rank-radix-stats.out /tmp/rank-radix-stats.err \
    /tmp/rank-plan-key.err /tmp/rank-plan-debug.out /tmp/rank-plan-debug.err \
    /tmp/rank-numeric.in /tmp/rank-numeric.out /tmp/rank-numeric.err \
    /tmp/rank-numeric.want /tmp/rank-numeric-key.in /tmp/rank-numeric-key.out \
    /tmp/rank-numeric-key.want /tmp/rank-numeric-sort-word.out \
    /tmp/rank-general-numeric.in /tmp/rank-general-numeric.out \
    /tmp/rank-general-numeric.err /tmp/rank-general-numeric.want \
    /tmp/rank-general-numeric-sort-g.out \
    /tmp/rank-plan-force.out /tmp/rank-plan-force.err \
    /tmp/rank-external-options.out /tmp/rank-bad-buffer.out /tmp/rank-bad-buffer.err \
    /tmp/rank-bad-batch.out /tmp/rank-bad-batch.err \
    /tmp/rank-bad-temp.out /tmp/rank-bad-temp.err \
    /tmp/rank-after-operand.want /tmp/rank-key.out /tmp/rank-key.err \
    /tmp/rank-key-radix.in /tmp/rank-key-radix.out /tmp/rank-key-radix.err \
    /tmp/rank-key-radix.want /tmp/rank-key-radix-last.out \
    /tmp/rank-key-radix-last.err /tmp/rank-key-radix-last.want \
    /tmp/rank-key-radix-multi.in /tmp/rank-key-radix-multi.out \
    /tmp/rank-key-radix-multi.err /tmp/rank-key-radix-multi.want \
    /tmp/rank-key-radix-multi-fallback.out /tmp/rank-key-radix-multi-fallback.err \
    /tmp/rank-key-radix-multi-fallback.want /tmp/rank-key-radix-global-rev-fallback.want \
    /tmp/rank-key-radix-nkey.in /tmp/rank-key-radix-nkey.out \
    /tmp/rank-key-radix-nkey.err /tmp/rank-key-radix-nkey.want \
    /tmp/rank-key-radix-rev.out /tmp/rank-key-radix-rev.err \
    /tmp/rank-key-radix-rev.want /tmp/rank-key-radix-global-rev.out \
    /tmp/rank-key-radix-global-rev.err /tmp/rank-key-radix-global-rev.want \
    /tmp/rank-key-radix-mixed-rev.out /tmp/rank-key-radix-mixed-rev.err \
    /tmp/rank-key-radix-mixed-rev.want /tmp/rank-key-radix-global-rev-fallback.out \
    /tmp/rank-key-radix-global-rev-fallback.err \
    /tmp/rank-key-radix-unique.in /tmp/rank-key-radix-unique.out \
    /tmp/rank-key-radix-unique.err /tmp/rank-key-radix-unique.want \
    /tmp/rank-key-radix-unique-rev.out /tmp/rank-key-radix-unique-rev.err \
    /tmp/rank-key-radix-unique-rev.want /tmp/rank-key-radix-unique-multi.in \
    /tmp/rank-key-radix-unique-multi.out /tmp/rank-key-radix-unique-multi.err \
    /tmp/rank-key-radix-unique-multi.want \
    /tmp/rank-key-radix-monotonic.in /tmp/rank-key-radix-monotonic.out \
    /tmp/rank-key-radix-monotonic.err /tmp/rank-key-radix-monotonic.want \
    /tmp/rank-key-b.out /tmp/rank-key-b.err /tmp/rank-debug-key.out \
    /tmp/rank-debug-key.err /tmp/rank-random.out \
    /tmp/rank-random.err /tmp/rank-random-sort-word.out \
    /tmp/rank-random.content /tmp/rank-random.content.want \
    /tmp/rank-random-short.out /tmp/rank-random-short.err \
    /tmp/rank-random-seed-a.out /tmp/rank-random-seed-a-repeat.out \
    /tmp/rank-random-seed-b.out \
    /tmp/rank-random-bad-source.out /tmp/rank-random-bad-source.err \
    /tmp/rank-random-key.out /tmp/rank-random-key-repeat.out \
    /tmp/rank-random-key-unique.out \
    /tmp/rank-bad-sep.out /tmp/rank-bad-sep.err /tmp/rank-old-key.out \
    /tmp/rank-old-key.err
printf 'unit smoke ok\n'
