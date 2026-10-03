// ADLIB runtime (libadlib): plan instances, behaviours (state machine), interactors (event
// handlers), the statement interpreter, message dispatch, the agenda framework and decision-path
// recording. Game-agnostic: everything game-specific (actions, decisions, behaviour subclasses,
// agenda-item classes, pronouns, the id vocabulary) is supplied by the host through Registry,
// Vocabulary and PlanHost. See docs/host-api.md.
//
// Ported from HyperBlade's HYPERX.EXE (Rosen & Duisberg's ADLIB, 1996); addresses in comments.
// Semantics: docs/host-api.md.
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "adlib/pbn.h"
#include "adlib/vocabulary.h"

namespace adlib {

class PlanObject;
class PlanScene;
class PlanInstance;
class Behavior;
class Interactor;
class AgendaItem;
class Registry;

// Agenda ids index Behavior::purge; vocabularies with more agenda item types are clamped.
constexpr int kMaxAgendaIds = 256;

// ---------------------------------------------------------------------------------------------
// Vocabulary-dependent ids the interpreter itself uses. Resolved by name from the Registry's
// vocabulary (RuntimeIds::resolve). A feature whose role is unbound, or whose name the bound
// namespace lacks, is disabled (-1); the timer item is the exception (see below). Hosts normally
// bind every role through their vocabulary header; mutableIds() can set a value directly.
struct RuntimeIds {
    // Role::Message
    int msgExitBehavior = -1;   // "ExitBehavior": sent to self (synchronously) before a behaviour change
    int msgInitiate = -1;       // "Initiate": queued to self by Behavior::enter()
    int msgNone = -1;           // "NOMSG": no message (timer default, behaviour "no event message")
    int messageCount = 0;       // size of the per-object message handler table (namespace size)
    // Role::Agenda
    int agdTimerActivity = 0;   // "CTimerActivity"/"TimerActivity": SET_TIMEOUTMSG's item. If the
                                // namespace (or the role) lacks it, it gets the id after the last one.
    int agendaCount = 1;        // agenda ids < agendaCount honour SET_PurgeFlag
    // Role::Object
    int32_t timerSender = -1;   // "NOMODEL": sender of timer messages
    // Role::BehaviorParam (framework implementations, registerFrameworkDefaults)
    int bsetKeyMonitorMode = -1, bsetInteractorLatency = -1, bsetPersistentAnimation = -1, bsetAnimation = -1,
        bsetAnimationEvent = -1, bsetDigressive = -1, bsetPurgeFlag = -1;
    // Role::InteractorParam
    int isetInteractorName = -1, isetActiveState = -1;

