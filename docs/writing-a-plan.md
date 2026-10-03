# Writing a plan

This tutorial walks through the vending machine
([examples/vending_machine](../examples/vending_machine)), the example Rosen & Duisberg used in
1996 to explain ADLIB: a machine that waits for coins, tallies them, waits for a selection,
dispenses and gives change. The full rules are in [language.md](language.md); this page only
covers what you need to write plans.

## 1. The vocabulary comes first

A plan never defines names. Every message, action, decision and behaviour type it mentions comes
from the game's **vocabulary header**, a file of C enums that the game's C++ code shares
([VENDING.H](../examples/vending_machine/VENDING.H)):

```c
enum ID_MSG { id_MSG_Coin, id_MSG_Select, id_MSG_Refund, id_MSG_VendDone, id_MSG_Dispensed,
	id_MSG_Initiate,		// runtime-reserved: queued to self when a behaviour is entered
	id_MSG_ExitBehavior,	// runtime-reserved: sent to self before a behaviour change
	id_MSG_NOMSG };			// runtime-reserved: "no message"; also the table size

enum Behavior_IDs { id_BEH_Machine, id_BEH_Person, id_BEH_NUMBEHAVIORS };

enum ActionFunctionIDs {
	id_ACF_AddCredit, id_ACF_Display, id_ACF_Dispense, id_ACF_ReturnChange, id_ACF_SendMessage,
	id_ACF_Say,
	id_ACF_NUMACTIONFUNCTIONS };

enum DecisionFunctionIDs { id_DCF_EnoughCredit, id_DCF_InStock, id_DCF_NUMDECISIONFUNCTIONS };
```

- A symbol's value is its **position** in its enum (`id_ACF_Display` is 1). There is no `= value`
  in the original format.
