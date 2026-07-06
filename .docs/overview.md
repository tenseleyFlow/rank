# rank - overview

A from-scratch C reimplementation of GNU `sort(1)`. Parity target: coreutils sort
(latest stable). Output matches GNU sort byte for byte; it should be faster on every
workload measured, with the largest wins on bytewise string sorts, keyed field sorts,
and inputs with long shared prefixes. Binary: `rank`.

This is part of a family of "same output, fewer cycles" C rewrites under
`tenseleyFlow/`: `aspen` (tree), `ferret` (find), `tally` (wc), and now `rank`
(sort). Each targets byte-identical parity with the GNU original and a measurable speed
win on every workload, enforced by a CI perf gate. The stack, conventions, and testing
philosophy are shared across the family.

Read alongside the stage-1 audits (when written) in `.docs/audits/`. This document is
the load-bearing design; the sprint files in `.docs/sprints/` implement it incrementally.
The source references cloned during research live in `.docs/refs/`.

---

## 1. Design tenets

1. Parity first, fast second, but never slow. rank is a drop-in for GNU sort. Stdout,
   stderr, diagnostics, temp-file behavior, and exit codes match coreutils sort for every
   supported invocation. Speed never regresses parity. Where GNU sort has a genuine bug
   (deterministic but wrong), rank does the correct thing and documents the deviation in
   `.docs/deviations.md`.
2. Beat sort on every workload. The CI perf gate enforces this. The most important
   benchmarks are default bytewise sorting, `-k`/`-t` field sorting, numeric sorting,
   external sorting under `-S`, and merge/check modes because those cover the common
   command-line shapes.
3. Radix is the thesis, not a polish step. GNU sort is a comparison sorter. For byte
   strings and extracted byte keys, rank uses MSD radix passes so common prefixes are
   scanned once per level instead of re-compared O(log n) times. Without this path, rank
   is just another sort.
4. Precompute what GNU recomputes. Key ranges, transformed key slices, numeric parse
   facts, and locale collation keys are per-line data. Do not redo `-k` field walking,
   case folding, dictionary filtering, or numeric parsing inside every comparator call.
5. Few deps, small surface. libc only. No autotools, no CMake. SIMD is allowed through
   compiler intrinsics for delimiter scans and bounded byte comparison, but the first
   win is algorithmic: fewer comparisons and fewer key re-scans.
6. Portable by construction. Linux (glibc/musl), FreeBSD 15 (primary dev box), macOS
   arm64/x86_64. Platform differences live behind a thin `sys/` abstraction and
   compile-time feature detection.

## 2. The performance thesis

`sort` spends most of its time doing comparisons and the work each comparison triggers.
For N lines, a comparison sorter performs O(N log N) comparator calls. Each comparator
may scan the same long prefix, extract the same field, parse the same number, or invoke
the same locale collation machinery again.

rank avoids this in the common cases:

1. Default bytewise sort. In C/POSIX collation, and in identity-collating locales such as
   the common `C.UTF-8` case, GNU sort eventually compares raw bytes. It still performs
   a merge sort and rechecks shared prefixes on every comparison. rank uses an MSD
   byte-radix sorter: bucket by byte at depth D, recurse only inside non-singleton
   buckets, and treat end-of-key as bucket 0. This changes the hot cost from repeated
   `memcmp` on the same prefix to near-linear scans over the line/key bytes.
2. Keyed sorts (`-k`, `-t`). GNU sort precomputes only the first key boundary in
   `fillbuf()`. Subsequent keys are found by `begfield()` and `limfield()` during
   comparator calls, so multi-key sorts repeatedly walk fields. BusyBox and toybox are
   worse: they allocate/copy key substrings inside the qsort comparator. rank builds a
   per-line key plan once, then sorts using spans into the input or packed transformed
   keys.
3. Long common prefixes. Logs, paths, URLs, timestamps, zero-padded IDs, UUID-like
   strings, CSV rows, and fixed-width records are bad for comparison sort: a comparator
   rereads the prefix until it finds a differing byte. MSD radix consumes those shared
   bytes once per active bucket.
4. Locale collation. GNU's normal string comparison in a hard collation locale routes
   through collation-aware comparison on every compare. aspen already showed the right
   pattern for filenames: if `strxfrm` is an identity transform, use bytes; otherwise
   transform each string once and compare packed keys. rank does the same when it is
   safe for sort input. Lines containing embedded NUL bytes or cases where `strxfrm`
   cannot represent the key exactly fall back to the scalar GNU-compatible comparator.
5. Numeric sorts. GNU `-n` uses string numeric comparison, `-g` calls `strtold()`, and
   `-h` adds human-readable suffix logic. uutils improves this by caching numeric parse
   state per line. rank adopts that idea and keeps numeric comparator fallback first;
   later passes add numeric-key bucketing for the common integer and fixed-decimal cases.
