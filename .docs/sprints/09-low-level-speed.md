# Sprint 09 - Low-Level Speed

## Objective

Add measured low-level acceleration after the algorithmic wins are already correct: `mmap`, SIMD scans, parallelism, and final tuning.

## Current status

- Sprint 09 started after Sprint 08 closeout.
- Carried exceptions after 09B: `-r -u` filtered radix remains scalar until survivor ordering is proven; non-stable key-local-only text modifiers remain scalar; embedded-NUL locale spans fall back to byte comparison.

## Subsprints

- 09A perf gate baseline: consolidate benchmark scripts, record known exceptions, and make local speed checks reproducible before adding more low-level code.
- 09B filtered ASCII fast paths: materialize filtered/folded byte spans for C/POSIX `-f`, `-d`, and `-i`, then route exact eligible cases through radix.
- 09C SIMD scanners: delimiter scan, field separator scan, blank classification, and ASCII transform kernels with scalar oracle tests.
- 09D mmap tuning: measure existing mmap path, add size/shape gates, and compare read vs mmap on identical fixtures.
- 09E radix/output tuning: tune thresholds, recursion limits, aux allocation, output batching, and merge heap choices.
- 09F parallelism: implement `--parallel=N` only for deterministic eligible phases, with single-thread parity tests and TSan where available.

## 09A status

- Starting point: individual benchmark scripts exist for default, keyed, special, check, merge, external, and locale workloads.
- `bench/run-all.sh` runs the local perf suite and writes `bench/results/all-*.txt` with per-script result paths. Exit 77 from a benchmark is recorded as skipped.
- This gives future SIMD/radix changes a consistent before/after record.
- Latest local baseline manifest: `bench/results/all-20260707003325.txt`.

## 09B status

- C/POSIX text modifiers now materialize filtered byte spans in the existing transform arena.
- Whole-line byte sorts with `-f`, `-d`, or `-i` route to `RANK_PLAN_RADIX_TRANSFORMED` as `whole-line filtered radix` when collation is identity. Reverse and unique are enabled except for combined `-r -u`, which remains scalar until survivor ordering is proven.
- Filtered whole-line radix now sorts equal filtered groups by original bytes for GNU last-resort ordering unless `-s` or `-u` suppresses it.
- Single-key byte sorts with global text modifiers route to transformed keyed radix when exact. Stable single-key key-local modifiers also use the same storage; non-stable key-local-only cases stay scalar until whole-line last-resort filtering is represented exactly.
- Unit coverage checks `-f`, `-fr`, and `-fu` select filtered radix, while `-fru` stays scalar. Golden coverage compares global and key-local `-f`, `-d`, and `-i`, including fold-equal last-resort, reverse, unique, and stable key-local cases against GNU sort.
- Latest local locale benchmark after filtered radix: `bench/results/locale-20260707004453.txt`; whole-line `C.UTF-8` was 0.01s vs GNU 0.02s, keyed `C.UTF-8` was 0.03s vs GNU 0.04s, and `LC_ALL=C -f -d` was 0.01s vs GNU 0.01s.

## 09E status

- Raised only `KEY_RADIX_INSERTION_THRESHOLD` from 24 to 48. Whole-line `RADIX_INSERTION_THRESHOLD` stays 24 after the 48 experiment regressed default URL/tiny cases.
- Focused benchmark after the kept threshold split: `bench/results/default-20260707005331.txt` and `bench/results/keyed-20260707005331.txt`.
- Compared to `bench/results/keyed-20260707003331.txt`, keyed rank means improved on `-s -k4,4` logs (18.1ms to 15.9ms), `-s -k5,5 -k6,6` logs (22.4ms to 20.3ms), keyed URLs (19.6ms to 18.5ms), and keyed prefix CSV (13.4ms to 12.0ms). Keyed paths regressed (24.2ms to 26.6ms), so path separator/key extraction became the next 09E target.
- A keyed-only mmap gate was measured in `bench/results/default-20260707005548.txt` and `bench/results/keyed-20260707005548.txt`, then rejected because it regressed keyed cases.
- Multi-key explicit-separator key prep now caches field starts/ends once per line. Focused benchmark after the cache: `bench/results/keyed-20260707005924.txt` and `bench/results/default-20260707005924.txt`; keyed paths improved from 26.6ms to 21.9ms versus the threshold-only run, and from 24.2ms to 21.9ms versus the 09A baseline.
- Plain output byte-threshold flushing was measured in `bench/results/default-20260707011449.txt`, `bench/results/keyed-20260707011449.txt`, and `bench/results/merge-20260707011449.txt`, then rejected because the default/keyed signal was mixed and tiny/variable cases regressed.
- Merge direct output now coalesces original record delimiters into the same iov entry when the delimiter is still present in input storage. Focused benchmark after the kept change: `bench/results/merge-20260707011631.txt`; coarse version merge cases improved from 0.05s/0.04s to 0.03s/0.03s versus the rejected-output run.

## Build targets

- Add optional `mmap` input for large regular files behind a planner decision.
- Add SIMD delimiter scanning for newline and NUL records.
- Add SIMD field-separator scanning for `-t SEP`.
- Add SIMD blank classification for sort field splitting.
- Add SIMD ASCII transforms for `-f`, `-d`, and `-i` where locale permits.
- Add runtime CPU feature detection for SSE2/AVX2 on x86_64 and NEON on aarch64.
- Implement `--parallel=N` for eligible in-memory sort/run generation/merge phases.
- Tune radix thresholds, aux allocation, output buffering, and merge heap/loser tree choice.

## Implementation notes

- Scalar remains the oracle for every SIMD kernel.
- SIMD code must handle unaligned buffers and short tails.
- `mmap` is only for regular files and only when measurement supports it.
- Parallel work must preserve deterministic GNU output.
- Keep feature detection isolated under `src/sys/`.

## Parity tests

- Scalar vs SIMD fuzz tests for delimiter scan, separator scan, blank scan, and transforms.
- `read()` path vs `mmap` path on the same fixtures.
- Parallel vs single-thread output for default, keyed, radix, external, merge, `-s`, and `-u` cases.
- TSan for threaded paths where available.

## Performance notes

- Required benchmarks across Linux, macOS, and FreeBSD: default words, long prefixes, paths, CSV keys, external sort, stdin/stdout.
- Measure warm and cold cache where required by the overview.
- SIMD should improve parser/key-precompute time. It cannot compensate for a wrong algorithm.
- Parallelism should help large inputs and not slow tiny inputs.

## Pitfalls

- Do not require AVX-512.
- Do not route pipes or devices through `mmap`.
- Do not let thread scheduling change stable or unique output.
- Do not add CPU-specific code without scalar fallback.

## Exit criteria

- SIMD and `mmap` paths match scalar/read paths under fuzz and golden tests.
- `--parallel` matches single-thread output and improves large eligible workloads.
- Full perf gate passes locally on target machines or has explicit tracked exceptions.
- Sanitizers and TSan are clean where supported.
