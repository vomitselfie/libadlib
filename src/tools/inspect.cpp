// adlib-tools: per-plan statistics, structural lint, id checks, reachability, structural diff.
//
// Lint rules = the conventional statement shapes of docs/research/grammar.md §4/§5.2 (what all 32
// shipped plans follow) plus the runtime's acceptance rules (Behavior::setup throws on other
// statement classes in a declaration, PlanObject::execute on non-actions). Errors are things the
// runtime rejects or misreads; warnings are departures from the shipped conventions. These are
// the rules intended for `adlibc --strict`; lintPlan() is the shared implementation.
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <sstream>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

using Sev = Issue::Severity;

const char *kw(uint32_t id) {
    const char *n = nodeIdName(id);
    return n ? n : "?";
}

bool isInteractor(const Node &n) { return n.cls() == 0x00050000 || n.cls() == 0x00030000; }
bool isAction(const Node &n) {
    switch (n.id) {
    case DO_ACTION: case DO_LOCAL_ACTION: case DO_NET_ACTION: case DECIDE_BY_AMONG: case DECIDE_BY_WITH_AMONG:
    case CHANGE_TO_BEHAVIOR: case CHANGE_OF_PLAN: case ADD_AGENDA_ITEM: case REMOVE_AGENDA_ITEM:
    case SET_TIMEOUTMSG: case SET_BEHAVIOR_PARAM: case SET_INTERACTOR_PARAM: case SET_DEBUG: case SET_TRACE:
    case BLOCK:
        return true;
    default: return false;
    }
}

struct Linter {
    const PlanTree &tree;
    const SymbolTable *syms;
    std::vector<Issue> out;
    size_t nbeh = 0;

    void add(Sev s, const std::string &p, const std::string &m) { out.push_back({s, p, m}); }

    std::string label(const Node &n, size_t idx) const {
        std::string s = kw(n.id);
        if (n.id == DECLARE_BEHAVIOR && n.args.size() > 1 && n.args[1].kind == Arg::Kind::String) s += "\"" + n.args[1].str + "\"";
        return std::to_string(idx) + ":" + s;
    }

    void leaves(const Node &n, const std::string &p, size_t from, bool allowStr = true) {
        for (size_t i = from; i < n.args.size(); ++i) {
            if (n.args[i].isNode()) add(Sev::Warning, p, std::string("child ") + std::to_string(i) + " is a statement where a value is expected");
            else if (!allowStr && n.args[i].kind == Arg::Kind::String) add(Sev::Warning, p, "child " + std::to_string(i) + " is a string where a value is expected");
        }
    }
    bool leafAt(const Node &n, size_t i) const { return i < n.args.size() && !n.args[i].isNode(); }
    bool needLeaf(const Node &n, size_t i, const std::string &p, const char *what) {
        if (leafAt(n, i)) return true;
        add(Sev::Error, p, std::string(kw(n.id)) + ": missing " + what + " (child " + std::to_string(i) + ")");
        return false;
    }
    void arity(const Node &n, const std::string &p, size_t lo, size_t hi, Sev s = Sev::Warning) {
        size_t c = n.args.size();
        if (c < lo || c > hi) {
            std::string want = lo == hi ? std::to_string(lo) : hi == SIZE_MAX ? ">= " + std::to_string(lo) : std::to_string(lo) + ".." + std::to_string(hi);
            add(s, p, std::string(kw(n.id)) + " has " + std::to_string(c) + " children, conventional " + want);
        }
    }

