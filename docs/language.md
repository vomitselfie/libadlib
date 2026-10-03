# ADLIB Plans language specification

This is the specification of the ADLIB Plans source language as the 1996 ADLIB compiler (shipped
inside HyperBlade's executable) accepts and translates it. It contains **only rules confirmed by running
that compiler**: every rule is marked **CONFIRMED BY ORACLE** and names the fixtures that prove it.
Inferred conventions, speculation and the 1996 article's different notation are research material
([research/grammar.md](research/grammar.md), [research/article-1996.md](research/article-1996.md)).
The implementation is `adlibc` ([adlibc.md](adlibc.md)).

**How a rule is confirmed.** Each rule was confirmed by running the original 1996 compiler (the
routine inside HyperBlade's executable, under emulation; the *oracle*) on test sources. A fixture
is a source file plus a `.expected.json` recording what the original compiler did with it: the PBN
it wrote (sha256 and every dword) or the exact diagnostic, with line and byte offset. Fixture names
below are relative to `fixtures/compiler/` (`valid/`, `quirks/`, `errors/`, `fuzz-regressions/`);
their symbols come from the package's test vocabulary (`fixtures/vocab/`). `corpus` means the 32
plans shipped with HyperBlade, which rebuild byte-identically from source (not part of this
package). `ctest` replays every fixture against adlibc. In addition, 17 547 random and mutated
sources agreed byte for byte between adlibc and the original
([compatibility.md](compatibility.md)).

Notation: EBNF; `CR`, `LF`, `TAB`, `NUL` are the bytes 0x0D, 0x0A, 0x09, 0x00.

---

## 1. Source text

1. A source is a sequence of bytes (cp1252/ASCII). There is no preprocessor and no include.
   CONFIRMED BY ORACLE: `valid/cmt_hash` (`#include` is a comment).
2. A source of 79 999 bytes or more is rejected with
   `This plan file is too large.  Why not break it up into separate plans?`.
   CONFIRMED BY ORACLE: `errors/limit_too_large`.
3. A plan's source is `Data\Plans\<plan>.txt`; `CHANGE_OF_PLAN` reads other plans' sources by
   the same rule (§7.1). CONFIRMED BY ORACLE: `quirks/cop_sibling`.

## 2. Lexical structure

```ebnf
source        = { separator | line-end | comment | token } ;
separator     = " " | TAB | "," | "&" ;
line-end      = LF | CR LF ;
comment       = ( "//" | "/*" | "(" | "#" ) { byte - LF } ;      (* to the end of the line *)
token         = "}" | word ;
word          = word-byte { word-byte } ;
word-byte     = byte - ( separator | CR | LF | "}" | NUL ) ;      (* and a comment start ends it *)
```

| rule | CONFIRMED BY ORACLE |
|---|---|
| Space, TAB, `,` and `&` separate tokens; `;`, `:`, `{`, `)` are ordinary word bytes | `valid/sep_commas`, `valid/sep_ampersand`, `valid/sep_semicolon`, `quirks/article_named_label_elsewhere` |
| LF and CR LF end lines; layout, indentation and blank lines carry no meaning | `valid/base_minimal_lf`, `valid/base_minimal_crlf`, `valid/base_one_line`, `valid/base_tabs_blank_lines`, `valid/base_no_final_newline` |
| A bare CR is not a line end: it swallows the next line (here `END_PLAN`) | `errors/base_cr_only` |
| `//`, `/*`, `(` and `#` each start a comment that runs to the end of the line; `/*` does not look for `*/`, `(` does not look for `)` | `valid/cmt_slashslash`, `valid/cmt_hash`, `valid/cmt_paren_rest_of_line`, `quirks/cmt_block_single_line`, `quirks/cmt_block_multiline` |
| A comment start ends the word it is glued to | `valid/cmt_glued` |
| `DECLARE_BEHAVIOR(2 ...` is the keyword followed by a comment | `errors/cmt_paren_call_syntax`, `errors/paren_count` |
| `}` is always a one-byte token, even glued (`"S"}}` is three tokens); `{` is an ordinary byte | `quirks/brace_glued_close`, `quirks/brace_glued_open`, `quirks/brace_tokens` |
| There is no quoting in the tokenizer: `"Two Words"` is two tokens | `errors/str_space_inside` |
| A byte 0x0B/0x0C (vertical tab, form feed) is a word byte, not a separator | `fuzz-regressions/fz_seed1_vertical_tab` |

## 3. Token classes

Each word is classified by its first bytes, in this order. Tests are on bytes and
case-sensitive.

```ebnf
identifier = ( "id" | "_" | "A_" | "AA_" ) { word-byte } ;
number     = ( digit | "." | "+" | "-" ) { word-byte } ;
string     = '"' { word-byte } ;
other      = word - identifier - number - string ;        (* keyword, or dropped *)
```

### 3.1 Keywords

The 27 keywords and the ids they emit (`0xABCDEFFF, id`):

| keyword | id | keyword | id |
|---|---|---|---|
| `SET_COLLISION_INTERACTOR` | 0x00030000 | `SET_BEHAVIOR_PARAM` | 0x00070003 |
| `SET_MESSAGE_INTERACTOR` | 0x00050000 | `SET_INTERACTOR_PARAM` | 0x00180001 |
| `SET_LOC_COLLISION_INTERACTOR` | 0x00030001 | `DO_ACTION` | 0x00180000 |
| `SET_NET_COLLISION_INTERACTOR` | **0x00050002** | `DO_LOCAL_ACTION` | 0x00180002 |
| `SET_LOC_MESSAGE_INTERACTOR` | 0x00050001 | `DO_NET_ACTION` | 0x00180003 |
| `SET_NET_MESSAGE_INTERACTOR` | 0x00050002 | `DECIDE_BY_AMONG` | 0x00170000 |
| `ADD_AGENDA_ITEM` | 0x001A0001 | `DECIDE_BY_WITH_AMONG` | 0x00170001 |
| `REMOVE_AGENDA_ITEM` | 0x001A0003 | `SET_DEBUG` | 0x00060001 |
| `SET_TIMEOUTMSG` | 0x001A0002 | `SET_TRACE` | 0x00060002 |
| `GLOBAL_INTERACTORS` | 0x001B0000 | `Enable` | 0x80000000 |
| `CHANGE_OF_PLAN` | 0x00160000 | `Block` | 0x40000000 |
| `CHANGE_TO_BEHAVIOR` | 0x00070002 | `DataBlock` | 0x20000000 |
| `DECLARE_BEHAVIOR` | 0x00070000 | `END_PLAN` | 0xFFFF0000 |
| `DECLARE_LOCAL_REALS` | 0x00160001 | | |

CONFIRMED BY ORACLE: the corpus (every keyword the shipped plans use), `quirks/unused_do_local_action`,
`quirks/unused_do_net_action`, `valid/unused_loc_msg_interactor`, `valid/unused_net_msg_interactor`,
`valid/unused_loc_coll_interactor`, `quirks/unused_net_coll_interactor` (SET_NET_COLLISION_INTERACTOR
emits 0x00050002, the same id as SET_NET_MESSAGE_INTERACTOR), `valid/unused_debug_trace`,
`quirks/unused_datablock_block`, `valid/unused_global_interactors_empty`, `quirks/unused_local_reals`.

Keywords match **exactly and case-sensitively**. CONFIRMED BY ORACLE: `quirks/case_keyword_upper`,
`quirks/case_enable_lower`, `quirks/lowercase_keyword`.

### 3.2 Dropped words

A word that is not an identifier, number or string and not a keyword emits **nothing**, but it is
still a token for the positional rules (§7). CONFIRMED BY ORACLE: `quirks/unknown_keyword`,
`quirks/article_named_label_elsewhere`, `quirks/brace_tokens`, `quirks/brace_between_ctb`,
`quirks/beh_ref_label_shift`, `quirks/sym_RET_ignored` (`RET_*` can never be referenced),
`quirks/case_symbol_upper_body` (`ID_ACF_...` is not an identifier), `quirks/str_curly_quotes`
(cp1252 curly quotes are not quotes).

### 3.3 Identifiers

1. After the prefix test, the identifier is looked up in the **special-constant table** (exact,
   case-sensitive), then in the **symbol table** (case-insensitive, first match wins). Unknown
   identifiers are an error `Bad plan id value: <first 20 bytes of the token>`.
   CONFIRMED BY ORACLE: `quirks/sym_specials`, `errors/case_special_lower`, `valid/case_symbol_mixed`,
   `errors/case_symbol_upper`, `errors/sym_unknown`, `errors/sym_idle_word` (`idle` starts with
   `id`), `errors/bad_plan_id`, `errors/unknown_action`, `errors/msg_bad_id_truncated` (20 bytes).
2. Special constants: `_BROADCAST_` = 0x0000FFFF, `_STRAIGHTAHEAD_` = 0xABCDABCD, `_TEMPORARILY_` =
   0xABCDABCC, `_MESSAGE_DATA_` = 0xABCDABCE, `_NO_BEH_CHANGE_` = 0xABCDABCB, `_PREVIOUS_BEH_` =
   0xABCDABCA, and the *argument enumerators* `_0_` ... `_50_` = 0 ... 50 (`_51_` is unknown).
   CONFIRMED BY ORACLE: `quirks/sym_specials`, `quirks/sym_enumerators_as_values`,
   `valid/count_enumerator_form`, `errors/count_enumerator_51`.
3. The symbol table is built from four files, in this order: `Data\enumIDs.h` (every token inside
   `{ }` is a symbol whose value is its position in that enum), `Data\AnimData\Animassm.txt` (`AA_*`
   tokens, numbered), `Data\Sndfiles.lst` (`id_SND_<word>`, numbered), `Data\Infobtxt.lst`
   (`id_TXT_<first word of each line>`, numbered). CONFIRMED BY ORACLE: the corpus (all symbol
   values), `quirks/sym_anim`, `quirks/sym_sound`, `quirks/sym_infotext`, `quirks/sym_direction_A_`,
   `quirks/sym_duplicate_first_wins`.
4. A symbol whose table name has `PRN` at bytes 3..5 (`id_PRN_*`) emits `0xFEDC0000 | value`.
   CONFIRMED BY ORACLE: `quirks/sym_pronoun`, the corpus.
5. There is no type or namespace check: a symbol emits its value wherever it appears.
   CONFIRMED BY ORACLE: `quirks/sym_wrong_namespace`, `quirks/wrong_enum_namespace`,
   `valid/beh_decl_enum_id`.

### 3.4 Numbers

Every byte must be one of `xXABCDEFabcdef0123456789.+-_`, otherwise the error is
`Bad number in plan: <first 20 bytes of the token>`. Then the first matching rule applies:

| form | value | CONFIRMED BY ORACLE |
|---|---|---|
| starts with `0x`/`0X` and a hex digit follows | the hex digits read (raw dword, up to the first non-hex byte) | `quirks/num_hex_lower`, `quirks/num_hex_upper`, `quirks/num_hex_floatbits`, `valid/count_hex` |
| contains `.` | IEEE single of the decimal prefix; `e`, `E`, `d`, `D` mark an exponent, an exponent without digits is ignored | `quirks/num_float`, `quirks/num_float_one_point_zero`, `quirks/num_float_leading_dot`, `quirks/num_float_trailing_dot`, `quirks/num_float_neg`, `quirks/num_exponent_dot`, `quirks/num_exponent_d` |
| otherwise | the signed decimal prefix, modulo 2^32 (`1e3` = 1, `12ab` = 12, `1_000` = 1) | `quirks/num_int`, `quirks/num_neg`, `quirks/num_plus`, `quirks/num_big`, `quirks/num_exponent`, `quirks/num_hexdigit_garbage`, `quirks/num_underscore` |
| a byte outside the set | error | `errors/num_bad_char`, `errors/msg_bad_number_truncated` |

`1` and `1.0` are different dwords (1 and 0x3F800000). CONFIRMED BY ORACLE: `quirks/num_float_one_point_zero`.

A token that is exactly `0x` (or `0X`) has no defined value: the original emits uninitialised
stack memory. CONFIRMED BY ORACLE: `fuzz-regressions/fz_seed2_case01469` (and two more cases).

### 3.5 Strings

1. A string token emits `0xABCDEFFE`, one dword per character up to the next `"`, then `0`. `_`
   is emitted as a space; there are no escapes; anything after the closing quote in the same
   token is lost. CONFIRMED BY ORACLE: `quirks/str_basic`, `quirks/str_underscore_space`,
   `quirks/str_escape`, `quirks/str_behname_with_space`.
2. `""` (and any other string text) that equals a declared local-real or interactor name emits
   that name's reference instead (§7.1); with no local reals declared `""` emits 0xEDCB0000.
   CONFIRMED BY ORACLE: `quirks/str_empty`.
3. If no closing quote is found within 41 bytes the error is `Inability to read string: <token>`.
   CONFIRMED BY ORACLE: `errors/str_unterminated`, `errors/unterminated_string`,
   `errors/str_space_inside`.
4. The quote search continues past the end of the token into the bytes an earlier, longer token
   left in the same buffer, so an unterminated string can be completed by stale bytes.
   CONFIRMED BY ORACLE: `quirks/str_stale_bytes`.
5. A string token of 40 bytes or more (quotes included) crashes the original compiler; 39 bytes
   is the maximum. CONFIRMED BY ORACLE: `quirks/tok_len_str_30` ... `quirks/tok_len_str_39`
   (accepted), `errors/tok_len_str_40` ... `errors/tok_len_str_43` (crash).

## 4. Statements

A statement is a keyword followed by its **child count** and then that many children; a child is
one value, one string or one nested statement, however many dwords it encodes to. `END_PLAN` has
no count.

```ebnf
statement = keyword count { child } | "END_PLAN" ;
count     = number | identifier ;              (* any leaf: emitted as written *)
child     = statement | number | identifier | string ;
```

1. The count is an ordinary leaf, emitted exactly as written; the compiler does not count
   children. `2`, `_2_` and `0x2` are 2; `2.0` is 0x40000000; `-1` is 0xFFFFFFFF; `32770` keeps bit
   15. CONFIRMED BY ORACLE: `quirks/count_is_children_not_tokens`, `valid/count_enumerator_form`,
   `valid/count_hex`, `quirks/count_float`, `quirks/count_negative`, `valid/count_bit15`.
2. A wrong count is not an error; it changes the tree the game reads.
   CONFIRMED BY ORACLE: `quirks/count_too_large`, `quirks/count_too_small`,
   `quirks/count_missing_action`, `quirks/wrong_child_count_too_large`,
   `quirks/wrong_child_count_too_small`.
3. The output must contain `END_PLAN`; without it the original writes past its output buffer.
   CONFIRMED BY ORACLE: `errors/base_missing_end_plan`, `errors/missing_end_plan`.

## 5. The PBN produced

The file is `u32 n` followed by the dwords emitted up to and including the first 0xFFFF0000; `n`
counts **every** dword emitted, including those after `END_PLAN`. CONFIRMED BY ORACLE:
`quirks/base_tokens_after_end`, every accepted fixture (`pbn_words`).

## 6. Plans as the shipped data writes them

The 32 plans shipped with HyperBlade, written in the form below, rebuild byte-identically.
CONFIRMED BY ORACLE: the corpus.
These shapes are what the ADLIB runtime reads; the compiler itself enforces none of them (§4).

```ebnf
plan        = [ "GLOBAL_INTERACTORS" n { interactor } ] behaviour { behaviour } "END_PLAN" ;
behaviour   = "DECLARE_BEHAVIOR" n identifier string { "SET_BEHAVIOR_PARAM" n value { value } | interactor } ;
interactor  = "SET_MESSAGE_INTERACTOR" n value [ change ] [ handler ]
            | "SET_COLLISION_INTERACTOR" n value value [ change ] [ handler ] ;
handler     = "Enable" n { action } ;
action      = "DO_ACTION" n value { value | string }
            | "DECIDE_BY_AMONG" n value action { action }
            | "DECIDE_BY_WITH_AMONG" n value "DataBlock" n { value } action { action }
            | "Block" n { action }
            | change | "CHANGE_OF_PLAN" n string ( string | value )
            | "ADD_AGENDA_ITEM" n value { value } | "REMOVE_AGENDA_ITEM" n value
            | "SET_TIMEOUTMSG" n value value [ value [ value ] ]
            | "SET_BEHAVIOR_PARAM" n value { value } ;
change      = "CHANGE_TO_BEHAVIOR" n ( string | value ) ;
value       = number | identifier ;
```

Here `n` is the number of children (the shipped plans always state it correctly).

## 7. Positional rules

The compiler keeps, per keyword `DECLARE_BEHAVIOR`, `CHANGE_TO_BEHAVIOR`, `CHANGE_OF_PLAN`,
`SET_INTERACTOR_PARAM` and `DECLARE_LOCAL_REALS`, a count of tokens since the keyword's last
occurrence. Every token counts, including dropped words (§3.2).

### 7.1 String resolution

A string token is translated by the first rule that applies:

1. its text is a declared local-real name: `0xEDCB0000 | index`. CONFIRMED BY ORACLE: `quirks/unused_local_reals`;
2. its text is a declared interactor name: `0xDCBA0000 | index`. CONFIRMED BY ORACLE: `valid/unused_interactor_name`;
3. it is the 3rd token after `DECLARE_BEHAVIOR`: a string (the behaviour's name). CONFIRMED BY ORACLE: the corpus;
4. it is the 2nd token after `CHANGE_OF_PLAN`: a string (the plan name). CONFIRMED BY ORACLE: `quirks/cop_sibling`;
5. it is the 3rd token after `CHANGE_OF_PLAN`: the index of that behaviour in the named plan, found
   by reading that plan's source; `Could not find behavior named: <name>` otherwise. A number there
   needs no sibling. CONFIRMED BY ORACLE: `quirks/cop_sibling`, `errors/cop_unknown_beh`, `quirks/cop_numeric`;
6. it is the 2nd token after `CHANGE_TO_BEHAVIOR`: the index of that behaviour in this plan
   (declaration order from 0; forward references work; names compare case-sensitively, with `_`
   as written; the first of duplicate names wins); `Could not find behavior named: <name>`
   otherwise. CONFIRMED BY ORACLE: `quirks/beh_ref_forward`, `quirks/beh_ref_numeric`,
   `errors/beh_ref_unknown`, `errors/case_behname`, `quirks/beh_dup_name`,
   `quirks/str_behname_with_space`, `errors/unknown_behavior_name`;
7. it is the 3rd token after `SET_INTERACTOR_PARAM`: error `Could not find interactor named: <name>`
   (declared names were taken by rule 2). CONFIRMED BY ORACLE: `errors/unused_interactor_unknown`;
8. otherwise: a string.

So the reference must follow the count directly: `CHANGE_TO_BEHAVIOR 1 Target: "S"` emits a
string, and in `CHANGE_TO_BEHAVIOR 2 "S" "S"` only the first is resolved. CONFIRMED BY ORACLE:
`quirks/beh_ref_label_shift`, `quirks/brace_between_ctb`, `quirks/beh_ref_ctb_count2`.

The plan name in rule 5 is the last string at rule-4 position **or the last keyword seen**: any
non-string token in between replaces it. CONFIRMED BY ORACLE: `quirks/cop_plan_name_keyword`
(`CHANGE_OF_PLAN 2 Enable "One"` reads `Data\Plans\Enable.txt`).

### 7.2 Declarations (read before translation)

Before translating, the compiler reads the whole source (including text after `END_PLAN`) and
records names. It recognises four keywords **case-insensitively** here:

| keyword | then | errors | CONFIRMED BY ORACLE |
|---|---|---|---|
| `DECLARE_BEHAVIOR` | an identifier or number; an identifier; a string (the name) | `Expected an argument enumerator.` / `Expected an id_BEH_ type id.` / `Expected a string name in Behavior Declaration.` | `errors/beh_decl_numeric_id`, `valid/beh_decl_enum_id`, `errors/beh_decl_no_name`, `errors/count_missing_decl`, `errors/article_verbatim_cp1252`, `errors/article_ascii_quotes`, `errors/article_ascii_plus_end` |
| `SET_MESSAGE_INTERACTOR` | an identifier or number; an identifier or number | `Expected an argument enumerator.` / `Expected an id_MSG_ type id.` | `errors/msg_interactor_no_count`, `errors/msg_interactor_string`, `quirks/msg_interactor_numeric` |
| `SET_INTERACTOR_PARAM` | an identifier or number; if then `id_SET_InteractorName` (any case), a string: a declared interactor name | `Expected an argument enumerator.` / `Expected a string name in Behavior Declaration.` | `valid/unused_interactor_name` |
| `DECLARE_LOCAL_REALS` | an identifier or number *n*; *n* strings: declared local-real names | `Expected an argument enumerator.  First two args are id and string name.` / `Expected a string name in LocalVar Declaration.` | `errors/unused_local_reals_no_count`, `errors/unused_local_reals_not_string`, `valid/unused_local_reals_enumerator` |

A lower-case `declare_behavior` therefore declares a behaviour but emits no node.
CONFIRMED BY ORACLE: `quirks/case_declare_lower`. During translation an identifier right after
`DECLARE_LOCAL_REALS` must be an argument enumerator, else `expected arg enumeration of localvars.`.
CONFIRMED BY ORACLE: `errors/unused_local_reals_symbol_count`.

### 7.3 Table sizes

Names are kept in fixed tables (60 behaviours, 60 local reals, 150 interactor names, 32 bytes
each) without bounds checks. A 61st behaviour name lands in the local-real table, a 61st
local-real name in the interactor-name table, and a name longer than 31 characters spills into
the next entry. CONFIRMED BY ORACLE: `quirks/beh_61_overflow`, `quirks/local_reals_61`,
`quirks/beh_name_spill`, `valid/tok_len_prescan_30` ... `quirks/tok_len_prescan_39`.

## 8. Diagnostics

The original stops at the first error. Its messages, verbatim (several end in a newline):

| message | CONFIRMED BY ORACLE |
|---|---|
| `This plan file is too large.  Why not break it up into separate plans?` | `errors/limit_too_large` |
| `Expected an argument enumerator.` | `errors/paren_count`, `errors/cmt_paren_call_syntax`, `errors/msg_interactor_no_count` |
| `Expected an argument enumerator.  First two args are id and string name.` | `errors/unused_local_reals_no_count` |
| `Expected an id_BEH_ type id.` | `errors/beh_decl_numeric_id`, `errors/count_missing_decl` |
| `Expected a string name in Behavior Declaration.` | `errors/beh_decl_no_name` |
| `Expected an id_MSG_ type id.` | `errors/msg_interactor_string` |
| `Expected a string name in LocalVar Declaration.` | `errors/unused_local_reals_not_string` |
| `expected arg enumeration of localvars.` | `errors/unused_local_reals_symbol_count` |
| `Bad plan id value: <token, first 20 bytes>` | `errors/bad_plan_id`, `errors/sym_unknown`, `errors/msg_bad_id_truncated` |
| `Bad number in plan: <token, first 20 bytes>` | `errors/num_bad_char`, `errors/msg_bad_number_truncated` |
| `Inability to read string: <token>` | `errors/str_unterminated`, `errors/str_space_inside` |
| `Could not find behavior named: <name>` | `errors/beh_ref_unknown`, `errors/cop_unknown_beh` |
| `Could not find interactor named: <name>` | `errors/unused_interactor_unknown` |

The position reported is the compiler's cursor, just after the failing token (and after the
separators following it); `line` counts the LFs before it. CONFIRMED BY ORACLE: every `errors/`
fixture (`line`, `byte`).

Inputs on which the original compiler crashes rather than reporting an error: string tokens of 40+
bytes, any token of 44+ bytes, identifiers of about 60 bytes. CONFIRMED BY ORACLE:
`errors/tok_len_str_40`, `errors/tok_len_prescan_40`, `errors/limit_long_symbol` (the oracle
reports an emulation fault; adlibc reports kind `crash`).
