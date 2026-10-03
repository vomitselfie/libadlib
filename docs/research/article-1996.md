# The 1996 ADLIB article

Research notes. The confirmed language is in [../language.md](../language.md).

## Citation

Stuart Rosen & Robert Duisberg, **"Bringing Life to HyperBlade"**, in the *Anatomy of a Game*
column, *Game Developer* magazine, August/September 1996, pp. 22–25 (Miller Freeman).
The scan is on GDC Vault: <https://media.gdcvault.com/GD_Mag_Archives/GDM_AugSept_1996.pdf>
(PDF pp. 11–14). The authors were "the two principals of WizBang!", the developer of HyperBlade
for Activision. The coauthor is Stuart Rosen, not "Ted".

This package does not contain a copy of the article. Read it in the scan above; the notes
below cite short passages and summarise the rest.

## What the article says

### Listing 1 (the only code sample)

This is reproduced exactly as printed, including the stray `a` after `“High”` and the
typographic quotes. The comment lines are indented in the print.

```
Listing 1. Sample ADLIB Code

DECLARE_BEHAVIOR Named: “GoForTheGoal” BehaviorType: “GlideToTarget”
SET_BEHAVIOR_PARAM Target: “TheGoal”
SET_INTERACTION Message: “YerHit”
            //Sent by the collision detection mechanism
RESPONSE
SetAnimation: “DoubleBackFlip”
            //which has a trigger set at the 35th frame
SET_INTERACTION Message: “YerDown”
            //Sent by the trigger in the previously set animation
RESPONSE
SetFriction: “High”a
SetUserControl: “Disabled”
PlaySoundType: “Yelp”
DecrementDamageBar: 20
SendMessage: “TeamManager” “ImDown” “MyDamageLevel”
ChangeToBehavior: “GetUp”
```

### On the language

The authors name the system "ADLIB (Authoring and Design Language for Interactive Behavior)" and
list "three parts": a language for describing behaviours and interactions, an authoring
environment, and a run-time system that executes the descriptions. The descriptions are called
*Plans*. They describe the language as "a declarative, object-oriented language with a extensible
vocabulary" in which a Plan is a collection of Behavior declarations and interactor declarations.
At its simplest it defines a finite-state machine (behaviours as states, interactions as
transitions); their worked example is a **vending machine** that waits for a coin, tallies coins,
waits for a selection and makes change (the example this package ships, see
[../writing-a-plan.md](../writing-a-plan.md)). Behaviours are composed of simultaneous
*Activities*, so a change of behaviour can look like a blend rather than a switch. An interaction
can invoke a decision function to choose whether, and to which behaviour, to change.

### On the authoring environment and compiler

Authors typed the language as text; "smart editing" (syntax- and template-driven) was a stated
goal, not a 1996 feature. Behaviour specifications are "recast as declarative data", and "the
p-code compiler is integrated into the game itself", so plans could be changed and tested without
quitting the game.

### On extensibility

The article describes libraries of activities, behaviours, interactions and decision criteria,
and "facilities ... for defining new actions, decisions, and messages" as compiled C++.

### On the runtime and p-code

Three stages: the author's text is compiled into "a compact p-code representation"; the p-code
builds a network of linked objects when a character adopts a Plan; switching and blending
behaviours then has little overhead. Interactions are processed "at rates of several dozen per
second". Their example of fine-grained interaction: a falling animation messages the skater at its
35th frame, which may make the skater slide to a halt, take damage, yelp and message the team
manager (this is Listing 1).

### On networking

Instead of transmitting behaviour, "you send indices into duplicate plan instruction streams on
either side of the network", so behaviours run locally and stay in sync.

### On cameras

Cameras are ADLIB objects with their own Plans and Behaviors, tuned by non-programmers through
plan parameters ("spring constants and damping factors"). This matches the float arguments of
HyperBlade's camera plans.

## Analysis: what the binary confirms

