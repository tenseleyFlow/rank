# Sprint 02 - Keys And Debug

## Objective

Implement GNU key semantics and debug output. This sprint unlocks the workloads where rank must later win by avoiding repeated field extraction.

## Build targets

- Parse `-k KEYDEF`, `--key=KEYDEF`, `-t SEP`, `--field-separator=SEP`, `-b`, and per-key modifiers.
- Implement global option inheritance rules for key modifiers.
- Implement obsolete `+POS -POS` key syntax where GNU accepts it.
- Resolve all key spans for every line in one pass.
- Add field tables for default blank-run splitting and explicit separator splitting.
- Implement multi-key scalar comparison.
- Implement last-resort whole-line comparison unless `-s` or `-u` suppresses it.
- Implement `--debug` annotations, warnings, and locale notes close enough for golden tests.

## Implementation notes

- Precompute every key span needed by the invocation. This is a design rule, not an optimization later.
- Default splitting is not CSV. Without `-t`, fields are nonblank runs separated by blanks, and leading blanks matter unless `-b` applies.
- With `-t`, adjacent separators create empty fields.
- Keep spans into original input. Materialize only for transforms in later sprints.
- Make key extraction separately unit-testable. Most later bugs will come from off-by-one field and character rules.

## Parity tests

- Single key: `-k1,1`, `-k2,2`, `-k1.2,1.4`, open-ended keys, end character 0.
- Multiple keys with and without last-resort whole-line compare.
- Default blank fields with leading blanks, tabs, empty-looking lines, and `-b`.
- `-t,`, `-t '\t'`, adjacent separators, leading/trailing separators.
- Per-key `r` and inherited global modifiers.
- `-s` and `-u` with equal keys and different full lines.
- Obsolete key forms accepted by GNU and rejected forms with matching diagnostics.
- `--debug` stdout annotations and stderr warnings for bad or suspicious keys.

## Performance notes

- Add counters for field scans and key extractions. For a given run, extraction should be O(lines x keys), not O(comparisons x keys).
- Avoid per-key heap allocation.
- Keep field tables compact. Many common commands use one or two keys.

## Pitfalls

- Do not trim blanks globally. GNU's `-b` applies at specific key start/end points.
- Do not copy BusyBox/toybox style key allocation inside the comparator.
- Do not forget the last-resort whole-line compare.
- Do not treat `-u` equality as full-line identity when active keys exist.

## Exit criteria

- Keyed scalar sorting matches GNU for the fixture and option matrix.
- `--debug` has golden coverage for key annotations and warnings.
- Key extraction is precomputed before sorting.
- Sanitizers are clean.
