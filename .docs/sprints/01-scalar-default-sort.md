# Sprint 01 - Scalar Default Sort

## Objective

Ship the scalar parity core for default sorting. This is the internal oracle for every fast path. It must be correct, portable, and simple enough to trust.

## Build targets

- Implement option parsing for default sort, `-r`, `-s`, `-u`, `-z`, `-o`, stdin, file operands, and `--help`/`--version`.
- Implement contiguous input arenas and counted line records.
- Implement newline and NUL record splitting.
- Preserve missing final terminator behavior exactly as GNU sort does.
- Implement output writing with selected record terminator.
- Implement safe `-o` behavior for input/output aliasing.
- Implement scalar whole-line comparator for identity byte ordering and current locale fallback shape.
- Implement comparator sort using the simplest correct standard-library or local merge sort.
- Implement base reverse, stable, and unique behavior.
- Add the first internal comparator-vs-output assertions in debug builds.

## Implementation notes

- Store bytes once. Sort pointers or compact records.
- Do not allocate per line.
- Keep comparator state explicit in a context object where possible.
- `-s` suppresses last-resort ordering only once keys exist, but default behavior still needs stable semantics tested.
- `-u` must follow GNU's active comparison semantics. Golden tests decide survivor behavior.
- Prefer a stable merge sort if it simplifies `-s` and later keyed grouping.

## Subsprints

- 01A - Scalar core: parse base options, read stdin/files into arenas, split counted records, stable merge sort by whole-line bytes, write stdout.
- 01B - Output safety and diagnostics: implement `-o`, missing-file diagnostics, read/write error paths, and input/output alias preservation.
- 01C - Base semantics: finish `-r`, `-s`, `-u`, `-z`, multiple files, repeated stdin operands, missing final terminators, and comparator counters.
- 01D - Coverage pass: expand golden fixtures, sanitizer coverage, and debug counter checks before entering Sprint 02.

## Parity tests

- Empty input, one line, two lines, already sorted, reverse sorted, all equal lines.
- Missing trailing newline.
- Very long line, many tiny lines, binary bytes, embedded NUL in newline mode.
- `-z` with newline bytes inside records.
- Multiple input files and repeated `-` stdin operands where GNU accepts them.
- `-r`, `-s`, `-u`, and combinations.
- `-o out in`, `-o in in`, hard links, symlinks where practical.
- Diagnostics and exit codes for missing files, bad `-o`, write failures where practical.

## Performance notes

- The comparator path may tie or lose to GNU initially, but it must not be careless.
- Avoid repeated strlen. Every line is counted.
- Use buffered `read()` and buffered output writes from the start.
- Add debug counters for comparator calls and compared bytes. These counters justify radix later.

## Pitfalls

- Do not call C string functions on input records.
- Do not assume newline mode means no embedded NUL bytes.
- Do not reverse the final array for every `-r` case if later options will make that invalid. Keep the operation modeled as comparison reversal.
- Do not truncate `-o` output before the input is safe.

## Exit criteria

- Default sort output matches GNU sort for the sprint fixture matrix.
- `-r`, `-s`, `-u`, `-z`, and `-o` match GNU for covered cases.
- Sanitizers are clean.
- Comparator counters are available in a debug/perf build.