    // Resolve from `v` (names listed above). Names a bound namespace lacks go to `missing`.
    static RuntimeIds resolve(const Vocabulary *v, std::vector<std::string> *missing = nullptr);
};

// Network roles returned by CGameObject vf18 (obj+0x214).
enum NetRole : int { RoleLocal = 1, RoleLocal2 = 2, RoleRemote = 3 };

// ---------------------------------------------------------------------------------------------
// The owning game object, as far as the Plans runtime needs it (CGameObject vtable 0x4fca60).
// Implemented by the game layer; everything has a harmless default so tests can use a dummy.
struct PlanHost {
    virtual ~PlanHost() = default;
    virtual int16_t objectIndex() const = 0;                 // vf01/vf04: index in the scene
    virtual int netRole() const { return RoleLocal; }       // vf18 (obj+0x214)
    virtual bool netConnected() const { return false; }     // vf15 (aux+0x134 != 0)
    virtual void netSend(uint32_t packed, uint32_t data) {} // vf19: outgoing net ring (obj+0x3a0)
    // Behavior::enter(): CGameObject vf10 (+0x28) play assembly / vf11 (+0x2c) play sound
    // (the behaviour's id_SET_Animation / id_SET_AnimationEvent values).
    virtual void playBehaviorAnimation(uint32_t anim, uint32_t f40, int32_t i48) {}
    virtual void playBehaviorSound(uint32_t a58, uint32_t sound, uint32_t a5c) {}
};

// ---------------------------------------------------------------------------------------------
// Decision path (FUN_004cd7c0): a u16 carried with every event so the receiving peer replays
// the sender's DECIDE_BY_* outcomes. bit15 = record (decide locally), bits12-13 = depth.
//   depth1: d1 = bits0-11               depth2: d1 = bits0-6, d2 = bits7-11
//   depth3: d1 = bits0-4, d2 = bits5-8, d3 = bits9-11  (only 3 decisions fit)
constexpr uint16_t kPathRecord = 0xFFFF; // what local senders pass ("decide now")
inline bool pathRecording(uint16_t p) { return (p & 0x8000) != 0; }
// Record branch `idx` (after the DCF ran). Reproduces the original's 3rd-level bug: the third
// decision is stored as (idx & 0xE00), i.e. normally 0.
uint16_t pathRecord(uint16_t path, uint16_t idx);
// Replay: returns the next recorded branch and advances `path`.
uint16_t pathReplay(uint16_t &path);

// Net packing (FUN_004cd670/6a0 messages, FUN_004cd630/700 collisions).
inline uint32_t packNetMessage(int16_t msg, uint16_t sender, uint16_t path) {
    return uint32_t(int32_t(msg) << 22) | uint32_t(sender & 0xFF) << 14 | (path & 0x3FFF);
}
inline uint32_t packNetCollision(int16_t index, int16_t cobMine, int16_t cobOther, uint16_t path) {
    return 0x80000000u | uint32_t(index & 0x7F) << 24 | uint32_t(cobMine & 0xFF) << 16 |
           uint32_t(cobOther & 0xFF) << 8 | (path & 0xFF);
}

// ---------------------------------------------------------------------------------------------
// Context handed to ACF/DCF/param functions.
struct ActionCtx {
    PlanObject &obj;
    Interactor *it;       // executing interactor; nullptr for DO_ACTION inside DECLARE_BEHAVIOR
    Behavior *beh;        // current behaviour of the active plan (or the one being set up)
    const Node &node;     // DO_ACTION / DECIDE_BY_* node; children[0] = function id
    uint16_t *path;       // decision path (may be nullptr at setup time)
    uint32_t msgData;     // message data word (_MESSAGE_DATA_)

    // children[i] raw, with _MESSAGE_DATA_ substituted. DECIDE_BY_WITH_AMONG: use dataArgs().
    uint32_t arg(size_t i) const;
    // children[i] with runtime pronoun resolution (0x522BB0 table via Registry).
    int32_t argResolved(size_t i) const;
    // FUN_004a6240: int literal -> float, or local real (666.666 when out of range).
    float argFloat(size_t i) const;
    // For DECIDE_BY_WITH_AMONG: the DataBlock node (children[1]) or nullptr.
    const Node *dataBlock() const;
};

using ActionFn = std::function<void(ActionCtx &)>;
using DecisionFn = std::function<uint16_t(ActionCtx &)>; // returns branch index
using BehaviorParamFn = std::function<void(PlanObject &, const Node &, Behavior *)>;
using InteractorParamFn = std::function<void(PlanObject &, const Node &, Behavior *, Interactor *)>;
using AgendaFactory = std::function<std::unique_ptr<AgendaItem>()>;
using BehaviorFactory = std::function<std::unique_ptr<Behavior>()>;
using RuntimePronounFn = std::function<int32_t(PlanObject &)>;
using TraceFn = std::function<void(PlanObject &, const std::string &)>;
// Structured trace event (Registry::traceEvent), for tools (adlib::tools::Tracer). Emitted at the
// same points as the string trace; costs nothing when no hook is installed.
struct TraceEvent {
    enum class Kind : uint8_t {
        Message,         // id = message, a = data, b = sender
        Collision,       // a = cob mine, b = cob other
        Action,          // id = ACF (node = DO_ACTION node)
        Decision,        // id = DCF, a = branch, b = 1 when replayed from a decision path
        BehaviorParam,   // id = set id
        InteractorParam, // id = set id
        Change,          // a = from behaviour index (-1 none), b = to index; CHANGE_TO_BEHAVIOR applied
        ChangeRequest,   // a = requested target (raw, kPreviousBeh / kNoBehChange possible)
        PlanChange,      // a = plan index, b = behaviour index (CHANGE_OF_PLAN request)
        PlanActivated,   // id = plan index
        AgendaAdd,       // id = agenda id
        AgendaRemove,    // id = agenda id
        TimerSet,        // id = message, a = delay, b = recipient
        TimerFire,       // id = message, b = recipient
    };
    Kind kind;
    int id = -1;
    int64_t a = 0, b = 0;
    const Node *node = nullptr;
    std::string text; // Change: "from -> to" names
};
using TraceEventFn = std::function<void(PlanObject &, const TraceEvent &)>;
// Load-time pronoun resolution (0x522AC8 in the exe): the dword stored in place of a
// 0xFEDCxxxx leaf when `owner` loads a plan. Return `token` to keep it for run-time resolution.
using LoadPronounFn = std::function<uint32_t(PlanObject &owner, uint16_t prn, uint32_t token)>;
// Plan bytes by name (PlanObject::loadPlans). Return false when the plan does not exist.
using PlanLoader = std::function<bool(const std::string &name, std::vector<uint8_t> &bytes)>;

// Tables 0x527420 (ACF), 0x527660 (DCF), 0x5269D8 (behaviour params), 0x527650 (interactor
// params), 0x526430 (agenda factories), 0x527380 (behaviour prototypes), 0x522BB0 (runtime
// pronouns). Missing entries resolve to "unimplemented: log once" stubs.
//
// Primitives are registered by name against the vocabulary (`reg.action("SendMessage", fn)`,
// short or full enum names), or by raw id (`setAction(5, fn)`).
class Registry {
public:
    // Installs the framework entries (registerFrameworkDefaults) at the ids `vocab` gives them.
    explicit Registry(std::shared_ptr<const Vocabulary> vocab = nullptr);

