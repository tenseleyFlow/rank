# Sprint 10 - Release

## Objective

Prepare rank for a parity release: documented, packaged, portable, and gated by correctness plus performance.

## Build targets

- Write `doc/rank.1` with GNU-compatible option documentation and rank-specific notes.
- Add release tarball generation.
- Add packaging metadata for AUR and Homebrew if still desired.
- Add release CI jobs for Linux, macOS, FreeBSD, and musl/Alpine.
- Add version stamping.
- Add `.docs/deviations.md` if any intentional GNU deviations exist.
- Add contributor-facing docs for running golden tests, fuzz tests, and perf gates.
- Freeze the v0.1 supported flag surface.

## Implementation notes

- The release is not ready because it is fast on one benchmark. It is ready when the matrix is green.
- Any unsupported GNU option must be documented before release or implemented.
- Perf claims must cite stored benchmark results with machine and corpus hashes.
- Keep release automation simple and reproducible.

## Parity tests

- Full golden matrix from all prior sprints.
- Differential fuzzer at release depth.
- Locale matrix on machines where locales are available.
- External sorting stress tests with cleanup checks.
- `--help`, `--version`, diagnostics, man page examples.

## Performance notes

- Run the full perf gate on FreeBSD dev box, `hasu` Linux, and `nomad` macOS before release.
- Required result set: warm-cache suite, required Linux cold-cache suite, external sorting, tiny files, stdin/stdout.
- Fail release if rank is slower than GNU on a supported workload unless there is an explicit issue and release note.

## Pitfalls

- Do not publish broad claims without benchmark artifacts.
- Do not ship generated files that hide local paths or machine state.
- Do not add compatibility shims at release time without tests.
- Do not expand scope beyond GNU sort parity for v0.1.

## Exit criteria

- Full CI passes.
- Full golden and fuzz matrices pass locally.
- Full perf gate passes on required machines or exceptions are documented and owned.
- Man page and release docs are complete.
- Tarball builds from a clean checkout.
