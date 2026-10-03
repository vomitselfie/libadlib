// ADLIB runtime implementation. Addresses refer to HYPERX.EXE; see docs/provenance.md and docs/research/.
#include "adlib/runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace adlib {

namespace {
// The exe throws a char* ("Invalid Instruction object in In...") through __CxxThrowException.
struct PlanError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}
} // namespace

// =============================================================================================
// Decision path (Interactor_ExecuteActions 0x4cd7c0, DECIDE_BY_* case)

uint16_t pathRecord(uint16_t p, uint16_t idx) {
    if (p == 0xFFFF) p = 0;
    switch (p & 0x3000) {
    case 0x0000: return uint16_t((p & 0x0FFF) | idx | 0x9000);
    case 0x1000: return uint16_t(((idx | 0xFF40) << 7) | (p & 0x7F)); // = 0xA000|idx<<7|d1
    default:     return uint16_t(((p & 0x780) >> 2) | (idx & 0xE00) | (p & 0x1F) | 0xB000); // sic
    }
}

uint16_t pathReplay(uint16_t &p) {
    uint16_t idx;
    switch (p & 0x3000) {
    case 0x1000:
        idx = p & 0x0FFF;
        p = 0;
        break;
    case 0x2000:
        idx = p & 0x7F;
        p = uint16_t(((int16_t(p) >> 7) & 0xFE1F) | 0x1000);
        break;
    default:
        idx = p & 0x1F;
        p = uint16_t((int16_t(((p & 0x1E0) >> 3) | (p & 0xE00)) >> 2) | 0x2000);
        break;
    }
    return idx;
}

// =============================================================================================
// ActionCtx helpers

uint32_t ActionCtx::arg(size_t i) const {
    uint32_t v = node.leaf(i);
    return v == kMessageData ? msgData : v;
}

int32_t ActionCtx::argResolved(size_t i) const {
    uint32_t v = arg(i);
    if ((v >> 16) == kTagPronoun) return obj.registry().resolveRuntimePronoun(int(v & 0xFFFF), obj);
    return int32_t(v);
}

float ActionCtx::argFloat(size_t i) const {
    uint32_t v = node.leaf(i);
    if ((v >> 16) != kTagLocalReal) return float(int32_t(v));
    PlanInstance *p = obj.active();
    size_t r = v & 0xFFFF;
    if (!p || r >= p->localReals.size()) return 666.666f;
    return p->localReals[r];
}

const Node *ActionCtx::dataBlock() const {
    const Node *d = node.child(1);
    return d && d->id == DATA_BLOCK ? d : nullptr;
}

// =============================================================================================
// Registry

// RuntimeIds ----------------------------------------------------------------------------------

RuntimeIds RuntimeIds::resolve(const Vocabulary *v, std::vector<std::string> *missing) {
    RuntimeIds r;
    if (!v) return r;
    auto get = [&](Role role, std::initializer_list<const char *> names, int &out) {
        const Namespace *n = v->role(role);
        if (!n) return; // role unbound: disabled (the default)
        for (const char *nm : names) {
            int id = n->id(nm);
            if (id >= 0) { out = id; return; }
        }
        out = -1;
        if (missing) missing->push_back(std::string(roleName(role)) + "." + *names.begin());
    };
    if (const Namespace *m = v->role(Role::Message)) r.messageCount = m->size();
    get(Role::Message, {"ExitBehavior"}, r.msgExitBehavior);
    get(Role::Message, {"Initiate"}, r.msgInitiate);
    get(Role::Message, {"NOMSG", "NoMsg"}, r.msgNone);
    if (const Namespace *a = v->role(Role::Agenda)) {
        r.agendaCount = a->size();
        int t = a->id("CTimerActivity");
        if (t < 0) t = a->id("TimerActivity");
        if (t < 0) { t = r.agendaCount; r.agendaCount += 1; } // implicit item after the last id
        r.agdTimerActivity = t;
    }
    if (v->role(Role::Object)) {
        int s = v->id(Role::Object, "NOMODEL");
        if (s >= 0) r.timerSender = s;
    }
    get(Role::BehaviorParam, {"KeyMonitorMode"}, r.bsetKeyMonitorMode);
    get(Role::BehaviorParam, {"InteractorLatency"}, r.bsetInteractorLatency);
    get(Role::BehaviorParam, {"PersistentAnimation"}, r.bsetPersistentAnimation);
    get(Role::BehaviorParam, {"Animation"}, r.bsetAnimation);
    get(Role::BehaviorParam, {"AnimationEvent"}, r.bsetAnimationEvent);
    get(Role::BehaviorParam, {"Digressive"}, r.bsetDigressive);
    get(Role::BehaviorParam, {"PurgeFlag"}, r.bsetPurgeFlag);
    get(Role::InteractorParam, {"InteractorName"}, r.isetInteractorName);
    get(Role::InteractorParam, {"ActiveState"}, r.isetActiveState);
    return r;
}

Registry::Registry(std::shared_ptr<const Vocabulary> vocab) {
    log = [](const std::string &s) { std::fprintf(stderr, "[adlib] %s\n", s.c_str()); };
    setVocabulary(std::move(vocab));
}

void Registry::setVocabulary(std::shared_ptr<const Vocabulary> vocab) {
    vocab_ = std::move(vocab);
    ids_ = RuntimeIds::resolve(vocab_.get());
    registerFrameworkDefaults(*this);
}

const char *Registry::symbol(Role r, int id) const {
    const char *n = vocab_ ? vocab_->name(r, id) : nullptr;
    return n ? n : "?";
}

int Registry::idOf(Role r, std::string_view name) const { return vocab_ ? vocab_->id(r, name) : -1; }

int Registry::lookup(Role r, std::string_view name) {
    int id = idOf(r, name);
    if (id < 0) {
        std::string m = std::string(roleName(r)) + " " + std::string(name);
        unresolved_.push_back(m);
        if (log) log("unknown " + m + " (not in the vocabulary; not registered)");
    }
    return id;
}
int Registry::action(std::string_view n, ActionFn f) { int id = lookup(Role::Action, n); setAction(id, std::move(f)); return id; }
int Registry::decision(std::string_view n, DecisionFn f) { int id = lookup(Role::Decision, n); setDecision(id, std::move(f)); return id; }
int Registry::behaviorParam(std::string_view n, BehaviorParamFn f) { int id = lookup(Role::BehaviorParam, n); setBehaviorParam(id, std::move(f)); return id; }
int Registry::interactorParam(std::string_view n, InteractorParamFn f) { int id = lookup(Role::InteractorParam, n); setInteractorParam(id, std::move(f)); return id; }
int Registry::agenda(std::string_view n, AgendaFactory f) { int id = lookup(Role::Agenda, n); setAgenda(id, std::move(f)); return id; }
int Registry::behavior(std::string_view n, BehaviorFactory f) { int id = lookup(Role::Behavior, n); setBehavior(id, std::move(f)); return id; }
int Registry::runtimePronoun(std::string_view n, RuntimePronounFn f) { int id = lookup(Role::Pronoun, n); setRuntimePronoun(id, std::move(f)); return id; }