    // Replaces the vocabulary, re-resolves ids() and re-installs the framework entries (call it
    // before registering game primitives and before creating PlanObjects).
    void setVocabulary(std::shared_ptr<const Vocabulary> vocab);
    const Vocabulary *vocabulary() const { return vocab_.get(); }
    const RuntimeIds &ids() const { return ids_; }
    RuntimeIds &mutableIds() { return ids_; } // host overrides (e.g. timerSender)
    // Name lookups ("?" for unknown, like the exe's debug tables).
    const char *symbol(Role r, int id) const;
    int idOf(Role r, std::string_view name) const; // -1 if unknown

    // By-name registration. Returns the id, or -1 (logged, counted in unresolved()) when the
    // vocabulary has no such name.
    int action(std::string_view name, ActionFn f);
    int decision(std::string_view name, DecisionFn f);
    int behaviorParam(std::string_view name, BehaviorParamFn f);
    int interactorParam(std::string_view name, InteractorParamFn f);
    int agenda(std::string_view name, AgendaFactory f);
    int behavior(std::string_view name, BehaviorFactory f);
    int runtimePronoun(std::string_view name, RuntimePronounFn f);
    const std::vector<std::string> &unresolved() const { return unresolved_; }

    void setAction(int id, ActionFn f);
    void setDecision(int id, DecisionFn f);
    void setBehaviorParam(int id, BehaviorParamFn f);
    void setInteractorParam(int id, InteractorParamFn f);
    void setAgenda(int id, AgendaFactory f);
    void setBehavior(int id, BehaviorFactory f);
    void setRuntimePronoun(int id, RuntimePronounFn f);

    void callAction(int id, ActionCtx &c);
    uint16_t callDecision(int id, ActionCtx &c);
    void callBehaviorParam(int id, PlanObject &o, const Node &n, Behavior *b);
    void callInteractorParam(int id, PlanObject &o, const Node &n, Behavior *b, Interactor *it);
    std::unique_ptr<AgendaItem> createAgenda(int id);
    std::unique_ptr<Behavior> createBehavior(int id);
    int32_t resolveRuntimePronoun(int id, PlanObject &o);

