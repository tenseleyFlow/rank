# Sprint 00 - Foundations

## Objective

Create the project skeleton and the two control systems that keep rank on track: GNU parity tests and performance tests. This sprint does not need useful `sort` behavior. It must make future work measurable.

## Build targets

- Add a C11 source tree matching the overview architecture.
- Add a single GNU Make build. Use `gmake` on FreeBSD.
- Add a hand-written configure probe that writes `config.h` and `config.mk`.
- Build a `rank` binary with stub option handling and clear unsupported diagnostics.
- Add `-Wall -Wextra -Werror` plus the stricter warning set chosen for this repo.
- Add ASan/UBSan build targets.
- Add scripts to build the pinned GNU coreutils `sort` reference from `.docs/refs/gnu-coreutils`.
- Add a golden harness that can run GNU sort and rank with the same args, input, locale, stdout, stderr, and exit-code capture.
- Add a perf harness with `hyperfine` support and a stable result format under `bench/results/`.
- Add minimal CI: build, unit smoke, golden harness self-test, sanitizer build.

## Implementation notes

- Keep dependencies to libc and standard developer tools.
- Treat GNU sort as the external truth and the future scalar comparator as the internal truth.
- Normalize only the program-name token in diagnostics when comparing stderr.
- Make test fixtures reproducible. Seed generators and store corpus hashes in perf results.
- Keep platform probes small: compiler flags, headers, functions, endian, SIMD feature macros, `mmap`, `posix_fadvise`, temp-file primitives.

## Parity tests

- Reference self-test: GNU sort from `.docs/refs/gnu-coreutils` must match system GNU sort for a small fixture when available.
- Harness smoke: empty input, one line, two lines, stdin input, file input, unsupported option path.
- Exit-code capture: prove the harness distinguishes success, disorder, and serious error once later sprints add behavior.

## Performance notes

- Establish baseline commands now even before rank is fast.
- Record machine name, OS, compiler, locale, command, corpus hash, rank version, GNU sort version, wall time, user time, system time, and RSS when available.
- Do not optimize the skeleton. Optimize measurement repeatability.

## Pitfalls

- Do not let CI compare against BSD `/usr/bin/sort` as the parity reference.
- Do not use autotools or CMake.
- Do not hide unsupported behavior behind success exits.
- Do not add optional libraries for benchmarking, parsing, hashing, or testing.

## Exit criteria

- `gmake` builds `rank` on FreeBSD.
- `gmake check` runs the golden harness smoke tests.
- `gmake sanitize` builds and runs the smoke tests under sanitizers where supported.
- The pinned GNU sort reference can be built or the script reports a clear missing-tool error.
- The perf harness can run at least one no-op/baseline command and write a result file.
