# Sprint 04 - Keyed Radix

## Objective

Make `-k` and `-t` fast. Precomputed keys from Sprint 02 become radix input, with group recursion for multi-key and last-resort ordering.

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
