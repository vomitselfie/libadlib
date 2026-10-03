# ADLIB Plans: grammar of the compiler source (research notes)

> **Research notes from the reconstruction** (unchanged apart from this note and relative links).
> This is the grammar agent's evidence-tagged reconstruction, including inferred (**I**) and
> speculative (**S**) material and the discussion of the 1996 article. The public language
> specification, limited to rules confirmed by the original compiler, is
> [../language.md](../language.md); the compiler is described in [../adlibc.md](../adlibc.md).

This document describes the text that the compiler embedded in HYPERX.EXE turns into `.PBN`
files. It is called **ADLIB compiler source** below. The format is confirmed: all 32 shipped
plans have been reconstructed in it, and each recompiles **byte-identically** with the original
compiler. The 1996 article shows a different, labelled **ADLIB authoring syntax**. That syntax is
covered only in §10, and its status is unresolved.

Companion documents:
- [binary-evidence.md](binary-evidence.md): addresses, tables and diagnostics.
- [article-1996.md](article-1996.md): the article.
- the annotated HyperBlade examples (MINIMAL, TURNSTIL, ROK) and the corpus of all 32 plans are
  kept with the HyperBlade port, not in this package; the package's worked example is the vending
  machine ([../writing-a-plan.md](../writing-a-plan.md)).
- the grammar-probe harness (HyperBlade repository) → `out/grammar_probe.jsonl`: 159 oracle experiments. Their ids appear
  as `[id]` below.

### Provenance tags

| tag | meaning |
|---|---|
| **P** confirmed-parser | read from the decompiled compiler code (addresses in binary-evidence.md) |
| **K** confirmed-keyword-table | from the keyword or constant tables built in `CPlanCompiler_ctor` |
| **E** confirmed-error-path | implied by a diagnostic and the condition that throws it |
| **O** confirmed-oracle | checked by running the original compiler: a probe, or the 32/32 round trip |
| **A** confirmed-article | stated in Rosen & Duisberg 1996 |
| **I** inferred-from-PBN | a convention seen in the shipped data that the compiler does not enforce |
| **S** speculative | |

---

## 1. The model: a flat token translator (P, O)

The compiler is **not** a recursive-descent parser. `Compile` runs in two passes.

1. `PrescanDecls` reads every token and records behaviour names, local-real names and interactor
   names. It checks the shape of only four statements (§6).
2. `EmitLeaf` translates each token **independently** into 0, 1, 2 or *n* dwords and appends them
   to the output.

No tree is built and nothing is balanced or checked. **The child count that follows every
keyword is written by the author, and it is the only thing that gives the PBN its tree
structure.** A wrong count compiles without complaint and produces a broken tree
[count_too_large, count_too_small, count_missing_action]. A compiler that aims to be
byte-identical must therefore emit the count *as written*, or compute exactly the number of
child items (§5) when the source is well-formed.

---

## 2. Lexical grammar

```ebnf
(* Characters are bytes (cp1252/ASCII). The compiler reads at most 79998 bytes. *)   (* P E O *)

source        = { separator | newline | comment | token } ;

separator     = " " | TAB | "," | "&" ;                                               (* P O sep_* *)
newline       = CR LF | LF ;                (* a bare CR is NOT a line end: base_cr_only *) (* P O *)
comment       = comment-start { any-byte - (CR | LF) } ;   (* always to END OF LINE *)  (* P O cmt_* *)
comment-start = "//" | "/*" | "(" | "#" ;  (* "/*" does not look for "*/"; "(" does not look for ")" *)

token         = close-brace | word ;
close-brace   = "}" ;                       (* always a 1-char token, even when glued *) (* P O brace_* *)
word          = word-char { word-char } ;   (* also ends where a comment-start begins *)  (* P O cmt_glued *)
word-char     = any-byte - (separator | CR | LF | "}" | NUL) ;
```

Each word is classified by its first characters, in this order (P, O):

