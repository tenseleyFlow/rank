# Sprint 07 - External Sorting

## Objective

Handle inputs larger than the memory budget while preserving GNU temp-file behavior. Fast run sorting should reuse the radix and key-precompute paths.

## Current status

- Sprint 07 is closed locally.
- Tracked carry-over from Sprint 06 is resolved for the smoke matrix: keyed/special merge-head caches now match or beat GNU in the local merge benchmark cases.

## Subsprints

- 07A option surface and budgeting: parse `-S`, `-T`, `--batch-size`, and `--compress-program`; validate GNU-shaped errors; keep current in-memory execution until run generation is ready.
- 07B run generation: force external mode with small `-S`, write sorted temp runs, clean them on success/error, and merge byte runs with existing streaming heap logic.
- 07C keyed/special merge heads: add per-head key metadata caches for external merge and reuse them to fix Sprint 06 keyed/special merge performance exceptions.
- 07D temp behavior hardening: multiple temp dirs, bounded descriptors and merge passes, `--compress-program`, `-o` alias safety, cleanup on fatal paths, and forced-external benchmarks.

## 07A status

- `-S SIZE`, `--buffer-size=SIZE`, `-T DIR`, `--temporary-directory=DIR`, `--batch-size=NMERGE`, and `--compress-program=PROG` parse.
- Buffer sizes currently accept plain bytes plus `K`, `M`, and `G` suffixes. Budget enforcement starts in 07B.
- Temporary directories and compressor program are stored for 07B/07D, but current execution remains in-memory until run generation lands.
- Unit coverage checks accepted option combinations and invalid buffer size, batch size, and empty temp directory diagnostics.

## 07B status

- Forced-external path is in place for `-S`: input is read in chunks, accumulated into budget-sized record runs, each run is sorted with the existing planner, written as a private temp file, and merged with the existing merge path.
- Temp run cleanup is covered on normal completion. Default temp dir is `${TMPDIR}` when set, otherwise `/tmp`; `-T` directories are used round-robin.
- Run output disables local duplicate suppression while keeping unique-aware run comparison. Final `-u` is applied during merge.
- Current forced-external coverage includes default byte sort, keyed numeric sort, `-u`, `-z`, empty input, long records crossing the budget/read boundary, temp cleanup, `-T`, and `-o` aliasing.
- The bounded reader flushes only after complete records. Memory may exceed `-S` by one read chunk or one long record.
- External benchmark smoke added as `bench/run-external.sh`. Latest local run: `bench/results/external-20260706225558.txt`; byte forced external was 0.02s vs GNU 0.03s, keyed numeric forced external was 0.04s vs GNU 0.07s.

## 07C status

- Merge-head fast paths are in place for whole-line numeric, general numeric, human numeric, month, and version merge, plus simple single default-blank numeric/general/human/month/version key merge.
- These paths keep per-run head metadata only, update it as heads advance, and write merged output directly with batched `writev(2)` instead of materializing a merged item array.
- The original Sprint 06 merge performance exceptions are fixed in the smoke matrix. Latest local run: `bench/results/merge-20260706232130.txt`; byte `-m` 0.01s vs GNU 0.01s, byte `-mu` 0.01s vs 0.02s, keyed numeric `-m -k2,2n` 0.03s vs 0.03s, version `-m -V` 0.03s vs 0.03s, keyed version `-m -k2,2V` 0.03s vs 0.03s.
- Broader keyed/special merge modes still use the in-memory cached path until they get matching head-cache implementations.

## 07D status

- `--batch-size=NMERGE` is enforced for external temp runs. When run count exceeds the batch size, rank merges runs in bounded groups into new private temp runs until the final merge fits the requested fan-in.
- Intermediate temp runs are cleaned after each merge pass. Golden coverage forces many runs with `--batch-size=2` and checks the temp directory is empty afterward.
- `--compress-program=PROG` writes external runs through `PROG` and decompresses them through `PROG -d` for merge passes. The current implementation materializes decompressed scratch runs before merging to keep the merge reader simple.
- Missing/unwritable temp directories and missing compressor failures have golden coverage.
- External temp files are registered for cleanup on `HUP`, `INT`, `TERM`, and `QUIT` where available.
- Golden coverage includes a practical `SIGTERM` cleanup case using a sleeping compressor.
- `bench/run-external.sh` now includes low-memory `--batch-size=2` and gzip-compressed cases.
- Latest local external benchmark: `bench/results/external-20260706235335.txt`; byte forced external was 0.02s vs GNU 0.03s, keyed numeric was 0.03s vs GNU 0.07s, `--batch-size=2` was 0.03s vs GNU 0.19s, gzip temp runs were 0.30s vs GNU 2.23s.
- Remaining known limitation: compressor integration assumes `PROG` compresses stdin to stdout and `PROG -d` decompresses stdin to stdout. This covers gzip-style tools but is not yet a full GNU compressor compatibility audit.

## Build targets

- Parse and enforce `-S SIZE`, `--buffer-size=SIZE`, `-T DIR`, `--temporary-directory=DIR`, `--batch-size=NMERGE`, and `--compress-program=PROG`.
- Implement memory budgeting for input bytes, line records, key metadata, transformed keys, radix aux, and output buffers.
- Generate sorted temp runs when budget is exceeded.
- Merge temp runs with bounded file descriptors and `--batch-size`. Basic batch-size enforcement is in place.
- Use temp directories in GNU-compatible order and diagnostics.
- Pipe temp runs through `--compress-program` where requested. Basic compressor/decompressor support is in place for programs that accept `-d`.
- Clean temp files on normal exit and fatal signals. Signal cleanup is best-effort and covers the external temp registry.
- Preserve input/output alias safety under external sorting.

## Implementation notes

- Run generation pipeline: read budgeted chunk, precompute keys, choose plan, sort, optionally safe local unique, write temp run.
- Final unique still needs global merge-time duplicate handling.
- Temp files must be private and robust against signals.
- Budgeting does not need to match GNU RSS exactly, but option syntax and errors must match.
- Keep run file format simple: original records plus terminators unless measurement proves a keyed format is worth it.

## Parity tests

- Force external sort with tiny `-S` on default, keyed, numeric, and `-u` inputs.
- Multiple `-T` dirs, missing dirs, permission errors, bad sizes, bad batch sizes.
- `--compress-program` success and missing executable/nonzero exit.
- Signals or simulated fatal cleanup paths where practical.
- `-o` aliasing when external runs are needed.
- `-z` external sorting.

## Performance notes

- Required benchmarks: external `-S 1M`, path list, CSV key sort, long-prefix records, duplicate-heavy `-u`.
- Track temp bytes written, number of runs, merge passes, peak RSS, and wall time.
- Large external sorts can be I/O-bound; rank should still reduce CPU time in run generation.
- Avoid flushing tiny writes to temp files.

## Pitfalls

- Do not apply `-u` only within runs and call it done.
- Do not leak temp files on errors.
- Do not exceed file descriptor limits when many runs exist.
- Do not store pointers into freed run arenas.

## Exit criteria

- External sorting matches GNU for the sprint matrix.
- Temp cleanup works on normal and error exits.
- Forced external benchmark does not lose to GNU on core workloads without an explicit tracked issue.
- Sanitizers are clean.
