// PBN writer, token dumper, symbolic plan dumper and the C++ plan builder.
#include <cstdio>
#include <fstream>

#include "adlib/pbn.h"
#include "adlib/vocabulary.h"

namespace adlib {

// =============================================================================================
// Encoding

namespace {
void encodeArg(const Arg &a, std::vector<uint32_t> &out) {
    if (a.kind == Arg::Kind::Node) {
        if (a.node) encodeNode(*a.node, out);
        return;
    }
    if (a.kind == Arg::Kind::String || !a.str.empty()) {
        out.push_back(kTokString);
        for (unsigned char c : a.str) out.push_back(c);
        out.push_back(0);
        return;
    }
    out.push_back(a.u);
}
} // namespace

void encodeNode(const Node &n, std::vector<uint32_t> &out) {
    out.push_back(kTokNode);
    out.push_back(n.id);
    if (n.id == END_PLAN) return;
    uint32_t cnt = uint32_t(n.args.size());
    out.push_back((n.rawCount & 0x7FFFu) == cnt ? n.rawCount : cnt);
    for (const Arg &a : n.args) encodeArg(a, out);
}

std::vector<uint32_t> encodePlan(const std::vector<std::unique_ptr<Node>> &top) {
    std::vector<uint32_t> w;
    bool ended = false;
    for (const auto &n : top) {
        if (!n) continue;
        encodeNode(*n, w);
        if (n->id == END_PLAN) { ended = true; break; }
    }
    if (!ended) { w.push_back(kTokNode); w.push_back(END_PLAN); }
    return w;
}

std::vector<uint32_t> encodePlan(const PlanTree &tree) { return encodePlan(tree.top); }

std::vector<uint8_t> pbnFileBytes(const std::vector<uint32_t> &words) {
    std::vector<uint8_t> b;
    b.reserve(4 + words.size() * 4);
    auto put = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
    };
    put(uint32_t(words.size()));
    for (uint32_t w : words) put(w);
    return b;
}

bool writePbnFile(const std::string &path, const std::vector<uint32_t> &words, std::string *err) {
    std::vector<uint8_t> b = pbnFileBytes(words);
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot create " + path; return false; }
    f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    if (!f) { if (err) *err = "write failed: " + path; return false; }
    return true;
}

bool readPbnFile(const std::string &path, std::vector<uint32_t> &words, std::string *err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return readPbnWords(b, words, err);
}

// =============================================================================================
// Dumpers

std::string dumpTokens(const std::vector<uint32_t> &w) {
    std::string s;
    char buf[96];
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t t = w[i];
        const char *what = "";
        std::string extra;
        if (t == kTokNode && i + 1 < w.size()) {
            const char *nm = nodeIdName(w[i + 1]);
            extra = std::string("NODE ") + (nm ? nm : "?");
            what = extra.c_str();
        } else if (t == kTokString) {
            what = "STRING";
        } else if ((t >> 16) == kTagPronoun) {
            what = "PRONOUN";
        } else if ((t >> 16) == kTagLocalReal) {
            what = "LOCAL_REAL";
        } else if ((t >> 16) == kTagNameRef) {
            what = "NAME_REF";
        }
        std::snprintf(buf, sizeof buf, "%6zu  %08x  %s\n", i, t, what);
        s += buf;
    }
    return s;
}

namespace {
struct PlanDumper {
    const Vocabulary *v;
    std::vector<std::string> behNames;
    std::string s;

