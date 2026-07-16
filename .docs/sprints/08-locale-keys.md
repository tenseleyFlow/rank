# Sprint 08 - Locale Keys

## Objective

Handle locale collation without giving up speed where exact transformed keys are possible. Fall back when libc cannot represent the ordering safely.

## Current status

- Sprint 08 is closed locally.
- Tracked exceptions move to Sprint 09 speed work: `-f`/`-d`/`-i` are scalar-only and slower than GNU in the locale benchmark, transformed radix is disabled for `-r`/`-u`, and embedded-NUL locale spans fall back to byte comparison.

## Subsprints

- 08A locale setup and scalar collation: call `setlocale` like GNU sort, replace env-name identity checks with a locale module/probe, and use scalar `strcoll` for non-identity C-string-safe spans.
- 08B packed transformed keys: generate length-addressed `strxfrm` keys for whole-line and key spans where exact, then sort transformed bytes.
- 08C transformed radix plans: add `PLAN_RADIX_TRANSFORMED` and route exact transformed-key cases through radix.
- 08D ASCII modifiers and locale matrix: add exact `-f`, `-d`, and `-i` support for C/POSIX semantics and expand locale/debug/perf coverage.

## 08A status

- Locale setup now calls `setlocale(LC_ALL, "")` after option validation and before plan selection.
- Project locale declarations moved to `src/rank_locale.h` so they do not shadow the system `<locale.h>` header.
- Plan selection uses a cached collation identity probe instead of raw environment-name checks. `C` and `POSIX` are identity; other single-byte locales must prove byte-order equivalence for every non-NUL byte. Multibyte locales are scalar until transformed-key support lands.
- Scalar byte comparison uses `strcoll` in non-identity locales when both spans are C-string-safe. Spans with embedded NUL fall back to byte comparison until a counted-byte locale path is proven exact.
- Unit coverage checks `C.UTF-8` plans fall back to scalar when available. Golden coverage compares whole-line and keyed UTF-8 sorting against GNU sort for available `C.UTF-8` and `en_US.UTF-8` locales.

## 08B status

- `rank_lines` now has packed transformed-key storage: one transform byte arena plus length-addressed whole-line and per-key transform spans.
- Non-identity byte sorts precompute `strxfrm` output for whole lines. Keyed non-identity byte sorts precompute `strxfrm` output for byte-mode key spans and whole lines for last-resort comparison.
- Transforms are generated only for C-string-safe spans. Spans containing embedded NUL keep `valid=false` and fall back to scalar locale comparison, which currently falls back to byte comparison for those spans.
- Scalar comparison uses packed transformed spans when both sides are valid, avoiding repeated `strcoll` calls. `PLAN_RADIX_TRANSFORMED` remains 08C.
- Existing locale golden cases cover transformed whole-line and keyed comparison against GNU sort for available UTF-8 locales.

## 08C status

- `RANK_PLAN_RADIX_TRANSFORMED` is in place for exact whole-line locale byte sorts.
- Plan selection routes non-identity whole-line byte sorts without `-r` or `-u` to transformed radix. Single-key non-identity byte sorts without reverse or `-u` also use transformed radix.
- The transformed radix is stable and sorts `struct rank_line` records by precomputed `strxfrm` spans. If any record lacks a valid transform, it declines and sort falls back to scalar comparison.
- Unit coverage checks `C.UTF-8` whole-line and keyed sorts select `radix-transformed` and emit transformed-radix stats.
- Remaining 08C/08D work: reverse/unique proof and broader locale performance benchmarks.

## 08D status

- `-f`, `--ignore-case`, `-d`, `--dictionary-order`, `-i`, and `--ignore-nonprinting` now parse globally.
- Key modifiers `f`, `d`, and `i` now parse in `-k` definitions.
- Text modifiers currently force scalar planning. The scalar comparator implements C/POSIX ASCII semantics: `-f` folds `a-z` to `A-Z`, `-d` keeps only blanks and alphanumeric bytes, and `-i` keeps only printable ASCII bytes.
- Golden coverage compares global and key-local `-f`, `-d`, and `-i` behavior against GNU sort under `LC_ALL=C`.
- `bench/run-locale.sh` covers whole-line transformed locale sorting, transformed keyed locale sorting, and C-locale `-f -d`.
- Remaining limitation: text modifiers are scalar-only. Fast filtered transform/radix storage is deferred.
- Latest local locale benchmark: `bench/results/locale-20260707002747.txt`; whole-line `C.UTF-8` was 0.01s vs GNU 0.02s, keyed `C.UTF-8` was 0.03s vs GNU 0.04s, scalar `LC_ALL=C -f -d` was 0.03s vs GNU 0.01s. The `-f -d` loss is tracked until filtered transform/radix storage lands.

## Build targets

- Set locale categories like GNU sort.
- Implement and test collation identity probing.
- Add packed `strxfrm` key generation for whole-line and key spans where exact.
- Add `PLAN_RADIX_TRANSFORMED` for transformed byte keys.
- Add transformed-key support for ASCII-safe `-f`, `-d`, and `-i` under C/POSIX semantics.
- Detect embedded-NUL and other cases requiring comparator fallback.
- Add locale-aware debug/golden test support.

## Implementation notes

- C and POSIX collations are byte identity by definition.
- `C.UTF-8` and similar locales require probing. Do not assume.
- Packed transformed keys must be length-addressed, not NUL-terminated.
- `strxfrm` APIs are C-string based; if input shape cannot be represented exactly, do not use this fast path.
- Keep scalar locale comparator as the oracle.

## Parity tests

- `LC_ALL=C`, `LC_ALL=POSIX`, available `C.UTF-8`, and at least one nontrivial UTF-8 collation locale.
- UTF-8 text, invalid UTF-8, embedded NULs, long keys, empty keys.
- Locale collation with default sort and `-k`.
- `-f`, `-d`, `-i`, and combinations under C/POSIX and UTF-8 locales.
- Fallback cases verified against scalar comparator and GNU.

## Performance notes

- Required benchmarks: UTF-8 hard locale, `C.UTF-8` identity-probed byte sort, transformed-key sort, ASCII `-f` and `-d`.
- Track transform bytes generated, transform time, fallback counts, and radix time.
- The fast path is transform once plus radix, not `strcoll` per comparison.

## Pitfalls

- Do not implement custom Unicode collation.
- Do not use `strxfrm` on raw records with embedded NUL and pretend it is exact.
- Do not treat invalid UTF-8 inconsistently with GNU/libc behavior.
- Do not let locale-sensitive classification leak into C-locale byte paths.

## Exit criteria

- Locale behavior matches GNU for the available locale matrix.
- Identity locales use byte radix.
- Exact transformed-key cases use packed keys and beat comparator sort where measured.
- Hard cases fall back cleanly.
- Sanitizers are clean.