6. External sort. GNU already chunks, sorts runs, writes temp files, and performs k-way
   merges. The run sort is still comparison-driven, and merge compares keys repeatedly.
   rank accelerates run generation with radix/key precompute and uses cached merge keys
   plus a loser tree or binary heap for final merging. Large inputs are I/O-bound, so the
   win is smaller than in-memory sort, but fewer CPU cycles per emitted line still matter.
7. I/O layer. GNU sort uses `FILE *` and `fread()` into a buffer, then `memchr()` to find
   line terminators. FreeBSD sort has an optional `--mmap` path. rank starts with
   buffered `read()` for correctness on pipes and devices, then adds `mmap` for large
   regular files when measurement shows a win. The parser keeps line storage contiguous
   and avoids per-line allocation.

The key shift is from "call a rich comparator many times" to "parse once, classify the
invocation, then choose the cheapest exact ordering engine." Comparator fallback remains
the parity oracle, but it should be cold on the workloads rank exists to speed up.

## 3. Reference implementation findings

Reference trees are cloned under `.docs/refs/`:

| Reference | Path | What it teaches |
|---|---|---|
| GNU coreutils | `.docs/refs/gnu-coreutils/src/sort.c` | Full parity target; merge-sort core; rich option and temp-file behavior |
| FreeBSD sort | `.docs/refs/freebsd-src/usr.bin/sort/` | Existing radix path, key preprocessing, optional mmap, wide-char costs |
| uutils sort | `.docs/refs/uutils-coreutils/src/uu/sort/` | Rust parity work, chunked external sort, per-line precompute, benchmark list |
| BusyBox sort | `.docs/refs/busybox/coreutils/sort.c` | Small implementation; shows cost of key copies inside qsort |
| toybox sort | `.docs/refs/toybox/toys/posix/sort.c` | Small implementation; simple qsort comparator and key-copy path |
| aspen sort | `../aspen/src/sort.c` | Local MSD byte-radix implementation to adapt for line/key spans |

Concrete observations:

- GNU line storage is good: it stores counted lines in a single buffer and keeps a line
  array at the end of the buffer. Keep that shape. Do not allocate one object per line.
- GNU's internal sorter is a recursive merge sort (`sequential_sort()`), with a threaded
  merge tree for `--parallel`. It is stable enough for sort semantics but still performs
  comparison work at each merge edge.
- GNU `fillbuf()` uses `memchr()` to split input and NUL-terminates each line in place.
  It precomputes the first key's start/end only. rank should precompute every key needed
  by the selected plan.
- GNU `keycompare()` copies transformed keys into a stack/heap buffer when `-d`, `-f`,
  `-i`, locale collation, numeric, month, random, or version modes need NUL-terminated
  data. That is correct but expensive inside O(N log N) comparator calls.
- GNU `compare()` falls back to whole-line comparison when all keys compare equal unless
  `-s` or `-u` is active. This last-resort compare is easy to forget and must be modeled
  in radix plans.
- FreeBSD sort validates the radix idea. `radixsort.c` buckets by byte and only falls
  back to mergesort for tiny nodes, deep levels, multi-key leaves, and complex cases. Its
  design also shows the traps: wide-character conversion, dynamic per-level allocation,
  and fallback comparators can erase part of the win.
- FreeBSD precomputes keys into a `keys_array` per line. That is the right direction, but
  it stores `bwstring` keys and often pays wide-character costs. rank should keep byte
  spans and only materialize transformed bytes when a flag requires them.
- uutils precomputes selections, numeric parse facts, floats, and collation keys in
  `LineData`, then uses Rust parallel sort callbacks. Keep the precompute idea; replace
  comparator sorting for eligible cases.
- uutils benchmarking notes state the main truth plainly: most time is spent comparing
  lines, and comparison functions differ heavily by option set. rank's perf matrix must
  benchmark option classes separately, not one headline workload.
- BusyBox and toybox demonstrate what not to do for speed: `get_key()`/`get_key_data()`
  allocate and filter key copies for every comparator call. Their simplicity is useful for
  reading semantics, not performance.

## 4. Parity contract

Given the same input, environment, files, and flags, rank produces the same observable
behavior as GNU coreutils sort:

- Same sorted output bytes on stdout or the `-o` output file.
- Same stderr diagnostics, with only the program-name token normalized (`rank:` vs
  `sort:`) in golden tests.
- Same exit codes. GNU sort generally uses 0 for success, 1 for disorder in check mode,
  and 2 for serious errors.
