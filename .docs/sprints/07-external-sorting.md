# Sprint 07 - External Sorting

## Objective

Handle inputs larger than the memory budget while preserving GNU temp-file behavior. Fast run sorting should reuse the radix and key-precompute paths.

## Build targets

- Parse and enforce `-S SIZE`, `--buffer-size=SIZE`, `-T DIR`, `--temporary-directory=DIR`, `--batch-size=NMERGE`, and `--compress-program=PROG`.
- Implement memory budgeting for input bytes, line records, key metadata, transformed keys, radix aux, and output buffers.
- Generate sorted temp runs when budget is exceeded.
- Merge temp runs with bounded file descriptors and `--batch-size`.
- Use temp directories in GNU-compatible order and diagnostics.
- Pipe temp runs through `--compress-program` where requested.
- Clean temp files on normal exit and fatal signals.
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
- `--compress-program` success, failure, missing executable, nonzero exit.
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