```ebnf
identifier = ( "id" | "_" | "A_" | "AA_" ) { word-char } ;   (* case-SENSITIVE prefix test: P O case_symbol_upper *)
number     = ( digit | "." | "+" | "-" ) { word-char } ;     (* must then pass the number rules *)
string     = '"' { word-char } ;                              (* no spaces possible: tokenizer splits *)
other-word = word - identifier - number - string ;            (* keyword, or IGNORED *)
```

Rules:
- **Identifiers** (P, O). After the prefix test, the lookup goes to (a) the special-constant
  table, compared exactly, then (b) the symbol table (enumIDs.h → `Animassm.txt` `AA_*` →
  `Sndfiles.lst` as `id_SND_<word>` → `Infobtxt.lst` as `id_TXT_<word>`), compared with
  `_stricmp`, first match wins. If nothing matches, the error is `Bad plan id value: <tok>`.
  Consequences: `ID_ACF_X` is not an identifier and is silently dropped [case_symbol_upper_body].
  `idle` is an identifier and fails [sym_idle_word]. `RET_*` can never be referenced
  [sym_RET_ignored].
- **Numbers** (P, O num_*). Every character must be in `xXABCDEFabcdef0123456789.+-_`, otherwise
  the error is `Bad number in plan: <tok>`. Then the first matching rule applies:
  `_N_` → `atol(N)`; `0x…`/`0X…` → hex dword; contains `.` → IEEE single from `atof`;
  otherwise `atol` (so `1e3` → 1, `12ab` → 12, `1_000` → 1).
- **Strings** (P, O str_*). The token starts with `"`. The content runs to the **next** `"`.
  Anything after that quote in the same token is lost, and there are no escapes. `_` is emitted
  as a space, so a string can never contain `_` or a real space in the source. Token length must
  stay **< 40 bytes including quotes** (≤ 37 characters of content). Longer string tokens crash
  the original compiler [tok_len_str_40…]. The content must close within 41 characters,
  otherwise the error is `Inability to read string: <tok>`. Typographic quotes `“ ”` are not
  quotes [str_curly_quotes].
- **Other words** (P, K, O). Matched **exactly and case-sensitively** against the keyword table
  (§4). If there is no match, **nothing is emitted, but the positional counters (§5.3) still
  advance**. This covers `{`, `}`, `)`, `;`-suffixed words, labels such as `Named:`, lower-case
  keywords and `ID_…` words [article_named_label_elsewhere, case_keyword_upper, brace_tokens].
- Line structure, indentation, blank lines and trailing whitespace carry no meaning
  [base_one_line, base_tabs_blank_lines]. `#include`/`#define` are comments: there is no
  preprocessor and no include mechanism [cmt_hash].

---

## 3. Leaf encoding (what each token emits)