    bool hasAction(int id) const { return has(acf_, id); }
    bool hasDecision(int id) const { return has(dcf_, id); }
    bool hasBehaviorParam(int id) const { return has(bset_, id); }
    bool hasInteractorParam(int id) const { return has(iset_, id); }
    bool hasAgenda(int id) const { return has(agd_, id); }
    bool hasBehavior(int id) const { return has(beh_, id); }
    bool hasRuntimePronoun(int id) const { return has(prn_, id); }
    // Registered entries, for tests / tooling (e.g. verifying a host's registration table).
    const std::vector<ActionFn> &actions() const { return acf_; }
    const std::vector<DecisionFn> &decisions() const { return dcf_; }
    const std::vector<BehaviorParamFn> &behaviorParams() const { return bset_; }
    const std::vector<InteractorParamFn> &interactorParams() const { return iset_; }
    const std::vector<AgendaFactory> &agendaFactories() const { return agd_; }
    const std::vector<BehaviorFactory> &behaviorFactories() const { return beh_; }
    const std::vector<RuntimePronounFn> &runtimePronouns() const { return prn_; }

    LoadPronounFn loadPronoun;                    // default: keep every token (all run-time)

    std::function<void(const std::string &)> log; // default: stderr
    TraceFn trace;                                // optional: every executed statement
    TraceEventFn traceEvent;                      // optional: structured events (tools/Tracer)
    void emit(PlanObject &o, TraceEvent::Kind k, int id = -1, int64_t a = 0, int64_t b = 0,
              const Node *n = nullptr) {
        if (traceEvent) traceEvent(o, TraceEvent{k, id, a, b, n, {}});
    }
    void warnOnce(const std::string &key, const std::string &msg);

private:
    template <class T> static void put(std::vector<T> &v, int id, T f);
    template <class T> static bool has(const std::vector<T> &v, int id) {
        return id >= 0 && size_t(id) < v.size() && bool(v[size_t(id)]);
    }
    int lookup(Role r, std::string_view name);
    std::shared_ptr<const Vocabulary> vocab_;
    RuntimeIds ids_;
    std::vector<std::string> unresolved_;
    std::vector<ActionFn> acf_;
    std::vector<DecisionFn> dcf_;
    std::vector<BehaviorParamFn> bset_;
    std::vector<InteractorParamFn> iset_;
    std::vector<AgendaFactory> agd_;
    std::vector<BehaviorFactory> beh_;
    std::vector<RuntimePronounFn> prn_;
    std::set<std::string> warned_;
};

// The SET_BEHAVIOR_PARAM / SET_INTERACTOR_PARAM functions that only touch CBehavior /
// CInteractor fields (KeyMonitorMode, InteractorLatency, PersistentAnimation, Animation,
// AnimationEvent, Digressive, PurgeFlag; InteractorName, ActiveState) and the TimerActivity
// factory, at r.ids(). Hosts may re-register any of them (e.g. to substitute animations).
void registerFrameworkDefaults(Registry &r);

// ---------------------------------------------------------------------------------------------
// Interactor (pooled CInteractor, 0x34 bytes; ctors 0x4cdea0 collision / 0x4ce240 message).
class Interactor {
public:
    enum class Kind : uint8_t { Message, Collision };
    Kind kind = Kind::Message;
    PlanObject *obj = nullptr;  // +0x04
    Behavior *beh = nullptr;    // owning behaviour (nullptr = GLOBAL_INTERACTORS)
    const Node *enable = nullptr; // +0x0C Enable(action*) node, may be null
    int32_t active = 0;         // +0x14 0 = latent, 1 = armed, -1 = disabled (id_SET_ActiveState)
    uint32_t nextBeh = kNoBehChange; // +0x18 (persistently overwritten by CHANGE_TO_BEHAVIOR)
    uint16_t variant = 0;       // +0x1C 0 normal, 1 LOC (local only), 2 NET
    int16_t planChange = -1;    // +0x20 CHANGE_OF_PLAN target plan (reset per dispatch)
    int16_t planChangeBeh = 0;  // +0x22
    bool allowBehChange = true; // +0x24 (always 1 in the exe)
    uint32_t a28 = 0;           // +0x28 msg: sender index ; coll: COB mine (may be pronoun)
    uint32_t a2c = 0;           // +0x2C msg: message id  ; coll: COB other
    int16_t collIndex = -1;     // +0x32 collision interactor number (net replay key)