- Same locale behavior for `LC_COLLATE`, `LC_CTYPE`, `LC_NUMERIC`, and `LC_TIME`.
- Same line model: default line terminator is newline; `-z` changes the terminator to
  NUL. The terminator is not part of the comparison key but is restored on output.
- Same behavior for binary data inside records. In newline mode, NUL bytes inside a line
  are data; in zero-terminated mode, newline bytes are data.
- Same key syntax and warnings, including obsolete `+POS -POS` key forms when GNU accepts
  them.
- Same last-resort comparison behavior: after all keys compare equal, compare the whole
  line unless `-s` or `-u` suppresses that fallback.
- Same stable and unique behavior. `-s` preserves input order among equal keys. `-u`
  outputs only one line per equal key group, using GNU's chosen survivor semantics.
- Same reverse behavior. Global `-r` and per-key `r` reverse only the comparisons they
  apply to, including the last-resort whole-line comparison.
- Same check/merge behavior for `-c`, `-C`, and `-m`; do not sort in those modes when GNU
  does not.
- Same temp-file behavior for external sorting, `-S`, `-T`, `--batch-size`, and
  `--compress-program`.
- Same input/output safety behavior: `sort -o file file` must not truncate the input
  before it has been read or safely copied.
- Same `--files0-from` behavior, including errors for extra operands, empty filenames,
  `-` inside the file list, and missing input.
- Same signal cleanup for temp files and graceful SIGPIPE behavior.

The golden suite compares rank vs GNU sort on generated corpora. The only normalized
difference is the program-name token on stderr diagnostics.

## 5. Parity surface (GNU coreutils sort)

### Ordering flags

| Short | Long | Meaning |
|---|---|---|
| `-b` | `--ignore-leading-blanks` | ignore leading blanks when finding/comparing keys |
| `-d` | `--dictionary-order` | compare only blanks and alphanumeric characters |
| `-f` | `--ignore-case` | fold lower case to upper case for comparison |
| `-g` | `--general-numeric-sort` | compare according to general numeric value (`strtold` semantics) |
| `-h` | `--human-numeric-sort` | compare numbers with human-readable suffixes |
| `-i` | `--ignore-nonprinting` | compare only printable characters |
| `-M` | `--month-sort` | compare month names using `LC_TIME` |
| `-n` | `--numeric-sort` | compare according to string numeric value |
| `-R` | `--random-sort` | sort by randomized hash, grouping equal keys |
| `-r` | `--reverse` | reverse comparison result |
| `-V` | `--version-sort` | compare version strings |
| | `--sort=WORD` | select one of `general-numeric`, `human-numeric`, `month`, `numeric`, `random`, `version` |
| | `--random-source=FILE` | seed random sort from FILE |

### Key and field flags

| Short | Long | Meaning |
|---|---|---|
| `-k KEYDEF` | `--key=KEYDEF` | sort by a key range and optional per-key modifiers |
| `-t SEP` | `--field-separator=SEP` | use SEP instead of blank-run field separation |
| | `--debug` | annotate key extraction and emit key warnings |

`KEYDEF` syntax is `F[.C][OPTS][,F[.C][OPTS]]`. Fields and characters are 1-based. A
missing end position means the key extends to end of line. End character 0 has special
GNU semantics. Per-key `OPTS` may include ordering modifiers such as `bdfghinrMV`. Global
ordering options are inherited by keys only when GNU would inherit them.

Default field splitting is not CSV-style. Without `-t`, fields are runs of nonblank
characters separated by blanks, and leading blanks are significant unless `-b` applies.
With `-t`, every occurrence of the separator delimits a field; adjacent separators create
empty fields.

### Operation and output flags

| Short | Long | Meaning |
|---|---|---|
| `-c` | `--check[=diagnose-first]` | check whether input is sorted, print first disorder |
| `-C` | `--check=quiet`, `--check=silent` | check whether input is sorted, print no disorder line |
| `-m` | `--merge` | merge already sorted inputs; do not sort each input |
| `-o FILE` | `--output=FILE` | write result to FILE |
| `-s` | `--stable` | disable last-resort whole-line comparison |
| `-u` | `--unique` | output one line per equal-key group by GNU rules |
| `-z` | `--zero-terminated` | use NUL as line delimiter |

### Resource and temp-file flags

| Short | Long | Meaning |
|---|---|---|
| `-S SIZE` | `--buffer-size=SIZE` | use SIZE memory for main sort buffer |
| `-T DIR` | `--temporary-directory=DIR` | write temp files under DIR; may be repeated |
| | `--batch-size=NMERGE` | merge at most NMERGE files at once |
| | `--compress-program=PROG` | compress temp files through PROG |
| | `--parallel=N` | use up to N concurrent workers |
| | `--files0-from=F` | read NUL-delimited input file names from F |

### Informational

`--help`, `--version`