| source token | PBN dwords | tag | probes |
|---|---|---|---|
| keyword `K` | `0xABCDEFFF, id(K)` | P K O | |
| integer `123`, `-5`, `+7` | `atol` value (two's complement) | P O | num_int/neg/plus |
| hex `0x1F` | raw dword (spells any float bit pattern exactly) | P O | num_hex_* |
| float `1.5`, `.25`, `3.`, `1.5e2` | IEEE-754 single bits | P O | num_float* |
| `_N_` (N = 0..50) | N | P K O | sym_enumerators_as_values |
| `_BROADCAST_` | 0x0000FFFF | K O | sym_specials |
| `_STRAIGHTAHEAD_` | 0xABCDABCD | K O | |
| `_TEMPORARILY_` | 0xABCDABCC | K O | |
| `_MESSAGE_DATA_` | 0xABCDABCE | K O | |
| `_NO_BEH_CHANGE_` | 0xABCDABCB | K O | |
| `_PREVIOUS_BEH_` | 0xABCDABCA | K O | |
| `id_PRN_x` (a symbol whose table name has `PRN` at [3..5]) | `0xFEDC0000 \| index` | P O | sym_pronoun |
| other symbol (`id_MSG_x`, `AA_x`, `id_SND_x`, `A_FORWARD`, …) | its value (position in its enum or list) | P O | |
| `"text"` declared by `DECLARE_LOCAL_REALS` | `0xEDCB0000 \| index` | P O | unused_local_reals |
| `"text"` declared by `SET_INTERACTOR_PARAM … id_SET_InteractorName` | `0xDCBA0000 \| index` | P O | unused_interactor_name |
| `""` (when fewer than 60 local reals are declared) | `0xEDCB0000 \| first empty slot` | P O | str_empty |
| `"text"` at a resolving position (§5.3) | a behaviour index | P O | |
| any other `"text"` | `0xABCDEFFE, c1, c2, …, 0` (`_` → 0x20) | P O | str_basic |
| unknown word | (nothing) | P O | |

**Float vs int** (P, O num_float_one_point_zero): `1` is int 1, `1.0` is 0x3F800000. The PBN does
not tag the type, so a byte-identical decompiler must print a `.` exactly where the dword is
meant as a float. the reconstruction's Python decompiler treats a value as an int when
|v| < 0x100000 and otherwise prints the shortest decimal with a `.` that round-trips. It falls
back to `0x…` when no such decimal exists.

**Header and end** (P, O base_tokens_after_end, base_missing_end_plan): the file is
`u32 ndwords` followed by the dwords up to and including the first `0xFFFF0000`. `ndwords` counts
**every** dword emitted, including any after `END_PLAN`. A well-formed source puts nothing after
`END_PLAN` except comments and ignored words.

---

## 4. Keywords and their PBN ids (K)

| keyword | id | children convention (I, from the 32 shipped plans) | uses |
|---|---|---|---|
| `GLOBAL_INTERACTORS` | 0x001B0000 | interactor* | 24 |
| `DECLARE_BEHAVIOR` | 0x00070000 | beh-type, "name", { SET_BEHAVIOR_PARAM \| interactor } | 185 |
| `DECLARE_LOCAL_REALS` | 0x00160001 | "name"… (n = count) | 0 |
| `SET_MESSAGE_INTERACTOR` | 0x00050000 | msg, [CHANGE_TO_BEHAVIOR], [Enable] | 1174 |
| `SET_LOC_MESSAGE_INTERACTOR` | 0x00050001 | as SET_MESSAGE_INTERACTOR | 0 |
| `SET_NET_MESSAGE_INTERACTOR` | 0x00050002 | as SET_MESSAGE_INTERACTOR | 0 |
| `SET_COLLISION_INTERACTOR` | 0x00030000 | cob-mine, cob-other, [CHANGE_TO_BEHAVIOR], [Enable] | 256 |
| `SET_LOC_COLLISION_INTERACTOR` | 0x00030001 | as SET_COLLISION_INTERACTOR | 0 |
| `SET_NET_COLLISION_INTERACTOR` | **0x00050002** (exe bug: collides with NET_MESSAGE) | as SET_COLLISION_INTERACTOR | 0 |
| `Enable` | 0x80000000 | action* | 1183 |
| `Block` | 0x40000000 | action* | 582 |
| `DataBlock` | 0x20000000 | value* | 69 |
| `DO_ACTION` | 0x00180000 | acf, value* | 3323 |
| `DO_LOCAL_ACTION` | 0x00180002 | acf, value* | 0 |
| `DO_NET_ACTION` | 0x00180003 | acf, value* | 0 |
| `DECIDE_BY_AMONG` | 0x00170000 | dcf, branch… | 509 |
| `DECIDE_BY_WITH_AMONG` | 0x00170001 | dcf, DataBlock, branch… | 69 |
| `CHANGE_TO_BEHAVIOR` | 0x00070002 | behaviour | 1242 |
| `CHANGE_OF_PLAN` | 0x00160000 | "PLAN", behaviour-in-that-plan | 135 |
| `SET_BEHAVIOR_PARAM` | 0x00070003 | set-id, value* | 216 |
| `SET_INTERACTOR_PARAM` | 0x00180001 | set-id, value* | 0 |
| `ADD_AGENDA_ITEM` | 0x001A0001 | agd, value* | 84 |
| `REMOVE_AGENDA_ITEM` | 0x001A0003 | agd | 84 |
| `SET_TIMEOUTMSG` | 0x001A0002 | delay, msg, [recipient, [data]] | 253 |
| `SET_DEBUG` | 0x00060001 | value* (ignored by the runtime) | 0 |
| `SET_TRACE` | 0x00060002 | value* (ignored by the runtime) | 0 |
| `END_PLAN` | 0xFFFF0000 | **no count** | 32 |

These are case-sensitive and exact. `Enable`, `Block` and `DataBlock` are mixed case; all the
others are upper case.

---

## 5. Syntactic grammar

### 5.1 What the compiler accepts (P, O)

```ebnf
compiler-input = { token } ;          (* any token sequence; only §6 prescan rules and the  *)
                                      (* per-token errors of §2/§3/§5.3 can reject it      *)
```

### 5.2 Well-formed plans (the grammar a source should follow)

A `count` is the number of **child items** that follow. A nested statement, a string and a value
each count as **one** child, however many dwords they encode to [count_is_children_not_tokens].
Below, `n` marks where the count goes, and every rule is shown *with* its count.

```ebnf
plan            = [ local-reals ] [ globals ] behaviour { behaviour } "END_PLAN" ;      (* I; order I *)
                  (* shipped: 24 x GLOBAL_INTERACTORS first; 6 plans without; LOCAL_REALS unused *)

local-reals     = "DECLARE_LOCAL_REALS" n string { string } ;           (* P E O; n = #names *)
globals         = "GLOBAL_INTERACTORS" n { interactor } ;               (* K I O *)
behaviour       = "DECLARE_BEHAVIOR" n beh-type beh-name { beh-item } ; (* P E O; n = 2 + #items *)
beh-type        = identifier ;                     (* id_BEH_*; a bare number is REJECTED: E O *)
beh-name        = string ;                         (* 3rd token after keyword: E O *)
beh-item        = behaviour-param | interactor ;   (* I *)

interactor      = msg-interactor | coll-interactor ;
msg-interactor  = ( "SET_MESSAGE_INTERACTOR" | "SET_LOC_MESSAGE_INTERACTOR"
                  | "SET_NET_MESSAGE_INTERACTOR" ) n msg-id { int-option } ;            (* K I O *)
coll-interactor = ( "SET_COLLISION_INTERACTOR" | "SET_LOC_COLLISION_INTERACTOR"
                  | "SET_NET_COLLISION_INTERACTOR" ) n cob cob { int-option } ;         (* K I O *)
int-option      = next-beh | handler | interactor-param | debug ;
                  (* runtime ctors 0x4cdea0/0x4ce240 accept these in any order, anything else throws; *)
                  (* shipped data uses only  [ next-beh ] [ handler ]  in that order                   *)
msg-id          = identifier | number ;            (* id_MSG_*; prescan E O *)
cob             = value ;                          (* id_COB_* or a pronoun such as id_PRN_ThisTurnstileCOB: I *)
next-beh        = change-beh ;                     (* behaviour to switch to after the handler: I *)
handler         = "Enable" n { action } ;          (* K I *)

action          = do-action | decide | decide-with | change-beh | change-plan
                | agenda-add | agenda-remove | timeout | behaviour-param
                | interactor-param | debug | block ;                                    (* I *)
block           = "Block" n { action } ;                                                (* K I *)
do-action       = ( "DO_ACTION" | "DO_LOCAL_ACTION" | "DO_NET_ACTION" ) n acf { value } ; (* K I O *)
decide          = "DECIDE_BY_AMONG" n dcf branch { branch } ;      (* branch i = DCF result i (0-based) : I *)
decide-with     = "DECIDE_BY_WITH_AMONG" n dcf data-block branch { branch } ;
                                                    (* DCF returns 1-based: children[1+i]; I + runtime *)
data-block      = "DataBlock" n { value } ;                                             (* K I *)
branch          = action ;
change-beh      = "CHANGE_TO_BEHAVIOR" n beh-ref ;                  (* n = 1 in all 1242 uses: I *)
beh-ref         = string                (* resolved, ONLY as 2nd token after keyword: P O *)
                | "_NO_BEH_CHANGE_" | "_PREVIOUS_BEH_" | number ;
change-plan     = "CHANGE_OF_PLAN" n plan-name plan-beh-ref ;      (* n = 2 in all 135 uses: I *)
plan-name       = string ;              (* 2nd token; emitted as string; names Data\Plans\<x>.txt *)
plan-beh-ref    = string | number ;     (* 3rd token: resolved by compiling the sibling plan: P O *)
agenda-add      = "ADD_AGENDA_ITEM" n agd { value } ;              (* n=1 => default init: runtime *)
agenda-remove   = "REMOVE_AGENDA_ITEM" n agd ;
timeout         = "SET_TIMEOUTMSG" n delay msg-id [ value [ value ] ] ;  (* 3 or 4 children: I *)
behaviour-param = "SET_BEHAVIOR_PARAM" n set-id { value } ;        (* id_SET_* of BehaviorSetParamFnIDs *)
interactor-param= "SET_INTERACTOR_PARAM" n set-id { value } ;      (* InteractorSetParamFnIDs; P O *)
                  (* SET_INTERACTOR_PARAM n id_SET_InteractorName string  declares a name (prescan) *)
debug           = ( "SET_DEBUG" | "SET_TRACE" ) n { value } ;      (* K O; runtime ignores class 6 *)

value           = number | identifier | string ;   (* identifier incl. specials and pronouns *)
acf = dcf = agd = set-id = delay = value ;          (* no namespace checking: sym_wrong_namespace O *)
n               = number | enumerator ;             (* any leaf works; see 5.4 *)
enumerator      = "_0_" | "_1_" | … | "_50_" ;
```

Where interactors and their parts may appear (I, from the shipped data):
- An interactor's optional parts always come in the order `next-beh` then `handler` (the
  `Enable` list), though the runtime would accept any order. 870 message interactors
  are `msg Enable`, 241 are `msg CHANGE_TO_BEHAVIOR` and 63 are `msg CHANGE_TO_BEHAVIOR Enable`.