    void interactor(const Node &n, const std::string &p) {
        bool msg = n.cls() == 0x00050000;
        size_t first = msg ? 1 : 2;
        needLeaf(n, 0, p, msg ? "message id" : "own collision object");
        if (!msg) needLeaf(n, 1, p, "other collision object");
        if (n.id == SET_NET_MESSAGE_INTERACTOR)
            add(Sev::Info, p, "0x00050002 is SET_NET_MESSAGE_INTERACTOR (also what SET_NET_COLLISION_INTERACTOR compiles to)");
        int seen = 0; // 1 = CHANGE seen, 2 = Enable seen
        for (size_t i = first; i < n.args.size(); ++i) {
            const Node *c = n.args[i].node.get();
            std::string cp = p + "/" + std::to_string(i);
            if (!c) { add(Sev::Error, cp, "interactor option is a value (the runtime throws)"); continue; }
            cp = p + "/" + label(*c, i);
            switch (c->id) {
            case CHANGE_TO_BEHAVIOR: case CHANGE_OF_PLAN:
                if (seen >= 1) add(Sev::Warning, cp, seen == 2 ? "CHANGE option after Enable (shipped order: CHANGE, Enable)" : "second CHANGE option");
                seen = std::max(seen, 1);
                stmt(*c, cp);
                break;
            case ENABLE:
                if (seen == 2) add(Sev::Warning, cp, "second Enable list");
                seen = 2;
                actionList(*c, cp, 0);
                break;
            case SET_INTERACTOR_PARAM: case SET_DEBUG: case SET_TRACE:
                stmt(*c, cp);
                break;
            default:
                add(Sev::Error, cp, std::string(kw(c->id)) + " is not an interactor option (runtime throws)");
            }
        }
        if (n.args.size() <= first) add(Sev::Warning, p, "interactor has neither CHANGE_TO_BEHAVIOR nor Enable");
    }

    void actionList(const Node &n, const std::string &p, size_t from) {
        for (size_t i = from; i < n.args.size(); ++i) {
            const Node *c = n.args[i].node.get();
            if (!c) { add(Sev::Error, p + "/" + std::to_string(i), "value in an action list (runtime throws)"); continue; }
            std::string cp = p + "/" + label(*c, i);
            if (!isAction(*c)) add(Sev::Error, cp, std::string(kw(c->id)) + " is not an action");
            stmt(*c, cp);
        }
    }

    void stmt(const Node &n, const std::string &p) {
        if (n.rawCount & 0x8000) add(Sev::Warning, p, "count has bit 15 set (never in shipped data; the loader masks it)");
        switch (n.id) {
        case DO_ACTION: case DO_LOCAL_ACTION: case DO_NET_ACTION:
            arity(n, p, 1, SIZE_MAX, Sev::Error);
            needLeaf(n, 0, p, "function id");
            leaves(n, p, 1);
            break;
        case DECIDE_BY_AMONG:
            needLeaf(n, 0, p, "decision function id");
            arity(n, p, 2, SIZE_MAX);
            actionList(n, p, 1);
            break;
        case DECIDE_BY_WITH_AMONG:
            needLeaf(n, 0, p, "decision function id");
            if (n.args.size() < 2 || !n.args[1].node || n.args[1].node->id != DATA_BLOCK)
                add(Sev::Error, p, "DECIDE_BY_WITH_AMONG: child 1 must be a DataBlock");
            else leaves(*n.args[1].node, p + "/1:DataBlock", 0);
            arity(n, p, 3, SIZE_MAX);
            actionList(n, p, 2);
            break;
        case CHANGE_TO_BEHAVIOR: {
            arity(n, p, 1, 1);
            if (!needLeaf(n, 0, p, "behaviour")) break;
            uint32_t v = n.leaf(0);
            if (v != kNoBehChange && v != kPreviousBeh && v >= nbeh)
                add(Sev::Warning, p, "behaviour index " + std::to_string(v) + " >= " + std::to_string(nbeh) +
                                         " behaviours (the runtime wraps modulo the count)");
            break;
        }
        case CHANGE_OF_PLAN:
            arity(n, p, 2, 2);
            if (n.args.empty() || n.args[0].isNode() || (n.args[0].kind != Arg::Kind::String && n.args[0].str.empty()))
                add(Sev::Error, p, "CHANGE_OF_PLAN: child 0 must be the plan name string");
            needLeaf(n, 1, p, "behaviour index");
            break;
        case SET_BEHAVIOR_PARAM: case SET_INTERACTOR_PARAM:
            arity(n, p, 1, SIZE_MAX, Sev::Error);
            needLeaf(n, 0, p, "parameter id");
            leaves(n, p, 1);
            break;
        case ADD_AGENDA_ITEM:
            arity(n, p, 1, SIZE_MAX, Sev::Error);
            needLeaf(n, 0, p, "agenda id");
            leaves(n, p, 1);
            break;
        case REMOVE_AGENDA_ITEM:
            arity(n, p, 1, 1);
            needLeaf(n, 0, p, "agenda id");
            break;
        case SET_TIMEOUTMSG:
            arity(n, p, 3, 4, n.args.size() < 3 ? Sev::Error : Sev::Warning); // recipient read unconditionally
            leaves(n, p, 0);
            break;
        case SET_DEBUG: case SET_TRACE:
            add(Sev::Info, p, std::string(kw(n.id)) + " is ignored by the runtime");
            break;
        case BLOCK:
            if (n.args.empty()) add(Sev::Warning, p, "empty Block");
            actionList(n, p, 0);
            break;
        case DATA_BLOCK:
            leaves(n, p, 0);
            break;
        default:
            break;
        }
    }