### Parity gotchas

- `-o` opens/truncates output only after preserving any input file that aliases the
  output. GNU may copy the input to a temp file first.
- `--files0-from=F` cannot be combined with positional operands. It rejects zero-length
  filenames and `-` entries.
- `-` as a positional operand means stdin. The same token inside `--files0-from` is a GNU
  error.
- `--debug` is part of the parity surface. Its warnings, key underlines, locale messages,
  and numeric-field warnings need golden coverage even though debug output is not hot.
- `LC_COLLATE` changes default string ordering. `LC_NUMERIC` changes decimal and thousands
  separators for numeric modes. `LC_TIME` changes month names.
- Numeric keys can span fields. GNU warns under `--debug`, but still sorts using its
  numeric parser.
- `-n` and `-g` are different. `-n` is decimal string numeric comparison and does not
  accept all `strtold()` syntax. `-g` does.
- `-h` unit ordering comes before raw numeric magnitude for different units, matching GNU.
- `-R` groups identical keys before applying random hash ordering. Equal keys are not
  independently shuffled.
- `-u` equality is based on the active key comparison, not necessarily full-line identity.
- `-s` and `-u` suppress the last-resort full-line compare. This is visible with keys such
  as `sort -k1,1` where multiple input lines share field 1.
- Reverse can be global or per-key. Do not implement reverse by blindly reversing the final
  array unless the selected plan proves that is equivalent.
- In check mode, GNU stops on first disorder for `-c`, exits silently for `-C`, and uses
  different exit codes for disorder vs operational errors.
- Temp-file cleanup on fatal signals is observable in practice. Install handlers once the
  temp layer exists.

## 6. Architecture sketch

```
src/
  main.c          # argv -> options -> plan -> read/sort/merge/check -> output
  options.[ch]    # GNU-compatible parser, key syntax, inherited modifiers
  plan.[ch]       # classify invocation: radix, transformed-key radix, comparator fallback
  line.[ch]       # line records, input arena, line splitting, record terminator handling
  key.[ch]        # key range extraction, transformed-key packing, debug annotations
  radix.[ch]      # MSD byte-radix engine over line/key spans
  cmp.[ch]        # GNU-compatible scalar comparator and parity oracle
  numeric.[ch]    # -n/-g/-h parsers, cached numeric metadata, numeric comparator
  locale.[ch]     # locale setup, collation identity probe, strxfrm key generation
  merge.[ch]      # k-way merge, loser tree/heap, temp file fan-in
  external.[ch]   # run generation, -S policy, temp dirs, compression hooks
  check.[ch]      # -c/-C streaming order checks
  output.[ch]     # buffered/writev output, -o safety
  util.[ch]       # xmalloc, diagnostics, quoting helpers
  sys/
    file.[ch]     # open/read/mmap/stat/temp-file primitives
    cpu.[ch]      # optional SIMD/runtime feature detection
```

The planner is the center of the program. It receives parsed GNU options and produces one
of these plans:

1. `PLAN_RADIX_BYTES`: whole-line or key-span byte comparison in an identity collation
   locale, no transforms that require scalar fallback.
2. `PLAN_RADIX_TRANSFORMED`: exact transformed byte keys exist (`-f`, `-d`, `-i`, simple
   collation keys, or packed field keys) and can be sorted lexicographically.
3. `PLAN_RADIX_GROUPED`: sort a primary key by radix, then recursively sort equal-key
   groups by later keys or last-resort whole-line compare.
4. `PLAN_NUMERIC_FAST`: precomputed numeric metadata with comparator fallback first;
   later extended to radix/counting buckets for integer-heavy data.
5. `PLAN_CHECK`: streaming check, no sorting.
6. `PLAN_MERGE`: k-way merge of already sorted inputs, no run generation.
7. `PLAN_COMPARATOR`: scalar GNU-compatible comparator, used for complex locale, version,
   random, month, and any edge case not proven safe for radix.

The scalar comparator is required from M1 onward. Every optimized plan diff-tests against
it before diff-testing against GNU sort.

## 7. Core data model

### Line records

Store input bytes in large arenas and sort pointers/records, not copied strings:

```c
struct line {
    const unsigned char *text;
    uint32_t len;        /* bytes excluding record terminator */
    uint32_t ordinal;    /* input order for stable fallback and diagnostics */
    uint32_t key_index;  /* first key metadata index, if any */
    uint32_t flags;      /* embedded NUL, invalid UTF-8, needs fallback, etc. */
};
```

The input parser owns the backing memory. The sorter moves `struct line *` or compact
indices. Output writes `text[0..len]` plus the selected record terminator.

### Key metadata

For each line and each key that the plan needs:

