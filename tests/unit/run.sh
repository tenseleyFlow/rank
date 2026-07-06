#!/bin/sh
set -eu

export LC_ALL=C

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

RANK_DEBUG_STATS=1 ./rank /tmp/rank-file.in >/tmp/rank-stats.out 2>/tmp/rank-stats.err
grep 'rank: comparator calls=' /tmp/rank-stats.err >/dev/null

RANK_DEBUG_VERIFY=1 ./rank /tmp/rank-file.in >/tmp/rank-verify.out
cmp /tmp/rank-verify.out /tmp/rank-file.want

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
    /tmp/rank-after-operand.want
printf 'unit smoke ok\n'
