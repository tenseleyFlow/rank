# Sprint 06 - Check And Merge

## Objective

Implement modes that must not sort input: check mode and merge mode. Keep them streaming and key-aware.

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