template <class T> void Registry::put(std::vector<T> &v, int id, T f) {
    if (id < 0) return;
    if (size_t(id) >= v.size()) v.resize(size_t(id) + 1);
    v[size_t(id)] = std::move(f);
}
void Registry::setAction(int id, ActionFn f) { put(acf_, id, std::move(f)); }
void Registry::setDecision(int id, DecisionFn f) { put(dcf_, id, std::move(f)); }
void Registry::setBehaviorParam(int id, BehaviorParamFn f) { put(bset_, id, std::move(f)); }
void Registry::setInteractorParam(int id, InteractorParamFn f) { put(iset_, id, std::move(f)); }
void Registry::setAgenda(int id, AgendaFactory f) { put(agd_, id, std::move(f)); }
void Registry::setBehavior(int id, BehaviorFactory f) { put(beh_, id, std::move(f)); }
void Registry::setRuntimePronoun(int id, RuntimePronounFn f) { put(prn_, id, std::move(f)); }

void Registry::warnOnce(const std::string &key, const std::string &msg) {
    if (warned_.insert(key).second && log) log(msg);
}

void Registry::callAction(int id, ActionCtx &c) {
    if (trace) trace(c.obj, fmt("DO_ACTION %s", symbol(Role::Action, id)));
    emit(c.obj, TraceEvent::Kind::Action, id, 0, 0, &c.node);
    if (hasAction(id)) { acf_[size_t(id)](c); return; }
    warnOnce(fmt("acf%d", id), fmt("unimplemented ACF %d %s", id, symbol(Role::Action, id)));
}

uint16_t Registry::callDecision(int id, ActionCtx &c) {
    // Stub: first real branch. DECIDE_BY_WITH_AMONG branches are 1-based (children[1] is the
    // DataBlock; the exe would throw on branch 0, and those DCFs never return it).
    uint16_t r = c.node.id == DECIDE_BY_WITH_AMONG ? 1 : 0;
    if (hasDecision(id)) r = dcf_[size_t(id)](c);
    else warnOnce(fmt("dcf%d", id), fmt("unimplemented DCF %d %s (first branch)", id, symbol(Role::Decision, id)));
    if (trace) trace(c.obj, fmt("DECIDE %s -> %u", symbol(Role::Decision, id), r));
    emit(c.obj, TraceEvent::Kind::Decision, id, r, 0, &c.node);
    return r;
}

void Registry::callBehaviorParam(int id, PlanObject &o, const Node &n, Behavior *b) {
    if (trace) trace(o, fmt("SET_BEHAVIOR_PARAM %s", symbol(Role::BehaviorParam, id)));
    emit(o, TraceEvent::Kind::BehaviorParam, id, 0, 0, &n);
    if (id >= 0 && size_t(id) < bset_.size() && bset_[size_t(id)]) { bset_[size_t(id)](o, n, b); return; }
    warnOnce(fmt("bset%d", id), fmt("unimplemented SET_BEHAVIOR_PARAM %d %s", id, symbol(Role::BehaviorParam, id)));
}

void Registry::callInteractorParam(int id, PlanObject &o, const Node &n, Behavior *b, Interactor *it) {
    if (trace) trace(o, fmt("SET_INTERACTOR_PARAM %s", symbol(Role::InteractorParam, id)));
    emit(o, TraceEvent::Kind::InteractorParam, id, 0, 0, &n);
    if (id >= 0 && size_t(id) < iset_.size() && iset_[size_t(id)]) { iset_[size_t(id)](o, n, b, it); return; }
    warnOnce(fmt("iset%d", id), fmt("unimplemented SET_INTERACTOR_PARAM %d", id));
}

std::unique_ptr<AgendaItem> Registry::createAgenda(int id) {
    std::unique_ptr<AgendaItem> p;
    if (id >= 0 && size_t(id) < agd_.size() && agd_[size_t(id)]) p = agd_[size_t(id)]();
    if (!p) {
        warnOnce(fmt("agd%d", id), fmt("unimplemented agenda item %d %s (inert stub)", id, symbol(Role::Agenda, id)));
        p = std::make_unique<AgendaItem>();
    }
    p->id = int16_t(id);
    return p;
}

std::unique_ptr<Behavior> Registry::createBehavior(int id) {
    std::unique_ptr<Behavior> p;
    if (id >= 0 && size_t(id) < beh_.size() && beh_[size_t(id)]) p = beh_[size_t(id)]();
    if (!p) {
        warnOnce(fmt("beh%d", id), fmt("behaviour %d %s uses the generic CBehavior base", id, symbol(Role::Behavior, id)));
        p = std::make_unique<Behavior>();
    }
    p->id = int16_t(id);
    p->evtSound54 = uint32_t(ids_.msgNone);
    return p;
}

int32_t Registry::resolveRuntimePronoun(int id, PlanObject &o) {
    if (id >= 0 && size_t(id) < prn_.size() && prn_[size_t(id)]) return prn_[size_t(id)](o);
    warnOnce(fmt("prn%d", id), fmt("unresolved runtime pronoun %d %s (-1)", id, symbol(Role::Pronoun, id)));
    return -1;
}