- The **enum names** tell the runtime what each list is (`ID_MSG` = messages,
  `ActionFunctionIDs` = actions, ...; see [host-api.md](host-api.md#vocabulary)).
- Identifiers in plans must start with `id`, `_`, `A_` or `AA_`; the convention is
  `id_<KIND>_<Name>`. `id_PRN_*` names are **pronouns** ("Me", "Customer"), resolved by the game.
- Lists of names (texts, sounds, animations) can come from list files: `VENDTEXT.LST` gives
  `id_TXT_T_Welcome`, `id_TXT_T_Credit`, ...

## 2. Statements carry their child count

ADLIB source is prefix notation: **every keyword is followed by the number of children it has**,
and a child is one value, one string or one nested statement. Indentation is only for people.

```
DECLARE_BEHAVIOR 5 id_BEH_Machine "Idle"
```

`DECLARE_BEHAVIOR` has 5 children: the behaviour type `id_BEH_Machine`, the name `"Idle"`, and
the three interactors that follow. The compiler does not check counts; a wrong count silently
builds a different tree. Run `adlibc --strict` and `adlib lint` to catch that.

## 3. Behaviours and interactors

A plan is a list of behaviours, ended by `END_PLAN`. Behaviours are numbered 0, 1, 2... in
declaration order; the game usually starts a plan in behaviour 0. Inside a behaviour,
**interactors** say what happens when something arrives:

```
DECLARE_BEHAVIOR 5 id_BEH_Machine "Idle"
    SET_MESSAGE_INTERACTOR 2 id_MSG_Initiate        // sent to itself on entering a behaviour
        Enable 1
            DO_ACTION 2 id_ACF_Display id_TXT_T_Welcome
    SET_MESSAGE_INTERACTOR 3 id_MSG_Coin
        CHANGE_TO_BEHAVIOR 1 "HasCredit"            // the behaviour to switch to afterwards
        Enable 1
            DO_ACTION 1 id_ACF_AddCredit
    SET_MESSAGE_INTERACTOR 2 id_MSG_Select
        Enable 1
            DO_ACTION 2 id_ACF_Display id_TXT_T_InsertCoins
```

- `SET_MESSAGE_INTERACTOR n <message> [CHANGE_TO_BEHAVIOR ...] [Enable ...]` reacts to a message.
  `SET_COLLISION_INTERACTOR n <mine> <other> ...` reacts to a collision between two collision
  object ids.
- `Enable n` is the list of actions to run. The optional `CHANGE_TO_BEHAVIOR` before it is the
  behaviour to switch to afterwards. Put the change first, then `Enable`: that is the order all
  shipped plans use.
- `DO_ACTION n <action> <args...>` calls a C++ action registered by the game. The count includes
  the action id: `DO_ACTION 2 id_ACF_Display id_TXT_T_Welcome` is the action plus one argument.
- Entering a behaviour queues `id_MSG_Initiate` to the object itself (when the vocabulary
  declares it), so the `Initiate` interactor is the "on enter" hook.

## 4. Referring to behaviours

`CHANGE_TO_BEHAVIOR 1 "HasCredit"` names the target. The compiler replaces the string by the
behaviour's index, but only when the string is **the token right after the count**. Forward
references are fine, names are case-sensitive, and a name may not contain spaces (`_` in a name is
emitted as a space). Two special targets:

- `_NO_BEH_CHANGE_`: stay in the current behaviour;
- `_PREVIOUS_BEH_`: go back to the behaviour before this one.

## 5. Decisions

A decision is a C++ function that returns a branch number. `DECIDE_BY_AMONG n <decision>
<branch 0> <branch 1> ...` runs the branch it returns; each branch is a single statement, so
several actions are grouped in a `Block`:

```
    SET_MESSAGE_INTERACTOR 2 id_MSG_Select
        Enable 1
            DECIDE_BY_AMONG 3 id_DCF_EnoughCredit     // 0 = not enough, 1 = enough
                Block 2
                    DO_ACTION 2 id_ACF_Display id_TXT_T_NeedMore
                    CHANGE_TO_BEHAVIOR 1 _NO_BEH_CHANGE_
                DECIDE_BY_AMONG 3 id_DCF_InStock
                    Block 3
                        DO_ACTION 2 id_ACF_Display id_TXT_T_SoldOut
                        DO_ACTION 1 id_ACF_ReturnChange
                        CHANGE_TO_BEHAVIOR 1 "Idle"
                    CHANGE_TO_BEHAVIOR 1 "Vending"
```

Decisions nest. A `CHANGE_TO_BEHAVIOR` *inside* an action list sets the interactor's target **for
good** (an ADLIB quirk: the next time the interactor fires it still has that target), so when one
branch changes behaviour, give every branch an explicit target, as above.

In networked games every decision outcome is recorded into the event's **decision path** and
replayed on the other machines instead of being re-decided ([host-api.md](host-api.md#decision-paths-and-lockstep-networking)).
Only about three nested decisions fit into one path.

## 6. Timers, messages to others, pronouns

```
DECLARE_BEHAVIOR 5 id_BEH_Machine "Vending"
    SET_MESSAGE_INTERACTOR 2 id_MSG_Initiate
        Enable 2
            DO_ACTION 1 id_ACF_Dispense
            SET_TIMEOUTMSG 3 100 id_MSG_VendDone id_PRN_Me   // in 100/100 s, to itself
    SET_MESSAGE_INTERACTOR 2 id_MSG_Coin
        Enable 1
            DO_ACTION 2 id_ACF_Display id_TXT_T_Busy
    SET_MESSAGE_INTERACTOR 3 id_MSG_VendDone
        CHANGE_TO_BEHAVIOR 1 "Idle"
        Enable 3
            DO_ACTION 3 id_ACF_SendMessage id_PRN_Customer id_MSG_Dispensed
            DO_ACTION 2 id_ACF_Display id_TXT_T_ThankYou
            DO_ACTION 1 id_ACF_ReturnChange

END_PLAN
```

- `SET_TIMEOUTMSG 3 <delay> <message> <recipient>` schedules a message (delay in hundredths of a
  second).
- Messaging another object is an ordinary action the game provides (`SendMessage` here).
- `id_PRN_Me` is fixed when the plan loads; `id_PRN_Customer` is resolved each time it is used.
  Which is which is the game's choice (`Registry::loadPronoun`, `Registry::runtimePronoun`).
- `_MESSAGE_DATA_` as an argument passes the data word of the message being handled.

Other statements: `GLOBAL_INTERACTORS n ...` (interactors active in every behaviour, before the
first behaviour), `SET_BEHAVIOR_PARAM`, `SET_INTERACTOR_PARAM`, `ADD_AGENDA_ITEM` /
`REMOVE_AGENDA_ITEM` (long-running activities), `DECIDE_BY_WITH_AMONG` (a decision with a
`DataBlock` of arguments), `CHANGE_OF_PLAN n "PLAN" "Behaviour"` (switch to another plan of the
same object), `DECLARE_LOCAL_REALS`. See [language.md](language.md) §4 and §6.

## 7. Lexical details

- Comments start with `//`, `/*`, `(` or `#` and **always run to the end of the line**; `/*` does
  not look for `*/`. So `DECLARE_BEHAVIOR(2 ...` is a keyword followed by a comment.
- Space, tab, `,` and `&` separate tokens. Strings have no escapes and no spaces.
- Keywords are case-sensitive (`Enable`, `Block` and `DataBlock` are mixed case); symbol lookup is
  case-insensitive.
- A word that is neither keyword, number, string nor identifier is **silently dropped** (it still
  counts for the positional rules). `adlibc --strict` warns about it.

## 8. Compile, check, look

```sh
adlibc --enumids VENDING.H --texts VENDTEXT.LST --strict -o VENDING.PBN VENDING.txt
adlib --symbols . lint VENDING.PBN                 # structural lint
adlib --symbols . graph --mermaid VENDING.PBN      # behaviour graph
adlib --symbols . decompile -o - VENDING.PBN       # back to source, byte-identical on recompile
```

Errors use the 1996 compiler's own messages with a line and byte offset, e.g.

```
VENDING.txt:19: error [compile/throw] (byte 1085): Bad plan id value: id_TXT_T_Welcome
```

(here the texts list was not given). See [adlibc.md](adlibc.md) for modes and warnings.