    int msgId() const { return int(a2c); }
    int32_t sender() const { return int32_t(a28); }
    int16_t cobMine() const;  // vf07: resolves runtime pronouns
    int16_t cobOther() const; // vf08
    void setActive(int32_t v) { active = v; } // vf0a
};

// ---------------------------------------------------------------------------------------------
// Agenda items (CAgendaItem, vtable 0x4fab98). Ticked every frame, sorted by id.
class AgendaItem {
public:
    virtual ~AgendaItem() = default;
    virtual void update(PlanObject &o) {}       // vf01 (role 1/2): per-frame work
    virtual void updateRemote(PlanObject &o) {} // vf02 (role 3 / net-driven objects)
    virtual void init(PlanObject &o, Behavior *b, const Node &n, uint32_t msgData) { owner = &o; } // vf03
    virtual void initDefault(PlanObject &o, Behavior *b) { owner = &o; }                          // vf04
    virtual bool removeOnBehaviorChange() const { return false; } // vf07
    virtual bool allowMultiple() const { return false; }          // vf08
    int16_t id = -1;            // +0x04 id_AGD_* (also the sort key)
    bool owned = false;         // +0x07 owned by a behaviour (Behavior::owned), never freed by agenda
    PlanObject *owner = nullptr; // +0x08
    bool dead = false;          // port: removed during a tick (deferred delete)
};

// CTimerActivity (the agenda's timer item): SET_TIMEOUTMSG(delay, msg, recipient [, data]). Methods 0x42ba20 init,
// 0x42b9e0 default init, 0x42bb10 update.
class TimerActivity : public AgendaItem {
public:
    uint32_t fireTime = 0;           // +0x10 scene clock + delay*10
    int32_t msg = -1;                // +0x14 (initDefault: ids().msgNone)
    uint32_t recipient = 0;          // +0x18 object index or 0xFFFF broadcast (set by init/initDefault)
    uint32_t data = 0;               // +0x1C
    void init(PlanObject &o, Behavior *b, const Node &n, uint32_t msgData) override;
    void initDefault(PlanObject &o, Behavior *b) override;
    void update(PlanObject &o) override;
    void updateRemote(PlanObject &o) override { update(o); }
    bool removeOnBehaviorChange() const override { return true; }
    bool allowMultiple() const override { return true; }
};

// The object's agenda list (aux+4; vf04/vf05 get/set head).
class Agenda {
public:
    ~Agenda();
    void insert(AgendaItem *it);     // CAgendaItem vf05 0x48dd20 (ownership: !owned -> agenda)
    void remove(AgendaItem *it);     // CAgendaItem vf06 0x48df20: unlink + free if !owned
    void unlink(AgendaItem *it);     // unlink only
    void removeById(int id);         // Agenda_RemoveItem 0x4a26a0
    void clear();                    // teardown: free all !owned
    void tick(PlanObject &o, bool remote);
    const std::vector<AgendaItem *> &items() const { return items_; }
    std::vector<AgendaItem *> &mutableItems() { return items_; }
    void collect();                  // free deferred deletions
    void release(AgendaItem *it);    // vf24: free (deferred) unless owned
private:
    std::vector<AgendaItem *> items_;
    std::vector<AgendaItem *> graveyard_;
};

// ---------------------------------------------------------------------------------------------
// Behaviour (CBehavior, vtable 0x4fe858, 0x98 bytes). Subclasses (CBeh*, other agents) override
// setup()/enter() and add owned agenda items; the base class implements the plan statements.
class Behavior {
public:
    virtual ~Behavior();
    // vf01: subclass creates its owned agenda items (initDefault, owned=true) and purge flags,
    // then calls Behavior::setup (CBehavior_Setup 0x4a1dd0) which parses the declaration.
    virtual void setup(PlanObject &o, const Node &decl);
    virtual void enter();                          // vf04 0x4a22e0
    virtual void setInteractorsActive(int32_t v);  // vf05 0x4a2270
    virtual void process() {}                      // vf03 (behaviour-specific per-frame physics)
    void tick();                                   // FUN_004a2360 (latency countdown)

