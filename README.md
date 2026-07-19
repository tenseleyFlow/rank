# rank

A from-scratch C11 reimplementation of GNU `sort(1)`. Same output bytes,
same diagnostics, same exit codes; different algorithms underneath.

GNU sort is a comparison sorter, so each comparison may re-read the same
long prefix, re-extract the same field, or re-parse the same number.
rank parses input once, precomputes per-line key metadata, selects an
execution plan for the exact flag and locale combination, and sorts
eligible plans with an MSD byte radix. Combinations not proven
equivalent fall back to a scalar GNU-compatible comparator, which is
also the internal verification oracle.

## Compatibility

The v0.1 surface covers GNU sort's options: keys (`-k`, `-t`, `-b`,
obsolete `+POS -POS` forms), ordering modes (`-n`, `-g`, `-h`, `-M`,
`-V`, `-R` with GNU-identical MD5 ordering), text modifiers (`-f`,
`-d`, `-i`), check and merge modes (`-c`, `-C`, `-m`), `-o` with
sort-onto-self safety, `-z`, `-u`, `-s`, `-r`, external sorting (`-S`,
`-T`, `--batch-size`, `--compress-program`), `--files0-from`,
`--parallel`, `--debug`, and locale collation through `strxfrm`
transforms. GNU's key-option inheritance rules, last-resort comparison,
and incompatible-option diagnostics are reproduced.

Known deviations, all narrow:

- Obsolete `+POS -POS` keys are accepted unconditionally; GNU gates
  them on `_POSIX2_VERSION`.
- `-R` combined with `-d`/`-f`/`-i` in a non-C multibyte locale hashes
  ASCII-translated bytes where GNU consults locale tables.
- Distinct strings that collate equal in a hard locale order by raw
  bytes in non-stable whole-line sorts; unobservable on glibc.
- `--compress-program` assumes the `PROG` / `PROG -d` convention.

## Benchmarks

Warm-cache wall time versus GNU coreutils sort, `LC_ALL=C`, hyperfine,
100k-line generated fixtures unless noted. Full scripts and stored
results live under `bench/`; `sh bench/run-all.sh` reproduces the
matrix against whatever GNU sort it finds.

Linux x86_64 (i9-11900K, glibc, coreutils 9.11):

| workload | speedup |
|---|---|
| pre-sorted lines | 3.15x ± 0.28 |
| long shared prefixes | 2.85x ± 0.31 |
| stable three-key CSV | 2.63x ± 0.26 |
| keyed path components (`-t/ -k4,4 -k5,5`) | 2.93x ± 0.22 |
| duplicate-heavy stable keyed | 1.83x ± 0.11 |
| log lines by field (`-s -k4,4`) | 1.56x ± 0.12 |
| `-R` shuffle | 7x |
| external with gzip temps | 3.9x |
| 1M lines, `--parallel=8` vs GNU's default parallelism | 1.54x ± 0.10 |

Two Linux shapes sit at statistical parity rather than ahead: whole-line
path lists (GNU 1.04x ± 0.10) and late-field multi-key logs
(GNU 1.09x ± 0.04).

macOS arm64 (Apple M5 Pro, coreutils via Homebrew) is ahead on every
workload in the matrix, including those two:

| workload | speedup |
|---|---|
| stable three-key CSV | 4.67x ± 0.14 |
| keyed path components | 5.25x ± 0.14 |
| late-field multi-key logs | 1.18x ± 0.06 |
| whole-line path lists | 1.34x ± 0.10 |
| external with gzip temps | 21x |
| 1M lines, `--parallel=16` vs GNU's default parallelism | 1.82x ± 0.08 |

`--parallel` output is byte-identical to single-threaded output; on the
1M-line fixture it used roughly a third of GNU's total CPU time.

## Building

```
./configure
make
make check     # unit + golden-vs-GNU + fuzz smoke + perf smoke
make install   # PREFIX=/usr/local by default
```

C11 and libc only; no autotools, no CMake, no dependencies. GNU make is
required (`gmake` on FreeBSD). CI builds and tests on Linux (glibc and
musl), macOS arm64, and FreeBSD.

The golden suite compares against a GNU coreutils sort found as
`gsort` or `sort`, or built from a pinned source tree via
`make ref-sort`; without one it degrades to self-consistency checks.

## Testing

- Golden tests diff stdout, stderr, and exit codes against GNU sort
  across the option and locale matrix, normalizing only the program
  name.
- A differential fuzzer (`make fuzz`) drives random inputs and random
  accepted option sets against GNU sort with plan verification enabled;
  failures save reproduction files.
- Optimized plans are verified against the scalar comparator
  (`RANK_DEBUG_VERIFY=1`), SIMD kernels are fuzzed against scalar
  oracles, and CI runs ASan/UBSan on every platform.

## Packaging

`packaging/` carries an AUR PKGBUILD and a Homebrew formula pinned to
the current release tarball. Releases are tag-driven: CI distchecks and
publishes the tarball.

## License

MIT. See `LICENSE`.
