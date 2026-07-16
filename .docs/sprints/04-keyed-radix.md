# Sprint 04 - Keyed Radix

## Objective

Make `-k` and `-t` fast. Precomputed keys from Sprint 02 become radix input, with group recursion for multi-key and last-resort ordering.

Sprint 04 starts after Sprint 03 proved the whole-line byte plan and benchmark matrix. The weak Sprint 03 workloads are structured records (`urls`, `paths`, `variable`), so this sprint should focus on skipping irrelevant bytes by sorting precomputed key spans.

## Build targets

- Add radix sorting over `key_span` instead of whole-line spans.
- Implement single-key byte radix for identity collation and no transforms.
- Implement multi-key group recursion.
- After final key, recurse into whole-line ordering unless `-s` or `-u` suppresses it.
- Support per-key reverse where byte bucket ordering can model it exactly.
- Support explicit separator and default blank-field key spans.
- Add plan diagnostics in debug builds: why radix was chosen or rejected.

## Implementation notes

- Key extraction must happen once before sorting.
- Equal-key groups are first-class data. Do not rely on incidental adjacency without recording group ranges.
- Per-key reverse is not the same as reversing final output.
- Use the scalar keyed comparator for small buckets, fallback groups, and verification.
- Keep transformed flags out of this sprint except routing to scalar.
- Do not reuse Sprint 03's unstable in-place partition for key plans until survivor and stability semantics are proven. Start with stable scatter for keyed radix groups.
- Reuse compact records where possible, but key plan records need access to `key_index` or precomputed key spans.
- Use scalar keyed sort as the internal oracle before comparing with GNU.

## Starting plan

1. Add a keyed radix plan kind gated to identity collation, `key_count > 0`, no debug output, and bytewise key flags only.
2. Implement single-key stable byte radix over precomputed key spans.
3. For equal-key groups, apply whole-line fallback unless `-s` or `-u` suppresses it.
4. Add debug verification comparing keyed radix output to scalar keyed output.
5. Add CSV/TSV/log key benchmarks before tuning.

## Subsprints

- 04A - Stable single-key radix: gate to `-s`, one key, identity collation, no reverse, no `-u`; sort precomputed key spans with stable scatter.
- 04B - Last-resort groups: enable non-`-s` single-key plans only after equal-key whole-line groups beat GNU on duplicate-heavy benchmarks.
- 04C - Multi-key recursion: stable N-key recursion, sorting equal prior-key groups by the next key without rescanning fields.
- 04D - Reverse and unique: exact reverse ordering and keyed `-u` survivor semantics.
- 04E - Keyed benchmark gate: CSV, duplicated keys, TSV multi-key, log field sort, and Sprint 03 structured weak cases recast as key sorts.

## Current status

