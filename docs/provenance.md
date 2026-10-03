# Provenance

Where ADLIB comes from, and how each part of this package was derived.

## History

ADLIB (*Authoring and Design Language for Interactive Behavior*) was created by Stuart Rosen and
Robert Duisberg, the principals of Wizbang!, and used to build Activision's **HyperBlade** (1996).
They described it in *Game Developer*, Aug/Sep 1996 ("Bringing Life to HyperBlade") as a language
for behaviours and interactions, an authoring environment and a run-time system. HyperBlade
shipped the compiler inside the game's executable and its plans as compiled `.PBN` files; the
language's source form and the compiler's exact behaviour were never published. This package is a
reconstruction of that system from the shipped game, plus new tooling.

## Sources

| source | used for | in this package |
|---|---|---|
| HyperBlade's executable, `HYPERX.EXE` (PE timestamp 1996-10-21) | the compiler (`CPlanCompiler`, 0x487790..0x48a900) and the runtime were read from it by static analysis; its compiler routine, run under emulation, produced every compatibility expectation | no: identified by hash only |
| HyperBlade's data (symbol files, 32 compiled plans) | the vocabulary format; the shipped plans as compatibility targets | no |
| Rosen & Duisberg's 1996 article | the system's name and structure, the vending-machine example, the networking model | short quotations in [research/article-1996.md](research/article-1996.md) |

The executable all evidence refers to:

| | |
|---|---|
| File | `HYPERX.EXE`, 1,359,872 bytes, CD file date 1996-10-21 |
| SHA-256 | `08c7b7a462847089d41a4e3ac0d61df3409ec8c9107cb97deff08364c4c0de60` |
| PE TimeDateStamp | `0x326BCBDE` = 1996-10-21 19:15:42 UTC |

## Methodology

1. **Static analysis** of the compiler in the executable: its keyword and special-constant
   tables, the symbol loader, the tokenizer, the prescan and the emitter. Every claim with its
   address: [research/binary-evidence.md](research/binary-evidence.md).
2. **Running the original.** The compiler routine was run under x86 emulation (unicorn) with the
   C runtime's file I/O and heap replaced by hooks, so that any source and any symbol files could
   be compiled by the 1996 code. 159 grammar probes tested each hypothesis; the working notes,
   with evidence tags and what remains inference, are in [research/grammar.md](research/grammar.md).
   This harness needs the game and is maintained outside this package.
3. **The specification** ([language.md](language.md)) keeps only rules the original compiler
   confirmed, each citing the fixtures that prove it.
4. **adlibc** was written from the specification and the analysed machine (its fixed-size
   buffers included), then held to the original by the fixture corpus and differential fuzzing
   ([compatibility.md](compatibility.md)). The fixtures' expectations were produced by the
   original compiler with this package's own symbol files (`fixtures/vocab/`), so they need no
   game data; `fixtures/MANIFEST.json` records the executable's hash and every fixture's outcome.
5. **The runtime** was reconstructed from the executable's interpreter (the comments in
   `include/adlib/runtime.h` give the original function addresses), verified by running the 32
   shipped plans in a source port of the game, then made independent of the game.
6. **The tools** (decompiler, graphs, lint, trace) are new; the decompiler was verified by
   recompiling its output of the 32 shipped plans through both compilers.

## The 1996 article versus the shipped compiler

The article prints one code listing in a labelled syntax (`DECLARE_BEHAVIOR Named: ...`) that the
shipped compiler rejects; the shipped form is prefix notation with explicit child counts. Every
construct of the listing has a counterpart in the shipped language. Whether the labelled form was
presentation pseudocode or an earlier dialect is unresolved:
[research/article-1996.md](research/article-1996.md).

## What the package contains

No files, code, vocabulary or plans of the game. The research notes (`docs/research/`) discuss the
executable and cite some of the game's symbol names as evidence; the compiler emulates the 1996
machine's table layout, buffer sizes and diagnostic texts, which is what byte-exact compatibility
means.