    int16_t id = 0;              // +0x04 id_BEH_*
    int16_t index = 0;           // +0x06 declaration order in the plan
    PlanObject *obj = nullptr;   // +0x34
    std::string name;            // +0x60
    std::vector<std::unique_ptr<AgendaItem>> owned; // +0x08[10] items merged on activation
    uint32_t anim = 0;           // +0x38 assembly (AA_* index in the port)
    uint32_t persistentAnim = 0; // +0x3c
    uint32_t animF40 = 0xBF800000, animF44 = 0xBF800000; // -1.0f
    int32_t animI48 = -1, animI4C = -1;
    uint32_t evt50 = 0, evtSound54 = 0, evt58 = 0, evt5C = 0; // id_SET_AnimationEvent (evtSound54
                                 // is set to ids().msgNone by Registry::createBehavior)
    std::vector<std::unique_ptr<Interactor>> coll; // +0x64
    std::vector<std::unique_ptr<Interactor>> msg;  // +0x68
    int16_t latencyCountdown = -1; // +0x70
    int16_t latency = 10;          // +0x72 id_SET_InteractorLatency
    bool digressive = false;       // +0x74 id_SET_Digressive
    std::array<uint8_t, kMaxAgendaIds> purge{}; // +0x75 id_SET_PurgeFlag (ids < ids().agendaCount)
};

// ---------------------------------------------------------------------------------------------
// Plan instance (0x3C bytes; one per SetPlans entry, only the active one is built).
class PlanInstance {
public:
    PlanObject *obj = nullptr;         // +0x00
    std::string name;                  // +0x04
    int16_t index = 0;                 // +0x08
    std::shared_ptr<PlanTree> tree;    // +0x0C nodes
    std::vector<std::unique_ptr<Behavior>> behaviors; // +0x14 list
    Behavior *current = nullptr;       // +0x18
    int16_t curIndex = 0;              // +0x1C
    int16_t prevIndex = 0;             // +0x1E (_PREVIOUS_BEH_)
    std::vector<std::unique_ptr<Interactor>> globalColl; // +0x20
    std::vector<std::unique_ptr<Interactor>> globalMsg;  // +0x24
    int16_t rearmCountdown = 10;       // +0x2A global collision re-arm countdown
    std::vector<float> localReals;     // +0x2C/+0x30
    std::vector<Interactor *> names;   // +0x34/+0x38 id_SET_InteractorName slots
    bool built = false;

    bool build();                      // PlanInstance_Build 0x45b300
    void teardown();                   // FUN_0045b650
    void changeToBehavior(uint32_t idx); // FUN_0045ace0
    void tick();                       // FUN_0045ade0
    Interactor *collisionByIndex(int16_t n) const; // FUN_0045ac90
    Behavior *behavior(int16_t i) const;
};

// ---------------------------------------------------------------------------------------------
// Per-game-object Plans state (the plan-related part of CGameObject).
class PlanObject {
public:
    PlanObject(PlanScene &scene, PlanHost &host, Registry &reg);
    ~PlanObject();
    PlanObject(const PlanObject &) = delete;
    PlanObject &operator=(const PlanObject &) = delete;

    // InitModel 0x4614c0 + GameObject_CompilePlans 0x462070: load the plan bytes of every
    // SetPlans entry through `load` and build the node trees. Returns false (and logs) on failure.
    bool loadPlans(const PlanLoader &load, const std::vector<std::string> &planNames);
    // Convenience: <dir>/<name><ext> files.
    bool loadPlansFromDir(const std::string &dir, const std::vector<std::string> &planNames,
                          const std::string &ext = ".PBN");
    bool loadPlanFromWords(const std::string &name, const std::vector<uint32_t> &words);
    int planIndex(const std::string &name) const; // vf20 0x462290 (case-sensitive _mbscmp)

    void setActivePlan(int16_t idx);   // vf17 0x463430
    PlanInstance *active() { return active_; }
    PlanInstance &plan(size_t i) { return *plans_[i]; }
    size_t planCount() const { return plans_.size(); }
    // Start (StartMatch/FindTeamPlans): activate plan + enter behaviour.
    void start(int16_t planIdx, uint32_t behIdx);