    void behaviour(const Node &n, const std::string &p) {
        arity(n, p, 2, SIZE_MAX, Sev::Error);
        needLeaf(n, 0, p, "behaviour type");
        if (n.args.size() < 2 || n.args[1].kind != Arg::Kind::String) add(Sev::Error, p, "DECLARE_BEHAVIOR: child 1 must be the name string");
        bool any = false;
        for (size_t i = 2; i < n.args.size(); ++i) {
            const Node *c = n.args[i].node.get();
            if (!c) { add(Sev::Error, p + "/" + std::to_string(i), "value in a behaviour declaration (runtime throws)"); continue; }
            std::string cp = p + "/" + label(*c, i);
            if (isInteractor(*c)) { interactor(*c, cp); any = true; continue; }
            switch (c->cls()) {
            case 0x00070000:
                if (c->id != SET_BEHAVIOR_PARAM) add(Sev::Warning, cp, std::string(kw(c->id)) + " in a declaration is ignored by the runtime");
                stmt(*c, cp);
                break;
            case 0x00060000: stmt(*c, cp); break;
            case 0x00180000: case 0x001A0000:
                add(Sev::Warning, cp, std::string(kw(c->id)) + " directly in a declaration (runs at behaviour setup; not used in shipped plans)");
                stmt(*c, cp);
                break;
            default:
                add(Sev::Error, cp, std::string(kw(c->id)) + " is not allowed in a behaviour declaration (runtime throws)");
            }
        }
        if (!any) add(Sev::Info, p, "no interactors: entering it falls through to the next behaviour");
    }

    void run() {
        for (auto &t : tree.top) nbeh += t->id == DECLARE_BEHAVIOR;
        bool sawEnd = false, sawBeh = false;
        for (size_t i = 0; i < tree.top.size(); ++i) {
            const Node &n = *tree.top[i];
            std::string p = "/" + label(n, i);
            switch (n.id) {
            case GLOBAL_INTERACTORS:
                if (sawBeh) add(Sev::Warning, p, "GLOBAL_INTERACTORS after a behaviour (shipped plans put it first)");
                for (size_t k = 0; k < n.args.size(); ++k) {
                    const Node *c = n.args[k].node.get();
                    if (!c || !isInteractor(*c)) add(Sev::Error, p + "/" + std::to_string(k), "GLOBAL_INTERACTORS child is not an interactor");
                    else interactor(*c, p + "/" + label(*c, k));
                }
                break;
            case DECLARE_BEHAVIOR: sawBeh = true; behaviour(n, p); break;
            case DECLARE_LOCAL_REALS: leaves(n, p, 0); break;
            case END_PLAN: sawEnd = true; break;
            default: add(Sev::Error, p, std::string(kw(n.id)) + " at top level"); stmt(n, p);
            }
        }
        if (!sawEnd) add(Sev::Error, "/", "no END_PLAN");
        if (!nbeh) add(Sev::Error, "/", "no behaviours");
    }
};

} // namespace

std::vector<Issue> lintPlan(const PlanTree &tree, const SymbolTable *syms, const std::vector<uint32_t> *words) {
    Linter l{tree, syms, {}};
    l.run();
    if (words) {
        // dwords after END_PLAN (counted by the header but never read)
        for (size_t i = 0; i + 1 < words->size(); ++i)
            if ((*words)[i] == kTokNode && (*words)[i + 1] == END_PLAN) {
                if (i + 2 < words->size())
                    l.add(Sev::Warning, "/", std::to_string(words->size() - i - 2) + " dwords after END_PLAN");
                break;
            }
    }
    return l.out;
}

