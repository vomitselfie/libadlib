# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

## [0.1.0] - 2026-10-03

First standalone release.

### Added
- **libadlib**: the ADLIB runtime (behaviours, interactors, agenda, message queue, timers,
  decision-path record/replay for lockstep networking), PBN reader/writer and `adlib::build`
  plan builder, `Vocabulary` (enum headers and list files, roles bound by enum name), primitives
  registered by name, `PlanHost` host interface, structured trace events. Builds natively and
  with Emscripten. Interpreter ids come only from the vocabulary (unbound features are disabled).
- **adlibc**: the ADLIB compiler, byte-identical to the 1996 ADLIB compiler; modes
  `--compat=adlib` (default, 1996 limits lifted) and `--compat=adlib1996` (the 1996 compiler
  exactly, fixed limits included); `--strict` warnings and `--Werror`; symbols from the four
  1996 symbol files (`--symbols DIR`, `--enumids ...`) or from a `Vocabulary`; library API
  `adlib/compiler.h`.
- **adlib** CLI and `adlib_tools` library: `dump`, `decompile` (byte-identical recompile),
  `graph` (DOT, Mermaid; single plan or all plans), `inspect`/`lint`, `compare`, `trace`
  (with `adlib::tools::Tracer` as the run-time sink); action-argument hints (`--hint`).
- **Example**: the vending machine host, its plans compiled from source by adlibc at build time.
- **Fixtures**: 185 compiler fixtures whose expectations were produced by the original 1996
  compiler with the package's own symbol files (`fixtures/vocab/`), so the whole test suite runs
  with no external data; `MANIFEST.json` with the executable's hash and every outcome.
- CMake package (`find_package(adlib 0.1 CONFIG)`, targets `adlib::adlib`, `adlib::tools`),
  version header, `--version` on both CLIs, CI workflow.
- Documentation: README, getting started, plan-writing tutorial, host API, language
  specification (confirmed rules only), adlibc, tools, compatibility, provenance, research notes.

### Verified (against the original executable, outside this package)
- Every fixture expectation re-derived from the original compiler: unchanged.
- Differential fuzzing, final campaign: 17,547/17,547 compared cases identical, 0 mismatches.
- The 32 plans shipped with HyperBlade: rebuilt byte-identically from source; decompile round
  trip 32/32 through adlibc and the original compiler.
- A source port of HyperBlade runs on this package unchanged (plan traces, headless matches,
  lockstep determinism hash and network tests identical).