```c
struct key_span {
    const unsigned char *ptr;
    uint32_t len;
    uint32_t transformed_off;  /* UINT32_MAX when not materialized */
    uint32_t aux_index;        /* numeric/month/version/random metadata */
};
```

Key extraction is a linear pass over each line. For no `-t`, build field starts by scanning
blank/nonblank transitions. For `-t`, scan separator bytes. For many keys, store field
start/end positions once and resolve all key ranges from that table.

Do not allocate a C string per key. Prefer spans into the original input. Materialize only
when a flag changes comparison bytes (`-f`, `-d`, `-i`, collation transform) or when a
NUL-terminated libc API requires a safe scratch copy in fallback code.

### Transformed key storage

Use one packed byte arena per run:

```c
struct key_arena {
    unsigned char *data;
    size_t len;
    size_t cap;
};
```

Each transformed key is length-addressed. Do not rely on NUL terminators because sort input
can contain NUL bytes in newline mode. If a libc collation API cannot handle the exact key
bytes, mark the line or plan as comparator-only.

## 8. Radix architecture

The first radix engine is a generalized version of aspen's `radix_msd()`:

```c
bucket 0       = end of key
bucket 1..256  = byte value + 1

radix_msd(lines, aux, n, key_id, depth):
    if n <= insertion_threshold:
        insertion sort with scalar span compare from depth
        return
    count bucket(key(line, key_id), depth) for every line
    prefix sum counts
    scatter to aux
    copy aux back
    for each non-empty byte bucket:
        recurse at depth + 1
```

Important details:

- End-of-key sorts before any byte for ascending order, matching `memcmp` prefix rules.
- For descending order, bucket iteration reverses and end-of-key sorts after real bytes.
- `-r` cannot always be implemented by reversing the final array because per-key reverse
  and last-resort comparison can differ. Only whole-line global reverse may use final
  reversal after proof.
- The sorter must be stable when the active comparison demands stable grouping (`-s`) or
  when a later key/last-resort stage expects input order to be preserved among equal keys.
  Use stable scatter; do not use in-place American-flag partitioning until parity tests
  prove it safe for a plan.
- Equal-key groups need explicit recursion into the next key. After the final key, recurse
  into whole-line fallback unless `-s`, `-u`, or random mode suppresses it.
- `-u` can emit one representative per equal-key group after grouping. Match GNU's
  survivor choice with golden tests before optimizing away duplicates during sorting.
- Deep recursion must be bounded. For extremely long equal prefixes, switch to scalar
  compare/insertion or iterative stack frames to avoid stack blowups.

### Radix eligibility in v0.1

Fast path first, broad path later:

1. Whole-line byte sort in identity collation, default options, newline and NUL record
   modes.
2. Whole-line byte sort with global `-r`, `-s`, and full-line `-u` where semantics are
   provably equivalent.
3. Single-key byte sort: `-k F[.C][,F[.C]]` with optional `-t`, no transforms, identity
   collation.
4. Multi-key byte sort with group recursion and correct last-resort whole-line compare.
5. Transformed byte sort for ASCII-only `-f`, `-d`, and `-i` under C/POSIX semantics.
6. Locale transformed-key sort when `strxfrm` can be used exactly for the input shape.
7. Numeric bucketing for common integer/fixed-decimal `-n` after comparator parity is
   locked.

Everything else uses `PLAN_COMPARATOR` until proven safe.

## 9. Locale and collation

Locale is not an afterthought. GNU sort's default order is locale-dependent. Many users
benchmark with `LC_ALL=C`, but the default shell locale is often UTF-8.

rank uses this policy:

1. Set locale exactly as GNU sort does. Record the active `LC_COLLATE`, `LC_CTYPE`,
   `LC_NUMERIC`, and `LC_TIME` categories for debug output and tests.
2. Probe whether collation is byte identity. C and POSIX are identity by definition.
   Locales such as `C.UTF-8` can be detected with a `strxfrm` identity probe, as aspen
   does.
3. If collation is identity, byte radix is exact.
4. If collation is not identity and the key bytes are safe for `strxfrm`, transform each
   key once into a packed arena and sort transformed keys.
5. If collation is hard and input contains embedded NULs or another case that prevents an
   exact transformed key, use comparator fallback.

Do not optimize by assuming UTF-8 locale equals byte order. It often does not. The probe
must decide, and golden tests must cover `C`, `C.UTF-8`, and at least one real UTF-8
locale with nontrivial collation.

## 10. Numeric, month, version, and random modes

These modes are parity traps. Implement scalar-compatible behavior first, then optimize.

### `-n` numeric

GNU `-n` compares decimal strings without converting to floating point. It handles leading
blanks, optional minus, decimal point, thousands separator, and digit runs according to
locale. rank should cache:

