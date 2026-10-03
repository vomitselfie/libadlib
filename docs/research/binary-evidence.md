# ADLIB compiler: binary evidence from HYPERX.EXE

This file records what the shipped executable proves about the ADLIB Plans compiler. Every
claim gives an address. Claims marked **[oracle]** were also checked by running the original
code (an emulation harness, unicorn running HYPERX.EXE's compiler; maintained outside this package). The experiment log is
`out/grammar_probe.jsonl`, written by the grammar-probe harness (HyperBlade repository). The experiment ids appear in
brackets.

Terms used here:
- **ADLIB compiler source** is the text format that this exe compiles. It is prefix notation
  with an explicit child count after every keyword. The grammar is confirmed: see
  [grammar.md](grammar.md).
- **ADLIB authoring syntax** is the labelled form printed in the 1996 article
  (`DECLARE_BEHAVIOR Named: "..."`). Its status is unresolved; see
  [article-1996.md](article-1996.md). This exe rejects it.

## 0. The binary

| | |
|---|---|
| File | `original/cd/HYPERX.EXE`, 1,359,872 bytes, CD file date 1996-10-21 |
| SHA-256 | `08c7b7a462847089d41a4e3ac0d61df3409ec8c9107cb97deff08364c4c0de60` |
| MD5 | `d6b07cbbfc5b6eb0d70c30fbe401c704` |
| PE TimeDateStamp | `0x326BCBDE` = 1996-10-21 19:15:42 UTC |
| Image base | 0x400000 |
| Sections (VA / raw offset) | `.text` 0x401000/0x400, `.rdata` 0x4FA000/0xF8C00, `.data` 0x50B000/0x109000, `.idata` 0x52B000/0x11F400, `.rsrc` 0x52D000/0x121400 |

File offset = VA − 0x402000 for `.data` and VA − 0x401400 for `.rdata`.

## 1. `AdlibError` RTTI

| item | VA | RVA | file offset |
|---|---|---|---|
| `TypeDescriptor` for `.?AUAdlibError@@` (`struct AdlibError`) | 0x513570 | 0x113570 | 0x111570 |
| its name string `.?AUAdlibError@@` | 0x513578 | 0x113578 | 0x111578 |

- TypeDescriptor = `{pVFTable = 0x500204 (type_info vftable), spare = 0, name}`.
- **It is caught but never thrown.** No ThrowInfo/CatchableType in the image references the
  descriptor. Only three `HandlerType` entries reference it. Each is `{adjectives = 8 (by
  reference), pType = 0x513570, dispCatchObj, handler}`, and each is followed by a second handler
  for `char*` (`.PAD`, TD 0x512ED8):

  | HandlerType array | function (try/catch owner) | `catch (AdlibError&)` funclet | `catch (char*)` funclet |
  |---|---|---|---|
  | 0x504A28 | `CGameObject__SetActivePlan` 0x463430 (plan instantiation) | 0x463505 | 0x4634EE |
  | 0x5057C0 | `FUN_0047a830`, dev dialog "Compile" (one plan) | 0x47A90B | 0x47A8F4 |
  | 0x505838 | `DevDlg_CompileAllPlans` 0x47A9B0, "Compile All" | 0x47AA94 | 0x47AA7D |

- The AdlibError funclets read `[obj+0]` as a severity: 1 calls `ErrFatal(&g_ErrorReporter,
  [obj+4])` (0x4041BF), and 2 calls `ErrWarning` (0x4016BD). So the layout is
  `struct AdlibError { int severity; const char *message; }`.
- The `char*` funclets call `ErrFatal(&g_ErrorReporter, msg)`. **This is the path every
  compiler diagnostic takes.** Each compiler error is `throw "literal"`: `__CxxThrowException@8`
  0x4E5B70 with ThrowInfo 0x504628, whose CatchableTypeArray holds `char*` (`.PAD`) and `void*`
  (`.PAX`).
- The string next to the descriptor, `"Plan instantiation failed. Setting %s plan to default
  minimal."` (0x513590), belongs to the plan loader. It explains why `MINIMAL.PBN` exists.
- Interpretation: `AdlibError` is a leftover of a more general ADLIB error type. In this build
  the compiler and loader throw plain C strings, and the AdlibError handlers are dead code.

## 2. Compiler class (`CPlanCompiler`, name ours; no RTTI, no vtable)

The object is allocated with `operator new(0x2289C)` at these sites: the plans dev dialog
(dwInitParam → GWL_USERDATA, `LoadSymbols` in WM_INITDIALOG 0x47A020), the nested compile for
`CHANGE_OF_PLAN` (inside 0x48903F), and the animation-assembly loader 0x496B00, which uses only
the symbol table.

| offset | size | field |
|---|---|---|
| +0x00 | u16 | object index (`OpenPlanSource` arg 1). It is passed again to the nested compile |
| +0x04 | i32 | source length |
| +0x08 | i32 | cursor (byte offset). The oracle reports errors at this offset; it sits just *after* the failing token |
| +0x0C | i32 | number of dwords emitted (becomes the PBN header) |
| +0x10 | char* | source buffer, `new(80000)` |
| +0x14 | u32* | output buffer, `new(120000)` = 30000 dwords. **There is no bounds check** |
| +0x18 | char[60][32] | behaviour names (PrescanDecls) |
| +0x798 | char[60][32] | local-real names (`DECLARE_LOCAL_REALS`) |
| +0xF18 | char[150][32] | interactor names (`SET_INTERACTOR_PARAM … id_SET_InteractorName "x"`) |
| +0x21DC | 28 × 0x28 | **keyword table** (§3) |
| +0x3DFC | 58 × 0x28 | **special-constant table** (§4) |
| +0x4A7C | ≤2101 × 0x28 | **symbol table** (§5) |
| +0x1929C | char[61][20][32] | message-interactor ids per behaviour. Row 0 holds the interactors before the first `DECLARE_BEHAVIOR` (the globals) and row b+1 holds behaviour b. Only the dev dialog's interactor list box uses it (`FUN_0047afd0`) |

Functions (all in `decomp/HYPERX.c`; the names are ours):

| addr | name | role |
|---|---|---|
| 0x487790 | `CPlanCompiler_ctor` | builds the keyword and special tables and allocates the buffers |
| 0x488220 | (dtor helper) | frees the buffers |
| 0x488490 | `CPlanCompiler_ReadSource` | reads the whole file byte by byte. It throws "too large" at 79999 bytes and clears all name tables |
| 0x4885E0 | `OpenPlanSource` | `<exe dir>Data\Plans\` + name + `.txt` |
| 0x488880 | `CPlanCompiler_Compile` | `PrescanDecls`, then `while (pos < len && buf[pos]) EmitLeaf()` |
| 0x488900 | `CPlanCompiler_PrescanDecls` | first pass: collects names (§8) |
| 0x488EC0 | `IsSeparator` | `' '`, `'\t'`, `','`, `'&'` |
| 0x488EF0 | `IsCommentStart` | `//`, `/*`, `(`, `#` |
| 0x488F30 | `IsIdentifier` | the token starts with `id`, `_`, `A_` or `AA_` (a case-sensitive prefix test) |
| 0x488F90 | `IsAnimName` | `AA_` |
| 0x488FC0 | `IsNumber` | the first character is `0-9`, `.`, `+` or `-` |
| 0x488FF0 | `IsPronounName` | characters [3..5] of the *table* name are `PRN` |
| 0x489020 / 0x48903F | `CPlanCompiler_EmitLeaf` / `ParsePlanArg` | **the code generator**: one token in, 0..n dwords out (§6). Ghidra splits it at a jump |
| 0x489780 | `ParsePlanNumber` | number literals (§7) |
| 0x4898B0 | `CPlanCompiler_EmitString` | string → `0xABCDEFFE c… 0` |
| 0x489940 | `CPlanCompiler_NextToken` | tokenizer (§7) |
| 0x489AC0 | `SkipLine` | skips to the end of the line, CR/LF/CRLF aware |
| 0x489B40 | `SkipSeparators` | |
| 0x489B80 | `SkipComment` (**dead code**, no callers) | a real `( … )` and `/* … */` skipper that is never called |
| 0x489C20 | `FindLocalReal` | string vs. the +0x798 table → index or −1 |
| 0x489D30 | `FindInteractorName` | string vs. the +0xF18 table → index or −1 |
| 0x489E40 | `FindKeyword` | **exact, case-sensitive** compare against the keyword table |
| 0x489F00 | `CPlanCompiler_LookupSymbol` | special table (case-sensitive), then symbol table (`_stricmp`) |
| 0x48A030 | `FindBehavior` | behaviour name → index. Throws "Could not find behavior named: " |
| 0x48A160 | `LookupInteractorName` | throws "Could not find interactor named: " |
| 0x48A240 | `CPlanCompiler_LoadSymbols` | §5 |
| 0x48A900 | `StripQuotes` | drops the first and last character in place |
| 0x488260 | `CPlanCompiler_WritePbn` | writes `<exe dir>Data\Plans\<name>.pbn` |

## 3. Keyword table (compiler+0x21DC, 0x28-byte entries)

Entry layout: `char name[32]; u32 tag; u32 id`. **There are no argument descriptors.** The table
does not encode arity or argument types. `tag` is always the PBN node marker 0xABCDEFFF, and
EmitLeaf copies `tag, id` into the output verbatim. The terminator entry has name `""` (0x50CA90)
and tag = id = 0xFFFFFFFF. FindKeyword stops at `id == -1` and returns that entry, so any unknown
word produces **no output** (§6).

| # | keyword (string VA) | id | note |
|---|---|---|---|
| 0 | `SET_COLLISION_INTERACTOR` (0x517DCC) | 0x00030000 | |
| 1 | `SET_MESSAGE_INTERACTOR` (0x517DB0) | 0x00050000 | prescan-scanned |
| 2 | `SET_LOC_COLLISION_INTERACTOR` (0x517D8C) | 0x00030001 | |
| 3 | `SET_NET_COLLISION_INTERACTOR` (0x517D68) | **0x00050002** | exe bug: same id as SET_NET_MESSAGE_INTERACTOR [unused_net_coll_interactor] |
| 4 | `SET_LOC_MESSAGE_INTERACTOR` (0x517D48) | 0x00050001 | |
| 5 | `SET_NET_MESSAGE_INTERACTOR` (0x517D28) | 0x00050002 | |
| 6 | `ADD_AGENDA_ITEM` (0x517D14) | 0x001A0001 | |
| 7 | `REMOVE_AGENDA_ITEM` (0x517CFC) | 0x001A0003 | |
| 8 | `SET_TIMEOUTMSG` (0x517CE8) | 0x001A0002 | |
| 9 | `GLOBAL_INTERACTORS` (0x517CD0) | 0x001B0000 | |
| 10 | `CHANGE_OF_PLAN` (0x517CBC) | 0x00160000 | positional counter `cop` |
| 11 | `CHANGE_TO_BEHAVIOR` (0x517CA4) | 0x00070002 | positional counter `ctb` |
| 12 | `DECLARE_BEHAVIOR` (0x517C90) | 0x00070000 | positional counter `beh`; prescan-scanned |
| 13 | `DECLARE_LOCAL_REALS` (0x517C78) | 0x00160001 | positional counter `dlr`; prescan-scanned |
| 14 | `SET_BEHAVIOR_PARAM` (0x517C60) | 0x00070003 | |
| 15 | `SET_INTERACTOR_PARAM` (0x517C44) | 0x00180001 | positional counter `sip`; prescan-scanned |
| 16 | `DO_ACTION` (0x517C38) | 0x00180000 | |
| 17 | `DO_LOCAL_ACTION` (0x517C24) | 0x00180002 | |
| 18 | `DO_NET_ACTION` (0x517C14) | 0x00180003 | |
| 19 | `DECIDE_BY_AMONG` (0x517C00) | 0x00170000 | |
| 20 | `DECIDE_BY_WITH_AMONG` (0x517BE4) | 0x00170001 | |
| 21 | `SET_DEBUG` (0x517BD8) | 0x00060001 | |
| 22 | `SET_TRACE` (0x517BCC) | 0x00060002 | |
| 23 | `Enable` (0x517BC4) | 0x80000000 | mixed case is significant [case_enable_lower] |
| 24 | `Block` (0x517BBC) | 0x40000000 | |
| 25 | `DataBlock` (0x517BB0) | 0x20000000 | |
| 26 | `END_PLAN` (0x517BA4) | 0xFFFF0000 | no count follows |
| 27 | `""` (terminator) | 0xFFFFFFFF | |

That makes 27 keywords with 26 distinct ids. Here is the matching entry from table #3:
"SET_NET_COLLISION_INTERACTOR" → 0x50002.

## 4. Special-constant table (compiler+0x3DFC, `char name[0x24]; u32 value`)

| name | value |
|---|---|
| `_BROADCAST_` | 0x0000FFFF |
| `_STRAIGHTAHEAD_` | 0xABCDABCD |
| `_TEMPORARILY_` | 0xABCDABCC |
| `_MESSAGE_DATA_` | 0xABCDABCE |
| `_NO_BEH_CHANGE_` | 0xABCDABCB |
| `_PREVIOUS_BEH_` | 0xABCDABCA |
| `_0_` … `_50_` | 0 … 50, built with `sprintf("_%hd_")` (fmt 0x517B2C) |
| `""` | 0xFFFFFFFF (terminator) |

The `_N_` entries are the **"argument enumerators"** named by the error messages. They can be
used anywhere a number can [count_enumerator_form, sym_enumerators_as_values]. Lookup is an exact,
case-sensitive compare [case_special_lower]. `_51_` is not a symbol [count_enumerator_51].

## 5. Symbol table (compiler+0x4A7C, `char name[0x24]; i32 value`) — `LoadSymbols` 0x48A240

Files are opened **relative to the current directory**, not the exe directory: `Data\enumIDs.h`
(0x5181B8), `Data\AnimData\Animassm.txt` (0x518198), `Data\Sndfiles.lst` (0x518180) and
`Data\Infobtxt.lst` (0x51815C). Each is read through `ReadSource`, so the 79999-byte limit
applies. All four use the plan tokenizer, so the comment rules of §7 apply to them too.

1. **enumIDs.h**: skip to the first `{`. Every token becomes a symbol whose value is its index
   in the current enum. `}` resets the index and skips to the next `{`. `= value` is not
   supported (it would become symbols `=` and `value`). `/* … */` comments end at the end of the
   line.
2. **Animassm.txt**: the first line is skipped. A token starting with `AA_` becomes a symbol with
   a running value. Any other token skips the rest of its line.
3. **Sndfiles.lst**: a line starting with a comment is skipped. A non-numeric token `w` becomes
   `id_SND_w` with a running value, and a numeric token skips the rest of the line. The first
   line, `22 kHzAnnouncer`, is therefore ignored.
4. **Infobtxt.lst**: for each line that does not start with a comment, its first token `w`
   becomes `id_TXT_w` with a running value, and the rest of the line is skipped (`##` lines are
   comments).
5. A terminator gets value −1. Lookup returns the **first** `_stricmp` match. There are 5
   duplicate names (`AA_GldToStnd`, `id_SND_satunny6`, `id_SND_decapit`, `id_SND_fatality`,
   `id_SND_subbing`), and the first occurrence wins.

The loaded table holds 1723 symbols: 364 `id_SND_`, 253 `AA_`, 143 `id_MSG_`, 141 `id_ACF_`,
140 `id_DCF_`, 118 `id_COB_`, 103 `id_MOD_`, 96 `id_TXT_`, 64 `id_RAT_`, 58 `id_PRN_` …. Its
values match the enum parse in the reconstruction's Python decoder exactly. The `A_*` names (`DirectionFlag`:
`A_NODIRECTION`, `A_FORWARD`, `A_REVERSE`, `A_PAUSE`) explain why `A_` is an identifier prefix
[sym_direction_A_]. `RET_*` (Reticle_State) is in the table but can never be referenced: it fails
the identifier test, so it is dropped as an unknown word [sym_RET_ignored].