    std::string sym(Role r, uint32_t val) const {
        char buf[48];
        if ((val >> 16) == kTagPronoun) return pron(val);
        const char *n = v ? v->name(r, int32_t(val)) : nullptr;
        if (n) return n;
        std::snprintf(buf, sizeof buf, "%d", int32_t(val));
        return buf;
    }
    std::string pron(uint32_t val) const {
        const char *n = v ? v->name(Role::Pronoun, int(val & 0xFFFF)) : nullptr;
        char buf[48];
        if (n) return n;
        std::snprintf(buf, sizeof buf, "PRN:%u", val & 0xFFFF);
        return buf;
    }
    std::string leaf(const Arg &a) const {
        char buf[64];
        if (a.kind == Arg::Kind::String) return "\"" + a.str + "\"";
        if (!a.str.empty()) return "\"" + a.str + "\"";
        switch (a.u) {
        case kBroadcast: return "_BROADCAST_";
        case kStraightAhead: return "_STRAIGHTAHEAD_";
        case kTemporarily: return "_TEMPORARILY_";
        case kMessageData: return "_MESSAGE_DATA_";
        case kNoBehChange: return "_NO_BEH_CHANGE_";
        case kPreviousBeh: return "_PREVIOUS_BEH_";
        default: break;
        }
        switch (a.tag()) {
        case kTagPronoun: return pron(a.u);
        case kTagLocalReal: std::snprintf(buf, sizeof buf, "LOCAL_REAL[%u]", a.u & 0xFFFF); return buf;
        case kTagNameRef: std::snprintf(buf, sizeof buf, "NAME_REF[%u]", a.u & 0xFFFF); return buf;
        default: break;
        }
        std::snprintf(buf, sizeof buf, "%d", a.i());
        return buf;
    }
    // Role of argument i of node n, or Role::Count when untyped.
    static Role argRole(const Node &n, size_t i) {
        switch (n.cls()) {
        case 0x00050000: return i == 0 ? Role::Message : Role::Count;
        case 0x00030000: return i < 2 ? Role::CollisionObject : Role::Count;
        case 0x00070000:
            if (n.id == DECLARE_BEHAVIOR && i == 0) return Role::Behavior;
            if (n.id == SET_BEHAVIOR_PARAM && i == 0) return Role::BehaviorParam;
            return Role::Count;
        case 0x00180000:
            if (i != 0) return Role::Count;
            return n.id == SET_INTERACTOR_PARAM ? Role::InteractorParam : Role::Action;
        case 0x00170000: return i == 0 ? Role::Decision : Role::Count;
        case 0x001A0000:
            if (n.id == SET_TIMEOUTMSG) return i == 1 ? Role::Message : Role::Count;
            return i == 0 ? Role::Agenda : Role::Count;
        default: return Role::Count;
        }
    }
    void node(const Node &n, int depth) {
        s.append(size_t(depth) * 2, ' ');
        const char *nm = nodeIdName(n.id);
        char buf[64];
        if (nm) s += nm; else { std::snprintf(buf, sizeof buf, "NODE_%08x", n.id); s += buf; }
        if (n.id == END_PLAN) { s += '\n'; return; }
        // Leaves inline, child statements indented below.
        s += '(';
        bool first = true;
        for (size_t i = 0; i < n.args.size(); ++i) {
            const Arg &a = n.args[i];
            if (a.isNode()) continue;
            if (!first) s += ", ";
            first = false;
            Role r = argRole(n, i);
            std::string txt = r != Role::Count && a.kind == Arg::Kind::Leaf && a.str.empty() ? sym(r, a.u) : leaf(a);
            if (n.id == CHANGE_TO_BEHAVIOR && i == 0 && a.u < behNames.size()) txt += " /*" + behNames[a.u] + "*/";
            s += txt;
        }
        s += ")\n";
        for (const Arg &a : n.args)
            if (a.isNode() && a.node) node(*a.node, depth + 1);
    }
};
} // namespace

std::string dumpPlan(const PlanTree &tree, const Vocabulary *vocab) {
    PlanDumper d{vocab, {}, {}};
    for (const auto &n : tree.top)
        if (n && n->id == DECLARE_BEHAVIOR)
            d.behNames.push_back(n->args.size() > 1 && n->args[1].kind == Arg::Kind::String ? n->args[1].str : "?");
    d.s = "// plan " + tree.name + "\n";
    for (const auto &n : tree.top)
        if (n) d.node(*n, 0);
    return d.s;
}

// =============================================================================================
// Builder

