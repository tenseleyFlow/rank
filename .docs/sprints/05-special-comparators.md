# Sprint 05 - Special Comparators

## Objective

Implement GNU-compatible special ordering modes before optimizing them: numeric, general numeric, human numeric, month, version, and random.

## Build targets

- Implement `-n` numeric comparison with cached parse metadata.
- Implement `-g` general numeric comparison using `strtold()`-compatible behavior.
- Implement `-h` human numeric suffix ordering and numeric metadata.
- Implement `-M` month parsing using `LC_TIME` month names.
- Implement `-V` GNU-compatible version comparison. Do not rely on platform `strverscmp`.
- Implement `-R` random sort grouping and `--random-source=FILE` semantics.
- Wire `--sort=WORD` aliases.
- Apply these modes to whole lines and keys.

## Implementation notes

- Start comparator-first. Fast bucketing comes later only after parity locks.
- Cache metadata per line/key. Do not parse numbers, months, versions, or random hashes on every compare.
- Keep locale categories explicit: `LC_NUMERIC` for numeric, `LC_TIME` for month.
- Treat NaNs, infinities, signs, thousands separators, decimal separators, suffix order, and unknown months as parity cases.
- Add focused unit tests for each parser/comparator.

## Parity tests

- `-n`: signs, leading blanks, leading zeros, decimals, thousands separators, huge digit counts, non-numbers.
- `-g`: infinities, NaNs, exponents, hex/locale behavior where GNU accepts it.
- `-h`: K/M/G suffixes, powers, signs, unit-before-magnitude cases.
- `-M`: localized month names, folded case, unknown months.
- `-V`: digit runs, leading zeros, punctuation, empty components, GNU edge fixtures.
- `-R`: repeated equal keys, deterministic `--random-source`, error paths.
- Every mode with `-k`, `-t`, `-r`, `-s`, and `-u` where GNU accepts the combination.

## Performance notes

- The win in this sprint is cached metadata, not radix.
- Required benchmarks: integer `-n`, decimal `-n`, human units `-h`, versions `-V`, random sort grouping.
- Track parse counts. Each line/key should parse once per run.
- Numeric integer bucketing may be prototyped behind a disabled plan flag, but do not enable until GNU parity is deep.

## Pitfalls

- Do not convert `-n` to floating point.
- Do not assume `-h` compares scaled byte values only. GNU unit ordering matters.
- Do not substitute libc `strverscmp` for GNU `filevercmp` semantics.
- Do not shuffle equal `-R` keys independently.

## Exit criteria

- Special ordering modes match GNU for the sprint matrix.
- Metadata caching is in place and measured.
- Complex modes remain comparator-planned unless an optimized path is proven exact.
- Sanitizers are clean.
