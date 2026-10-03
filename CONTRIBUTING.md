# Contributing

Thanks for helping. ADLIB has one overriding rule: **the 1996 compiler is the specification.**

## The fixture-first rule

Every difference between adlibc and the original compiler becomes a **fixture before it is
fixed**:

1. Reduce the input to the smallest source that shows the difference, using only symbols from
   `fixtures/vocab/` (extend those files by appending if you need more).
2. Add it to `fixtures/compiler/fuzz-regressions/` (differences found by fuzzing) or to `valid/`,
   `quirks/` or `errors/`, with a `"found_by"` and, for quirks, a `"mechanism"` in its
   `.expected.json`.
3. Freeze the expectation from the original 1996 compiler. That needs the original executable and
   the emulation harness, which are not part of this package: open an issue with the source and
   a maintainer who has them will freeze it. Commit the fixture, failing.
4. Fix adlibc until `ctest` passes.

Never edit an expectation by hand, and never "fix" one to match adlibc.

## Other rules

- **No game content.** Do not add files from HyperBlade or any other game, text extracted from
  them, game vocabularies or reconstructed plans of a commercial game. See [NOTICE.md](NOTICE.md).
- **Quirks are reproduced, not fixed.** The language's oddities are part of it. If one is a likely
  mistake, add a `--strict` warning; never change the output of the default modes.
- **The spec holds only confirmed rules.** A rule enters `docs/language.md` with the fixtures
  that prove it ("CONFIRMED BY ORACLE"); inference goes in `docs/research/`.
- **Runtime changes keep determinism.** No new floating-point paths without the build flags in
  `CMakeLists.txt`; the trace output of the examples should only change on purpose.
- Code style: C++17, the surrounding file's formatting, no new dependencies in the library.
- Run `ctest --test-dir build` before sending a change; CI runs it on GCC and Clang.

By contributing you agree that your contribution is licensed under the MIT licence.