## 6. Code generation (`EmitLeaf` 0x489020): a flat token translator, not a parser

`Compile` calls `EmitLeaf` once per token until the end of the buffer. **No tree is built. There
is no arity check, no type or namespace check, and no recursion.** The structure comes entirely
from the counts the author writes. For each token `t`:

1. Increment five global positional counters, `beh`, `ctb`, `cop`, `sip` and `dlr`
   (0x51803C–0x51804C, initial value 100). Each is reset to 0 by its keyword (§3), so
   "counter == k" means "this is the k-th token after that keyword".
2. If `IsIdentifier(t)`, look it up (§4, then §5). Value −1 throws `"Bad plan id value: " + t`.
   If the *table name* has `PRN` at [3..5], OR the value with 0xFEDC0000. If `dlr == 1` (the token
   right after `DECLARE_LOCAL_REALS`), the token must also parse as a number, otherwise
   `"expected arg enumeration of localvars."` is thrown. Emit the value.
3. Otherwise, if `IsNumber(t)`, emit `ParsePlanNumber(t)`. On failure, throw
   `"Bad number in plan: " + t`.
4. If `t` starts with `"` (a string):
   - the stripped text is in the local-real table: emit `0xEDCB0000 | i`;
   - otherwise it is in the interactor-name table: emit `0xDCBA0000 | i`;
   - otherwise `beh == 3`: EmitString (the behaviour name);
   - otherwise `cop == 2`: EmitString (the plan name), and remember the name;
   - otherwise `cop == 3`: create a **new compiler** (`ctor`, `LoadSymbols`,
     `OpenPlanSource(objIdx, plan)`, `PrescanDecls`) and emit `FindBehavior(text)` in that plan;
   - otherwise `ctb == 2`: emit `FindBehavior(text)` in this plan;
   - otherwise `sip == 3`: emit `LookupInteractorName(text)`. This branch always throws,
     because known names were already caught above [unused_interactor_unknown];
   - otherwise EmitString.