| Article claim | Evidence in HYPERX.EXE / data | Verdict |
|---|---|---|
| Plans = Behavior declarations + interactor declarations | `DECLARE_BEHAVIOR`, `SET_MESSAGE_INTERACTOR`, `SET_COLLISION_INTERACTOR`, `GLOBAL_INTERACTORS` | confirmed |
| FSM: behaviours = states, interactions = transitions | `CHANGE_TO_BEHAVIOR` in interactor action lists; one active behaviour per plan | confirmed |
| Behaviours are composed of simultaneous "Activities" | agenda items (`ADD_AGENDA_ITEM`, classes `CGlideOnSurface`, `CTimerActivity`, …); `id_SET_Digressive` keeps them across a change | confirmed. The article's "Activity" = the agenda item (`CTimerActivity` keeps the word) |
| An interaction can invoke a decision function to choose the next behaviour | `DECIDE_BY_AMONG` / `DECIDE_BY_WITH_AMONG` + DCF table 0x527660 | confirmed. Neural or fuzzy DCFs are not evidenced; the DCFs are plain C++ |
| The author's text is compiled to compact p-code | text `.txt` → `.PBN` dword stream | confirmed |
| p-code → network of linked objects when a plan is adopted | PBN loader clones behaviour / interactor / agenda prototypes (HyperBlade port: PBN loader) | confirmed |
| "p-code compiler is integrated into the game itself … without quitting the game" | the CPlanCompiler in HYPERX.EXE, plus the dev Plans dialog: Compile, Compile All, Load Plan (hot reload), force a behaviour | confirmed (binary-evidence §10) |
| "authors type in the text of the language" | prefix source in `Data\Plans\*.txt`; "smart editing" was only a goal | consistent |
| "send indices into duplicate plan instruction streams … in sync" | the 16-bit decision path recorded and replayed per event (the runtime's decision paths, [../host-api.md](../host-api.md)) | confirmed |
| "facilities … for defining new actions, decisions, and messages … compiled C++" | ACF/DCF/agenda/behaviour tables indexed by ENUMIDS.H enums; the C header is shared with the C++ code | confirmed (static tables, recompile needed) |
| Cameras are ADLIB objects with plans | CAMERA.PBN, HELMCAM.PBN | confirmed |

## Listing 1 versus the compiler in the exe

Each article construct has an exact counterpart in the shipped vocabulary:

| Article (authoring syntax) | Exe compiler source | Symbol exists in ENUMIDS.H? |
|---|---|---|
| `DECLARE_BEHAVIOR Named: “GoForTheGoal” BehaviorType: “GlideToTarget”` | `DECLARE_BEHAVIOR <n> id_BEH_<type> "GoForTheGoal"` | `GlideToTarget` no; closest are `id_BEH_GlideToObject`, `id_BEH_GlideToPosition` |
| `SET_BEHAVIOR_PARAM Target: “TheGoal”` | `SET_BEHAVIOR_PARAM 2 id_SET_Target <value>` | `id_SET_Target` yes |
| `SET_INTERACTION Message: “YerHit”` | `SET_MESSAGE_INTERACTOR 2 id_MSG_YerHit` | `id_MSG_YerHit` yes |
| `RESPONSE` | `Enable <n>` | — |
| `SetAnimation: “DoubleBackFlip”` | `DO_ACTION 2 id_ACF_SetAnimation AA_…` | `id_ACF_SetAnimation` yes |
| `SET_INTERACTION Message: “YerDown”` | `SET_MESSAGE_INTERACTOR 2 id_MSG_YerDown` | `id_MSG_YerDown` yes |
| `SetFriction: “High”` | `DO_ACTION 2 id_ACF_SetFriction <float>` | `id_ACF_SetFriction` yes |
| `SetUserControl: “Disabled”`, `PlaySoundType: “Yelp”`, `DecrementDamageBar: 20` | `DO_ACTION …` | not by those names (`id_ACF_PlaySound`, `id_ACF_ModifyDamage` / `id_MSG_ModifyDamage` exist) |
| `SendMessage: “TeamManager” “ImDown” “MyDamageLevel”` | `DO_ACTION 4 id_ACF_SendMessage <id_PRN_…> id_MSG_… <data>` | `id_ACF_SendMessage` yes; recipient is a pronoun |
| `ChangeToBehavior: “GetUp”` | `CHANGE_TO_BEHAVIOR 1 "GetUp"` | — |

What the oracle shows when Listing 1 is fed to the shipped compiler unchanged
(fixtures `errors/article_*`):
- **Verbatim** (cp1252 curly quotes) and **with ASCII quotes**, with or without an added
  `END_PLAN`, compilation fails at once. The error is `Expected an argument enumerator.` at line 1,
  thrown by `PrescanDecls` because the token after `DECLARE_BEHAVIOR` is `Named:`, which is
  neither a number nor an identifier.
- The compiler has no table of labels. `Named:`, `BehaviorType:`, `Target:`, `Message:`,
  `SET_INTERACTION`, `RESPONSE`, `SetAnimation:` and the rest occur nowhere in the exe. Where the
  prescan does not look, the emitter **silently drops** any such word. So
  `DO_ACTION Action: 2 id_ACF_PlaySound Sound: id_SND_Sxlaunch` compiles to exactly the same bytes
  as `DO_ACTION 2 id_ACF_PlaySound id_SND_Sxlaunch` [article_named_label_elsewhere]. The dropped
  words still advance the positional counters, though. So a label between `CHANGE_TO_BEHAVIOR 1`
  and its name changes the output [beh_ref_label_shift].
- The article's `“…”` strings would not be strings. Curly quotes are ordinary bytes, so `“YerHit”`
  is an unknown word and is dropped [str_curly_quotes]. The typography is the magazine's, not the
  source's.
- The article form has **no child counts**. The exe form cannot work without them, because the
  compiler is a flat token translator and the counts are the only structure (binary-evidence §6).

### Interpretation (the relationship between the two forms)

Status: **unresolved**. The evidence ranks the possibilities as follows.

1. **Presentation pseudocode / earlier dialect (most likely).** The listing is a simplified,
   readable rendering: no counts, labelled arguments, symbolic strings in place of ENUMIDS
   identifiers, and action names without the `id_ACF_` prefix. Several names do not exist in the
   shipped enums (`GlideToTarget`, `SetUserControl`, `PlaySoundType`, `DecrementDamageBar`,
   `TeamManager`). So either the listing predates the October 1996 vocabulary (the Aug/Sep issue
   went to press about two to three months before the exe was built on 1996-10-21), or it was written for readers. The keyword
   families match one for one: `SET_INTERACTION` ↔ `SET_*_INTERACTOR`, `RESPONSE` ↔ `Enable`,
   `ChangeToBehavior` ↔ `CHANGE_TO_BEHAVIOR`. So it describes the same language.
2. **A separate front end that produced the prefix text (possible, no evidence).** The article
   says that "smart editing … syntax- and template-driven" was only a *goal* in 1996. The exe has
   no trace of a labelled-syntax reader. Several features of the exe dialect would suit
   machine-generated or preprocessed text: the comment characters `#` and `(`, `&` and `,` as
   whitespace, ignored unknown words (labels as decoration), and the `_N_` "argument enumerator"
   constants for counts. None of these features requires a generator, though, and the error
   texts ("Expected an argument enumerator.", "First two args are id and string name.") address a
   human typing counts.
3. **One compiler accepting both forms**: **ruled out** for this exe (the oracle rejects the
   listing in the prescan).

The article does establish that the explicit-count prefix form was the shipped *compiler source*,
and that labels such as `Named:` are at most decorative in it. Whether the developers ever wrote
labels in their real sources cannot be told from the PBNs, because the compiler discards them.