- `DECLARE_BEHAVIOR` children after the name are `SET_BEHAVIOR_PARAM` and interactors only.
  `ADD_AGENDA_ITEM` occurs only inside action lists (`Enable`, `Block`, decision branches).
- `DECIDE_BY_*` branches are single actions. A `Block` is used where a branch needs several.

### 5.3 Positional rules (P, O)

Five counters count tokens, including ignored words, since the last `DECLARE_BEHAVIOR`,
`CHANGE_TO_BEHAVIOR`, `CHANGE_OF_PLAN`, `SET_INTERACTOR_PARAM` and `DECLARE_LOCAL_REALS`. When a
token is a **string**, it is resolved in this order:

1. its text is a declared local-real name → `0xEDCB0000|i`;
2. its text is a declared interactor name → `0xDCBA0000|i`;
3. it is token #3 after `DECLARE_BEHAVIOR` → string (the behaviour name);
4. it is token #2 after `CHANGE_OF_PLAN` → string (the plan name; remembered);
5. it is token #3 after `CHANGE_OF_PLAN` → index of that behaviour in the remembered plan. A
   fresh compiler prescans `Data\Plans\<plan>.txt`. Errors: `Could not find behavior named: …`;
   if the file is missing, `Plan file could not be opened.`;
6. it is token #2 after `CHANGE_TO_BEHAVIOR` → index of that behaviour in this plan, or
   `Could not find behavior named: …`;