5. Otherwise (a word): `FindKeyword(t)`. If found, emit `0xABCDEFFF, id` and reset that keyword's
   counter. If not found, **emit nothing**. Examples: `Named:`, `Target:`, `{`, `}`, `)`,
   lower-case keywords, `RET_*`, “curly-quoted” strings [article_named_label_elsewhere,
   brace_tokens, case_keyword_upper, str_curly_quotes].

Consequences, all **[oracle]**:
- A count is just the next leaf. `2.0` gives float bits, `0x2` and `_2_` give 2, `-1` gives
  0xFFFFFFFF, and `32770` sets bit 15 [count_*]. The compiler never sets bit 15 by itself.
- Unknown words are ignored, **but they still advance the positional counters**. So
  `CHANGE_TO_BEHAVIOR 1 Target: "S"` emits the string `"S"` rather than a behaviour index
  [beh_ref_label_shift, brace_between_ctb].
- The PBN header is the total number of dwords emitted, but `WritePbn` writes only up to and
  including the first 0xFFFF0000. Tokens after `END_PLAN` therefore make the header disagree with
  the file [base_tokens_after_end]. A source with no `END_PLAN` makes `WritePbn` run past its
  buffer [base_missing_end_plan].
- `DECLARE_LOCAL_REALS 2 "a" "b"` emits `0xEDCB0000 0xEDCB0001` as its children, because the
  declared names are found in the table already filled by the prescan [unused_local_reals].
  Likewise `SET_INTERACTOR_PARAM 2 id_SET_InteractorName "X"` emits `0xDCBA0000`
  [unused_interactor_name].