- sign class
- exponent or effective integer digit count
- span of significant digits
- fractional span
- zero/non-number class

The comparator then compares metadata first and digit spans second. Later, integer-heavy
inputs can use sign/exponent buckets plus radix over significant digits.

### `-g` general numeric

GNU `-g` uses floating conversion semantics, including infinities and NaNs. Start with a
`strtold()`-compatible parser/comparator. Do not radix this in v0.1.

### `-h` human numeric

GNU orders by sign, unit order, then numeric value. Cache suffix order and numeric
metadata. Be careful: different units compare by unit before raw scaled magnitude, matching
GNU and uutils behavior.

### `-M` month

Month names come from `LC_TIME`. Build a 12-entry folded lookup table like GNU. Cache the
month value per key. Unknown month sorts before known months.

### `-V` version

Use GNU-compatible `filevercmp` semantics. Pull a local implementation or write one
against golden tests. Do not substitute libc `strverscmp`; it is not identical across
platforms.

### `-R` random

GNU groups identical keys and orders groups by a randomized hash seeded from
`--random-source` or system randomness. This mode is correctness-sensitive and not central
to rank's performance thesis. Implement comparator/hash parity, then leave it outside
radix until tests are deep.

## 11. I/O, memory, and external sorting

### Input

- Start with `read()` into large buffers. It works for regular files, pipes, devices, and
  stdin.
- Split records with `memchr()` first. Add SIMD delimiter scanning only after the parser is
  correct and benchmarked.
- Preserve the final unterminated line behavior exactly. GNU sort supplies a logical
  terminator internally and outputs records with the selected terminator.
- Use `mmap` only for large regular files, only behind a measured plan, and never for
  pipes/devices/stdin.

### Memory policy

`-S SIZE` controls run memory, not total process RSS exactly. Match GNU's accepted size
syntax and error behavior. The internal policy should budget:

- input bytes
- line records or pointers
- key metadata
- transformed key arena
- radix aux array
- output buffer

When the budget is exceeded, finish the current run, sort it, write a temp file, and reuse
the arenas.

### External sort

Run generation:

1. Read until memory budget or EOF.
2. Precompute keys for the run.
3. Sort with the selected plan.
4. Apply `-u` within the run only when it is semantically safe; final global unique still
   needs merge-time duplicate handling.
5. Write a temp run with original records and terminators.

Merge:

- Use a k-way heap or loser tree, bounded by `--batch-size` and file descriptor limits.
- Cache the current key metadata for each input head. Do not recompute field ranges for
  the same line across heap comparisons.
- For `-m`, do not sort inputs. Read each input as an already sorted stream and merge.
- For `--compress-program`, pipe temp run writes/reads through the program, matching GNU
  diagnostics and exit handling.
- Clean temp files on normal exit and fatal signals.

## 12. SIMD and low-level acceleration

SIMD is useful, but it is not the first-order win. Add it only where the scalar algorithm
is already correct and measured.

Good SIMD targets:

