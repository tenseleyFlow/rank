# Sprint 03 - Default Radix

## Objective

Make rank fast for the baseline case: whole-line byte sorting in an identity collation locale. This is the first proof that rank is not another comparison-sort clone.

## Build targets

- Implement `PLAN_RADIX_BYTES` for whole-line sort.
- Add identity collation detection for `C`, `POSIX`, and probed identity locales such as safe `C.UTF-8` cases.
- Implement MSD byte radix over line spans.
- Add insertion-sort fallback for tiny buckets using the scalar comparator from the current depth.
- Add recursion depth bounds and iterative fallback for extreme common prefixes.
- Support default sort, `-z`, global `-r`, `-s`, and full-line `-u` only where proven equivalent.
- Add debug mode that verifies radix output against the scalar comparator on sampled or full small inputs.

## Implementation notes

- Bucket 0 is end-of-key. Buckets 1..256 are byte+1.
- Whole-line byte radix uses in-place partitioning. This is valid only because equal full-line keys have identical output bytes, so instability is unobservable for the eligible plan.
- Keyed radix must not reuse this instability without separate proof; keep stable grouping for key plans.
- Keep radix plan eligibility strict. If locale or options are not proven safe, fall back to scalar.
- Sorting must operate on counted spans. No C string assumptions.
- Regular-file input may use `mmap`; stdin, pipes, multiple inputs, `-o`, and failed mappings use the read fallback.
- Plain output uses batched `writev`, with a contiguous-run fast path for monotonic forward output.
- Radix work records are compact 32-bit `off,len` pairs with scalar fallback for inputs larger than 4 GiB.

## Subsprints

- 03A - Plan gate: add explicit scalar/radix plan selection and only choose radix for whole-line byte sorts in `LC_ALL=C` or `LC_COLLATE=C`/`POSIX`.
- 03B - Radix engine: implement stable MSD byte radix over whole-line spans with insertion fallback and bounded recursion.
- 03C - Verification: add debug verification that radix output matches scalar ordering, plus GNU-backed golden fixtures for radix-eligible cases.
- 03D - Reverse and unique: enable only the full-line `-r`, `-s`, `-u`, and `-z` cases proven equivalent by tests.
- 03E - Perf pass: add warm-cache default workloads and record before/after GNU numbers.

## Parity tests

- All Sprint 01 default fixtures through both scalar and radix plans.
- Long shared prefixes, paths, URLs, timestamps, random bytes, all equal lines.
- Prefix cases: `a`, `aa`, `aaa`, empty line, embedded NUL.
- `-z` records with newline data.
- Global `-r` cases where final reversal is valid and cases routed to comparator if not.
- Full-line `-u` duplicate groups with GNU survivor golden tests.
- Locale plan tests: byte radix enabled in identity locales, disabled in non-identity locales.

## Performance notes

- This sprint must beat GNU sort on warm-cache default byte workloads.
- Required benchmark corpora: shuffled words, long common prefixes, paths, URLs/log lines, many equal lines, tiny files.
- Track records/sec, MB/sec, comparator calls, bytes classified by radix, peak RSS.
- Tune insertion threshold by measurement, not guesswork.
- Optimization order for the current pass: first regular-file `mmap` input with read fallback, then compact whole-line radix metadata, then contiguous-run output, while expanding the benchmark set in parallel.

## Closeout

- Sprint 03 is complete as of `bench/results/default-20260706152334.txt` on the FreeBSD dev box.
- Expanded benchmark matrix: reverse words, sorted words, long common prefixes, shuffled words, paths, URL/log-like records, duplicate-heavy equal keys, variable-length records, and tiny files.
- Current benchmark result: rank beats GNU sort on every matrix workload.
- Strong workloads: reverse words `3.26x`, sorted words `3.45x`, prefixes `2.98x`, duplicate-heavy `2.08x`.
- Radix-heavy shuffled workload is near the target at `1.95x`.
- Weak workloads to inform Sprint 04 and later plan work: URL/log-like records `1.17x`, variable-length records `1.33x`, paths `1.35x`.
- Do not keep tuning Sprint 03 in isolation unless a regression appears. Next performance work should come from keyed radix and richer plan selection.

## Pitfalls

- Do not assume every UTF-8 locale sorts by byte.
- Do not allocate bucket arrays per recursive node if stack/local fixed arrays suffice.
- Do not make recursion proportional to line length without a hard fallback.
- Do not optimize `-u` by dropping lines before GNU survivor semantics are proven.

## Exit criteria

- Radix plan matches scalar and GNU for the sprint matrix.
- Radix beats GNU sort on default warm-cache benchmark workloads on the dev box.
- Unsupported option/locale combinations fall back to scalar.
- Sanitizers are clean.