- The string `""` matches the first *empty* local-real slot and emits 0xEDCB0000
  [str_empty].

## 7. Lexical rules (`NextToken` 0x489940, `ParsePlanNumber` 0x489780, `EmitString` 0x4898B0)

- Separators are space, TAB, `,` and `&`. CR and LF end a token. `;`, `:`, `{` and `)` are
  ordinary characters [sep_*].
- Comment starts are `//`, `/*`, `(` and `#`. **Each skips to the end of the line** via SkipLine.
  `/*` does *not* look for `*/` [cmt_block_*], and `(` does *not* look for `)`
  [cmt_paren_rest_of_line]. A comment start also ends the current token [cmt_glued]. The
  `( … )` / `/* … */` skipper 0x489B80 exists but is never called. `#include` and `#define` are
  just comments, so there is **no preprocessor** [cmt_hash].
- `}` is always a one-character token, even when glued: `"S"}}` gives `"S"`, `}`, `}`. `{` is an
  ordinary character [brace_*].
- A bare CR (no LF) is not handled: SkipLine runs to the next LF or to the end of the buffer
  [base_cr_only]. CRLF and LF both work.
- A token is a maximal run of non-separator, non-newline, non-comment characters. There is no
  quote handling in the tokenizer: `"Two Words"` is two tokens [str_space_inside].
