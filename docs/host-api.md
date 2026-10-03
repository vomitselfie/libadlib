# Embedding the ADLIB runtime (host API)

libadlib is the ADLIB runtime: the interpreter that runs compiled plans (`.PBN`). It is a
host-agnostic C++17 static library (`adlib`, namespace `adlib`) with no dependencies. The game
("host") provides three things:

```
 vocabulary (data)             host (C++)                       plans (data)
 enum header(s) ─────────┐     PlanHost (object index,          *.PBN (compiled by adlibc,
 list files (texts, ...) ├──>  messaging hooks, net)            or built with adlib::build)
 programmatic adds ──────┘     Registry: primitives BY NAME            │
            │                          │                               │
            └────────────> adlib runtime (interpreter, behaviours, <───┘
                           interactors, agenda, decision paths)
```

This is the split Rosen & Duisberg describe for the 1996 system: the plan vocabulary (actions,
decisions, messages) is extended in compiled C++, and the plans are data.

The library compiles with `-ffp-contract=off -fno-fast-math` (plus SSE on x86) for lockstep
determinism; hosts that need bit-identical simulation across builds should use the same flags.
It also builds under Emscripten.

## Embedding, step by step

The vending machine ([examples/vending_machine/main.cpp](../examples/vending_machine/main.cpp))
is a complete host in about 150 lines. The steps:

**1. Load the vocabulary.** The same header the plans were compiled against:

```cpp
#include "adlib/adlib.h"
auto vocab = std::make_shared<adlib::Vocabulary>();
std::string err;
vocab->loadEnumHeaderFile("VENDING.H", &err);
vocab->loadList("Texts", readText("VENDTEXT.LST"));      // optional list namespaces
```

**2. Register primitives by name.** Ids come from the vocabulary, so the C++ never hard-codes
numbers:

```cpp
adlib::Registry reg(vocab);
reg.action("AddCredit", [&](adlib::ActionCtx &c) { machine.credit += 1; });
reg.action("Display", [&](adlib::ActionCtx &c) { show(int(c.arg(1))); });   // arg(0) = the ACF id
reg.decision("EnoughCredit", [&](adlib::ActionCtx &c) -> uint16_t {
    return machine.credit >= machine.price ? 1 : 0;                          // the branch to run
});
reg.behavior("Machine", [] { return std::make_unique<adlib::Behavior>(); });
reg.agenda("CGlideOnSurface", [] { return std::make_unique<MyActivity>(); }); // long-running items
reg.loadPronoun = [](adlib::PlanObject &o, uint16_t prn, uint32_t tok) { ... };  // "Me"
reg.runtimePronoun("Customer", [&](adlib::PlanObject &) { return int32_t(customer.idx); });
assert(reg.unresolved().empty());   // every name exists in the vocabulary
```

**3. Implement `PlanHost`** for your game objects (only `objectIndex()` is required):

```cpp
struct Thing : adlib::PlanHost {
    int16_t idx;
    int16_t objectIndex() const override { return idx; }
};
```

**4. Create the scene and objects, load plans, start them:**

```cpp
adlib::PlanScene scene;
adlib::PlanObject mObj(scene, machine, reg), cObj(scene, customer, reg);
scene.add(&mObj);
scene.add(&cObj);
mObj.loadPlansFromDir(pbnDir, {"VENDING"});    // or loadPlans(loader, names), loadPlanFromWords
mObj.start(/*plan*/ 0, /*behaviour*/ 0);
```

**5. Tick.** Once per simulation frame:

```cpp
scene.clock += 100;     // ms; timers (SET_TIMEOUTMSG) run on this clock
scene.update();         // = tick() (agenda + plan tick per object), processMessages(), endFrame()
```

**6. Send messages** from game code (input, physics, other systems). Messages are queued and
delivered in `processMessages()`:

```cpp
mObj.sendMessage(vocab->id(adlib::Role::Message, "Coin"), /*data*/ 0, /*sender*/ customer.idx);
scene.broadcast(msgId, data, sender);              // to every object
mObj.fireCollision(interactor, data);              // from your collision system
```

From an action, `c.obj.scene().object(i)->sendMessage(...)` sends to another object (this is how
the vending machine's `SendMessage` action works).

## Decision paths and lockstep networking

ADLIB networks by sending **events, not state**: every message or collision carries a 16-bit
**decision path**. When an event is handled locally (`kPathRecord`), each `DECIDE_BY_*` runs its
C++ decision and records the branch into the path. When the same event is replayed on another
machine, the recorded branches are used instead of calling the decisions again, so both machines
run the same actions even if their local state differs slightly.

- `pathRecord(path, idx)` / `pathReplay(path)` are the codec; `pathRecording(p)` tells the modes
  apart. The path holds up to 3 nested decisions (12 bits; the third level reproduces the
  original's bit-packing quirk).
- `PlanHost::netRole()` returns `RoleLocal` (1, the object is simulated here and is
  authoritative), `RoleLocal2` (2) or `RoleRemote` (3, a copy driven by the network).
- On a `RoleLocal` object, every message handled and every collision fired is also handed to
  `netSend(packed, data)` when `netConnected()`; the packed word comes from
  `packNetMessage(msg, sender, path)` / `packNetCollision(...)` and includes the recorded path.
- A `RoleRemote` object ignores locally generated messages and collisions. Hand the packed words
  that arrive from the network to `PlanObject::netEnqueue(code, data)`; `PlanScene::processMessages()`
  replays them with the recorded decision path.
- `DO_LOCAL_ACTION` runs only where the object is not remote, `DO_NET_ACTION` only where it is
  not `RoleLocal`; plain `DO_ACTION` runs everywhere the event runs.
- `ActionCtx::path` gives an action the current path; actions that themselves send events
  should pass it on.
- `Registry::traceEvent` reports decisions with a "replayed" flag
  ([tools.md](tools.md#trace-the-run-time-trace-sink)).

Determinism also needs the same plans, the same vocabulary, the same order of `update()` calls
and messages on every machine, and the floating-point flags above.

## Reference: files

| File | Contents |
|---|---|
| `include/adlib/vocabulary.h`, `src/vocabulary.cpp` | `Role`, `Namespace`, `Vocabulary`: the enum-header parser and the list loader |
| `include/adlib/pbn.h`, `src/pbn.cpp` | Token constants, `Node`/`Arg`/`PlanTree`, the PBN reader (`readPbnWords`, `buildPlanTree`) and `dumpNode` |
| `src/pbn_writer.cpp` | PBN writer (`encodePlan`, `writePbnFile`), `dumpTokens`, symbolic `dumpPlan`, and the `adlib::build` plan builder |
| `include/adlib/runtime.h`, `src/runtime.cpp` | `RuntimeIds`, `PlanHost`, `Registry`, `ActionCtx`, `Interactor`, `AgendaItem`/`TimerActivity`/`Agenda`, `Behavior`, `PlanInstance`, `PlanObject`, `PlanScene`, and the decision-path codec |
| `include/adlib/adlib.h` | Umbrella header |
| `include/adlib/compiler.h`, `src/compiler/` | adlibc as a library (`compilePlan`, `CompilerSymbols`) |
| `include/adlib/tools.h`, `src/tools/` | adlib-tools (separate library `adlib_tools`) |
| `include/adlib/version.h` (generated) | `ADLIB_VERSION_STRING` |
| `examples/vending_machine/` | An example host (see below) |

## Vocabulary

A `Vocabulary` is a set of named `Namespace`s. Each namespace maps name <-> id, and a name can
have gaps. Sources:

- `loadEnumHeader(text)` / `loadEnumHeaderFile(path)` parse the same "nothing but enum statements
  and comments" format the original compiler read. `//` and `/* */` comments and `#` lines are
  skipped, and tokens outside braces are ignored, except the identifier before `{`, which names
  the namespace. A value is the symbol's 0-based position. **Extension:** `name = value` is
  accepted, and the names after it continue from value+1. The original has no such syntax, and
  1996 headers use none. Duplicate names and unterminated enums are errors. Loading an enum name a
  second time appends to it.
- `loadList(ns, text, ListOptions{skipLines, comment, requirePrefix})` reads one symbol per line
  (the first whitespace-separated token), with the id in order of appearance. This is for
  `AnimAssm.txt`, `Sndfiles.lst` and `Infobtxt.lst`-style files. Use `requirePrefix = "AA_"` for
  assemblies and `skipLines = 1` for SNDFILES.LST's header.
- `ns(name).add(sym, id)` / `.append(sym)` add symbols from code.

**Roles** are the namespaces the interpreter consumes. By default each role is bound to the
original enum name, so a new game that reuses those enum names needs no binding code. Rebind a
role with `vocab.bind(Role::Action, "MyActions")`.

| Role | Default enum | Used by |
|---|---|---|
| Message | `ID_MSG` | message interactors, SET_TIMEOUTMSG, handler-table size |
| CollisionObject | `CollisionObjectID` | collision interactors (names only) |
| Behavior | `Behavior_IDs` | DECLARE_BEHAVIOR type -> behaviour factory |
| Agenda | `AgendaItemIDs` | ADD/REMOVE_AGENDA_ITEM, SET_PurgeFlag range |
| Action | `ActionFunctionIDs` | DO_ACTION |
| Decision | `DecisionFunctionIDs` | DECIDE_BY_* |
| BehaviorParam | `BehaviorSetParamFnIDs` | SET_BEHAVIOR_PARAM |
| InteractorParam | `InteractorSetParamFnIDs` | SET_INTERACTOR_PARAM |
| Pronoun | `Pronouns` | 0xFEDCxxxx leaves |
| Object | `ID_MOD` | timer sender (optional) |

Lookups accept the full name (`id_ACF_SendMessage`) or the short name (`SendMessage`), which is
the full name without the `id_XXX_` prefix. Other namespaces (animations, sounds, texts, ...) are
open-ended. A host reads them with `vocab.find("Texts")->id(...)`. The compiler can take its
symbols from a Vocabulary (`CompilerSymbols::fromVocabulary`) or, like the 1996 compiler, from the
header and list files themselves (`CompilerSymbols::fromFiles`); see [adlibc.md](adlibc.md).

### Runtime ids from the vocabulary

The interpreter uses a few vocabulary ids itself. `RuntimeIds::resolve` looks each one up by name.
If the role is unbound, or the bound namespace lacks the name, the feature is disabled (-1)
and, for a bound namespace, the name is reported as missing. The timer item is the exception: it
gets the id after the last agenda id. `Registry::mutableIds()` sets a value directly (for example
an object index for `timerSender` when the host has no object enum).

| Field | Name looked up | Unbound |
|---|---|---|
| `msgExitBehavior` | Message `ExitBehavior` (sent to self before a behaviour change) | -1 |
| `msgInitiate` | Message `Initiate` (queued to self on behaviour entry) | -1 |
| `msgNone` | Message `NOMSG` / `NoMsg` (timer default, "no event message") | -1 |
| `messageCount` | size of the Message namespace (handler table) | 0 |
| `agdTimerActivity` | Agenda `CTimerActivity` / `TimerActivity` | 0 |
| `agendaCount` | size of the Agenda namespace (PurgeFlag range, max 256) | 1 |
| `timerSender` | Object `NOMODEL` | -1 |
| `bset*` / `iset*` | BehaviorParam `KeyMonitorMode`, `InteractorLatency`, `PersistentAnimation`, `Animation`, `AnimationEvent`, `Digressive`, `PurgeFlag`; InteractorParam `InteractorName`, `ActiveState` | -1 |

A new game declares `Initiate`, `ExitBehavior` and `NOMSG` in its message enum if it wants those
runtime messages. It declares the framework parameters it uses under their names, at any position.

## Registry: primitives by name

```cpp
auto vocab = std::make_shared<adlib::Vocabulary>();
vocab->loadEnumHeaderFile("MYGAME.H");
adlib::Registry reg(vocab);                      // also installs the framework entries
reg.action("SendMessage", [](adlib::ActionCtx &c) { ... });
reg.decision("EnoughCredit", [](adlib::ActionCtx &c) -> uint16_t { return branch; });
reg.behavior("Machine", [] { return std::make_unique<MyBehavior>(); });
reg.agenda("CGlideOnSurface", [] { return std::make_unique<MyItem>(); });
reg.runtimePronoun("ViewOwner", [](adlib::PlanObject &o) { return int32_t(...); });
reg.behaviorParam("PersistentAnimation", ...);    // overrides the framework version
reg.loadPronoun = [](adlib::PlanObject &o, uint16_t prn, uint32_t tok) { ... };
```

- The id comes from the vocabulary, so shipped PBNs keep their exact numbers, and a new game's
  ids come from its own header. An unknown name is not registered. It is logged and appended to
  `reg.unresolved()`, which tests should check is empty.
- The raw-id forms (`setAction(id, fn)`, ...) remain for computed ids and loops.
- `reg.setVocabulary(v)` re-resolves `ids()` and re-installs the framework entries. Call it before
  registering game primitives and before creating `PlanObject`s, because the handler table is
  sized at construction. `mutableIds()` lets a host override a resolved value.
- Framework entries (`registerFrameworkDefaults`) cover the params that only touch CBehavior /
  CInteractor fields, plus the `TimerActivity` factory. A host replaces any of them by
  registering the same name.
- `symbol(role, id)` returns names for logs and traces ("?" when unknown). `log` defaults to
  stderr with an `[adlib]` prefix. `trace` sees every executed statement.
- `hasX(id)` and the table accessors (`actions()`, ...) are for tooling.

## Reference: `PlanHost`

| Hook | Use |
|---|---|
| `objectIndex()` | Object index in the scene: message recipient, `Me` pronouns |
| `netRole()` / `netConnected()` / `netSend(packed, data)` | Lockstep authority model: Local=1, Local2=2, Remote=3. Outgoing events are packed with `packNetMessage` / `packNetCollision` and carry the decision path. The receiving side calls `PlanObject::netEnqueue` |
| `playBehaviorAnimation(anim, f40, i48)` / `playBehaviorSound(a58, msg, a5c)` | Behaviour entry (`id_SET_Animation` / `id_SET_AnimationEvent` values) |

Pronouns: the load-time ones are resolved by `Registry::loadPronoun`, the run-time ones by
`Registry::runtimePronoun`. Plan loading takes a `PlanLoader` callback (`name -> bytes`), or uses
`loadPlansFromDir(dir, names, ".PBN")`. `PlanScene::onBroadcastHook` remains for scene-level side
effects of broadcasts.

Interpreter semantics (behaviours, interactors, agenda, message queue, decision-path
record/replay, the original's quirks) are those of the 1996 runtime, as reconstructed from the
original executable; the comments in `include/adlib/runtime.h` give the original function for each
member.

## PBN writer, dumpers and builder

Plans are normally written as ADLIB source and compiled with `adlibc`
([adlibc.md](adlibc.md), language: [language.md](language.md)). The writer and the
builder below are the in-memory route (tools, tests, generated plans).

- `encodePlan(top | tree)` produces the token stream, `pbnFileBytes` / `writePbnFile` add the
  `u32 count` header, and `readPbnFile` reads one back. A leaf that kept its `str` (a
  CHANGE_OF_PLAN target resolved at load) is written back as the string. As a result,
  `buildPlanTree(words, LoadHooks{})` -> `encodePlan` reproduces a compiled plan byte for byte
  (verified on HyperBlade's 32 shipped plans).
- `dumpTokens(words)` gives one raw token per line with offsets, for diffing compiler output.
  `dumpPlan(tree, &vocab)` prints a symbolic tree. It names function, message, COB, behaviour,
  agenda, param and pronoun ids by statement shape, and annotates CHANGE_TO_BEHAVIOR targets with
  the behaviour name.
- `adlib::build` has helpers for each statement: `behavior`, `globals`, `onMessage`, `onCollision`,
  `action` (variant 2/3 = LOCAL/NET), `decide`, `decideWith`, `block`, `changeTo`, `changePlan`,
  `behaviorParam`, `interactorParam`, `addAgenda`, `removeAgenda`, `timeout`, `localReals`,
  `endPlan`. Leaf helpers are `lit`, `real`, `str` and `pronoun`. The `list(...)` / `args(...)`
  helpers build the move-only vectors.

## Extending a shipped vocabulary

Never renumber or reuse shipped ids. Original PBNs must keep working unmodified.

- New primitives go **after** the existing ids of their role, with a distinctive prefix:
  `vocab.extend(Role::Action, "id_ACF_X_Foo")`. Alternatively, put them in a second header
  (`enum EXT_ActionFunctionIDs { id_ACF_EXT_Foo, ... };`) and call
  `vocab.mergeInto(Role::Action, "EXT_ActionFunctionIDs")`. Then register them by name as usual.
  Use one prefix per extension source (the game's own port, each mod), so names never clash.
- Shipped enums end in a sentinel (`id_ACF_NUMACTIONFUNCTIONS`, `id_MSG_NOMSG`). Appended ids come
  after it, so the sentinel keeps its value. New messages also grow the handler table
  (`messageCount`). Build the vocabulary before creating the Registry, or call `setVocabulary`.
- A modded plan that uses extension ids needs the same extended vocabulary at compile and run
  time. Ship the extension header next to the mod.

## Example host: the vending machine

`examples/vending_machine/` contains the 1996 article's explanatory example:

- `VENDING.H` holds its own enums. Messages are Coin/Select/Refund/VendDone/Dispensed plus the
  runtime-reserved names. It defines 6 actions, 2 decisions, 2 behaviour types, the framework
  params at positions of its own, the pronouns `Me` (load-time) and `Customer`
  (run-time), and `ID_MOD`.
- `VENDTEXT.LST` is a list file loaded into a `Texts` namespace.
- `VENDING.txt` and `CUSTOMER.txt` are the plans, written in ADLIB source ("Idle" /
  "HasCredit" / "Vending", with nested decisions and a SET_TIMEOUTMSG timer; plus a customer
  plan). The build compiles them with `adlibc --enumids VENDING.H --texts VENDTEXT.LST --strict`
  (target `vending_plans`, output in `<build>/vending_plans/`); this is libadlib's hello world.
- `main.cpp` registers its primitives by name, loads the compiled `VENDING.PBN` / `CUSTOMER.PBN`
  with `loadPlansFromDir` like a shipped game, prints `dumpPlan`, and runs a scripted customer:

```
t= 500ms customer: Coin
  machine display: Credit: 1
t= 500ms machine: Idle -> HasCredit
t= 700ms customer: Select
  machine display: Not enough credit, insert another coin.
...
t=1100ms customer: Select
  *clunk* a snack drops (stock now 0)
t=1100ms machine: HasCredit -> Vending
t=1300ms customer: Coin
  machine display: Busy, please wait.
  machine display: Thank you, enjoy!
  customer: Yum!
...
t=3400ms customer: Select
  machine display: Sorry, sold out.
  machine returns 3 coin(s)
```

Run it with `./vending_machine [data dir] [dir of the compiled .PBN files] [-v] [--trace FILTER]`.
With `-v` it traces every executed statement; `--trace` uses the structured trace sink
([tools.md](tools.md#trace-the-run-time-trace-sink)).