// Framework entries: the SET_BEHAVIOR_PARAM functions that touch only CBehavior fields
// (InitPlanFnFactories 0x4025a9 table 0x5269D8), the SET_INTERACTOR_PARAM functions
// (0x527650/54) and the CTimerActivity factory (0x526478).
void registerFrameworkDefaults(Registry &r) {
    const RuntimeIds &ids = r.ids();
    const int agendaCount = std::min(ids.agendaCount, kMaxAgendaIds);
    // 1 id_SET_KeyMonitorMode: FUN_004c4e40 is an empty function.
    r.setBehaviorParam(ids.bsetKeyMonitorMode, [](PlanObject &, const Node &, Behavior *) {});
    // 2 id_SET_InteractorLatency (FUN_004c4710): frames = ftol(arg * 0.01 * fps).
    r.setBehaviorParam(ids.bsetInteractorLatency, [](PlanObject &o, const Node &n, Behavior *b) {
        if (!b) return;
        double v = double(int16_t(n.leaf(1))) * 0.01 * double(o.scene().frameRate);
        b->latency = int16_t(int32_t(v));
    });
    // 9 id_SET_PersistentAnimation (FUN_004c4a10): anim + persistent anim. (A host can
    // re-register this parameter, e.g. to substitute animations for an injured character.)
    r.setBehaviorParam(ids.bsetPersistentAnimation, [](PlanObject &, const Node &n, Behavior *b) {
        if (!b) return;
        uint32_t a = uint32_t(int16_t(n.leaf(1)));
        b->anim = b->persistentAnim = a;
        if (n.count() == 4) {
            b->animF40 = b->animF44 = n.leaf(2);
            b->animI48 = b->animI4C = int32_t(n.leaf(3));
        } else {
            b->animF40 = b->animF44 = 0xBF800000;
            b->animI48 = b->animI4C = 1; // sic: 1, not -1 as in id_SET_Animation
        }
    });
    // 11 id_SET_Animation (FUN_004c4d00)
    r.setBehaviorParam(ids.bsetAnimation, [](PlanObject &, const Node &n, Behavior *b) {
        if (!b) return;
        b->anim = uint32_t(int16_t(n.leaf(1)));
        if (n.count() == 4) { b->animF40 = n.leaf(2); b->animI48 = int32_t(n.leaf(3)); }
        else { b->animF40 = 0xBF800000; b->animI48 = -1; }
    });
    // 12 id_SET_AnimationEvent (FUN_004c4d70): +0x50,+0x58,+0x54 (sound id), +0x5c
    r.setBehaviorParam(ids.bsetAnimationEvent, [](PlanObject &, const Node &n, Behavior *b) {
        if (!b) return;
        b->evt50 = n.leaf(1);
        b->evt58 = n.leaf(2);
        b->evtSound54 = n.leaf(3);
        b->evt5C = n.count() == 5 ? n.leaf(4) : 0;
    });
    // 15 id_SET_Digressive (FUN_004c4760)
    r.setBehaviorParam(ids.bsetDigressive, [](PlanObject &, const Node &n, Behavior *b) {
        if (b) b->digressive = uint8_t(n.leaf(1)) != 0;
    });
    // 16 id_SET_PurgeFlag (FUN_004c4780): purge[agdId] = flag
    r.setBehaviorParam(ids.bsetPurgeFlag, [agendaCount](PlanObject &, const Node &n, Behavior *b) {
        uint32_t id = n.leaf(1);
        if (b && id < uint32_t(agendaCount)) b->purge[id] = uint8_t(n.leaf(2));
    });

    // Interactor params. 0 id_SET_InteractorName (FUN_004a6180): names[arg1] = interactor.
    r.setInteractorParam(ids.isetInteractorName, [](PlanObject &o, const Node &n, Behavior *, Interactor *it) {
        PlanInstance *p = o.active();
        size_t slot = size_t(int16_t(n.leaf(1)));
        if (!p || slot > 0x7FFF) return;
        if (slot >= p->names.size()) p->names.resize(slot + 1, nullptr);
        p->names[slot] = it;
    });
    // 1 id_SET_ActiveState (FUN_004a61b0): (state) on this interactor, or (NAMEREF, state).
    r.setInteractorParam(ids.isetActiveState, [](PlanObject &o, const Node &n, Behavior *, Interactor *it) {
        if (n.count() == 2) { if (it) it->setActive(int32_t(n.leaf(1))); return; }
        if (n.count() == 3 && (n.leaf(1) >> 16) == kTagNameRef) {
            PlanInstance *p = o.active();
            size_t slot = n.leaf(1) & 0xFFFF;
            if (p && slot < p->names.size() && p->names[slot]) p->names[slot]->setActive(int32_t(n.leaf(2)));
        }
    });

    r.setAgenda(ids.agdTimerActivity, [] { return std::make_unique<TimerActivity>(); });
}

// =============================================================================================
// Interactors

int16_t Interactor::cobMine() const {
    if ((a28 >> 16) == kTagPronoun) return int16_t(obj->registry().resolveRuntimePronoun(int(a28 & 0xFFFF), *obj));
    return int16_t(a28);
}
int16_t Interactor::cobOther() const {
    if ((a2c >> 16) == kTagPronoun) return int16_t(obj->registry().resolveRuntimePronoun(int(a2c & 0xFFFF), *obj));
    return int16_t(a2c);
}

// CCollInteractor_ctor 0x4cdea0 / CMsgInteractor_ctor 0x4ce240.
static std::unique_ptr<Interactor> makeInteractor(PlanObject &o, const Node &n, Behavior *beh) {
    auto it = std::make_unique<Interactor>();
    it->obj = &o;
    it->beh = beh;
    size_t first;
    if (n.cls() == SET_COLLISION_INTERACTOR) {
        it->kind = Interactor::Kind::Collision;
        it->a28 = n.leaf(0);
        it->a2c = n.leaf(1);
        first = 2;
    } else {
        it->kind = Interactor::Kind::Message;
        it->a2c = n.leaf(0);
        it->a28 = uint32_t(int32_t(o.index()));
        first = 1;
    }
    for (size_t i = first; i < n.args.size(); ++i) {
        const Node *c = n.child(i);
        if (!c) throw PlanError("Invalid Instruction object in interactor (leaf)");
        switch (c->cls()) {
        case 0x00070000:
            if (c->variant() == 2) it->nextBeh = c->leaf(0);
            break;
        case 0x00060000: break; // SET_DEBUG / SET_TRACE
        case 0x00180000:
            if (c->variant() == 1) {
                PlanInstance *p = o.active();
                o.registry().callInteractorParam(int16_t(c->leaf(0)), o, *c, p ? p->current : nullptr, it.get());
            }
            break;
        case ENABLE: it->enable = c; break;
        default:
            throw PlanError(n.cls() == SET_COLLISION_INTERACTOR ? "Invalid Instruction object in CollInteractor"
                                                                : "Invalid Instruction object in MsgInteractor");
        }
    }
    it->variant = n.variant(); // vf03 (+0x0c) called by the creator with id & 0xffff
    return it;
}