- Record delimiter scanning (`\n` or `\0`) over input buffers.
- Field separator scanning for `-t SEP`.
- Blank classification for default field splitting (`space` and `tab` for sort fields;
  do not confuse this with wc's whitespace set).
- ASCII case folding and dictionary/nonprinting filters into transformed-key arenas.
- Bounded common-prefix comparison in comparator fallback when libc `memcmp` is not enough
  for transformed slices.

Bad SIMD targets in v0.1:

- Reimplementing locale collation.
- Reimplementing `strtold()` for `-g`.
- AVX-512-specific kernels before AVX2/SSE2/NEON paths are stable.

Tiering:

- Scalar is the parity oracle.
- SSE2 is the x86_64 baseline for simple byte scans.
- AVX2 widens scans when runtime detection allows it.
- NEON covers aarch64.
- Every SIMD kernel fuzzes against scalar with unaligned buffers and boundary cases.

## 13. Testing strategy

- Golden parity tests (`tests/golden/`): build GNU sort from the pinned coreutils source
  in `.docs/refs/gnu-coreutils`, generate fixtures, run rank and GNU sort with the same
  env, diff stdout/stderr/exit code. A ref-vs-ref self-test runs first.
- Fixture corpus: empty input, one line, missing trailing newline, all equal lines, random
  bytes, embedded NULs, newline mode, zero-terminated mode, very long lines, many tiny
  lines, long common prefixes, paths, URLs, timestamps, CSV/TSV, UTF-8, invalid UTF-8,
  month names, numbers, human units, versions, duplicates.
- Option matrix: default, every ordering flag, key ranges, multiple keys, `-t`, `-b`,
  `-s`, `-u`, `-r`, `-z`, `-c`, `-C`, `-m`, `-o`, `-S`, `-T`, `--batch-size`,
  `--parallel`, `--files0-from`, `--debug`.
- Locale matrix: `LC_ALL=C`, `LC_ALL=C.UTF-8`, one UTF-8 locale with nontrivial collation,
  one locale with non-dot decimal point if available.
- Differential fuzzer: random byte records x random option sets, constrained to options
  GNU accepts. Diff rank vs GNU sort. When an optimized plan is used, also compare the
  sorted order against `PLAN_COMPARATOR` in a debug build.
- Unit tests: key parser, field splitter, key range resolver, numeric parser, version
  comparator, month parser, radix grouping, reverse semantics, stable/unique semantics,
  temp-file cleanup.
- Sanitizers: ASan/UBSan on all paths; TSan for threaded sort/merge once `--parallel`
  exists. Fuzz with both `read()` and `mmap` paths.
- Perf gate: hyperfine rank vs GNU sort on the benchmark corpus. Fail if rank is slower
  on any supported workload; allow comparator-only edge cases to tie initially only with a
  tracked issue and explicit sprint owner.

## 14. Benchmark and perf gate

The perf suite must be workload-shaped, not one synthetic file.

Required warm-cache benchmarks:

| Workload | Why it matters | Expected rank win |
|---|---|---|
| shuffled word list, default | baseline byte string sort | radix beats comparison sort |
| long common prefixes | comparison sort worst case | largest radix win |
| path list | real prefix-heavy data | radix and packed output |
| URL/log lines | long records, late differences | radix prefix savings |
| CSV `-t, -k2,2` | key extraction cost | precomputed fields |
| multi-key TSV | group recursion and fallback | no repeated `begfield()` |
| numeric integers `-n` | common CLI case | cached numeric metadata |
| human units `-h` | suffix parser | cached suffix/number |
| versions `-V` | fallback correctness | tie or modest win |
| UTF-8 hard locale | collation cost | transformed keys or fallback |
| `-u` many duplicates | equal-key grouping | radix grouping |
| `-c` sorted file | streaming check | no allocation/sort |
| `-m` sorted slices | merge path | cached merge keys |
| external `-S 1M` | temp/run behavior | faster run sort |
| stdin to stdout | pipe overhead | no regression |
| tiny files | startup dominates | tie or slight win |

Required cold-cache benchmarks on Linux: word list, path list, CSV key sort, external sort.
Cold cache narrows CPU wins; rank still should not lose.

Measure:

- wall time via `hyperfine` with warmups
- user/system time via `/usr/bin/time -v` where available
- peak RSS
- temp bytes written
- number of comparisons in debug/perf builds
- records/sec and input MB/sec

The gate compares rank to the system GNU sort and to the pinned coreutils build when
available. Store results in `bench/results/` with machine, locale, corpus hash, command,
and tool versions.

## 15. CI

GitHub Actions matrix: ubuntu x86_64, macOS arm64, FreeBSD, musl/Alpine.

Pipeline: build (`-Werror`) -> unit -> golden parity -> fuzzer smoke -> sanitizer build ->
perf gate.

Perf gate is allowed to run on a smaller corpus in hosted CI, but local pre-flight must run
the full matrix on `hasu` (Linux x86_64) and `nomad` (macOS arm64) before big pushes.
FreeBSD 15 is the dev box.

## 16. Milestones

- M0 - Skeleton: repo, Makefile, configure probe, CI, golden harness, perf harness,
  reference build script. Builds `rank`, no behavior yet.
- M1 - Scalar parity core: option parser, line reader, line writer, scalar comparator,
  default sort, `-r`, `-s`, `-u`, stdin/files, `-o` safety. Comparator is the oracle.
- M2 - Key parity: `-k`, `-t`, inherited modifiers, obsolete key syntax, `--debug`,
  field extraction tests, multi-key last-resort behavior.
- M3 - MSD radix default: whole-line byte radix in identity collation, reverse/stable/full
  line unique cases, fallback verification against scalar comparator. First major win.
- M4 - Radix keyed sorts: precompute all key spans, single-key and multi-key byte radix,
  group recursion, correct last-resort whole-line compare. Biggest `-k/-t` win.
- M5 - Numeric/month/version/random: GNU-compatible `-n`, `-g`, `-h`, `-M`, `-V`, `-R`,
  cached metadata, scalar fallback. Numeric optimizations only after parity locks.
- M6 - Check and merge modes: `-c`, `-C`, `-m`, k-way merge, cached merge keys, `-u` in
  merge, disorder diagnostics.
- M7 - External sort: `-S`, `-T`, `--batch-size`, temp runs, `--compress-program`, cleanup
  on signals, output/input alias handling under temp pressure.
- M8 - Locale and transformed keys: identity collation probe, packed `strxfrm` keys where
  exact, C/UTF-8 locale matrix, fallback for embedded-NUL/hard cases.
- M9 - mmap, SIMD, and parallel: large-regular-file `mmap`, SIMD delimiter/key scans,
  `--parallel`, perf tuning across Linux/macOS/FreeBSD.
- M10 - Portability and release: man page (`doc/rank.1`), packaging (AUR, Homebrew,
  tarball), release automation.

## 17. Sprint index (`.docs/sprints/`)

Each sprint file is self-contained: objective, targets, parity tests, pitfalls,
performance notes, and exit criteria. They build on one another in order.

| # | Sprint | Milestone | Delivers |
|---|---|---|---|
| 00 | Foundations | M0 | Makefile, configure, CI, reference build, golden/perf harnesses |
| 01 | Scalar default sort | M1 | read/write, default comparator sort, stdin/files, `-o`, base flags |
| 02 | Keys and debug | M2 | `-k`, `-t`, inherited modifiers, `--debug`, key warnings |
| 03 | Default radix | M3 | whole-line MSD radix for byte identity collation |
| 04 | Keyed radix | M4 | key precompute, single/multi-key radix, group recursion |
| 05 | Special comparators | M5 | numeric, human, general numeric, month, version, random |
| 06 | Check and merge | M6 | `-c`, `-C`, `-m`, k-way merge, merge unique |
| 07 | External sorting | M7 | `-S`, `-T`, temp files, compression, signal cleanup |
| 08 | Locale keys | M8 | collation probe, packed `strxfrm`, hard-locale fallback |
| 09 | Low-level speed | M9 | mmap, SIMD scans, `--parallel`, tuning |
| 10 | Release | M10 | man page, packaging, release automation |

## 18. Stack and constraints

- C11, single Makefile, hand-rolled `configure` probe (writes `config.h` / `config.mk`).
  No autotools, no CMake. GNU make required (`gmake` on FreeBSD).
- Deps: libc only. Optional SIMD via compiler intrinsics (`<immintrin.h>`, `<arm_neon.h>`),
  not external libraries.
- Platforms: Linux x86_64 (`ssh hasu`), macOS arm64 (`ssh nomad`), FreeBSD 15 (this dev
  box), musl/Alpine.
- The scalar comparator must compile everywhere and remain the parity oracle.
- Optimized code must be plan-gated. If a plan cannot prove exact GNU ordering for the
  active flags/input/locale, it falls back to scalar comparator sorting.
- No global mutable comparator state except where a libc compatibility shim requires it;
  if needed, it must be single-thread guarded and documented.

## 19. Performance budget

| Cost | GNU sort default C locale | rank radix default |
|---|---|---|
| line split | `fread` + `memchr` | `read`/`mmap` + `memchr` or SIMD scan |
| line storage | contiguous buffer + line structs | contiguous arena + compact line records |
| core sort | O(N log N) merge sort | O(total distinguishing bytes + bucket overhead) |
| common prefixes | re-read by many comparisons | scanned once per radix depth/group |
| key extraction | first key partly cached; later keys re-scanned | all key spans cached once |
| transforms | often inside comparator | packed once per line/key |
| numeric parse | inside comparator | cached metadata once per line/key |
| merge | comparator per heap operation | cached current keys + heap/loser tree |

The default target is not "a faster `memcmp`". It is fewer calls to `memcmp` and fewer
bytes re-read. On 10 million records with a 24-byte shared prefix, comparison sort can
touch that prefix hundreds of millions of times. MSD radix touches it once per active
level, then recurses only where the next byte still ties.

## 20. Non-goals (v0.1)

- No flags beyond GNU sort's surface. Extensions gate behind `--rank-*` flags after
  parity release.
- No custom collation implementation. Use libc-compatible behavior or fallback.
- No GPU sorting.
- No AVX-512 requirement. AVX2/SSE2/NEON are optional accelerators after scalar parity.
- No in-place unstable radix for key plans until stable scatter has shipped and parity
  tests prove where instability is unobservable.
- No distributed/external service mode. rank is a local Unix command.

## 21. Working agreement

- Commit often, in chunks, terse imperative messages (<250 chars). Never co-author or add
  "Generated with" trailers.
- Tests + CI + perf gate block merges. Every behavioral change needs a golden test; every
  perf change needs a before/after benchmark number.
- Pre-flight big changes on `nomad` (macOS) and `hasu` (Linux) over Tailscale before pushing.
- Build with the full `-W` set as `-Werror`; keep ASan/UBSan clean.
- Write prose that a terse engineer would approve of. No boldface for emphasis, no hype, no
  marketing speak. State facts. See `~/.claude/CLAUDE.md` for the full anti-slop guidelines.
- Every choice: parity first, then fastest.