    // vf09 0x463930: deliver a message now (handler table lookup).
    void receiveMessage(int msg, uint32_t data, uint16_t path, int32_t sender, bool isNet);
    // vf06 0x463890: queue on the scene if this object has a handler for msg.
    bool sendMessage(int msg, uint32_t data, int32_t sender, int32_t isNet = 0);
    // Collision interactor fired by the collision system (FUN_004cdfe0, isNetReplay = 0).
    void fireCollision(Interactor &it, uint32_t data, uint16_t path = kPathRecord, bool isNetReplay = false);
    // Net receive ring (FUN_00472010, 150 entries; drops oldest). Specials: -2 plan+behaviour,
    // -3 behaviour, bit31 collision, else packed message.
    void netEnqueue(uint32_t code, uint32_t data);

    // Message handler table (obj+0x23a byte map, obj+0x2c8 slots; vf12 clear / vf14 register).
    void clearHandlers();
    void registerHandler(Interactor *it);
    Interactor *handlerFor(int msg) const;

    int16_t index() const { return host_.objectIndex(); }
    PlanHost &host() { return host_; }
    PlanScene &scene() { return scene_; }
    Registry &registry() { return reg_; }
    Agenda &agenda() { return agenda_; }

    // Executor (Interactor_ExecuteActions 0x4cd7c0).
    void execute(Interactor &it, const std::vector<Arg> &list, size_t first, size_t count,
                 uint32_t msgData, uint16_t &path);
    void trace(const std::string &s);

private:
    friend class PlanScene;
    friend class PlanInstance;
    void dispatch(Interactor &it, uint32_t data, uint16_t &path); // tail of 4ce3b0/4cdfe0
    void processNetQueue();
    LoadHooks loadHooks();

    PlanScene &scene_;
    PlanHost &host_;
    Registry &reg_;
    std::vector<std::shared_ptr<PlanTree>> trees_;
    std::vector<std::string> planNames_;
    std::vector<std::unique_ptr<PlanInstance>> plans_;
    PlanInstance *active_ = nullptr;
    Agenda agenda_;
    std::vector<uint8_t> handlerSlot_; // ids().messageCount entries
    std::vector<Interactor *> handlers_{nullptr};
    std::deque<std::pair<uint32_t, uint32_t>> netRing_;
    // torn-down plan objects are kept until the end of the frame (the exe pools them).
    std::vector<std::unique_ptr<Behavior>> deadBehaviors_;
    std::vector<std::unique_ptr<Interactor>> deadInteractors_;
};

// ---------------------------------------------------------------------------------------------
// Scene-level message queue and frame driver (the Plans part of CSceneManager).
class PlanScene {
public:
    struct Queued { int16_t recipient; int32_t msg; uint32_t data; int32_t sender; int32_t isNet; };

    void add(PlanObject *o);
    void remove(PlanObject *o);
    PlanObject *object(int16_t idx) const;
    const std::vector<PlanObject *> &objects() const { return objects_; }

    uint32_t clock = 0;       // scene+0x5260 (timer base, ms [guess]; SET_TIMEOUTMSG adds delay*10)
    float frameRate = 18.0f;  // scene+0x522c (frames/s; id_SET_InteractorLatency is in 1/100 s)

    bool queue(int16_t recipient, int32_t msg, uint32_t data, int32_t sender, int32_t isNet); // 0x4de830
    void broadcast(int32_t msg, uint32_t data, int16_t sender); // CSceneManager_PlayEvent 0x4ddfc0
    // Per frame (CSceneManager_Update 0x4de100, play state): tick() for every object,
    // then processMessages(), then endFrame().
    void tick();             // agenda + plan tick per object (IntegrateObject 0x411f50)
    void processMessages();  // CSceneManager_ProcessMessages 0x4de8f0
    void endFrame();         // CSceneManager_ForcedNetBehaviors 0x4deb70
    void update() { tick(); processMessages(); endFrame(); }
    size_t pending() const { return queue_.size(); }
    const std::vector<Queued> &queued() const { return queue_; } // [mp] Scene::stateHash
    std::function<void(int32_t msg)> onBroadcastHook; // PlayEvent clock side effects (msg 0/7/0x5b)

private:
    std::vector<PlanObject *> objects_;
    std::vector<Queued> queue_;
    int16_t maxQueued_ = 0;
};

} // namespace adlib