// =============================================================================================
// Agenda

Agenda::~Agenda() { clear(); collect(); }

void Agenda::release(AgendaItem *it) {
    if (it->owned) return;
    it->dead = true;
    graveyard_.push_back(it);
}

void Agenda::unlink(AgendaItem *it) {
    auto i = std::find(items_.begin(), items_.end(), it);
    if (i != items_.end()) items_.erase(i);
}

void Agenda::remove(AgendaItem *it) {
    unlink(it);
    release(it);
}

void Agenda::insert(AgendaItem *it) {
    // CAgendaItem vf05 0x48dd20: keep ascending id order; an equal id is replaced unless the
    // newcomer allows multiples (then it goes in front of the existing ones).
    // Not reproduced: removal of lower-id items owned by other objects; the id 8
    // (CAvoidObjects) merge path (FUN_00429380) for allowMultiple items.
    size_t pos = 0;
    while (pos < items_.size() && items_[pos]->id < it->id) ++pos;
    if (pos < items_.size() && items_[pos]->id == it->id && !it->allowMultiple()) {
        AgendaItem *old = items_[pos];
        if (old == it) return;
        items_.erase(items_.begin() + long(pos));
        release(old);
    }
    items_.insert(items_.begin() + long(pos), it);
}

void Agenda::removeById(int id) {
    // Agenda_RemoveItem 0x4a26a0: unowned items are released, owned ones only unlinked.
    std::vector<AgendaItem *> copy = items_;
    for (AgendaItem *it : copy)
        if (it->id == id) remove(it);
}

void Agenda::clear() {
    std::vector<AgendaItem *> copy;
    copy.swap(items_);
    for (AgendaItem *it : copy) release(it);
}

void Agenda::tick(PlanObject &o, bool remote) {
    // The exe calls head->vf01(0); each item calls next->vf01 itself (a recursive chain).
    std::vector<AgendaItem *> snap = items_;
    for (AgendaItem *it : snap) {
        if (it->dead || std::find(items_.begin(), items_.end(), it) == items_.end()) continue;
        if (remote) it->updateRemote(o); else it->update(o);
    }
}

void Agenda::collect() {
    for (AgendaItem *it : graveyard_) delete it;
    graveyard_.clear();
}

// CTimerActivity ------------------------------------------------------------------------------

void TimerActivity::init(PlanObject &o, Behavior *, const Node &n, uint32_t msgData) {
    owner = &o;
    id = int16_t(o.registry().ids().agdTimerActivity);
    fireTime = o.scene().clock + uint32_t(int32_t(n.leaf(0)) * 10);
    msg = int32_t(n.leaf(1));
    recipient = n.leaf(2); // read unconditionally in the exe
    if (n.count() == 4) {
        uint32_t d = n.leaf(3);
        data = d == kMessageData ? msgData : d;
    } else {
        data = 0;
    }
}

void TimerActivity::initDefault(PlanObject &o, Behavior *) {
    owner = &o;
    id = int16_t(o.registry().ids().agdTimerActivity);
    fireTime = 0;
    msg = o.registry().ids().msgNone;
    recipient = uint32_t(o.registry().ids().timerSender);
    data = 0;
}

void TimerActivity::update(PlanObject &o) {
    if (o.scene().clock <= fireTime) return; // fires when fireTime < clock
    if (o.registry().trace) o.trace(fmt("timer fires %s -> %d", o.registry().symbol(Role::Message, msg), int(int16_t(recipient))));
    o.registry().emit(o, TraceEvent::Kind::TimerFire, msg, 0, int(int16_t(recipient)));
    if (recipient == kBroadcast) {
        o.scene().broadcast(msg, data, o.index());
    } else {
        int32_t r = int32_t(recipient);
        if ((recipient >> 16) == kTagPronoun) r = o.registry().resolveRuntimePronoun(int(recipient & 0xFFFF), o); // port
        if (PlanObject *dst = o.scene().object(int16_t(r))) dst->sendMessage(msg, data, o.registry().ids().timerSender, 0);
    }
    o.agenda().remove(this);
}

// =============================================================================================
// Behaviours

Behavior::~Behavior() = default;

// CBehavior_Setup 0x4a1dd0
void Behavior::setup(PlanObject &o, const Node &decl) {
    obj = &o;
    size_t first = decl.id == DECLARE_BEHAVIOR ? 2 : 0;
    for (size_t i = first; i < decl.args.size(); ++i) {
        const Node *c = decl.child(i);
        if (!c) throw PlanError("Invalid Instruction object in Behavior (leaf)");
        switch (c->cls()) {
        case 0x00050000: {
            auto it = makeInteractor(o, *c, this);
            msg.push_back(std::move(it));
            break;
        }
        case 0x00030000: {
            auto it = makeInteractor(o, *c, this);
            coll.push_back(std::move(it));
            break;
        }
        case 0x00070000:
            if (c->variant() == 3) o.registry().callBehaviorParam(int16_t(c->leaf(0)), o, *c, this);
            break;
        case 0x00060000: break;
        case 0x00180000: {
            uint16_t v = c->variant();
            ActionCtx ctx{o, nullptr, this, *c, nullptr, 0};
            int id = int16_t(c->leaf(0));
            if (v == 0) o.registry().callAction(id, ctx);
            else if (v == 1) o.registry().callInteractorParam(id, o, *c, this, nullptr);
            else if (v == 2) { if (o.host().netRole() != RoleRemote) o.registry().callAction(id, ctx); }
            else if (v == 3) { if (o.host().netRole() != RoleLocal) o.registry().callAction(id, ctx); }
            break;
        }
        case 0x001A0000:
            if (c->id == ADD_AGENDA_ITEM) {
                auto item = o.registry().createAgenda(int16_t(c->leaf(0)));
                item->initDefault(o, this);
                item->owner = &o;
                o.agenda().insert(item.release());
            } else if (c->id == REMOVE_AGENDA_ITEM) {
                o.agenda().removeById(int32_t(c->leaf(0)));
            } // SET_TIMEOUTMSG is silently ignored here
            break;
        default:
            throw PlanError("Invalid Instruction object in Behavior");
        }
    }
}