std::vector<Issue> checkIds(const PlanTree &tree, const SymbolTable &syms) {
    std::vector<Issue> out;
    const Vocabulary &v = syms.vocab();
    auto check = [&](Role r, uint32_t raw, const std::string &p, const char *what) {
        if ((raw >> 16) == kTagPronoun) {
            if (!v.name(Role::Pronoun, int(raw & 0xFFFF)))
                out.push_back({Sev::Error, p, "unknown pronoun " + std::to_string(raw & 0xFFFF)});
            return;
        }
        if (raw == kBroadcast || (raw >> 16) == 0xABCD) return;
        const Namespace *ns = v.role(r);
        if (!ns) return; // role not in this vocabulary
        int id = int32_t(raw);
        const char *n = ns->name(id);
        if (!n || !*n) out.push_back({Sev::Error, p, std::string("unknown ") + what + " id " + std::to_string(id)});
        else if (id == ns->size() - 1 && std::string(n).find("NUM") != std::string::npos)
            out.push_back({Sev::Warning, p, std::string(what) + " id " + std::to_string(id) + " is the enum sentinel " + n});
    };
    std::function<void(const Node &, const std::string &)> walk = [&](const Node &n, const std::string &p) {
        std::string q = p + "/" + kw(n.id);
        auto L = [&](size_t i) { return i < n.args.size() && !n.args[i].isNode() && n.args[i].kind == Arg::Kind::Leaf; };
        switch (n.cls()) {
        case 0x00050000: if (L(0)) check(Role::Message, n.leaf(0), q, "message"); break;
        case 0x00030000:
            if (L(0)) check(Role::CollisionObject, n.leaf(0), q, "collision object");
            if (L(1)) check(Role::CollisionObject, n.leaf(1), q, "collision object");
            break;
        case 0x00170000: if (L(0)) check(Role::Decision, n.leaf(0), q, "decision"); break;
        case 0x00180000:
            if (L(0)) check(n.id == SET_INTERACTOR_PARAM ? Role::InteractorParam : Role::Action, n.leaf(0), q,
                            n.id == SET_INTERACTOR_PARAM ? "interactor param" : "action");
            break;
        case 0x001A0000:
            if (n.id == SET_TIMEOUTMSG) { if (L(1)) check(Role::Message, n.leaf(1), q, "message"); }
            else if (L(0)) check(Role::Agenda, n.leaf(0), q, "agenda");
            break;
        case 0x00070000:
            if (n.id == DECLARE_BEHAVIOR && L(0)) check(Role::Behavior, n.leaf(0), q, "behaviour type");
            if (n.id == SET_BEHAVIOR_PARAM && L(0)) check(Role::BehaviorParam, n.leaf(0), q, "behaviour param");
            break;
        default: break;
        }
        for (size_t i = 0; i < n.args.size(); ++i) {
            if (n.args[i].node) walk(*n.args[i].node, q + "[" + std::to_string(i) + "]");
            else if ((n.args[i].u >> 16) == kTagPronoun && !v.name(Role::Pronoun, int(n.args[i].u & 0xFFFF)))
                out.push_back({Sev::Error, q, "unknown pronoun " + std::to_string(n.args[i].u & 0xFFFF)});
        }
    };
    for (size_t i = 0; i < tree.top.size(); ++i) walk(*tree.top[i], "/" + std::to_string(i));
    return out;
}

std::vector<int> unreachableBehaviours(const PlanModel &m, const std::vector<int> &extraRoots) {
    size_t n = m.behaviours.size();
    std::vector<char> seen(n, 0);
    std::deque<int> q;
    auto push = [&](int b) { if (b >= 0 && size_t(b) < n && !seen[size_t(b)]) { seen[size_t(b)] = 1; q.push_back(b); } };
    push(0);
    for (int r : extraRoots) push(r);
    // global interactors fire in any behaviour: their targets are reachable once anything is
    bool prev = false;
    for (auto &t : m.transitions) {
        if (t.from < 0 && t.kind == Transition::Kind::Behaviour) push(t.to);
        prev |= t.kind == Transition::Kind::Previous;
    }
    while (!q.empty()) {
        int b = q.front();
        q.pop_front();
        for (auto &t : m.transitions)
            if (t.from == b && t.kind == Transition::Kind::Behaviour) push(t.to);
        if (m.behaviours[size_t(b)].fallthrough) push(b + 1);
    }
    std::vector<int> out;
    for (size_t i = 0; i < n; ++i)
        if (!seen[i]) out.push_back(int(i));
    (void)prev;
    return out;
}

