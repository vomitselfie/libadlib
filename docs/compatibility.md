# Compatibility: how adlibc is held to the 1996 compiler

adlibc's claim ([adlibc.md](adlibc.md)) is that, in `--compat=adlib1996` mode, it produces the
same bytes as the 1996 ADLIB compiler for every input that compiler accepts, and the same
diagnostic (message, line, byte offset) for every input it rejects; inputs the original crashes or
hangs on, or answers with uninitialised memory, are refused with a clear error. `--compat=adlib`
is the same language with the 1996 buffer limits lifted: identical output wherever those limits
are not hit.

The evidence was produced against the **original executable**: the compiler routine inside
HyperBlade's `HYPERX.EXE` (1996; sha256 `08c7b7a4…c0de60`, see [provenance.md](provenance.md)),
run under x86 emulation with its file I/O redirected, so that any source text and any symbol
files could be fed to it. That harness needs a copy of the game and is not part of this package;
what the package contains is its results, frozen as fixtures that run anywhere.

## 1. The fixture corpus (in `ctest`, no external data)

`fixtures/compiler/` holds 185 cases, each a source file and `<case>.expected.json`:

| directory | cases | content |
|---|---|---|
| `valid/` | 28 | accepted, and clean under `--strict` |
| `quirks/` | 105 | accepted although malformed; each records the mechanism (`"mechanism"`) |
| `errors/` | 45 | rejected by the original, or crashing it |
| `fuzz-regressions/` | 7 | every disagreement the fuzzer ever found, minimised |
| `plans/` | 4 | sibling plans that `CHANGE_OF_PLAN` cases read |

An expectation records the outcome, the exact diagnostic or the PBN's sha256 and every dword, the
decoded tree, and where it came from: `"expected_from": "oracle"` (the original compiler's
result) or `"adlibc (the original compiler crashes on this input)"`. The `"oracle"` and
`"adlibc"` blocks keep both results as they were when the case was frozen; `"adlibc_agrees"`
records that they matched.

The original compiler was run with the package's own symbol files, `fixtures/vocab/`
(`enumIDs.h`, `AnimData/Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst`: a small made-up
vocabulary in the 1996 layout), so the expectations depend on nothing outside the package.
`adlibc_test` compiles every case with `--compat=adlib1996` against the same files and compares
outcome, diagnostic, sha256 and every dword.

The cases cover one rule each: the lexical rules, every keyword, every token class, counts,
positional resolution, declarations, table overflows, token-length limits, every diagnostic of
the original, and the cases found while writing adlibc. [language.md](language.md) cites them
rule by rule.

## 2. Differential fuzzing

A fuzzer generated sources and compiled each with the original compiler and with
`adlibc --compat=adlib1996`:

- grammar-built plans from the keyword table, the vocabulary's symbols and the behaviour names of
  real plans;
- token mutations (counts, numbers in every odd form, strings near the 40-byte limit, special
  constants, case changes, dropped words, separators) and byte mutations;
- generators aimed at the 1996 machine's limits: 61 behaviours or local reals, long names, stale
  string bytes, positional counters, `CHANGE_OF_PLAN` siblings.

The outcomes had to be identical: the same PBN, or the same diagnostic line. Cases on which the
original crashes or hangs were not compared, but adlibc had to reject them. Lone `0x` tokens (the
original emits uninitialised stack bytes) were counted separately. The emulator started every
compile from a fresh stack, so that results did not depend on case order.

| campaign | cases | compared | agree | original crashed (adlibc rejects) | uninitialised | mismatches |
|---|---|---|---|---|---|---|
| development (seeds 1-5) | 20,300 | 19,888 | 19,881 | 337 (337) | 145 | 7, all now fixtures, fixed |
| **v0.1 final** (seeds 21-23, release binary) | **18,000** | **17,547** | **17,547** | 288 (288) | 165 | **0** |

The fuzzer also compared `--compat=adlib` with `--compat=adlib1996` wherever the latter neither
refuses nor warns: 13,133 cases, identical.

Every mismatch became a fixture in `fuzz-regressions/` **before** adlibc was changed
([../CONTRIBUTING.md](../CONTRIBUTING.md)).

## 3. Real plans

The 32 plans that shipped with HyperBlade were reconstructed as source and each rebuilds its
shipped `.PBN` byte for byte through adlibc and through the original compiler; `adlib decompile`
output for all 32 recompiles byte-identically through both. Those sources and the game's files are
not part of this package.

## Release criterion

A release requires:

1. the test suite passes on Linux with GCC and Clang (CI);
2. every fixture expectation still matches the original compiler when re-derived from it, and
   the real-plan checks above pass;
3. a fresh fuzz campaign of at least 15,000 compared cases on the release binary with **zero**
   mismatches.

v0.1.0 met all three (see the table above and [../CHANGELOG.md](../CHANGELOG.md)).