// CBehGlideToPosition__vf04 0x4a22e0 (base CBehavior::Enter)
void Behavior::enter() {
    const RuntimeIds &ids = obj->registry().ids();
    obj->sendMessage(ids.msgInitiate, 0, obj->index(), 0); // vf07 -> queued
    latencyCountdown = latency;
    if (anim) obj->host().playBehaviorAnimation(anim, animF40, animI48);
    if (evtSound54 != uint32_t(ids.msgNone)) obj->host().playBehaviorSound(evt58, evtSound54, evt5C);
}

// 0x4a2270
void Behavior::setInteractorsActive(int32_t v) {
    for (auto &i : coll) if (i->active != -1) i->setActive(v);
    for (auto &i : msg) if (i->active != -1) i->setActive(v);
}

// FUN_004a2360
void Behavior::tick() {
    int16_t old = latencyCountdown;
    latencyCountdown = int16_t(old - 1);
    if (old == 0) setInteractorsActive(1);
}

// =============================================================================================
// Plan instances

Behavior *PlanInstance::behavior(int16_t i) const {
    return i >= 0 && size_t(i) < behaviors.size() ? behaviors[size_t(i)].get() : nullptr;
}

// PlanInstance_Build 0x45b300
bool PlanInstance::build() {
    PlanObject &o = *obj;
    built = false;
    if (!tree) return false;
    int16_t nColl = 0;
    try {
        for (auto &np : tree->top) {
            const Node &n = *np;
            uint32_t c = n.cls();
            if (n.id == END_PLAN) break;
            if (c == 0x00160000) { // DECLARE_LOCAL_REALS (CHANGE_OF_PLAN shares the class)
                localReals.assign(size_t(n.count() > 0 ? n.count() : 0), 0.0f); // DAT_004fc890 = 0.0f
            } else if (c == 0x00070000) {
                if (n.variant() != 0) throw PlanError("Expected a Behavior Declaration");
                auto b = o.registry().createBehavior(int16_t(n.leaf(0)));
                b->digressive = false;
                b->obj = &o;
                b->setup(o, n);
                int16_t k = nColl; // FUN_004a2670: behaviour collision interactors follow globals
                for (auto &ci : b->coll) ci->collIndex = k++;
                b->setInteractorsActive(0);
                if (n.args.size() > 1 && n.args[1].kind == Arg::Kind::String) b->name = n.args[1].str;
                b->index = int16_t(behaviors.size());
                behaviors.push_back(std::move(b));
            } else if (c == GLOBAL_INTERACTORS) {
                for (size_t i = 0; i < n.args.size(); ++i) {
                    const Node *g = n.child(i);
                    if (g && g->cls() == 0x00030000) {
                        auto it = makeInteractor(o, *g, nullptr);
                        it->collIndex = nColl++;
                        globalColl.push_back(std::move(it));
                    } else if (g && g->cls() == 0x00050000) {
                        globalMsg.push_back(makeInteractor(o, *g, nullptr));
                    } else {
                        throw PlanError("Invalid Instruction object in global interactors");
                    }
                }
            } else {
                throw PlanError("Invalid Instruction in Plan Instantiation");
            }
        }
    } catch (const std::exception &e) {
        o.registry().log(fmt("plan %s: %s", name.c_str(), e.what()));
        return false;
    }
    curIndex = prevIndex = 0;
    current = behaviors.empty() ? nullptr : behaviors.front().get();
    rearmCountdown = 10;
    built = true;
    return true;
}

// FUN_0045b650
void PlanInstance::teardown() {
    PlanObject &o = *obj;
    localReals.clear();
    for (auto &i : globalColl) o.deadInteractors_.push_back(std::move(i));
    for (auto &i : globalMsg) o.deadInteractors_.push_back(std::move(i));
    globalColl.clear();
    globalMsg.clear();
    names.clear();
    o.agenda().clear(); // every agenda item is released (owned ones belong to their behaviour)
    o.clearHandlers();  // port: the exe leaves stale slots pointing at pooled interactors
    for (auto &b : behaviors) o.deadBehaviors_.push_back(std::move(b));
    behaviors.clear();
    current = nullptr;
    built = false;
}

// FUN_0045ac90
Interactor *PlanInstance::collisionByIndex(int16_t n) const {
    int16_t base = int16_t(globalColl.size());
    if (n < base) return n >= 0 ? globalColl[size_t(n)].get() : nullptr;
    if (!current) return nullptr;
    size_t k = size_t(n - base);
    return k < current->coll.size() ? current->coll[k].get() : nullptr;
}

// FUN_0045ade0
void PlanInstance::tick() {
    int16_t old = rearmCountdown;
    rearmCountdown = int16_t(old - 1);
    if (old == 0)
        for (auto &i : globalColl) i->setActive(1);
    if (current) current->tick();
}

// FUN_004a23a0: agenda swap + message handler registration for a behaviour being entered.
static void activateBehavior(PlanObject &o, PlanInstance &p, Behavior &b) {
    Agenda &ag = o.agenda();
    const int purgeCount = std::min(o.registry().ids().agendaCount, kMaxAgendaIds);
    // 1. purge / drop items of the previous state
    std::vector<AgendaItem *> snap = ag.items();
    for (AgendaItem *it : snap) {
        if (it->id >= 0 && it->id < purgeCount && b.purge[size_t(it->id)]) {
            ag.remove(it); // unowned released, owned unlinked
        } else if ((it->removeOnBehaviorChange() || !it->owned) && !b.digressive) {
            ag.remove(it);
        }
    }
    // 2. merge the behaviour's own items (ascending id; equal id replaces)
    auto &items = ag.mutableItems();
    size_t cur = 0;
    for (auto &up : b.owned) {
        AgendaItem *it = up.get();
        if (!it) break;
        while (cur < items.size() && items[cur]->id < it->id) ++cur;
        if (cur < items.size() && items[cur]->id == it->id) {
            // exe: the old item is unlinked (and leaked if unowned). If it *is* this item the
            // exe truncates the list after it; the port keeps the list intact.
            if (items[cur] != it) {
                AgendaItem *old = items[cur];
                items[cur] = it;
                ag.release(old);
            }
        } else {
            items.insert(items.begin() + long(cur), it);
        }
        ++cur;
    }
    // 3. message handler table: globals first, then the behaviour's (later wins per msg id)
    o.clearHandlers();
    int role = o.host().netRole();
    auto reg = [&](Interactor *i) {
        if (i->variant == 0 || (i->variant == 1 && role == RoleLocal) || (i->variant == 2 && role == RoleRemote))
            o.registerHandler(i);
    };
    for (auto &i : p.globalMsg) reg(i.get());
    for (auto &i : b.msg) reg(i.get());
}

