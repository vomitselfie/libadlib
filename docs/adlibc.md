# adlibc: the ADLIB Plans compiler

`adlibc` compiles ADLIB plan source (`.txt`) into the binary plans (`.PBN`) the ADLIB runtime
loads. It reproduces the 1996 ADLIB compiler (the one Wizbang! shipped inside HyperBlade's
executable) **byte for byte**: the same bytes for every input the original accepts, the same
diagnostic, line and byte offset for every input it rejects. The language is specified in
[language.md](language.md); the compiler's internals are in `src/compiler/` (API:
`include/adlib/compiler.h`).

```
adlibc [--symbols DIR | --enumids F [--anims F] [--sounds F] [--texts F]]
       [--compat=adlib|adlib1996] [--plans-dir DIR] [--strict] [--Werror] [-o OUT.pbn] [-q] FILE.txt
```

| option | meaning |
|---|---|
| `--symbols DIR` | the four symbol files in the 1996 layout: `enumIDs.h`, `AnimData/Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst` (names case-insensitive; all but `enumIDs.h` optional) |
| `--enumids F --anims F --sounds F --texts F` | the files individually (any may be omitted) |
| `--compat=adlib` | (default) the ADLIB language with the 1996 implementation limits lifted; see below |
| `--compat=adlib1996` | the 1996 compiler exactly, limits included; see below |
| `--plans-dir DIR` | where `CHANGE_OF_PLAN` finds sibling plans' sources (default: FILE's directory) |
| `--strict` | extra warnings for input the 1996 compiler accepted silently; never changes the output |
| `--Werror` | warnings are errors: nothing is written |
| `-o OUT` | output file (default `<plan>.pbn` in the current directory, like the original's `<plan>.pbn`) |
| `--version` | print the version |

Exit status: 0 written, 1 rejected (diagnostic on stderr), 2 usage or symbol-file problem.

Example:

```
adlibc --enumids VENDING.H --texts VENDTEXT.LST --strict -o VENDING.PBN VENDING.txt
```

## Symbols

The original builds one flat symbol table from four files, in a fixed order, searched front to
back case-insensitively (first match wins): every token inside `{ }` of `enumIDs.h` (value = its
position in its enum), the `AA_*` tokens of `Animassm.txt`, `id_SND_<word>` for `Sndfiles.lst`,
`id_TXT_<first word of a line>` for `Infobtxt.lst`. adlibc reads the files with the same
tokenizer and the same rules (`CompilerSymbols::fromFiles`), so a new game simply provides its own
files (the vending machine uses `VENDING.H` and `VENDTEXT.LST`). A host that already has a libadlib
`Vocabulary` can compile against it directly (`CompilerSymbols::fromVocabulary`): namespaces in
creation order, ids as given. `adlibc_test` checks that both routes agree.

## The two compatibility modes

Two claims, one per mode:

**`--compat=adlib` (default): "the 1996 ADLIB language, any vocabulary."** Tokenizer, symbol
lookup, translation of every token, positional resolution, the counts written by the author and
all diagnostics are the 1996 compiler's, exactly; the PBN is byte-identical to the original's for
every input that stays within the 1996 implementation's limits. Those limits are lifted: names of
any length (no 32-byte table slots, no 36-byte symbol names), any number of behaviours, local
reals and interactor names, tokens and strings of any length, sources over 79 998 bytes and more
than 30 000 output dwords; an unterminated string is the original's own error (`Inability to read
string`) instead of being completed from stale memory; a lone `0x` is 0. This is the mode for new
games. The vending machine compiles in it without a single `--strict` warning.

**`--compat=adlib1996`: "exactly what the 1996 compiler does."** The output is what the 1996
compiler would write, byte for byte, including the effects of its fixed limits, with any
vocabulary. Use it to target a 1996 runtime, or to study the original:

- symbol files the 1996 loader would mishandle (a token of 40+ bytes overwrites its counters, a
  file of 79 999+ bytes, an `enumIDs.h` without `{`) are refused;
- input on which the original compiler crashes, hangs, writes past its memory or emits
  uninitialised memory is an error (kinds `crash`, `corrupt`, `hang`, `undefined`), with a note
  saying so;
- input the original accepts but mangles is compiled exactly as the original mangles it, with a
  warning that is always shown: a 61st behaviour (stored over the local-real table), names over 31
  characters (spill into the next slot), an unterminated string completed by stale bytes, a 40-43
  byte token (corrupts the exception chain, so a later error would crash the original), dwords
  after `END_PLAN` (counted by the header, not written).

| input | the 1996 compiler | `--compat=adlib1996` | `--compat=adlib` |
|---|---|---|---|
| string token of 40+ bytes | crashes | error `crash` | the whole string |
| any token of 44+ bytes | crashes | error `crash` | fine |
| lone `0x` | uninitialised stack value | error `undefined` | 0 |
| more than 30 000 output dwords | overruns its buffer | error `corrupt` | fine |
| source of 79 999+ bytes | `This plan file is too large...` | same error | compiled |
| `SET_MESSAGE_INTERACTOR` after the 60th behaviour | writes past its object | error `corrupt` | fine |
| 61st behaviour / local real / 151st interactor name | lands in the next table | reproduced + warning | unbounded tables |
| name of 32-37 characters | spills into the next slot | reproduced + warning | exact |
| unterminated string, quote among stale bytes | completed from stale bytes | reproduced + warning | `Inability to read string` |
| comment / bare CR on the last line, no line end | reads past the source | reproduced (cursor only) + warning | stops at the end |
| unterminated string whose quote search reaches stack bytes no code of this compile wrote | depends on earlier code | those bytes taken as zero (as in a fresh process) + warning | `Inability to read string` |
| missing `END_PLAN` | overruns its buffer | error `no-end` | same |
| a sibling plan that does not exist | `Plan file could not be opened.` | same | same |

Everything else (the language) is the same in both modes and is the 1996 behaviour, quirks
included: lower-case keywords are dropped, unknown words still shift the positional counters,
counts are emitted as written, symbols from the wrong enum compile to their number, and so on
([language.md](language.md)).

## Diagnostics

Errors use one fixed format, the one the compatibility evidence was recorded in, so outputs can be
compared line for line:

```
FILE:LINE: error [STAGE/KIND] (byte N): MESSAGE
unknown_behavior_name.txt:3: error [compile/throw] (byte 81): Could not find behavior named: Nowhere
missing_end_plan.txt: error [write/no-end]: no END_PLAN (0xFFFF0000) in output; the original WritePbn would overrun its buffer
```

`STAGE` is `read` (loading the source), `compile` or `write`; `KIND` is `throw` (the original's
own diagnostic, text verbatim), `no-end`, or, for input the original cannot survive, `crash`,
`corrupt`, `hang` or `undefined` (adlibc's text, followed by a `note:` line). `N` is the
original's cursor (just after the failing token and its trailing separators) and `LINE` counts the
LF bytes before it. Like the original, adlibc stops at the first error.

Warnings: `FILE:LINE: warning: MESSAGE [code]`; tree-level lint (`--strict`) prints
`FILE: warning: /tree/path: MESSAGE [lint]`.

## --strict

`--strict` never changes the output; it adds warnings (in either mode) for input the 1996
compiler accepted silently but that is almost certainly a mistake:

- dropped words: unknown words, lower-case keywords (`[keyword-case]`; the prescan still treats
  `declare_behavior` as a declaration), `ID_...`/`a_...` words that miss the case-sensitive
  identifier prefixes, braces;
- namespace mismatches per slot: a symbol from the wrong enum where the statement expects a
  behaviour type, message, collision object, action, decision, agenda item or parameter id
  (pronouns are always allowed) `[namespace]`;
- counts: a count that is not an integer literal (`2.0`, a symbol, a string), one with bits above
  15, `END_PLAN` swallowed by a too-large count, stray values at the top level (count too small);
  and, from the shared adlib-tools lint (`tools::lintPlan`), every statement whose children do not
  match its conventional arity and shape (interactor options, action lists, DataBlock) `[lint]`;
- unresolved references: a `CHANGE_TO_BEHAVIOR` / `CHANGE_OF_PLAN` name emitted as a string
  because a word shifted it out of its position `[beh-ref]`;
- lengths: string tokens of 36+ bytes (the original crashes from 40), other tokens of 36+ bytes
  (its buffers are 40 bytes, 44+ crashes it);
- numbers with ignored trailing characters (`12ab`, `0x1.8`);
- a bare CR (it swallows the next line) and a file ending in a comment without a newline;
- symbol files the 1996 loader would mishandle (`--compat=adlib` only; adlib1996 refuses them).

`--Werror` turns every warning into a failure.

## Quirks policy

adlibc's default is the 1996 behaviour. A quirk of the original language is never "fixed": it is
reproduced (and, where it is a likely mistake, reported by `--strict`). Only limits of the 1996
*implementation* differ between the modes: `--compat=adlib1996` keeps them (and refuses what the
original cannot survive), `--compat=adlib` lifts them. When a new difference between adlibc and the
original is found it first becomes a permanent fixture, then adlibc is fixed
([../CONTRIBUTING.md](../CONTRIBUTING.md)).

## Compatibility evidence

Summarised here, in full in [compatibility.md](compatibility.md). All of it was produced by
running the original 1996 compiler (inside HyperBlade's executable, under emulation) next to
adlibc:

- **Fixture corpus** (`fixtures/compiler/`, 185 cases, replayed by `ctest` with no external data):
  every expectation is the original compiler's result with the package's own symbol files
  (`fixtures/vocab/`), or adlibc's refusal where the original crashes.
- **Differential fuzzing**: in the final v0.1 campaign **17,547 of 17,547** compared cases were
  identical (bytes or diagnostic line); 0 mismatches. `--compat=adlib` agreed with
  `--compat=adlib1996` on all 13,133 cases where the latter neither refuses nor warns.
- **Real plans**: all 32 plans shipped with HyperBlade were rebuilt byte-identically from source,
  and decompiled sources recompiled byte-identically through both compilers.

## Behaviour found beyond the earlier documentation

Found while building adlibc from the decompiled compiler and confirmed against the original
(fixtures in parentheses; [language.md](language.md) cites them):

- Error messages carry only the first **20 bytes** of a bad identifier or number
  (`errors/msg_bad_id_truncated`, `errors/msg_bad_number_truncated`): EmitLeaf truncates the token
  buffer before the `strcat`.
- `atof` accepts **`d`/`D` exponents** and ignores an exponent without digits
  (`quirks/num_exponent_d`: `1.5d2` = 150.0, `.5e` = 0.5).
- A lone **`0x`** emits **uninitialised stack bytes** (sscanf returns EOF; running the original
  shows NextToken's separator flag over residue of an earlier call, e.g. 0x01000000)
  (`fuzz-regressions/fz_seed2_*`).
- An **unterminated string can succeed**: EmitString scans 41 bytes, past the token's NUL, into
  stale bytes of earlier tokens in the same stack buffer (`quirks/str_stale_bytes`).
- **Table overflows are deterministic:** the 61st behaviour name is stored over local-real slot 0
  (so `"B60"` compiles as 0xEDCB0000, even in its own declaration) (`quirks/beh_61_overflow`); the
  61st local real lands in interactor slot 0 (`quirks/local_reals_61`); a 36-character behaviour
  name spills its tail into the next slot, creating a phantom behaviour (`quirks/beh_name_spill`).
- **CHANGE_OF_PLAN's plan name** is the last string at its position *or the keyword-table entry of
  the last non-string token* (every non-string token is looked up as a keyword into the same
  buffer): `CHANGE_OF_PLAN 2 Enable "One"` compiles `Data\Plans\Enable.txt`
  (`quirks/cop_plan_name_keyword`).
- A vertical tab or form feed is part of a word (`fuzz-regressions/fz_seed1_vertical_tab`).
- The positional counters are 16-bit and process-global; the local-real count read by the prescan
  starts as -1 (OpenPlanSource's try level left on the stack) when it is not a number.
- Writing a message-interactor name after the 60th behaviour, or more than 30 000 dwords, writes
  past the compiler's memory (the dev dialog's interactor list; the 120 000-byte output buffer).
- Token-length crash points: string tokens crash at 40 bytes (FindLocalReal's 40-byte copy hits
  the return address), every token at 44 bytes (PrescanDecls' return address); tokens of 40-43
  bytes overwrite the prescan's local-real count and EmitLeaf's exception-chain link, harmless
  unless an error follows.
- `SET_NET_COLLISION_INTERACTOR` emits 0x00050002, the id of `SET_NET_MESSAGE_INTERACTOR`
  (`quirks/unused_net_coll_interactor`).

## Library use

```cpp
#include "adlib/compiler.h"
adlib::CompilerSymbols syms = adlib::CompilerSymbols::fromDataDir("symbols/", nullptr, /*originalLimits*/ false);
// or CompilerSymbols::fromVocabulary(vocab), or fromFiles({enumIds, anims, sounds, texts})
adlib::CompileOptions opt;                       // opt.compat = adlib::Compat::Adlib1996 for the 1996 machine
opt.strict = true;
opt.planSource = [](const std::string &plan) -> std::optional<std::string> { /* sibling source */ };
adlib::CompileResult r = adlib::compilePlan(sourceText, syms, opt);
if (r.ok) writeFile("VENDING.PBN", r.pbn); else std::puts(r.error()->format("VENDING.txt").c_str());
```

`CompileResult::words` holds every dword emitted (the PBN header counts them all);
`CompileResult::pbn` is the file the original would write.