namespace build {

Arg lit(uint32_t v) { Arg a; a.u = v; return a; }
Arg lit(int v) { return lit(uint32_t(v)); }
Arg real(float v) { uint32_t u; std::memcpy(&u, &v, 4); return lit(u); }
Arg str(const std::string &s) { Arg a; a.kind = Arg::Kind::String; a.str = s; return a; }
Arg pronoun(uint16_t prn) { return lit(uint32_t(kTagPronoun) << 16 | prn); }
Arg node(std::unique_ptr<Node> n) { Arg a; a.kind = Arg::Kind::Node; a.node = std::move(n); return a; }

std::unique_ptr<Node> make(uint32_t id, std::vector<Arg> args) {
    auto n = std::make_unique<Node>();
    n->id = id;
    n->args = std::move(args);
    n->rawCount = uint16_t(n->args.size());
    return n;
}

static void appendNodes(std::vector<Arg> &a, std::vector<std::unique_ptr<Node>> &v) {
    for (auto &n : v) a.push_back(node(std::move(n)));
}

std::unique_ptr<Node> behavior(int type, const std::string &name, std::vector<std::unique_ptr<Node>> items) {
    std::vector<Arg> a;
    a.push_back(lit(type));
    a.push_back(str(name));
    appendNodes(a, items);
    return make(DECLARE_BEHAVIOR, std::move(a));
}

std::unique_ptr<Node> globals(std::vector<std::unique_ptr<Node>> interactors) {
    std::vector<Arg> a;
    appendNodes(a, interactors);
    return make(GLOBAL_INTERACTORS, std::move(a));
}

static std::unique_ptr<Node> interactor(uint32_t id, std::vector<Arg> head, std::vector<std::unique_ptr<Node>> &opts,
                                        std::vector<std::unique_ptr<Node>> &actions) {
    appendNodes(head, opts);
    if (!actions.empty()) {
        std::vector<Arg> en;
        appendNodes(en, actions);
        head.push_back(node(make(ENABLE, std::move(en))));
    }
    return make(id, std::move(head));
}

std::unique_ptr<Node> onMessage(int msg, std::vector<std::unique_ptr<Node>> opts,
                                std::vector<std::unique_ptr<Node>> actions, uint32_t variant) {
    return interactor(SET_MESSAGE_INTERACTOR | variant, args(lit(msg)), opts, actions);
}

std::unique_ptr<Node> onCollision(uint32_t mine, uint32_t other, std::vector<std::unique_ptr<Node>> opts,
                                  std::vector<std::unique_ptr<Node>> actions, uint32_t variant) {
    return interactor(SET_COLLISION_INTERACTOR | variant, args(lit(mine), lit(other)), opts, actions);
}

std::unique_ptr<Node> action(int acf, std::vector<Arg> a, uint32_t variant) {
    a.insert(a.begin(), lit(acf));
    return make(DO_ACTION | variant, std::move(a));
}

std::unique_ptr<Node> decide(int dcf, std::vector<std::unique_ptr<Node>> branches) {
    std::vector<Arg> a;
    a.push_back(lit(dcf));
    appendNodes(a, branches);
    return make(DECIDE_BY_AMONG, std::move(a));
}

std::unique_ptr<Node> decideWith(int dcf, std::vector<Arg> data, std::vector<std::unique_ptr<Node>> branches) {
    std::vector<Arg> a;
    a.push_back(lit(dcf));
    a.push_back(node(make(DATA_BLOCK, std::move(data))));
    appendNodes(a, branches);
    return make(DECIDE_BY_WITH_AMONG, std::move(a));
}

std::unique_ptr<Node> block(std::vector<std::unique_ptr<Node>> stmts) {
    std::vector<Arg> a;
    appendNodes(a, stmts);
    return make(BLOCK, std::move(a));
}

std::unique_ptr<Node> changeTo(uint32_t behIndex) { return make(CHANGE_TO_BEHAVIOR, args(lit(behIndex))); }
std::unique_ptr<Node> changePlan(const std::string &plan, int behIndex) {
    return make(CHANGE_OF_PLAN, args(str(plan), lit(behIndex)));
}
std::unique_ptr<Node> behaviorParam(int setId, std::vector<Arg> a) {
    a.insert(a.begin(), lit(setId));
    return make(SET_BEHAVIOR_PARAM, std::move(a));
}
std::unique_ptr<Node> interactorParam(int setId, std::vector<Arg> a) {
    a.insert(a.begin(), lit(setId));
    return make(SET_INTERACTOR_PARAM, std::move(a));
}
std::unique_ptr<Node> addAgenda(int agd, std::vector<Arg> a) {
    a.insert(a.begin(), lit(agd));
    return make(ADD_AGENDA_ITEM, std::move(a));
}
std::unique_ptr<Node> removeAgenda(int agd) { return make(REMOVE_AGENDA_ITEM, args(lit(agd))); }
std::unique_ptr<Node> timeout(int32_t delay, int msg, Arg recipient, std::optional<Arg> data) {
    std::vector<Arg> a = args(lit(delay), lit(msg), std::move(recipient));
    if (data) a.push_back(std::move(*data));
    return make(SET_TIMEOUTMSG, std::move(a));
}
std::unique_ptr<Node> localReals(int n) {
    std::vector<Arg> a;
    for (int i = 0; i < n; ++i) a.push_back(lit(0));
    return make(DECLARE_LOCAL_REALS, std::move(a));
}
std::unique_ptr<Node> endPlan() { return make(END_PLAN, {}); }

} // namespace build
} // namespace adlib