namespace {
const char *sevName(Sev s) { return s == Sev::Error ? "error" : s == Sev::Warning ? "warning" : "info"; }

void countNodes(const Node &n, std::map<std::string, int> &kwc, std::map<std::string, std::map<std::string, int>> &use,
                const SymbolTable *syms) {
    kwc[kw(n.id)]++;
    auto nm = [&](Role r, uint32_t v) { return syms ? syms->displayName(r, int32_t(v)) : std::to_string(int32_t(v)); };
    if (!n.args.empty() && !n.args[0].isNode()) {
        switch (n.cls()) {
        case 0x00050000: use["messages handled"][nm(Role::Message, n.leaf(0))]++; break;
        case 0x00170000: use["decisions"][nm(Role::Decision, n.leaf(0))]++; break;
        case 0x00180000:
            if (n.id == SET_INTERACTOR_PARAM) use["interactor params"][nm(Role::InteractorParam, n.leaf(0))]++;
            else use["actions"][nm(Role::Action, n.leaf(0))]++;
            break;
        case 0x001A0000:
            if (n.id == SET_TIMEOUTMSG) { if (n.args.size() > 1) use["timer messages"][nm(Role::Message, n.leaf(1))]++; }
            else use["agenda items"][nm(Role::Agenda, n.leaf(0))]++;
            break;
        case 0x00070000:
            if (n.id == SET_BEHAVIOR_PARAM) use["behaviour params"][nm(Role::BehaviorParam, n.leaf(0))]++;
            if (n.id == DECLARE_BEHAVIOR) use["behaviour types"][nm(Role::Behavior, n.leaf(0))]++;
            break;
        default: break;
        }
    }
    if (n.cls() == 0x00180000 && n.id != SET_INTERACTOR_PARAM && syms && !n.args.empty()) {
        std::string a = syms->displayName(Role::Action, int32_t(n.leaf(0)));
        if (syms->isMessageArg(a, 2) && n.args.size() > 2) use["messages sent"][nm(Role::Message, n.leaf(2))]++;
    }
    for (auto &a : n.args) {
        if (a.node) countNodes(*a.node, kwc, use, syms);
        else if ((a.u >> 16) == kTagPronoun) use["pronouns"][syms ? syms->displayName(Role::Pronoun, int(a.u & 0xFFFF)) : std::to_string(a.u & 0xFFFF)]++;
    }
}
} // namespace

std::string inspectReport(const PlanTree &tree, const PlanModel &m, const SymbolTable *syms,
                          const std::vector<uint32_t> *words, const std::vector<int> &extraRoots, int *errors) {
    std::ostringstream s;
    s << "plan " << m.name;
    if (words) s << "  (" << words->size() << " dwords)";
    s << "\n\nbehaviours (" << m.behaviours.size() << "):\n";
    std::map<int, int> outDeg;
    for (auto &t : m.transitions) outDeg[t.from]++;
    char buf[256];
    for (auto &b : m.behaviours) {
        std::snprintf(buf, sizeof buf, "  #%-3d %-24s %-22s %2zu interactors %2d params %3d transitions%s\n", b.index,
                      b.name.c_str(), b.typeName.c_str(), b.triggers.size(), b.params, outDeg[b.index],
                      b.fallthrough ? "  (falls through)" : "");
        s << buf;
    }
    s << "global interactors (" << m.globalTriggers.size() << ")";
    for (size_t i = 0; i < m.globalTriggers.size(); ++i) s << (i ? ", " : ": ") << m.globalTriggers[i];
    s << "\n";
    size_t nInt = m.globalTriggers.size();
    for (auto &b : m.behaviours) nInt += b.triggers.size();
    s << "interactors total: " << nInt << ", transitions: " << m.transitions.size() << ", timers: " << m.timers.size() << "\n";
    std::set<std::string> plans;
    for (auto &t : m.transitions)
        if (t.kind == Transition::Kind::Plan) plans.insert(t.plan);
    if (!plans.empty()) {
        s << "CHANGE_OF_PLAN targets:";
        for (auto &p : plans) s << " " << p;
        s << "\n";
    }

    std::map<std::string, int> kwc;
    std::map<std::string, std::map<std::string, int>> use;
    for (auto &t : tree.top) countNodes(*t, kwc, use, syms);
    s << "\nnode counts:\n";
    for (auto &[k, c] : kwc) {
        std::snprintf(buf, sizeof buf, "  %-30s %5d\n", k.c_str(), c);
        s << buf;
    }
    s << "\nsymbol usage:\n";
    for (auto &[cat, mm] : use) {
        std::vector<std::pair<int, std::string>> v;
        for (auto &[k, c] : mm) v.push_back({-c, k});
        std::sort(v.begin(), v.end());
        s << "  " << cat << " (" << mm.size() << " distinct):";
        for (auto &[c, k] : v) s << " " << k << (c < -1 ? "x" + std::to_string(-c) : "");
        s << "\n";
    }

    std::vector<Issue> issues = lintPlan(tree, syms, words);
    if (syms && !syms->empty()) {
        auto ids = checkIds(tree, *syms);
        issues.insert(issues.end(), ids.begin(), ids.end());
    }
    for (int b : unreachableBehaviours(m, extraRoots))
        issues.push_back({Sev::Warning, "/\"" + m.behaviours[size_t(b)].name + "\"",
                          "behaviour #" + std::to_string(b) + " is not entered from #0 by any CHANGE_TO_BEHAVIOR/fallthrough" +
                              (extraRoots.empty() ? "" : " or another plan's CHANGE_OF_PLAN") + " (only a host start can enter it)"});
    int ne = 0, nw = 0, ni = 0;
    for (auto &i : issues) (i.severity == Sev::Error ? ne : i.severity == Sev::Warning ? nw : ni)++;
    s << "\nissues: " << ne << " errors, " << nw << " warnings, " << ni << " info\n";
    for (auto &i : issues) s << "  " << sevName(i.severity) << ": " << i.path << ": " << i.message << "\n";
    if (errors) *errors = ne;
    return s.str();
}