- 04A is complete: `RANK_PLAN_RADIX_KEYS` handles stable single-key byte sorts.
- 04B is complete: non-stable single-key byte sorts use stable whole-line radix inside equal-key groups for last-resort ordering.
- 04C is complete for stable N-key byte sorts. Recursion stays range-local so later keys only reorder records equal on all prior keys. Non-stable multi-key and `-u` still fall back to scalar.
- 04C benchmark `bench/results/keyed-20260706170517.txt`: stable two-key rank `25.6 ms`, GNU `41.6 ms`; rank is `1.62x` faster. Stable three-key rank `37.6 ms`, GNU `78.9 ms`; rank is `2.10x` faster.
- 04B benchmark in the same run: stable CSV `1.07x`, stable duplicate keys `1.50x`, non-stable duplicate keys `1.01x` faster than GNU. The non-stable CSV case was noisy in the full run; a focused 10-run rerun showed rank `2.19x` faster than GNU.
- 04D reverse ordering is complete for stable keyed radix and non-stable single-key per-key reverse. Global reverse on non-stable non-unique keyed sorts still falls back to scalar because last-resort whole-line ordering would also need to be reversed.
- 04D benchmark `bench/results/keyed-20260706170958.txt`: stable N-key global reverse rank `33.4 ms`, GNU `77.8 ms`; rank is `2.33x` faster. Stable mixed per-key reverse rank `37.7 ms`, GNU `203.7 ms`; rank is `5.41x` faster.
- Focused 10-run reruns after 04D showed non-stable single-key CSV rank `2.16x` faster than GNU and duplicate-key rank `2.11x` faster than GNU.
- 04D keyed `-u` is complete for identity collation. Golden tests pin GNU survivor behavior: equal keys keep the first input record, not the smallest full line. Radix therefore skips last-resort whole-line grouping for `-u`.
- 04D unique benchmark `bench/results/keyed-20260706171343.txt`: single-key unique rank `13.6 ms`, GNU `19.0 ms`; rank is `1.39x` faster. Reverse unique rank `13.2 ms`, GNU `16.7 ms`; rank is `1.27x` faster. N-key unique rank `43.7 ms`, GNU `87.9 ms`; rank is `2.01x` faster.
- 04B/04C implementation note: equal-key groups use stable whole-line radix over full `rank_line` records so `key_index` and `ordinal` stay attached to records.
- 04E is complete: `bench/run-keyed.sh` now gates CSV, duplicate keys, TSV multi-key, N-key, reverse, unique, log field, path component, URL field, and long shared-prefix key workloads.
- 04E added key LCP skipping and stable single-key monotonic detection. Shared-prefix key sort improved from GNU `4.05x` faster than rank to rank `1.29x` faster than GNU.
- 04E benchmark `bench/results/keyed-20260706172638.txt`: TSV multi-key `1.96x`, log service field `1.66x`, path components `3.33x`, URL field `1.23x`, and shared-prefix key `1.29x` faster than GNU. Stable CSV had one noisy full-run outlier; focused and adjacent runs showed rank faster.
- 04E follow-up implemented one-pass field-span extraction for multiple default blank-separated keys and range-local monotonic detection for recursive key groups.
- 04E follow-up benchmark `bench/results/keyed-20260706174115.txt`: late default-blank multi-key log sort (`-k5,5 -k6,6`) rank `20.4 ms`, GNU `21.5 ms`; rank is `1.05x` faster in the full gate. Focused reruns were borderline, around GNU `1.06x` faster, so this shape remains a narrow variance risk for Sprint 09/low-level key work.

## Parity tests

- CSV and TSV: `-t, -k2,2`, `-t '\t' -k1,1 -k3,3`.
- Multi-key ties requiring the second key.
- Multi-key ties requiring last-resort whole-line comparison.
- `-s` suppressing last-resort compare.
- `-u` equality by active key, not full line.
- Per-key `r`, global `-r`, and mixed reverse cases.
- Empty fields, missing fields, open-ended keys, long keys, many duplicate keys.
- Radix output vs scalar output in debug builds.

## Performance notes

- This should be the largest visible win for common CLI usage.
- Required benchmarks: CSV key sort, multi-key TSV, logs keyed by field, duplicated primary keys, long common field prefixes.
- Track field scans. They must not grow with comparator calls.
- Track radix-classified key bytes and fallback group sizes.
- Include Sprint 03 weak shapes recast as keyed sorts: URL/log field sort, path component sort, and variable-length records keyed by a fixed field.

## Pitfalls

- Do not recompute fields during radix recursion.
- Do not lose stability between key stages.
- Do not apply full-line fallback when `-s` or `-u` suppresses it.
- Do not use unstable in-place partitioning for key plans.

## Exit criteria

- Keyed radix matches scalar and GNU for the sprint matrix.
- Keyed radix beats GNU sort on CSV and TSV key benchmarks.
- Plan fallback reasons are observable in debug builds.
- Sanitizers are clean.

## Closeout

- Sprint 04 closes with keyed radix enabled for identity-collation byte keys, including stable N-key recursion, exact reverse ordering, keyed `-u`, key LCP skip, default blank multi-key field-span caching, and key monotonic detection.
- Unsupported or not-yet-proven keyed shapes still fall back to scalar: non-identity collation, debug output, and non-stable non-unique global reverse with last-resort whole-line ordering.
- Proceed to Sprint 05 after final sanitizer and benchmark checks pass.