// FUN_0045ace0
void PlanInstance::changeToBehavior(uint32_t idx) {
    PlanObject &o = *obj;
    if (idx == kPreviousBeh) idx = uint16_t(prevIndex);
    if (current && !current->digressive) prevIndex = curIndex;
    curIndex = int16_t(idx);
    if (current) current->setInteractorsActive(0);
    if (behaviors.empty()) return;
    size_t target = size_t(uint16_t(idx)) % behaviors.size(); // walking past the end wraps
    Behavior *b = behaviors[target].get();
    if (o.registry().trace) o.trace(fmt("-> behaviour %d \"%s\"", int(target), b->name.c_str()));
    if (o.registry().traceEvent) {
        TraceEvent ev{TraceEvent::Kind::Change, -1, -1, int64_t(target), nullptr, {}};
        if (current) ev.a = current->index;
        ev.text = (current ? current->name : std::string("-")) + " -> " + b->name;
        o.registry().traceEvent(o, ev);
    }
    current = b;
    activateBehavior(o, *this, *b);
    b->enter();
    // A behaviour without interactors falls through to the next declared one (curIndex stays).
    while (b->coll.empty() && b->msg.empty() && target + 1 < behaviors.size()) {
        b = behaviors[++target].get();
        if (o.registry().traceEvent) {
            TraceEvent ev{TraceEvent::Kind::Change, -1, current->index, int64_t(target), nullptr, {}};
            ev.text = current->name + " -> " + b->name + " (fallthrough)";
            o.registry().traceEvent(o, ev);
        }
        current = b;
        activateBehavior(o, *this, *b);
        b->enter();
    }
}

// =============================================================================================
// PlanObject

PlanObject::PlanObject(PlanScene &scene, PlanHost &host, Registry &reg)
    : scene_(scene), host_(host), reg_(reg), handlerSlot_(size_t(std::max(reg.ids().messageCount, 0)), 0) {}

PlanObject::~PlanObject() {
    if (active_) active_->teardown();
    scene_.remove(this);
    agenda_.collect();
}

void PlanObject::trace(const std::string &s) {
    if (reg_.trace) reg_.trace(*this, s);
}

int PlanObject::planIndex(const std::string &name) const {
    for (size_t i = 0; i < planNames_.size(); ++i)
        if (planNames_[i] == name) return int(i); // _mbscmp (case-sensitive) in the exe
    for (size_t i = 0; i < planNames_.size(); ++i) { // port: tolerate DOS-style case mismatches
        const std::string &a = planNames_[i];
        if (a.size() == name.size() &&
            std::equal(a.begin(), a.end(), name.begin(), [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); }))
            return int(i);
    }
    reg_.warnOnce("noplan" + name, "Could not find plan named " + name);
    return -1;
}

LoadHooks PlanObject::loadHooks() {
    LoadHooks h;
    if (reg_.loadPronoun) {
        h.pronoun = [this](uint16_t prn, uint32_t tok) { return reg_.loadPronoun(*this, prn, tok); };
    }
    h.planIndex = [this](const std::string &s) { return planIndex(s); };
    return h;
}

bool PlanObject::loadPlans(const PlanLoader &load, const std::vector<std::string> &names) {
    std::vector<std::string> keep = names;
    planNames_ = keep; // CHANGE_OF_PLAN targets are looked up while loading
    trees_.clear();
    plans_.clear();
    bool ok = true;
    for (size_t i = 0; i < keep.size(); ++i) {
        std::vector<uint8_t> bytes;
        std::vector<uint32_t> words;
        std::string err;
        if (!load || !load(keep[i], bytes)) { reg_.log("Binary Plan " + keep[i] + " could not be opened"); ok = false; continue; }
        if (!readPbnWords(bytes, words, &err)) { reg_.log(keep[i] + ": " + err); ok = false; continue; }
        if (!loadPlanFromWords(keep[i], words)) ok = false;
    }
    return ok;
}

bool PlanObject::loadPlansFromDir(const std::string &dir, const std::vector<std::string> &names,
                                  const std::string &ext) {
    return loadPlans([&](const std::string &n, std::vector<uint8_t> &bytes) {
        std::ifstream f(dir + "/" + n + ext, std::ios::binary);
        if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }, names);
}

bool PlanObject::loadPlanFromWords(const std::string &name, const std::vector<uint32_t> &words) {
    if (std::find(planNames_.begin(), planNames_.end(), name) == planNames_.end()) planNames_.push_back(name);
    auto tree = std::make_shared<PlanTree>();
    tree->name = name;
    std::string err;
    if (!buildPlanTree(words, loadHooks(), *tree, &err)) {
        reg_.log("Failed to compile " + name + ": " + err);
        return false;
    }
    if (!err.empty()) reg_.log(name + ": " + err);
    auto inst = std::make_unique<PlanInstance>();
    inst->obj = this;
    inst->name = name;
    inst->index = int16_t(plans_.size());
    inst->tree = tree;
    trees_.push_back(tree);
    plans_.push_back(std::move(inst));
    return true;
}

// CGameObject vf17 0x463430
void PlanObject::setActivePlan(int16_t idx) {
    if (active_) active_->teardown();
    active_ = (idx >= 0 && size_t(idx) < plans_.size()) ? plans_[size_t(idx)].get() : nullptr;
    if (!active_) return;
    trace(fmt("plan -> %s", active_->name.c_str()));
    reg_.emit(*this, TraceEvent::Kind::PlanActivated, idx);
    if (!active_->build()) reg_.log("Plan instantiation failed: " + active_->name);
}

void PlanObject::start(int16_t planIdx, uint32_t behIdx) {
    setActivePlan(planIdx);
    if (active_ && active_->built) active_->changeToBehavior(behIdx);
}

void PlanObject::clearHandlers() {
    std::fill(handlerSlot_.begin(), handlerSlot_.end(), uint8_t(0));
    handlers_.assign(1, nullptr);
}

void PlanObject::registerHandler(Interactor *it) {
    int m = it->msgId();
    if (m < 0 || size_t(m) >= handlerSlot_.size()) { reg_.warnOnce(fmt("badmsg%d", m), fmt("message id %d out of range", m)); return; }
    handlerSlot_[size_t(m)] = uint8_t(handlers_.size());
    handlers_.push_back(it);
}

