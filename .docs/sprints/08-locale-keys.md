# Sprint 08 - Locale Keys

## Objective

Handle locale collation without giving up speed where exact transformed keys are possible. Fall back when libc cannot represent the ordering safely.

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