- **Identifier**: starts with `id`, `_`, `A_` or `AA_`. This is a **case-sensitive** prefix test,
  so `ID_ACF_X` is a dropped word [case_symbol_upper*]. Lookup after the test is
  case-insensitive [case_symbol_mixed]. Any word starting with `id` is a symbol reference, so
  `idle` throws "Bad plan id value" [sym_idle_word].
- **Number**: starts with a digit, `.`, `+` or `-`. Every character must be in
  `xXABCDEFabcdef0123456789.+-_` (0x5180DC), otherwise "Bad number in plan". Then:
  - `_N_` → `atol(N)`;
  - `0x…` / `0X…` → `sscanf("%x")`, raw dword (exact float bits can be spelled this way);
  - contains `.` → `(float)atof` (IEEE single bits);
  - otherwise `atol`, so `1e3` → 1, `12ab` → 12, `1_000` → 1 and `4294967295` → 0xFFFFFFFF
    [num_*].
- **String**: a token whose first character is `"`. EmitString emits `0xABCDEFFE`, then one dword
  per character, **translating `_` to space**, stopping at the next `"`, then `0`. Anything after
  that quote in the same token is lost [str_escape]. There are no escapes. A string token of 40 or
  more bytes, quotes included, overflows a 40-byte stack buffer in `FindLocalReal` and **crashes
  the original compiler**. So string content is at most 37 characters [tok_len_str_*,
  tok_len_prescan_*]. The longest token in the shipped corpus is 33 characters. Unknown words of
  43 bytes do not crash [tok_len_word_*]. Identifiers of about 60 characters crash
  [limit_long_symbol].