Interactor *PlanObject::handlerFor(int msg) const {
    if (msg < 0 || size_t(msg) >= handlerSlot_.size()) return nullptr;
    uint8_t s = handlerSlot_[size_t(msg)];
    return s && s < handlers_.size() ? handlers_[s] : nullptr;
}

// CGameObject vf09 0x463930
void PlanObject::receiveMessage(int msg, uint32_t data, uint16_t path, int32_t sender, bool isNet) {
    Interactor *it = handlerFor(msg);
    if (!it) return;
    int role = host_.netRole();
    if (isNet || role != RoleRemote) {
        if (it->active != -1) {
            it->a28 = uint32_t(sender); // FUN_004ce380
            trace(fmt("msg %s data=%d from %d", reg_.symbol(Role::Message, msg), int32_t(data), sender));
            reg_.emit(*this, TraceEvent::Kind::Message, msg, int32_t(data), sender);
            dispatch(*it, data, path);
        }
    }
    if (host_.netConnected() && !isNet && role == RoleLocal)
        host_.netSend(packNetMessage(int16_t(msg), uint16_t(sender), path), data);
}

// CGameObject vf06 0x463890
bool PlanObject::sendMessage(int msg, uint32_t data, int32_t sender, int32_t isNet) {
    if (!handlerFor(msg)) return false;
    if (!scene_.queue(index(), msg, data, sender, isNet)) {
        reg_.log(fmt("Overflow of Sceneman MessagesToProcess (object %d)", index()));
        return false;
    }
    return true;
}

// FUN_004cdfe0
void PlanObject::fireCollision(Interactor &it, uint32_t data, uint16_t path, bool isNetReplay) {
    it.active = 0;
    int role = host_.netRole();
    bool run = isNetReplay ? role == RoleRemote : role != RoleRemote;
    if (!run) return;
    trace(fmt("collision %d/%d", it.cobMine(), it.cobOther()));
    reg_.emit(*this, TraceEvent::Kind::Collision, -1, it.cobMine(), it.cobOther());
    uint16_t p = path;
    it.planChange = -1;
    it.planChangeBeh = 0;
    try {
        if (it.enable) execute(it, it.enable->args, 0, it.enable->args.size(), data, p);
    } catch (const std::exception &e) {
        reg_.log(fmt("collision interactor: %s", e.what()));
    }
    if (it.nextBeh < kNoBehChange && it.allowBehChange && active_) {
        receiveMessage(reg_.ids().msgExitBehavior, 0, 0xFFFF, index(), false);
        if (active_) active_->changeToBehavior(it.nextBeh);
    }
    if (!isNetReplay && role != RoleRemote && host_.netConnected())
        host_.netSend(packNetCollision(it.collIndex, int16_t(it.a28), int16_t(it.a2c), p), data);
    if (active_) active_->rearmCountdown = 12;
    int16_t plan = it.planChange, beh = it.planChangeBeh;
    if (plan >= 0) {
        receiveMessage(reg_.ids().msgExitBehavior, 0, 0xFFFF, index(), false);
        setActivePlan(plan);
        if (active_) active_->changeToBehavior(uint32_t(int32_t(beh)));
    }
}

// Message interactor dispatch FUN_004ce3b0
void PlanObject::dispatch(Interactor &it, uint32_t data, uint16_t &path) {
    it.planChange = -1;
    it.planChangeBeh = 0;
    try {
        if (it.enable) execute(it, it.enable->args, 0, it.enable->args.size(), data, path);
    } catch (const std::exception &e) {
        reg_.log(fmt("message interactor %s: %s", reg_.symbol(Role::Message, it.msgId()), e.what()));
    }
    if (it.nextBeh < kNoBehChange && it.allowBehChange && active_) {
        receiveMessage(reg_.ids().msgExitBehavior, 0, 0xFFFF, index(), false);
        if (active_) active_->changeToBehavior(it.nextBeh);
    }
    if (active_) active_->rearmCountdown = 12;
    int16_t plan = it.planChange, beh = it.planChangeBeh;
    if (plan >= 0) {
        receiveMessage(reg_.ids().msgExitBehavior, 0, 0xFFFF, index(), false);
        setActivePlan(plan);
        if (active_) active_->changeToBehavior(uint32_t(int32_t(beh)));
    }
}

