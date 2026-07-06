# AGENTS.md

This file is root guidance for agents working on `rank`.

## Project

`rank` is a from-scratch C11 replacement for GNU `sort(1)`. The binary is `rank`.

The goal is byte-identical GNU sort behavior with fewer cycles. Rank must be faster than GNU sort on every supported workload, with the largest wins on bytewise sorting, keyed field sorting, long shared prefixes, and external sorts where CPU still matters.

Read these before implementation work:

- `.docs/overview.md` - design source of truth.
- `.docs/sprints/*.md` - ordered implementation plan.
- `.docs/refs/gnu-coreutils/src/sort.c` - parity target.
- Other `.docs/refs/` trees as research references, not parity authorities.

## Working Rules

- Follow the sprint files in order. Do not skip ahead without updating docs and tests.
- Keep changes small and reviewable.
- Commit often when asked to commit. Use short imperative commit messages.
- Never add co-author trailers or generated-by trailers.
- Keep prose terse. Avoid marketing language and filler.
- Prefer the smallest correct implementation.
- Do not add compatibility layers unless there is a concrete need.
- Do not add dependencies beyond libc unless explicitly approved.
- Use C11, a single Makefile, and a hand-written configure probe. No autotools. No CMake.
- Build with the full warning set as errors.
- Keep ASan/UBSan clean. Use TSan for threaded code when available.

## Correctness Contract

GNU coreutils `sort` is the external truth.

Rank must match GNU sort for:

- stdout bytes and `-o` output bytes.
- stderr diagnostics, except the program-name token may differ in tests.
- exit codes.
- locale behavior for `LC_COLLATE`, `LC_CTYPE`, `LC_NUMERIC`, and `LC_TIME`.
- newline and `-z` record models.
- binary data, including embedded NUL bytes in newline mode.
- key syntax, obsolete key syntax, warnings, and `--debug` output.
- `-s`, `-u`, `-r`, `-c`, `-C`, `-m`, temp files, `--files0-from`, and signal cleanup.

The scalar comparator is the internal oracle from Sprint 01 onward. Every optimized plan must be diff-tested against it before being trusted against GNU sort.

## Performance Contract

Rank is not a faster `memcmp`. Rank is fewer repeated comparisons and fewer repeated key scans.

The core thesis:

- Parse once.
- Precompute line/key metadata once.
- Choose an exact plan.
- Use MSD byte radix for eligible byte and key sorts.
- Fall back to the scalar comparator when exact fast ordering is not proven.

Never move expensive work into comparator hot paths if it can be precomputed per line/key.

Track and benchmark:

- wall time
- user/system time
- peak RSS
- temp bytes written
- comparator calls
- records/sec
- input MB/sec

Perf changes need before/after numbers. The perf gate should fail when rank is slower than GNU sort on supported workloads unless there is an explicit tracked exception.

## Architecture Shape

Use the architecture from `.docs/overview.md` unless there is a documented reason to change it:

- `src/main.c` - argv to plan to execution.
- `src/options.[ch]` - GNU-compatible parser.
- `src/plan.[ch]` - plan selection.
- `src/line.[ch]` - arenas, records, splitting.
- `src/key.[ch]` - field/key extraction and debug annotations.
- `src/radix.[ch]` - MSD radix engine.
- `src/cmp.[ch]` - scalar comparator and oracle.
- `src/numeric.[ch]` - numeric modes and metadata.
- `src/locale.[ch]` - locale setup and collation transforms.
- `src/merge.[ch]` - k-way merge.
- `src/external.[ch]` - run generation and temp files.
- `src/check.[ch]` - check modes.
- `src/output.[ch]` - buffered output and `-o` safety.
- `src/util.[ch]` - allocation, diagnostics, helpers.
- `src/sys/` - platform abstraction.

## Data Rules

- Store input bytes in large arenas.
- Sort pointers or compact records, not copied strings.
- Keep record lengths explicit. Do not rely on NUL termination.
- Do not allocate one object per line.
- Precompute every needed key span before sorting.
- Keep key spans as pointers into original input where possible.
- Materialize transformed keys only when flags or locale require it.
- Store transformed keys in packed length-addressed arenas.

## Fast Path Rules

- Radix is required, not optional polish.
- Start with whole-line byte radix in identity collation.
- Then add keyed byte radix with group recursion.
- Keep stable scatter until tests prove instability is unobservable for a plan.
- Bound recursion for long equal prefixes.
- Do not reverse the final array for `-r` unless the selected plan proves that is equivalent.
- Do not optimize `-u` until GNU survivor semantics are covered by golden tests.
- SIMD, `mmap`, and `--parallel` come after scalar and radix correctness.

## Fallback Rules

Use scalar comparator fallback for any case not proven exact, especially:

- hard locale collation
- embedded NUL with C-string collation APIs
- version sort before GNU `filevercmp` parity is locked
- random sort before grouping/hash parity is locked
- month sort before locale tests are deep
- numeric fast paths before scalar numeric parity is locked

Fallback is allowed. Silent wrong fast paths are not.

## Testing Rules

Every behavioral change needs tests.

Required test types over the project:

- golden tests: rank vs pinned GNU sort
- unit tests: parsers, key extraction, numeric, version, month, radix grouping
- differential fuzzing: random bytes and random accepted option sets
- optimized-plan verification: fast plan vs scalar comparator in debug builds
- sanitizer runs
- perf gate

Golden tests should normalize only the program-name token in stderr diagnostics.

## Sprint Order

Work in this order unless explicitly directed otherwise:

1. `00-foundations.md`
2. `01-scalar-default-sort.md`
3. `02-keys-and-debug.md`
4. `03-default-radix.md`
5. `04-keyed-radix.md`
6. `05-special-comparators.md`
7. `06-check-and-merge.md`
8. `07-external-sorting.md`
9. `08-locale-keys.md`
10. `09-low-level-speed.md`
11. `10-release.md`

Each sprint must exit with its documented tests and performance checks passing.

## Reference Use

- GNU coreutils is the parity source.
- FreeBSD sort is useful for radix design and pitfalls.
- uutils sort is useful for cached metadata and benchmark shape.
- BusyBox and toybox are useful for semantic reading and as examples of comparator hot-path work to avoid.
- `../aspen/src/sort.c` is useful for local MSD radix structure.

Do not copy code blindly. Preserve license boundaries and document any imported implementation.

## Platform Rules

Target platforms:

- FreeBSD 15 development box.
- Linux x86_64 on `hasu`.
- macOS arm64 on `nomad`.
- musl/Alpine.

Use portable libc first. Put platform-specific code behind `src/sys/`.

## Non-Goals For v0.1

- No flags beyond GNU sort's surface.
- No custom collation implementation.
- No GPU sorting.
- No AVX-512 requirement.
- No in-place unstable radix for key plans until stable scatter ships and tests prove safety.
- No distributed or service mode.
