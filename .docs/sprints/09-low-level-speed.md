# Sprint 09 - Low-Level Speed

## Objective

Add measured low-level acceleration after the algorithmic wins are already correct: `mmap`, SIMD scans, parallelism, and final tuning.

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
