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

## Current status

- Sprint 05 is closed. All exit criteria are met for the sprint matrix, with the explicit temporary `-R` hash-order exception documented below.
- 05A started with scalar `-n` numeric ordering.
- `-n` and `--numeric-sort` parse as global numeric sort mode.
- Key modifier `n` parses for `-k` definitions.
- Numeric plans force scalar fallback with reason `special comparator`; radix fast paths stay disabled for special modes.
- Current numeric comparator handles C-locale signs, leading blanks, leading zeros, integer length, and decimal fractions without converting to floating point.
- Golden coverage compares against GNU for whole-line `-n`, keyed `-k2,2n`, global `-n -k2,2`, reverse, and unique keyed numeric output.
- Numeric metadata caching is in place for whole-line numeric sorts and keyed numeric sorts. Metadata is built once during `rank_lines_prepare_keys()` and scalar comparison reads cached values.
- Expanded `-n` golden coverage includes leading zeros, decimals, leading blanks, nonnumeric prefixes, keyed numeric, global numeric with keys, reverse, and unique.
- `--sort=numeric` and `--sort=n` map to `-n`.
- 05B started with scalar `-g` general numeric ordering.
- `-g`, `--general-numeric-sort`, `--sort=general-numeric`, and key modifier `g` parse and force scalar fallback.
- General numeric metadata is cached for whole-line and keyed sorts using `strtold()` in C locale. Current class order matches the initial GNU fixtures: nonnumeric, NaN, numeric.
- Initial `-g` golden coverage includes exponents, infinities, NaNs, nonnumeric strings, keyed general numeric, reverse, and `--sort=general-numeric`.
- Expanded `-g` coverage includes signed NaNs, hex floats, overflow/underflow, invalid prefixes, and `--sort=g`.
- 05C started with scalar `-h` human numeric ordering.
- `-h`, `--human-numeric-sort`, `--sort=human-numeric`, `--sort=h`, and key modifier `h` parse and force scalar fallback.
- Human numeric metadata is cached for whole-line and keyed sorts. Current coverage includes common binary suffixes, signs, suffix variants, invalid/NaN-like zero behavior, keyed human numeric, and `--sort=human-numeric`.
- Expanded `-h` coverage includes all GNU-observed uppercase units through `Q`, K/Ki/KB variants, decimals, unknown suffixes, and lowercase `q`/`r` behavior.
- 05D started with scalar `-M` month ordering.
- `-M`, `--month-sort`, `--sort=month`, `--sort=M`, and key modifier `M` parse and force scalar fallback.
- Month metadata is cached for whole-line and keyed sorts. Current implementation covers C-locale English month names by folded three-letter prefixes, with unknown months ordered before known months.
- Expanded `-M` coverage includes full names, mixed case, keyed reverse, and keyed unique survivor behavior.
- 05E started with scalar `-V` version ordering.
- `-V`, `--version-sort`, `--sort=version`, `--sort=V`, and key modifier `V` parse and force scalar fallback.
- Current `-V` implementation follows GNU `filevercmp` structure for counted byte strings: leading-dot special cases, suffix-prefix pass, Debian-style reverse comparison, tilde ordering, numeric runs, and last-resort byte comparison outside `-s`/`-u`.
- Expanded `-V` coverage includes tilde chains, punctuation context after digit runs, large digit runs, leading punctuation, case, release suffixes, empty components, dot-special names, suffix restore behavior, Debian-ish package revisions, and selected gnulib `filevercmp` examples.
- `-R`, `--random-sort`, `--sort=random`, `--sort=R`, key modifier `R`, and `--random-source=FILE` parse and force scalar fallback.
- `-R` currently uses a rank-specific cached 64-bit hash over each whole line or key span. The default seed is fixed for reproducible tests; `--random-source` hashes file bytes into the seed. This is an explicit temporary exception, not byte-identical GNU random ordering. GNU-compatible MD5 seeding remains future work.
- Current `-R` unit coverage checks deterministic output, `--sort=random`, seed-file influence, keyed random sort, and scalar planning. Golden tests intentionally do not compare exact `-R` order against GNU yet.
- Special comparator smoke benchmark added as `bench/run-special.sh`. Latest local run: `bench/results/special-20260706204126.txt`. Results: `-n` 0.01s vs GNU 0.02s, `-g` 0.03s vs 0.33s, `-h` 0.02s vs 0.03s, `-k1,1M` 0.02s vs 0.06s, `-V` 0.06s vs 0.08s, `-R` 0.02s vs 0.32s.

## Implementation notes

- Start comparator-first. Fast bucketing comes later only after parity locks.
- Cache metadata per line/key. Do not parse numbers, months, versions, or random hashes on every compare.
- Keep locale categories explicit: `LC_NUMERIC` for numeric, `LC_TIME` for month.
- Treat NaNs, infinities, signs, thousands separators, decimal separators, suffix order, and unknown months as parity cases.
- Add focused unit tests for each parser/comparator.
- `-V` behavior is matched against GNU/gnulib `filevercmp`; rank's implementation is local counted-byte code and does not import gnulib source.

## Parity tests

- `-n`: signs, leading blanks, leading zeros, decimals, thousands separators, huge digit counts, non-numbers.
- `-g`: infinities, NaNs, exponents, hex/locale behavior where GNU accepts it.
- `-h`: K/M/G suffixes, powers, signs, unit-before-magnitude cases.
- `-M`: localized month names, folded case, unknown months.
- `-V`: digit runs, leading zeros, punctuation, empty components, GNU edge fixtures, file suffix pass, leading-dot cases.
- `-R`: repeated equal keys, deterministic `--random-source`, keyed random sort, error paths. Exact GNU random order is deferred.
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

- Special ordering modes match GNU for the sprint matrix, except the documented temporary `-R` hash-order exception.
- Metadata caching is in place and measured.
- Complex modes remain comparator-planned unless an optimized path is proven exact.
- Sanitizers are clean.

## Closeout

- `gmake check`, `gmake sanitize`, and `git diff --check` passed at closeout.
- Optimized build was restored after sanitizer.
- Sprint 06 starts with check mode before merge mode.
