# Sprint 06 - Check And Merge

## Objective

Implement modes that must not sort input: check mode and merge mode. Keep them streaming and key-aware.

## Current status

- Sprint 06 started after Sprint 05 closeout.
- Initial implementation order: check mode first, then merge mode.
- Check mode may reuse the current in-memory record reader initially; streaming optimization is required before sprint closeout.
- First check-mode slice is in place: `-c`, `-C`, `--check=diagnose-first`, `--check=quiet`, and `--check=silent` parse and compare adjacent records.
- Check mode now uses a streaming reader instead of loading the full input. It keeps only previous/current record buffers and reuses them across records.
- No-key byte check has a direct byte comparator fast path. Keyed and special comparator checks use the existing comparator with two-record metadata.
- Current check coverage includes sorted input, stdin diagnostics, disorder diagnostics, quiet mode, reverse order, numeric keyed checks, duplicate disorder under `-cu`, keyed duplicate disorder, zero-terminated diagnostics, long records crossing read chunks, and version checks.
- GNU-compatible check operand validation is in place: more than one operand with `-c` is rejected before reading input.
- Check benchmark smoke added as `bench/run-check.sh`. Latest local run: `bench/results/check-20260706205915.txt`; sorted byte `-c` tied GNU at timer resolution, early disorder tied, keyed numeric remains slower because it uses generic per-pair metadata.
- Generic check comparisons now reuse a two-record `rank_lines` context and metadata buffers. Latest local run after reuse: `bench/results/check-20260706211523.txt`; keyed numeric improved to 0.02s vs GNU 0.01s.
- Initial merge-mode slice is in place: `-m`/`--merge` parse, inputs are read as already-sorted runs, and a binary heap merges heads without sorting each run.
- Current merge coverage includes two-file byte merge, many-file byte merge, stdin plus file merge, reverse merge, `-u` duplicate suppression across file boundaries, keyed numeric merge, and zero-terminated merge.
- Merge benchmark smoke added as `bench/run-merge.sh`. Latest local streaming byte run: `bench/results/merge-20260706213835.txt`; 16-run merge was 0.01s vs GNU 0.02s, and 16-run unique merge was 0.01s vs GNU 0.03s.
- No-key byte merge now uses streaming run heads and writes output directly. It keeps one current record buffer per input and uses a binary heap over active heads.
- Expanded merge benchmark: `bench/results/merge-20260706221526.txt`. Byte `-m` was 0.01s vs GNU 0.03s, byte `-mu` was 0.01s vs 0.03s, keyed numeric `-m -k2,2n` was 0.07s vs 0.03s, and version `-m -V` was 0.05s vs 0.04s.
- Keyed and special-comparator merge still use the in-memory run path. Current coverage includes numeric, month, version, keyed numeric, keyed unique, stable keyed merge, output-file merge, and zero-terminated merge. Metadata is cached once for all loaded records, so comparator hot paths do not re-extract keys.
- Keyed/special streaming merge-head caches are deferred. This is a tracked Sprint 06 performance exception for keyed numeric and version merge; correctness is covered against GNU.

## Closeout

- Sprint 06 is closed for the planned matrix with one tracked performance exception: keyed/special merge still uses the in-memory cached path instead of streaming per-head key caches.
- `gmake check`, `gmake sanitize`, and `git diff --check` passed at closeout.
- Optimized build was restored after sanitizer.
- Sprint 07 starts with external sorting and should revisit keyed/special merge-head caches when run generation and k-way external merge share the same head abstraction.
- Remaining check work before closeout: broader special comparator/key matrices and deeper keyed/special metadata reuse.

## Build targets

- Implement `-c`, `--check`, and `--check=diagnose-first`.
- Implement `-C`, `--check=quiet`, and `--check=silent`.
- Emit GNU-compatible disorder diagnostics and exit codes.
- Implement `-m` merge of already sorted inputs.
- Add k-way merge using a heap or loser tree.
- Cache current key metadata for each merge head.
- Implement merge-time `-u` duplicate suppression.
- Support keys, reverse, stable/unique semantics, record terminators, and special comparators in check/merge modes.

## Implementation notes

- Check mode compares adjacent records and stops as GNU does.
- Merge mode does not sort each input. It assumes each input is sorted according to the active comparator.
- Use the same comparator/planner semantics as sort mode.
- For `-m`, read only what is needed from each input stream. Do not load every file by default.
- Merge unique must compare active keys according to GNU's `-u` semantics.

## Parity tests

- Sorted, unsorted, and equal-key inputs for `-c` and `-C`.
- Disorder line diagnostics, including file names and record numbers.
- `-c` and `-C` with `-k`, `-t`, `-r`, `-s`, `-u`, `-z`, numeric, month, and version modes.
- `-m` with two files, many files, stdin, empty files, one-record files, duplicate boundaries.
- Merge unique across file boundaries.
- Operational errors vs disorder exit codes.

## Performance notes

- `-c` should be near streaming read speed on sorted input.
- `-m` should avoid recomputing key spans for the same head record across heap comparisons.
- Required benchmarks: sorted file `-c`, first-disorder early exit, merge sorted slices, merge with many duplicate keys.
- Compare heap vs loser tree after correctness lands.

## Pitfalls

- Do not allocate and sort in check mode.
- Do not sort inputs in merge mode.
- Do not report quiet check diagnostics for `-C`.
- Do not treat merge `-u` as per-file unique only.

## Exit criteria

- Check and merge modes match GNU for the sprint matrix.
- `-c` sorted-file benchmark is faster than or tied with GNU without extra allocation.
- Merge mode caches head keys and shows no repeated field extraction for unchanged heads.
- Sanitizers are clean.