7. it is token #3 after `SET_INTERACTOR_PARAM` → `Could not find interactor named: …` (known names
   were already handled by rule 2);
8. otherwise → string.

So the count must come **immediately** after the keyword, and the reference immediately after the
count. `CHANGE_TO_BEHAVIOR 1 Target: "S"` emits a *string* [beh_ref_label_shift].
`CHANGE_TO_BEHAVIOR 2 "S" "S"` resolves only the first [beh_ref_ctb_count2]. Behaviour names
are matched case-sensitively against the raw declared text (with `_`), and forward references
work [beh_ref_forward]. With duplicate names, the first wins [beh_dup_name]. An identifier
right after `DECLARE_LOCAL_REALS` must be numeric (`_N_`), otherwise the error is
`expected arg enumeration of localvars.`.

### 5.4 Counts (P, O count_*)

A count is an ordinary leaf. `2`, `_2_` and `0x2` give 2. `2.0` gives 0x40000000, which is a
broken tree. `-1` gives 0xFFFFFFFF. `32770` gives 0x8002: the compiler writes bit 15 through
unchanged, and the loader masks it off with `& 0x7FFF`. **The compiler never sets bit 15**, and
no shipped file has it. Its meaning, if it has one, belongs to the loader or runtime
(the HyperBlade port's PBN notes). The count may be any value; only the prescan positions require the
count token to be an identifier or number (`Expected an argument enumerator.`).

---

## 6. Prescan constraints (P, E, O)

Only these four keywords are prescanned. The prescan matches them **case-insensitively**. The
emitter matches all keywords case-sensitively, so a lower-case `declare_behavior` is registered
as a behaviour but emits no node [case_declare_lower]. The prescan also runs over any text after
`END_PLAN`.

| keyword | token+1 | token+2 | token+3 … |
|---|---|---|---|
| `DECLARE_BEHAVIOR` | identifier or number, else **Expected an argument enumerator.** | identifier, else **Expected an id_BEH_ type id.** | `"…`, else **Expected a string name in Behavior Declaration.** |
| `SET_MESSAGE_INTERACTOR` | identifier or number, else *Expected an argument enumerator.* | identifier or number, else **Expected an id_MSG_ type id.** | — |
| `SET_INTERACTOR_PARAM` | identifier or number, else *Expected an argument enumerator.* | if it is `id_SET_InteractorName` (stricmp): | `"…`, else *Expected a string name in Behavior Declaration.* |
| `DECLARE_LOCAL_REALS` | identifier or number, else **Expected an argument enumerator.  First two args are id and string name.** | *n* × `"…`, else **Expected a string name in LocalVar Declaration.** | |

Consequences: `DECLARE_BEHAVIOR(2 …` fails, because `(` starts a comment that swallows the rest
of the line [cmt_paren_call_syntax]. The article's `DECLARE_BEHAVIOR Named: …` fails
[article_*]. A behaviour type written as a bare number fails, but `_6_` passes
[beh_decl_numeric_id, beh_decl_enum_id].

---

## 7. Byte-identity checklist for a reimplementation

1. Process tokens strictly in order. A keyword emits `0xABCDEFFF id`, and every leaf emits as in
   §3. **Do not insert counts**: the count is the next source token.
2. Reproduce the tokenizer exactly: the separators `space TAB , &`, the four comment starts, each
   to the end of line, a glued `}` as a separate token, and no quote-awareness.
3. Classify identifiers by the case-sensitive prefixes `id`, `_`, `A_`, `AA_`. Then look up the
   specials exactly and the symbols with case-insensitive first-match, built from the four files
   in the exe's order (binary-evidence §5). Tag `PRN` symbols with 0xFEDC.
4. Numbers: apply the charset check, then `_N_`, hex, `.`→float32, else `atol`. Note
   `float(atof())` rounding (IEEE single, round-to-nearest).
5. Strings: `_`→space, end at the next quote, local-real and interactor-name substitution first,
   then the positional resolution of §5.3 using counters that **include ignored words**.
6. Write `ndwords` = all dwords emitted, then the dwords up to the first 0xFFFF0000 inclusive.
7. Unknown words are silently ignored. Report the original diagnostics with the same wording
   (§6 and binary-evidence §9) when emulating "original mode".

---

## 8. Canonical layout (used by `pbn.py decompile --source` and the examples)

```
<KEYWORD> <count> <leading leaves and strings on the same line>
    <nested statements, one per line, indented 4 spaces per level>
```

This layout is our choice. The compiler cannot see layout, so the original authors' layout is
unknown (**S**). Behaviour references are written as `"Name"` (they compile to the same index as
a number). Values are written as ENUMIDS / `AA_` / `id_SND_` names where the argument type is
known, and in decimal otherwise. A `CHANGE_OF_PLAN` target behaviour is written by name, so the
sibling `.txt` must be present; this affects CARRYROK, DEFENSE, HUMAN, OFFWING and SIDELN.

---

## 9. Pronouns and special constants (K, P, O, I)

- `id_PRN_*` (enum `Pronouns`, 58 symbols) compile to `0xFEDC0000 | index` and are resolved at
  run time (`0x522AC8` table). The tag comes from the *name* (`PRN` at characters 3..5), not from
  the argument position. A pronoun may stand anywhere a value may, including collision-bubble
  slots: TURNSTIL uses `SET_COLLISION_INTERACTOR 3 id_PRN_ThisTurnstileCOB id_COB_DromerBody_0`.
- The meanings below come from the runtime (I): `_BROADCAST_` is the recipient "everyone" for
  messages. `_MESSAGE_DATA_` is replaced with the triggering message's data by the ACF/DCF that
  reads it. `_NO_BEH_CHANGE_` / `_PREVIOUS_BEH_` are values for `CHANGE_TO_BEHAVIOR`.
  `_STRAIGHTAHEAD_` appears only as `DO_ACTION 2 id_ACF_SetObjectToWatch _STRAIGHTAHEAD_` (12
  uses). `_TEMPORARILY_` and `_PREVIOUS_BEH_` never appear in shipped plans; `_BROADCAST_`
  appears 40 times. The compiler treats all six as plain constants.

---

## 10. The article's authoring syntax (S, A)

The following form reproduces Listing 1 of Rosen & Duisberg 1996 ([article-1996.md](article-1996.md)).
**HYPERX.EXE rejects it** with `Expected an argument enumerator.` at line 1
[article_verbatim_cp1252, article_ascii_quotes].

```ebnf
(* SPECULATIVE: reconstructed from one 16-line listing; not accepted by the shipped compiler *)
a-behaviour   = "DECLARE_BEHAVIOR" "Named:" string "BehaviorType:" string { a-param } { a-interaction } ;
a-param       = "SET_BEHAVIOR_PARAM" label string ;                   (* Target: "TheGoal" *)
a-interaction = "SET_INTERACTION" "Message:" string "RESPONSE" { a-action } ;
a-action      = action-label ( string | number ) { string } ;         (* SetAnimation: "X", SendMessage: "A" "B" "C" *)
              | "ChangeToBehavior:" string ;
label         = name ":" ;
```

Its mapping to the compiler source is `DECLARE_BEHAVIOR 2+k id_BEH_<BehaviorType> "<Named>"`,
`SET_MESSAGE_INTERACTOR 2 id_MSG_<Message>` + `Enable k` (`RESPONSE`),
`DO_ACTION n id_ACF_<Label> args…` and `CHANGE_TO_BEHAVIOR 1 "<name>"`. This is a 1:1
correspondence apart from the counts and the symbol spelling. Whether a tool ever translated one
form into the other is open; see article-1996.md. The exe contains no trace of such a tool.

---

## 11. Confidence summary

| construct | confidence | basis |
|---|---|---|
| prefix notation + explicit child count after every keyword except END_PLAN | **certain** | P, E, O (32/32 byte-identical) |
| tokenizer (separators, comments to EOL, `}` token, no quoting) | **certain** | P, O |
| identifier / number / string classification; float vs int; hex; `_N_` | **certain** | P, O |
| keyword ids, special constants, PRN tag | **certain** | K, O |
| symbol table construction (4 files, order, first-match, stricmp) | **certain** | P, O (values match all 32 plans) |
| positional behaviour / plan / interactor resolution | **certain** | P, O |
| prescan rules and all 16 diagnostics | **certain** (except `Plan file could not be opened.` at nested depth, which is untested: oracle harness gap) | P, E, O |
| DECLARE_LOCAL_REALS → 0xEDCB, interactor names → 0xDCBA | **certain** for the compiler; runtime use untested | P, O |
| child conventions per statement (§5.2) | **high**: exact for the shipped data, but not enforced | I |
| SET_NET_COLLISION_INTERACTOR = 0x50002 | **certain** (exe table); intent unknown (probably 0x30002) | K, O |
| original authors' layout, count style (`2` vs `_2_`), symbol spellings, labels | **unknown**: discarded by the compiler | S |
| article syntax | **not accepted** by this exe; relationship unresolved | A, O |