// =============================================================================================
// compare
// =============================================================================================
namespace {

struct Differ {
    const SymbolTable *syms;
    std::ostringstream out;
    int n = 0;

    std::string val(const Arg &a) const {
        if (a.node) return std::string(kw(a.node->id)) + "(" + std::to_string(a.node->args.size()) + ")";
        if (a.kind == Arg::Kind::String) return "\"" + a.str + "\"";
        if (!a.str.empty()) return "\"" + a.str + "\"";
        char b[32];
        std::snprintf(b, sizeof b, "%d (0x%x)", a.i(), a.u);
        return b;
    }
    std::string sig(const Arg &a) const {
        if (!a.node) return "L" + val(a);
        std::string s = kw(a.node->id);
        if (!a.node->args.empty() && !a.node->args[0].node) s += ":" + val(a.node->args[0]);
        if (a.node->id == DECLARE_BEHAVIOR && a.node->args.size() > 1) s += ":" + val(a.node->args[1]);
        return s;
    }
    std::string seg(const Arg &a, size_t i) const {
        if (!a.node) return std::to_string(i);
        std::string s = std::to_string(i) + ":" + kw(a.node->id);
        const Node &n = *a.node;
        if (n.id == DECLARE_BEHAVIOR && n.args.size() > 1) s += "\"" + n.args[1].str + "\"";
        else if (!n.args.empty() && !n.args[0].node && syms) {
            uint32_t v = n.leaf(0);
            switch (n.cls()) {
            case 0x00050000: s += "(" + syms->displayName(Role::Message, int32_t(v)) + ")"; break;
            case 0x00170000: s += "(" + syms->displayName(Role::Decision, int32_t(v)) + ")"; break;
            case 0x00180000: if (n.id != SET_INTERACTOR_PARAM) s += "(" + syms->displayName(Role::Action, int32_t(v)) + ")"; break;
            default: break;
            }
        }
        return s;
    }
    // Symbolic name of a leaf from its position (parent statement, child index).
    std::string named(const Node *parent, size_t i, const Arg &a) const {
        std::string v = val(a);
        if (!syms || a.node || a.kind == Arg::Kind::String || !parent) return v;
        if ((a.u >> 16) == kTagPronoun) return v + " " + syms->displayName(Role::Pronoun, int(a.u & 0xFFFF));
        int x = a.i();
        const Node &n = *parent;
        std::string nm;
        auto sym = [&](Role r) { nm = syms->displayName(r, x); };
        switch (n.cls()) {
        case 0x00050000: if (i == 0) sym(Role::Message); break;
        case 0x00030000: if (i < 2) sym(Role::CollisionObject); break;
        case 0x00170000: if (i == 0) sym(Role::Decision); break;
        case 0x00180000:
            if (i == 0) sym(n.id == SET_INTERACTOR_PARAM ? Role::InteractorParam : Role::Action);
            else if (i == 2 && n.id != SET_INTERACTOR_PARAM) {
                std::string acf = syms->displayName(Role::Action, int32_t(n.leaf(0)));
                if (syms->isMessageArg(acf, int(i))) sym(Role::Message);
            }
            break;
        case 0x001A0000:
            if (n.id == SET_TIMEOUTMSG) { if (i == 1) sym(Role::Message); }
            else if (i == 0) sym(Role::Agenda);
            break;
        case 0x00070000:
            if (n.id == DECLARE_BEHAVIOR && i == 0) sym(Role::Behavior);
            if (n.id == SET_BEHAVIOR_PARAM && i == 0) sym(Role::BehaviorParam);
            break;
        default: break;
        }
        return nm.empty() || nm == std::to_string(x) ? v : v + " " + nm;
    }
    void diff(const std::string &p, const char *what, const std::string &a, const std::string &b) {
        ++n;
        out << p << ": " << what << "\n    - " << a << "\n    + " << b << "\n";
    }