// Interactor_ExecuteActions 0x4cd7c0
void PlanObject::execute(Interactor &it, const std::vector<Arg> &list, size_t first, size_t count,
                         uint32_t msgData, uint16_t &path) {
    for (size_t k = first; k < first + count && k < list.size(); ++k) {
        const Node *np = list[k].node.get();
        if (!np) throw PlanError("Invalid Instruction object in Interactor (leaf)");
        const Node &n = *np;
        uint32_t c = n.cls();
        Behavior *cur = active_ ? active_->current : nullptr;
        if (c < 0x00160001) {
            if (c == 0x00160000) {
                if (n.variant() == 0) {
                    it.planChange = int16_t(n.leaf(0));
                    it.planChangeBeh = int16_t(n.leaf(1));
                    trace(fmt("CHANGE_OF_PLAN %d,%d", it.planChange, it.planChangeBeh));
                    reg_.emit(*this, TraceEvent::Kind::PlanChange, -1, it.planChange, it.planChangeBeh, &n);
                }
            } else if (c == 0x00070000) {
                if (n.variant() == 2) {
                    it.nextBeh = n.leaf(0);
                    trace(fmt("CHANGE_TO_BEHAVIOR 0x%x", it.nextBeh));
                    reg_.emit(*this, TraceEvent::Kind::ChangeRequest, -1, it.nextBeh, 0, &n);
                } else if (n.variant() == 3) {
                    reg_.callBehaviorParam(int16_t(n.leaf(0)), *this, n, cur);
                }
            } else {
                throw PlanError("Invalid Instruction object in Interactor");
            }
        } else if (c < 0x00180001) {
            if (c == 0x00180000) {
                ActionCtx ctx{*this, &it, cur, n, &path, msgData};
                int id = int16_t(n.leaf(0));
                switch (n.variant()) {
                case 0: reg_.callAction(id, ctx); break;
                case 1: reg_.callInteractorParam(id, *this, n, cur, &it); break;
                case 2: if (host_.netRole() != RoleRemote) reg_.callAction(id, ctx); break;
                case 3: if (host_.netRole() != RoleLocal) reg_.callAction(id, ctx); break;
                default: break;
                }
            } else if (c == 0x00170000) {
                uint16_t idx;
                if (path & 0x8000) {
                    ActionCtx ctx{*this, &it, cur, n, &path, msgData};
                    idx = reg_.callDecision(int16_t(n.leaf(0)), ctx);
                    path = pathRecord(path, idx);
                } else {
                    idx = pathReplay(path);
                    trace(fmt("DECIDE %s replay -> %u", reg_.symbol(Role::Decision, int16_t(n.leaf(0))), idx));
                    reg_.emit(*this, TraceEvent::Kind::Decision, int16_t(n.leaf(0)), idx, 1, &n);
                }
                int16_t si = int16_t(idx);
                if (si >= 0 && int(si) < n.count() - 1) execute(it, n.args, size_t(1 + si), 1, msgData, path);
            } else {
                throw PlanError("Invalid Instruction object in Interactor");
            }
        } else if (c == 0x001A0000) {
            if (n.id == ADD_AGENDA_ITEM) {
                auto item = reg_.createAgenda(int16_t(n.leaf(0)));
                if (n.count() == 1) item->initDefault(*this, cur);
                else item->init(*this, cur, n, msgData);
                item->owner = this;
                trace(fmt("ADD_AGENDA_ITEM %s", reg_.symbol(Role::Agenda, item->id)));
                reg_.emit(*this, TraceEvent::Kind::AgendaAdd, item->id, 0, 0, &n);
                agenda_.insert(item.release());
            } else if (n.id == SET_TIMEOUTMSG) {
                auto item = reg_.createAgenda(reg_.ids().agdTimerActivity);
                item->init(*this, cur, n, msgData);
                item->owner = this;
                trace(fmt("SET_TIMEOUTMSG %d %s", int32_t(n.leaf(0)), reg_.symbol(Role::Message, int32_t(n.leaf(1)))));
                reg_.emit(*this, TraceEvent::Kind::TimerSet, int32_t(n.leaf(1)), int32_t(n.leaf(0)),
                          n.count() > 2 ? int64_t(int32_t(n.leaf(2))) : 0, &n);
                agenda_.insert(item.release());
            } else if (n.id == REMOVE_AGENDA_ITEM) {
                trace(fmt("REMOVE_AGENDA_ITEM %s", reg_.symbol(Role::Agenda, int32_t(n.leaf(0)))));
                reg_.emit(*this, TraceEvent::Kind::AgendaRemove, int32_t(n.leaf(0)), 0, 0, &n);
                agenda_.removeById(int32_t(n.leaf(0)));
            }
        } else if (c == BLOCK) {
            execute(it, n.args, 0, n.args.size(), msgData, path);
        } else {
            throw PlanError("Invalid Instruction object in Interactor");
        }
    }
}

// FUN_00472010
void PlanObject::netEnqueue(uint32_t code, uint32_t data) {
    while (netRing_.size() + 1 > 150) netRing_.pop_front();
    netRing_.emplace_back(code, data);
}

// CSceneManager_ProcessMessages, first part (per object, net ring)
void PlanObject::processNetQueue() {
    while (!netRing_.empty()) {
        auto [code, data] = netRing_.front();
        netRing_.pop_front();
        if (code == 0xFFFFFFFEu) {
            setActivePlan(int16_t(data >> 16));
            if (active_) active_->changeToBehavior(uint32_t(int32_t(int16_t(data))));
        } else if (code == 0xFFFFFFFDu) {
            if (active_) active_->changeToBehavior(uint32_t(int32_t(int16_t(data))));
        } else if (code & 0x80000000u) {
            int16_t idx = int16_t((code >> 24) & 0x7F), mine = int16_t((code >> 16) & 0xFF);
            int16_t other = int16_t((code >> 8) & 0xFF);
            uint16_t p = uint16_t(code & 0xFF);
            Interactor *it = active_ ? active_->collisionByIndex(idx) : nullptr;
            if (it && it->cobMine() == mine && uint16_t(it->cobOther()) == uint16_t(other))
                fireCollision(*it, data, p, true);
        } else {
            receiveMessage(int(code >> 22), data, uint16_t(code & 0x3FFF), int32_t((code >> 14) & 0xFF), true);
        }
    }
}

// =============================================================================================
// PlanScene

void PlanScene::add(PlanObject *o) {
    if (std::find(objects_.begin(), objects_.end(), o) == objects_.end()) objects_.push_back(o);
}
void PlanScene::remove(PlanObject *o) {
    auto i = std::find(objects_.begin(), objects_.end(), o);
    if (i != objects_.end()) objects_.erase(i);
}
PlanObject *PlanScene::object(int16_t idx) const {
    for (PlanObject *o : objects_)
        if (o->index() == idx) return o;
    return nullptr;
}

// FUN_004de830
bool PlanScene::queue(int16_t recipient, int32_t msg, uint32_t data, int32_t sender, int32_t isNet) {
    if (queue_.size() >= 0x400) return false;
    queue_.push_back({recipient, msg, data, sender, isNet});
    return true;
}

// CSceneManager_PlayEvent 0x4ddfc0 (the 0x5b-suppression flag DAT_0051107c is not modelled)
void PlanScene::broadcast(int32_t msg, uint32_t data, int16_t sender) {
    for (PlanObject *o : std::vector<PlanObject *>(objects_))
        if (o->index() != sender) o->sendMessage(msg, data, sender, 0); // vf07
    if (onBroadcastHook) onBroadcastHook(msg);
}

void PlanScene::tick() {
    for (PlanObject *o : std::vector<PlanObject *>(objects_)) {
        int role = o->host().netRole();
        if (role > 0) o->agenda().tick(*o, role == RoleRemote);
        if (o->active_ && o->active_->built) o->active_->tick();
    }
}

void PlanScene::processMessages() {
    for (PlanObject *o : std::vector<PlanObject *>(objects_)) o->processNetQueue();
    // Messages queued while processing are delivered in the same pass (count is re-read).
    for (size_t i = 0; i < queue_.size(); ++i) {
        Queued q = queue_[i];
        if (PlanObject *o = object(q.recipient)) o->receiveMessage(q.msg, q.data, 0xFFFF, q.sender, q.isNet != 0);
    }
}

void PlanScene::endFrame() {
    if (int16_t(queue_.size()) > maxQueued_) maxQueued_ = int16_t(queue_.size());
    queue_.clear();
    for (PlanObject *o : objects_) {
        o->agenda_.collect();
        o->deadBehaviors_.clear();
        o->deadInteractors_.clear();
    }
}

} // namespace adlib