- Characters are signed bytes; cp1252 bytes ≥ 0x80 would be sign-extended by EmitString. None
  occur in the shipped data.

## 8. PrescanDecls (0x488900): forward references

The prescan reads the whole file token by token (the same tokenizer and comment rules) **before**
any code is emitted. It compares keywords **case-insensitively** (`_stricmp`), unlike the
emitter. So `declare_behavior 2 …` registers a behaviour but emits no node
[case_declare_lower]. It also scans text after `END_PLAN`.

| keyword | what the prescan requires (error text) | records |
|---|---|---|
| `DECLARE_LOCAL_REALS` | next token is an identifier or number ("Expected an argument enumerator.  First two args are id and string name."), then *n* tokens starting with `"` ("Expected a string name in LocalVar Declaration.") | names → +0x798[i] |
| `DECLARE_BEHAVIOR` | count: identifier or number ("Expected an argument enumerator."); type: **identifier** ("Expected an id_BEH_ type id.", so `6` is rejected but `_6_` passes [beh_decl_*]); name: `"…"` ("Expected a string name in Behavior Declaration.") | name → +0x18[b], b++ ; interactor column reset |
| `SET_MESSAGE_INTERACTOR` | count: identifier or number; message: identifier or number ("Expected an id_MSG_ type id.") | token text → +0x1929C[b][k] (dev UI only) |
| `SET_INTERACTOR_PARAM` | count: identifier or number; if the next token `_stricmp`-equals `id_SET_InteractorName`, the next must be `"…"` ("Expected a string name in Behavior Declaration.", the wrong message is the exe's) | name → +0xF18[k] |

Only these four keywords are prescanned. `SET_COLLISION_INTERACTOR`, the LOC/NET interactors and
everything else are emitted blindly. Behaviour names are stored raw (with `_`, not yet turned
into spaces), and references are matched **case-sensitively** against those raw names
[case_behname, str_behname_with_space]. Duplicate names give no error, and the first match wins
[beh_dup_name]. Limits: 60 behaviours, 60 local reals, 150 interactor names and 20 message
interactors per behaviour for the UI table. None of these is checked.

## 9. Diagnostics catalogue (all thrown as `char*`, shown by ErrFatal)

| VA | text | thrower | parser state it implies | probe |
|---|---|---|---|---|
| 0x517E3C | `This plan file is too large.  Why not break it up into separate plans?` | ReadSource 0x488490 | source ≥ 79999 bytes | limit_too_large |
| 0x517E94 | `Plan file could not be opened.` | OpenPlanSource 0x4885E0 | `Data\Plans\<name>.txt` is missing (also the CHANGE_OF_PLAN target) | cop_missing_source (oracle harness faults in `GetResString` 0x4464F0 first) |
| 0x517DEC | `Cannot open binary plan file. Is it locked?` | WritePbn 0x488260 | `.pbn` not writable (read-only on CD) | — |
| 0x517EC4 | `Aborting Plan Parse.` | Compile 0x488880 | EmitLeaf returned false. In practice unreachable, because every failure path throws first | — |
| 0x517F80 | `Expected an argument enumerator.` | Prescan | the token after DECLARE_BEHAVIOR / SET_MESSAGE_INTERACTOR / SET_INTERACTOR_PARAM is not an identifier or number. **Fires on the article's `Named:`** and on `DECLARE_BEHAVIOR(2 …` | article_*, cmt_paren_call_syntax |
| 0x517FE4 | `Expected an argument enumerator.  First two args are id and string name.` | Prescan | after DECLARE_LOCAL_REALS | unused_local_reals_no_count |
| 0x517F5C | `Expected an id_BEH_ type id.` | Prescan | second token after DECLARE_BEHAVIOR is not an identifier (a missing count shifts the string into this slot) | beh_decl_numeric_id, count_missing_decl |
| 0x517F20 | `Expected a string name in Behavior Declaration.` | Prescan | third token after DECLARE_BEHAVIOR (or InteractorName arg) is not `"…` | beh_decl_no_name |
| 0x517EFC | `Expected an id_MSG_ type id.` | Prescan | second token after SET_MESSAGE_INTERACTOR is not an identifier or number | msg_interactor_* |
| 0x517FA8 | `Expected a string name in LocalVar Declaration.` | Prescan | | unused_local_reals_not_string |
| 0x518094 | `expected arg enumeration of localvars.` | EmitLeaf | an identifier right after DECLARE_LOCAL_REALS that is not `_N_` | unused_local_reals_symbol_count |
| 0x5180C4 | `Bad plan id value: ` + token | EmitLeaf | unknown identifier (`id…`, `_…`, `A_…`, `AA_…`) | sym_unknown, case_special_lower |
| 0x518078 | `Bad number in plan: ` + token | EmitLeaf | a character outside the number set | num_bad_char |
| 0x518058 | `Inability to read string: ` + token | EmitLeaf / EmitString | no closing `"` within 41 characters (space inside a string, unterminated) | str_space_inside, str_unterminated |
| 0x518100 | `Could not find behavior named: ` + name | FindBehavior 0x48A030 | CHANGE_TO_BEHAVIOR / CHANGE_OF_PLAN target not declared (case-sensitive) | beh_ref_unknown, cop_unknown_beh |
| 0x518128 | `Could not find interactor named: ` + name | LookupInteractorName 0x48A160 | token #3 after SET_INTERACTOR_PARAM is an undeclared name | unused_interactor_unknown |

The message buffers live in `.data` and are built by `strcat` with the token. Each throw first
truncates the buffer to its fixed prefix (for example `[0x13] = 0`). The oracle reports the
cursor *after* the failing token.

## 10. Where sources live; how they are compiled

- Source: `GetModuleFileName` dir + `Data\Plans\` (0x517E2C) + name + `.txt` (0x517EBC).
  Output: the same dir + name + `.pbn` (0x517E24). The name is the plan name from the object's
  `SetPlans` list (obj+0x234, count +0x238), for example `ROK`.
- **PLANS.CAT is not used by the compiler.** It is read only by the multiplayer asset check (see
  the HyperBlade port's PBN notes).
- There is a developer **Plans dialog** (resource strings `&Plans...`, `&Load Plan`, `&Compile`,
  `Compile &All`, `Behaviors`). Its command handler 0x47A270 dispatches:
  - 0x408 `PlanDebugger_LoadPlan` 0x47A610: re-reads the `.pbn` into the running object, then
    "New plan successfully loaded" (0x5148D8) or "Cannot set and init this plan" (0x5148FC);
  - 0x421 compiles the selected plan (0x47A830);
  - 0x422 `DevDlg_CompileAllPlans` 0x47A9B0: for every scene object and each of its plans,
    `OpenPlanSource`, `Compile` and `WritePbn`, then "Compilation successful!" (0x514920);
  - 0x420 / 0x42F fill list boxes from a **prescan** of the source: behaviour names, and message
    interactors per behaviour from +0x1929C (0x47AE90, 0x47AFD0);
  - 0x432 forces the object into the selected behaviour (`PlanInstance_ChangeToBehavior`), after
    checking the plan name: "This is not the Plan you think it is.  Press the Load Plan button."
    (0x51493C).

  This is the article's "p-code compiler is integrated into the game itself so that changes in
  Plans can be made and tested immediately, without quitting the game".
- **CHANGE_OF_PLAN** compiles the target plan's `.txt` again (a fresh compiler and a fresh
  `LoadSymbols`) to turn a behaviour *name* into an index. A numeric index needs no sibling file
  [cop_numeric].
- The positional counters are process-global and are *not* reset between compiles. The oracle
  restores `.data` before each compile, so it always starts at 100. In the real dialog, a file
  that ended within 3 tokens of a `CHANGE_TO_BEHAVIOR` / `CHANGE_OF_PLAN` / `DECLARE_BEHAVIOR`
  could affect the first strings of the next file. This does not happen with sane sources.

## 11. Strings searched for and NOT present

None of the following occur in HYPERX.EXE (ASCII or UTF-16) or anywhere on the CD: `Named`,
`BehaviorType`, `Target:`, `Response`, `RESPONSE`, `SET_INTERACTION`, `Interaction`,
`ChangeToBehavior`, `DecrementDamageBar`, `SetUserControl`, `PlaySoundType`, `GlideToTarget`,
`GoForTheGoal`, `TheGoal` and `ADLIB` (the only `Adlib` is the RTTI name). `DECLARE_BEHAVIOR`
and the other keywords appear only in HYPERX.EXE, and no plan source ships on the disc. So the
exe contains no table of "named-argument" labels: such labels can only be words that the
compiler ignores (§6.5).

## 12. Verification

- the reconstruction's Python decompiler regenerates source for all 32 shipped PBNs
  (the HyperBlade plan corpus, kept outside this package). **All 32 recompile byte-identically through the original
  compiler** (oracle, with a raised instruction budget for the large plans and for those that
  use `CHANGE_OF_PLAN` sibling compiles).
- the grammar-probe harness (HyperBlade repository) runs 159 experiments. Every explicit expectation is confirmed, with
  no contradictions. The Python model `exe_model.py` (HyperBlade repository) predicts every oracle
  result except the crash cases: the token-length overflows and the harness fault on a missing
  sibling plan.