    // LCS alignment of child lists by signature, then recurse into aligned pairs.
    void list(const std::vector<Arg> &A, const std::vector<Arg> &B, const std::string &p, const Node *pa = nullptr,
              const Node *pb = nullptr) {
        size_t na = A.size(), nb = B.size();
        std::vector<std::vector<int>> L(na + 1, std::vector<int>(nb + 1, 0));
        std::vector<std::string> sa(na), sb(nb);
        for (size_t i = 0; i < na; ++i) sa[i] = sig(A[i]);
        for (size_t j = 0; j < nb; ++j) sb[j] = sig(B[j]);
        for (size_t i = na; i-- > 0;)
            for (size_t j = nb; j-- > 0;)
                L[i][j] = sa[i] == sb[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
        size_t i = 0, j = 0;
        while (i < na || j < nb) {
            if (i < na && j < nb && sa[i] == sb[j]) {
                pair(A[i], B[j], p + "/" + seg(A[i], i), pa, pb, i, j);
                ++i, ++j;
            } else if (i < na && j < nb && L[i + 1][j + 1] == L[i][j] && A[i].isNode() == B[j].isNode() &&
                       (!A[i].node || A[i].node->id == B[j].node->id)) {
                // same shape, different key: changed in place
                pair(A[i], B[j], p + "/" + seg(A[i], i), pa, pb, i, j);
                ++i, ++j;
            } else if (j < nb && (i == na || L[i][j + 1] >= L[i + 1][j])) {
                ++n;
                out << p << "/" << seg(B[j], j) << ": added " << named(pb, j, B[j]) << "\n";
                ++j;
            } else {
                ++n;
                out << p << "/" << seg(A[i], i) << ": removed " << named(pa, i, A[i]) << "\n";
                ++i;
            }
        }
    }
    void pair(const Arg &a, const Arg &b, const std::string &p, const Node *pa = nullptr, const Node *pb = nullptr,
              size_t ia = 0, size_t ib = 0) {
        if (!a.node || !b.node) {
            if (a.node || b.node || a.u != b.u || a.str != b.str || a.kind != b.kind)
                diff(p, "value", named(pa, ia, a), named(pb, ib, b));
            return;
        }
        if (a.node->id != b.node->id) { diff(p, "statement", val(a), val(b)); return; }
        if (a.node->rawCount != b.node->rawCount && (a.node->rawCount & 0x8000) != (b.node->rawCount & 0x8000))
            diff(p, "count bit 15", std::to_string(a.node->rawCount), std::to_string(b.node->rawCount));
        list(a.node->args, b.node->args, p, a.node.get(), b.node.get());
    }
};

} // namespace

int comparePlans(const PlanTree &a, const PlanTree &b, const SymbolTable *syms, std::string &out) {
    Differ d{syms, {}, 0};
    std::vector<Arg> A, B;
    // Wrap the top-level lists as Args without copying (borrow via a fake parent).
    auto wrap = [](const std::vector<std::unique_ptr<Node>> &top, std::vector<Arg> &v) {
        for (auto &t : top) {
            Arg x;
            x.kind = Arg::Kind::Node;
            x.node.reset(const_cast<Node *>(t.get()));
            v.push_back(std::move(x));
        }
    };
    wrap(a.top, A);
    wrap(b.top, B);
    d.list(A, B, "");
    for (auto &x : A) x.node.release();
    for (auto &x : B) x.node.release();
    out = d.out.str();
    return d.n;
}

} // namespace adlib::tools
